package rules

import (
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/lexing/rules"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/lexing/rules/factory"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/rules/definitions"
)

type RuleFactory struct {
	factory *factory.RuleFactory[definitions.LexingTokenType]
}

// NewRuleFactory returns a new RuleFactory instance.
func NewRuleFactory() *RuleFactory {
	return &RuleFactory{factory: &factory.RuleFactory[definitions.LexingTokenType]{}}
}

// GetLexingRules returns all configured lexing rules in the correct order.
func (f *RuleFactory) GetLexingRules() []rules.LexingRuleInterface[definitions.LexingTokenType] {
	// group slices of multiple rules
	groups := [][]rules.LexingRuleInterface[definitions.LexingTokenType]{
		f.keywordRules(),
		f.operatorRules(),
	}

	var all []rules.LexingRuleInterface[definitions.LexingTokenType]
	for _, group := range groups {
		all = append(all, group...)
	}

	// single-rule lexers
	all = append(
		all,
		f.newLineRule(),
		f.whitespaceRule(),
		f.identifierValueRule(),
		f.identifierKeyRule(),
		f.curlyBracketRule(),
		f.digitRule(),
		f.letterRule(),
		f.invalidTokenRule(),
	)

	return all
}

// PRIVATE HELPERS (not part of public API)

// keywordRules returns rules for all keywords.
func (f *RuleFactory) keywordRules() []rules.LexingRuleInterface[definitions.LexingTokenType] {
	return []rules.LexingRuleInterface[definitions.LexingTokenType]{
		f.keywordRule("METADATA", definitions.MetadataKeywordToken, "MetadataKeywordLexer"),
		f.keywordRule("NAME", definitions.NameKeywordToken, "NameKeywordLexer"),
		f.keywordRule("VERSION", definitions.VersionKeywordToken, "VersionKeywordLexer"),
		f.keywordRule("STRICTNESS", definitions.StrictnessKeywordToken, "StrictnessKeywordLexer"),
		f.keywordRule("ALL", definitions.AllKeywordToken, "AllKeywordLexer"),
		f.keywordRule("SOFT", definitions.SoftKeywordToken, "SoftKeywordLexer"),
		f.keywordRule("SEMI-STRICT", definitions.SemiStrictKeywordToken, "SemiStrictKeywordLexer"),
		f.keywordRule("STRICT", definitions.StrictKeywordToken, "StrictKeywordLexer"),
		f.keywordRule("SUPER-STRICT", definitions.SuperStrictKeywordToken, "SuperStrictKeywordLexer"),
		f.keywordRule("var", definitions.VariableKeywordToken, "VariableKeywordLexer"),
	}
}

func (f *RuleFactory) identifierFilterCharactersRule() rules.LexingRuleInterface[definitions.LexingTokenType] {
	return f.factory.NewOrLexingRule(definitions.IdentifierValueToken, "identifierFilterCharacters",
		f.digitRule(), f.letterRule(), f.whitespaceRule(), f.identifierAllowedSpecialChars())
}

func (f *RuleFactory) identifierAllowedSpecialChars() rules.LexingRuleInterface[definitions.LexingTokenType] {
	return f.factory.NewCharacterOptionLexingRule([]rune{'.', '_', '-'}, definitions.IdentifierValueToken, "identifierAllowedSpecialChars")
}

func (f *RuleFactory) keywordBoundaryRule() rules.LexingRuleInterface[definitions.LexingTokenType] {
	return f.factory.NewOrLexingRule(
		definitions.IdentifierValueToken,
		"keywordBoundary",
		f.letterRule(),
		f.digitRule(),
		f.identifierAllowedSpecialChars(),
	)
}

// operatorRules returns rules for operators.
func (f *RuleFactory) operatorRules() []rules.LexingRuleInterface[definitions.LexingTokenType] {
	return []rules.LexingRuleInterface[definitions.LexingTokenType]{
		f.assignmentOperatorRule(),
	}
}

// assignmentOperatorRule returns the assignment operator rule.
func (f *RuleFactory) assignmentOperatorRule() rules.LexingRuleInterface[definitions.LexingTokenType] {
	return f.factory.NewKeywordLexingRule(
		"=>", definitions.AssignmentOperatorToken, "AssignmentOperatorLexer",
		f.factory.NewCharacterOptionLexingRule([]rune{'=', '>'}, definitions.AssignmentOperatorToken, "AssignmentOperatorLexer"),
	)
}

// keywordRule constructs a keyword rule for a given string and token type.
func (f *RuleFactory) keywordRule(
	literal string,
	tokenType definitions.LexingTokenType,
	symbol string,
) rules.LexingRuleInterface[definitions.LexingTokenType] {
	return f.factory.NewKeywordLexingRule(literal, tokenType, symbol, f.keywordBoundaryRule())
}

// whitespaceRule matches whitespace.
func (f *RuleFactory) whitespaceRule() rules.LexingRuleInterface[definitions.LexingTokenType] {
	return f.factory.NewWhitespaceLexingRule(definitions.WhitespaceToken, "WhitespaceLexer")
}

// newLineRule matches new lines.
func (f *RuleFactory) newLineRule() rules.LexingRuleInterface[definitions.LexingTokenType] {
	return f.factory.NewCharacterOptionLexingRule([]rune{'\r', '\n'}, definitions.NewLineToken, "newline")
}

// digitRule matches numeric digits.
func (f *RuleFactory) digitRule() rules.LexingRuleInterface[definitions.LexingTokenType] {
	return f.factory.NewNumberLexingRule(definitions.DigitToken, "DigitLexer")
}

// invalidTokenRule matches any invalid token.
func (f *RuleFactory) invalidTokenRule() rules.LexingRuleInterface[definitions.LexingTokenType] {
	return f.factory.NewMatchAnyTokenRule(definitions.IgnoreToken)
}

// curlyBracketRule matches '{' or '}'.
func (f *RuleFactory) curlyBracketRule() rules.LexingRuleInterface[definitions.LexingTokenType] {
	return f.factory.NewCharacterOptionLexingRule([]rune{'{', '}'}, definitions.CurlyBracketToken, "CurlyBracketLexer")
}

func (f *RuleFactory) letterRule() rules.LexingRuleInterface[definitions.LexingTokenType] {
	return f.factory.NewAlphaNumericRuleSingle(definitions.LetterToken, "LetterLexer", false)
}

func (f *RuleFactory) identifierValueRule() rules.LexingRuleInterface[definitions.LexingTokenType] {
	return f.factory.NewQuotedIdentifierLexingRule(definitions.IdentifierValueToken, "IdentifierValueLexer", false, f.identifierFilterCharactersRule())
}

func (f *RuleFactory) identifierKeyRule() rules.LexingRuleInterface[definitions.LexingTokenType] {
	return f.factory.NewUnquotedIdentifierLexingRule(definitions.IdentifierKeyToken, "IdentifierKeyLexer", f.keywordBoundaryRule())
}
