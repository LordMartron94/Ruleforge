package config

import "fmt"

type OverrideMap map[string]string

type Color struct {
	Red   *uint8 `json:"red"`
	Green *uint8 `json:"green"`
	Blue  *uint8 `json:"blue"`
	Alpha *uint8 `json:"alpha"`
}

func (c *Color) Clone() *Color {
	if c == nil {
		return nil
	}
	clone := &Color{}
	if c.Red != nil {
		val := *c.Red
		clone.Red = &val
	}
	if c.Green != nil {
		val := *c.Green
		clone.Green = &val
	}
	if c.Blue != nil {
		val := *c.Blue
		clone.Blue = &val
	}
	if c.Alpha != nil {
		val := *c.Alpha
		clone.Alpha = &val
	}
	return clone
}

type Minimap struct {
	Size   *int    `json:"Size,omitempty"`
	Shape  *string `json:"Shape,omitempty"`
	Colour *string `json:"Colour,omitempty"`
}

func (m *Minimap) Clone() *Minimap {
	if m == nil {
		return nil
	}
	clone := &Minimap{}
	if m.Size != nil {
		val := *m.Size
		clone.Size = &val
	}
	if m.Shape != nil {
		val := *m.Shape
		clone.Shape = &val
	}
	if m.Colour != nil {
		val := *m.Colour
		clone.Colour = &val
	}
	return clone
}

type Beam struct {
	Color *string `json:"Color,omitempty"`
	Temp  *bool   `json:"Temp,omitempty"`
}

func (b *Beam) Clone() *Beam {
	if b == nil {
		return nil
	}
	clone := &Beam{}
	if b.Color != nil {
		val := *b.Color
		clone.Color = &val
	}
	if b.Temp != nil {
		val := *b.Temp
		clone.Temp = &val
	}
	return clone
}

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
	Comment         *string  `json:"Comment,omitempty"`
}

func (s *Style) Clone() *Style {
	if s == nil {
		return nil
	}
	clone := &Style{Name: s.Name}
	if s.FontSize != nil {
		val := *s.FontSize
		clone.FontSize = &val
	}
	if s.DropSound != nil {
		val := *s.DropSound
		clone.DropSound = &val
	}
	if s.DropVolume != nil {
		val := *s.DropVolume
		clone.DropVolume = &val
	}
	if s.Comment != nil {
		val := *s.Comment
		clone.Comment = &val
	}
	clone.TextColor = s.TextColor.Clone()
	clone.BorderColor = s.BorderColor.Clone()
	clone.BackgroundColor = s.BackgroundColor.Clone()
	clone.Minimap = s.Minimap.Clone()
	clone.Beam = s.Beam.Clone()
	return clone
}

func (s *Style) MergeStyles(other *Style, overrides OverrideMap) (*Style, error) {
	if s == nil {
		return other.Clone(), nil
	}
	if other == nil {
		return s.Clone(), nil
	}

	result := s.Clone()
	result.Name = "Merged"
	var err error

	if result.TextColor, err = mergeColor(s.TextColor, other.TextColor, s.Name, other.Name, "TextColor", overrides); err != nil {
		return nil, fmt.Errorf("property 'TextColor': %w", err)
	}
	if result.BorderColor, err = mergeColor(s.BorderColor, other.BorderColor, s.Name, other.Name, "BorderColor", overrides); err != nil {
		return nil, fmt.Errorf("property 'BorderColor': %w", err)
	}
	if result.BackgroundColor, err = mergeColor(s.BackgroundColor, other.BackgroundColor, s.Name, other.Name, "BackgroundColor", overrides); err != nil {
		return nil, fmt.Errorf("property 'BackgroundColor': %w", err)
	}
	if result.Minimap, err = mergeMinimap(s.Minimap, other.Minimap, s.Name, other.Name, "Minimap", overrides); err != nil {
		return nil, fmt.Errorf("property 'Minimap': %w", err)
	}
	if result.Beam, err = mergeBeam(s.Beam, other.Beam, s.Name, other.Name, "Beam", overrides); err != nil {
		return nil, fmt.Errorf("property 'Beam': %w", err)
	}

	if other.FontSize != nil {
		if result.FontSize == nil {
			result.FontSize = other.FontSize
		} else if err := handleConflict(overrides, "FontSize", s.Name, other.Name, func() {
			result.FontSize = other.FontSize
		}); err != nil {
			return nil, err
		}
	}
	if other.DropSound != nil {
		if result.DropSound == nil {
			result.DropSound = other.DropSound
		} else if err := handleConflict(overrides, "DropSound", s.Name, other.Name, func() {
			result.DropSound = other.DropSound
		}); err != nil {
			return nil, err
		}
	}
	if other.DropVolume != nil {
		if result.DropVolume == nil {
			result.DropVolume = other.DropVolume
		} else if err := handleConflict(overrides, "DropVolume", s.Name, other.Name, func() {
			result.DropVolume = other.DropVolume
		}); err != nil {
			return nil, err
		}
	}
	if other.Comment != nil && result.Comment == nil {
		result.Comment = other.Comment
	}

	return result, nil
}

func mergeColor(base, other *Color, baseName, otherName, parentPropName string, overrides OverrideMap) (*Color, error) {
	if base == nil {
		return other.Clone(), nil
	}
	if other == nil {
		return base.Clone(), nil
	}
	result := base.Clone()
	if other.Red != nil {
		if result.Red == nil {
			result.Red = other.Red
		} else if err := handleConflict(overrides, parentPropName, baseName, otherName, func() {
			result.Red = other.Red
		}); err != nil {
			return nil, fmt.Errorf("sub-property 'Red': %w", err)
		}
	}
	if other.Green != nil {
		if result.Green == nil {
			result.Green = other.Green
		} else if err := handleConflict(overrides, parentPropName, baseName, otherName, func() {
			result.Green = other.Green
		}); err != nil {
			return nil, fmt.Errorf("sub-property 'Green': %w", err)
		}
	}
	if other.Blue != nil {
		if result.Blue == nil {
			result.Blue = other.Blue
		} else if err := handleConflict(overrides, parentPropName, baseName, otherName, func() {
			result.Blue = other.Blue
		}); err != nil {
			return nil, fmt.Errorf("sub-property 'Blue': %w", err)
		}
	}
	if other.Alpha != nil {
		if result.Alpha == nil {
			result.Alpha = other.Alpha
		} else if err := handleConflict(overrides, parentPropName, baseName, otherName, func() {
			result.Alpha = other.Alpha
		}); err != nil {
			return nil, fmt.Errorf("sub-property 'Alpha': %w", err)
		}
	}
	return result, nil
}

func mergeMinimap(base, other *Minimap, baseName, otherName, parentPropName string, overrides OverrideMap) (*Minimap, error) {
	if base == nil {
		return other.Clone(), nil
	}
	if other == nil {
		return base.Clone(), nil
	}
	result := base.Clone()
	if other.Size != nil {
		if result.Size == nil {
			result.Size = other.Size
		} else if err := handleConflict(overrides, parentPropName, baseName, otherName, func() {
			result.Size = other.Size
		}); err != nil {
			return nil, fmt.Errorf("sub-property 'Size': %w", err)
		}
	}
	if other.Shape != nil {
		if result.Shape == nil {
			result.Shape = other.Shape
		} else if err := handleConflict(overrides, parentPropName, baseName, otherName, func() {
			result.Shape = other.Shape
		}); err != nil {
			return nil, fmt.Errorf("sub-property 'Shape': %w", err)
		}
	}
	if other.Colour != nil {
		if result.Colour == nil {
			result.Colour = other.Colour
		} else if err := handleConflict(overrides, parentPropName, baseName, otherName, func() {
			result.Colour = other.Colour
		}); err != nil {
			return nil, fmt.Errorf("sub-property 'Colour': %w", err)
		}
	}
	return result, nil
}

func mergeBeam(base, other *Beam, baseName, otherName, parentPropName string, overrides OverrideMap) (*Beam, error) {
	if base == nil {
		return other.Clone(), nil
	}
	if other == nil {
		return base.Clone(), nil
	}
	result := base.Clone()
	if other.Color != nil {
		if result.Color == nil {
			result.Color = other.Color
		} else if err := handleConflict(overrides, parentPropName, baseName, otherName, func() {
			result.Color = other.Color
		}); err != nil {
			return nil, fmt.Errorf("sub-property 'Color': %w", err)
		}
	}
	if other.Temp != nil {
		if result.Temp == nil {
			result.Temp = other.Temp
		} else if err := handleConflict(overrides, parentPropName, baseName, otherName, func() {
			result.Temp = other.Temp
		}); err != nil {
			return nil, fmt.Errorf("sub-property 'Temp': %w", err)
		}
	}
	return result, nil
}

func handleConflict(overrides OverrideMap, propName, baseStyleName, otherStyleName string, applyOverride func()) error {
	winner, specificOverrideExists := overrides[propName]
	if specificOverrideExists {
		if winner == otherStyleName {
			applyOverride()
		}
		return nil
	}

	if len(overrides) > 0 {
		return nil
	}

	return fmt.Errorf("property %q has a conflict between styles %q and %q with no !override clause specified", propName, baseStyleName, otherStyleName)
}
