package special

import (
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/lexing/internal/util"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/lexing/rules"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/lexing/scanning"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/lexing/shared"
)

// KeywordRule defines a rule that matches a specific keyword, ensuring it is
// not followed by characters that would make it part of a larger identifier.
type KeywordRule[T shared.TokenTypeConstraint] struct {
	rules.BaseLexingRule[T]
	keyword        []rune
	identifierRule rules.LexingRuleInterface[T]
}

// NewKeywordLexingRule creates a lexing rule for a specific keyword like "if" or "else".
func NewKeywordLexingRule[T shared.TokenTypeConstraint](keyword, symbol string, associatedToken T, identifierRule rules.LexingRuleInterface[T]) rules.LexingRuleInterface[T] {
	rule := &KeywordRule[T]{
		keyword:        []rune(keyword),
		identifierRule: identifierRule,
	}
	rule.SymbolString = symbol
	rule.AssociatedToken = associatedToken
	rule.MatchFunc = rule.isMatch
	rule.GetContentFunc = func(scanning.PeekInterface) []rune {
		return rule.keyword
	}
	return rule
}

func (kr *KeywordRule[T]) isMatch(scanner scanning.PeekInterface) bool {
	// 1. Check if the current input matches the keyword's runes.
	if scanner.Current() != kr.keyword[0] {
		return false
	}
	peeked, err := scanner.Peek(len(kr.keyword) - 1)
	if err != nil {
		return false
	}
	fullMatchRunes := append([]rune{scanner.Current()}, peeked...)
	if string(fullMatchRunes) != string(kr.keyword) {
		return false
	}

	// 2. Peek one more rune to enforce a word boundary.
	if nextRunes, err := scanner.Peek(len(kr.keyword)); err == nil {
		nextRune := nextRunes[len(nextRunes)-1]
		stubScanner := &util.SingleRuneScanner{R: nextRune}

		// If the character *after* our keyword could start an identifier,
		// then this isn't a keyword match (e.g., "if" in "ifthen").
		if kr.identifierRule.IsMatch(stubScanner) {
			return false
		}
	}

	// It's the keyword, and it's properly bounded.
	return true
}
