package rules

import (
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/lexing/internal/util"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/lexing/scanning"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/lexing/shared"
)

// WhitespaceRule defines a rule for matching consecutive whitespace characters.
type WhitespaceRule[T shared.TokenTypeConstraint] struct {
	BaseLexingRule[T]
}

// NewWhitespaceLexingRule creates a new lexing rule for whitespace.
func NewWhitespaceLexingRule[T shared.TokenTypeConstraint](tokenType T, symbol string) LexingRuleInterface[T] {
	rule := &WhitespaceRule[T]{}
	rule.SymbolString = symbol
	rule.AssociatedToken = tokenType
	rule.MatchFunc = rule.isMatch
	rule.GetContentFunc = rule.getContent
	return rule
}

func (ws *WhitespaceRule[T]) isWhitespace(r rune) bool {
	return r == '\t' || r == '\n' || r == '\r' || r == ' ' || r == '\f' || r == '\v'
}

func (ws *WhitespaceRule[T]) isMatch(scanner scanning.PeekInterface) bool {
	return ws.isWhitespace(scanner.Current())
}

func (ws *WhitespaceRule[T]) getContent(scanner scanning.PeekInterface) []rune {
	return util.ScanWhile(scanner, ws.isWhitespace)
}
