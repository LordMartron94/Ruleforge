package config

import (
	"encoding/json"
	"fmt"
	"github.com/google/uuid"
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
func LoadStyles(path string) (map[string]Style, error) {
	data, err := os.ReadFile(path)
	if err != nil {
		return nil, fmt.Errorf("failed to read style file: %w", err)
	}

	var raw map[string]interface{}
	if err := json.Unmarshal(data, &raw); err != nil {
		return nil, fmt.Errorf("failed to unmarshal styles json: %w", err)
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

	return finalStyles, nil
}

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
