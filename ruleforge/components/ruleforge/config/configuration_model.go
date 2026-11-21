package config

import (
	"fmt"
	"math"
	"os"
	"slices"
	"strings"
	"time"

	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/extensions"
)

type EconomyWeights struct {
	Rarity float64 `toml:"Rarity"`
	Value  float64 `toml:"Value"`
	// Trend weighs whether a base type's value is rising or falling across its
	// recorded economy history. It requires at least two snapshots recorded at
	// distinct times to have any effect; until then it contributes nothing to
	// a base type's score.
	Trend float64 `toml:"Trend"`
}

type LeagueWeights struct {
	League string  `toml:"League"`
	Weight float64 `toml:"Weight"`
}

type EquipmentPreset struct {
	PreferredViscera     []string `toml:"PreferredViscera"`
	DesiredWeaponClasses []string `toml:"DesiredWeaponClasses"`
	DesiredArmourTypes   []string `toml:"DesiredArmourTypes"`
}

type ConfigurationModel struct {
	// FilterOutputDirs defines the directories where the filters should be outputted to.
	FilterOutputDirs []string `toml:"FilterOutputDirs"`

	// RuleforgeInputDir specifies the directory which filter(s) should be processed.
	RuleforgeInputDir string `toml:"RuleforgeInputDir"`

	// StylesDir holds one TOML file per style. Every enabled style produces its
	// own filter for every script, named "<script name> (<style suffix>)".
	StylesDir string `toml:"StylesDir"`

	// StudioFile is where the Ruleforge Studio page (palette checks and loot
	// simulator) is written after compiling. Leave it empty to skip the studio.
	StudioFile string `toml:"StudioFile"`

	// PathOfBuildingDataPath is where all Path of Building data is stored.
	PathOfBuildingDataPath string `toml:"PathOfBuildingDataPath"`

	// EconomyHistoryDBPath is where the SQLite economy price history is stored.
	EconomyHistoryDBPath string `toml:"EconomyHistoryDBPath"`

	// EconomyHistoryRefreshHours is how many hours must pass since a league's
	// last recorded snapshot before Ruleforge fetches a new one from poe.ninja.
	EconomyHistoryRefreshHours float64 `toml:"EconomyHistoryRefreshHours"`
	// A configured service supplies raw history instead of doing desktop polls.
	EconomyHistoryURL       string         `toml:"EconomyHistoryURL"`
	EconomyHistoryTokenEnv  string         `toml:"EconomyHistoryTokenEnv"`
	EconomyHistoryTokenFile string         `toml:"EconomyHistoryTokenFile"`
	EconomyMemory           *EconomyMemory `toml:"EconomyMemory"`

	// LeagueWeights defines the leagues to be used for economy-based data, along with their relative weighting.
	LeagueWeights map[string]float64 `toml:"LeagueWeights"`

	// EconomyWeights provide the weights to use for rarity and value.
	EconomyWeights *EconomyWeights `toml:"EconomyWeights"`

	// EconomyNormalizationStrategy determines the strategy to use when normalizing data.
	EconomyNormalizationStrategy string `toml:"EconomyNormalizationStrategy"`

	// ChaseVSGeneralPotentialFactor specifies how much to value an item's chase potential vs. its general value.
	ChaseVSGeneralPotentialFactor float64 `toml:"ChaseVSGeneralPotentialFactor"`

	// EconomyValueHalfLifeDays exponentially decays how much each recorded
	// economy snapshot contributes to a base type's value/rarity estimate: a
	// snapshot this many days old counts half as much as today's. This keeps
	// a long-lived permanent league's ancient prices from dominating what
	// should be a "current value" estimate, without discarding old data
	// outright (a thin history still uses everything it has).
	EconomyValueHalfLifeDays float64 `toml:"EconomyValueHalfLifeDays"`

	// CustomEquipmentPresets lets you specify named presets with weapons/armour lists.
	CustomEquipmentPresets map[string]EquipmentPreset `toml:"CustomEquipmentPresets"`

	// CurrencyTiering blends the economy score with crafting use for the
	// currency_tiering macro. Left out, it reads
	// configuration/currency/currency_profiles.csv with a crafting weight of 0.4.
	CurrencyTiering *CurrencyTiering `toml:"CurrencyTiering"`
}

// EconomyHistoryToken uses an explicit environment override first, then a
// private file so GUI launches do not depend on a terminal's environment.
func (c *ConfigurationModel) EconomyHistoryToken() (string, error) {
	if token := os.Getenv(c.EconomyHistoryTokenEnv); token != "" {
		return token, nil
	}
	if c.EconomyHistoryTokenFile == "" {
		return "", nil
	}
	raw, err := os.ReadFile(c.EconomyHistoryTokenFile)
	if err != nil {
		return "", fmt.Errorf("read economy history token file: %w", err)
	}
	token := strings.TrimSpace(string(raw))
	if token == "" || strings.ContainsAny(token, "\r\n\t ") {
		return "", fmt.Errorf("economy history token file must contain one nonempty token")
	}
	return token, nil
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
	sb.WriteString(fmt.Sprintf("🎨 Styles Dir:          %s\n", c.StylesDir))
	sb.WriteString(fmt.Sprintf("🧪 Studio File:         %s\n", c.StudioFile))
	sb.WriteString(fmt.Sprintf("📘 PoB Data Path:       %s\n", c.PathOfBuildingDataPath))
	sb.WriteString(fmt.Sprintf("📈 Economy History DB:  %s (refresh every %.1fh, kept forever)\n",
		c.EconomyHistoryDBPath, c.EconomyHistoryRefreshHours))

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
	sb.WriteString(fmt.Sprintf("📉 Economy Value Half-Life: %.1f days\n", c.EconomyValueHalfLifeDays))
	if c.CurrencyTiering != nil {
		sb.WriteString(fmt.Sprintf("🪙 Currency Tiering: crafting weight %.2f, profiles %s\n", c.CurrencyTiering.CraftingWeight, c.CurrencyTiering.Profiles))
	}

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

// GetLeagueWeightsFor returns the configured league weights restricted to the
// leagues that actually have economy data, scaled so they still add up to 1.
// A configured league with no data yet therefore hands its share to the others
// in proportion to their own weights.
func (c *ConfigurationModel) GetLeagueWeightsFor(available []string) ([]LeagueWeights, error) {
	total := 0.0
	for _, league := range available {
		total += c.LeagueWeights[league]
	}
	if total <= 0 {
		return nil, fmt.Errorf("none of the leagues with economy data (%v) has a positive weight in LeagueWeights", available)
	}
	weights := make([]LeagueWeights, 0, len(available))
	for _, league := range available {
		weights = append(weights, LeagueWeights{League: league, Weight: c.LeagueWeights[league] / total})
	}
	return weights, nil
}

//goland:noinspection t
func (c *ConfigurationModel) Validate() error {
	if !slices.Contains(validNormalizationStrategies, c.EconomyNormalizationStrategy) {
		validString := extensions.GetFormattedString(validNormalizationStrategies)
		return fmt.Errorf("invalid normalization strategy '%s', expected one of: %s", c.EconomyNormalizationStrategy, validString)
	}

	if c.EconomyWeights == nil {
		return fmt.Errorf("EconomyWeights must be configured")
	}
	if len(c.LeagueWeights) == 0 {
		return fmt.Errorf("LeagueWeights must contain at least one league")
	}
	totalEconomyWeights := c.EconomyWeights.Rarity + c.EconomyWeights.Value + c.EconomyWeights.Trend

	if math.Abs(totalEconomyWeights-1.0) > 1e-9 {
		return fmt.Errorf("invalid sum EconomyWeights (Rarity+Value+Trend), expected 1, got %f", totalEconomyWeights)
	}

	totalLeagueWeights := 0.0

	for _, weight := range c.LeagueWeights {
		totalLeagueWeights += weight
	}

	if math.Abs(totalLeagueWeights-1.0) > 1e-9 {
		return fmt.Errorf("invalid sum LeagueWeights, expected 1, got %f", totalLeagueWeights)
	}

	if c.ChaseVSGeneralPotentialFactor < 0.0 || c.ChaseVSGeneralPotentialFactor > 1.0 {
		return fmt.Errorf("invalid potential factor %f, must be between 0 and 1", c.ChaseVSGeneralPotentialFactor)
	}
	if c.EconomyValueHalfLifeDays <= 0 {
		return fmt.Errorf("EconomyValueHalfLifeDays must be greater than 0, got %f", c.EconomyValueHalfLifeDays)
	}

	if c.StylesDir == "" {
		return fmt.Errorf("StylesDir must point at a directory of style files")
	}

	if c.EconomyHistoryRefreshHours <= 0 {
		return fmt.Errorf("EconomyHistoryRefreshHours must be greater than 0, got %f", c.EconomyHistoryRefreshHours)
	}

	if c.CurrencyTiering != nil && !(c.CurrencyTiering.CraftingWeight >= 0 && c.CurrencyTiering.CraftingWeight <= 1) {
		return fmt.Errorf("CurrencyTiering.CraftingWeight must be between 0 and 1, got %f", c.CurrencyTiering.CraftingWeight)
	}
	if c.EconomyMemory != nil {
		if err := c.EconomyMemory.Validate(); err != nil {
			return err
		}
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

// GetEconomyHistoryRefreshInterval is how long a league's recorded snapshot
// may age before it's considered stale and refetched.
func (c *ConfigurationModel) GetEconomyHistoryRefreshInterval() time.Duration {
	return time.Duration(c.EconomyHistoryRefreshHours * float64(time.Hour))
}
