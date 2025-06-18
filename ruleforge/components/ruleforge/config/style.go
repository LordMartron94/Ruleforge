package config

// Color represents an RGBA color; any component may be omitted.
type Color struct {
	Red   *uint8 `json:"red"`
	Green *uint8 `json:"green"`
	Blue  *uint8 `json:"blue"`
	Alpha *uint8 `json:"alpha"`
}

// Minimap settings.
type Minimap struct {
	Size   *int    `json:"Size,omitempty"`
	Shape  *string `json:"Shape,omitempty"`
	Colour *string `json:"Colour,omitempty"`
}

// Beam settings.
type Beam struct {
	Color *string `json:"Color,omitempty"`
	Temp  *bool   `json:"Temp,omitempty"`
}

// Style holds all possible fields for a style. Every field is a pointer
// so that absence in the JSON yields a nil value here.
// Name is filled in by the loader based on the JSON map key.
type Style struct {
	Name            string   `json:"-"`
	TextColor       *Color   `json:"TextColor,omitempty"`
	BorderColor     *Color   `json:"BorderColor,omitempty"`
	BackgroundColor *Color   `json:"BackgroundColor,omitempty"`
	FontSize        *int     `json:"FontSize,omitempty"`
	Minimap         *Minimap `json:"Minimap,omitempty"`
	DropSound       *string  `json:"DropSound,omitempty"`
	DropVolume      *int     `json:"DropVolume,omitempty"`
	Beam            *Beam    `json:"Beam,omitempty"`
}
