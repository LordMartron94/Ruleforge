package config

import (
	"fmt"
	"slices"
	"strings"

	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/extensions"
)

type EconomyWeights struct {
	Rarity float64 `json:"Rarity"`
	Value  float64 `json:"Value"`
}

type LeagueWeights struct {
	League string  `json:"League"`
	Weight float64 `json:"Weight"`
}

type EquipmentPreset struct {
	DesiredWeaponClasses []string `json:"DesiredWeaponClasses"`
	DesiredArmourTypes   []string `json:"DesiredArmourTypes"`
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

	// CustomEquipmentPresets lets you specify named presets with weapons/armour lists.
	CustomEquipmentPresets map[string]EquipmentPreset `json:"CustomEquipmentPresets"`
}

func (c *ConfigurationModel) String() string {
	var sb strings.Builder

	sb.WriteString("\n───────────────────────────────\n")
	sb.WriteString("  ⚙️  CONFIGURATION SUMMARY\n")
	sb.WriteString("───────────────────────────────\n\n")

	sb.WriteString("📂 Filter Output Dirs:\n")
	if len(c.FilterOutputDirs) > 0 {
		for _, dir := range c.FilterOutputDirs {
			sb.WriteString(fmt.Sprintf("   • %s\n", dir))
		}
	} else {
		sb.WriteString("   (none)\n")
	}

	sb.WriteString(fmt.Sprintf("\n📥 Ruleforge Input Dir: %s\n", c.RuleforgeInputDir))
	sb.WriteString(fmt.Sprintf("🎨 Style JSON File:     %s\n", c.StyleJSONFile))
	sb.WriteString(fmt.Sprintf("🎨 Style Color CSS:     %s\n", c.StyleColorCSSFile))
	sb.WriteString(fmt.Sprintf("📊 BaseType CSV File:   %s\n", c.BaseTypeCSVFile))
	sb.WriteString(fmt.Sprintf("📘 PoB Data Path:       %s\n", c.PathOfBuildingDataPath))

	sb.WriteString("\n💰 League Weights:\n")
	if len(c.LeagueWeights) > 0 {
		for league, weight := range c.LeagueWeights {
			sb.WriteString(fmt.Sprintf("   • %-15s %.2f\n", league, weight))
		}
	} else {
		sb.WriteString("   (none)\n")
	}

	if c.EconomyWeights != nil {
		sb.WriteString(fmt.Sprintf("\n💎 Economy Weights: %+v\n", *c.EconomyWeights))
	} else {
		sb.WriteString("\n💎 Economy Weights: (none)\n")
	}

	sb.WriteString(fmt.Sprintf("\n📈 Normalization Strategy: %s\n", c.EconomyNormalizationStrategy))
	sb.WriteString(fmt.Sprintf("⚖️  Chase vs General Factor: %.2f\n", c.ChaseVSGeneralPotentialFactor))

	sb.WriteString("\n🧩 Custom Equipment Presets:\n")
	if len(c.CustomEquipmentPresets) > 0 {
		for name, preset := range c.CustomEquipmentPresets {
			sb.WriteString(fmt.Sprintf("   • %s → %+v\n", name, preset))
		}
	} else {
		sb.WriteString("   (none)\n")
	}

	sb.WriteString("\n───────────────────────────────\n")

	return sb.String()
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

//goland:noinspection t
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

	for name, preset := range c.CustomEquipmentPresets {
		if len(preset.DesiredWeaponClasses) == 0 && len(preset.DesiredArmourTypes) == 0 {
			return fmt.Errorf("preset %q must specify at least one DesiredWeaponClasses or DesiredArmourTypes", name)
		}
	}

	return nil
}

func (c *ConfigurationModel) GetLeaguesToRetrieve() []string {
	var leaguesToRetrieve []string

	for league := range c.LeagueWeights {
		leaguesToRetrieve = append(leaguesToRetrieve, league)
	}

	return leaguesToRetrieve
}

func (c *ConfigurationModel) GetEquipmentPreset(name string) (EquipmentPreset, bool) {
	preset, ok := c.CustomEquipmentPresets[name]
	return preset, ok
}
