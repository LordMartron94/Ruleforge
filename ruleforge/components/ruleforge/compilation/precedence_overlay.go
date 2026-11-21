package compilation

import (
	"fmt"
	"slices"
	"strings"

	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/config"
)

// maxRollClasses are the item classes whose base defences roll within a range.
// Belts are left out: their defences come from implicits, which the game
// reports as the 100th percentile.
var maxRollClasses = []string{"Body Armours", "Boots", "Gloves", "Helmets", "Shields"}

// maxRolledCondition matches armour whose every base defence rolled its maximum.
const maxRolledCondition = `BaseDefencePercentile == "100" `

// MaxRollPrecedence gives max-rolled armour precedence over every later Show
// rule without changing what is shown. Each Show rule that could match armour
// is preceded by a copy restricted to max-rolled armour. The copy keeps the
// rule's purpose (background and icon shape), takes the max-roll border, and
// takes the higher of the two priority tiers. Hide rules get no copy, so an
// item the filter hides stays hidden however well it rolled.
type MaxRollPrecedence struct {
	style     config.Style
	baseTypes map[string]bool
}

// NewMaxRollPrecedence builds the overlay from the max-roll style and the base
// types of every armour item with rolled defences.
func NewMaxRollPrecedence(style config.Style, armourBaseTypes []string) *MaxRollPrecedence {
	bases := make(map[string]bool, len(armourBaseTypes))
	for _, base := range armourBaseTypes {
		bases[base] = true
	}
	return &MaxRollPrecedence{style: style, baseTypes: bases}
}

// variantConditions returns the conditions of the max-roll copy of a Show rule,
// or false when the rule cannot match armour and needs no copy.
func (m *MaxRollPrecedence) variantConditions(conditions []string) ([]string, bool) {
	variant := make([]string, 0, len(conditions)+2)
	restricted := false
	for _, line := range conditions {
		fields := tokenizeFilterLine(strings.TrimSpace(line))
		if len(fields) == 0 {
			variant = append(variant, line)
			continue
		}
		condition := parseCondition(fields)
		exact := condition.Operator == "=="
		switch condition.Key {
		case "BaseDefencePercentile":
			return nil, false
		case "Rarity":
			if (exact || condition.Operator == "" || condition.Operator == "=") && onlyUnique(condition.Values) {
				return nil, false
			}
		case "Class":
			if !exact {
				return nil, false
			}
			classes := intersect(condition.Values, func(v string) bool { return slices.Contains(maxRollClasses, v) })
			if len(classes) == 0 {
				return nil, false
			}
			line = compiledCondition("Class", "==", classes)
			restricted = true
		case "BaseType":
			if !exact {
				return nil, false
			}
			bases := intersect(condition.Values, func(v string) bool { return m.baseTypes[v] })
			if len(bases) == 0 {
				return nil, false
			}
			line = compiledCondition("BaseType", "==", bases)
			restricted = true
		}
		variant = append(variant, line)
	}
	if !restricted {
		variant = append(variant, compiledCondition("Class", "==", maxRollClasses))
	}
	return append(variant, maxRolledCondition), true
}

// variantStyle keeps the rule's purpose and gives the max-roll style the border
// and, when it ranks higher, the priority channels.
func (m *MaxRollPrecedence) variantStyle(rule config.Style) config.Style {
	result := *rule.Clone()
	result.Name = fmt.Sprintf("%s over %s", m.style.Name, rule.Name)
	result.BorderColor = m.style.BorderColor.Clone()

	if fontSize(m.style) <= fontSize(rule) {
		return result
	}
	// Font size is the priority tier's size, so it decides which tier ranks higher.
	result.TextColor = m.style.TextColor.Clone()
	result.FontSize = m.style.FontSize
	result.DropSound = m.style.DropSound
	result.DropVolume = m.style.DropVolume
	result.Beam = m.style.Beam.Clone()
	minimap := &config.Minimap{}
	if m.style.Minimap != nil {
		minimap = m.style.Minimap.Clone()
	}
	if rule.Minimap != nil && rule.Minimap.Shape != nil {
		minimap.Shape = rule.Minimap.Shape
	}
	result.Minimap = minimap
	return result
}

func fontSize(style config.Style) int {
	if style.FontSize == nil {
		return 32 // the game's default label size
	}
	return *style.FontSize
}

func onlyUnique(values []string) bool {
	for _, value := range values {
		if value != "Unique" {
			return false
		}
	}
	return len(values) > 0
}

func intersect(values []string, keep func(string) bool) []string {
	var kept []string
	for _, value := range values {
		if keep(value) {
			kept = append(kept, value)
		}
	}
	return kept
}

func compiledCondition(key, operator string, values []string) string {
	var builder strings.Builder
	builder.WriteString(key + " ")
	if operator != "" {
		builder.WriteString(operator + " ")
	}
	for _, value := range values {
		builder.WriteString(fmt.Sprintf("%q ", value))
	}
	return builder.String()
}
