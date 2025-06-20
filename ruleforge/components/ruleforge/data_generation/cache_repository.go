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

type CacheModel struct {
	ExpiryDate time.Time        `json:"expiry_date"`
	Items      []model.ItemBase `json:"items"`
	Essences   []model.Essence  `json:"essences"`
	Gems       []model.Gem      `json:"gems"`
}

type CacheRepository struct {
	filePath string
}

func NewCacheRepository(path string) *CacheRepository {
	return &CacheRepository{
		filePath: path,
	}
}

// LoadCache reads the named JSON file and unmarshals it into a CacheModel.
func (c *CacheRepository) LoadCache() (*CacheModel, error) {
	ok, err := files.Exists(c.filePath)
	if err != nil || !ok {
		return nil, fmt.Errorf("could not load cache file %s", c.filePath)
	}

	data, err := os.ReadFile(c.filePath)
	if err != nil {
		return nil, err
	}

	// Unmarshal into the model
	var cache CacheModel
	if err := json.Unmarshal(data, &cache); err != nil {
		return nil, err
	}

	currentTime := time.Now()
	if currentTime.After(cache.ExpiryDate) {
		return nil, nil
	}

	return &cache, nil
}

func (c *CacheRepository) SaveCache(items []model.ItemBase, essences []model.Essence, gems []model.Gem) error {
	expiryDate := time.Now().AddDate(0, 0, 14)
	cache := CacheModel{expiryDate, items, essences, gems}

	dir := filepath.Dir(c.filePath)
	if err := os.MkdirAll(dir, 0755); err != nil {
		return err
	}

	f, err := os.Create(c.filePath)
	if err != nil {
		return err
	}
	defer f.Close()

	enc := json.NewEncoder(f)
	enc.SetIndent("", "  ")
	return enc.Encode(cache)
}
