package data_generation

import (
	"encoding/json"
	"fmt"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/files"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/data_generation/model"
	"os"
	"path/filepath"
	"time"
)

type ItemCacheModel struct {
	ExpiryDate time.Time        `json:"expiry_date"`
	Items      []model.ItemBase `json:"items"`
	Essences   []model.Essence  `json:"essences"`
	Gems       []model.Gem      `json:"gems"`
	Uniques    []model.Unique   `json:"uniques"`
}

type EconomyCacheItem struct {
	Name         string  `json:"name"`
	BaseType     string  `json:"base_type"`
	Count        int     `json:"count"`
	ListingCount int     `json:"listing_count"`
	ChaosValue   float64 `json:"chaos_value"`
	DivineValue  float64 `json:"divine_value"`
	ExaltedValue float64 `json:"exalted_value"`
}

type EconomyCacheModel struct {
	ExpiryDate        time.Time                     `json:"expiry_date"`
	EconomyCacheItems map[string][]EconomyCacheItem `json:"items"`
}

type CacheRepository struct {
	baseTypeCachePath   string
	economyCacheRawPath string
}

func NewCacheRepository(baseTypeCachePath, economyCachePath string) *CacheRepository {
	return &CacheRepository{
		baseTypeCachePath:   baseTypeCachePath,
		economyCacheRawPath: economyCachePath,
	}
}

// LoadCache reads both the item and economy cache files and unmarshals them.
// It returns a nil value for any cache that doesn't exist or has expired,
// without returning an error. An error is only returned for actual file I/O or JSON parsing issues.
func (c *CacheRepository) LoadCache() (*ItemCacheModel, *EconomyCacheModel, error) {
	var itemCache *ItemCacheModel
	var economyCache *EconomyCacheModel
	var err error

	// 1. Attempt to load the item base type cache
	itemCacheExists, err := files.Exists(c.baseTypeCachePath)
	if err != nil {
		return nil, nil, fmt.Errorf("error checking for item cache: %w", err)
	}
	if itemCacheExists {
		data, err := os.ReadFile(c.baseTypeCachePath)
		if err != nil {
			return nil, nil, err
		}
		var cache ItemCacheModel
		if err := json.Unmarshal(data, &cache); err != nil {
			return nil, nil, err
		}
		// Only use the cache if it's not expired
		if time.Now().Before(cache.ExpiryDate) {
			itemCache = &cache
		}
	}

	// 2. Attempt to load the economy cache
	economyCacheExists, err := files.Exists(c.economyCacheRawPath)
	if err != nil {
		return nil, nil, fmt.Errorf("error checking for economy cache: %w", err)
	}
	if economyCacheExists {
		data, err := os.ReadFile(c.economyCacheRawPath)
		if err != nil {
			return nil, nil, err
		}
		var cache EconomyCacheModel
		if err := json.Unmarshal(data, &cache); err != nil {
			return nil, nil, err
		}
		// Only use the cache if it's not expired
		if time.Now().Before(cache.ExpiryDate) {
			economyCache = &cache
		}
	}

	return itemCache, economyCache, nil
}

// SaveItemCache saves the item, essence, gem, and unique data to the item cache file.
func (c *CacheRepository) SaveItemCache(items []model.ItemBase, essences []model.Essence, gems []model.Gem, uniques []model.Unique) error {
	expiryDate := time.Now().AddDate(0, 0, 14)
	cache := ItemCacheModel{expiryDate, items, essences, gems, uniques}

	dir := filepath.Dir(c.baseTypeCachePath)
	if err := os.MkdirAll(dir, 0755); err != nil {
		return err
	}

	f, err := os.Create(c.baseTypeCachePath)
	if err != nil {
		return err
	}
	defer f.Close()

	enc := json.NewEncoder(f)
	enc.SetIndent("", "  ")
	return enc.Encode(cache)
}

// SaveEconomyCache saves the economy data to the economy cache file.
func (c *CacheRepository) SaveEconomyCache(economyItems map[string][]EconomyCacheItem) error {
	expiryDate := time.Now().Add(24 * time.Hour)
	cache := EconomyCacheModel{
		ExpiryDate:        expiryDate,
		EconomyCacheItems: economyItems,
	}

	dir := filepath.Dir(c.economyCacheRawPath)
	if err := os.MkdirAll(dir, 0755); err != nil {
		return err
	}

	f, err := os.Create(c.economyCacheRawPath)
	if err != nil {
		return err
	}
	defer f.Close()

	enc := json.NewEncoder(f)
	enc.SetIndent("", "  ")
	return enc.Encode(cache)
}
