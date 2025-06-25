package composite

import (
	"fmt"
	lexshared "github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/lexing/shared"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/parsing/rules/internal"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/parsing/rules/shared"
	parseshared "github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/parsing/shared"
)

// NewNestedRule creates a rule that matches a sequence of other parsing rules.
func NewNestedRule[T lexshared.TokenTypeConstraint](symbol string, childRules ...shared.ParsingRuleInterface[T]) shared.ParsingRuleInterface[T] {
	return &NestedRule[T]{
		BaseParsingRule: internal.BaseParsingRule[T]{SymbolString: symbol},
		childRules:      childRules,
	}
}

// NestedRule matches a fixed sequence of other child rules.
type NestedRule[T lexshared.TokenTypeConstraint] struct {
	internal.BaseParsingRule[T]
	childRules []shared.ParsingRuleInterface[T]
}

func (r *NestedRule[T]) Match(tokens []*lexshared.Token[T], index int) (*parseshared.ParseTree[T], error, int) {
	children := make([]*parseshared.ParseTree[T], len(r.childRules))
	currentIndex := index
	totalConsumed := 0

	for i, rule := range r.childRules {
		childTree, err, consumed := rule.Match(tokens, currentIndex)
		if err != nil {
			return nil, fmt.Errorf("sub-rule %s failed in %s: %w", rule.Symbol(), r.Symbol(), err), 0
		}
		children[i] = childTree
		currentIndex += consumed
		totalConsumed += consumed
	}

	tree := &parseshared.ParseTree[T]{
		Symbol:   r.Symbol(),
		Children: children,
	}
	return tree, nil, totalConsumed
}
