package data_generation

import (
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/data_generation/model"
	"log"
	"path/filepath"
	"strings"
	"time"
)

// PathOfBuildingExporter is the main public-facing struct for this package.
type PathOfBuildingExporter struct {
	luaExecutor *LuaExecutor
	cache       *CacheModel
	cacheRepo   *CacheRepository
}

func NewPathOfBuildingExporter() *PathOfBuildingExporter {
	cacheRepository := NewCacheRepository("./cache/basetypes.json")
	cache, err := cacheRepository.LoadCache()

	if err != nil {
		log.Printf("WARN: Could not load cache: %v. Will regenerate data.", err)
	}

	return &PathOfBuildingExporter{
		luaExecutor: NewLuaExecutor(),
		cache:       cache,
		cacheRepo:   cacheRepository,
	}
}

func (e *PathOfBuildingExporter) LoadItemBases(luaFilePaths []string) ([]model.ItemBase, error) {
	if e.cache != nil {
		return (*e.cache).Items, nil
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
	if e.cache != nil {
		return (*e.cache).Essences, nil
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
	if e.cache != nil {
		return (*e.cache).Gems, nil
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

func (e *PathOfBuildingExporter) SaveCache(items []model.ItemBase, essences []model.Essence, gems []model.Gem) error {
	currentTime := time.Now()
	if e.cache != nil && !currentTime.After(e.cache.ExpiryDate) {
		return nil
	}
	return e.cacheRepo.SaveCache(items, essences, gems)
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
