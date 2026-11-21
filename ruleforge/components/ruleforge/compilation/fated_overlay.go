package compilation

import (
	"slices"
	"strings"

	model "github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/compilation/model"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/config"
)

var fatedClasses = []string{"Amulets", "Belts", "Body Armours", "Boots", "Gloves", "Helmets", "Quivers", "Rings", "Shields", "Bows", "Claws", "Daggers", "Rune Daggers", "Sceptres", "Staves", "Warstaves", "Wands", "One Hand Axes", "One Hand Maces", "One Hand Swords", "Thrusting One Hand Swords", "Two Hand Axes", "Two Hand Maces", "Two Hand Swords", "Jewels", "Abyss Jewels"}

func (r *RuleFactory) constructDecoratedBlock(action model.RuleType, style config.Style, conditions []string, source string) []string {
	if action != model.ShowRule || r.fated == nil || higherPropertyBorder(style) {
		return r.constructBlock(action, style, conditions, source)
	}
	variant := append([]string(nil), conditions...)
	restricted := false
	rarityRestricted := false
	for i, line := range conditions {
		fields := tokenizeFilterLine(strings.TrimSpace(line))
		if len(fields) == 0 {
			continue
		}
		c := parseCondition(fields)
		switch c.Key {
		case "Fated":
			return r.constructBlock(action, style, conditions, source)
		case "Rarity":
			if !rareAllowed(c) {
				return r.constructBlock(action, style, conditions, source)
			}
			variant[i] = compiledCondition("Rarity", "==", []string{"Rare"})
			rarityRestricted = true
		case "Class":
			if c.Operator != "==" {
				return r.constructBlock(action, style, conditions, source)
			}
			keep := intersect(c.Values, func(v string) bool { return slices.Contains(fatedClasses, v) })
			if len(keep) == 0 {
				return r.constructBlock(action, style, conditions, source)
			}
			variant[i] = compiledCondition("Class", "==", keep)
			restricted = true
		case "BaseType":
			if c.Operator != "==" {
				return r.constructBlock(action, style, conditions, source)
			}
			keep := intersect(c.Values, func(v string) bool { return r.gearBases[v] })
			if len(keep) == 0 {
				return r.constructBlock(action, style, conditions, source)
			}
			variant[i] = compiledCondition("BaseType", "==", keep)
			restricted = true
		}
	}
	if !restricted {
		variant = append(variant, compiledCondition("Class", "==", fatedClasses))
	}
	variant = append(variant, compiledCondition("Fated", "", []string{"True"}))
	if !rarityRestricted {
		variant = append(variant, compiledCondition("Rarity", "==", []string{"Rare"}))
	}
	decorated := *style.Clone()
	decorated.Name = "Fated property over " + style.Name
	decorated.BorderColor = r.fated.BorderColor.Clone()
	out := r.constructBlock(action, decorated, variant, "Fated property over: "+source)
	return append(out, r.constructBlock(action, style, conditions, source)...)
}

func higherPropertyBorder(style config.Style) bool {
	if style.BorderColor == nil {
		return false
	}
	variable := style.BorderColor.Var
	return strings.HasPrefix(variable, "rad-") || slices.Contains([]string{"bd-max-rolled", "bd-fractured", "bd-corrupted", "bd-veiled"}, variable)
}

func rareAllowed(c StudioCondition) bool {
	ranks := map[string]int{"Normal": 0, "Magic": 1, "Rare": 2, "Unique": 3}
	for _, v := range c.Values {
		n, ok := ranks[v]
		if !ok {
			continue
		}
		switch c.Operator {
		case "", "=", "==":
			if n == 2 {
				return true
			}
		case "!", "!=":
			if n != 2 {
				return true
			}
		case ">":
			if 2 > n {
				return true
			}
		case ">=":
			if 2 >= n {
				return true
			}
		case "<":
			if 2 < n {
				return true
			}
		case "<=":
			if 2 <= n {
				return true
			}
		}
	}
	return false
}
