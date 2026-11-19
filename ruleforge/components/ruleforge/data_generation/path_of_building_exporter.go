package data_generation

import (
 "context"
	"fmt"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/data_generation/model"
	lua "github.com/yuin/gopher-lua"
	"log"
	"path/filepath"
	"strings"
	"time"
)

// PathOfBuildingExporter is the main public-facing struct for this package.
type PathOfBuildingExporter struct {
	luaExecutor    *LuaExecutor
	economyScraper *PoeNinjaClient
	dropLevels     *dropLevelClient
	baseTypeCache  *ItemCacheModel
	historyStore   *EconomyHistoryStore
	cacheRepo      *CacheRepository
}

// NewPathOfBuildingExporter opens the item cache at cacheDir and the economy
// history database at economyHistoryDBPath, creating either if missing.
func NewPathOfBuildingExporter(cacheDir string, economyHistoryDBPath string) (*PathOfBuildingExporter, error) {
	cacheRepository := NewCacheRepository(filepath.Join(cacheDir, "basetypes-standard-only.json"))
	baseTypeCache, err := cacheRepository.LoadCache()
	if err != nil {
		log.Printf("WARN: Could not load baseTypeCache: %v. Will regenerate data.", err)
	}

	historyStore, err := NewEconomyHistoryStore(economyHistoryDBPath)
	if err != nil {
		return nil, fmt.Errorf("opening economy history database: %w", err)
	}

	return &PathOfBuildingExporter{
		luaExecutor:    NewLuaExecutor(),
		economyScraper: NewPoeNinjaClient(),
		dropLevels:     newDropLevelClient(),
		baseTypeCache:  baseTypeCache,
		cacheRepo:      cacheRepository,
		historyStore:   historyStore,
	}, nil
}

// Close releases resources held by the exporter, such as the economy history
// database handle.
func (e *PathOfBuildingExporter) Close() error {
	if e.historyStore == nil {
		return nil
	}
	return e.historyStore.Close()
}

// CachedUniques returns the previously parsed standard/upcoming unique-item set.
// The low-memory cache is separate from caches containing generated uniques.
func (e *PathOfBuildingExporter) CachedUniques() ([]model.Unique, bool) {
	if e.baseTypeCache == nil {
		return nil, false
	}
	return e.baseTypeCache.Uniques, true
}

// InvalidateItemCache ensures a new PoB snapshot cannot reuse parsed data
// from the previous source revision.
func (e *PathOfBuildingExporter) InvalidateItemCache() { e.baseTypeCache = nil }

func (e *PathOfBuildingExporter) LoadItemBases(luaFilePaths []string) ([]model.ItemBase, error) {
	if e.baseTypeCache != nil {
		return (*e.baseTypeCache).Items, nil
	}

	var allBases []model.ItemBase
	for _, luaFile := range luaFilePaths {
		dataTable, err := e.luaExecutor.ExecuteScriptAsFunc(luaFile)
		if err != nil {
			return nil, err
		}

		bases := mapItemBasesFromLua(dataTable)

		allBases = append(allBases, bases...)
		log.Printf("Successfully converted %d item base models from %s\n", len(bases), filepath.Base(luaFile))
	}

	// Without any bases every build-dependent rule (@class_use, progression)
	// compiles to empty lists, and the result would be cached for two weeks.
	if len(allBases) == 0 {
		return nil, fmt.Errorf("no item bases were read from %d PoB base file(s); the Path of Building data format may have changed", len(luaFilePaths))
	}

	if err := enrichItemBasesWithDropLevels(allBases, e.dropLevels); err != nil {
		return nil, err
	}
	return allBases, nil
}

func (e *PathOfBuildingExporter) LoadEssences(luaFilePath string) ([]model.Essence, error) {
	if e.baseTypeCache != nil {
		return (*e.baseTypeCache).Essences, nil
	}

	dataTable, err := e.luaExecutor.ExecuteScriptWithReturn(luaFilePath)
	if err != nil {
		return nil, err
	}

	// Delegate the mapping logic
	models := mapEssencesFromLua(dataTable)

	log.Printf("Successfully converted %d essence models from %s\n", len(models), filepath.Base(luaFilePath))
	return models, nil
}

func (e *PathOfBuildingExporter) LoadGems(luaFilePath string) ([]model.Gem, error) {
	if e.baseTypeCache != nil {
		return (*e.baseTypeCache).Gems, nil
	}

	dataTable, err := e.luaExecutor.ExecuteScriptWithReturn(luaFilePath)
	if err != nil {
		return nil, err
	}

	// Delegate the mapping logic
	models := mapGemsFromLua(dataTable)

	log.Printf("Successfully converted %d gem models from %s\n", len(models), filepath.Base(luaFilePath))
	return models, nil
}

func (e *PathOfBuildingExporter) LoadUniques(luaFilePaths []string) ([]model.Unique, error) {
	if e.baseTypeCache != nil {
		return e.baseTypeCache.Uniques, nil
	}

	var allBases []model.Unique
	for _, luaFile := range luaFilePaths {
		uniqueBasesFile, err := e.loadUniqueItems(luaFile)

		if err != nil {
			return nil, err
		}

		allBases = append(allBases, uniqueBasesFile...)
		log.Printf("Successfully converted and enriched %d item base models from %s\n", len(uniqueBasesFile), filepath.Base(luaFile))
	}

	return allBases, nil
}

// loadUniqueItems parses a Lua file containing an array of unique item strings.
func (e *PathOfBuildingExporter) loadUniqueItems(luaFilePath string) ([]model.Unique, error) {
	// Execute the script to get the table (array) of strings.
	dataTable, err := e.luaExecutor.ExecuteScriptWithReturn(luaFilePath)
	if err != nil {
		return nil, err
	}

	var models []model.Unique

	dataTable.ForEach(func(key lua.LValue, value lua.LValue) {
		itemString, ok := value.(lua.LString)
		if !ok {
			log.Printf("WARN: Value at key '%s' is not a string, skipping.", key.String())
			return
		}

		parsedItem := parseUniqueItemString(string(itemString))
		if parsedItem != nil {
			models = append(models, *parsedItem)
		}
	})

	log.Printf("Successfully converted %d unique item models from %s\n", len(models), filepath.Base(luaFilePath))
	return models, nil
}

// LoadUpcomingUniqueItems parses a Lua file that assigns uniques to a global variable path.
// This handles files that use the format `data.uniques.new = { ... }`.
func (e *PathOfBuildingExporter) LoadUpcomingUniqueItems(luaFilePath string) ([]model.Unique, error) {
	dataTable, err := e.luaExecutor.ExecuteAndGetNestedGlobal(luaFilePath, "data", "uniques", "new")
	if err != nil {
		return nil, fmt.Errorf("failed to execute and get global table for %s: %w", luaFilePath, err)
	}

	if dataTable == nil {
		return nil, fmt.Errorf("table 'data.uniques.new' not found in Lua script: %s", luaFilePath)
	}

	var models []model.Unique

	// The rest of the parsing logic is identical to LoadUniqueItems
	dataTable.ForEach(func(key lua.LValue, value lua.LValue) {
		itemString, ok := value.(lua.LString)
		if !ok {
			log.Printf("WARN: Value at key '%s' is not a string, skipping.", key.String())
			return
		}

		parsedItem := parseUniqueItemString(string(itemString))
		if parsedItem != nil {
			models = append(models, *parsedItem)
		}
	})

	log.Printf("Successfully converted %d upcoming unique item models from %s\n", len(models), filepath.Base(luaFilePath))
	return models, nil
}

// LoadGeneratedUniques executes Generated.lua with its source dependencies.
func (e *PathOfBuildingExporter) LoadGeneratedUniques(luaFilePath string) ([]model.Unique, error) {
	log.Printf("Loading generated uniques from %s (selective PoB modifier loading)", filepath.Base(luaFilePath))
	started := time.Now()
	source, err := e.luaExecutor.ExecuteGeneratedUniques(luaFilePath)
	if err != nil {
		return nil, fmt.Errorf("load generated uniques from %s: %w", luaFilePath, err)
	}
	result := make([]model.Unique, 0, len(source))
	for index, text := range source {
		item := parseUniqueItemString(text)
		if item == nil {
			return nil, fmt.Errorf("invalid generated unique at index %d in %s", index+1, luaFilePath)
		}
		result = append(result, *item)
	}
	log.Printf("Successfully converted %d generated unique item models from %s in %s", len(result), filepath.Base(luaFilePath), time.Since(started).Round(time.Millisecond))
	return result, nil
}

// economyEndpoint selects which poe.ninja API shape a category is fetched
// through: "stash" for individually stash-listed items (chaosValue/listingCount
// per line), "exchange" for items only traded through the bulk exchange
// (a value/volume pair, with display names in a separate lookup - see
// PoeNinjaClient.FetchExchangeData).
type economyEndpoint string

const (
	stashEndpoint    economyEndpoint = "stash"
	exchangeEndpoint economyEndpoint = "exchange"
)

var economyCategories = []EconomyCategory{
 {Kind:"UniqueWeapon",Class:"Uniques",Endpoint:"stash"},
 {Kind:"UniqueArmour",Class:"Uniques",Endpoint:"stash"},
 {Kind:"UniqueAccessory",Class:"Uniques",Endpoint:"stash"},
 {Kind:"UniqueFlask",Class:"Uniques",Endpoint:"stash"},
 {Kind:"UniqueJewel",Class:"Uniques",Endpoint:"stash"},
 {Kind:"SkillGem",Class:"Gems",Endpoint:"stash"},
 {Kind:"DivinationCard",Class:"Divination Cards",Endpoint:"exchange"},
 {Kind:"Tattoo",Class:"Tattoos",Endpoint:"exchange"},
 {Kind:"Currency",Class:"Currency",Endpoint:"exchange"},
 {Kind:"DeliriumOrb",Class:"Currency",Endpoint:"exchange"},
 {Kind:"Omen",Class:"Currency",Endpoint:"exchange"},
}

// RefreshEconomyData fetches a fresh poe.ninja snapshot and records it in the
// economy history database for any configured league whose latest recorded
// snapshot is older than maxAge (or unconditionally, if forceRegenerate is set).
// Leagues with a recent-enough snapshot are left untouched.
func (e *PathOfBuildingExporter) RefreshEconomyData(leagues []string, maxAge time.Duration, force bool) error {
 return CollectEconomy(context.Background(), e.economyScraper, e.historyStore, leagues, economyCategories, maxAge, force, CurrentEconomyVersion)
}

// HistoryStore is shared by local collection and remote synchronization.
func (e *PathOfBuildingExporter) HistoryStore() *EconomyHistoryStore { return e.historyStore }

// LeaguesWithHistory returns the subset of leagues that have at least one
// recorded economy snapshot, in the order given, and warns about the rest.
// Leagues without history (not started yet, ended before ever being recorded,
// or misspelled) must not be passed to EconomyHistory or weighted in tiering.
func (e *PathOfBuildingExporter) LeaguesWithHistory(leagues []string) ([]string, error) {
	available := make([]string, 0, len(leagues))
	for _, league := range leagues {
		_, ok, err := e.historyStore.LatestSnapshotAt(league)
		if err != nil {
			return nil, fmt.Errorf("checking economy history for league %q: %w", league, err)
		}
		if !ok {
			log.Printf("WARNING: no economy data for league %q; its weight is redistributed to the other leagues.", league)
			continue
		}
		available = append(available, league)
	}
	if len(available) == 0 {
		return nil, fmt.Errorf("no economy data for any configured league (%s); check LeagueWeights in config.toml", strings.Join(leagues, ", "))
	}
	return available, nil
}

// EconomyHistory returns every economy snapshot ever recorded for each
// configured league, oldest first. Nothing is ever pruned, so tiering (and
// its trend signal) naturally gets more accurate and covers more time as
// history accumulates. It errors if a league has no history yet; call
// RefreshEconomyData first.
func (e *PathOfBuildingExporter) EconomyHistory(leaguesToRetrieve []string) (map[string][]EconomySnapshot, error) {
	result := make(map[string][]EconomySnapshot, len(leaguesToRetrieve))
	total := 0
	for _, league := range leaguesToRetrieve {
		snapshots, err := e.historyStore.Window(league, time.Time{})
		if err != nil {
			return nil, fmt.Errorf("loading economy history for league %q: %w", league, err)
		}
		if len(snapshots) == 0 {
			return nil, fmt.Errorf("no economy snapshots recorded yet for league %q; refresh economy data first", league)
		}
		result[league] = snapshots
		total += len(snapshots)
	}
	log.Printf("Loaded %d economy snapshot(s) across %d league(s)", total, len(result))
	return result, nil
}

// SaveItemCache saves the item, essence, gem, and unique data.
// It checks the item cache's expiry date and only saves if the cache is missing or expired.
func (e *PathOfBuildingExporter) SaveItemCache(items []model.ItemBase, essences []model.Essence, gems []model.Gem, uniques []model.Unique) error {
	if e.baseTypeCache != nil && time.Now().Before(e.baseTypeCache.ExpiryDate) {
		log.Println("Item cache is still valid. Skipping save.")
		return nil
	}

	log.Println("Item cache is expired or missing. Saving new item cache...")
	return e.cacheRepo.SaveItemCache(items, essences, gems, uniques)
}

// GetBaseTypes is a generic utility function that can live here or in models.go
func GetBaseTypes[T model.POBDataType](data []T) []string {
	basetypes := make([]string, 0)
	for _, dataItem := range data {
		baseType := dataItem.GetBaseType()
		if strings.Contains(baseType, "Energy Blade") || strings.Contains(baseType, "Random") {
			continue
		}
		if strings.Contains(dataItem.GetVariantID(), "Alt") {
			continue
		}

		basetypes = append(basetypes, baseType)
	}
	return basetypes
}
