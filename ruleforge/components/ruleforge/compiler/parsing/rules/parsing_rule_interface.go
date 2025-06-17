package rules

import (
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/compiler/lexing/shared"
	shared2 "github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/compiler/parsing/shared"
)

type ParsingRuleInterface[T comparable] interface {
	// Symbol returns the grammar symbol this rule represents (e.g., "expression", "statement", "term").
	Symbol() string

	// Match checks if the given sequence of tokens matches this rule's pattern.
	// It might return a ParseTree node if successful, or an error if it fails.
	// It will also return the amount of tokens consumed by the match.
	Match(tokens []*shared.Token[T], currentIndex int) (*shared2.ParseTree[T], error, int)
}
