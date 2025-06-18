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
	// THE FIX: The repetition of assignments must also be allowed to consume whitespace.
	// Since RepetitionRule now requires forward progress (consumed > 0), this is safe.
	assignments := composite.NewRepetitionRule[symbols.LexingTokenType](
		symbols.ParseSymbolAssignments.String(),
		nameAssignment(),
		versionAssignment(),
		strictnessAssignment(),
		whitespaceOptional, // This allows whitespace/newlines between assignments.
	)

	// We define the sequence manually here because `seq` is too simple for this case.
	return composite.NewNestedRule[symbols.LexingTokenType](symbols.ParseSymbolMetadata.String(),
		token(symbols.ParseSymbolKeyword, symbols.MetadataKeywordToken),
		whitespaceOptional,
		token(symbols.ParseSymbolOperator, symbols.OpenCurlyBracketToken),
		whitespaceOptional,
		assignments,
		// No need for whitespace here, the last assignment's trailing space is handled by the loop.
		token(symbols.ParseSymbolOperator, symbols.CloseCurlyBracketToken),
	)
}

func sectionRule() shared.ParsingRuleInterface[symbols.LexingTokenType] {
	// A section can contain other sections. We explicitly handle the whitespace.
	sectionContent := composite.NewRepetitionRule[symbols.LexingTokenType](
		"SectionContent",
		metadataRule(),
		conditionListRule(),
		whitespaceOptional, // Allow whitespace between inner sections
	)
	return composite.NewNestedRule[symbols.LexingTokenType](symbols.ParseSymbolSection.String(),
		token(symbols.ParseSymbolKeyword, symbols.SectionKeywordToken),
		whitespaceOptional,
		token(symbols.ParseSymbolOperator, symbols.OpenCurlyBracketToken),
		whitespaceOptional,
		sectionContent,
		token(symbols.ParseSymbolOperator, symbols.CloseCurlyBracketToken),
	)
}

func conditionListRule() shared.ParsingRuleInterface[symbols.LexingTokenType] {
	conditions := composite.NewRepetitionRule[symbols.LexingTokenType]("Conditions",
		conditionRule(),
		whitespaceOptional,
	)
	return seq(symbols.ParseSymbolConditionList,
		token(symbols.ParseSymbolKeyword, symbols.SectionConditionsKeywordToken),
		token(symbols.ParseSymbolOperator, symbols.OpenCurlyBracketToken),
		conditions,
		token(symbols.ParseSymbolOperator, symbols.CloseCurlyBracketToken),
	)
}

// --- Assignment and Declaration Rules ---

func conditionRule() shared.ParsingRuleInterface[symbols.LexingTokenType] {
	comparisonOps := conditional.NewTokenSetRepetitionRule(symbols.ParseSymbolOperator.String(),
		[]symbols.LexingTokenType{
			symbols.GreaterThanOrEqualOperatorToken, symbols.LessThanOrEqualOperatorToken,
			symbols.GreaterThanOperatorToken, symbols.LessThanOperatorToken, symbols.ExactMatchOperatorToken,
		},
		[]string{"Operator", "Operator", "Operator", "Operator", "Operator"},
	)
	return seq(symbols.ParseSymbolCondition,
		token(symbols.ParseSymbolKeyword, symbols.ConditionAssignmentKeywordToken),
		token(symbols.ParseSymbolIdentifier, symbols.ConditionKeywordToken),
		comparisonOps,
		token(symbols.ParseSymbolIdentifier, symbols.VariableReferenceToken),
	)
}

func variableRule() shared.ParsingRuleInterface[symbols.LexingTokenType] {
	chainedPart := composite.NewRepetitionRule[symbols.LexingTokenType]("ChainedAssignments",
		// This sequence represents one "-> key => value" chain link.
		composite.NewNestedRule[symbols.LexingTokenType](symbols.ParseSymbolAssignment.String(),
			whitespaceOptional,
			token(symbols.ParseSymbolOperator, symbols.ChainOperatorToken),
			whitespaceOptional,
			token(symbols.ParseSymbolIdentifier, symbols.IdentifierKeyToken),
			whitespaceOptional,
			token(symbols.ParseSymbolOperator, symbols.AssignmentOperatorToken),
			whitespaceOptional,
			token(symbols.ParseSymbolValue, symbols.IdentifierValueToken),
		),
	)

	initialValue := conditional.NewTokenSetRepetitionRule(symbols.ParseSymbolValue.String(),
		[]symbols.LexingTokenType{symbols.IdentifierValueToken, symbols.NumberToken},
		[]string{symbols.ParseSymbolIdentifier.String(), symbols.ParseSymbolNumber.String()},
	)

	initialAssignment := composite.NewNestedRule[symbols.LexingTokenType](symbols.ParseSymbolAssignment.String(),
		token(symbols.ParseSymbolKeyword, symbols.VariableKeywordToken),
		requiredWhitespace,
		token(symbols.ParseSymbolIdentifier, symbols.IdentifierKeyToken),
		requiredWhitespace,
		token(symbols.ParseSymbolOperator, symbols.AssignmentOperatorToken),
		requiredWhitespace,
		initialValue,
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
