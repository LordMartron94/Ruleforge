package config

import (
	"encoding/json"
	"fmt"
	"os"
)

// ConfigurationLoader is responsible for loading ConfigurationModel from disk.
type ConfigurationLoader struct {
}

// NewConfigurationLoader creates a new loader.
func NewConfigurationLoader() *ConfigurationLoader {
	return &ConfigurationLoader{}
}

// LoadConfiguration reads the named JSON file and unmarshals it into a ConfigurationModel.
func (c *ConfigurationLoader) LoadConfiguration(file string) (*ConfigurationModel, error) {
	// Read the entire file’s contents
	data, err := os.ReadFile(file)
	if err != nil {
		return nil, err
	}

	// Unmarshal into the model
	var cfg ConfigurationModel
	if err := json.Unmarshal(data, &cfg); err != nil {
		return nil, err
	}

	base, err := configurationBaseDir(file)
	if err != nil {
		return nil, err
	}
	resolve := func(value string) string { return resolveConfigurationPath(base, value) }
	cfg.RuleforgeInputDir = resolve(cfg.RuleforgeInputDir)
	cfg.StyleJSONFile = resolve(cfg.StyleJSONFile)
	cfg.StyleColorCSSFile = resolve(cfg.StyleColorCSSFile)
	cfg.BaseTypeCSVFile = resolve(cfg.BaseTypeCSVFile)
	if cfg.PathOfBuildingDataPath == "" {
		cfg.PathOfBuildingDataPath = ".ruleforge-cache/pob"
	}
	cfg.PathOfBuildingDataPath = resolve(cfg.PathOfBuildingDataPath)
	if cfg.EconomyHistoryDBPath == "" {
		cfg.EconomyHistoryDBPath = ".ruleforge-cache/economy_history.db"
	}
	cfg.EconomyHistoryDBPath = resolve(cfg.EconomyHistoryDBPath)
	if cfg.EconomyHistoryRefreshHours <= 0 {
		cfg.EconomyHistoryRefreshHours = 24
	}
	if cfg.EconomyValueHalfLifeDays <= 0 {
		cfg.EconomyValueHalfLifeDays = 14
	}
	for i, directory := range cfg.FilterOutputDirs {
		cfg.FilterOutputDirs[i] = resolve(directory)
	}
	fmt.Println(cfg.String())

	return &cfg, nil
}
