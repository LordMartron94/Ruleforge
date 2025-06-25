package rules

import (
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/lexing/scanning"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/lexing/shared"
	"slices"
)

// NewSpecificCharacterLexingRule creates a rule that matches exactly one specific character.
func NewSpecificCharacterLexingRule[T shared.TokenTypeConstraint](character rune, associatedToken T, symbol string) LexingRuleInterface[T] {
	return &BaseLexingRule[T]{
		SymbolString: symbol,
		MatchFunc: func(scanner scanning.PeekInterface) bool {
			return scanner.Current() == character
		},
		AssociatedToken: associatedToken,
		GetContentFunc: func(scanner scanning.PeekInterface) []rune {
			return []rune{character}
		},
	}
}

// NewCharacterOptionLexingRule creates a rule that matches any single character from a given set.
func NewCharacterOptionLexingRule[T shared.TokenTypeConstraint](characters []rune, associatedToken T, symbol string) LexingRuleInterface[T] {
	return &BaseLexingRule[T]{
		SymbolString: symbol,
		MatchFunc: func(scanner scanning.PeekInterface) bool {
			return slices.Contains(characters, scanner.Current())
		},
		AssociatedToken: associatedToken,
		GetContentFunc: func(scanner scanning.PeekInterface) []rune {
			return []rune{scanner.Current()}
		},
	}
}
