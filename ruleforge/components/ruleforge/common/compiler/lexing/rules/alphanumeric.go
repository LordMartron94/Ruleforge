package rules

import (
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/lexing/scanning"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/lexing/shared"
)

// NewAlphaNumericRuleSingle creates a new lexing rule for a single letter,
// optionally allowing digits as well.
func NewAlphaNumericRuleSingle[T shared.TokenTypeConstraint](tokenType T, symbol string, includeDigits bool) LexingRuleInterface[T] {
	isValid := func(ch rune) bool {
		isLetter := ('a' <= ch && ch <= 'z') || ('A' <= ch && ch <= 'Z')
		if isLetter {
			return true
		}
		if includeDigits {
			isDigit := '0' <= ch && ch <= '9'
			return isDigit
		}
		return false
	}

	return &BaseLexingRule[T]{
		SymbolString:    symbol,
		MatchFunc:       func(scanner scanning.PeekInterface) bool { return isValid(scanner.Current()) },
		AssociatedToken: tokenType,
		GetContentFunc:  func(scanner scanning.PeekInterface) []rune { return []rune{scanner.Current()} },
	}
}
