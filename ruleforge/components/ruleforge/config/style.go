package config

// ===================================================================
// Data Structures (Unchanged)
// ===================================================================

type OverrideMap map[string]string

type Color struct {
	Red   *uint8 `json:"red"`
	Green *uint8 `json:"green"`
	Blue  *uint8 `json:"blue"`
	Alpha *uint8 `json:"alpha"`
}

type Minimap struct {
	Size  *int    `json:"Size,omitempty"`
	Shape *string `json:"Shape,omitempty"`
	Color *string `json:"Color,omitempty"`
}

type Beam struct {
	Color *string `json:"Color,omitempty"`
	Temp  *bool   `json:"Temp,omitempty"`
}

type Style struct {
	Name            string    `json:"-"`
	TextColor       *Color    `json:"TextColor,omitempty"`
	BorderColor     *Color    `json:"BorderColor,omitempty"`
	BackgroundColor *Color    `json:"BackgroundColor,omitempty"`
	FontSize        *int      `json:"FontSize,omitempty"`
	Minimap         *Minimap  `json:"Minimap,omitempty"`
	DropSound       *string   `json:"DropSound,omitempty"`
	DropVolume      *int      `json:"DropVolume,omitempty"`
	Beam            *Beam     `json:"Beam,omitempty"`
	Comment         *string   `json:"Comment,omitempty"`
	Combination     *[]string `json:"Combination,omitempty"`
}

// ===================================================================
// Cloning Methods (Unchanged)
// ===================================================================

func (c *Color) Clone() *Color {
	if c == nil {
		return nil
	}
	clone := *c
	return &clone
}

func (m *Minimap) Clone() *Minimap {
	if m == nil {
		return nil
	}
	clone := *m
	return &clone
}

func (b *Beam) Clone() *Beam {
	if b == nil {
		return nil
	}
	clone := *b
	return &clone
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

	if s.Combination != nil {
		newCombination := make([]string, len(*s.Combination))
		copy(newCombination, *s.Combination)
		clone.Combination = &newCombination
	}
	return clone
}

// ===================================================================
// Style Merging Logic (REFACTORED AND FIXED)
// ===================================================================

// shouldApplyOther checks if the 'other' style should win a conflict.
// If no override rule exists, the base wins by default (returns false).
func shouldApplyOther(overrides OverrideMap, propName, otherStyleName string) bool {
	// Comments are a special case; they are often combined rather than conflicting.
	// Here, we assume 'other' always gets to add its comment.
	if propName == "Comment" {
		return true
	}

	winner, ruleExists := overrides[propName]
	if ruleExists {
		return winner == otherStyleName
	}

	// If no override rule exists for a conflicting property, the base style wins by default.
	return false
}

// mergeProperty is a generic helper to merge any simple pointer-based property.
func mergeProperty[T any](base, other *T, propName, otherName string, overrides OverrideMap) *T {
	if other == nil {
		return base // Nothing to merge from other
	}
	if base == nil {
		return other // Base is empty, so we can freely take other's value
	}

	// Both are non-nil, a conflict exists. Decide which one to use.
	if shouldApplyOther(overrides, propName, otherName) {
		return other
	}

	return base
}

func (s *Style) MergeStyles(other *Style, overrides OverrideMap) (*Style, error) {
	if s == nil {
		return other.Clone(), nil
	}
	if other == nil {
		return s.Clone(), nil
	}

	result := &Style{Name: "Merged"}

	// --- Merge Root Properties using the generic helper ---
	result.FontSize = mergeProperty(s.FontSize, other.FontSize, "FontSize", other.Name, overrides)
	result.DropSound = mergeProperty(s.DropSound, other.DropSound, "DropSound", other.Name, overrides)
	result.DropVolume = mergeProperty(s.DropVolume, other.DropVolume, "DropVolume", other.Name, overrides)
	result.Comment = mergeProperty(s.Comment, other.Comment, "Comment", other.Name, overrides)

	// --- Merge Nested Structs using the "build-up" pattern ---
	result.TextColor = mergeColor(s.TextColor, other.TextColor, "TextColor", other.Name, overrides)
	result.BorderColor = mergeColor(s.BorderColor, other.BorderColor, "BorderColor", other.Name, overrides)
	result.BackgroundColor = mergeColor(s.BackgroundColor, other.BackgroundColor, "BackgroundColor", other.Name, overrides)
	result.Minimap = mergeMinimap(s.Minimap, other.Minimap, other.Name, overrides)
	result.Beam = mergeBeam(s.Beam, other.Beam, other.Name, overrides)

	// In this revised logic, conflicts are always resolved, so we don't expect errors.
	// The error return is kept for API compatibility and future validation.
	return result, nil
}

func mergeColor(base, other *Color, parentPropName, otherName string, overrides OverrideMap) *Color {
	if base == nil && other == nil {
		return nil
	}
	if other == nil {
		return base.Clone()
	}
	if base == nil {
		return other.Clone()
	}

	// Both exist, this is a conflict for the whole object.
	if shouldApplyOther(overrides, parentPropName, otherName) {
		return other.Clone()
	}

	return base.Clone()
}

func mergeMinimap(base, other *Minimap, otherName string, overrides OverrideMap) *Minimap {
	if base == nil && other == nil {
		return nil
	}

	result := &Minimap{}

	// Use empty structs for sources if they are nil to avoid nil pointer panics
	baseSource := base
	if baseSource == nil {
		baseSource = &Minimap{}
	}
	otherSource := other
	if otherSource == nil {
		otherSource = &Minimap{}
	}

	// Merge each field using the generic helper
	result.Size = mergeProperty(baseSource.Size, otherSource.Size, "Minimap.Size", otherName, overrides)
	result.Shape = mergeProperty(baseSource.Shape, otherSource.Shape, "Minimap.Shape", otherName, overrides)
	result.Color = mergeProperty(baseSource.Color, otherSource.Color, "Minimap.Color", otherName, overrides)

	// If the resulting struct is empty, return nil to keep the JSON clean
	if result.Size == nil && result.Shape == nil && result.Color == nil {
		return nil
	}

	return result
}

func mergeBeam(base, other *Beam, otherName string, overrides OverrideMap) *Beam {
	if base == nil && other == nil {
		return nil
	}

	result := &Beam{}

	baseSource := base
	if baseSource == nil {
		baseSource = &Beam{}
	}
	otherSource := other
	if otherSource == nil {
		otherSource = &Beam{}
	}

	result.Color = mergeProperty(baseSource.Color, otherSource.Color, "Beam.Color", otherName, overrides)
	result.Temp = mergeProperty(baseSource.Temp, otherSource.Temp, "Beam.Temp", otherName, overrides)

	if result.Color == nil && result.Temp == nil {
		return nil
	}

	return result
}
