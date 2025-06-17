package rules

import (
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/parsing/rules"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/parsing/rules/factory"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/rules/definitions"
)

// ruleFactory wraps a ParsingRuleFactory and accepts ParseSymbol directly.
type ruleFactory struct {
	inner *factory.ParsingRuleFactory[definitions.LexingTokenType]
}

// newRuleFactory constructs a ruleFactory from a ParsingRuleFactory.
func newRuleFactory(inner *factory.ParsingRuleFactory[definitions.LexingTokenType]) *ruleFactory {
	return &ruleFactory{inner: inner}
}

func (r *ruleFactory) NewSingle(sym definitions.ParseSymbol, token definitions.LexingTokenType) rules.ParsingRuleInterface[definitions.LexingTokenType] {
	return r.inner.NewSingleTokenParsingRule(sym.String(), token)
}

func (r *ruleFactory) NewNested(sym definitions.ParseSymbol, children []rules.ParsingRuleInterface[definitions.LexingTokenType]) rules.ParsingRuleInterface[definitions.LexingTokenType] {
	return r.inner.NewNestedParsingRule(sym.String(), children)
}

func (r *ruleFactory) NewOptional(sym definitions.ParseSymbol, children []rules.ParsingRuleInterface[definitions.LexingTokenType]) rules.ParsingRuleInterface[definitions.LexingTokenType] {
	return r.inner.NewOptionalNestedParsingRule(sym.String(), children)
}

func (r *ruleFactory) NewMatchUntil(sym definitions.ParseSymbol, childTypes []definitions.LexingTokenType, childSymbols []string) rules.ParsingRuleInterface[definitions.LexingTokenType] {
	return r.inner.NewMatchUntilTokenWithFilterParsingRule(sym.String(), childTypes, childSymbols)
}

func (r *ruleFactory) NewMatchAny(sym definitions.ParseSymbol) rules.ParsingRuleInterface[definitions.LexingTokenType] {
	return r.inner.NewMatchAnyTokenParsingRule(sym.String())
}

// DSLParsingRules encapsulates parsing rules for the metadata DSL section.
type DSLParsingRules struct {
	factory *ruleFactory
}

// NewDSLParsingRules constructs a new DSLParsingRules instance.
func NewDSLParsingRules() *DSLParsingRules {
	innerFactory := factory.NewParsingRuleFactory[definitions.LexingTokenType]()
	return &DSLParsingRules{
		factory: newRuleFactory(innerFactory),
	}
}

// GetParsingRules returns the parsing rules including the metadata section.
func (d *DSLParsingRules) GetParsingRules() []rules.ParsingRuleInterface[definitions.LexingTokenType] {
	return []rules.ParsingRuleInterface[definitions.LexingTokenType]{
		d.metadataSectionRule(),
		d.matchAnyFallbackRule(),
	}
}

// metadataSectionRule parses: Metadata keyword, optional whitespace/newlines,
// '{', zero or more assignments..., then '}'.
func (d *DSLParsingRules) metadataSectionRule() rules.ParsingRuleInterface[definitions.LexingTokenType] {
	return d.factory.NewNested(definitions.ParseSymbolMetadataSection, []rules.ParsingRuleInterface[definitions.LexingTokenType]{
		d.factory.NewSingle(definitions.ParseSymbolMetadataKeyword, definitions.MetadataKeywordToken),
		d.whitespaceOptional(),
		d.factory.NewSingle(definitions.ParseSymbolOpenBrace, definitions.CurlyBracketToken),
		d.factory.NewOptional(definitions.ParseSymbolAssignments, []rules.ParsingRuleInterface[definitions.LexingTokenType]{
			d.nameAssignmentRule(),
			d.whitespaceOptional(),
			d.versionAssignmentRule(),
			d.whitespaceOptional(),
			d.strictnessAssignmentRule(),
			d.whitespaceOptional(),
		}),
		d.factory.NewSingle(definitions.ParseSymbolCloseBrace, definitions.CurlyBracketToken),
	})
}

// whitespaceOptional matches zero or more Whitespace or NewLine tokens.
func (d *DSLParsingRules) whitespaceOptional() rules.ParsingRuleInterface[definitions.LexingTokenType] {
	return d.factory.NewOptional(definitions.ParseSymbolWhitespace, []rules.ParsingRuleInterface[definitions.LexingTokenType]{
		d.factory.NewSingle(definitions.ParseSymbolWhitespaceToken, definitions.WhitespaceToken),
		d.factory.NewSingle(definitions.ParseSymbolNewLineToken, definitions.NewLineToken),
	})
}

func (d *DSLParsingRules) nameAssignmentRule() rules.ParsingRuleInterface[definitions.LexingTokenType] {
	return d.makeAssignmentRule(
		definitions.ParseSymbolNameAssignment, definitions.NameKeywordToken,
		[]definitions.LexingTokenType{definitions.IdentifierToken},
		[]definitions.ParseSymbol{definitions.ParseSymbolIdentifier},
	)
}

func (d *DSLParsingRules) versionAssignmentRule() rules.ParsingRuleInterface[definitions.LexingTokenType] {
	return d.makeAssignmentRule(
		definitions.ParseSymbolVersionAssignment, definitions.VersionKeywordToken,
		[]definitions.LexingTokenType{definitions.IdentifierToken},
		[]definitions.ParseSymbol{definitions.ParseSymbolIdentifier},
	)
}

func (d *DSLParsingRules) strictnessAssignmentRule() rules.ParsingRuleInterface[definitions.LexingTokenType] {
	return d.makeAssignmentRule(
		definitions.ParseSymbolStrictnessAssignment, definitions.StrictnessKeywordToken,
		[]definitions.LexingTokenType{definitions.AllKeywordToken, definitions.SoftKeywordToken, definitions.SemiStrictKeywordToken, definitions.StrictKeywordToken, definitions.SuperStrictKeywordToken, definitions.LetterToken},
		[]definitions.ParseSymbol{definitions.ParseSymbolAll, definitions.ParseSymbolSoft, definitions.ParseSymbolSemiStrict, definitions.ParseSymbolStrict, definitions.ParseSymbolSuperStrict, definitions.ParseSymbolAny},
	)
}

// makeAssignmentRule builds a rule for 'Key Whitespace AssignmentOp Whitespace Value...'.
func (d *DSLParsingRules) makeAssignmentRule(
	symbol definitions.ParseSymbol,
	keyType definitions.LexingTokenType,
	childTypes []definitions.LexingTokenType,
	childSymbols []definitions.ParseSymbol,
) rules.ParsingRuleInterface[definitions.LexingTokenType] {
	symbols := make([]string, len(childSymbols))
	for i, cs := range childSymbols {
		symbols[i] = cs.String()
	}
	return d.factory.NewNested(symbol, []rules.ParsingRuleInterface[definitions.LexingTokenType]{
		d.factory.NewSingle(definitions.ParseSymbolKey, keyType),
		d.factory.NewSingle(definitions.ParseSymbolWhitespaceBeforeOp, definitions.WhitespaceToken),
		d.factory.NewSingle(definitions.ParseSymbolAssignmentOp, definitions.AssignmentOperatorToken),
		d.factory.NewSingle(definitions.ParseSymbolWhitespaceAfterOp, definitions.WhitespaceToken),
		d.factory.NewMatchUntil(definitions.ParseSymbolValue, childTypes, symbols),
	})
}

func (d *DSLParsingRules) matchAnyFallbackRule() rules.ParsingRuleInterface[definitions.LexingTokenType] {
	return d.factory.NewMatchAny(definitions.ParseSymbolAny)
}
