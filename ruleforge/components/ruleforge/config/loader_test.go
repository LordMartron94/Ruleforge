package config

import (
	"encoding/json"
	"os"
	"path/filepath"
	"testing"
)

func TestLoadConfigurationResolvesPathsFromConfigDirectory(t *testing.T) {
	directory := t.TempDir()
	configDir := filepath.Join(directory, "nested")
	if err := os.MkdirAll(configDir, 0755); err != nil {
		t.Fatal(err)
	}
	path := filepath.Join(configDir, "config.json")
	contents := `{"FilterOutputDirs":["output","../other"],"RuleforgeInputDir":"examples","StyleJSONFile":"../styles.json","StyleColorCSSFile":"styles.css","BaseTypeCSVFile":"base.csv"}`
	if err := os.WriteFile(path, []byte(contents), 0644); err != nil {
		t.Fatal(err)
	}
	cfg, err := NewConfigurationLoader().LoadConfiguration(path)
	if err != nil {
		t.Fatal(err)
	}
	checks := map[string]string{
		cfg.FilterOutputDirs[0]:    filepath.Join(configDir, "output"),
		cfg.FilterOutputDirs[1]:    filepath.Join(directory, "other"),
		cfg.RuleforgeInputDir:      filepath.Join(configDir, "examples"),
		cfg.StyleJSONFile:          filepath.Join(directory, "styles.json"),
		cfg.StyleColorCSSFile:      filepath.Join(configDir, "styles.css"),
		cfg.BaseTypeCSVFile:        filepath.Join(configDir, "base.csv"),
		cfg.PathOfBuildingDataPath: filepath.Join(configDir, ".ruleforge-cache", "pob"),
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
	path := filepath.Join(directory, "config.json")
	data, _ := json.Marshal(map[string]any{"PathOfBuildingDataPath": absolute, "FilterOutputDirs": []string{absolute}})
	if err := os.WriteFile(path, data, 0644); err != nil {
		t.Fatal(err)
	}
	cfg, err := NewConfigurationLoader().LoadConfiguration(path)
	if err != nil {
		t.Fatal(err)
	}
	if cfg.PathOfBuildingDataPath != absolute || cfg.FilterOutputDirs[0] != absolute {
		t.Fatalf("absolute paths changed: %+v", cfg)
	}
}
