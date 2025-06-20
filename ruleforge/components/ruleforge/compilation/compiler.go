package compilation

import (
	"fmt"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/parsing/shared"
	shared2 "github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/transforming/shared"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/config"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/data_generation"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/rules/symbols"
	"log"
	"slices"
	"sort"
	"strings"
)

type ParsedRule struct {
	Style          *config.Style
	Action         RuleType
	Conditions     []condition
	Variables      *map[string][]string
	ValidBaseTypes []string
}

type Compiler struct {
	parseTree             *shared.ParseTree[symbols.LexingTokenType]
	compilerConfiguration CompilerConfiguration
	ruleFactory           *RuleFactory
	styles                *map[string]*config.Style
	validBaseTypes        []string
	armorBases            []data_generation.ItemBase
	weaponBases           []data_generation.ItemBase
	flaskBases            []data_generation.ItemBase
}

func NewCompiler(parseTree *shared.ParseTree[symbols.LexingTokenType], configuration CompilerConfiguration, validBaseTypes []string, itemBases []data_generation.ItemBase) *Compiler {
	var armorBaseTypes []data_generation.ItemBase
	var weaponBaseTypes []data_generation.ItemBase
	var flaskBaseTypes []data_generation.ItemBase

	utils := NewPobUtils()

	for _, item := range itemBases {
		if !slices.Contains(validBaseTypes, item.GetBaseType()) {
			continue
		}

		if utils.IsArmor(item) {
			armorBaseTypes = append(armorBaseTypes, item)
		} else if utils.IsWeapon(item) {
			weaponBaseTypes = append(weaponBaseTypes, item)
		} else if utils.IsFlask(item) {
			flaskBaseTypes = append(flaskBaseTypes, item)
		}
	}

	return &Compiler{
		parseTree:             parseTree,
		compilerConfiguration: configuration,
		ruleFactory:           &RuleFactory{},
		validBaseTypes:        validBaseTypes,
		armorBases:            armorBaseTypes,
		weaponBases:           weaponBaseTypes,
		flaskBases:            flaskBaseTypes,
	}
}

func (c *Compiler) CompileIntoFilter() ([]string, error, string) {
	styles, err := config.LoadStyles(c.compilerConfiguration.StyleJsonPath)
	c.styles = &styles

	if err != nil {
		return nil, err, ""
	}

	output := make([]string, 0)
	variables := make(map[string][]string)

	filterName := "UNKNOWN_THIS_SHOULD_NOT_HAPPEN"
	build := ""

	metadataNode := c.parseTree.FindSymbolNode(symbols.ParseSymbolRootMetadata.String())
	c.extractMetadataText(&output, &filterName, &build)(metadataNode)

	variableNodes := c.parseTree.FindAllSymbolNodes(symbols.ParseSymbolVariable.String())
	for _, variableNode := range variableNodes {
		c.extractVariables(&variables)(variableNode)
	}

	buildType := GetBuildType(build)

	sections := c.parseTree.FindAllSymbolNodes(symbols.ParseSymbolSection.String())
	for _, sectionNode := range sections {
		c.handleSection(&output, &variables, buildType)(sectionNode)
	}

	output = append(output, c.constructSectionHeading("Fallback", "Shows anything that wasn't caught by upstream rules."), "")

	fallbackRule := c.ruleFactory.ConstructRule(ShowRule, *styles["Fallback"], []string{})
	output = append(output, fallbackRule...)

	return output, nil, filterName
}

func (c *Compiler) extractMetadataText(metadataText *[]string, filterName, build *string) shared2.TransformCallback[symbols.LexingTokenType] {
	return func(node *shared.ParseTree[symbols.LexingTokenType]) {
		assignments := node.FindAllSymbolNodes(symbols.ParseSymbolAssignment.String())

		filterVersion := "<unknown>"
		filterStrictness := "<unknown>"

		for _, assignment := range assignments {
			key, value := c.extractAssignmentKeyAndValue(assignment)

			switch key {
			case "NAME":
				*filterName = value
				break
			case "VERSION":
				filterVersion = value
				break
			case "STRICTNESS":
				filterStrictness = value
				break
			case "BUILD":
				*build = value
			}
		}

		lines := []string{
			"This filter is automatically generated through the Ruleforge program.",
			"Ruleforge metadata (from the user's script): ",
			fmt.Sprintf("Ruleforge \"%s\" @ %s -> strictness: %s", *filterName, filterVersion, filterStrictness),
			"",
			"For questions reach out to Mr. Hoorn (Ruleforge author):",
			"Discord: \"mr.hoornasp.learningexpert\" (without quotations)",
			"Email: md.career@protonmail.com",
			"-----------------------------------------------------------------------",
		}

		for _, line := range lines {
			commented := c.constructComment(line)
			*metadataText = append(*metadataText, commented)
		}

	}
}

func (c *Compiler) extractAssignmentKeyAndValue(assignment *shared.ParseTree[symbols.LexingTokenType]) (string, string) {
	key := assignment.Children[0].Token.ValueToString()
	value := assignment.Children[2].Token.ValueToString()

	return key, value
}

func (c *Compiler) constructComment(content string) string {
	return fmt.Sprintf("# %s", content)
}

func (c *Compiler) extractVariables(variables *map[string][]string) shared2.TransformCallback[symbols.LexingTokenType] {
	return func(node *shared.ParseTree[symbols.LexingTokenType]) {
		assignmentNodes := node.FindAllSymbolNodes(symbols.ParseSymbolAssignment.String())

		for _, assignmentNode := range assignmentNodes {
			identifier := assignmentNode.Children[1].Token.ValueToString()
			valueNodes := assignmentNode.FindAllSymbolNodes(symbols.ParseSymbolValue.String())

			assignments := make([]string, 0, len(valueNodes))

			for _, valueNode := range valueNodes {
				assignments = append(assignments, valueNode.Token.ValueToString())
			}

			if *variables == nil {
				*variables = make(map[string][]string)
			}

			(*variables)[identifier] = assignments
		}
	}
}

func (c *Compiler) handleSection(output *[]string, variables *map[string][]string, build BuildType) shared2.TransformCallback[symbols.LexingTokenType] {
	return func(node *shared.ParseTree[symbols.LexingTokenType]) {
		sectionMetadata := node.FindSymbolNode(symbols.ParseSymbolSectionMetadata.String())
		assignments := sectionMetadata.FindAllSymbolNodes(symbols.ParseSymbolAssignment.String())

		sectionName := "<unknown>"
		sectionDescription := "<unknown>"

		for _, assignment := range assignments {
			key, value := c.extractAssignmentKeyAndValue(assignment)

			switch key {
			case "NAME":
				sectionName = value
				break
			case "DESCRIPTION":
				sectionDescription = value
				break
			}
		}

		*output = append(*output, c.constructSectionHeading(sectionName, sectionDescription))

		// 1. Get section-wide conditions BUT DO NOT COMPILE THEM YET.
		conditionListNode := node.FindSymbolNode(symbols.ParseSymbolConditionList.String())
		sectionRuleConditions := c.retrieveConditions(conditionListNode)

		// 2. Delegate rule extraction and compilation, passing the RAW conditions.
		ruleListNode := node.FindSymbolNode(symbols.ParseSymbolRules.String())
		compiledRules := c.extractAndCompileRules(ruleListNode, sectionRuleConditions, variables, build)

		// 3. Append the results to the output
		for _, rule := range compiledRules {
			*output = append(*output, rule...)
		}
	}
}

func (c *Compiler) extractAndCompileRules(
	ruleListNode *shared.ParseTree[symbols.LexingTokenType],
	sectionConditions []condition,
	variables *map[string][]string, buildType BuildType) [][]string {
	var allGeneratedRules [][]string

	for _, childNode := range ruleListNode.Children {
		// --- 1. Parse the expression into a high-level ParsedRule struct ---
		switch childNode.Symbol {
		case symbols.ParseSymbolRuleExpression.String():
			c.handleRuleExpression(childNode, variables, sectionConditions, buildType, &allGeneratedRules)
		case symbols.ParseSymbolMacroExpression.String():
			c.handleMacroExpression(childNode, variables, sectionConditions, buildType, &allGeneratedRules)
		default:
			panic("Unsupported symbol: " + childNode.Symbol)
		}
	}

	return allGeneratedRules
}

func (c *Compiler) handleRuleExpression(
	ruleExpressionNode *shared.ParseTree[symbols.LexingTokenType],
	variables *map[string][]string,
	sectionConditions []condition,
	buildType BuildType,
	allGeneratedRules *[][]string) {
	styleValue := ruleExpressionNode.Children[2].Token.ValueToString()
	style := c.extractStyle(styleValue, variables)

	showOrHideStr := ruleExpressionNode.Children[4].Token.ValueToString()[1:]
	var action RuleType
	if showOrHideStr == "Show" {
		action = ShowRule
	} else {
		action = HideRule
	}

	ruleSpecificConditions := c.retrieveConditions(ruleExpressionNode)

	rule := &ParsedRule{
		Style:          style,
		Action:         action,
		Conditions:     ruleSpecificConditions,
		Variables:      variables,
		ValidBaseTypes: c.validBaseTypes,
	}

	// --- 2. Delegate compilation to a dedicated function ---
	generatedRules := c.compileParsedRule(rule, sectionConditions, buildType)
	*allGeneratedRules = append(*allGeneratedRules, generatedRules...)
}

func (c *Compiler) handleMacroExpression(
	macroExpressionNode *shared.ParseTree[symbols.LexingTokenType],
	variables *map[string][]string,
	sectionConditions []condition,
	buildType BuildType,
	allGeneratedRules *[][]string) {
	macroType := macroExpressionNode.Children[1].Token.ValueToString()
	styleOneKey := macroExpressionNode.Children[3].Token.ValueToString()
	styleOneValue := macroExpressionNode.Children[5].Token.ValueToString()
	styleTwoKey := macroExpressionNode.Children[7].Token.ValueToString()
	styleTwoValue := macroExpressionNode.Children[9].Token.ValueToString()

	var hiddenStyle *config.Style
	var shownStyle *config.Style

	if styleOneKey == "$hidden" {
		hiddenStyle = c.extractStyle(styleOneValue, variables)
	} else if styleOneKey == "$show" {
		shownStyle = c.extractStyle(styleOneValue, variables)
	} else {
		panic("Unsupported style state: " + styleOneKey)
	}

	if styleTwoKey == "$hidden" {
		hiddenStyle = c.extractStyle(styleTwoValue, variables)
	} else if styleTwoKey == "$show" {
		shownStyle = c.extractStyle(styleTwoValue, variables)
	} else {
		panic("Unsupported style state: " + styleTwoKey)
	}

	if hiddenStyle == nil || shownStyle == nil {
		panic("One or multiple style states missing (ensure you have both 'Hidden' and 'Show' defined)")
	}

	switch macroType {
	case "item_progression":
		c.handleItemProgression(variables, buildType, allGeneratedRules, hiddenStyle, shownStyle)
	default:
		panic("Unsupported macro type: " + macroType)
	}
}

// handleItemProgression generates rules to show and then hide item base types progressively.
// It groups items with the same drop level, showing them all as the "current" tier. When a
// higher-level tier of items becomes available, it generates rules to hide all items from the previous tier.
func (c *Compiler) handleItemProgression(
	variables *map[string][]string,
	buildType BuildType,
	allGeneratedRules *[][]string,
	hiddenStyle, shownStyle *config.Style,
) {
	// 1. Group all relevant weapons and armor by their category (Type).
	itemsByCategory := make(map[string][]*data_generation.ItemBase)
	for i := range c.weaponBases {
		weapon := c.weaponBases[i]
		if IsWeaponAssociatedWithBuild(weapon, buildType) {
			itemsByCategory[weapon.Type] = append(itemsByCategory[weapon.Type], &c.weaponBases[i])
		}
	}
	for i := range c.armorBases {
		armor := c.armorBases[i]
		if IsArmorAssociatedWithBuild(armor, buildType) {
			itemsByCategory[armor.Type] = append(itemsByCategory[armor.Type], &c.armorBases[i])
		}
	}
	for i := range c.flaskBases {
		flask := c.flaskBases[i]

		if flask.SubType == "Life" || flask.SubType == "Mana" || flask.SubType == "Hybrid" {
			itemsByCategory[flask.Type+flask.SubType] = append(itemsByCategory[flask.Type+flask.SubType], &c.flaskBases[i])
		} else {
			itemsByCategory[flask.GetBaseType()] = append(itemsByCategory[flask.GetBaseType()], &c.flaskBases[i])
		}
	}

	// 2. For each category, sort the items by their drop level.
	for category := range itemsByCategory {
		sort.Slice(itemsByCategory[category], func(i, j int) bool {
			itemA := itemsByCategory[category][i]
			itemB := itemsByCategory[category][j]
			dropLevelA := 0
			if itemA.DropLevel != nil {
				dropLevelA = *itemA.DropLevel
			}
			dropLevelB := 0
			if itemB.DropLevel != nil {
				dropLevelB = *itemB.DropLevel
			}
			if dropLevelA == dropLevelB {
				return itemA.Name < itemB.Name
			}
			return dropLevelA < dropLevelB
		})
	}

	// 3. Build the progression rules (Show and Hide) for each sorted category.
	for _, categoryItems := range itemsByCategory {
		if len(categoryItems) == 0 {
			continue
		}

		i := 0
		for i < len(categoryItems) {
			currentItem := categoryItems[i]
			startLevel := 0
			if currentItem.DropLevel != nil {
				startLevel = *currentItem.DropLevel
			}

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
					c.constructItemProgressionRule(variables, ShowRule, *tierItem, allGeneratedRules, shownStyle, fmt.Sprintf("%d", showEndLevel))
				}
			}

			if !isLastTier && hideStartLevel <= 69 {
				hideEndLevel := 69
				for _, tierItem := range currentTierItems {
					c.constructItemProgressionRule(variables, HideRule, *tierItem, allGeneratedRules, hiddenStyle, fmt.Sprintf("%d", hideEndLevel))
				}
			}

			i = tierEndIndex + 1
		}
	}
}

// getDropLevel is a helper function to safely get an item's drop level, defaulting to 0.
func getDropLevel(item *data_generation.ItemBase) int {
	if item.DropLevel != nil {
		return *item.DropLevel
	}
	log.Printf("WARNING: Item '%s' has a nil DropLevel; defaulting to level 0.", item.Name)
	return 0
}

func (c *Compiler) constructItemProgressionRule(variables *map[string][]string, ruleType RuleType, item data_generation.ItemBase, allGeneratedRules *[][]string, style *config.Style, maxAreaLevel string) {
	areaCondition := condition{
		identifier: "@area_level",
		operator:   "<=",
		value:      []string{maxAreaLevel},
	}
	baseTypeCondition := condition{
		identifier: "@item_type",
		operator:   "==",
		value:      []string{item.GetBaseType()},
	}
	rarityCondition := condition{
		identifier: "@rarity",
		operator:   "!=",
		value:      []string{"Unique"},
	}
	compiledConditions := []string{
		baseTypeCondition.ConstructCompiledCondition(variables, c.validBaseTypes),
		areaCondition.ConstructCompiledCondition(variables, c.validBaseTypes),
		rarityCondition.ConstructCompiledCondition(variables, c.validBaseTypes),
	}
	*allGeneratedRules = append(*allGeneratedRules, c.ruleFactory.ConstructRule(ruleType, *style, compiledConditions))
}

func (c *Compiler) compileParsedRule(rule *ParsedRule, sectionConditions []condition, buildType BuildType) [][]string {
	// 1. Separate the rule's own conditions into standard and macro.
	var macroConditions []condition
	var standardRuleConditions []condition
	macros := []string{"@class_use"}

	for _, cond := range rule.Conditions {
		if slices.Contains(macros, cond.identifier) {
			macroConditions = append(macroConditions, cond)
		} else {
			standardRuleConditions = append(standardRuleConditions, cond)
		}
	}

	// 2. Build the final list of conditions, preserving section order and handling overrides.
	ruleConditionsMap := make(map[string]condition, len(standardRuleConditions))
	for _, ruleCond := range standardRuleConditions {
		key := ruleCond.identifier + ":" + ruleCond.operator
		ruleConditionsMap[key] = ruleCond
	}

	finalStandardConditions := make([]condition, 0, len(standardRuleConditions)+len(sectionConditions))

	for _, sectionCond := range sectionConditions {
		key := sectionCond.identifier + ":" + sectionCond.operator

		if overrideCond, found := ruleConditionsMap[key]; found {
			finalStandardConditions = append(finalStandardConditions, overrideCond)
			delete(ruleConditionsMap, key)
		} else {
			finalStandardConditions = append(finalStandardConditions, sectionCond)
		}
	}

	for _, ruleCond := range standardRuleConditions {
		key := ruleCond.identifier + ":" + ruleCond.operator
		if _, found := ruleConditionsMap[key]; found {
			finalStandardConditions = append(finalStandardConditions, ruleCond)
		}
	}

	// 3. Compile the final, merged list of standard conditions into strings.
	compiledFinalConditions := make([]string, len(finalStandardConditions))
	for i, cond := range finalStandardConditions {
		compiledFinalConditions[i] = cond.ConstructCompiledCondition(rule.Variables, rule.ValidBaseTypes)
	}

	// 4. Handle macros using the final compiled base conditions.
	if len(macroConditions) == 0 {
		finalRule := c.ruleFactory.ConstructRule(rule.Action, *rule.Style, compiledFinalConditions)
		return [][]string{finalRule}
	}

	var allGeneratedRules [][]string
	for _, macro := range macroConditions {
		var generatedForMacro [][]string
		switch macro.identifier {
		case "@class_use":
			generatedForMacro = c.handleClassUseMacro(rule.Action, rule.Style, compiledFinalConditions, macro, buildType, rule.Variables)
		default:
			panic("unknown macro: " + macro.identifier)
		}
		allGeneratedRules = append(allGeneratedRules, generatedForMacro...)
	}
	return allGeneratedRules
}

// handleClassUseMacro generates all rules associated with a @class_use macro.
// This is where you will implement your weaponry and equipment logic.
func (c *Compiler) handleClassUseMacro(
	action RuleType, style *config.Style, baseConditions []string,
	macro condition, buildType BuildType, variables *map[string][]string) [][]string {
	operator := macro.operator
	value := macro.value[0]

	var weaponClasses []string
	var armorClasses []string
	switch value {
	case "true":
		weaponClasses = GetAssociatedWeaponClasses(buildType)
		for _, item := range c.armorBases {
			if IsArmorAssociatedWithBuild(item, buildType) {
				armorClasses = append(armorClasses, item.GetBaseType())
			}
		}
	case "false":
		weaponClasses = GetUnassociatedWeaponClasses(buildType)
		for _, item := range c.armorBases {
			if !IsArmorAssociatedWithBuild(item, buildType) {
				armorClasses = append(armorClasses, item.GetBaseType())
			}
		}
	default:
		panic(fmt.Sprintf("invalid value for @class_use: %s", value))
	}

	weaponryCond := condition{identifier: "@item_class", operator: operator, value: weaponClasses}
	compiledWeaponryCond := weaponryCond.ConstructCompiledCondition(variables, c.validBaseTypes)
	finalWeaponryConditions := slices.Concat(baseConditions, []string{compiledWeaponryCond})
	weaponryRule := c.ruleFactory.ConstructRule(action, *style, finalWeaponryConditions)

	armorCond := condition{identifier: "@item_type", operator: operator, value: armorClasses}
	compiledArmorCond := armorCond.ConstructCompiledCondition(variables, c.validBaseTypes)
	finalArmorConditions := slices.Concat(baseConditions, []string{compiledArmorCond})
	armorRule := c.ruleFactory.ConstructRule(action, *style, finalArmorConditions)

	// --- Combine and Return ---
	return [][]string{weaponryRule, armorRule}
}

func (c *Compiler) constructSectionHeading(sectionName, sectionDescription string) string {
	return c.constructComment(fmt.Sprintf("---------------- SECTION: %s (%s) ----------------", sectionName, sectionDescription))
}

func (c *Compiler) extractStyle(styleValue string, variables *map[string][]string) *config.Style {
	if !isVariableRef(styleValue) {
		return c.lookupStyle(styleValue)
	}
	return c.resolveVariableStyle(styleValue, *variables)
}

func isVariableRef(value string) bool {
	return strings.HasPrefix(value, "$")
}

func stripVarPrefix(value string) string {
	return strings.TrimPrefix(value, "$")
}

func (c *Compiler) lookupStyle(key string) *config.Style {
	style, ok := (*c.styles)[key]
	if !ok {
		panic(fmt.Sprintf("style %q not found", key))
	}
	return style
}

func (c *Compiler) getVariableRefs(name string, variables map[string][]string) []string {
	refs, ok := variables[name]
	if !ok || len(refs) == 0 {
		panic(fmt.Sprintf("no variable style reference found for %q", name))
	}
	return refs
}

func (c *Compiler) resolveVariableStyle(styleValue string, variables map[string][]string) *config.Style {
	varName := stripVarPrefix(styleValue)
	refs := c.getVariableRefs(varName, variables)

	var merged *config.Style
	for i, ref := range refs {
		var toMerge *config.Style

		if isVariableRef(ref) {
			// recurse if this ref is itself a variable
			toMerge = c.extractStyle(ref, &variables)
		} else {
			toMerge = c.lookupStyle(ref)
		}

		if i == 0 {
			merged = toMerge
			continue
		}
		var err error
		if merged == nil {
			panic("merged style is nil")
		}

		merged, err = merged.MergeStyles(toMerge)
		if err != nil {
			panic(fmt.Sprintf("error merging style %q: %v", ref, err))
		}
	}

	return merged
}

func (c *Compiler) retrieveConditions(node *shared.ParseTree[symbols.LexingTokenType]) []condition {
	conditionNodes := node.FindAllSymbolNodes(symbols.ParseSymbolConditionExpression.String())
	conditions := make([]condition, len(conditionNodes))

	for i, conditionNode := range conditionNodes {
		identifier := conditionNode.Children[1].Token.ValueToString()
		operator := conditionNode.Children[2].Token.ValueToString()
		value := conditionNode.Children[3].Token.ValueToString()

		conditions[i] = condition{
			identifier: identifier,
			operator:   operator,
			value:      []string{value},
		}
	}
	return conditions
}

// TODO - Refactor
