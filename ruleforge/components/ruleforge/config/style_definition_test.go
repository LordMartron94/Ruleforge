package config

import (
	"path/filepath"
	"strings"
	"testing"
)

func TestLoadStyleDefinitionsResolvesPathsAndDefaults(t *testing.T) {
	directory := t.TempDir()
	stylesDir := filepath.Join(directory, "styles")
	writeFile(t, filepath.Join(stylesDir, "b-second.toml"), `
Name = "Second"
Suffix = "Two: second"
Stylesheet = "../sheets/main.toml"
BaseTypes = "../base.csv"
[Palette]
t2 = "#FFD700"
`)
	writeFile(t, filepath.Join(stylesDir, "a-first.toml"), `
Stylesheet = "/abs/sheet.toml"
BaseTypes = "base.csv"
Enabled = false
`)
	writeFile(t, filepath.Join(stylesDir, "notes.txt"), "ignored")

	styles, err := LoadStyleDefinitions(stylesDir)
	if err != nil {
		t.Fatal(err)
	}
	if len(styles) != 2 || styles[0].ID != "a-first" || styles[1].ID != "b-second" {
		t.Fatalf("unexpected styles or order: %+v", styles)
	}
	first, second := styles[0], styles[1]
	if first.Name != "a-first" || first.Suffix != "a-first" || first.IsEnabled() {
		t.Errorf("defaults not applied: %+v", first)
	}
	if first.Stylesheet != filepath.Clean("/abs/sheet.toml") || first.BaseTypes != filepath.Join(stylesDir, "base.csv") {
		t.Errorf("paths not resolved: %+v", first)
	}
	if second.Stylesheet != filepath.Join(directory, "sheets", "main.toml") || !second.IsEnabled() {
		t.Errorf("paths not resolved: %+v", second)
	}
	if got := second.FilterFileName("Campaign"); got != "Campaign (Two- second).filter" {
		t.Errorf("file name: got %q", got)
	}
}

func TestLoadStyleDefinitionsRejectsBadInput(t *testing.T) {
	cases := map[string]string{
		"bad colour":    "Stylesheet = 's'\nBaseTypes = 'b'\n[Palette]\nt2 = 'gold'\n",
		"no stylesheet": "BaseTypes = 'b'\n",
		"unknown key":   "Stylesheet = 's'\nBaseTypes = 'b'\nColour = 'x'\n",
		"no base types": "Stylesheet = 's'\n",
	}
	for name, contents := range cases {
		dir := t.TempDir()
		writeFile(t, filepath.Join(dir, "style.toml"), contents)
		if _, err := LoadStyleDefinitions(dir); err == nil {
			t.Errorf("%s: expected an error", name)
		}
	}
}

func TestLoadStyleDefinitionsRejectsDuplicateSuffixes(t *testing.T) {
	dir := t.TempDir()
	writeFile(t, filepath.Join(dir, "one.toml"), "Suffix = 'Same'\nStylesheet = 's'\nBaseTypes = 'b'\n")
	writeFile(t, filepath.Join(dir, "two.toml"), "Suffix = 'same'\nStylesheet = 's'\nBaseTypes = 'b'\n")
	_, err := LoadStyleDefinitions(dir)
	if err == nil || !strings.Contains(err.Error(), "overwrite") {
		t.Fatalf("expected duplicate suffix error, got %v", err)
	}
}
