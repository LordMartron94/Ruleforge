package config

import (
	"encoding/json"
	"fmt"
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
	Styles map[string]Style
	Studio StudioSettings
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
	var raw map[string]interface{}
	if _, err := toml.DecodeFile(path, &raw); err != nil {
		return nil, fmt.Errorf("failed to read stylesheet %s: %w", path, err)
	}

	var settings struct {
		Studio StudioSettings `toml:"Studio"`
	}
	if _, err := toml.DecodeFile(path, &settings); err != nil {
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

	return &Stylesheet{Styles: finalStyles, Studio: settings.Studio}, nil
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
