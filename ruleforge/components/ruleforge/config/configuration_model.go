package config

type ConfigurationModel struct {
	// FilterOutputDirs defines the directories where the filters should be outputted to.
	FilterOutputDirs []string `json:"FilterOutputDirs"`

	// RuleforgeInputDir specifies the directory which filter(s) should be processed.
	RuleforgeInputDir string `json:"RuleforgeInputDir"`

	// StyleJSONFile indicates the JSON file where all styles are housed.
	StyleJSONFile string `json:"StyleJSONFile"`
}
