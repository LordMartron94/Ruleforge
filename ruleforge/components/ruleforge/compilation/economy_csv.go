package compilation

import (
	"fmt"
	"sort"

	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/parsing/shared"
	model "github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/compilation/model"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/config"
	data "github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/data_generation"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/rules/symbols"
)

// Economy rules precede curated CSV rules. Only fresh, sufficiently liquid
// observations may override those baselines. Unsupported/new feeds retain them.
func (rg *RuleGenerator) handleEconomyCSV(v *map[string][]string, p []*shared.ParseTree[symbols.LexingTokenType], section []model.Condition) ([][]string, error) {
	category := ""
	var styles []*config.Style
	for _, parameter := range p {
		key, value := rg.getKeyAndValueFromParameter(parameter)
		if key == "$category" {
			category = value
			continue
		}
		style, err := rg.styleManager.GetStyle(value)
		if err != nil {
			return nil, err
		}
		styles = append(styles, style)
	}
	if category == "" || len(styles) == 0 {
		return nil, fmt.Errorf("economy_csv needs $category and ordered tier styles")
	}
	var baseParams []*shared.ParseTree[symbols.LexingTokenType]
	for _, parameter := range p {
		key, _ := rg.getKeyAndValueFromParameter(parameter)
		if key == "$category" {
			baseParams = append(baseParams, parameter)
		}
	}
	fallback, err := rg.handleCSVMacro(v, baseParams, section)
	if err != nil {
		return nil, err
	}
	if rg.economyState == nil {
		return fallback, nil
	}
	eligible := map[string]bool{}
	for _, e := range rg.economyState.Evidence {
		if e.State == "reliable" {
			eligible[e.BaseType] = true
		}
	}
	var profiles []config.CurrencyProfile
	names := map[string]bool{}
	for _, row := range rg.baseTypeData {
		if row.Category == category && !row.Hide && row.Rarity == nil && (row.MinStackSize == nil || *row.MinStackSize <= 1) && eligible[row.BaseType] && !names[row.BaseType] {
			names[row.BaseType] = true
			profiles = append(profiles, config.CurrencyProfile{BaseType: row.BaseType})
		}
	}
	if len(profiles) < 2 {
		return fallback, nil
	}
	sort.Slice(profiles, func(i, j int) bool { return profiles[i].BaseType < profiles[j].BaseType })
	params, err := rg.economyParameters()
	if err != nil {
		return nil, err
	}
	tiers, err := data.GenerateCurrencyTiers(rg.economyCache, profiles, len(styles), data.CurrencyTieringParameters{Economy: params, StackSizes: currencyStackSizes})
	if err != nil {
		return nil, err
	}
	proposed := map[int][]string{}
	for _, tier := range tiers {
		proposed[tier.Tier] = append(proposed[tier.Tier], tier.BaseType)
	}
	stable, err := rg.stabilizeTiers("csv:"+category, len(styles), proposed, profiles)
	if err != nil {
		return nil, err
	}
	byBase := map[string]int{}
	for tier, names := range stable {
		for _, name := range names {
			byBase[name] = tier
		}
	}
	for i := range tiers {
		tiers[i].Tier = byBase[tiers[i].BaseType]
	}
	// CSV categories inherit their section constraints just like their static
	// fallback. Currency emission normally runs in an unconstrained section.
	var generated [][]string
	for _, currency := range tiers {
		best := currency.Tier
		for j := len(currency.Stacks) - 1; j >= 0; j-- {
			stack := currency.Stacks[j]
			stack.Tier = min(best, stack.Tier)
			rule := &model.ParsedRule{Action: model.ShowRule, Style: styles[stack.Tier-1], Conditions: []model.Condition{{Identifier: "@item_type", Operator: "==", Value: []string{currency.BaseType}}, {Identifier: "@stack_size", Operator: ">=", Value: []string{fmt.Sprint(stack.MinStack)}}}, Variables: v, ValidBaseTypes: rg.validBaseTypes}
			generated = append(generated, rg.compileParsedRule(rule, section)...)
		}
		rule := &model.ParsedRule{Action: model.ShowRule, Style: styles[currency.Tier-1], Conditions: []model.Condition{{Identifier: "@item_type", Operator: "==", Value: []string{currency.BaseType}}}, Variables: v, ValidBaseTypes: rg.validBaseTypes}
		generated = append(generated, rg.compileParsedRule(rule, section)...)
	}
	return append(generated, fallback...), nil
}
