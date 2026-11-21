package compilation

import (
	"fmt"
	"strconv"

	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/parsing/shared"
	model "github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/compilation/model"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/config"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/rules/symbols"
)

var tieredExoticBases = []string{"Astrolabe Amulet", "Cogwork Ring", "Composite Ring", "Cord Belt", "Focused Amulet", "Geodesic Ring", "Helical Ring", "Manifold Ring", "Ratcheting Ring", "Simplex Amulet", "Stygian Vise"}

// handleItemTierMatrix crosses game tiers with build use, base quality and
// special classes. It must precede ordinary equipment/jewellery show/hide rules.
func (rg *RuleGenerator) handleItemTierMatrix(variables *map[string][]string, parameters []*shared.ParseTree[symbols.LexingTokenType]) ([][]string, error) {
	styles := map[int]*config.Style{}
	for _, parameter := range parameters {
		key, value := rg.getKeyAndValueFromParameter(parameter)
		var tier int
		if _, err := fmt.Sscanf(key, "$tier_%d", &tier); err != nil || tier < 2 || tier > 5 {
			return nil, fmt.Errorf("item_tier_matrix expects $tier_2 through $tier_5")
		}
		style, err := rg.styleManager.GetStyle(value)
		if err != nil {
			return nil, err
		}
		styles[tier] = style
	}
	for tier := 2; tier <= 5; tier++ {
		if styles[tier] == nil {
			return nil, fmt.Errorf("item_tier_matrix missing $tier_%d", tier)
		}
	}
	var out [][]string
	properties := []struct{ path, key, op, value string }{
		{"Final/Equipment/Fractured", "@fractured", "==", "True"},
		{"Final/Equipment/Corrupted", "@corrupted_mods", ">=", "1"},
		{"Final/Equipment/Veiled", "@has_explicit_mod", "", "Veil"},
	}
	propertyStyles := map[string]*config.Style{}
	for _, p := range properties {
		style, err := rg.styleManager.GetStyle(p.path)
		if err != nil {
			return nil, err
		}
		propertyStyles[p.path] = style
	}
	appendRule := func(rule *model.ParsedRule) {
		for _, p := range properties {
			variant := *rule
			style := *rule.Style.Clone()
			property := propertyStyles[p.path]
			style.Name = property.Name + " property over " + rule.Style.Name
			style.BorderColor = property.BorderColor.Clone()
			variant.Style = &style
			variant.Conditions = append(append([]model.Condition(nil), rule.Conditions...), model.Condition{Identifier: p.key, Operator: p.op, Value: []string{p.value}})
			if p.key == "@has_explicit_mod" {
				variant.Conditions = append(variant.Conditions, model.Condition{Identifier: "@identified", Operator: "==", Value: []string{"True"}})
			}
			out = append(out, rg.compileParsedRule(&variant, rg.sectionConditions)...)
		}
		out = append(out, rg.compileParsedRule(rule, rg.sectionConditions)...)
	}
	emit := func(tier int, conditions ...model.Condition) {
		base := []model.Condition{{Identifier: "@rarity", Operator: "==", Value: []string{"Magic", "Rare"}}, {Identifier: "@unidentified_item_tier", Operator: ">=", Value: []string{strconv.Itoa(tier)}}}
		rule := &model.ParsedRule{Action: model.ShowRule, Style: styles[tier], Conditions: append(base, conditions...), Variables: variables, ValidBaseTypes: rg.validBaseTypes}
		appendRule(rule)
	}
	cond := func(key, op string, values ...string) model.Condition {
		return model.Condition{Identifier: key, Operator: op, Value: values}
	}
	// Rank special jewellery independently of @class_use and old crafting ilvls.
	emit(5, cond("@item_type", "==", tieredExoticBases...))
	emit(5, cond("@class_use", "==", "true"))
	emit(4, cond("@item_type", "==", tieredExoticBases...))
	emit(4, cond("@class_use", "==", "true"))
	emit(3, cond("@item_type", "==", tieredExoticBases...))
	emit(3, cond("@item_class", "==", "Jewels", "Abyss Jewels", "Utility Flasks", "Heist Brooches", "Heist Cloaks", "Heist Gear", "Heist Tools"))
	emit(3, cond("@area_level", "<", "68"), cond("@class_use", "==", "true"))
	emit(3, cond("@area_level", ">=", "68"), cond("@drop_level", ">=", "60"), cond("@class_use", "==", "true"))
	// Best known armour bases can still be upgrades at game tier 2.
	var bases []string
	for _, row := range rg.baseTypeData {
		if row.Category == "Endgame Rare Bases" {
			bases = append(bases, row.BaseType)
		}
	}
	if len(bases) > 0 {
		emit(2, cond("@item_type", "==", bases...), cond("@class_use", "==", "true"))
	}
	emit(2, cond("@area_level", "<", "68"), cond("@class_use", "==", "true"))
	// Preserve tier-5 off-build gear as crafting/trade possibilities without
	// elevating every lower-tier off-build drop to the same priority.
	rule := &model.ParsedRule{Action: model.ShowRule, Style: styles[3], Conditions: []model.Condition{cond("@rarity", "==", "Magic", "Rare"), cond("@unidentified_item_tier", ">=", "5"), cond("@item_class", "==", "Amulets", "Belts", "Body Armours", "Boots", "Gloves", "Helmets", "Quivers", "Rings", "Shields", "Bows", "Claws", "Daggers", "Rune Daggers", "Sceptres", "Staves", "Warstaves", "Wands", "One Hand Axes", "One Hand Maces", "One Hand Swords", "Thrusting One Hand Swords", "Two Hand Axes", "Two Hand Maces", "Two Hand Swords")}, Variables: variables, ValidBaseTypes: rg.validBaseTypes}
	appendRule(rule)
	return out, nil
}
