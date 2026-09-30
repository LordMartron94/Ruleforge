package config

import (
	"encoding/json"
	"fmt"
	"os"
	"slices"
	"strconv"
	"strings"

	"github.com/BurntSushi/toml"
	"github.com/google/uuid"
)

var allowedMinimapShapes = []string{
	"Circle", "Diamond", "Hexagon", "Square", "Star", "Triangle",
	"Cross", "Moon", "Raindrop", "Kite", "Pentagon", "UpsideDownHouse",
}
var allowedColorLiterals = []string{
	"Red", "Green", "Blue", "Brown", "White", "Yellow",
	"Cyan", "Grey", "Orange", "Pink", "Purple",
}

// knownStyleKeys is a set of top-level keys that identify an object as a Style.
// We use this to know when to stop recursing.
var knownStyleKeys = map[string]struct{}{
	"TextColor":       {},
	"BorderColor":     {},
	"BackgroundColor": {},
	"FontSize":        {},
	"Minimap":         {},
	"DropSound":       {},
	"DropVolume":      {},
	"Beam":            {},
	"Comment":         {},
	"Combination":     {},
}

// colorKeys are the style properties that hold a colour.
var colorKeys = map[string]struct{}{
	"TextColor":       {},
	"BorderColor":     {},
	"BackgroundColor": {},
}

// studioKey is the top-level stylesheet table with studio settings instead of styles.
const studioKey = "Studio"

// Stylesheet is a loaded stylesheet: every resolved style plus the studio settings it declares.
type Stylesheet struct {
	Styles  map[string]Style
	Studio  StudioSettings
	Sources map[string]StyleSource
}

// StudioSettings configures the checks the Ruleforge Studio runs on a palette.
type StudioSettings struct {
	// Distinct lists groups of palette colours that must stay perceptually apart.
	Distinct map[string]DistinctGroup `toml:"Distinct"`
}

// DistinctGroup is a set of palette colours whose pairwise CIEDE2000 distance should be at least Min.
type DistinctGroup struct {
	Min  float64  `toml:"Min"`
	Vars []string `toml:"Vars"`
}

// LoadStyles reads a TOML stylesheet and resolves its colours against palette.
func LoadStyles(path string, palette map[string]string) (map[string]Style, error) {
	sheet, err := LoadStylesheet(path, palette)
	if err != nil {
		return nil, err
	}
	return sheet.Styles, nil
}

// LoadStylesheet reads a TOML stylesheet, recursively parses it, resolves its colours
// against palette, and resolves style combinations.
//
// A colour is written as a palette name ("t2"), a literal ("#RRGGBB" or "#RRGGBBAA"),
// { var = "t2" }, or { red = .., green = .., blue = .., alpha = .. }.
//
//goland:noinspection t
func LoadStylesheet(path string, palette map[string]string) (*Stylesheet, error) {
	contents, err := os.ReadFile(path)
	if err != nil {
		return nil, fmt.Errorf("failed to read stylesheet %s: %w", path, err)
	}
	return ParseStylesheet(path, contents, palette)
}

// ParseStylesheet is LoadStylesheet for a stylesheet's contents; path only names it in errors.
//
//goland:noinspection t
func ParseStylesheet(path string, contents []byte, palette map[string]string) (*Stylesheet, error) {
	var raw map[string]interface{}
	if _, err := toml.Decode(string(contents), &raw); err != nil {
		return nil, fmt.Errorf("failed to read stylesheet %s: %w", path, err)
	}

	var settings struct {
		Studio StudioSettings `toml:"Studio"`
	}
	if _, err := toml.Decode(string(contents), &settings); err != nil {
		return nil, fmt.Errorf("failed to read studio settings in %s: %w", path, err)
	}
	delete(raw, studioKey)

	if err := resolveVarColorsInTree(raw, palette); err != nil {
		return nil, fmt.Errorf("stylesheet %s: %w", path, err)
	}

	allStyles := make(map[string]*Style)
	if err := parseStylesRecursive(raw, "", &allStyles); err != nil {
		return nil, err
	}

	resolvedStylesCache := make(map[string]*Style)
	visiting := make(map[string]bool)

	for name := range allStyles {
		if _, err := resolveCombination(name, allStyles, resolvedStylesCache, visiting); err != nil {
			return nil, err
		}
	}

	// Convert the pointer map to a value map for the final result.
	finalStyles := make(map[string]Style, len(resolvedStylesCache))
	for name, stylePtr := range resolvedStylesCache {
		finalStyles[name] = *stylePtr
	}

	return &Stylesheet{Styles: finalStyles, Studio: settings.Studio, Sources: computeSources(allStyles)}, nil
}

//goland:noinspection t
func resolveCombination(
	styleName string,
	allStyles map[string]*Style,
	resolvedStyles map[string]*Style,
	visiting map[string]bool,
) (*Style, error) {
	if visiting[styleName] {
		return nil, fmt.Errorf("circular dependency detected in style combinations involving %q", styleName)
	}
	visiting[styleName] = true
	defer func() { delete(visiting, styleName) }()

	if resolved, found := resolvedStyles[styleName]; found {
		return resolved, nil
	}

	originalStyle, ok := allStyles[styleName]
	if !ok {
		return nil, fmt.Errorf("referenced style %q not found", styleName)
	}

	if originalStyle.Id == "" {
		originalStyle.Id = uuid.New().String()
	}

	if originalStyle.Combination == nil || len(*originalStyle.Combination) == 0 {
		if err := validateStyle(originalStyle); err != nil {
			return nil, fmt.Errorf("style %q: %w", originalStyle.Name, err)
		}
		resolvedStyles[styleName] = originalStyle
		return originalStyle, nil
	}

	var combinedStyle *Style
	var err error
	for _, dependencyName := range *originalStyle.Combination {
		dependencyStyle, err := resolveCombination(dependencyName, allStyles, resolvedStyles, visiting)
		if err != nil {
			return nil, fmt.Errorf("failed to resolve dependency %q for style %q: %w", dependencyName, styleName, err)
		}

		if combinedStyle == nil {
			combinedStyle = dependencyStyle.Clone()
		} else {
			combinedStyle, err = dependencyStyle.MergeStyles(combinedStyle, make(OverrideMap))
			if err != nil {
				return nil, fmt.Errorf("unexpected error merging existing combination into dependency %q for style %q: %w", dependencyName, styleName, err)
			}
		}
	}

	if combinedStyle == nil {
		combinedStyle = &Style{}
	}

	localProperties := originalStyle.Clone()
	localProperties.Combination = nil

	finalStyle, err := localProperties.MergeStyles(combinedStyle, make(OverrideMap))
	if err != nil {
		return nil, fmt.Errorf("unexpected error merging combined dependencies into local properties for style %q: %w", styleName, err)
	}
	finalStyle.Name = styleName

	var canonicalStyle *Style
	for _, existingStyle := range resolvedStyles {
		if finalStyle.IsEqual(existingStyle) {
			canonicalStyle = existingStyle
			break
		}
	}

	if canonicalStyle != nil {
		finalStyle = canonicalStyle
	}

	if err := validateStyle(finalStyle); err != nil {
		return nil, fmt.Errorf("style %q: %w", finalStyle.Name, err)
	}

	resolvedStyles[styleName] = finalStyle
	return finalStyle, nil
}

func isStyleObject(data map[string]interface{}) bool {
	for key := range data {
		if _, found := knownStyleKeys[key]; found {
			return true
		}
	}
	return false
}

//goland:noinspection t
func parseStylesRecursive(data map[string]interface{}, prefix string, styles *map[string]*Style) error {
	if isStyleObject(data) {
		styleName := prefix
		if styleName == "" {
			return fmt.Errorf("found a style object at the root of the JSON without a name")
		}
		styleBytes, err := json.Marshal(data)
		if err != nil {
			return fmt.Errorf("style %q: failed to re-marshal: %w", styleName, err)
		}
		var style *Style
		if err := json.Unmarshal(styleBytes, &style); err != nil {
			return fmt.Errorf("style %q: failed to unmarshal into StyleID struct: %w", styleName, err)
		}
		(*styles)[styleName] = style
		return nil
	}

	for key, value := range data {
		currentPath := key
		if prefix != "" {
			currentPath = prefix + "/" + key
		}
		if nestedMap, ok := value.(map[string]interface{}); ok {
			if err := parseStylesRecursive(nestedMap, currentPath, styles); err != nil {
				return err
			}
		}
	}
	return nil
}

//goland:noinspection t
func validateStyle(style *Style) error {
	if style.Minimap != nil {
		if style.Minimap.Color != nil && !slices.Contains(allowedColorLiterals, *style.Minimap.Color) {
			return fmt.Errorf("invalid minimap color: %v", *style.Minimap.Color)
		}
		if style.Minimap.Shape != nil && !slices.Contains(allowedMinimapShapes, *style.Minimap.Shape) {
			return fmt.Errorf("invalid minimap shape: %v", *style.Minimap.Shape)
		}
	}
	if style.Beam != nil && style.Beam.Color != nil && !slices.Contains(allowedColorLiterals, *style.Beam.Color) {
		return fmt.Errorf("invalid beam color: %v", *style.Beam.Color)
	}
	return nil
}

// resolveVarColorsInTree turns every colour in the stylesheet into an RGBA table.
// Colours that came from the palette keep their palette name under "var", so the
// studio can recolour them when the palette changes.
//
//goland:noinspection t
func resolveVarColorsInTree(node interface{}, palette map[string]string) error {
	switch val := node.(type) {
	case map[string]interface{}:
		for key, child := range val {
			if _, isColor := colorKeys[key]; isColor {
				resolved, err := resolveColor(child, palette)
				if err != nil {
					return fmt.Errorf("%s: %w", key, err)
				}
				val[key] = resolved
				continue
			}
			if err := resolveVarColorsInTree(child, palette); err != nil {
				return err
			}
		}
	case []interface{}:
		for _, item := range val {
			if err := resolveVarColorsInTree(item, palette); err != nil {
				return err
			}
		}
	case []map[string]interface{}:
		for _, item := range val {
			if err := resolveVarColorsInTree(item, palette); err != nil {
				return err
			}
		}
	}
	return nil
}

func resolveColor(value interface{}, palette map[string]string) (map[string]interface{}, error) {
	var name, hex string
	switch v := value.(type) {
	case string:
		if strings.HasPrefix(v, "#") {
			hex = v
		} else {
			name = v
		}
	case map[string]interface{}:
		reference, hasVar := v["var"].(string)
		if !hasVar {
			return v, nil
		}
		name = reference
	default:
		return nil, fmt.Errorf("unsupported colour value %v", value)
	}

	if name != "" {
		found, ok := palette[name]
		if !ok {
			return nil, fmt.Errorf("colour %q is not in the style's palette", name)
		}
		hex = found
	}
	r, g, b, a, err := parseHexRGBA(hex)
	if err != nil {
		return nil, fmt.Errorf("invalid colour %q: %w", hex, err)
	}
	resolved := map[string]interface{}{"red": uint8(r), "green": uint8(g), "blue": uint8(b), "alpha": uint8(a)}
	if name != "" {
		resolved["var"] = name
	}
	return resolved, nil
}

func parseHexRGBA(hex string) (r, g, b, a uint64, err error) {
	if len(hex) == 0 || hex[0] != '#' {
		return 0, 0, 0, 0, fmt.Errorf("missing “#”")
	}
	hex = hex[1:]

	switch len(hex) {
	case 6:
		hex += "FF"
	case 8:
	default:
		return 0, 0, 0, 0, fmt.Errorf("hex must be 6 or 8 digits")
	}

	// each pair is one channel
	r, err = strconv.ParseUint(hex[0:2], 16, 8)
	if err != nil {
		return
	}
	g, err = strconv.ParseUint(hex[2:4], 16, 8)
	if err != nil {
		return
	}
	b, err = strconv.ParseUint(hex[4:6], 16, 8)
	if err != nil {
		return
	}
	a, err = strconv.ParseUint(hex[6:8], 16, 8)
	return
}

// StyleSource records how a style was composed: the styles it combines, in
// order, the channels it sets itself, and which style supplied each channel of
// the resolved result. Channel names match the studio's look fields.
type StyleSource struct {
	Combination []string          `json:"combination,omitempty"`
	Own         []string          `json:"own,omitempty"`
	Channels    map[string]string `json:"channels"`
}

// ownChannels lists the channels a style sets itself, before any combination.
func ownChannels(style *Style) []string {
	var channels []string
	add := func(set bool, name string) {
		if set {
			channels = append(channels, name)
		}
	}
	add(style.TextColor != nil, "text")
	add(style.BackgroundColor != nil, "background")
	add(style.BorderColor != nil, "border")
	add(style.FontSize != nil, "fontSize")
	if style.Beam != nil {
		add(style.Beam.Color != nil, "beam")
		add(style.Beam.Temp != nil, "beamTemp")
	}
	if style.Minimap != nil {
		add(style.Minimap.Size != nil, "minimapSize")
		add(style.Minimap.Color != nil, "minimapColor")
		add(style.Minimap.Shape != nil, "minimapShape")
	}
	add(style.DropSound != nil, "sound")
	add(style.DropVolume != nil, "volume")
	return channels
}

// computeSources mirrors resolveCombination's precedence: later combined styles
// win, and a style's own channels win over everything it combines.
func computeSources(all map[string]*Style) map[string]StyleSource {
	sources := make(map[string]StyleSource, len(all))
	var visit func(name string) StyleSource
	visit = func(name string) StyleSource {
		if source, done := sources[name]; done {
			return source
		}
		style := all[name]
		source := StyleSource{Channels: make(map[string]string)}
		if style == nil {
			return source
		}
		if style.Combination != nil {
			source.Combination = append([]string(nil), *style.Combination...)
			for _, dependency := range source.Combination {
				for channel, owner := range visit(dependency).Channels {
					source.Channels[channel] = owner
				}
			}
		}
		source.Own = ownChannels(style)
		for _, channel := range source.Own {
			source.Channels[channel] = name
		}
		sources[name] = source
		return source
	}
	for name := range all {
		visit(name)
	}
	return sources
}

// MinimapShapes lists the minimap icon shapes a style can use.
func MinimapShapes() []string { return append([]string(nil), allowedMinimapShapes...) }

// ColourLiterals lists the named colours of minimap icons and beams.
func ColourLiterals() []string { return append([]string(nil), allowedColorLiterals...) }
