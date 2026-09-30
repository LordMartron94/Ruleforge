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
var pageShell string

// editorsScript holds the theme, stylesheet and script editors, which a page
// served by the Studio server turns on.
//
//go:embed editors.js
var editorsScript string

const editorsPlaceholder = "/*STUDIO_EDITORS*/"

// pageTemplate is the page with its editors inlined.
var pageTemplate = strings.Replace(pageShell, editorsPlaceholder, strings.ReplaceAll(editorsScript, "</script", `<\/script`), 1)

const dataPlaceholder = "/*STUDIO_DATA*/null"

// Data is everything the studio page needs.
type Data struct {
	GeneratedAt string   `json:"generatedAt"`
	Styles      []Style  `json:"styles"`
	Filters     []Filter `json:"filters"`
	Items       ItemPool `json:"items"`
	// Server is set when a Studio server serves the page; it turns the editors on.
	Server *ServerInfo `json:"server,omitempty"`
}

// ServerInfo is what a served page needs to call its server.
type ServerInfo struct {
	// Token authenticates the page's requests.
	Token string `json:"token"`
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

	// PaletteGroups orders the palette as its style file does.
	PaletteGroups []config.PaletteGroup `json:"paletteGroups"`
	// Sources says, for every style in the stylesheet (templates included),
	// what it combines and which style supplied each channel.
	Sources map[string]config.StyleSource `json:"sources"`
}

// NamedLook is a resolved style from the stylesheet, shown in the studio's gallery.
type NamedLook struct {
	Name string                 `json:"name"`
	Look compilation.StudioLook `json:"look"`
}

// Filter is one compiled filter: a script compiled with a style.
type Filter struct {
	Script string `json:"script"`
	// ScriptFile is the script's file name in the input folder.
	ScriptFile string                    `json:"scriptFile"`
	StyleID    string                    `json:"style"`
	FileName   string                    `json:"file"`
	Blocks     []compilation.StudioBlock `json:"blocks"`
}

// NewStyle describes a style definition and its loaded stylesheet. Only the
// styles a script can use directly are included: final styles and the fallback.
func NewStyle(definition config.StyleDefinition, sheet *config.Stylesheet) Style {
	style := Style{
		ID:            definition.ID,
		Name:          definition.Name,
		Suffix:        definition.Suffix,
		File:          definition.Path,
		Palette:       definition.Palette,
		Distinct:      sheet.Studio.Distinct,
		PaletteGroups: definition.PaletteGroups,
		Sources:       sheet.Sources,
	}
	for name, resolved := range sheet.Styles {
		if name == "Fallback" || strings.HasPrefix(name, "Final/") || strings.Contains(name, "/Final/") {
			style.Looks = append(style.Looks, NamedLook{Name: name, Look: compilation.LookFromStyle(resolved)})
		}
	}
	sort.Slice(style.Looks, func(i, j int) bool { return style.Looks[i].Name < style.Looks[j].Name })
	return style
}

// page is Data as the page receives it. Blocks repeat the same conditions,
// sources and looks many times over, so each distinct value is stored once and
// blocks refer to it by index.
type page struct {
	GeneratedAt string                          `json:"generatedAt"`
	Server      *ServerInfo                     `json:"server,omitempty"`
	Styles      []Style                         `json:"styles"`
	Items       ItemPool                        `json:"items"`
	Conditions  [][]compilation.StudioCondition `json:"conditions"`
	Sources     []string                        `json:"sources"`
	Looks       []compilation.StudioLook        `json:"looks"`
	Filters     []pageFilter                    `json:"filters"`
}

type pageFilter struct {
	Script     string      `json:"script"`
	ScriptFile string      `json:"scriptFile"`
	StyleID    string      `json:"style"`
	FileName   string      `json:"file"`
	Blocks     []pageBlock `json:"blocks"`
}

type pageBlock struct {
	Line      int    `json:"line"`
	Action    string `json:"action"`
	Section   string `json:"section"`
	Style     string `json:"style,omitempty"`
	Condition int    `json:"cond"`
	Source    int    `json:"src"`
	Look      int    `json:"lk"`
}

// interner hands out a stable index for each distinct value.
type interner struct{ index map[string]int }

func (in *interner) id(value any, add func()) (int, error) {
	key, err := json.Marshal(value)
	if err != nil {
		return 0, err
	}
	if id, seen := in.index[string(key)]; seen {
		return id, nil
	}
	id := len(in.index)
	in.index[string(key)] = id
	add()
	return id, nil
}

func compact(data Data) (page, error) {
	out := page{GeneratedAt: data.GeneratedAt, Server: data.Server, Styles: data.Styles, Items: data.Items}
	conditions := interner{map[string]int{}}
	sources := interner{map[string]int{}}
	looks := interner{map[string]int{}}
	for _, filter := range data.Filters {
		compacted := pageFilter{Script: filter.Script, ScriptFile: filter.ScriptFile, StyleID: filter.StyleID, FileName: filter.FileName}
		for _, block := range filter.Blocks {
			condition, err := conditions.id(block.Conditions, func() { out.Conditions = append(out.Conditions, block.Conditions) })
			if err != nil {
				return out, err
			}
			source, err := sources.id(block.Source, func() { out.Sources = append(out.Sources, block.Source) })
			if err != nil {
				return out, err
			}
			look, err := looks.id(block.Look, func() { out.Looks = append(out.Looks, block.Look) })
			if err != nil {
				return out, err
			}
			compacted.Blocks = append(compacted.Blocks, pageBlock{
				Line: block.Line, Action: block.Action, Section: block.Section, Style: block.Style,
				Condition: condition, Source: source, Look: look,
			})
		}
		out.Filters = append(out.Filters, compacted)
	}
	return out, nil
}

// Render builds the studio page.
func Render(data Data) ([]byte, error) {
	compacted, err := compact(data)
	if err != nil {
		return nil, fmt.Errorf("encoding studio data: %w", err)
	}
	payload, err := json.Marshal(compacted)
	if err != nil {
		return nil, fmt.Errorf("encoding studio data: %w", err)
	}
	// The data sits inside a <script> element, so it must never close it.
	safe := strings.ReplaceAll(string(payload), "</", `<\/`)
	if !strings.Contains(pageTemplate, dataPlaceholder) {
		return nil, fmt.Errorf("studio template is missing its data placeholder")
	}
	return []byte(strings.Replace(pageTemplate, dataPlaceholder, safe, 1)), nil
}

// Write renders the studio page to path.
func Write(path string, data Data) error {
	page, err := Render(data)
	if err != nil {
		return err
	}
	if err := os.MkdirAll(filepath.Dir(path), 0755); err != nil {
		return fmt.Errorf("creating studio directory: %w", err)
	}
	return os.WriteFile(path, page, 0644)
}
