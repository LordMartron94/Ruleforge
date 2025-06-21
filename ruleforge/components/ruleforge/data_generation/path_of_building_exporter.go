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

func NewPathOfBuildingExporter() *PathOfBuildingExporter {
	cacheRepository := NewCacheRepository("./cache/basetypes.json", "./cache/economy_cache.json")
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

// LoadGeneratedUniques handles the `generated.lua` file. It executes the script to populate
// the initial `data.uniques.generated` table and parses the resulting items.
// Note: This does NOT call functions that require external data (like `buildTreeDependentUniques`).
func (e *PathOfBuildingExporter) LoadGeneratedUniques(luaFilePath string) ([]model.Unique, error) {
	dataTable, err := e.luaExecutor.ExecuteAndGetNestedGlobal(luaFilePath, "data", "uniques", "generated")
	if err != nil {
		return nil, fmt.Errorf("failed to load generated uniques from %s: %w", luaFilePath, err)
	}

	if dataTable == nil {
		return nil, fmt.Errorf("table 'data.uniques.generated' not found in script: %s", luaFilePath)
	}

	var models []model.Unique

	dataTable.ForEach(func(key lua.LValue, value lua.LValue) {
		itemString, ok := value.(lua.LString)
		if !ok {
			log.Printf("WARN: Value in generated uniques at key '%s' is not a string, skipping.", key.String())
			return
		}

		parsedItem := parseUniqueItemString(string(itemString))
		if parsedItem != nil {
			models = append(models, *parsedItem)
		}
	})

	log.Printf("Successfully converted %d generated unique item models from %s\n", len(models), filepath.Base(luaFilePath))
	return models, nil
}

func (e *PathOfBuildingExporter) GetEconomyData(leaguesToRetrieve []string) (map[string][]EconomyCacheItem, error) {
	if e.economyCache != nil {
		return (*e.economyCache).EconomyCacheItems, nil
	}

	allEconomyData := make(map[string][]EconomyCacheItem)

	categories := map[string]map[string][]string{
		"itemoverview": {
			"Uniques": []string{
				"UniqueWeapon",
				"UniqueArmour",
				"UniqueAccessory",
				"UniqueFlask",
				"UniqueJewel",
			},
			"Gems": []string{
				"SkillGem",
			},
		},
	}

	fetched := 0

	for _, league := range leaguesToRetrieve {
		var leagueEconomyData []EconomyCacheItem

		for endpoint, classes := range categories {
			for class, types := range classes {
				for _, itemType := range types {
					items, err := e.economyScraper.FetchData(endpoint, itemType, league, class)
					if err != nil {
						log.Printf("ERROR: Could not fetch data for type '%s': %v", itemType, err)
						continue
					}
					leagueEconomyData = append(leagueEconomyData, items...)
					fetched += len(items)
					log.Printf("Successfully fetched %d items for type '%s'", len(items), itemType)
					time.Sleep(1 * time.Second)
				}
			}
		}

		allEconomyData[league] = leagueEconomyData
	}

	log.Printf("-----------------------------------------")
	log.Printf("Total economy items fetched (across leagues): %d", fetched)
	log.Println("Economy data fetching complete.")

	return allEconomyData, nil
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
func (e *PathOfBuildingExporter) SaveEconomyCache(economy map[string][]EconomyCacheItem) error {
	if e.economyCache != nil && time.Now().Before(e.economyCache.ExpiryDate) {
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
		basetypes = append(basetypes, baseType)
	}
	return basetypes
}
