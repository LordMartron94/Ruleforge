package rules

import (
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/lexing/scanning"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/lexing/shared"
)

// NewIgnoreTokenLexingRule creates a rule that matches a pattern but is marked
// to be ignored by the lexer pipeline, effectively consuming the input without
// producing a token.
func NewIgnoreTokenLexingRule[T shared.TokenTypeConstraint](symbol string, associatedToken T, matchFunc func(scanning.PeekInterface) bool) LexingRuleInterface[T] {
	return &BaseLexingRule[T]{
		SymbolString:    symbol,
		MatchFunc:       matchFunc,
		AssociatedToken: associatedToken,
		GetContentFunc: func(scanner scanning.PeekInterface) []rune {
			return []rune("IGNORE")
		},
	}
}
