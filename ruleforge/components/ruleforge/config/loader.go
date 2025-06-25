package config

import (
	"encoding/json"
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

	return &cfg, nil
}
