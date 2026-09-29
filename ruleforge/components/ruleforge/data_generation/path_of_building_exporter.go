package data_generation

import (
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

var economyCategories = []struct {
	kind     string
	class    string
	endpoint economyEndpoint
}{
	{"UniqueWeapon", "Uniques", stashEndpoint},
	{"UniqueArmour", "Uniques", stashEndpoint},
	{"UniqueAccessory", "Uniques", stashEndpoint},
	{"UniqueFlask", "Uniques", stashEndpoint},
	{"UniqueJewel", "Uniques", stashEndpoint},
	{"SkillGem", "Gems", stashEndpoint},
	{"DivinationCard", "Divination Cards", exchangeEndpoint},
	{"Tattoo", "Tattoos", exchangeEndpoint},
}

// RefreshEconomyData fetches a fresh poe.ninja snapshot and records it in the
// economy history database for any configured league whose latest recorded
// snapshot is older than maxAge (or unconditionally, if forceRegenerate is set).
// Leagues with a recent-enough snapshot are left untouched.
func (e *PathOfBuildingExporter) RefreshEconomyData(leaguesToRetrieve []string, maxAge time.Duration, forceRegenerate bool) error {
	now := time.Now()

	// Decide which leagues actually need a fetch before resolving anything, so
	// a fully up-to-date history needs no network access at all.
	staleLeagues := leaguesToRetrieve
	if !forceRegenerate {
		staleLeagues = nil
		for _, configured := range leaguesToRetrieve {
			latest, ok, err := e.historyStore.LatestSnapshotAt(configured)
			if err != nil {
				return fmt.Errorf("checking latest economy snapshot for league %q: %w", configured, err)
			}
			if ok && now.Sub(latest) < maxAge {
				log.Printf("Economy snapshot for %q is %s old (refresh interval %s); skipping refetch.", configured, now.Sub(latest).Round(time.Minute), maxAge)
				continue
			}
			staleLeagues = append(staleLeagues, configured)
		}
	}
	if len(staleLeagues) == 0 {
		return nil
	}

	leagues, err := e.economyScraper.ResolveLeagues(staleLeagues)
	if err != nil {
		return err
	}

	requestCount := 0
	for _, configured := range staleLeagues {
		leagueID := leagues[configured]
		var items []EconomyCacheItem
		for _, category := range economyCategories {
			// Avoid bursts; no delay before the first request.
			if requestCount > 0 && e.economyScraper.requestDelay > 0 {
				time.Sleep(e.economyScraper.requestDelay)
			}
			requestCount++
			var fetched []EconomyCacheItem
			var fetchErr error
			switch category.endpoint {
			case stashEndpoint:
				fetched, fetchErr = e.economyScraper.FetchData("itemoverview", category.kind, leagueID, category.class)
			case exchangeEndpoint:
				fetched, fetchErr = e.economyScraper.FetchExchangeData(category.kind, leagueID, category.class)
			default:
				fetchErr = fmt.Errorf("unknown economy endpoint %q", category.endpoint)
			}
			if fetchErr != nil {
				return fmt.Errorf("fetch poe.ninja league %q (id %q), category %q: %w", configured, leagueID, category.kind, fetchErr)
			}
			log.Printf("Fetched %d %s records for %s", len(fetched), category.kind, configured)
			items = append(items, fetched...)
		}
		if len(items) == 0 {
			return fmt.Errorf("no item prices returned for league %q (id %q); refusing to record an empty snapshot", configured, leagueID)
		}

		if err := e.historyStore.RecordSnapshot(configured, now, items); err != nil {
			return fmt.Errorf("recording economy snapshot for league %q: %w", configured, err)
		}
		log.Printf("Recorded economy snapshot for %q: %d items", configured, len(items))
	}
	return nil
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
