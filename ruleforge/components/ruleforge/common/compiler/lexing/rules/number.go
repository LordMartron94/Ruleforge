package rules

import (
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/lexing/internal/util"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/lexing/scanning"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/lexing/shared"
)

// NumberRule defines a lexing rule for matching numerical sequences.
type NumberRule[T shared.TokenTypeConstraint] struct {
	BaseLexingRule[T]
}

// NewNumberRule creates a new lexing rule for numbers.
func NewNumberRule[T shared.TokenTypeConstraint](symbol string, associatedToken T) LexingRuleInterface[T] {
	rule := &NumberRule[T]{}
	rule.SymbolString = symbol
	rule.AssociatedToken = associatedToken
	rule.MatchFunc = rule.isMatch
	rule.GetContentFunc = rule.getContent
	return rule
}

func (nr *NumberRule[T]) isDigit(r rune) bool {
	return '0' <= r && r <= '9'
}

func (nr *NumberRule[T]) isMatch(scanner scanning.PeekInterface) bool {
	return nr.isDigit(scanner.Current())
}

func (nr *NumberRule[T]) getContent(scanner scanning.PeekInterface) []rune {
	return util.ScanWhile(scanner, nr.isDigit)
}
