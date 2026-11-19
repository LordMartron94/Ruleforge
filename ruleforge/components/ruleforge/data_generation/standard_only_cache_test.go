package data_generation

import (
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/data_generation/model"
	"os"
	"path/filepath"
	"testing"
)

func TestStandardOnlyExporterIgnoresLegacyCache(t *testing.T) {
	cacheDir := t.TempDir()
	// This older cache could contain huge generated-unique payloads or be corrupt.
	if err := os.WriteFile(filepath.Join(cacheDir, "basetypes.json"), []byte("invalid old cache"), 0644); err != nil {
		t.Fatal(err)
	}
	exporter, err := NewPathOfBuildingExporter(cacheDir, filepath.Join(cacheDir, "economy.db"))
	if err != nil {
		t.Fatal(err)
	}
	if _, exists := exporter.CachedUniques(); exists {
		t.Fatal("reused legacy generated-unique cache")
	}
	expected := []model.Unique{{Name: "Normal unique", BaseType: "Test"}}
	if err := exporter.SaveItemCache(nil, nil, nil, expected); err != nil {
		t.Fatal(err)
	}
	if _, err := os.Stat(filepath.Join(cacheDir, "basetypes-standard-only.json")); err != nil {
		t.Fatal(err)
	}
	reloaded, err := NewPathOfBuildingExporter(cacheDir, filepath.Join(cacheDir, "economy.db"))
	if err != nil {
		t.Fatal(err)
	}
	cached, exists := reloaded.CachedUniques()
	if !exists || len(cached) != 1 || cached[0].Name != expected[0].Name {
		t.Fatalf("new standard-only cache was not reused: %#v", cached)
	}
}
