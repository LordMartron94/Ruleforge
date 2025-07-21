package rules

import (
	// Use aliased imports for brevity and clarity.
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/parsing/rules/atomic"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/parsing/rules/composite"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/parsing/rules/conditional"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/parsing/rules/shared"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/rules/symbols"
)

// --- Re-usable, Stateless Rule Components ---

var (
	// whitespaceOptional matches zero or more whitespace or newline tokens.
	whitespaceOptional = composite.NewRepetitionRule[symbols.LexingTokenType](
		symbols.ParseSymbolWhitespace.String(),
		atomic.NewSingleTokenRule(symbols.ParseSymbolWhitespace.String(), symbols.WhitespaceToken),
		atomic.NewSingleTokenRule(symbols.ParseSymbolWhitespace.String(), symbols.NewLineToken),
	)
	// requiredWhitespace matches exactly one mandatory Whitespace token.
	requiredWhitespace = atomic.NewSingleTokenRule(symbols.ParseSymbolWhitespace.String(), symbols.WhitespaceToken)

	strictnessAssignmentValues = conditional.NewChoiceTokenRule(symbols.ParseSymbolValue.String(),
		[]symbols.LexingTokenType{
			symbols.AllKeywordToken, symbols.SoftKeywordToken, symbols.SemiStrictKeywordToken,
			symbols.StrictKeywordToken, symbols.SuperStrictKeywordToken,
		},
	)

	buildAssignmentValues = conditional.NewChoiceTokenRule(symbols.ParseSymbolValue.String(),
		[]symbols.LexingTokenType{
			symbols.MeleeBuildToken, symbols.DexBuildToken, symbols.SpellBuildToken,
			symbols.MeleeSpellHybridBuildToken, symbols.MeleeDexHybridBuildToken, symbols.SpellDexHybridBuildToken, symbols.IdentifierValueToken,
		},
	)
)

// GetParsingRules returns the list of all top-level parsing rules.
func GetParsingRules() []shared.ParsingRuleInterface[symbols.LexingTokenType] {
	return []shared.ParsingRuleInterface[symbols.LexingTokenType]{
		// The order of these top-level rules is the priority for parsing.
		metadataRule(symbols.ParseSymbolRootMetadata),
		sectionRule(),
		variableRule(),
		importRule(),
		// Fallbacks for any remaining standalone tokens.
		atomic.NewSingleTokenRule(symbols.ParseSymbolWhitespace.String(), symbols.NewLineToken),
		atomic.NewSingleTokenRule(symbols.ParseSymbolWhitespace.String(), symbols.WhitespaceToken),
		conditional.NewAnyTokenRule[symbols.LexingTokenType](symbols.ParseSymbolAny.String()),
	}
}

func importRule() shared.ParsingRuleInterface[symbols.LexingTokenType] {
	return seq(
		symbols.ParseSymbolImport,
		token(symbols.ParseSymbolKey, symbols.ImportKeywordToken),
		token(symbols.ParseSymbolValue, symbols.IdentifierValueToken),
	)
}

// --- High-Level Section Rules ---

func metadataRule(metadataSymbol symbols.ParseSymbol) shared.ParsingRuleInterface[symbols.LexingTokenType] {
	assignments := composite.NewRepetitionRule[symbols.LexingTokenType](
		symbols.ParseSymbolAssignments.String(),
		nameAssignment(),
		versionAssignment(),
		strictnessAssignment(),
		descriptionAssignment(),
		buildAssignment(),
		whitespaceOptional, // Allows whitespace/newlines between assignments.
	)

	return seq(metadataSymbol,
		token(symbols.ParseSymbolKeyword, symbols.MetadataKeywordToken),
		token(symbols.ParseSymbolBlockOperator, symbols.OpenCurlyBracketToken),
		assignments,
		token(symbols.ParseSymbolBlockOperator, symbols.CloseCurlyBracketToken),
	)
}

func sectionRule() shared.ParsingRuleInterface[symbols.LexingTokenType] {
	sectionContent := composite.NewRepetitionRule[symbols.LexingTokenType](
		symbols.ParseSymbolSectionContent.String(),
		metadataRule(symbols.ParseSymbolSectionMetadata),
		conditionListRule(),
		ruleSectionRule(),
		whitespaceOptional, // Allow whitespace between inner sections
	)

	return seq(symbols.ParseSymbolSection,
		token(symbols.ParseSymbolKeyword, symbols.SectionKeywordToken),
		token(symbols.ParseSymbolBlockOperator, symbols.OpenCurlyBracketToken),
		sectionContent,
		token(symbols.ParseSymbolBlockOperator, symbols.CloseCurlyBracketToken),
	)
}

func conditionListRule() shared.ParsingRuleInterface[symbols.LexingTokenType] {
	conditions := composite.NewRepetitionRule[symbols.LexingTokenType](symbols.ParseSymbolConditions.String(),
		conditionRule(),
		whitespaceOptional,
	)
	return seq(symbols.ParseSymbolConditionList,
		token(symbols.ParseSymbolKeyword, symbols.SectionConditionsKeywordToken),
		token(symbols.ParseSymbolBlockOperator, symbols.OpenCurlyBracketToken),
		conditions,
		token(symbols.ParseSymbolBlockOperator, symbols.CloseCurlyBracketToken),
	)
}

func ruleSectionRule() shared.ParsingRuleInterface[symbols.LexingTokenType] {
	rules := composite.NewRepetitionRule[symbols.LexingTokenType](symbols.ParseSymbolRules.String(),
		ruleExpressionRule(),
		macroExpressionRule(),
		whitespaceOptional,
	)

	return seq(symbols.ParseSymbolRuleSection,
		token(symbols.ParseSymbolKeyword, symbols.RuleKeywordToken),
		token(symbols.ParseSymbolBlockOperator, symbols.OpenCurlyBracketToken),
		rules,
		token(symbols.ParseSymbolBlockOperator, symbols.CloseCurlyBracketToken),
	)
}

func ruleExpressionRule() shared.ParsingRuleInterface[symbols.LexingTokenType] {
	valueOpts := conditional.NewChoiceTokenRule(symbols.ParseSymbolValue.String(),
		[]symbols.LexingTokenType{
			symbols.VariableReferenceToken, symbols.IdentifierValueToken,
		},
	)

	normalExpression := seq(symbols.ParseSymbolRuleExpression,
		conditionRule(),
		token(symbols.ParseSymbolOperator, symbols.AssignmentOperatorToken),
		valueOpts,
		token(symbols.ParseSymbolOperator, symbols.AssignmentOperatorToken),
		valueOpts,
	)

	elaborateExpression := seq(symbols.ParseSymbolRuleExpression,
		conditionRule(),
		token(symbols.ParseSymbolOperator, symbols.AssignmentOperatorToken),
		valueOpts,
		token(symbols.ParseSymbolOperator, symbols.AssignmentOperatorToken),
		valueOpts,
		token(symbols.ParseSymbolKeyword, symbols.RuleStrictnessIndicatorToken),
		strictnessAssignmentValues,
	)

	return composite.NewChoiceRule(symbols.ParseSymbolRuleExpression.String(), []shared.ParsingRuleInterface[symbols.LexingTokenType]{elaborateExpression, normalExpression})
}

func macroExpressionRule() shared.ParsingRuleInterface[symbols.LexingTokenType] {
	// --- Parameter Rules ---

	// 1. Define a new rule that accepts either a variable reference or an identifier.
	//    This creates a <Value> node in the parse tree.
	parameterValueOptions := conditional.NewChoiceTokenRule(symbols.ParseSymbolValue.String(),
		[]symbols.LexingTokenType{
			symbols.VariableReferenceToken,
			symbols.IdentifierValueToken,
		},
	)

	// 2. Update the parameter definition to use this new choice rule for its value.
	//    This rule matches '-> $key => ($value | Identifier)'
	parameter := seq(symbols.ParseSymbolParameter,
		token(symbols.ParseSymbolOperator, symbols.ChainOperatorToken),
		token(symbols.ParseSymbolKey, symbols.VariableReferenceToken),
		token(symbols.ParseSymbolOperator, symbols.AssignmentOperatorToken),
		parameterValueOptions,
	)

	// The list of parameters is a simple repetition of the 'parameter' rule.
	parameterList := composite.NewRepetitionRule[symbols.LexingTokenType](
		symbols.ParseSymbolParameterList.String(),
		parameter,
		whitespaceOptional,
	)

	// --- Final Macro Expression ---
	return seq(symbols.ParseSymbolMacroExpression,
		token(symbols.ParseSymbolKeyword, symbols.FunctionKeywordToken),
		token(symbols.ParseSymbolBlockOperator, symbols.OpenSquareBracketToken),
		token(symbols.ParseSymbolValue, symbols.IdentifierValueToken),
		parameterList,
		token(symbols.ParseSymbolBlockOperator, symbols.CloseSquareBracketToken),
	)
}

// --- Assignment and Declaration Rules ---

func conditionRule() shared.ParsingRuleInterface[symbols.LexingTokenType] {
	// Defines the set of valid comparison operators (e.g., >=, <=, ==).
	comparisonOps := conditional.NewChoiceTokenRule(symbols.ParseSymbolOperator.String(),
		[]symbols.LexingTokenType{
			symbols.GreaterThanOrEqualOperatorToken, symbols.LessThanOrEqualOperatorToken,
			symbols.GreaterThanOperatorToken, symbols.LessThanOperatorToken, symbols.ExactMatchOperatorToken, symbols.NotEqualToOperatorToken,
		},
	)

	// Defines the set of valid value types for a condition.
	valueOpts := conditional.NewChoiceTokenRule(symbols.ParseSymbolValue.String(),
		[]symbols.LexingTokenType{
			symbols.VariableReferenceToken, symbols.NumberToken, symbols.IdentifierValueToken,
		},
	)

	// Defines the `WHERE <identifier> <op> <value>` part as a single, flat sequence.
	// This creates one ConditionExpression node with four children.
	initialPart := seqWithSeparator(whitespaceOptional, symbols.ParseSymbolConditionExpression,
		token(symbols.ParseSymbolKeyword, symbols.ConditionAssignmentKeywordToken),
		token(symbols.ParseSymbolIdentifier, symbols.ConditionKeywordToken),
		comparisonOps,
		valueOpts,
	)

	// Defines the `-> <identifier> <op> <value>` part for chained conditions.
	// This also creates a single, flat ConditionExpression node.
	chainedPart := seqWithSeparator(whitespaceOptional, symbols.ParseSymbolConditionExpression,
		token(symbols.ParseSymbolOperator, symbols.ChainOperatorToken),
		token(symbols.ParseSymbolIdentifier, symbols.ConditionKeywordToken),
		comparisonOps,
		valueOpts,
	)

	// The makeChainedRule helper correctly assembles the initial part with any
	// subsequent chained parts.
	return makeChainedRule(
		symbols.ParseSymbolCondition,
		symbols.ParseSymbolChainedConditions,
		initialPart,
		chainedPart,
	)
}

func variableRule() shared.ParsingRuleInterface[symbols.LexingTokenType] {
	valueOpts := conditional.NewChoiceTokenRule(symbols.ParseSymbolValue.String(),
		[]symbols.LexingTokenType{
			symbols.NumberToken,
			symbols.IdentifierValueToken,
			symbols.VariableReferenceToken,
		},
	)

	combinedValuePart := seq(symbols.ParseSymbolCombinedValue,
		token(symbols.ParseSymbolOperator, symbols.StyleCombineToken),
		valueOpts,
	)

	repeatingCombinedValues := composite.NewRepetitionRule[symbols.LexingTokenType](
		symbols.ParseSymbolChainedValues.String(),
		whitespaceOptional,
		combinedValuePart,
	)

	fullValueExpression := composite.NewNestedRule[symbols.LexingTokenType](
		symbols.ParseSymbolFullValueExpression.String(),
		valueOpts,
		repeatingCombinedValues,
	)

	optionalOverrideBlock := composite.NewRepetitionRule[symbols.LexingTokenType](
		symbols.ParseSymbolOptionalOverrides.String(),
		styleOverrideBlockRule(),
	)

	initialPart := seq(symbols.ParseSymbolAssignment,
		token(symbols.ParseSymbolKeyword, symbols.VariableKeywordToken),
		token(symbols.ParseSymbolIdentifier, symbols.IdentifierKeyToken),
		token(symbols.ParseSymbolOperator, symbols.AssignmentOperatorToken),
		fullValueExpression,
		optionalOverrideBlock,
	)

	chainedPart := seq(symbols.ParseSymbolAssignment,
		token(symbols.ParseSymbolOperator, symbols.ChainOperatorToken),
		token(symbols.ParseSymbolIdentifier, symbols.IdentifierKeyToken),
		token(symbols.ParseSymbolOperator, symbols.AssignmentOperatorToken),
		fullValueExpression,
		optionalOverrideBlock,
	)

	return makeChainedRule(
		symbols.ParseSymbolVariable,
		symbols.ParseSymbolChainedAssignments,
		initialPart,
		chainedPart,
	)
}

func styleOverrideBlockRule() shared.ParsingRuleInterface[symbols.LexingTokenType] {
	stylePathValue := conditional.NewChoiceTokenRule(symbols.ParseSymbolValue.String(),
		[]symbols.LexingTokenType{
			symbols.VariableReferenceToken,
			symbols.IdentifierValueToken,
		},
	)

	overrideTarget := seq(symbols.ParseSymbolOverrideTarget,
		stylePathValue,
		token(symbols.ParseSymbolOperator, symbols.AssignmentOperatorToken),
		token(symbols.ParseSymbolValue, symbols.IdentifierValueToken),
	)

	chainedOverrideTarget := seq(symbols.ParseSymbolOverrideTarget,
		token(symbols.ParseSymbolOperator, symbols.ChainOperatorToken),
		overrideTarget,
	)

	overrideTargetList := makeChainedRule(
		symbols.ParseSymbolOverrideTargetList,
		symbols.ParseSymbolChainedOverrideTargets,
		overrideTarget,
		chainedOverrideTarget,
	)

	return seq(symbols.ParseSymbolStyleOverride,
		token(symbols.ParseSymbolKeyword, symbols.StyleOverrideToken),
		token(symbols.ParseSymbolBlockOperator, symbols.OpenSquareBracketToken),
		overrideTargetList,
		token(symbols.ParseSymbolBlockOperator, symbols.CloseSquareBracketToken),
	)
}

func nameAssignment() shared.ParsingRuleInterface[symbols.LexingTokenType] {
	return makeAssignmentRule(symbols.ParseSymbolAssignment, symbols.NameKeywordToken,
		token(symbols.ParseSymbolValue, symbols.IdentifierValueToken))
}

func versionAssignment() shared.ParsingRuleInterface[symbols.LexingTokenType] {
	return makeAssignmentRule(symbols.ParseSymbolAssignment, symbols.VersionKeywordToken,
		token(symbols.ParseSymbolValue, symbols.IdentifierValueToken))
}

func strictnessAssignment() shared.ParsingRuleInterface[symbols.LexingTokenType] {
	return makeAssignmentRule(symbols.ParseSymbolAssignment, symbols.StrictnessKeywordToken, strictnessAssignmentValues)
}

func descriptionAssignment() shared.ParsingRuleInterface[symbols.LexingTokenType] {
	return makeAssignmentRule(symbols.ParseSymbolAssignment, symbols.DescriptionAssignmentKeywordToken,
		token(symbols.ParseSymbolValue, symbols.IdentifierValueToken))
}

func buildAssignment() shared.ParsingRuleInterface[symbols.LexingTokenType] {
	return makeAssignmentRule(symbols.ParseSymbolAssignment, symbols.BuildKeywordToken, buildAssignmentValues)
}

// --- Rule Definition Helpers ---

func makeChainedRule(
	rootSymbol symbols.ParseSymbol,
	chainSymbol symbols.ParseSymbol,
	initialRule shared.ParsingRuleInterface[symbols.LexingTokenType],
	chainedRule shared.ParsingRuleInterface[symbols.LexingTokenType],
) shared.ParsingRuleInterface[symbols.LexingTokenType] {

	// The chained part is a repetition of the chainedRule logic.
	// FIX: The repetition must also consume the optional whitespace that
	// separates the chained expressions.
	chainedPart := composite.NewRepetitionRule[symbols.LexingTokenType](
		chainSymbol.String(),
		whitespaceOptional, // <-- THIS IS THE FIX
		chainedRule,
	)

	// The final rule is the initial part followed by the optional chained parts.
	return composite.NewNestedRule[symbols.LexingTokenType](
		rootSymbol.String(),
		initialRule,
		chainedPart,
	)
}

func makeAssignmentRule(sym symbols.ParseSymbol, keyToken symbols.LexingTokenType, valueRule shared.ParsingRuleInterface[symbols.LexingTokenType]) shared.ParsingRuleInterface[symbols.LexingTokenType] {
	return seq(sym,
		token(symbols.ParseSymbolKey, keyToken),
		token(symbols.ParseSymbolOperator, symbols.AssignmentOperatorToken),
		valueRule,
	)
}

func token(sym symbols.ParseSymbol, tokenType symbols.LexingTokenType) shared.ParsingRuleInterface[symbols.LexingTokenType] {
	return atomic.NewSingleTokenRule(sym.String(), tokenType)
}

func seqWithSeparator(separator shared.ParsingRuleInterface[symbols.LexingTokenType], sym symbols.ParseSymbol, children ...shared.ParsingRuleInterface[symbols.LexingTokenType]) shared.ParsingRuleInterface[symbols.LexingTokenType] {
	if len(children) <= 1 {
		return composite.NewNestedRule[symbols.LexingTokenType](sym.String(), children...)
	}
	rulesWithSeparators := make([]shared.ParsingRuleInterface[symbols.LexingTokenType], 0, len(children)*2-1)
	for i, child := range children {
		rulesWithSeparators = append(rulesWithSeparators, child)
		if i < len(children)-1 {
			rulesWithSeparators = append(rulesWithSeparators, separator)
		}
	}
	return composite.NewNestedRule[symbols.LexingTokenType](sym.String(), rulesWithSeparators...)
}

func seq(sym symbols.ParseSymbol, children ...shared.ParsingRuleInterface[symbols.LexingTokenType]) shared.ParsingRuleInterface[symbols.LexingTokenType] {
	return seqWithSeparator(whitespaceOptional, sym, children...)
}
