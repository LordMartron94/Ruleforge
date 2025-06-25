package composite

import (
	lexshared "github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/lexing/shared"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/parsing/rules/internal"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/parsing/rules/shared"
	parseshared "github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/parsing/shared"
)

// NewRepetitionRule creates a rule that matches one of its child rules zero or more times.
// It will continue matching as long as one of the child rules can successfully match and *consume* at least one token.
func NewRepetitionRule[T lexshared.TokenTypeConstraint](symbol string, childRules ...shared.ParsingRuleInterface[T]) shared.ParsingRuleInterface[T] {
	return &RepetitionRule[T]{
		BaseParsingRule: internal.BaseParsingRule[T]{SymbolString: symbol},
		childRules:      childRules,
	}
}

// RepetitionRule matches a sequence comprising any of its child rules, zero or more times.
type RepetitionRule[T lexshared.TokenTypeConstraint] struct {
	internal.BaseParsingRule[T]
	childRules []shared.ParsingRuleInterface[T]
}

func (r *RepetitionRule[T]) Match(tokens []*lexshared.Token[T], index int) (*parseshared.ParseTree[T], error, int) {
	children := make([]*parseshared.ParseTree[T], 0)
	currentIndex := index
	totalConsumed := 0

	for currentIndex < len(tokens) {
		var matchedThisIteration bool
		for _, rule := range r.childRules {
			childTree, err, consumed := rule.Match(tokens, currentIndex)

			if err == nil && consumed > 0 {
				children = append(children, childTree)
				currentIndex += consumed
				totalConsumed += consumed
				matchedThisIteration = true
				break
			}
		}

		if !matchedThisIteration {
			break
		}
	}

	tree := &parseshared.ParseTree[T]{
		Symbol:   r.Symbol(),
		Children: children,
	}
	return tree, nil, totalConsumed
}
