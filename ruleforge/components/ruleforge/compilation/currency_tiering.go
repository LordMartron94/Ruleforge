package compilation

import (
	"fmt"
	"sort"
	"strconv"

	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/parsing/shared"
	model2 "github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/compilation/model"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/config"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/data_generation"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/rules/symbols"
)

// CurrencySettings are what currency_tiering tiers and how much crafting use
// counts against the economy.
type CurrencySettings struct {
	Profiles       []config.CurrencyProfile
	CraftingWeight float64
	// Source is the profile file, for messages.
	Source string
}

// currencyStackSizes are the stack sizes currency_tiering scores on their own.
var currencyStackSizes = []int{3, 5, 10, 20}

// handleCurrencyTiering tiers the profiled currencies by economy and crafting
// use. Parameters other than $hidden are the tier styles, best first. With
// $hidden, one more tier is clustered below them and hidden in that style.
// A stack that scores into a better tier gets a rule of its own, so stacks
// rank higher and a hidden currency still shows once its stack is worth it.
func (rg *RuleGenerator) handleCurrencyTiering(variables *map[string][]string, parameters []*shared.ParseTree[symbols.LexingTokenType]) ([][]string, error) {
	var tierStyles []*config.Style
	var hiddenStyle *config.Style
	for _, parameter := range parameters {
		key, value := rg.getKeyAndValueFromParameter(parameter)
		style, err := rg.styleManager.GetStyle(value)
		if err != nil {
			return nil, fmt.Errorf("currency_tiering %s: %w", key, err)
		}
		if key == "$hidden" {
			hiddenStyle = style
			continue
		}
		tierStyles = append(tierStyles, style)
	}
	if len(tierStyles) == 0 {
		return nil, fmt.Errorf("currency_tiering needs at least one tier style")
	}
	if len(rg.currency.Profiles) == 0 {
		return nil, fmt.Errorf("currency_tiering has no currency profiles to tier: %s is missing or empty (CurrencyTiering.Profiles in the configuration)", rg.currency.Source)
	}
	numTiers := len(tierStyles)
	if hiddenStyle != nil {
		numTiers++
	}

	economy, err := rg.economyParameters()
	if err != nil {
		return nil, err
	}
	tiers, err := data_generation.GenerateCurrencyTiers(rg.economyCache, rg.currency.Profiles, numTiers, data_generation.CurrencyTieringParameters{
		Economy:        economy,
		CraftingWeight: rg.currency.CraftingWeight,
		StackSizes:     currencyStackSizes,
	})
	if err != nil {
		return nil, fmt.Errorf("currency_tiering: %w", err)
	}
	proposed := map[int][]string{}
	for _, currency := range tiers {
		proposed[currency.Tier] = append(proposed[currency.Tier], currency.BaseType+"\x1e1")
		for _, stack := range currency.Stacks {
			proposed[stack.Tier] = append(proposed[stack.Tier], currency.BaseType+"\x1e"+strconv.Itoa(stack.MinStack))
		}
	}
	stable, err := rg.stabilizeTiers("Currency", numTiers, proposed, rg.currency.Profiles)
	if err != nil {
		return nil, err
	}
	byName := map[string]int{}
	for tier, names := range stable {
		for _, name := range names {
			byName[name] = tier
		}
	}
	for i := range tiers {
		currency := &tiers[i]
		currency.Tier = byName[currency.BaseType+"\x1e1"]
		best := currency.Tier
		var kept []data_generation.StackTier
		for _, stack := range currency.Stacks {
			stack.Tier = min(best, byName[currency.BaseType+"\x1e"+strconv.Itoa(stack.MinStack)])
			if stack.Tier < best {
				kept = append(kept, stack)
				best = stack.Tier
			}
		}
		currency.Stacks = kept
	}
	return rg.currencyTierRules(tiers, tierStyles, hiddenStyle, variables), nil
}

// currencyTierRules turns tiers into rules: stacks before singles, bigger
// stacks first, and tiers past the last style hidden in hiddenStyle.
func (rg *RuleGenerator) currencyTierRules(tiers []data_generation.CurrencyTier, tierStyles []*config.Style, hiddenStyle *config.Style, variables *map[string][]string) [][]string {
	type group struct {
		minStack, tier int
	}
	baseTypes := make(map[group][]string)
	for _, currency := range tiers {
		for _, stack := range currency.Stacks {
			key := group{stack.MinStack, stack.Tier}
			baseTypes[key] = append(baseTypes[key], currency.BaseType)
		}
		key := group{1, currency.Tier}
		baseTypes[key] = append(baseTypes[key], currency.BaseType)
	}
	groups := make([]group, 0, len(baseTypes))
	for key := range baseTypes {
		groups = append(groups, key)
	}
	// Bigger stacks first, so a stack meets its own rule before a smaller one's.
	sort.Slice(groups, func(i, j int) bool {
		if groups[i].minStack != groups[j].minStack {
			return groups[i].minStack > groups[j].minStack
		}
		return groups[i].tier < groups[j].tier
	})

	var generatedRules [][]string
	for _, key := range groups {
		style, action := hiddenStyle, model2.HideRule
		if key.tier <= len(tierStyles) {
			style, action = tierStyles[key.tier-1], model2.ShowRule
		}
		conditions := []model2.Condition{{Identifier: "@item_type", Operator: "==", Value: baseTypes[key]}}
		if key.minStack > 1 {
			conditions = append(conditions, model2.Condition{Identifier: "@stack_size", Operator: ">=", Value: []string{strconv.Itoa(key.minStack)}})
		}
		rule := &model2.ParsedRule{
			Style:          style,
			Action:         action,
			Conditions:     conditions,
			Variables:      variables,
			ValidBaseTypes: rg.validBaseTypes,
		}
		generatedRules = append(generatedRules, rg.compileParsedRule(rule, nil)...)
	}
	return generatedRules
}
