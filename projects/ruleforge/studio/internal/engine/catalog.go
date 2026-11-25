package engine

import (
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"strings"
)

// Catalog warnings use the engine's lowered condition model, never a second source parser.
func validateCatalog(req Request, model json.RawMessage) ([]Diagnostic, error) {
	known := map[string]bool{}
	coveredClasses := map[string]bool{}
	configured := false
	for _, source := range []string{"game.bases", "game.gems", "game.uniques"} {
		path := req.Datasets[source]
		if path == "" {
			continue
		}
		configured = true
		if !filepath.IsAbs(path) {
			path = filepath.Join(req.Dir, path)
		}
		data, err := os.ReadFile(path)
		if err != nil {
			return nil, fmt.Errorf("catalogue %s: %w", source, err)
		}
		var rows []struct {
			BaseType string `json:"baseType"`
			Class    string `json:"class"`
		}
		if err := json.Unmarshal(data, &rows); err != nil {
			return nil, fmt.Errorf("catalogue %s: %w", source, err)
		}
		for _, row := range rows {
			known[strings.ToLower(row.BaseType)] = true
			if row.Class != "" {
				coveredClasses[strings.ToLower(row.Class)] = true
			}
		}
	}
	if !configured {
		return nil, nil
	}
	var product struct {
		Blocks []struct {
			Rule struct {
				File   string `json:"file"`
				Line   int    `json:"line"`
				Column int    `json:"column"`
			} `json:"rule"`
			Conditions []struct {
				Name   string            `json:"name"`
				Op     string            `json:"op"`
				Values []json.RawMessage `json:"values"`
			} `json:"conditions"`
		} `json:"blocks"`
	}
	if err := json.Unmarshal(model, &product); err != nil {
		return nil, err
	}
	seen := map[string]bool{}
	diagnostics := []Diagnostic{}
	for _, block := range product.Blocks {
		// PoB does not catalogue currency, cards, fragments or league drops.
		// Warn only for a class PoB covers, or a mixed name list containing a
		// known base (which can reveal a misspelled alternative).
		covered := false
		for _, condition := range block.Conditions {
			for _, raw := range condition.Values {
				var value string
				if json.Unmarshal(raw, &value) != nil {
					continue
				}
				if condition.Name == "class" && coveredClasses[strings.ToLower(value)] {
					covered = true
				}
				if condition.Name == "baseType" && known[strings.ToLower(value)] {
					covered = true
				}
			}
		}
		if !covered {
			continue
		}
		for _, condition := range block.Conditions {
			if condition.Name != "baseType" || condition.Op != "==" {
				continue
			}
			for _, raw := range condition.Values {
				var name string
				if json.Unmarshal(raw, &name) != nil {
					continue
				}
				key := strings.ToLower(name)
				if known[key] || seen[key] {
					continue
				}
				seen[key] = true
				diagnostics = append(diagnostics, Diagnostic{Severity: "warning", Code: "RF0901", File: block.Rule.File, Line: block.Rule.Line, Column: block.Rule.Column, Message: fmt.Sprintf("base type %q is absent from the configured PoB catalogue", name), Note: "The catalogue may be older than the game. Update gameData before changing the filter."})
			}
		}
	}
	return diagnostics, nil
}
