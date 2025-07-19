package config

import (
	"fmt"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/extensions"
	"slices"
)

type EconomyWeights struct {
	Rarity float64 `json:"Rarity"`
	Value  float64 `json:"Value"`
}

type LeagueWeights struct {
	League string  `json:"League"`
	Weight float64 `json:"Weight"`
}

type ConfigurationModel struct {
	// FilterOutputDirs defines the directories where the filters should be outputted to.
	FilterOutputDirs []string `json:"FilterOutputDirs"`

	// RuleforgeInputDir specifies the directory which filter(s) should be processed.
	RuleforgeInputDir string `json:"RuleforgeInputDir"`

	// StyleJSONFile indicates the JSON file where all styles are housed.
	StyleJSONFile string `json:"StyleJSONFile"`

	// StyleColorCSSFile indicates the CSS file where all colors are housed.
	StyleColorCSSFile string `json:"StyleColorCSSFile"`

	// BaseTypeCSVFile indicates the CSV file where all base type automation data is housed.
	BaseTypeCSVFile string `json:"BaseTypeCSVFile"`

	// PathOfBuildingDataPath is where all Path of Building data is stored.
	PathOfBuildingDataPath string `json:"PathOfBuildingDataPath"`

	// LeagueWeights defines the leagues to be used for economy-based data, along with their relative weighting.
	LeagueWeights map[string]float64 `json:"LeagueWeights"`

	// EconomyWeights provide the weights to use for rarity and value.
	EconomyWeights *EconomyWeights `json:"EconomyWeights"`

	// EconomyNormalizationStrategy determines the strategy to use when normalizing data.
	EconomyNormalizationStrategy string `json:"EconomyNormalizationStrategy"`

	// ChaseVSGeneralPotentialFactor specifies how much to value an item's chase potential vs. its general value.
	ChaseVSGeneralPotentialFactor float64 `json:"ChaseVSGeneralPotentialFactor"`
}

var validNormalizationStrategies = []string{
	"Global",
	"Per-League",
}

func (c *ConfigurationModel) GetLeagueWeights() []LeagueWeights {
	leagueWeights := make([]LeagueWeights, 0)

	for league, weight := range c.LeagueWeights {
		leagueWeights = append(leagueWeights, LeagueWeights{
			League: league,
			Weight: weight,
		})
	}

	return leagueWeights
}

func (c *ConfigurationModel) Validate() error {
	if !slices.Contains(validNormalizationStrategies, c.EconomyNormalizationStrategy) {
		validString := extensions.GetFormattedString(validNormalizationStrategies)
		return fmt.Errorf("invalid normalization strategy '%s', expected one of: %s", c.EconomyNormalizationStrategy, validString)
	}

	totalEconomyWeights := c.EconomyWeights.Rarity + c.EconomyWeights.Value

	if totalEconomyWeights != 1.0 {
		return fmt.Errorf("invalid sum EconomyWeights, expected 1, got %f", totalEconomyWeights)
	}

	totalLeagueWeights := 0.0

	for _, weight := range c.LeagueWeights {
		totalLeagueWeights += weight
	}

	if totalLeagueWeights != 1.0 {
		return fmt.Errorf("invalid sum LeagueWeights, expected 1, got %f", totalLeagueWeights)
	}

	if c.ChaseVSGeneralPotentialFactor < 0.0 || c.ChaseVSGeneralPotentialFactor > 1.0 {
		return fmt.Errorf("invalid potential factor %f, must be between 0 and 1", c.ChaseVSGeneralPotentialFactor)
	}

	return nil
}

func (c *ConfigurationModel) GetLeaguesToRetrieve() []string {
	var leaguesToRetrieve []string

	for league, _ := range c.LeagueWeights {
		leaguesToRetrieve = append(leaguesToRetrieve, league)
	}

	return leaguesToRetrieve
}
