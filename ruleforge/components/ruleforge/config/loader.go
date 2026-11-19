package config

import (
	"fmt"
	"sort"
	"strings"

	"github.com/BurntSushi/toml"
)

// ConfigurationLoader is responsible for loading ConfigurationModel from disk.
type ConfigurationLoader struct {
}

// NewConfigurationLoader creates a new loader.
func NewConfigurationLoader() *ConfigurationLoader {
	return &ConfigurationLoader{}
}

// LoadConfiguration reads the named TOML file and decodes it into a ConfigurationModel.
// Keys the model does not know are rejected, so a typo (or a setting from the old
// JSON configuration) fails loudly instead of being silently ignored.
func (c *ConfigurationLoader) LoadConfiguration(file string) (*ConfigurationModel, error) {
	cfg := ConfigurationModel{CurrencyTiering: &CurrencyTiering{CraftingWeight: 0.4}, EconomyMemory: DefaultEconomyMemory(), EconomyHistoryTokenEnv: "RULEFORGE_HISTORY_TOKEN"}
	metadata, err := toml.DecodeFile(file, &cfg)
	if err != nil {
		return nil, err
	}
	if err := rejectUndecodedKeys(metadata, file); err != nil {
		return nil, err
	}

	base, err := configurationBaseDir(file)
	if err != nil {
		return nil, err
	}
	resolve := func(value string) string { return resolveConfigurationPath(base, value) }
	cfg.RuleforgeInputDir = resolve(cfg.RuleforgeInputDir)
	cfg.StylesDir = resolve(cfg.StylesDir)
	cfg.StudioFile = resolve(cfg.StudioFile)
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
	if cfg.CurrencyTiering.Profiles == "" {
		cfg.CurrencyTiering.Profiles = "configuration/currency/currency_profiles.csv"
	}
	cfg.CurrencyTiering.Profiles = resolve(cfg.CurrencyTiering.Profiles)
	fmt.Println(cfg.String())

	return &cfg, nil
}

func rejectUndecodedKeys(metadata toml.MetaData, file string) error {
	undecoded := metadata.Undecoded()
	if len(undecoded) == 0 {
		return nil
	}
	keys := make([]string, len(undecoded))
	for i, key := range undecoded {
		keys[i] = key.String()
	}
	sort.Strings(keys)
	return fmt.Errorf("%s: unknown setting(s): %s", file, strings.Join(keys, ", "))
}
