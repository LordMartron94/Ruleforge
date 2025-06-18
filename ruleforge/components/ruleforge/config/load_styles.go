package config

import (
	"encoding/json"
	"fmt"
	"os"
	"slices"
)

var allowedMinimapShapes = []string{
	"Circle",
	"Diamond",
	"Hexagon",
	"Square",
	"Star",
	"Triangle",
	"Cross",
	"Moon",
	"Raindrop",
	"Kite",
	"Pentagon",
	"UpsideDownHouse",
}
var allowedColorLiterals = []string{
	"Red",
	"Green",
	"Blue",
	"Brown",
	"White",
	"Yellow",
	"Cyan",
	"Grey",
	"Orange",
	"Pink",
	"Purple",
}

// LoadStyles reads a JSON file at the given path, unmarshals it into
// a map of style‐name → *Style, injects each key into its Style.Name,
// and returns that map.
func LoadStyles(path string) (map[string]*Style, error) {
	data, err := os.ReadFile(path)
	if err != nil {
		return nil, err
	}

	// First unmarshal into a map so we can capture the dynamic keys
	raw := make(map[string]*Style)
	if err := json.Unmarshal(data, &raw); err != nil {
		return nil, err
	}

	// Populate the Name field on each Style
	for name, style := range raw {
		style.Name = name

		if style.Minimap != nil {
			if style.Minimap.Colour != nil && !slices.Contains(allowedColorLiterals, *style.Minimap.Colour) {
				return nil, fmt.Errorf("invalid minimap color: %v", *style.Minimap.Colour)
			}

			if style.Minimap.Shape != nil && !slices.Contains(allowedMinimapShapes, *style.Minimap.Shape) {
				return nil, fmt.Errorf("invalid shape: %v", *style.Minimap.Shape)
			}
		}

		if style.Beam != nil && !slices.Contains(allowedColorLiterals, *style.Beam.Color) {
			return nil, fmt.Errorf("invalid beam color: %v", *style.Beam.Color)
		}
	}

	return raw, nil
}
