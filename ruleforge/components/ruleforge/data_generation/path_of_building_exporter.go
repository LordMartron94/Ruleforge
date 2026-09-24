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
	baseTypeCache  *ItemCacheModel
	economyCache   *EconomyCacheModel
	cacheRepo      *CacheRepository
}

func NewPathOfBuildingExporter(cacheDir string) *PathOfBuildingExporter {
	cacheRepository := NewCacheRepository(filepath.Join(cacheDir, "basetypes-standard-only.json"), filepath.Join(cacheDir, "economy_cache.json"))
	baseTypeCache, economyCache, err := cacheRepository.LoadCache()

	if err != nil {
		log.Printf("WARN: Could not load baseTypeCache: %v. Will regenerate data.", err)
	}

	return &PathOfBuildingExporter{
		luaExecutor:    NewLuaExecutor(),
		economyScraper: NewPoeNinjaClient(),
		baseTypeCache:  baseTypeCache,
		cacheRepo:      cacheRepository,
		economyCache:   economyCache,
	}
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
		enrichItemBasesWithDropLevels(bases)

		allBases = append(allBases, bases...)
		log.Printf("Successfully converted and enriched %d item base models from %s\n", len(bases), filepath.Base(luaFile))
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

func (e *PathOfBuildingExporter) GetEconomyData(leaguesToRetrieve []string, forceRegenerate bool) (map[string][]EconomyCacheItem, error) {
	if !forceRegenerate && e.economyCache != nil && completeEconomyData(e.economyCache.EconomyCacheItems, leaguesToRetrieve) {
		return e.economyCache.EconomyCacheItems, nil
	}

	leagues, err := e.economyScraper.ResolveLeagues(leaguesToRetrieve)
	if err != nil {
		return nil, err
	}

	categories := []struct{ kind, class string }{
		{"UniqueWeapon", "Uniques"},
		{"UniqueArmour", "Uniques"},
		{"UniqueAccessory", "Uniques"},
		{"UniqueFlask", "Uniques"},
		{"UniqueJewel", "Uniques"},
		{"SkillGem", "Gems"},
	}
	all := make(map[string][]EconomyCacheItem, len(leaguesToRetrieve))
	total := 0
	requestCount := 0
	for _, configured := range leaguesToRetrieve {
		leagueID := leagues[configured]
		var items []EconomyCacheItem
		for _, category := range categories {
			// Avoid bursts; no delay before the first request.
			if requestCount > 0 && e.economyScraper.requestDelay > 0 {
				time.Sleep(e.economyScraper.requestDelay)
			}
			requestCount++
			fetched, fetchErr := e.economyScraper.FetchData("itemoverview", category.kind, leagueID, category.class)
			if fetchErr != nil {
				return nil, fmt.Errorf("fetch poe.ninja league %q (id %q), category %q: %w", configured, leagueID, category.kind, fetchErr)
			}
			log.Printf("Fetched %d %s records for %s", len(fetched), category.kind, configured)
			items = append(items, fetched...)
		}
		if len(items) == 0 {
			return nil, fmt.Errorf("no item prices returned for league %q (id %q); refusing to cache an empty economy", configured, leagueID)
		}
		all[configured] = items
		total += len(items)
	}
	log.Printf("Fetched %d economy items across %d configured leagues", total, len(all))
	return all, nil
}

func completeEconomyData(data map[string][]EconomyCacheItem, configured []string) bool {
	if len(configured) == 0 {
		return false
	}
	for _, league := range configured {
		if len(data[league]) == 0 {
			return false
		}
	}
	return true
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

// SaveEconomyCache saves the economy data.
// It checks the economy cache's expiry date and only saves if the cache is missing or expired.
func economyLeagues(economy map[string][]EconomyCacheItem) []string {
	names := make([]string, 0, len(economy))
	for name := range economy {
		names = append(names, name)
	}
	return names
}

func (e *PathOfBuildingExporter) SaveEconomyCache(economy map[string][]EconomyCacheItem, forceSave bool) error {
	if !forceSave && e.economyCache != nil && completeEconomyData(e.economyCache.EconomyCacheItems, economyLeagues(economy)) && time.Now().Before(e.economyCache.ExpiryDate) {
		log.Println("Economy cache is still valid. Skipping save.")
		return nil
	}

	log.Println("Economy cache is expired or missing. Saving new economy cache...")
	return e.cacheRepo.SaveEconomyCache(economy)
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
