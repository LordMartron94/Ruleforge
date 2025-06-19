package compilation

import (
	"fmt"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/parsing/shared"
	shared2 "github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/transforming/shared"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/config"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/data_generation"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/rules/symbols"
	"slices"
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
}

func NewCompiler(parseTree *shared.ParseTree[symbols.LexingTokenType], configuration CompilerConfiguration, validBaseTypes []string, itemBases []data_generation.ItemBase) *Compiler {
	var armorBaseTypes []data_generation.ItemBase

	utils := NewPobUtils()

	for _, item := range itemBases {
		if utils.IsArmor(item) {
			armorBaseTypes = append(armorBaseTypes, item)
		}
	}

	return &Compiler{
		parseTree:             parseTree,
		compilerConfiguration: configuration,
		ruleFactory:           &RuleFactory{},
		validBaseTypes:        validBaseTypes,
		armorBases:            armorBaseTypes,
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
	ruleExpressions := ruleListNode.FindAllSymbolNodes(symbols.ParseSymbolRuleExpression.String())
	var allGeneratedRules [][]string

	for _, ruleExpressionNode := range ruleExpressions {
		// --- 1. Parse the expression into a high-level ParsedRule struct ---
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
		allGeneratedRules = append(allGeneratedRules, generatedRules...)
	}

	return allGeneratedRules
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

	// --- Rule 1: Weaponry (Example Implementation) ---
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
