package config

import (
	"encoding/json"
	"fmt"
	"os"
	"slices"
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
}

// LoadStyles reads a JSON file and recursively parses it into a flattened map.
func LoadStyles(path string) (map[string]*Style, error) {
	data, err := os.ReadFile(path)
	if err != nil {
		return nil, fmt.Errorf("failed to read style file: %w", err)
	}

	var raw map[string]interface{}
	if err := json.Unmarshal(data, &raw); err != nil {
		return nil, fmt.Errorf("failed to unmarshal styles json: %w", err)
	}

	styles := make(map[string]*Style)
	if err := parseStylesRecursive(raw, "", &styles); err != nil {
		return nil, err
	}

	return styles, nil
}

// isStyleObject checks if a map contains keys that identify it as a Style struct.
func isStyleObject(data map[string]interface{}) bool {
	for key := range data {
		if _, found := knownStyleKeys[key]; found {
			return true
		}
	}
	return false
}

// parseStylesRecursive traverses the raw map, identifies style objects, and populates the results.
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
			return fmt.Errorf("style %q: failed to unmarshal into Style struct: %w", styleName, err)
		}

		style.Name = styleName

		if err := validateStyle(style); err != nil {
			return fmt.Errorf("style %q: %w", styleName, err)
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
		} else {
			return fmt.Errorf("invalid structure: found non-object value at path %q", currentPath)
		}
	}

	return nil
}

func validateStyle(style *Style) error {
	if style.Minimap != nil {
		if style.Minimap.Colour != nil && !slices.Contains(allowedColorLiterals, *style.Minimap.Colour) {
			return fmt.Errorf("invalid minimap color: %v", *style.Minimap.Colour)
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
