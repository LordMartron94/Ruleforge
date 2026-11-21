package compilation

import (
	"fmt"
	"sort"

	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/parsing/shared"
	model2 "github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/compilation/model"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/rules/symbols"
)

// MacroSpec describes a macro for editors.
type MacroSpec struct {
	Name    string       `json:"name"`
	Summary string       `json:"summary"`
	Params  []MacroParam `json:"params"`
	// Tiers says the macro takes an ordered list of tier styles, best first,
	// under any parameter names other than its named parameters.
	Tiers bool `json:"tiers,omitempty"`
}

// MacroParam is one named macro parameter.
type MacroParam struct {
	// Key is the parameter, such as "$style". A key ending in "_N" is a
	// pattern: "$quality_N" stands for $quality_1, $quality_20 and so on.
	Key string `json:"key"`
	// Value is what the parameter takes: "style" or "category" (a base type
	// automation category).
	Value    string `json:"value"`
	Required bool   `json:"required,omitempty"`
	Summary  string `json:"summary"`
}

var progressionParams = []MacroParam{
	{Key: "$show_normal", Value: "style", Required: true, Summary: "Normal items still worth showing at this level."},
	{Key: "$show_magic", Value: "style", Required: true, Summary: "Magic items still worth showing."},
	{Key: "$show_rare", Value: "style", Required: true, Summary: "Rare items still worth showing."},
	{Key: "$hidden_normal", Value: "style", Required: true, Summary: "Outdated normal items (hidden)."},
	{Key: "$hidden_magic", Value: "style", Required: true, Summary: "Outdated magic items (hidden)."},
	{Key: "$hidden_rare", Value: "style", Required: true, Summary: "Outdated rare items (hidden)."},
	{Key: "$max_roll", Value: "style", Summary: "Optional style for max-rolled bases the progression shows."},
}

var tieringParams = []MacroParam{
	{Key: "$fallback", Value: "style", Summary: "Optional style after the tiers, for items without a price."},
	{Key: "$quality_N", Value: "style", Summary: "Optional style combined with every tier for items with at least N% quality."},
}

// MacroSpecs lists every macro a script can call.
var MacroSpecs = []MacroSpec{
	{Name: "economy_csv", Summary: "Economy-tiers a CSV category when credible prices exist; preserves curated CSV rules as fallback.", Tiers: true, Params: []MacroParam{{Key: "$category", Value: "category", Required: true, Summary: "CSV category to tier."}}},
	{Name: "viscera_preferences", Summary: "Highlights the build preset's explicitly preferred Viscera.", Params: []MacroParam{{Key: "$style", Value: "style", Required: true, Summary: "Preferred Viscera priority."}}},
	{Name: "item_tier_matrix", Summary: "Prioritizes tiered magic and rare gear by build use, base quality and exotic identity.", Params: []MacroParam{
		{Key: "$tier_5", Value: "style", Required: true, Summary: "Game tier 5 on useful bases."},
		{Key: "$tier_4", Value: "style", Required: true, Summary: "Game tier 4 on useful bases."},
		{Key: "$tier_3", Value: "style", Required: true, Summary: "Game tier 3 on useful bases and tier-5 off-build gear."},
		{Key: "$tier_2", Value: "style", Required: true, Summary: "Game tier 2 campaign upgrades and selected endgame bases."},
	}},
	{Name: "fated_precedence", Summary: "Marks Fated items among later Show rules; never changes visibility.", Params: []MacroParam{{Key: "$style", Value: "style", Required: true, Summary: "Fated border style."}}},
	{Name: "item_progression-equipment-leveling", Summary: "Shows the build's weapons and armour while they are level-appropriate in the campaign (area levels 1-67) and hides them once outdated.", Params: progressionParams},
	{Name: "item_progression-equipment-mapping", Summary: "The same progression for maps (area levels 68-84).", Params: progressionParams},
	{Name: "item_progression-flasks", Summary: "Shows each flask kind while it is the best available and hides it once outdated.", Params: []MacroParam{
		{Key: "$show", Value: "style", Required: true, Summary: "Current flasks."},
		{Key: "$hidden", Value: "style", Required: true, Summary: "Outdated flasks (hidden)."},
	}},
	{Name: "unique_tiering", Summary: "Tiers unique items by their poe.ninja price.", Tiers: true, Params: tieringParams},
	{Name: "skill_gem_tiering", Summary: "Tiers skill and support gems by their poe.ninja price.", Tiers: true, Params: append(append([]MacroParam{}, tieringParams...),
		MacroParam{Key: "$vaal", Value: "style", Summary: "Optional border for Vaal gems; wins over quality."})},
	{Name: "divination_card_tiering", Summary: "Tiers divination cards by their poe.ninja price.", Tiers: true, Params: tieringParams},
	{Name: "tattoo_tiering", Summary: "Tiers Trial of the Ancestors tattoos by their poe.ninja price.", Tiers: true, Params: tieringParams},
	{Name: "currency_tiering", Summary: "Tiers the configured currency profiles by poe.ninja value, rarity and trend blended with their crafting rating. Stacks worth more rank higher.", Tiers: true, Params: []MacroParam{
		{Key: "$hidden", Value: "style", Summary: "Optional bottom tier that hides, in this style; stacks worth more still show."},
	}},
	{Name: "handle_csv", Summary: "Shows or hides every base type of one category of the style's base type automation CSV, in the CSV's styles.", Params: []MacroParam{
		{Key: "$category", Value: "category", Required: true, Summary: "The CSV category."},
	}},
	{Name: "veiled", Summary: "Shows identified items with a veiled modifier.", Params: []MacroParam{
		{Key: "$style", Value: "style", Required: true, Summary: "Their style."},
	}},
	{Name: "max_roll_precedence", Summary: "Generates no rules; gives max-rolled armour the given style's border in every later Show rule that could match it.", Params: []MacroParam{
		{Key: "$style", Value: "style", Required: true, Summary: "The max-roll style."},
	}},
}

type macroHandler func(rg *RuleGenerator, variables *map[string][]string, parameters []*shared.ParseTree[symbols.LexingTokenType], sectionConditions []model2.Condition) ([][]string, error)

// macroHandlers compiles each macro; every MacroSpecs entry has one.
var macroHandlers = map[string]macroHandler{
	"economy_csv": func(rg *RuleGenerator, v *map[string][]string, p []*shared.ParseTree[symbols.LexingTokenType], s []model2.Condition) ([][]string, error) {
		return rg.handleEconomyCSV(v, p, s)
	},
	"viscera_preferences": func(rg *RuleGenerator, v *map[string][]string, p []*shared.ParseTree[symbols.LexingTokenType], s []model2.Condition) ([][]string, error) {
		style, err := rg.extractStyle(p)
		if err != nil {
			return nil, err
		}
		if len(rg.build.Preset.PreferredViscera) == 0 {
			return nil, nil
		}
		for _, base := range rg.build.Preset.PreferredViscera {
			found := false
			for _, row := range rg.baseTypeData {
				if row.Category == "Wickerman" && row.BaseType == base {
					found = true
					break
				}
			}
			if !found {
				return nil, fmt.Errorf("unknown PreferredViscera base %q", base)
			}
		}
		rule := &model2.ParsedRule{Action: model2.ShowRule, Style: style, Conditions: []model2.Condition{{Identifier: "@item_class", Operator: "==", Value: []string{"Viscera"}}, {Identifier: "@item_type", Operator: "==", Value: rg.build.Preset.PreferredViscera}}, Variables: v, ValidBaseTypes: rg.validBaseTypes}
		return rg.compileParsedRule(rule, s), nil
	},
	"item_tier_matrix": func(rg *RuleGenerator, v *map[string][]string, p []*shared.ParseTree[symbols.LexingTokenType], _ []model2.Condition) ([][]string, error) {
		return rg.handleItemTierMatrix(v, p)
	},
	"fated_precedence": func(rg *RuleGenerator, _ *map[string][]string, p []*shared.ParseTree[symbols.LexingTokenType], _ []model2.Condition) ([][]string, error) {
		style, err := rg.extractStyle(p)
		if err != nil {
			return nil, err
		}
		rg.ruleFactory.fated = style
		return nil, nil
	},
	"max_roll_precedence": func(rg *RuleGenerator, _ *map[string][]string, parameters []*shared.ParseTree[symbols.LexingTokenType], _ []model2.Condition) ([][]string, error) {
		return rg.handleMaxRollPrecedence(parameters)
	},
	"item_progression-equipment-leveling": func(rg *RuleGenerator, variables *map[string][]string, parameters []*shared.ParseTree[symbols.LexingTokenType], _ []model2.Condition) ([][]string, error) {
		return rg.handleEquipmentProgression(variables, parameters, 0, 67)
	},
	"item_progression-equipment-mapping": func(rg *RuleGenerator, variables *map[string][]string, parameters []*shared.ParseTree[symbols.LexingTokenType], _ []model2.Condition) ([][]string, error) {
		return rg.handleEquipmentProgression(variables, parameters, 68, 84) // 84 is max zone level for maps (T17)
	},
	"item_progression-flasks": func(rg *RuleGenerator, variables *map[string][]string, parameters []*shared.ParseTree[symbols.LexingTokenType], _ []model2.Condition) ([][]string, error) {
		return rg.handleFlaskProgression(variables, parameters)
	},
	"unique_tiering": func(rg *RuleGenerator, variables *map[string][]string, parameters []*shared.ParseTree[symbols.LexingTokenType], _ []model2.Condition) ([][]string, error) {
		return rg.handleUniqueTiering(variables, parameters)
	},
	"skill_gem_tiering": func(rg *RuleGenerator, variables *map[string][]string, parameters []*shared.ParseTree[symbols.LexingTokenType], _ []model2.Condition) ([][]string, error) {
		return rg.handleGemTiering(variables, parameters)
	},
	"divination_card_tiering": func(rg *RuleGenerator, variables *map[string][]string, parameters []*shared.ParseTree[symbols.LexingTokenType], _ []model2.Condition) ([][]string, error) {
		return rg.handleDivinationCardTiering(variables, parameters)
	},
	"tattoo_tiering": func(rg *RuleGenerator, variables *map[string][]string, parameters []*shared.ParseTree[symbols.LexingTokenType], _ []model2.Condition) ([][]string, error) {
		return rg.handleTattooTiering(variables, parameters)
	},
	"currency_tiering": func(rg *RuleGenerator, variables *map[string][]string, parameters []*shared.ParseTree[symbols.LexingTokenType], _ []model2.Condition) ([][]string, error) {
		return rg.handleCurrencyTiering(variables, parameters)
	},
	"handle_csv": func(rg *RuleGenerator, variables *map[string][]string, parameters []*shared.ParseTree[symbols.LexingTokenType], sectionConditions []model2.Condition) ([][]string, error) {
		return rg.handleCSVMacro(variables, parameters, sectionConditions)
	},
	"veiled": func(rg *RuleGenerator, variables *map[string][]string, parameters []*shared.ParseTree[symbols.LexingTokenType], _ []model2.Condition) ([][]string, error) {
		return rg.handleVeiledEquipment(variables, parameters)
	},
}

// MacroNames lists the macros the compiler handles.
func MacroNames() []string {
	names := make([]string, 0, len(macroHandlers))
	for name := range macroHandlers {
		names = append(names, name)
	}
	sort.Strings(names)
	return names
}
