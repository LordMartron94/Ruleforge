package config

type EconomyWeights struct {
	Rarity float64 `json:"Rarity"`
	Value  float64 `json:"Value"`
}

type ConfigurationModel struct {
	// FilterOutputDirs defines the directories where the filters should be outputted to.
	FilterOutputDirs []string `json:"FilterOutputDirs"`

	// RuleforgeInputDir specifies the directory which filter(s) should be processed.
	RuleforgeInputDir string `json:"RuleforgeInputDir"`

	// StyleJSONFile indicates the JSON file where all styles are housed.
	StyleJSONFile string `json:"StyleJSONFile"`

	// PathOfBuildingDataPath is where all Path of Building data is stored.
	PathOfBuildingDataPath string `json:"PathOfBuildingDataPath"`

	// EconomyBasedDataLeagues defines the leagues to be uses for economy-based data.
	EconomyBasedDataLeagues []string `json:"EconomyBasedDataLeagues"`

	EconomyWeights *EconomyWeights `json:"EconomyWeights"`
}
