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

// Version 4 rebuilds caches whose per-item wiki requests silently failed.
const itemCacheSchemaVersion = 4

type ItemCacheModel struct {
	Version    int              `json:"version"`
	ExpiryDate time.Time        `json:"expiry_date"`
	Items      []model.ItemBase `json:"items"`
	Essences   []model.Essence  `json:"essences"`
	Gems       []model.Gem      `json:"gems"`
	Uniques    []model.Unique   `json:"uniques"`
}

// EconomyCacheItem is one poe.ninja price line for a base type. It is the row
// shape persisted per snapshot in EconomyHistoryStore.
type EconomyCacheItem struct {
	Class        string  `json:"class"`
	Name         string  `json:"name"`
	BaseType     string  `json:"base_type"`
	Count        int     `json:"count"`
	ListingCount int     `json:"listing_count"`
	ChaosValue   float64 `json:"chaos_value"`
	DivineValue  float64 `json:"divine_value"`
	ExaltedValue float64 `json:"exalted_value"`
}

// CacheRepository persists the item base-type cache. Economy data has its own
// history in EconomyHistoryStore, since a single JSON snapshot can't represent
// a time series.
type CacheRepository struct {
	baseTypeCachePath string
}

func NewCacheRepository(baseTypeCachePath string) *CacheRepository {
	return &CacheRepository{
		baseTypeCachePath: baseTypeCachePath,
	}
}

// LoadCache reads the item cache file and unmarshals it. It returns a nil
// value if the cache doesn't exist or has expired, without returning an error.
// An error is only returned for actual file I/O or JSON parsing issues.
func (c *CacheRepository) LoadCache() (*ItemCacheModel, error) {
	itemCacheExists, err := files.Exists(c.baseTypeCachePath)
	if err != nil {
		return nil, fmt.Errorf("error checking for item cache: %w", err)
	}
	if !itemCacheExists {
		return nil, nil
	}

	data, err := os.ReadFile(c.baseTypeCachePath)
	if err != nil {
		return nil, err
	}
	var cache ItemCacheModel
	if err := json.Unmarshal(data, &cache); err != nil {
		return nil, err
	}
	// Only use the cache if it's not expired
	if cache.Version != itemCacheSchemaVersion || !time.Now().Before(cache.ExpiryDate) {
		return nil, nil
	}
	return &cache, nil
}

// SaveItemCache saves the item, essence, gem, and unique data to the item cache file.
func (c *CacheRepository) SaveItemCache(items []model.ItemBase, essences []model.Essence, gems []model.Gem, uniques []model.Unique) error {
	expiryDate := time.Now().AddDate(0, 0, 14)
	cache := ItemCacheModel{
		Version:    itemCacheSchemaVersion,
		ExpiryDate: expiryDate,
		Items:      items,
		Essences:   essences,
		Gems:       gems,
		Uniques:    uniques,
	}

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
