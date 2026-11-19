package config

import (
	"crypto/sha256"
	"encoding/hex"
	"fmt"
	"os"
	"path/filepath"
	"strings"

	"github.com/BurntSushi/toml"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/config/tomledit"
)

// ThemeFile is a style file as the Studio's theme editor shows it: its
// settings and its palette in the groups its comments make.
type ThemeFile struct {
	ID         string       `json:"id"`
	File       string       `json:"file"`
	Name       string       `json:"name"`
	Suffix     string       `json:"suffix"`
	Enabled    bool         `json:"enabled"`
	Stylesheet string       `json:"stylesheet"`
	BaseTypes  string       `json:"baseTypes"`
	Groups     []PaletteSet `json:"groups"`
	// Hash identifies the file contents the theme was read from.
	Hash string `json:"hash"`
}

// PaletteSet is a run of palette colours under its comment heading.
type PaletteSet struct {
	Title string `json:"title"`
	// Comments are the heading's comment lines as written; the first holds the title.
	Comments []string `json:"comments,omitempty"`
	// Blank is the number of empty lines before the group.
	Blank   int             `json:"blank"`
	Colours []PaletteColour `json:"colours"`
}

// PaletteColour is one named palette colour.
type PaletteColour struct {
	Name  string `json:"name"`
	Value string `json:"value"`
}

// ContentHash identifies file contents for change detection.
func ContentHash(contents []byte) string {
	sum := sha256.Sum256(contents)
	return hex.EncodeToString(sum[:8])
}

// ReadThemeFile reads a style file for editing.
func ReadThemeFile(path string) (*ThemeFile, error) {
	contents, err := os.ReadFile(path)
	if err != nil {
		return nil, err
	}
	return ParseThemeFile(path, contents)
}

// ParseThemeFile reads a style file's contents for editing.
func ParseThemeFile(path string, contents []byte) (*ThemeFile, error) {
	var settings struct {
		Name       string `toml:"Name"`
		Suffix     string `toml:"Suffix"`
		Enabled    *bool  `toml:"Enabled"`
		Stylesheet string `toml:"Stylesheet"`
		BaseTypes  string `toml:"BaseTypes"`
	}
	if _, err := toml.Decode(string(contents), &settings); err != nil {
		return nil, fmt.Errorf("reading style %s: %w", path, err)
	}
	doc, err := tomledit.Parse(string(contents))
	if err != nil {
		return nil, fmt.Errorf("style %s: %w", path, err)
	}
	theme := &ThemeFile{
		ID:         strings.TrimSuffix(filepath.Base(path), filepath.Ext(path)),
		File:       filepath.Base(path),
		Name:       settings.Name,
		Suffix:     settings.Suffix,
		Enabled:    settings.Enabled == nil || *settings.Enabled,
		Stylesheet: settings.Stylesheet,
		BaseTypes:  settings.BaseTypes,
		Hash:       ContentHash(contents),
	}
	theme.Groups, err = paletteSets(doc.Body("Palette"))
	if err != nil {
		return nil, fmt.Errorf("style %s: %w", path, err)
	}
	return theme, nil
}

// paletteSets splits a [Palette] body into groups. A comment starts a group
// after an empty line or after colours; so does a colour after an empty line.
func paletteSets(body []string) ([]PaletteSet, error) {
	var sets []PaletteSet
	blank := 0
	current := func() *PaletteSet { return &sets[len(sets)-1] }
	for _, text := range body {
		trimmed := strings.TrimSpace(text)
		switch {
		case trimmed == "":
			blank++
		case strings.HasPrefix(trimmed, "#"):
			if len(sets) == 0 || blank > 0 || len(current().Colours) > 0 {
				sets = append(sets, PaletteSet{Blank: blank})
			}
			current().Comments = append(current().Comments, text)
			blank = 0
		default:
			colour, err := parsePaletteLine(trimmed)
			if err != nil {
				return nil, err
			}
			if len(sets) == 0 || blank > 0 {
				sets = append(sets, PaletteSet{Blank: blank})
			}
			current().Colours = append(current().Colours, colour)
			blank = 0
		}
	}
	for i := range sets {
		if len(sets[i].Comments) > 0 {
			sets[i].Title = strings.TrimSpace(strings.TrimPrefix(strings.TrimSpace(sets[i].Comments[0]), "#"))
		}
	}
	if blank > 0 {
		// Empty lines at the end of the table belong to no group.
		sets = append(sets, PaletteSet{Blank: blank})
	}
	return sets, nil
}

func parsePaletteLine(text string) (PaletteColour, error) {
	var decoded map[string]any
	if _, err := toml.Decode(text, &decoded); err != nil {
		return PaletteColour{}, err
	}
	for name, value := range decoded {
		s, ok := value.(string)
		if !ok {
			return PaletteColour{}, fmt.Errorf("palette colour %s is not a string", name)
		}
		return PaletteColour{Name: name, Value: s}, nil
	}
	return PaletteColour{}, fmt.Errorf("expected name = \"#RRGGBB\"")
}

// Render writes the theme into a style file's current contents, changing only
// what differs. With empty contents it writes a new file.
func (t *ThemeFile) Render(current string) (string, error) {
	if strings.TrimSpace(current) == "" {
		current = "# " + t.Name + "\n"
	}
	doc, err := tomledit.Parse(current)
	if err != nil {
		return "", err
	}
	old := map[string]string{}
	for _, kv := range doc.Keys("") {
		old[kv.Key] = kv.Value
	}
	setString := func(key, value string, keep bool) error {
		if _, exists := old[key]; !exists && !keep {
			return nil
		}
		if existing, exists := old[key]; exists && decodesTo(existing, value) {
			return nil
		}
		return doc.Set("", key, tomledit.String(value))
	}
	if err := setString("Name", t.Name, true); err != nil {
		return "", err
	}
	if _, exists := old["Suffix"]; exists && strings.TrimSpace(t.Suffix) == "" {
		// Without a suffix the theme's name is used.
		doc.Delete("", "Suffix")
	} else if err := setString("Suffix", t.Suffix, t.Suffix != "" && t.Suffix != t.Name); err != nil {
		return "", err
	}
	if existing, exists := old["Enabled"]; exists || !t.Enabled {
		if !exists || existing != tomledit.Bool(t.Enabled) {
			if err := doc.Set("", "Enabled", tomledit.Bool(t.Enabled)); err != nil {
				return "", err
			}
		}
	}
	if err := setString("Stylesheet", t.Stylesheet, true); err != nil {
		return "", err
	}
	if err := setString("BaseTypes", t.BaseTypes, true); err != nil {
		return "", err
	}

	previous := map[string]string{}
	for _, text := range doc.Body("Palette") {
		if colour, err := parsePaletteLine(strings.TrimSpace(text)); err == nil && !strings.HasPrefix(strings.TrimSpace(text), "#") && strings.TrimSpace(text) != "" {
			previous[colour.Name+"\x00"+colour.Value] = text
		}
	}
	var body []string
	for _, set := range t.Groups {
		for i := 0; i < set.Blank; i++ {
			body = append(body, "")
		}
		comments := append([]string(nil), set.Comments...)
		title := strings.TrimSpace(set.Title)
		switch {
		case len(comments) == 0 && title != "":
			comments = []string{"# " + title}
		case len(comments) > 0 && title == "":
			comments = comments[1:]
		case len(comments) > 0 && strings.TrimSpace(strings.TrimPrefix(strings.TrimSpace(comments[0]), "#")) != title:
			comments[0] = "# " + title
		}
		body = append(body, comments...)
		for _, colour := range set.Colours {
			if text, ok := previous[colour.Name+"\x00"+colour.Value]; ok {
				body = append(body, text)
				continue
			}
			body = append(body, tomledit.FormatKey(colour.Name)+" = "+tomledit.String(colour.Value))
		}
	}
	if doc.HasTable("Palette") {
		err = doc.ReplaceBody("Palette", body)
	} else {
		err = doc.AppendTable("Palette", body)
	}
	if err != nil {
		return "", err
	}
	return doc.String(), nil
}

// decodesTo reports whether a TOML value text holds the given string.
func decodesTo(valueText, want string) bool {
	var decoded map[string]any
	if _, err := toml.Decode("x = "+valueText, &decoded); err != nil {
		return false
	}
	s, ok := decoded["x"].(string)
	return ok && s == want
}

// Palette returns the theme's colours by name.
func (t *ThemeFile) Palette() map[string]string {
	palette := map[string]string{}
	for _, set := range t.Groups {
		for _, colour := range set.Colours {
			palette[colour.Name] = colour.Value
		}
	}
	return palette
}
