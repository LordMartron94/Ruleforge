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

// --- High-Level Section Rules ---

func (d *DSLParsingRules) sectionSectionRule() parsingrules.ParsingRuleInterface[symbols.LexingTokenType] {
	return d.factory.NewNested(symbols.ParseSymbolRuleSectionSection, []parsingrules.ParsingRuleInterface[symbols.LexingTokenType]{
		d.token(symbols.ParseSymbolGenericKeyWord, symbols.SectionKeywordToken),
		d.whitespaceOptional(),
		d.token(symbols.ParseSymbolOpenBrace, symbols.OpenCurlyBracketToken),
		d.whitespaceOptional(),
		d.metadataSectionRule(),
		d.whitespaceOptional(),
		d.sectionConditionsRule(),
		d.whitespaceOptional(),
		d.token(symbols.ParseSymbolCloseBrace, symbols.CloseCurlyBracketToken),
	})
}

func (d *DSLParsingRules) metadataSectionRule() parsingrules.ParsingRuleInterface[symbols.LexingTokenType] {
	return d.factory.NewNested(symbols.ParseSymbolMetadataSection, []parsingrules.ParsingRuleInterface[symbols.LexingTokenType]{
		d.token(symbols.ParseSymbolMetadataKeyword, symbols.MetadataKeywordToken),
		d.whitespaceOptional(),
		d.token(symbols.ParseSymbolOpenBrace, symbols.OpenCurlyBracketToken),
		d.factory.NewOptional(symbols.ParseSymbolAssignments, []parsingrules.ParsingRuleInterface[symbols.LexingTokenType]{
			d.nameAssignmentRule(),
			d.whitespaceOptional(),
			d.versionAssignmentRule(),
			d.whitespaceOptional(),
			d.strictnessAssignmentRule(),
			d.whitespaceOptional(),
		}),
		d.token(symbols.ParseSymbolCloseBrace, symbols.CloseCurlyBracketToken),
	})
}

func (d *DSLParsingRules) sectionConditionsRule() parsingrules.ParsingRuleInterface[symbols.LexingTokenType] {
	return d.factory.NewNested(symbols.ParseSymbolSectionConditionsSection, []parsingrules.ParsingRuleInterface[symbols.LexingTokenType]{
		d.token(symbols.ParseSymbolSectionConditionsSection, symbols.SectionConditionsKeywordToken),
		d.whitespaceOptional(),
		d.token(symbols.ParseSymbolOpenBrace, symbols.OpenCurlyBracketToken),
		d.whitespaceOptional(),
		d.conditionDeclarationRule(),
		d.whitespaceOptional(),
		d.token(symbols.ParseSymbolCloseBrace, symbols.CloseCurlyBracketToken),
	})
}

// --- Assignment and Declaration Rules ---

func (d *DSLParsingRules) conditionDeclarationRule() parsingrules.ParsingRuleInterface[symbols.LexingTokenType] {
	return d.factory.NewNested(symbols.ParseSymbolCondition, []parsingrules.ParsingRuleInterface[symbols.LexingTokenType]{
		d.token(symbols.ParseSymbolConditionAssignment, symbols.ConditionAssignmentKeywordToken),
		d.whitespaceOptional(),
		d.token(symbols.ParseSymbolConditionKeywordToken, symbols.ConditionKeywordToken),
		d.whitespaceOptional(),
		d.factory.NewEither(symbols.ParseSymbolComparisonOperator, []symbols.LexingTokenType{
			symbols.GreaterThanOrEqualOperatorToken,
			symbols.LessThanOrEqualOperatorToken,
			symbols.GreaterThanOperatorToken,
			symbols.LessThanOperatorToken,
			symbols.ExactMatchOperatorToken,
		}),
		d.whitespaceOptional(),
		d.token(symbols.ParseSymbolVariableReference, symbols.VariableReferenceToken),
	})
}

func (d *DSLParsingRules) generalVariableAssignmentRule() parsingrules.ParsingRuleInterface[symbols.LexingTokenType] {
	return d.factory.NewNested(symbols.ParseSymbolGeneralVariable, []parsingrules.ParsingRuleInterface[symbols.LexingTokenType]{
		d.token(symbols.ParseVariableAssignmentKey, symbols.VariableKeywordToken),
		d.requiredWhitespace(),
		d.token(symbols.ParseSymbolIdentifier, symbols.IdentifierKeyToken),
		d.requiredWhitespace(),
		d.token(symbols.ParseSymbolAssignmentOp, symbols.AssignmentOperatorToken),
		d.requiredWhitespace(),
		d.factory.NewEither(symbols.ParseSymbolValue, []symbols.LexingTokenType{
			symbols.IdentifierValueToken,
			symbols.DigitToken,
		}),
	})
}

func (d *DSLParsingRules) implicitVariableAssignmentRule() parsingrules.ParsingRuleInterface[symbols.LexingTokenType] {
	return d.factory.NewNested(symbols.ParseSymbolGeneralVariable, []parsingrules.ParsingRuleInterface[symbols.LexingTokenType]{
		d.token(symbols.ParseSymbolChainOperator, symbols.ChainOperatorToken),
		d.whitespaceOptional(),
		d.token(symbols.ParseSymbolIdentifier, symbols.IdentifierKeyToken),
		d.whitespaceOptional(),
		d.token(symbols.ParseSymbolAssignmentOp, symbols.AssignmentOperatorToken),
		d.whitespaceOptional(),
		d.token(symbols.ParseSymbolValue, symbols.IdentifierValueToken),
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

// --- Rule Definition Helpers ---

// makeAssignmentRule builds a rule for 'Key Whitespace AssignmentOp Whitespace Value...'.
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

	return d.factory.NewNested(assignmentSymbol, []parsingrules.ParsingRuleInterface[symbols.LexingTokenType]{
		d.token(symbols.ParseSymbolKey, keywordToken),
		d.token(symbols.ParseSymbolWhitespaceBeforeOp, symbols.WhitespaceToken),
		d.token(symbols.ParseSymbolAssignmentOp, symbols.AssignmentOperatorToken),
		d.token(symbols.ParseSymbolWhitespaceAfterOp, symbols.WhitespaceToken),
		d.factory.NewMatchUntil(symbols.ParseSymbolValue, valueTokenTypes, symbolStrings),
	})
}

// token is a shorthand for creating a simple, single-token parsing rule.
func (d *DSLParsingRules) token(sym symbols.ParseSymbol, tokenType symbols.LexingTokenType) parsingrules.ParsingRuleInterface[symbols.LexingTokenType] {
	return d.factory.NewSingle(sym, tokenType)
}

// whitespaceOptional matches zero or more Whitespace or NewLine tokens.
func (d *DSLParsingRules) whitespaceOptional() parsingrules.ParsingRuleInterface[symbols.LexingTokenType] {
	return d.factory.NewOptional(symbols.ParseSymbolWhitespace, []parsingrules.ParsingRuleInterface[symbols.LexingTokenType]{
		d.token(symbols.ParseSymbolWhitespaceToken, symbols.WhitespaceToken),
		d.token(symbols.ParseSymbolNewLineToken, symbols.NewLineToken),
	})
}

// requiredWhitespace matches exactly one mandatory Whitespace token.
func (d *DSLParsingRules) requiredWhitespace() parsingrules.ParsingRuleInterface[symbols.LexingTokenType] {
	return d.token(symbols.ParseSymbolWhitespace, symbols.WhitespaceToken)
}

// --- Standalone Fallback Matchers ---

func (d *DSLParsingRules) matchNewLine() parsingrules.ParsingRuleInterface[symbols.LexingTokenType] {
	return d.token(symbols.ParseSymbolNewLineToken, symbols.NewLineToken)
}

func (d *DSLParsingRules) matchWhiteSpace() parsingrules.ParsingRuleInterface[symbols.LexingTokenType] {
	return d.token(symbols.ParseSymbolWhitespace, symbols.WhitespaceToken)
}

func (d *DSLParsingRules) matchAnyFallbackRule() parsingrules.ParsingRuleInterface[symbols.LexingTokenType] {
	return d.factory.NewMatchAny(symbols.ParseSymbolAny)
}
