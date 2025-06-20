package compilation

import (
	"fmt"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/parsing/shared"
	model2 "github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/compilation/model"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/config"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/data_generation/model"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/rules/symbols"
	"log"
	"slices"
	"sort"
)

// RuleGenerator is the engine for compiling rules. It contains all complex game logic.
type RuleGenerator struct {
	ruleFactory    *RuleFactory
	styleManager   *StyleManager
	validBaseTypes []string
	armorBases     []model.ItemBase
	weaponBases    []model.ItemBase
	flaskBases     []model.ItemBase
}

// NewRuleGenerator creates the rule generation engine.
func NewRuleGenerator(
	factory *RuleFactory,
	styleMgr *StyleManager,
	validBases []string,
	armor []model.ItemBase,
	weapon []model.ItemBase,
	flask []model.ItemBase,
) *RuleGenerator {
	return &RuleGenerator{
		ruleFactory:    factory,
		styleManager:   styleMgr,
		validBaseTypes: validBases,
		armorBases:     armor,
		weaponBases:    weapon,
		flaskBases:     flask,
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
	style, err := rg.styleManager.GetStyle(styleValue, *variables)
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
	styleOneKey := macroExpressionNode.Children[3].Token.ValueToString()
	styleOneValue := macroExpressionNode.Children[5].Token.ValueToString()
	styleTwoKey := macroExpressionNode.Children[7].Token.ValueToString()
	styleTwoValue := macroExpressionNode.Children[9].Token.ValueToString()

	var hiddenStyle, shownStyle *config.Style
	var err error

	if styleOneKey == "$hidden" {
		hiddenStyle, err = rg.styleManager.GetStyle(styleOneValue, *variables)
	} else if styleOneKey == "$show" {
		shownStyle, err = rg.styleManager.GetStyle(styleOneValue, *variables)
	}
	if err != nil {
		return nil, err
	}

	if styleTwoKey == "$hidden" {
		hiddenStyle, err = rg.styleManager.GetStyle(styleTwoValue, *variables)
	} else if styleTwoKey == "$show" {
		shownStyle, err = rg.styleManager.GetStyle(styleTwoValue, *variables)
	}
	if err != nil {
		return nil, err
	}

	if hiddenStyle == nil || shownStyle == nil {
		return nil, fmt.Errorf("one or multiple style states missing in macro (ensure both 'Hidden' and 'Show' are defined)")
	}

	switch macroType {
	case "item_progression":
		return rg.handleItemProgression(variables, buildType, hiddenStyle, shownStyle), nil
	default:
		return nil, fmt.Errorf("unsupported macro type: %s", macroType)
	}
}

func (rg *RuleGenerator) handleItemProgression(
	variables *map[string][]string,
	buildType BuildType,
	hiddenStyle, shownStyle *config.Style,
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
	for i := range rg.flaskBases {
		flask := rg.flaskBases[i]
		key := flask.GetBaseType()
		if flask.SubType == "Life" || flask.SubType == "Mana" || flask.SubType == "Hybrid" {
			key = flask.Type + flask.SubType
		}
		itemsByCategory[key] = append(itemsByCategory[key], &rg.flaskBases[i])
	}

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
					rg.constructItemProgressionRule(variables, model2.ShowRule, *tierItem, &allGeneratedRules, shownStyle, fmt.Sprintf("%d", showEndLevel))
				}
			}

			if !isLastTier && hideStartLevel <= 69 {
				for _, tierItem := range currentTierItems {
					rg.constructItemProgressionRule(variables, model2.HideRule, *tierItem, &allGeneratedRules, hiddenStyle, fmt.Sprintf("%d", 69))
				}
			}

			i = tierEndIndex + 1
		}
	}
	return allGeneratedRules
}

func (rg *RuleGenerator) constructItemProgressionRule(variables *map[string][]string, ruleType model2.RuleType, item model.ItemBase, allGeneratedRules *[][]string, style *config.Style, maxAreaLevel string) {
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
		Operator:   "!=",
		Value:      []string{"Unique"},
	}
	compiledConditions := []string{
		baseTypeCondition.ConstructCompiledCondition(variables, rg.validBaseTypes),
		areaCondition.ConstructCompiledCondition(variables, rg.validBaseTypes),
		rarityCondition.ConstructCompiledCondition(variables, rg.validBaseTypes),
	}
	*allGeneratedRules = append(*allGeneratedRules, rg.ruleFactory.ConstructRule(ruleType, *style, compiledConditions))
}

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
		if overrideCond, found := ruleConditionsMap[key]; found {
			finalStandardConditions = append(finalStandardConditions, overrideCond)
			delete(ruleConditionsMap, key)
		} else {
			finalStandardConditions = append(finalStandardConditions, sectionCond)
		}
	}

	for _, ruleCond := range standardRuleConditions {
		key := ruleCond.Identifier + ":" + ruleCond.Operator
		if _, found := ruleConditionsMap[key]; found {
			finalStandardConditions = append(finalStandardConditions, ruleCond)
		}
	}

	compiledFinalConditions := make([]string, len(finalStandardConditions))
	for i, cond := range finalStandardConditions {
		compiledFinalConditions[i] = cond.ConstructCompiledCondition(rule.Variables, rule.ValidBaseTypes)
	}

	if len(macroConditions) == 0 {
		finalRule := rg.ruleFactory.ConstructRule(rule.Action, *rule.Style, compiledFinalConditions)
		return [][]string{finalRule}
	}

	var allGeneratedRules [][]string
	for _, macro := range macroConditions {
		var generatedForMacro [][]string
		switch macro.Identifier {
		case "@class_use":
			generatedForMacro = rg.handleClassUseMacro(rule.Action, rule.Style, compiledFinalConditions, macro, buildType, rule.Variables)
		default:
			panic("unknown macro: " + macro.Identifier)
		}
		allGeneratedRules = append(allGeneratedRules, generatedForMacro...)
	}
	return allGeneratedRules
}

func (rg *RuleGenerator) handleClassUseMacro(
	action model2.RuleType, style *config.Style, baseConditions []string,
	macro model2.Condition, buildType BuildType, variables *map[string][]string,
) [][]string {
	operator := macro.Operator
	value := macro.Value[0]

	var weaponClasses []string
	var armorClasses []string
	switch value {
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
		panic(fmt.Sprintf("invalid value for @class_use: %s", value))
	}

	weaponryCond := model2.Condition{Identifier: "@item_class", Operator: operator, Value: weaponClasses}
	compiledWeaponryCond := weaponryCond.ConstructCompiledCondition(variables, rg.validBaseTypes)
	finalWeaponryConditions := slices.Concat(baseConditions, []string{compiledWeaponryCond})
	weaponryRule := rg.ruleFactory.ConstructRule(action, *style, finalWeaponryConditions)

	armorCond := model2.Condition{Identifier: "@item_type", Operator: operator, Value: armorClasses}
	compiledArmorCond := armorCond.ConstructCompiledCondition(variables, rg.validBaseTypes)
	finalArmorConditions := slices.Concat(baseConditions, []string{compiledArmorCond})
	armorRule := rg.ruleFactory.ConstructRule(action, *style, finalArmorConditions)

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
