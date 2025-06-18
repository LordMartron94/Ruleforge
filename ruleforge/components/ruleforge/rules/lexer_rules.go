package rules

import (
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/lexing/rules"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/lexing/rules/special"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/rules/symbols"
)

// --- Re-usable Rule Components (defined once for efficiency) ---

var (
	// Basic building blocks for other rules.
	letterRule                    = rules.NewAlphaNumericRuleSingle(symbols.LetterToken, "LetterLexer", false)
	numberRule                    = rules.NewNumberRule("NumberLexer", symbols.NumberToken)
	whitespaceRule                = rules.NewWhitespaceLexingRule(symbols.WhitespaceToken, "WhitespaceLexer")
	identifierAllowedSpecialChars = rules.NewCharacterOptionLexingRule([]rune{'.', '_', '-'}, symbols.IdentifierValueToken, "identifierAllowedSpecialChars")

	// Composite rules built from the components above.
	// This rule defines what an unquoted identifier can be made of.
	unquotedIdentifierCharsRule = rules.NewOrLexingRule(
		symbols.IdentifierKeyToken, "unquotedIdentifierChars",
		letterRule, numberRule, identifierAllowedSpecialChars,
	)
	// This rule defines what a quoted identifier can contain.
	quotedIdentifierCharsRule = rules.NewOrLexingRule(
		symbols.IdentifierValueToken, "quotedIdentifierChars",
		numberRule, letterRule, whitespaceRule, identifierAllowedSpecialChars,
	)

	// Skip everything from "!!" to the end of the line (but keep the newline itself).
	lineCommentRule = special.NewLineCommentLexingRule(
		"LineCommentLexer",
		symbols.IgnoreToken,
		"!!",
	)

	// Skip everything between "!![" and "]!!", spanning multiple lines if needed.
	blockCommentRule = special.NewDelimitedContentLexingRule(
		"BlockCommentLexer",
		symbols.IgnoreToken,
		"!![",
		"]!!",
	)
)

// GetLexingRules returns all configured lexing rules in the correct order of precedence.
func GetLexingRules() []rules.LexingRuleInterface[symbols.LexingTokenType] {
	// The order is critical for correct tokenization. More specific rules must come first.
	return appendSlices(
		// 1. Comment rules
		[]rules.LexingRuleInterface[symbols.LexingTokenType]{
			blockCommentRule,
			lineCommentRule,
		},

		// 2. Fixed strings are most specific.
		buildKeywordRules(),
		buildOperatorRules(),

		// 3. Literals with specific patterns (numbers, quoted strings).
		buildLiteralValueRules(),

		// 4. General identifiers (unquoted keys, variable refs). This is less specific than a number.
		buildIdentifierRules(),

		// 5. Structural elements like brackets and whitespace.
		buildStructuralRules(),

		// 6. A final fallback for any character that wasn't matched.
		[]rules.LexingRuleInterface[symbols.LexingTokenType]{rules.NewMatchAnyTokenRule(symbols.IgnoreToken)},
	)
}

// buildKeywordRules defines all keyword lexing rules.
func buildKeywordRules() []rules.LexingRuleInterface[symbols.LexingTokenType] {
	keywordDefs := []struct {
		literal string
		token   symbols.LexingTokenType
		symbol  string
	}{
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
	for i, def := range keywordDefs {
		output[i] = special.NewKeywordLexingRule(def.literal, def.symbol, def.token, unquotedIdentifierCharsRule)
	}
	return output
}

// buildOperatorRules defines all operator lexing rules.
func buildOperatorRules() []rules.LexingRuleInterface[symbols.LexingTokenType] {
	operatorDefs := []struct {
		literal string
		token   symbols.LexingTokenType
		symbol  string
	}{
		{"=>", symbols.AssignmentOperatorToken, "AssignmentOperatorLexer"},
		{"->", symbols.ChainOperatorToken, "ChainOperatorLexer"},
		{"<=", symbols.LessThanOrEqualOperatorToken, "LessThanOrEqualOperatorLexer"},
		{">=", symbols.GreaterThanOrEqualOperatorToken, "GreaterThanOrEqualOperatorLexer"},
		{"==", symbols.ExactMatchOperatorToken, "ExactMatchOperatorLexer"},
		{"<", symbols.LessThanOperatorToken, "LessThanOperatorLexer"},
		{">", symbols.GreaterThanOperatorToken, "GreaterThanOperatorLexer"},
	}

	output := make([]rules.LexingRuleInterface[symbols.LexingTokenType], len(operatorDefs))
	for i, def := range operatorDefs {
		// The boundary for a multi-char operator is itself, to prevent '<' from matching '<='.
		boundary := rules.NewCharacterOptionLexingRule([]rune(def.literal), def.token, def.symbol)
		output[i] = special.NewKeywordLexingRule(def.literal, def.symbol, def.token, boundary)
	}
	return output
}

// buildLiteralValueRules defines rules for explicit data values like numbers and quoted strings.
func buildLiteralValueRules() []rules.LexingRuleInterface[symbols.LexingTokenType] {
	return []rules.LexingRuleInterface[symbols.LexingTokenType]{
		// Rule for numbers (e.g., 69, 123). This now runs before the general identifier rule.
		numberRule,
		// Rule for quoted strings (e.g., "hello world").
		special.NewQuotedValueRule("IdentifierValueLexer", symbols.IdentifierValueToken, false, quotedIdentifierCharsRule),
	}
}

// buildIdentifierRules defines rules for unquoted, named entities.
func buildIdentifierRules() []rules.LexingRuleInterface[symbols.LexingTokenType] {
	prefix := '$'
	return []rules.LexingRuleInterface[symbols.LexingTokenType]{
		// A variable reference must start with '$'.
		special.NewUnquotedValueRule("VariableReferenceLexer", symbols.VariableReferenceToken, unquotedIdentifierCharsRule, &prefix),
		// A general-purpose identifier for keys. This will no longer incorrectly match plain numbers.
		special.NewUnquotedValueRule("IdentifierKeyLexer", symbols.IdentifierKeyToken, unquotedIdentifierCharsRule, nil),
	}
}

// buildStructuralRules defines rules for syntax structure like brackets, newlines, and whitespace.
func buildStructuralRules() []rules.LexingRuleInterface[symbols.LexingTokenType] {
	return []rules.LexingRuleInterface[symbols.LexingTokenType]{
		rules.NewSpecificCharacterLexingRule('{', symbols.OpenCurlyBracketToken, "OpenCurlyBracketLexer"),
		rules.NewSpecificCharacterLexingRule('}', symbols.CloseCurlyBracketToken, "CloseCurlyBracketLexer"),
		rules.NewCharacterOptionLexingRule([]rune{'\r', '\n'}, symbols.NewLineToken, "newline"),
		whitespaceRule,
	}
}

// appendSlices is a small utility to make GetLexingRules cleaner.
func appendSlices(slices ...[]rules.LexingRuleInterface[symbols.LexingTokenType]) []rules.LexingRuleInterface[symbols.LexingTokenType] {
	var totalLen int
	for _, s := range slices {
		totalLen += len(s)
	}
	result := make([]rules.LexingRuleInterface[symbols.LexingTokenType], 0, totalLen)
	for _, s := range slices {
		result = append(result, s...)
	}
	return result
}
