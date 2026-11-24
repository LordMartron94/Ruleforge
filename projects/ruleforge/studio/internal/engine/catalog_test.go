package engine

import (
	"encoding/json"
	"os"
	"path/filepath"
	"testing"
)

func TestCatalogWarningsDeduplicateExpandedRules(t *testing.T) {
	dir := t.TempDir()
	path := filepath.Join(dir, "bases.json")
	os.WriteFile(path, []byte(`[{"baseType":"Driftwood Wand"}]`), 0o644)
	block := `{"rule":{"file":"entry.rf","line":8},"conditions":[{"name":"baseType","op":"==","values":["Driftwood Wand","Typo Wand"]}]}`
	model := json.RawMessage(`{"blocks":[` + block + `,` + block + `]}`)
	diagnostics, err := validateCatalog(Request{Dir: dir, Datasets: map[string]string{"game.bases": path}}, model)
	if err != nil || len(diagnostics) != 1 || diagnostics[0].Code != "RF0901" {
		t.Fatalf("warnings: %+v, %v", diagnostics, err)
	}
}
