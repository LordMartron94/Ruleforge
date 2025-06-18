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
	// Define optional whitespace once to reuse it everywhere.
	// This creates a rule that matches zero or more whitespace or newline tokens.
	whitespaceOptional = composite.NewRepetitionRule[symbols.LexingTokenType](
		symbols.ParseSymbolWhitespace.String(),
		atomic.NewSingleTokenRule(symbols.ParseSymbolWhitespaceToken.String(), symbols.WhitespaceToken),
		atomic.NewSingleTokenRule(symbols.ParseSymbolNewLineToken.String(), symbols.NewLineToken),
	)

	// Define required whitespace for clarity.
	requiredWhitespace = atomic.NewSingleTokenRule(symbols.ParseSymbolWhitespace.String(), symbols.WhitespaceToken)
)

// GetParsingRules returns the list of all top-level parsing rules.
// The order defines parsing priority.
func GetParsingRules() []shared.ParsingRuleInterface[symbols.LexingTokenType] {
	return []shared.ParsingRuleInterface[symbols.LexingTokenType]{
		// These are the top-level constructs the parser will try to match first.
		sectionSectionRule(),
		metadataSectionRule(),
		generalVariableAssignmentRule(),
		implicitVariableAssignmentRule(),
		// Fallbacks for standalone tokens that might not be part of a larger structure.
		atomic.NewSingleTokenRule(symbols.ParseSymbolNewLineToken.String(), symbols.NewLineToken),
		atomic.NewSingleTokenRule(symbols.ParseSymbolWhitespace.String(), symbols.WhitespaceToken),
		conditional.NewAnyTokenRule[symbols.LexingTokenType](symbols.ParseSymbolAny.String()),
	}
}

// --- High-Level Section Rules ---

// sectionSectionRule matches an entire SECTION { ... } block.
func sectionSectionRule() shared.ParsingRuleInterface[symbols.LexingTokenType] {
	return seq(symbols.ParseSymbolRuleSectionSection,
		token(symbols.ParseSymbolGenericKeyWord, symbols.SectionKeywordToken),
		token(symbols.ParseSymbolOpenBrace, symbols.OpenCurlyBracketToken),
		metadataSectionRule(),
		sectionConditionsRule(),
		token(symbols.ParseSymbolCloseBrace, symbols.CloseCurlyBracketToken),
	)
}

// metadataSectionRule matches a METADATA { ... } block.
func metadataSectionRule() shared.ParsingRuleInterface[symbols.LexingTokenType] {
	// An optional, repeating sequence of assignments within the metadata block.
	assignments := composite.NewRepetitionRule[symbols.LexingTokenType](symbols.ParseSymbolAssignments.String(),
		nameAssignmentRule(),
		versionAssignmentRule(),
		strictnessAssignmentRule(),
		whitespaceOptional,
	)

	return seq(symbols.ParseSymbolMetadataSection,
		token(symbols.ParseSymbolMetadataKeyword, symbols.MetadataKeywordToken),
		token(symbols.ParseSymbolOpenBrace, symbols.OpenCurlyBracketToken),
		assignments,
		token(symbols.ParseSymbolCloseBrace, symbols.CloseCurlyBracketToken),
	)
}

// sectionConditionsRule matches a SECTION_CONDITIONS { ... } block.
func sectionConditionsRule() shared.ParsingRuleInterface[symbols.LexingTokenType] {
	return seq(symbols.ParseSymbolSectionConditionsSection,
		token(symbols.ParseSymbolSectionConditionsSection, symbols.SectionConditionsKeywordToken),
		token(symbols.ParseSymbolOpenBrace, symbols.OpenCurlyBracketToken),
		conditionDeclarationRule(),
		token(symbols.ParseSymbolCloseBrace, symbols.CloseCurlyBracketToken),
	)
}

// --- Assignment and Declaration Rules ---

// conditionDeclarationRule matches 'WHERE @area_level <= $campaign_end'.
func conditionDeclarationRule() shared.ParsingRuleInterface[symbols.LexingTokenType] {
	comparisonOperators := conditional.NewTokenSetRepetitionRule(
		symbols.ParseSymbolComparisonOperator.String(),
		[]symbols.LexingTokenType{
			symbols.GreaterThanOrEqualOperatorToken, symbols.LessThanOrEqualOperatorToken,
			symbols.GreaterThanOperatorToken, symbols.LessThanOperatorToken, symbols.ExactMatchOperatorToken,
		},
		[]string{"ComparisonOp", "ComparisonOp", "ComparisonOp", "ComparisonOp", "ComparisonOp"},
	)

	return seq(symbols.ParseSymbolCondition,
		token(symbols.ParseSymbolConditionAssignment, symbols.ConditionAssignmentKeywordToken),
		token(symbols.ParseSymbolConditionKeywordToken, symbols.ConditionKeywordToken),
		comparisonOperators,
		token(symbols.ParseSymbolVariableReference, symbols.VariableReferenceToken),
	)
}

// generalVariableAssignmentRule matches 'var key => value'.
func generalVariableAssignmentRule() shared.ParsingRuleInterface[symbols.LexingTokenType] {
	value := conditional.NewTokenSetRepetitionRule(
		symbols.ParseSymbolValue.String(),
		[]symbols.LexingTokenType{symbols.IdentifierValueToken, symbols.NumberToken},
		[]string{"Identifier", "Number"},
	)

	return composite.NewNestedRule[symbols.LexingTokenType](symbols.ParseSymbolGeneralVariable.String(),
		token(symbols.ParseVariableAssignmentKey, symbols.VariableKeywordToken),
		requiredWhitespace,
		token(symbols.ParseSymbolIdentifier, symbols.IdentifierKeyToken),
		requiredWhitespace,
		token(symbols.ParseSymbolAssignmentOp, symbols.AssignmentOperatorToken),
		requiredWhitespace,
		value,
	)
}

// implicitVariableAssignmentRule matches '-> key => value'.
func implicitVariableAssignmentRule() shared.ParsingRuleInterface[symbols.LexingTokenType] {
	return seq(symbols.ParseSymbolGeneralVariable,
		token(symbols.ParseSymbolChainOperator, symbols.ChainOperatorToken),
		token(symbols.ParseSymbolIdentifier, symbols.IdentifierKeyToken),
		token(symbols.ParseSymbolAssignmentOp, symbols.AssignmentOperatorToken),
		token(symbols.ParseSymbolValue, symbols.IdentifierValueToken),
	)
}

func nameAssignmentRule() shared.ParsingRuleInterface[symbols.LexingTokenType] {
	return makeAssignmentRule(symbols.ParseSymbolNameAssignment, symbols.NameKeywordToken,
		token(symbols.ParseSymbolIdentifier, symbols.IdentifierValueToken))
}

func versionAssignmentRule() shared.ParsingRuleInterface[symbols.LexingTokenType] {
	return makeAssignmentRule(symbols.ParseSymbolVersionAssignment, symbols.VersionKeywordToken,
		token(symbols.ParseSymbolIdentifier, symbols.IdentifierValueToken))
}

func strictnessAssignmentRule() shared.ParsingRuleInterface[symbols.LexingTokenType] {
	allowedValues := conditional.NewTokenSetRepetitionRule(symbols.ParseSymbolValue.String(),
		[]symbols.LexingTokenType{
			symbols.AllKeywordToken, symbols.SoftKeywordToken, symbols.SemiStrictKeywordToken,
			symbols.StrictKeywordToken, symbols.SuperStrictKeywordToken,
		},
		[]string{"All", "Soft", "SemiStrict", "Strict", "SuperStrict"},
	)
	return makeAssignmentRule(symbols.ParseSymbolStrictnessAssignment, symbols.StrictnessKeywordToken, allowedValues)
}

// --- Rule Definition Helpers ---

// makeAssignmentRule builds a rule for 'Key => Value'.
func makeAssignmentRule(
	assignmentSymbol symbols.ParseSymbol,
	keywordToken symbols.LexingTokenType,
	valueRule shared.ParsingRuleInterface[symbols.LexingTokenType],
) shared.ParsingRuleInterface[symbols.LexingTokenType] {
	return seq(assignmentSymbol,
		token(symbols.ParseSymbolKey, keywordToken),
		token(symbols.ParseSymbolAssignmentOp, symbols.AssignmentOperatorToken),
		valueRule,
	)
}

// token is a shorthand for creating a simple, single-token parsing rule.
func token(sym symbols.ParseSymbol, tokenType symbols.LexingTokenType) shared.ParsingRuleInterface[symbols.LexingTokenType] {
	return atomic.NewSingleTokenRule(sym.String(), tokenType)
}

// seq is a powerful helper that creates a nested rule with optional whitespace between each element.
// This dramatically reduces boilerplate in the rule definitions.
func seq(sym symbols.ParseSymbol, children ...shared.ParsingRuleInterface[symbols.LexingTokenType]) shared.ParsingRuleInterface[symbols.LexingTokenType] {
	rulesWithWhitespace := make([]shared.ParsingRuleInterface[symbols.LexingTokenType], 0, len(children)*2-1)
	for i, child := range children {
		rulesWithWhitespace = append(rulesWithWhitespace, child)
		// Add optional whitespace between elements, but not after the last one.
		if i < len(children)-1 {
			rulesWithWhitespace = append(rulesWithWhitespace, whitespaceOptional)
		}
	}
	return composite.NewNestedRule[symbols.LexingTokenType](sym.String(), rulesWithWhitespace...)
}
