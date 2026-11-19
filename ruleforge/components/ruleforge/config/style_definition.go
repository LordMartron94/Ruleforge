package config

import (
	"fmt"
	"os"
	"path/filepath"
	"regexp"
	"sort"
	"strings"

	"github.com/BurntSushi/toml"
)

// StyleDefinition is one look for the compiled filters: which stylesheet to
// use, which base type automation CSV goes with it, and the palette that
// fills the stylesheet's colour names. Each *.toml file in the configured
// StylesDir is one StyleDefinition.
type StyleDefinition struct {
	// ID is the style file's name without extension.
	ID string `toml:"-"`

	// Path is the style file itself.
	Path string `toml:"-"`

	// Name is how the style is shown in logs and in the studio.
	Name string `toml:"Name"`

	// Suffix is appended to each filter's file name: "<script name> (<Suffix>).filter".
	// It defaults to Name.
	Suffix string `toml:"Suffix"`

	// Enabled lets a style stay in the directory without being compiled. Defaults to true.
	Enabled *bool `toml:"Enabled"`

	// Stylesheet is the TOML file with the style templates and final styles.
	Stylesheet string `toml:"Stylesheet"`

	// BaseTypes is the base type automation CSV used by the handle_csv macro.
	BaseTypes string `toml:"BaseTypes"`

	// Palette maps colour names used in the stylesheet to "#RRGGBB" or "#RRGGBBAA".
	Palette map[string]string `toml:"Palette"`

	// PaletteGroups orders the palette as the file does: each comment line in
	// [Palette] starts a group titled by that comment.
	PaletteGroups []PaletteGroup `toml:"-"`
}

// PaletteGroup is a titled run of palette colours from a style file.
type PaletteGroup struct {
	Title string   `json:"title"`
	Vars  []string `json:"vars"`
}

// IsEnabled reports whether the style should be compiled.
func (s StyleDefinition) IsEnabled() bool {
	return s.Enabled == nil || *s.Enabled
}

// FilterFileName is the file name (without directory) for a script compiled with this style.
func (s StyleDefinition) FilterFileName(scriptName string) string {
	return fmt.Sprintf("%s (%s).filter", scriptName, sanitizeFileNamePart(s.Suffix))
}

// LoadStyleDefinitions reads every *.toml file in dir, sorted by file name.
// Stylesheet and BaseTypes paths are resolved relative to the style file.
func LoadStyleDefinitions(dir string) ([]StyleDefinition, error) {
	entries, err := os.ReadDir(dir)
	if err != nil {
		return nil, fmt.Errorf("reading styles directory %s: %w", dir, err)
	}

	var styles []StyleDefinition
	for _, entry := range entries {
		if entry.IsDir() || filepath.Ext(entry.Name()) != ".toml" {
			continue
		}
		style, err := loadStyleDefinition(filepath.Join(dir, entry.Name()))
		if err != nil {
			return nil, err
		}
		styles = append(styles, style)
	}
	if len(styles) == 0 {
		return nil, fmt.Errorf("no style files (*.toml) found in %s", dir)
	}
	sort.Slice(styles, func(i, j int) bool { return styles[i].ID < styles[j].ID })

	suffixes := make(map[string]string)
	for _, style := range styles {
		if !style.IsEnabled() {
			continue
		}
		key := strings.ToLower(sanitizeFileNamePart(style.Suffix))
		if other, taken := suffixes[key]; taken {
			return nil, fmt.Errorf("styles %q and %q both use the suffix %q, so their filters would overwrite each other", other, style.ID, style.Suffix)
		}
		suffixes[key] = style.ID
	}
	return styles, nil
}

func loadStyleDefinition(path string) (StyleDefinition, error) {
	contents, err := os.ReadFile(path)
	if err != nil {
		return StyleDefinition{}, fmt.Errorf("reading style %s: %w", path, err)
	}
	return ParseStyleDefinition(path, contents)
}

// ParseStyleDefinition reads a style file's contents as if it were stored at
// path, which is what its Stylesheet and BaseTypes paths are relative to.
func ParseStyleDefinition(path string, contents []byte) (StyleDefinition, error) {
	var style StyleDefinition
	metadata, err := toml.Decode(string(contents), &style)
	if err != nil {
		return style, fmt.Errorf("reading style %s: %w", path, err)
	}
	if err := rejectUndecodedKeys(metadata, path); err != nil {
		return style, err
	}

	style.ID = strings.TrimSuffix(filepath.Base(path), filepath.Ext(path))
	style.Path = path
	if style.Name == "" {
		style.Name = style.ID
	}
	if style.Suffix == "" {
		style.Suffix = style.Name
	}
	if style.Stylesheet == "" {
		return style, fmt.Errorf("style %s: Stylesheet is required", path)
	}
	if style.BaseTypes == "" {
		return style, fmt.Errorf("style %s: BaseTypes is required", path)
	}

	base := filepath.Dir(path)
	style.Stylesheet = resolveConfigurationPath(base, style.Stylesheet)
	style.BaseTypes = resolveConfigurationPath(base, style.BaseTypes)

	style.PaletteGroups = paletteGroups(string(contents), style.Palette)

	for name, value := range style.Palette {
		if _, _, _, _, err := parseHexRGBA(value); err != nil {
			return style, fmt.Errorf("style %s: palette colour %q = %q: %w", path, name, value, err)
		}
	}
	return style, nil
}

// sanitizeFileNamePart replaces characters that are not allowed in Windows file names.
func sanitizeFileNamePart(value string) string {
	return strings.Map(func(r rune) rune {
		if strings.ContainsRune(`<>:"/\|?*`, r) || r < 32 {
			return '-'
		}
		return r
	}, strings.TrimSpace(value))
}

var paletteKeyLine = regexp.MustCompile(`^\s*"?([A-Za-z0-9_-]+)"?\s*=`)

// paletteGroups reads the [Palette] table's layout: a comment line starts a new
// group titled by it, and every key joins the latest group. Keys the layout does
// not reach (for example a palette written as an inline table) end up in "Other".
func paletteGroups(contents string, palette map[string]string) []PaletteGroup {
	var groups []PaletteGroup
	seen := make(map[string]bool)
	inPalette, startNew := false, true
	for _, raw := range strings.Split(contents, "\n") {
		line := strings.TrimSpace(raw)
		if strings.HasPrefix(line, "[") {
			inPalette = line == "[Palette]"
			startNew = true
			continue
		}
		if !inPalette || line == "" {
			startNew = true
			continue
		}
		if strings.HasPrefix(line, "#") {
			if startNew {
				groups = append(groups, PaletteGroup{Title: strings.TrimSpace(strings.TrimPrefix(line, "#"))})
				startNew = false
			}
			continue
		}
		match := paletteKeyLine.FindStringSubmatch(line)
		if match == nil {
			continue
		}
		if len(groups) == 0 {
			groups = append(groups, PaletteGroup{Title: "Palette"})
		}
		groups[len(groups)-1].Vars = append(groups[len(groups)-1].Vars, match[1])
		seen[match[1]] = true
		startNew = false
	}
	var rest []string
	for name := range palette {
		if !seen[name] {
			rest = append(rest, name)
		}
	}
	if len(rest) > 0 {
		sort.Strings(rest)
		groups = append(groups, PaletteGroup{Title: "Other", Vars: rest})
	}
	var kept []PaletteGroup
	for _, group := range groups {
		if len(group.Vars) > 0 {
			kept = append(kept, group)
		}
	}
	return kept
}
