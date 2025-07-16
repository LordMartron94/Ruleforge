package compilation

import (
	"fmt"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/parsing/shared"
	model2 "github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/compilation/model"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/config"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/data_generation"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/data_generation/model"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/rules/symbols"
	"log"
	"slices"
	"sort"
)

// RuleGenerator is the engine for compiling rules. It contains all complex game logic.
type RuleGenerator struct {
	ruleFactory           *RuleFactory
	styleManager          *StyleManager
	validBaseTypes        []string
	armorBases            []model.ItemBase
	weaponBases           []model.ItemBase
	flaskBases            []model.ItemBase
	economyCache          map[string][]data_generation.EconomyCacheItem
	economyWeights        config.EconomyWeights
	leagueWeights         []config.LeagueWeights
	normalizationStrategy string
	chasePotentialWeight  float64
	baseTypeData          []config.BaseTypeAutomationEntry
}

// NewRuleGenerator creates the rule generation engine.
func NewRuleGenerator(
	factory *RuleFactory,
	styleMgr *StyleManager,
	validBases []string,
	armor []model.ItemBase,
	weapons []model.ItemBase,
	flasks []model.ItemBase,
	economyCache map[string][]data_generation.EconomyCacheItem,
	economyWeights config.EconomyWeights,
	leagueWeights []config.LeagueWeights,
	normalizationStrategy string,
	chasePotentialWeight float64,
	baseTypeData []config.BaseTypeAutomationEntry,
) *RuleGenerator {
	return &RuleGenerator{
		ruleFactory:           factory,
		styleManager:          styleMgr,
		validBaseTypes:        validBases,
		armorBases:            armor,
		weaponBases:           weapons,
		flaskBases:            flasks,
		economyCache:          economyCache,
		economyWeights:        economyWeights,
		leagueWeights:         leagueWeights,
		normalizationStrategy: normalizationStrategy,
		chasePotentialWeight:  chasePotentialWeight,
		baseTypeData:          baseTypeData,
	}
}

// GenerateRulesForSection compiles all rules within a single logical section.
func (rg *RuleGenerator) GenerateRulesForSection(
	section ExtractedSection,
	variables map[string][]string,
	buildType BuildType,
) ([][]string, error) {
	var allGeneratedRules [][]string

	for _, childNode := range section.RuleNodes {
		var generatedRules [][]string
		var err error

		switch childNode.Symbol {
		case symbols.ParseSymbolRuleExpression.String():
			generatedRules, err = rg.handleRuleExpression(childNode, &variables, section.Conditions, buildType)
		case symbols.ParseSymbolMacroExpression.String():
			generatedRules, err = rg.handleMacroExpression(childNode, &variables, section.Conditions, buildType)
		default:
			return nil, fmt.Errorf("unsupported symbol in rule list: %s", childNode.Symbol)
		}

		if err != nil {
			return nil, err
		}
		allGeneratedRules = append(allGeneratedRules, generatedRules...)
	}
	return allGeneratedRules, nil
}

func (rg *RuleGenerator) handleRuleExpression(
	ruleExpressionNode *shared.ParseTree[symbols.LexingTokenType],
	variables *map[string][]string,
	sectionConditions []model2.Condition,
	buildType BuildType,
) ([][]string, error) {
	styleValue := ruleExpressionNode.Children[2].Token.ValueToString()
	style, err := rg.styleManager.GetStyle(styleValue)
	if err != nil {
		return nil, err
	}

	showOrHideStr := ruleExpressionNode.Children[4].Token.ValueToString()[1:]
	var action model2.RuleType
	if showOrHideStr == "Show" {
		action = model2.ShowRule
	} else {
		action = model2.HideRule
	}

	// retrieveConditions is a free function from treewalker.go
	ruleSpecificConditions := retrieveConditions(ruleExpressionNode)

	rule := &model2.ParsedRule{
		Style:          style,
		Action:         action,
		Conditions:     ruleSpecificConditions,
		Variables:      variables,
		ValidBaseTypes: rg.validBaseTypes,
	}

	return rg.compileParsedRule(rule, sectionConditions, buildType), nil
}

func (rg *RuleGenerator) handleMacroExpression(
	macroExpressionNode *shared.ParseTree[symbols.LexingTokenType],
	variables *map[string][]string,
	sectionConditions []model2.Condition,
	buildType BuildType,
) ([][]string, error) {
	macroType := macroExpressionNode.Children[1].Token.ValueToString()
	parameters := macroExpressionNode.FindAllSymbolNodes(symbols.ParseSymbolParameter.String())

	switch macroType {
	case "item_progression-equipment":
		return rg.handleEquipmentProgression(variables, buildType, parameters), nil
	case "item_progression-flasks":
		return rg.handleFlaskProgression(variables, parameters, buildType), nil
	case "unique_tiering":
		return rg.handleUniqueTiering(variables, parameters, buildType), nil
	case "skill_gem_tiering":
		return rg.handleGemTiering(variables, parameters, buildType), nil
	case "handle_csv":
		return rg.handleCSVMacro(variables, parameters, buildType, sectionConditions), nil
	default:
		return nil, fmt.Errorf("unsupported macro type: %s", macroType)
	}
}

func (rg *RuleGenerator) handleEquipmentProgression(
	variables *map[string][]string,
	buildType BuildType,
	parameters []*shared.ParseTree[symbols.LexingTokenType],
) [][]string {
	var allGeneratedRules [][]string
	itemsByCategory := make(map[string][]*model.ItemBase)
	for i := range rg.weaponBases {
		weapon := rg.weaponBases[i]
		if IsWeaponAssociatedWithBuild(weapon, buildType) {
			itemsByCategory[weapon.Type] = append(itemsByCategory[weapon.Type], &rg.weaponBases[i])
		}
	}
	for i := range rg.armorBases {
		armor := rg.armorBases[i]
		if IsArmorAssociatedWithBuild(armor, buildType) {
			itemsByCategory[armor.Type] = append(itemsByCategory[armor.Type], &rg.armorBases[i])
		}
	}

	required := []string{"$hidden_normal", "$hidden_magic", "$hidden_rare", "$show_normal", "$show_magic", "$show_rare"}

	styleMap, err := rg.extractStyleParameters(parameters, required)
	if err != nil {
		panic(err)
	}

	rg.produceProgression(itemsByCategory, variables, styleMap["$show_normal"], styleMap["$show_magic"], styleMap["$show_rare"], styleMap["$hidden_normal"], styleMap["$hidden_magic"], styleMap["$hidden_rare"], &allGeneratedRules, buildType, false)

	return allGeneratedRules
}

func (rg *RuleGenerator) handleFlaskProgression(
	variables *map[string][]string,
	parameters []*shared.ParseTree[symbols.LexingTokenType],
	buildType BuildType,
) [][]string {
	var allGeneratedRules [][]string
	itemsByCategory := make(map[string][]*model.ItemBase)
	for i := range rg.flaskBases {
		flask := rg.flaskBases[i]
		key := flask.GetBaseType()
		if flask.SubType == "Life" || flask.SubType == "Mana" || flask.SubType == "Hybrid" {
			key = flask.Type + flask.SubType
		}
		itemsByCategory[key] = append(itemsByCategory[key], &rg.flaskBases[i])
	}

	hiddenStyle, shownStyle := rg.getHiddenAndShownStyleFromParameters(parameters)
	rg.produceProgression(itemsByCategory, variables, shownStyle, shownStyle, shownStyle, hiddenStyle, hiddenStyle, hiddenStyle, &allGeneratedRules, buildType, true)

	return allGeneratedRules
}

func (rg *RuleGenerator) getHiddenAndShownStyleFromParameters(
	parameters []*shared.ParseTree[symbols.LexingTokenType],
) (*config.Style, *config.Style) {
	required := []string{"$hidden", "$show"}

	styleMap, err := rg.extractStyleParameters(parameters, required)
	if err != nil {
		panic(err)
	}

	return styleMap["$hidden"], styleMap["$show"]
}

func (rg *RuleGenerator) extractStyleParameters(
	parameters []*shared.ParseTree[symbols.LexingTokenType],
	requiredKeys []string,
) (map[string]*config.Style, error) {
	requiredSet := make(map[string]struct{}, len(requiredKeys))
	for _, key := range requiredKeys {
		requiredSet[key] = struct{}{}
	}

	foundStyles := make(map[string]*config.Style)

	for _, parameter := range parameters {
		key, value := rg.getKeyAndValueFromParameter(parameter)

		if _, isRequired := requiredSet[key]; isRequired {
			style, err := rg.styleManager.GetStyle(value)
			if err != nil {
				return nil, fmt.Errorf("failed to get style for key '%s': %w", key, err)
			}
			foundStyles[key] = style
		}
	}

	for _, key := range requiredKeys {
		if _, found := foundStyles[key]; !found {
			return nil, fmt.Errorf("required style parameter '%s' is absent", key)
		}
	}

	return foundStyles, nil
}

func (rg *RuleGenerator) getKeyAndValueFromParameter(parameter *shared.ParseTree[symbols.LexingTokenType]) (string, string) {
	key := parameter.Children[1].Token.ValueToString()
	value := parameter.Children[3].Token.ValueToString()

	return key, value
}

func (rg *RuleGenerator) produceProgression(
	itemsByCategory map[string][]*model.ItemBase,
	variables *map[string][]string,
	shownStyleNormal, shownStyleMagic, shownStyleRare, hiddenStyleNormal, hiddenStyleMagic, hiddenStyleRare *config.Style,
	allGeneratedRules *[][]string,
	buildType BuildType,
	disableRare bool,
) {
	for category := range itemsByCategory {
		sort.Slice(itemsByCategory[category], func(i, j int) bool {
			itemA := itemsByCategory[category][i]
			itemB := itemsByCategory[category][j]
			dropLevelA := getDropLevel(itemA)
			dropLevelB := getDropLevel(itemB)
			if dropLevelA == dropLevelB {
				return itemA.Name < itemB.Name
			}
			return dropLevelA < dropLevelB
		})
	}

	for _, categoryItems := range itemsByCategory {
		if len(categoryItems) == 0 {
			continue
		}

		i := 0
		for i < len(categoryItems) {
			currentItem := categoryItems[i]
			startLevel := getDropLevel(currentItem)

			if startLevel > 69 {
				i++
				continue
			}

			tierEndIndex := i
			for tierEndIndex+1 < len(categoryItems) &&
				getDropLevel(categoryItems[tierEndIndex+1]) == startLevel {
				tierEndIndex++
			}
			currentTierItems := categoryItems[i : tierEndIndex+1]

			nextTierStartIndex := tierEndIndex + 1
			isLastTier := nextTierStartIndex >= len(categoryItems)

			showEndLevel := 69
			hideStartLevel := 69

			if !isLastTier {
				nextItemDropLevel := getDropLevel(categoryItems[nextTierStartIndex])
				showEndLevel = nextItemDropLevel - 1
				hideStartLevel = nextItemDropLevel
			}

			if showEndLevel > 69 {
				showEndLevel = 69
			}

			if startLevel <= showEndLevel {
				for _, tierItem := range currentTierItems {
					rg.constructItemProgressionRule(variables, model2.ShowRule, *tierItem, allGeneratedRules, shownStyleNormal, fmt.Sprintf("%d", showEndLevel), "Normal", buildType)
					rg.constructItemProgressionRule(variables, model2.ShowRule, *tierItem, allGeneratedRules, shownStyleMagic, fmt.Sprintf("%d", showEndLevel), "Magic", buildType)

					if !disableRare {
						rg.constructItemProgressionRule(variables, model2.ShowRule, *tierItem, allGeneratedRules, shownStyleRare, fmt.Sprintf("%d", showEndLevel), "Rare", buildType)
					}
				}
			}

			if !isLastTier && hideStartLevel <= 69 {
				for _, tierItem := range currentTierItems {
					rg.constructItemProgressionRule(variables, model2.HideRule, *tierItem, allGeneratedRules, hiddenStyleNormal, fmt.Sprintf("%d", 69), "Normal", buildType)
					rg.constructItemProgressionRule(variables, model2.HideRule, *tierItem, allGeneratedRules, hiddenStyleMagic, fmt.Sprintf("%d", 69), "Magic", buildType)

					if !disableRare {
						rg.constructItemProgressionRule(variables, model2.HideRule, *tierItem, allGeneratedRules, hiddenStyleRare, fmt.Sprintf("%d", 69), "Rare", buildType)
					}
				}
			}

			i = tierEndIndex + 1
		}
	}
}

func (rg *RuleGenerator) constructItemProgressionRule(
	variables *map[string][]string,
	ruleType model2.RuleType,
	item model.ItemBase,
	allGeneratedRules *[][]string,
	style *config.Style,
	maxAreaLevel string,
	rarity string,
	buildType BuildType) {
	areaCondition := model2.Condition{
		Identifier: "@area_level",
		Operator:   "<=",
		Value:      []string{maxAreaLevel},
	}
	baseTypeCondition := model2.Condition{
		Identifier: "@item_type",
		Operator:   "==",
		Value:      []string{item.GetBaseType()},
	}
	rarityCondition := model2.Condition{
		Identifier: "@rarity",
		Operator:   "==",
		Value:      []string{rarity},
	}

	rule := &model2.ParsedRule{
		Style:          style,
		Action:         ruleType,
		Conditions:     []model2.Condition{areaCondition, baseTypeCondition, rarityCondition},
		Variables:      variables,
		ValidBaseTypes: rg.validBaseTypes,
	}

	*allGeneratedRules = append(*allGeneratedRules, rg.compileParsedRule(rule, []model2.Condition{}, buildType)...)
}

type TieringConfig struct {
	InitialCondition  model2.Condition
	ItemClassToFilter string
	TieredIdentifier  string
}

// handleUniqueTiering generates tiered rules for unique items based on economy data.
func (rg *RuleGenerator) handleUniqueTiering(variables *map[string][]string, parameters []*shared.ParseTree[symbols.LexingTokenType], buildType BuildType) [][]string {
	uniqueConfig := TieringConfig{
		InitialCondition: model2.Condition{
			Identifier: "@rarity",
			Operator:   "==",
			Value:      []string{"Unique"},
		},
		ItemClassToFilter: "Uniques",
		TieredIdentifier:  "@item_type",
	}
	return rg.generateTieredRules(variables, parameters, uniqueConfig, buildType)
}

func (rg *RuleGenerator) handleGemTiering(variables *map[string][]string, parameters []*shared.ParseTree[symbols.LexingTokenType], buildType BuildType) [][]string {
	gemConfig := TieringConfig{
		InitialCondition: model2.Condition{
			Identifier: "@item_class",
			Operator:   "==",
			Value:      []string{"Skill Gems", "Support Gems"},
		},
		ItemClassToFilter: "Gems",
		TieredIdentifier:  "@item_type",
	}
	return rg.generateTieredRules(variables, parameters, gemConfig, buildType)
}

func (rg *RuleGenerator) generateTieredRules(
	variables *map[string][]string,
	parameters []*shared.ParseTree[symbols.LexingTokenType],
	tieringConfiguration TieringConfig,
	buildType BuildType,
) [][]string {
	generatedRules := make([][]string, 0)

	// 1. Get tier styles from parameters (Common Logic)
	tierStyles := make([]*config.Style, len(parameters))
	for i, parameter := range parameters {
		_, value := rg.getKeyAndValueFromParameter(parameter)
		style, err := rg.styleManager.GetStyle(value)
		if err != nil {
			panic(err)
		}
		tierStyles[i] = style
	}

	// 2. Handle the case with only one tier (Common Logic)
	if len(tierStyles) == 1 {
		style := tierStyles[0]

		rule := &model2.ParsedRule{
			Style:          style,
			Action:         model2.ShowRule,
			Conditions:     []model2.Condition{tieringConfiguration.InitialCondition},
			Variables:      variables,
			ValidBaseTypes: rg.validBaseTypes,
		}

		generatedRules = append(generatedRules, rg.compileParsedRule(rule, []model2.Condition{}, buildType)...)
		return generatedRules
	}

	// 3. Filter items from the economy cache based on the provided class (Customizable Logic)
	itemsToCheck := make(map[string][]data_generation.EconomyCacheItem)
	for league, items := range rg.economyCache {
		validItems := make([]data_generation.EconomyCacheItem, 0)
		for _, item := range items {
			if item.Class != tieringConfiguration.ItemClassToFilter {
				continue
			}
			if !slices.Contains(rg.validBaseTypes, item.BaseType) {
				continue
			}
			validItems = append(validItems, item)
		}
		log.Printf("Valid %s for League %s: %d\n", tieringConfiguration.ItemClassToFilter, league, len(validItems))
		itemsToCheck[league] = validItems
	}

	var normStrategy data_generation.NormalizationStrategy

	switch rg.normalizationStrategy {
	case "Global":
		normStrategy = data_generation.Global
	case "Per-League":
		normStrategy = data_generation.PerLeague
	default:
		panic("unsupported normalization strategy")
	}

	// 4. Generate tiers using economy data (Common Logic)
	tiered, err := data_generation.GenerateTiers(itemsToCheck, len(tierStyles), data_generation.TieringParameters{
		ValueWeight:              rg.economyWeights.Value,
		RarityWeight:             rg.economyWeights.Rarity,
		LeagueWeights:            rg.leagueWeights,
		NormStrategy:             normStrategy,
		ChaosOutlierPercentile:   0.95,
		MinListingsForPercentile: 20,
		ChasePotentialWeight:     rg.chasePotentialWeight,
	})
	if err != nil {
		panic(err)
	}

	// 5. Sort tiers and construct rules (Common Logic)
	var sortedTiers []int
	for tier := range tiered {
		sortedTiers = append(sortedTiers, tier)
	}
	sort.Slice(sortedTiers, func(i, j int) bool {
		return sortedTiers[i] > sortedTiers[j]
	})

	for _, tier := range sortedTiers {
		tieredItems := tiered[tier]
		if len(tieredItems) == 0 {
			continue
		}

		style := tierStyles[tier-1]
		tieredItemsCondition := model2.Condition{
			Identifier: tieringConfiguration.TieredIdentifier,
			Operator:   "==",
			Value:      tieredItems,
		}

		rule := &model2.ParsedRule{
			Style:          style,
			Action:         model2.ShowRule,
			Conditions:     []model2.Condition{tieringConfiguration.InitialCondition, tieredItemsCondition},
			Variables:      variables,
			ValidBaseTypes: rg.validBaseTypes,
		}

		generatedRules = append(generatedRules, rg.compileParsedRule(rule, []model2.Condition{}, buildType)...)
	}

	return generatedRules
}

type AutomationGroup struct {
	MinStackSize *int
	Style        config.Style
	Tier         int
	BaseTypes    []string
	Rarity       *string
	Hide         bool
}

// GroupByProperties takes a slice of BaseTypeAutomationEntry and groups them
// by StyleID, Priority, and MinStackSize.
// The final result is a slice of
// AutomationGroup, sorted by Priority in ascending order.
//
//goland:noinspection t
func groupByProperties(entries []config.BaseTypeAutomationEntry, styleManager *StyleManager) []AutomationGroup {
	if len(entries) == 0 {
		return []AutomationGroup{}
	}

	type groupKey struct {
		StyleID      string
		MinStackSize int
		Rarity       *string
		Hide         bool
	}

	groupsMap := make(map[groupKey]*AutomationGroup)

	for _, entry := range entries {
		mssValue := -1
		if entry.MinStackSize != nil {
			mssValue = *entry.MinStackSize
		}
		style, err := styleManager.GetStyle(entry.Style)

		if err != nil {
			panic(err)
		}

		key := groupKey{
			StyleID:      style.Id,
			MinStackSize: mssValue,
			Rarity:       entry.Rarity,
			Hide:         entry.Hide,
		}

		if existingGroup, found := groupsMap[key]; found {
			existingGroup.BaseTypes = append(existingGroup.BaseTypes, entry.BaseType)
		} else {
			newGroup := &AutomationGroup{
				MinStackSize: entry.MinStackSize,
				Style:        *style,
				Tier:         entry.Priority,
				BaseTypes:    []string{entry.BaseType},
				Rarity:       entry.Rarity,
				Hide:         entry.Hide,
			}
			groupsMap[key] = newGroup
		}
	}

	groupedResult := make([]AutomationGroup, 0, len(groupsMap))
	for _, group := range groupsMap {
		groupedResult = append(groupedResult, *group)
	}

	sort.Slice(groupedResult, func(i, j int) bool {
		return groupedResult[i].Tier > groupedResult[j].Tier
	})

	return groupedResult
}

//goland:noinspection t
func (rg *RuleGenerator) handleCSVMacro(variables *map[string][]string, parameters []*shared.ParseTree[symbols.LexingTokenType], buildType BuildType, sectionConditions []model2.Condition) [][]string {
	allGeneratedRules := make([][]string, 0)

	category := ""

	for _, parameter := range parameters {
		key, value := rg.getKeyAndValueFromParameter(parameter)

		switch key[1:] {
		case "category":
			category = value
		default:
			panic("unsupported parameter: " + key)
		}
	}

	if category == "" {
		panic("ensure you have 'category' specified")
	}

	toBeProcessed := make([]config.BaseTypeAutomationEntry, 0)

	for _, entry := range rg.baseTypeData {
		if entry.Category == category {
			toBeProcessed = append(toBeProcessed, entry)
		}
	}

	ruleGroups := groupByProperties(toBeProcessed, rg.styleManager)

	for _, ruleGroup := range ruleGroups {
		baseTypes := ruleGroup.BaseTypes
		minStackSize := ruleGroup.MinStackSize
		rarity := ruleGroup.Rarity
		style := ruleGroup.Style

		conditions := []model2.Condition{
			{
				Identifier: "@item_type",
				Operator:   "==",
				Value:      baseTypes,
			},
		}

		if minStackSize != nil {
			conditions = append(conditions, model2.Condition{
				Identifier: "@stack_size",
				Operator:   ">=",
				Value:      []string{fmt.Sprintf("%d", *minStackSize)},
			})
		}

		if rarity != nil {
			conditions = append(conditions, model2.Condition{
				Identifier: "@rarity",
				Operator:   "==",
				Value:      []string{*rarity},
			})
		}

		var ruleAction model2.RuleType
		if ruleGroup.Hide {
			ruleAction = model2.HideRule
		} else {
			ruleAction = model2.ShowRule
		}

		rule := &model2.ParsedRule{
			Style:          &style,
			Action:         ruleAction,
			Conditions:     conditions,
			Variables:      variables,
			ValidBaseTypes: rg.validBaseTypes,
		}

		allGeneratedRules = append(allGeneratedRules, rg.compileParsedRule(rule, sectionConditions, buildType)...)
	}

	return allGeneratedRules
}

var conditionOrder = map[string]int{
	"@sockets":      2,
	"@stack_size":   3,
	"@socket_group": 5,
	"@height":       6,
	"@width":        7,
	"@item_class":   9,
	"@item_type":    10,
	"@map_tier":     11,
	"@rarity":       12,
	"@area_level":   13,
}

//goland:noinspection t
func (rg *RuleGenerator) compileParsedRule(rule *model2.ParsedRule, sectionConditions []model2.Condition, buildType BuildType) [][]string {
	var macroConditions []model2.Condition
	var standardRuleConditions []model2.Condition
	macros := []string{"@class_use"}

	for _, cond := range rule.Conditions {
		if slices.Contains(macros, cond.Identifier) {
			macroConditions = append(macroConditions, cond)
		} else {
			standardRuleConditions = append(standardRuleConditions, cond)
		}
	}

	ruleConditionsMap := make(map[string]model2.Condition, len(standardRuleConditions))
	for _, ruleCond := range standardRuleConditions {
		key := ruleCond.Identifier + ":" + ruleCond.Operator
		ruleConditionsMap[key] = ruleCond
	}

	finalStandardConditions := make([]model2.Condition, 0, len(standardRuleConditions)+len(sectionConditions))
	for _, sectionCond := range sectionConditions {
		key := sectionCond.Identifier + ":" + sectionCond.Operator
		if _, isOverridden := ruleConditionsMap[key]; !isOverridden {
			finalStandardConditions = append(finalStandardConditions, sectionCond)
		}
	}
	finalStandardConditions = append(finalStandardConditions, standardRuleConditions...)

	if len(macroConditions) == 0 {
		sort.Slice(finalStandardConditions, func(i, j int) bool {
			orderI, okI := conditionOrder[finalStandardConditions[i].Identifier]
			if !okI {
				orderI = 999
			}
			orderJ, okJ := conditionOrder[finalStandardConditions[j].Identifier]
			if !okJ {
				orderJ = 999
			}
			return orderI < orderJ
		})

		compiledFinalConditions := make([]string, len(finalStandardConditions))
		for i, cond := range finalStandardConditions {
			compiledFinalConditions[i] = cond.ConstructCompiledCondition(rule.Variables, rule.ValidBaseTypes)
		}
		finalRule := rg.ruleFactory.ConstructRule(rule.Action, *rule.Style, compiledFinalConditions)
		return [][]string{finalRule}
	}

	var allGeneratedRules [][]string
	for _, macro := range macroConditions {
		var generatedForMacro [][]string
		switch macro.Identifier {
		case "@class_use":
			generatedForMacro = rg.handleClassUseMacro(rule.Action, rule.Style, finalStandardConditions, macro, buildType, rule.Variables)
		default:
			panic("unknown macro: " + macro.Identifier)
		}
		allGeneratedRules = append(allGeneratedRules, generatedForMacro...)
	}
	return allGeneratedRules
}

func (rg *RuleGenerator) handleClassUseMacro(
	action model2.RuleType, style *config.Style, baseConditions []model2.Condition,
	macro model2.Condition, buildType BuildType, variables *map[string][]string,
) [][]string {
	generateRule := func(newCond model2.Condition) []string {
		finalConditions := make([]model2.Condition, 0, len(baseConditions)+1)
		finalConditions = append(finalConditions, baseConditions...)
		finalConditions = append(finalConditions, newCond)

		sort.Slice(finalConditions, func(i, j int) bool {
			orderI, okI := conditionOrder[finalConditions[i].Identifier]
			if !okI {
				orderI = 999
			}
			orderJ, okJ := conditionOrder[finalConditions[j].Identifier]
			if !okJ {
				orderJ = 999
			}
			return orderI < orderJ
		})

		compiledConditions := make([]string, len(finalConditions))
		for i, cond := range finalConditions {
			compiledConditions[i] = cond.ConstructCompiledCondition(variables, rg.validBaseTypes)
		}
		return rg.ruleFactory.ConstructRule(action, *style, compiledConditions)
	}

	var weaponClasses []string
	var armorClasses []string
	switch macro.Value[0] {
	case "true":
		weaponClasses = GetAssociatedWeaponClasses(buildType)
		for _, item := range rg.armorBases {
			if IsArmorAssociatedWithBuild(item, buildType) {
				armorClasses = append(armorClasses, item.GetBaseType())
			}
		}
	case "false":
		weaponClasses = GetUnassociatedWeaponClasses(buildType)
		for _, item := range rg.armorBases {
			if !IsArmorAssociatedWithBuild(item, buildType) {
				armorClasses = append(armorClasses, item.GetBaseType())
			}
		}
	default:
		panic(fmt.Sprintf("invalid value for @class_use: %s", macro.Value[0]))
	}

	weaponryCond := model2.Condition{Identifier: "@item_class", Operator: macro.Operator, Value: weaponClasses}
	weaponryRule := generateRule(weaponryCond)

	armorCond := model2.Condition{Identifier: "@item_type", Operator: macro.Operator, Value: armorClasses}
	armorRule := generateRule(armorCond)

	return [][]string{weaponryRule, armorRule}
}

// getDropLevel helper remains the same.
func getDropLevel(item *model.ItemBase) int {
	if item.DropLevel != nil {
		return *item.DropLevel
	}
	log.Printf("WARNING: Item '%s' has a nil DropLevel; defaulting to level 0.", item.Name)
	return 0
}
