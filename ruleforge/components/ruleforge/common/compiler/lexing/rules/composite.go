package rules

import (
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/lexing/scanning"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/lexing/shared"
)

// OrLexingRule defines a rule that succeeds if any of its subrules match.
type OrLexingRule[T shared.TokenTypeConstraint] struct {
	BaseLexingRule[T]
	subRules []LexingRuleInterface[T]
}

// NewOrLexingRule creates a lexing rule that succeeds if any of the given subrules match.
// It emits a single, overarching tokenType for all successful alternatives.
func NewOrLexingRule[T shared.TokenTypeConstraint](tokenType T, symbol string, subRules ...LexingRuleInterface[T]) LexingRuleInterface[T] {
	rule := &OrLexingRule[T]{
		subRules: subRules,
	}
	rule.SymbolString = symbol
	rule.AssociatedToken = tokenType
	rule.MatchFunc = rule.isMatch
	rule.GetContentFunc = rule.getContent
	return rule
}

func (or *OrLexingRule[T]) isMatch(scanner scanning.PeekInterface) bool {
	for _, rule := range or.subRules {
		if rule.IsMatch(scanner) {
			return true
		}
	}
	return false
}

func (or *OrLexingRule[T]) getContent(scanner scanning.PeekInterface) []rune {
	for _, rule := range or.subRules {
		if rule.IsMatch(scanner) {
			// A subrule's ExtractToken gives us the definitive content.
			token, _, _ := rule.ExtractToken(scanner)
			return []rune(string(token.Value))
		}
	}

	return nil
}
