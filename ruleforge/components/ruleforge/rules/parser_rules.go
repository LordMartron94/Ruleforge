package rules

import (
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
)

// GetParsingRules returns the list of all top-level parsing rules.
func GetParsingRules() []shared.ParsingRuleInterface[symbols.LexingTokenType] {
	return []shared.ParsingRuleInterface[symbols.LexingTokenType]{
		// The order of these top-level rules is the priority for parsing.
		metadataRule(),
		sectionRule(),
		variableRule(),
		// Fallbacks for any remaining standalone tokens.
		atomic.NewSingleTokenRule(symbols.ParseSymbolWhitespace.String(), symbols.NewLineToken),
		atomic.NewSingleTokenRule(symbols.ParseSymbolWhitespace.String(), symbols.WhitespaceToken),
		conditional.NewAnyTokenRule[symbols.LexingTokenType](symbols.ParseSymbolAny.String()),
	}
}

// --- High-Level Section Rules ---

func metadataRule() shared.ParsingRuleInterface[symbols.LexingTokenType] {
	// Since RepetitionRule now requires forward progress (consumed > 0), this is safe.
	assignments := composite.NewRepetitionRule[symbols.LexingTokenType](
		symbols.ParseSymbolAssignments.String(),
		nameAssignment(),
		versionAssignment(),
		strictnessAssignment(),
		descriptionAssignment(),
		whitespaceOptional, // This allows whitespace/newlines between assignments.
	)

	// We define the sequence manually here because `seq` is too simple for this case.
	return composite.NewNestedRule[symbols.LexingTokenType](symbols.ParseSymbolMetadata.String(),
		token(symbols.ParseSymbolKeyword, symbols.MetadataKeywordToken),
		whitespaceOptional,
		token(symbols.ParseSymbolBlockOperator, symbols.OpenCurlyBracketToken),
		whitespaceOptional,
		assignments,
		token(symbols.ParseSymbolBlockOperator, symbols.CloseCurlyBracketToken),
	)
}

func sectionRule() shared.ParsingRuleInterface[symbols.LexingTokenType] {
	// A section can contain other sections. We explicitly handle the whitespace.
	sectionContent := composite.NewRepetitionRule[symbols.LexingTokenType](
		symbols.ParseSymbolSectionContent.String(),
		metadataRule(),
		conditionListRule(),
		ruleSectionRule(),
		whitespaceOptional, // Allow whitespace between inner sections
	)
	return composite.NewNestedRule[symbols.LexingTokenType](symbols.ParseSymbolSection.String(),
		token(symbols.ParseSymbolKeyword, symbols.SectionKeywordToken),
		whitespaceOptional,
		token(symbols.ParseSymbolBlockOperator, symbols.OpenCurlyBracketToken),
		whitespaceOptional,
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

	return seq(symbols.ParseSymbolRuleExpression,
		conditionRule(),
		token(symbols.ParseSymbolOperator, symbols.AssignmentOperatorToken),
		valueOpts,
	)
}

// --- Assignment and Declaration Rules ---

func conditionRule() shared.ParsingRuleInterface[symbols.LexingTokenType] {
	comparisonOps := conditional.NewChoiceTokenRule(symbols.ParseSymbolOperator.String(),
		[]symbols.LexingTokenType{
			symbols.GreaterThanOrEqualOperatorToken, symbols.LessThanOrEqualOperatorToken,
			symbols.GreaterThanOperatorToken, symbols.LessThanOperatorToken, symbols.ExactMatchOperatorToken,
		},
	)

	valueOpts := conditional.NewChoiceTokenRule(symbols.ParseSymbolValue.String(),
		[]symbols.LexingTokenType{
			symbols.VariableReferenceToken, symbols.NumberToken, symbols.IdentifierValueToken,
		},
	)

	initialAssignment := composite.NewNestedRule[symbols.LexingTokenType](symbols.ParseSymbolConditionExpression.String(),
		token(symbols.ParseSymbolKeyword, symbols.ConditionAssignmentKeywordToken),
		whitespaceOptional,
		token(symbols.ParseSymbolIdentifier, symbols.ConditionKeywordToken),
		whitespaceOptional,
		comparisonOps,
		whitespaceOptional,
		valueOpts,
	)

	chainedPart := composite.NewRepetitionRule[symbols.LexingTokenType](symbols.ParseSymbolChainedConditions.String(),
		composite.NewNestedRule[symbols.LexingTokenType](symbols.ParseSymbolConditionExpression.String(),
			whitespaceOptional,
			token(symbols.ParseSymbolOperator, symbols.ChainOperatorToken),
			whitespaceOptional,
			token(symbols.ParseSymbolIdentifier, symbols.ConditionKeywordToken),
			whitespaceOptional,
			comparisonOps,
			whitespaceOptional,
			valueOpts,
		),
	)

	return composite.NewNestedRule[symbols.LexingTokenType](symbols.ParseSymbolCondition.String(),
		initialAssignment,
		chainedPart,
	)
}

func variableRule() shared.ParsingRuleInterface[symbols.LexingTokenType] {
	valueOpts := conditional.NewChoiceTokenRule(symbols.ParseSymbolValue.String(),
		[]symbols.LexingTokenType{
			symbols.NumberToken, symbols.IdentifierValueToken,
		},
	)

	chainedPart := composite.NewRepetitionRule[symbols.LexingTokenType](symbols.ParseSymbolChainedAssignments.String(),
		composite.NewNestedRule[symbols.LexingTokenType](symbols.ParseSymbolAssignment.String(),
			whitespaceOptional,
			token(symbols.ParseSymbolOperator, symbols.ChainOperatorToken),
			requiredWhitespace,
			token(symbols.ParseSymbolIdentifier, symbols.IdentifierKeyToken),
			requiredWhitespace,
			token(symbols.ParseSymbolOperator, symbols.AssignmentOperatorToken),
			requiredWhitespace,
			valueOpts,
		),
	)

	initialAssignment := composite.NewNestedRule[symbols.LexingTokenType](symbols.ParseSymbolAssignment.String(),
		token(symbols.ParseSymbolKeyword, symbols.VariableKeywordToken),
		requiredWhitespace,
		token(symbols.ParseSymbolIdentifier, symbols.IdentifierKeyToken),
		requiredWhitespace,
		token(symbols.ParseSymbolOperator, symbols.AssignmentOperatorToken),
		requiredWhitespace,
		valueOpts,
	)

	return composite.NewNestedRule[symbols.LexingTokenType](symbols.ParseSymbolVariable.String(),
		initialAssignment,
		chainedPart,
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
	allowedValues := conditional.NewTokenSetRepetitionRule(symbols.ParseSymbolValue.String(),
		[]symbols.LexingTokenType{
			symbols.AllKeywordToken, symbols.SoftKeywordToken, symbols.SemiStrictKeywordToken,
			symbols.StrictKeywordToken, symbols.SuperStrictKeywordToken,
		},
		[]string{symbols.ParseSymbolKeyword.String(), symbols.ParseSymbolKeyword.String(), symbols.ParseSymbolKeyword.String(), symbols.ParseSymbolKeyword.String(), symbols.ParseSymbolKeyword.String()},
	)
	return makeAssignmentRule(symbols.ParseSymbolAssignment, symbols.StrictnessKeywordToken, allowedValues)
}

func descriptionAssignment() shared.ParsingRuleInterface[symbols.LexingTokenType] {
	return makeAssignmentRule(symbols.ParseSymbolAssignment, symbols.DescriptionAssignmentKeywordToken,
		token(symbols.ParseSymbolValue, symbols.IdentifierValueToken))
}

// --- Rule Definition Helpers ---

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

func seq(sym symbols.ParseSymbol, children ...shared.ParsingRuleInterface[symbols.LexingTokenType]) shared.ParsingRuleInterface[symbols.LexingTokenType] {
	rulesWithWhitespace := make([]shared.ParsingRuleInterface[symbols.LexingTokenType], 0, len(children)*2-1)
	for i, child := range children {
		rulesWithWhitespace = append(rulesWithWhitespace, child)
		if i < len(children)-1 {
			rulesWithWhitespace = append(rulesWithWhitespace, whitespaceOptional)
		}
	}
	return composite.NewNestedRule[symbols.LexingTokenType](sym.String(), rulesWithWhitespace...)
}
