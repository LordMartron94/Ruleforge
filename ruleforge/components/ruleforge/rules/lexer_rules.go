package rules

import (
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/lexing/rules"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/lexing/rules/factory"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/rules/symbols"
)

// RuleFactory creates and configures lexing rules.
type RuleFactory struct {
	factory *factory.RuleFactory[symbols.LexingTokenType]
}

// NewRuleFactory returns a new, configured RuleFactory.
func NewRuleFactory() *RuleFactory {
	return &RuleFactory{factory: &factory.RuleFactory[symbols.LexingTokenType]{}}
}

// GetLexingRules returns all configured lexing rules in the correct order.
func (f *RuleFactory) GetLexingRules() []rules.LexingRuleInterface[symbols.LexingTokenType] {
	allRules := make([]rules.LexingRuleInterface[symbols.LexingTokenType], 0, 30) // Pre-allocate capacity

	// Append rule groups
	allRules = append(allRules, f.keywordRules()...)
	allRules = append(allRules, f.operatorRules()...)
	allRules = append(allRules, f.curlyBracketRules()...)

	// Append single rules
	allRules = append(allRules,
		f.newLineRule(),
		f.whitespaceRule(),
		f.varReferenceRule(),
		f.identifierValueRule(),
		f.digitRule(),
		f.identifierKeyRule(),
		f.letterRule(),
		f.invalidTokenRule(),
	)

	return allRules
}

// --- Rule Group symbols ---

// keywordRules defines all keyword lexing rules.
func (f *RuleFactory) keywordRules() []rules.LexingRuleInterface[symbols.LexingTokenType] {
	type keywordDefinition struct {
		literal string
		token   symbols.LexingTokenType
		symbol  string
	}

	keywordDefs := []keywordDefinition{
		{"METADATA", symbols.MetadataKeywordToken, "MetadataKeywordLexer"},
		{"NAME", symbols.NameKeywordToken, "NameKeywordLexer"},
		{"VERSION", symbols.VersionKeywordToken, "VersionKeywordLexer"},
		{"STRICTNESS", symbols.StrictnessKeywordToken, "StrictnessKeywordLexer"},
		{"ALL", symbols.AllKeywordToken, "AllKeywordLexer"},
		{"SOFT", symbols.SoftKeywordToken, "SoftKeywordLexer"},
		{"SEMI-STRICT", symbols.SemiStrictKeywordToken, "SemiStrictKeywordLexer"},
		{"STRICT", symbols.StrictKeywordToken, "StrictKeywordLexer"},
		{"SUPER-STRICT", symbols.SuperStrictKeywordToken, "SuperStrictKeywordLexer"},
		{"SECTION", symbols.SectionKeywordToken, "SectionKeywordLexer"},
		{"SECTION_CONDITIONS", symbols.SectionConditionsKeywordToken, "SectionConditionsKeywordLexer"},
		{"WHERE", symbols.ConditionAssignmentKeywordToken, "ConditionAssignmentKeywordLexer"},
		{"var", symbols.VariableKeywordToken, "VariableKeywordLexer"},
		{"@area_level", symbols.ConditionKeywordToken, "ConditionKeywordLexer"},
	}

	output := make([]rules.LexingRuleInterface[symbols.LexingTokenType], len(keywordDefs))
	boundary := f.keywordBoundaryRule()
	for i, def := range keywordDefs {
		output[i] = f.factory.NewKeywordLexingRule(def.literal, def.token, def.symbol, boundary)
	}
	return output
}

// operatorRules defines all operator lexing rules.
func (f *RuleFactory) operatorRules() []rules.LexingRuleInterface[symbols.LexingTokenType] {
	type operatorDefinition struct {
		literal string
		token   symbols.LexingTokenType
		symbol  string
		chars   []rune
	}

	operatorDefs := []operatorDefinition{
		{"=>", symbols.AssignmentOperatorToken, "AssignmentOperatorLexer", []rune{'=', '>'}},
		{"->", symbols.ChainOperatorToken, "ChainOperatorLexer", []rune{'-', '>'}},
		{"<=", symbols.LessThanOrEqualOperatorToken, "LessThanOrEqualOperatorLexer", []rune{'<', '='}},
		{">=", symbols.GreaterThanOrEqualOperatorToken, "GreaterThanOrEqualOperatorLexer", []rune{'>', '='}},
		{"==", symbols.ExactMatchOperatorToken, "ExactMatchOperatorLexer", []rune{'='}},
		{"<", symbols.LessThanOperatorToken, "LessThanOperatorLexer", []rune{'<'}},
		{">", symbols.GreaterThanOperatorToken, "GreaterThanOperatorLexer", []rune{'>'}},
	}

	output := make([]rules.LexingRuleInterface[symbols.LexingTokenType], len(operatorDefs))
	for i, def := range operatorDefs {
		boundary := f.factory.NewCharacterOptionLexingRule(def.chars, def.token, def.symbol)
		output[i] = f.factory.NewKeywordLexingRule(def.literal, def.token, def.symbol, boundary)
	}
	return output
}

// curlyBracketRules defines rules for open and close curly brackets.
func (f *RuleFactory) curlyBracketRules() []rules.LexingRuleInterface[symbols.LexingTokenType] {
	return []rules.LexingRuleInterface[symbols.LexingTokenType]{
		f.factory.NewSpecificCharacterLexingRule('{', symbols.OpenCurlyBracketToken, "OpenCurlyBracketLexer"),
		f.factory.NewSpecificCharacterLexingRule('}', symbols.CloseCurlyBracketToken, "CloseCurlyBracketLexer"),
	}
}

// --- Single Rule symbols ---
func (f *RuleFactory) newLineRule() rules.LexingRuleInterface[symbols.LexingTokenType] {
	return f.factory.NewCharacterOptionLexingRule([]rune{'\r', '\n'}, symbols.NewLineToken, "newline")
}

func (f *RuleFactory) whitespaceRule() rules.LexingRuleInterface[symbols.LexingTokenType] {
	return f.factory.NewWhitespaceLexingRule(symbols.WhitespaceToken, "WhitespaceLexer")
}

func (f *RuleFactory) varReferenceRule() rules.LexingRuleInterface[symbols.LexingTokenType] {
	prefix := '$'
	return f.factory.NewUnquotedIdentifierLexingRule(symbols.VariableReferenceToken, "VariableReferenceLexer", f.keywordBoundaryRule(), &prefix)
}

func (f *RuleFactory) identifierValueRule() rules.LexingRuleInterface[symbols.LexingTokenType] {
	return f.factory.NewQuotedIdentifierLexingRule(symbols.IdentifierValueToken, "IdentifierValueLexer", false, f.identifierFilterCharactersRule())
}

func (f *RuleFactory) digitRule() rules.LexingRuleInterface[symbols.LexingTokenType] {
	return f.factory.NewNumberLexingRule(symbols.DigitToken, "DigitLexer")
}

func (f *RuleFactory) identifierKeyRule() rules.LexingRuleInterface[symbols.LexingTokenType] {
	return f.factory.NewUnquotedIdentifierLexingRule(symbols.IdentifierKeyToken, "IdentifierKeyLexer", f.keywordBoundaryRule(), nil)
}

func (f *RuleFactory) letterRule() rules.LexingRuleInterface[symbols.LexingTokenType] {
	return f.factory.NewAlphaNumericRuleSingle(symbols.LetterToken, "LetterLexer", false)
}

func (f *RuleFactory) invalidTokenRule() rules.LexingRuleInterface[symbols.LexingTokenType] {
	return f.factory.NewMatchAnyTokenRule(symbols.IgnoreToken)
}

// --- Rule Helpers ---
func (f *RuleFactory) keywordBoundaryRule() rules.LexingRuleInterface[symbols.LexingTokenType] {
	return f.factory.NewOrLexingRule(
		symbols.IdentifierValueToken,
		"keywordBoundary",
		f.letterRule(),
		f.digitRule(),
		f.identifierAllowedSpecialChars(),
	)
}

func (f *RuleFactory) identifierFilterCharactersRule() rules.LexingRuleInterface[symbols.LexingTokenType] {
	return f.factory.NewOrLexingRule(symbols.IdentifierValueToken, "identifierFilterCharacters",
		f.digitRule(), f.letterRule(), f.whitespaceRule(), f.identifierAllowedSpecialChars())
}

func (f *RuleFactory) identifierAllowedSpecialChars() rules.LexingRuleInterface[symbols.LexingTokenType] {
	return f.factory.NewCharacterOptionLexingRule([]rune{'.', '_', '-'}, symbols.IdentifierValueToken, "identifierAllowedSpecialChars")
}
