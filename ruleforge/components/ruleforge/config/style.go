package config

import "fmt"

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

// MergeStyles deep-merges 'style' with 'other', returning a new Style.
// Any field set (non-nil) in both will cause an error.
// Name is treated as metadata: if both names are set and differ, it’s a conflict.
func (s *Style) MergeStyles(other *Style) (*Style, error) {
	if s == nil && other == nil {
		return nil, nil
	}
	result := &Style{}

	// --- Name ---
	result.Name = "Merged"

	// --- Colors ---
	var err error
	if result.TextColor, err = mergeColor(s.TextColor, other.TextColor); err != nil {
		return nil, fmt.Errorf("TextColor: %w", err)
	}
	if result.BorderColor, err = mergeColor(s.BorderColor, other.BorderColor); err != nil {
		return nil, fmt.Errorf("BorderColor: %w", err)
	}
	if result.BackgroundColor, err = mergeColor(s.BackgroundColor, other.BackgroundColor); err != nil {
		return nil, fmt.Errorf("BackgroundColor: %w", err)
	}

	// --- FontSize ---
	if result.FontSize, err = mergePtrInt(s.FontSize, other.FontSize); err != nil {
		return nil, fmt.Errorf("FontSize: %w", err)
	}

	// --- Minimap ---
	if result.Minimap, err = mergeMinimap(s.Minimap, other.Minimap); err != nil {
		return nil, fmt.Errorf("minimap: %w", err)
	}

	// --- DropSound & Volume ---
	if result.DropSound, err = mergePtrString(s.DropSound, other.DropSound); err != nil {
		return nil, fmt.Errorf("DropSound: %w", err)
	}
	if result.DropVolume, err = mergePtrInt(s.DropVolume, other.DropVolume); err != nil {
		return nil, fmt.Errorf("DropVolume: %w", err)
	}

	// --- Beam ---
	if result.Beam, err = mergeBeam(s.Beam, other.Beam); err != nil {
		return nil, fmt.Errorf("beam: %w", err)
	}

	return result, nil
}

func mergeColor(a, b *Color) (*Color, error) {
	if a == nil {
		return b, nil
	}
	if b == nil {
		return a, nil
	}
	// both non-nil → deep-merge
	out := &Color{}
	var err error
	if out.Red, err = mergePtrUint8(a.Red, b.Red); err != nil {
		return nil, fmt.Errorf("red: %w", err)
	}
	if out.Green, err = mergePtrUint8(a.Green, b.Green); err != nil {
		return nil, fmt.Errorf("green: %w", err)
	}
	if out.Blue, err = mergePtrUint8(a.Blue, b.Blue); err != nil {
		return nil, fmt.Errorf("blue: %w", err)
	}
	if out.Alpha, err = mergePtrUint8(a.Alpha, b.Alpha); err != nil {
		return nil, fmt.Errorf("alpha: %w", err)
	}
	return out, nil
}

func mergeMinimap(a, b *Minimap) (*Minimap, error) {
	if a == nil {
		return b, nil
	}
	if b == nil {
		return a, nil
	}
	out := &Minimap{}
	var err error
	if out.Size, err = mergePtrInt(a.Size, b.Size); err != nil {
		return nil, fmt.Errorf("size: %w", err)
	}
	if out.Shape, err = mergePtrString(a.Shape, b.Shape); err != nil {
		return nil, fmt.Errorf("shape: %w", err)
	}
	if out.Colour, err = mergePtrString(a.Colour, b.Colour); err != nil {
		return nil, fmt.Errorf("colour: %w", err)
	}
	return out, nil
}

func mergeBeam(a, b *Beam) (*Beam, error) {
	if a == nil {
		return b, nil
	}
	if b == nil {
		return a, nil
	}
	out := &Beam{}
	var err error
	if out.Color, err = mergePtrString(a.Color, b.Color); err != nil {
		return nil, fmt.Errorf("color: %w", err)
	}
	if out.Temp, err = mergePtrBool(a.Temp, b.Temp); err != nil {
		return nil, fmt.Errorf("temp: %w", err)
	}
	return out, nil
}

// mergePtrX returns the non-nil ptr, or errors if both are non-nil.
func mergePtrInt(a, b *int) (*int, error) {
	if a != nil && b != nil {
		return nil, fmt.Errorf("conflict: both values present (%d vs %d)", *a, *b)
	}
	if a != nil {
		return a, nil
	}
	return b, nil
}

func mergePtrString(a, b *string) (*string, error) {
	if a != nil && b != nil {
		return nil, fmt.Errorf("conflict: both values present (%q vs %q)", *a, *b)
	}
	if a != nil {
		return a, nil
	}
	return b, nil
}

func mergePtrBool(a, b *bool) (*bool, error) {
	if a != nil && b != nil {
		return nil, fmt.Errorf("conflict: both values present (%t vs %t)", *a, *b)
	}
	if a != nil {
		return a, nil
	}
	return b, nil
}

func mergePtrUint8(a, b *uint8) (*uint8, error) {
	if a != nil && b != nil {
		return nil, fmt.Errorf("conflict: both values present (%d vs %d)", *a, *b)
	}
	if a != nil {
		return a, nil
	}
	return b, nil
}
