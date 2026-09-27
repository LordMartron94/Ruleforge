// Package studio writes the Ruleforge Studio: a single HTML page for checking a
// style's palette and simulating random loot against the compiled filters.
package studio

import (
	_ "embed"
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"sort"
	"strings"

	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/compilation"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/config"
)

//go:embed studio.html
var pageTemplate string

const dataPlaceholder = "/*STUDIO_DATA*/null"

// Data is everything the studio page needs.
type Data struct {
	GeneratedAt string   `json:"generatedAt"`
	Styles      []Style  `json:"styles"`
	Filters     []Filter `json:"filters"`
	Items       ItemPool `json:"items"`
}

// Style is one style definition with its palette and every final style it resolves to.
type Style struct {
	ID       string                          `json:"id"`
	Name     string                          `json:"name"`
	Suffix   string                          `json:"suffix"`
	File     string                          `json:"file"`
	Palette  map[string]string               `json:"palette"`
	Distinct map[string]config.DistinctGroup `json:"distinct"`
	Looks    []NamedLook                     `json:"looks"`
}

// NamedLook is a resolved style from the stylesheet, shown in the studio's gallery.
type NamedLook struct {
	Name string                 `json:"name"`
	Look compilation.StudioLook `json:"look"`
}

// Filter is one compiled filter: a script compiled with a style.
type Filter struct {
	Script   string                    `json:"script"`
	StyleID  string                    `json:"style"`
	FileName string                    `json:"file"`
	Blocks   []compilation.StudioBlock `json:"blocks"`
}

// NewStyle describes a style definition and its loaded stylesheet. Only the
// styles a script can use directly are included: final styles and the fallback.
func NewStyle(definition config.StyleDefinition, sheet *config.Stylesheet) Style {
	style := Style{
		ID:       definition.ID,
		Name:     definition.Name,
		Suffix:   definition.Suffix,
		File:     definition.Path,
		Palette:  definition.Palette,
		Distinct: sheet.Studio.Distinct,
	}
	for name, resolved := range sheet.Styles {
		if name == "Fallback" || strings.HasPrefix(name, "Final/") || strings.Contains(name, "/Final/") {
			style.Looks = append(style.Looks, NamedLook{Name: name, Look: compilation.LookFromStyle(resolved)})
		}
	}
	sort.Slice(style.Looks, func(i, j int) bool { return style.Looks[i].Name < style.Looks[j].Name })
	return style
}

// Write renders the studio page to path.
func Write(path string, data Data) error {
	payload, err := json.Marshal(data)
	if err != nil {
		return fmt.Errorf("encoding studio data: %w", err)
	}
	// The data sits inside a <script> element, so it must never close it.
	safe := strings.ReplaceAll(string(payload), "</", `<\/`)
	if !strings.Contains(pageTemplate, dataPlaceholder) {
		return fmt.Errorf("studio template is missing its data placeholder")
	}
	page := strings.Replace(pageTemplate, dataPlaceholder, safe, 1)

	if err := os.MkdirAll(filepath.Dir(path), 0755); err != nil {
		return fmt.Errorf("creating studio directory: %w", err)
	}
	return os.WriteFile(path, []byte(page), 0644)
}
