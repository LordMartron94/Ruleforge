package config

import (
	"path/filepath"
	"strings"
	"testing"
)

func TestLoadCurrencyProfilesReadsRatingsNotesAndComments(t *testing.T) {
	path := filepath.Join(t.TempDir(), "profiles.csv")
	writeFile(t, path, "# What one unit is worth to crafting.\nBaseType,Crafting,Note\nChaos Orb,7,\"Rerolls rares, pays vendors\"\n# a comment between rows\nOrb of Alchemy,6.5\n")
	profiles, err := LoadCurrencyProfiles(path)
	if err != nil {
		t.Fatal(err)
	}
	want := []CurrencyProfile{{BaseType: "Chaos Orb", Crafting: 7, Note: "Rerolls rares, pays vendors"}, {BaseType: "Orb of Alchemy", Crafting: 6.5}}
	if len(profiles) != len(want) || profiles[0] != want[0] || profiles[1] != want[1] {
		t.Fatalf("got %+v, want %+v", profiles, want)
	}
}

func TestLoadCurrencyProfilesRejectsBadRows(t *testing.T) {
	for name, body := range map[string]string{
		"twice":       "BaseType,Crafting\nChaos Orb,7\nChaos Orb,6\n",
		"too high":    "BaseType,Crafting\nChaos Orb,11\n",
		"not number":  "BaseType,Crafting\nChaos Orb,lots\n",
		"nan":         "BaseType,Crafting\nChaos Orb,NaN\n",
		"infinite":    "BaseType,Crafting\nChaos Orb,+Inf\n",
		"no base":     "BaseType,Crafting\n,5\n",
		"bad header":  "Name,Rating\nChaos Orb,7\n",
		"extra field": "BaseType,Crafting,Note\nChaos Orb,7,note,more\n",
	} {
		path := filepath.Join(t.TempDir(), "profiles.csv")
		writeFile(t, path, body)
		if _, err := LoadCurrencyProfiles(path); err == nil {
			t.Errorf("%s: no error for %q", name, body)
		}
	}
}

func TestShippedCurrencyProfilesLoad(t *testing.T) {
	profiles, err := LoadCurrencyProfiles(filepath.Join("..", "..", "..", "..", "configuration", "currency", "currency_profiles.csv"))
	if err != nil {
		t.Fatal(err)
	}
	if len(profiles) < 50 {
		t.Fatalf("only %d currency profiles", len(profiles))
	}
	for _, profile := range profiles {
		if strings.TrimSpace(profile.Note) == "" {
			t.Errorf("%s has no note saying why it is rated %v", profile.BaseType, profile.Crafting)
		}
	}
}
