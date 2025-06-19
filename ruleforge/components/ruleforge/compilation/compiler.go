package compilation

import (
	"fmt"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/parsing/shared"
	shared2 "github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/transforming/shared"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/config"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/rules/symbols"
	"slices"
	"strings"
)

type Compiler struct {
	parseTree             *shared.ParseTree[symbols.LexingTokenType]
	compilerConfiguration CompilerConfiguration
	ruleFactory           *RuleFactory
	styles                *map[string]*config.Style
	validBaseTypes        []string
}

func NewCompiler(parseTree *shared.ParseTree[symbols.LexingTokenType], configuration CompilerConfiguration, validBaseTypes []string) *Compiler {
	return &Compiler{
		parseTree:             parseTree,
		compilerConfiguration: configuration,
		ruleFactory:           &RuleFactory{},
		validBaseTypes:        validBaseTypes,
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

		conditionListNode := node.FindSymbolNode(symbols.ParseSymbolConditionList.String())
		conditions := c.retrieveConditions(conditionListNode, build)

		compiledSectionConditions := make([]string, len(conditions))
		*output = append(*output, "")
		for i, condition := range conditions {
			compiledSectionConditions[i] = condition.ConstructCompiledCondition(variables, c.validBaseTypes)
		}

		ruleListNode := node.FindSymbolNode(symbols.ParseSymbolRules.String())

		rules := c.extractRules(ruleListNode, compiledSectionConditions, variables, build)
		*output = append(*output, rules...)
	}
}

func (c *Compiler) constructSectionHeading(sectionName, sectionDescription string) string {
	return c.constructComment(fmt.Sprintf("---------------- SECTION: %s (%s) ----------------", sectionName, sectionDescription))
}

func (c *Compiler) extractRules(
	ruleListNode *shared.ParseTree[symbols.LexingTokenType],
	compiledSectionConditions []string, variables *map[string][]string,
	buildType BuildType) []string {
	ruleExpressions := ruleListNode.FindAllSymbolNodes(symbols.ParseSymbolRuleExpression.String())

	ruleLines := make([]string, 0)

	for _, ruleExpressionNode := range ruleExpressions {
		styleValue := ruleExpressionNode.Children[2].Token.ValueToString()
		style := c.extractStyle(styleValue, variables)

		showOrHide := ruleExpressionNode.Children[4].Token.ValueToString()[1:]

		ruleConditionsRaw := c.retrieveConditions(ruleExpressionNode, buildType)
		compiledRuleConditions := make([]string, len(ruleConditionsRaw))
		for i, condition := range ruleConditionsRaw {
			compiledRuleConditions[i] = condition.ConstructCompiledCondition(variables, c.validBaseTypes)
		}

		mergedConditions := slices.Concat(compiledSectionConditions, compiledRuleConditions)

		var rule []string
		switch showOrHide {
		case "Show":
			rule = c.ruleFactory.ConstructRule(ShowRule, *style, mergedConditions)
			break
		case "Hide":
			rule = c.ruleFactory.ConstructRule(HideRule, *style, mergedConditions)
			break
		default:
			panic(fmt.Sprintf("unknown showOrHide value: %s", showOrHide))
		}

		ruleLines = append(ruleLines, rule...)
	}

	return ruleLines
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

func (c *Compiler) retrieveConditions(conditionListNode *shared.ParseTree[symbols.LexingTokenType], buildType BuildType) []condition {
	conditionNodes := conditionListNode.FindAllSymbolNodes(symbols.ParseSymbolConditionExpression.String())

	conditions := make([]condition, len(conditionNodes))

	for i, conditionNode := range conditionNodes {
		identifier := conditionNode.Children[1].Token.ValueToString()
		operator := conditionNode.Children[2].Token.ValueToString()
		value := conditionNode.Children[3].Token.ValueToString()

		switch identifier {
		case "@class_use":
			c.handleClassUseCondition(i, &conditions, operator, value, buildType)
		default:
			c.handleGenericCondition(i, &conditions, identifier, operator, value)
		}
	}

	return conditions
}

func (c *Compiler) handleClassUseCondition(conditionIndex int, conditions *[]condition, operator string, value string, build BuildType) {
	associatedWeaponry := GetAssociatedWeaponClasses(build)
	unassociatedWeaponry := GetUnassociatedWeaponClasses(build)

	var classes []string

	switch value {
	case "false":
		classes = unassociatedWeaponry
		break
	case "true":
		classes = associatedWeaponry
		break
	default:
		panic(fmt.Sprintf("invalid value for class use condition: %s", value))
	}

	(*conditions)[conditionIndex] = condition{
		identifier: "@item_class",
		operator:   operator,
		value:      classes,
	}
}

func (c *Compiler) handleGenericCondition(
	conditionIndex int, conditions *[]condition,
	identifier, operator, value string) {
	(*conditions)[conditionIndex] = condition{
		identifier: identifier,
		operator:   operator,
		value:      []string{value},
	}
}
