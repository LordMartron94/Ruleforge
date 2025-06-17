package rules

import (
	lexingshared "github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/lexing/shared"
	parsingrules "github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/parsing/rules"
	rulefactory "github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/parsing/rules/factory"
	parsingshared "github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/parsing/shared"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/extensions"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/rules/symbols"
)

// ruleFactory wraps a ParsingRuleFactory and accepts ParseSymbol directly.
type ruleFactory struct {
	inner *rulefactory.ParsingRuleFactory[symbols.LexingTokenType]
}

// newRuleFactory constructs a ruleFactory from a ParsingRuleFactory.
func newRuleFactory(inner *rulefactory.ParsingRuleFactory[symbols.LexingTokenType]) *ruleFactory {
	return &ruleFactory{inner: inner}
}

// --- ruleFactory Methods (Unchanged) ---

func (r *ruleFactory) NewSingle(sym symbols.ParseSymbol, token symbols.LexingTokenType) parsingrules.ParsingRuleInterface[symbols.LexingTokenType] {
	return r.inner.NewSingleTokenParsingRule(sym.String(), token)
}

func (r *ruleFactory) NewEither(symbol symbols.ParseSymbol, tokenOptions []symbols.LexingTokenType) parsingrules.ParsingRuleInterface[symbols.LexingTokenType] {
	return r.inner.NewParsingRule(symbol.String(), func(tokens []*lexingshared.Token[symbols.LexingTokenType], index int) (bool, string) {
		if extensions.Contains(tokenOptions, tokens[index].Type) {
			return true, ""
		}
		return false, "No Match"
	}, func(tokens []*lexingshared.Token[symbols.LexingTokenType], index int) *parsingshared.ParseTree[symbols.LexingTokenType] {
		return &parsingshared.ParseTree[symbols.LexingTokenType]{
			Symbol:   symbol.String(),
			Token:    tokens[index],
			Children: nil,
		}
	}, 0)
}

func (r *ruleFactory) NewNested(sym symbols.ParseSymbol, children []parsingrules.ParsingRuleInterface[symbols.LexingTokenType]) parsingrules.ParsingRuleInterface[symbols.LexingTokenType] {
	return r.inner.NewNestedParsingRule(sym.String(), children)
}

func (r *ruleFactory) NewOptional(sym symbols.ParseSymbol, children []parsingrules.ParsingRuleInterface[symbols.LexingTokenType]) parsingrules.ParsingRuleInterface[symbols.LexingTokenType] {
	return r.inner.NewOptionalNestedParsingRule(sym.String(), children)
}

func (r *ruleFactory) NewMatchUntil(sym symbols.ParseSymbol, childTypes []symbols.LexingTokenType, childSymbols []string) parsingrules.ParsingRuleInterface[symbols.LexingTokenType] {
	return r.inner.NewMatchUntilTokenWithFilterParsingRule(sym.String(), childTypes, childSymbols)
}

func (r *ruleFactory) NewMatchAny(sym symbols.ParseSymbol) parsingrules.ParsingRuleInterface[symbols.LexingTokenType] {
	return r.inner.NewMatchAnyTokenParsingRule(sym.String())
}

func (r *ruleFactory) NewSequential(sym symbols.ParseSymbol, childTypes []symbols.LexingTokenType, childSymbols []string) parsingrules.ParsingRuleInterface[symbols.LexingTokenType] {
	return r.inner.NewSequentialTokenParsingRule(sym.String(), childTypes, childSymbols)
}

// DSLParsingRules encapsulates parsing rules for the metadata DSL section.
type DSLParsingRules struct {
	factory *ruleFactory
}

// NewDSLParsingRules constructs a new DSLParsingRules instance.
func NewDSLParsingRules() *DSLParsingRules {
	innerFactory := rulefactory.NewParsingRuleFactory[symbols.LexingTokenType]()
	return &DSLParsingRules{
		factory: newRuleFactory(innerFactory),
	}
}

// GetParsingRules returns the list of all top-level parsing rules.
// The order is important as it defines parsing priority.
func (d *DSLParsingRules) GetParsingRules() []parsingrules.ParsingRuleInterface[symbols.LexingTokenType] {
	return []parsingrules.ParsingRuleInterface[symbols.LexingTokenType]{
		// These are the top-level constructs the parser will try to match.
		d.sectionSectionRule(),
		d.metadataSectionRule(),
		d.sectionConditionsRule(),
		d.conditionDeclarationRule(),
		d.generalVariableAssignmentRule(),
		d.implicitVariableAssignmentRule(),
		d.matchNewLine(),
		d.matchWhiteSpace(),
		d.matchAnyFallbackRule(),
	}
}

// metadataSectionRule parses: Metadata keyword, optional whitespace/newlines,
// '{', zero or more assignments..., then '}'.
func (d *DSLParsingRules) metadataSectionRule() parsingrules.ParsingRuleInterface[symbols.LexingTokenType] {
	// This structure is intentionally kept from the original to preserve the exact parse symbols.
	return d.factory.NewNested(symbols.ParseSymbolMetadataSection, []parsingrules.ParsingRuleInterface[symbols.LexingTokenType]{
		d.factory.NewSingle(symbols.ParseSymbolMetadataKeyword, symbols.MetadataKeywordToken),
		d.whitespaceOptional(),
		d.factory.NewSingle(symbols.ParseSymbolOpenBrace, symbols.OpenCurlyBracketToken),
		d.factory.NewOptional(symbols.ParseSymbolAssignments, []parsingrules.ParsingRuleInterface[symbols.LexingTokenType]{
			d.nameAssignmentRule(),
			d.whitespaceOptional(),
			d.versionAssignmentRule(),
			d.whitespaceOptional(),
			d.strictnessAssignmentRule(),
			d.whitespaceOptional(),
		}),
		d.factory.NewSingle(symbols.ParseSymbolCloseBrace, symbols.CloseCurlyBracketToken),
	})
}

func (d *DSLParsingRules) sectionSectionRule() parsingrules.ParsingRuleInterface[symbols.LexingTokenType] {
	return d.factory.NewNested(symbols.ParseSymbolRuleSectionSection, []parsingrules.ParsingRuleInterface[symbols.LexingTokenType]{
		d.factory.NewSingle(symbols.ParseSymbolGenericKeyWord, symbols.SectionKeywordToken),
		d.whitespaceOptional(),
		d.factory.NewSingle(symbols.ParseSymbolOpenBrace, symbols.OpenCurlyBracketToken),
		d.whitespaceOptional(), // allow whitespace/newlines before any inner sections
		d.metadataSectionRule(),
		d.whitespaceOptional(),
		d.sectionConditionsRule(),
		d.whitespaceOptional(), // allow trailing whitespace/newlines
		d.factory.NewSingle(symbols.ParseSymbolCloseBrace, symbols.CloseCurlyBracketToken),
	})
}

func (d *DSLParsingRules) sectionConditionsRule() parsingrules.ParsingRuleInterface[symbols.LexingTokenType] {
	return d.factory.NewNested(symbols.ParseSymbolSectionConditionsSection, []parsingrules.ParsingRuleInterface[symbols.LexingTokenType]{
		d.factory.NewSingle(symbols.ParseSymbolSectionConditionsSection, symbols.SectionConditionsKeywordToken),
		d.whitespaceOptional(),
		d.factory.NewSingle(symbols.ParseSymbolOpenBrace, symbols.OpenCurlyBracketToken),
		d.whitespaceOptional(),
		d.conditionDeclarationRule(),
		d.whitespaceOptional(),
		d.factory.NewSingle(symbols.ParseSymbolCloseBrace, symbols.CloseCurlyBracketToken),
	})
}

func (d *DSLParsingRules) conditionDeclarationRule() parsingrules.ParsingRuleInterface[symbols.LexingTokenType] {
	return d.factory.NewNested(symbols.ParseSymbolCondition, []parsingrules.ParsingRuleInterface[symbols.LexingTokenType]{
		// the 'WHERE' keyword
		d.factory.NewSingle(symbols.ParseSymbolConditionAssignment, symbols.ConditionAssignmentKeywordToken),
		d.whitespaceOptional(),
		// e.g. @area_level
		d.factory.NewSingle(symbols.ParseSymbolConditionKeywordToken, symbols.ConditionKeywordToken),
		d.whitespaceOptional(),
		// one of <=, >=, <, > or ==
		d.factory.NewEither(symbols.ParseSymbolComparisonOperator, []symbols.LexingTokenType{
			symbols.GreaterThanOrEqualOperatorToken,
			symbols.LessThanOrEqualOperatorToken,
			symbols.GreaterThanOperatorToken,
			symbols.LessThanOperatorToken,
			symbols.ExactMatchOperatorToken,
		}),
		d.whitespaceOptional(),
		// the variable reference, e.g. $campaign_end
		d.factory.NewSingle(symbols.ParseSymbolVariableReference, symbols.VariableReferenceToken),
	})
}

// whitespaceOptional matches zero or more Whitespace or NewLine tokens.
func (d *DSLParsingRules) whitespaceOptional() parsingrules.ParsingRuleInterface[symbols.LexingTokenType] {
	return d.factory.NewOptional(symbols.ParseSymbolWhitespace, []parsingrules.ParsingRuleInterface[symbols.LexingTokenType]{
		d.factory.NewSingle(symbols.ParseSymbolWhitespaceToken, symbols.WhitespaceToken),
		d.factory.NewSingle(symbols.ParseSymbolNewLineToken, symbols.NewLineToken),
	})
}

func (d *DSLParsingRules) nameAssignmentRule() parsingrules.ParsingRuleInterface[symbols.LexingTokenType] {
	return d.makeAssignmentRule(
		symbols.ParseSymbolNameAssignment, symbols.NameKeywordToken,
		[]symbols.LexingTokenType{symbols.IdentifierValueToken},
		[]symbols.ParseSymbol{symbols.ParseSymbolIdentifier},
	)
}

func (d *DSLParsingRules) versionAssignmentRule() parsingrules.ParsingRuleInterface[symbols.LexingTokenType] {
	return d.makeAssignmentRule(
		symbols.ParseSymbolVersionAssignment, symbols.VersionKeywordToken,
		[]symbols.LexingTokenType{symbols.IdentifierValueToken},
		[]symbols.ParseSymbol{symbols.ParseSymbolIdentifier},
	)
}

func (d *DSLParsingRules) strictnessAssignmentRule() parsingrules.ParsingRuleInterface[symbols.LexingTokenType] {
	return d.makeAssignmentRule(
		symbols.ParseSymbolStrictnessAssignment, symbols.StrictnessKeywordToken,
		[]symbols.LexingTokenType{symbols.AllKeywordToken, symbols.SoftKeywordToken, symbols.SemiStrictKeywordToken, symbols.StrictKeywordToken, symbols.SuperStrictKeywordToken, symbols.LetterToken},
		[]symbols.ParseSymbol{symbols.ParseSymbolAll, symbols.ParseSymbolSoft, symbols.ParseSymbolSemiStrict, symbols.ParseSymbolStrict, symbols.ParseSymbolSuperStrict, symbols.ParseSymbolAny},
	)
}

func (d *DSLParsingRules) generalVariableAssignmentRule() parsingrules.ParsingRuleInterface[symbols.LexingTokenType] {
	return d.factory.NewNested(symbols.ParseSymbolGeneralVariable, []parsingrules.ParsingRuleInterface[symbols.LexingTokenType]{
		d.factory.NewSingle(symbols.ParseVariableAssignmentKey, symbols.VariableKeywordToken),
		d.factory.NewSingle(symbols.ParseSymbolWhitespace, symbols.WhitespaceToken),
		d.factory.NewSingle(symbols.ParseSymbolIdentifier, symbols.IdentifierKeyToken),
		d.factory.NewSingle(symbols.ParseSymbolWhitespace, symbols.WhitespaceToken),
		d.factory.NewSingle(symbols.ParseSymbolAssignmentOp, symbols.AssignmentOperatorToken),
		d.factory.NewSingle(symbols.ParseSymbolWhitespace, symbols.WhitespaceToken),
		d.factory.NewEither(symbols.ParseSymbolValue, []symbols.LexingTokenType{
			symbols.IdentifierValueToken,
			symbols.DigitToken,
		}),
	})
}

func (d *DSLParsingRules) implicitVariableAssignmentRule() parsingrules.ParsingRuleInterface[symbols.LexingTokenType] {
	return d.factory.NewNested(symbols.ParseSymbolGeneralVariable, []parsingrules.ParsingRuleInterface[symbols.LexingTokenType]{
		d.factory.NewSingle(symbols.ParseSymbolChainOperator, symbols.ChainOperatorToken),
		d.whitespaceOptional(),
		d.factory.NewSingle(symbols.ParseSymbolIdentifier, symbols.IdentifierKeyToken),
		d.whitespaceOptional(),
		d.factory.NewSingle(symbols.ParseSymbolAssignmentOp, symbols.AssignmentOperatorToken),
		d.whitespaceOptional(),
		d.factory.NewSingle(symbols.ParseSymbolValue, symbols.IdentifierValueToken),
	})
}

// makeAssignmentRule builds a rule for 'Key Whitespace AssignmentOp Whitespace Value...'.
// Parameter names are improved for clarity, but logic is identical to original.
func (d *DSLParsingRules) makeAssignmentRule(
	assignmentSymbol symbols.ParseSymbol,
	keywordToken symbols.LexingTokenType,
	valueTokenTypes []symbols.LexingTokenType,
	valueParseSymbols []symbols.ParseSymbol,
) parsingrules.ParsingRuleInterface[symbols.LexingTokenType] {

	symbolStrings := make([]string, len(valueParseSymbols))
	for i, cs := range valueParseSymbols {
		symbolStrings[i] = cs.String()
	}

	// This structure preserves the original, specific parse symbols for whitespace.
	return d.factory.NewNested(assignmentSymbol, []parsingrules.ParsingRuleInterface[symbols.LexingTokenType]{
		d.factory.NewSingle(symbols.ParseSymbolKey, keywordToken),
		d.factory.NewSingle(symbols.ParseSymbolWhitespaceBeforeOp, symbols.WhitespaceToken),
		d.factory.NewSingle(symbols.ParseSymbolAssignmentOp, symbols.AssignmentOperatorToken),
		d.factory.NewSingle(symbols.ParseSymbolWhitespaceAfterOp, symbols.WhitespaceToken),
		d.factory.NewMatchUntil(symbols.ParseSymbolValue, valueTokenTypes, symbolStrings),
	})
}

// --- Standalone Fallback Matchers ---

func (d *DSLParsingRules) matchNewLine() parsingrules.ParsingRuleInterface[symbols.LexingTokenType] {
	return d.factory.NewSingle(symbols.ParseSymbolNewLineToken, symbols.NewLineToken)
}

func (d *DSLParsingRules) matchWhiteSpace() parsingrules.ParsingRuleInterface[symbols.LexingTokenType] {
	return d.factory.NewSingle(symbols.ParseSymbolWhitespace, symbols.WhitespaceToken)
}

func (d *DSLParsingRules) matchAnyFallbackRule() parsingrules.ParsingRuleInterface[symbols.LexingTokenType] {
	return d.factory.NewMatchAny(symbols.ParseSymbolAny)
}
