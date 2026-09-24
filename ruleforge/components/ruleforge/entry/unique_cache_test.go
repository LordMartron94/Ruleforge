package main

import (
	"log"
	"os"
	"path/filepath"
	"testing"

	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/data_generation"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/data_generation/model"
)

func TestExtractUniqueBasesDoesNotRegenerateValidCache(t *testing.T) {
	cacheDir := t.TempDir()
	cache := data_generation.NewCacheRepository(filepath.Join(cacheDir, "basetypes-standard-only.json"), filepath.Join(cacheDir, "economy_cache.json"))
	want := []model.Unique{{Name: "Cached standard unique", BaseType: "Test Base"}}
	if err := cache.SaveItemCache(nil, nil, nil, want); err != nil {
		t.Fatal(err)
	}
	app := &App{
		log:      log.New(os.Stdout, "", 0),
		exporter: data_generation.NewPathOfBuildingExporter(cacheDir),
	}
	// This directory intentionally does not exist: if the extractor attempts
	// to read source modules, the test must fail rather than silently pass.
	got, err := app.extractUniqueBases(filepath.Join(t.TempDir(), "missing-pob-source"))
	if err != nil {
		t.Fatal(err)
	}
	if len(got) != 1 || got[0].Name != want[0].Name {
		t.Fatalf("cached uniques were duplicated or lost: %#v", got)
	}
}

// Generated.lua is intentionally syntactically invalid; the low-memory path
// must not open it or allocate its dependency graph, even on a cold cache.
func TestExtractUniqueBasesSkipsGeneratedLuaOnColdCache(t *testing.T) {
	root := t.TempDir()
	uniqueDir := filepath.Join(root, "Uniques")
	specialDir := filepath.Join(uniqueDir, "Special")
	if err := os.MkdirAll(specialDir, 0755); err != nil {
		t.Fatal(err)
	}
	standard := `return { [[Standard Sword
Test Sword
Implicits: 0
]] }`
	for name, content := range map[string]string{
		filepath.Join(uniqueDir, "sword.lua"):      standard,
		filepath.Join(specialDir, "New.lua"):       "data.uniques.new = {}",
		filepath.Join(specialDir, "Generated.lua"): "this script is deliberately invalid lua",
	} {
		if err := os.WriteFile(name, []byte(content), 0644); err != nil {
			t.Fatal(err)
		}
	}
	app := &App{
		log:      log.New(os.Stdout, "", 0),
		exporter: data_generation.NewPathOfBuildingExporter(t.TempDir()),
	}
	uniques, err := app.extractUniqueBases(root)
	if err != nil {
		t.Fatal(err)
	}
	if len(uniques) != 1 || uniques[0].Name != "Standard Sword" {
		t.Fatalf("standard uniques omitted or generated uniques unexpectedly loaded: %#v", uniques)
	}
}
