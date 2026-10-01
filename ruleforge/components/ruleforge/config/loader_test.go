package config

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func writeFile(t *testing.T, path, contents string) {
	t.Helper()
	if err := os.MkdirAll(filepath.Dir(path), 0755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(path, []byte(contents), 0644); err != nil {
		t.Fatal(err)
	}
}

func TestLoadConfigurationResolvesPathsFromConfigDirectory(t *testing.T) {
	directory := t.TempDir()
	configDir := filepath.Join(directory, "nested")
	path := filepath.Join(configDir, "config.toml")
	writeFile(t, path, `
FilterOutputDirs = ["output", "../other"]
RuleforgeInputDir = "examples"
StylesDir = "../styles"
StudioFile = "output/studio.html"
`)
	cfg, err := NewConfigurationLoader().LoadConfiguration(path)
	if err != nil {
		t.Fatal(err)
	}
	checks := map[string]string{
		cfg.FilterOutputDirs[0]:      filepath.Join(configDir, "output"),
		cfg.FilterOutputDirs[1]:      filepath.Join(directory, "other"),
		cfg.RuleforgeInputDir:        filepath.Join(configDir, "examples"),
		cfg.StylesDir:                filepath.Join(directory, "styles"),
		cfg.StudioFile:               filepath.Join(configDir, "output", "studio.html"),
		cfg.PathOfBuildingDataPath:   filepath.Join(configDir, ".ruleforge-cache", "pob"),
		cfg.CurrencyTiering.Profiles: filepath.Join(configDir, "configuration", "currency", "currency_profiles.csv"),
	}
	if cfg.CurrencyTiering.CraftingWeight != 0.4 {
		t.Errorf("default crafting weight is %v, want 0.4", cfg.CurrencyTiering.CraftingWeight)
	}
	for got, want := range checks {
		if got != want {
			t.Errorf("got %q, want %q", got, want)
		}
	}
}

func TestAbsoluteConfigPathsPreserved(t *testing.T) {
	directory := t.TempDir()
	absolute := filepath.Join(directory, "data")
	path := filepath.Join(directory, "config.toml")
	writeFile(t, path, "PathOfBuildingDataPath = '"+absolute+"'\nFilterOutputDirs = ['"+absolute+"']\n[CurrencyTiering]\nCraftingWeight = 0\n")
	cfg, err := NewConfigurationLoader().LoadConfiguration(path)
	if err != nil {
		t.Fatal(err)
	}
	if cfg.PathOfBuildingDataPath != absolute || cfg.FilterOutputDirs[0] != absolute || cfg.CurrencyTiering.CraftingWeight != 0 {
		t.Fatalf("absolute paths changed: %+v", cfg)
	}
}

func TestLoadConfigurationRejectsUnknownKeys(t *testing.T) {
	path := filepath.Join(t.TempDir(), "config.toml")
	writeFile(t, path, "StylesDir = 'styles'\nStyleJSONFile = 'old.json'\n")
	_, err := NewConfigurationLoader().LoadConfiguration(path)
	if err == nil || !strings.Contains(err.Error(), "StyleJSONFile") {
		t.Fatalf("expected an unknown-setting error naming StyleJSONFile, got %v", err)
	}
}

func TestLoadConfigurationReadsNestedTables(t *testing.T) {
	path := filepath.Join(t.TempDir(), "config.toml")
	writeFile(t, path, `
StylesDir = "styles"

[LeagueWeights]
"Hardcore Allflame" = 0.7
Standard = 0.3

[EconomyWeights]
Value = 0.65
Rarity = 0.35

[CustomEquipmentPresets.MartronArada]
DesiredWeaponClasses = ["One Hand Swords"]
DesiredArmourTypes = ["Armour", "Armour/Evasion"]

[CurrencyTiering]
Profiles = "currency.csv"
`)
	cfg, err := NewConfigurationLoader().LoadConfiguration(path)
	if err != nil {
		t.Fatal(err)
	}
	if cfg.LeagueWeights["Hardcore Allflame"] != 0.7 || cfg.EconomyWeights.Value != 0.65 {
		t.Fatalf("weights not decoded: %+v %+v", cfg.LeagueWeights, cfg.EconomyWeights)
	}
	if cfg.CurrencyTiering.CraftingWeight != 0.4 || cfg.CurrencyTiering.Profiles != filepath.Join(filepath.Dir(path), "currency.csv") {
		t.Fatalf("currency profile override lost its default weight or relative path: %+v", cfg.CurrencyTiering)
	}
	preset, ok := cfg.GetEquipmentPreset("MartronArada")
	if !ok || preset.DesiredWeaponClasses[0] != "One Hand Swords" || len(preset.DesiredArmourTypes) != 2 {
		t.Fatalf("preset not decoded: %+v", preset)
	}
}
