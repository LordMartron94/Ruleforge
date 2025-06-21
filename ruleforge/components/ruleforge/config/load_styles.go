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
	"Combination":     {},
}

// LoadStyles reads a JSON file, recursively parses it, and resolves style combinations.
func LoadStyles(path string) (map[string]*Style, error) {
	data, err := os.ReadFile(path)
	if err != nil {
		return nil, fmt.Errorf("failed to read style file: %w", err)
	}

	var raw map[string]interface{}
	if err := json.Unmarshal(data, &raw); err != nil {
		return nil, fmt.Errorf("failed to unmarshal styles json: %w", err)
	}

	// First pass: Parse all style objects without resolving combinations.
	allStyles := make(map[string]*Style)
	if err := parseStylesRecursive(raw, "", &allStyles); err != nil {
		return nil, err
	}

	// Second pass: Resolve all combinations.
	resolvedStyles := make(map[string]*Style)
	visiting := make(map[string]bool) // For cycle detection

	for name := range allStyles {
		if _, err := resolveCombination(name, allStyles, resolvedStyles, visiting); err != nil {
			return nil, err
		}
	}

	return resolvedStyles, nil
}

// resolveCombination recursively resolves a style and its dependencies.
// It uses memoization (the resolvedStyles map) to avoid re-processing and
// a visiting map to detect cyclical dependencies.
func resolveCombination(
	styleName string,
	allStyles map[string]*Style,
	resolvedStyles map[string]*Style,
	visiting map[string]bool,
) (*Style, error) {
	// 1. Check for cyclical dependencies.
	if visiting[styleName] {
		return nil, fmt.Errorf("circular dependency detected in style combinations involving %q", styleName)
	}
	visiting[styleName] = true
	defer func() { delete(visiting, styleName) }()

	// 2. If already resolved, return from cache.
	if resolved, found := resolvedStyles[styleName]; found {
		return resolved, nil
	}

	// 3. Get the original style definition.
	originalStyle, ok := allStyles[styleName]
	if !ok {
		return nil, fmt.Errorf("referenced style %q not found", styleName)
	}

	// 4. If the style is not a combination, validate and cache it.
	if originalStyle.Combination == nil || len(*originalStyle.Combination) == 0 {
		if err := validateStyle(originalStyle); err != nil {
			return nil, fmt.Errorf("style %q: %w", originalStyle.Name, err)
		}
		resolvedStyles[styleName] = originalStyle
		return originalStyle, nil
	}

	// 5. It's a combination style. Resolve its dependencies and merge them.
	var combinedStyle *Style
	for _, dependencyName := range *originalStyle.Combination {
		dependencyStyle, err := resolveCombination(dependencyName, allStyles, resolvedStyles, visiting)
		if err != nil {
			return nil, fmt.Errorf("failed to resolve dependency %q for style %q: %w", dependencyName, styleName, err)
		}

		if combinedStyle == nil {
			combinedStyle = dependencyStyle.Clone()
		} else {
			combinedStyle.MergeOnto(dependencyStyle)
		}
	}

	// 6. A style with a "Combination" key can have its own properties that override the base.
	// We merge the original style's explicit properties on top of the combined result.
	if combinedStyle == nil {
		combinedStyle = &Style{}
	}
	combinedStyle.MergeOnto(originalStyle)
	combinedStyle.Name = styleName // Ensure the final style has the correct name.

	// 7. Validate and cache the final merged style.
	if err := validateStyle(combinedStyle); err != nil {
		return nil, fmt.Errorf("style %q: %w", styleName, err)
	}
	resolvedStyles[styleName] = combinedStyle
	return combinedStyle, nil
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

		// We no longer validate here, validation happens after combinations are resolved.
		// However, we still add the raw style to the map.
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
