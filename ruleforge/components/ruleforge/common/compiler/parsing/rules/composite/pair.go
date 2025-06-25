package composite

import (
	"fmt"
	lexshared "github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/lexing/shared"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/parsing/rules/internal"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/parsing/rules/shared"
	parseshared "github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/parsing/shared"
)

// NewPairRule creates a rule that matches two adjacent child rules.
func NewPairRule[T lexshared.TokenTypeConstraint](
	symbol string,
	firstRule shared.ParsingRuleInterface[T],
	secondRule shared.ParsingRuleInterface[T],
) shared.ParsingRuleInterface[T] {
	return &PairRule[T]{
		BaseParsingRule: internal.BaseParsingRule[T]{SymbolString: symbol},
		firstRule:       firstRule,
		secondRule:      secondRule,
	}
}

// PairRule matches a specific pair of other rules in sequence.
type PairRule[T lexshared.TokenTypeConstraint] struct {
	internal.BaseParsingRule[T]
	firstRule  shared.ParsingRuleInterface[T]
	secondRule shared.ParsingRuleInterface[T]
}

func (r *PairRule[T]) Match(tokens []*lexshared.Token[T], index int) (*parseshared.ParseTree[T], error, int) {
	// Match the first part of the pair.
	firstTree, err, firstConsumed := r.firstRule.Match(tokens, index)
	if err != nil {
		return nil, fmt.Errorf("first element of pair %s failed: %w", r.Symbol(), err), 0
	}

	// Match the second part of the pair, starting after the first part.
	secondTree, err, secondConsumed := r.secondRule.Match(tokens, index+firstConsumed)
	if err != nil {
		return nil, fmt.Errorf("second element of pair %s failed: %w", r.Symbol(), err), 0
	}

	// Assign conventional symbols for the children of the pair.
	firstTree.Symbol = "first_element"
	secondTree.Symbol = "second_element"

	tree := &parseshared.ParseTree[T]{
		Symbol: r.Symbol(),
		Children: []*parseshared.ParseTree[T]{
			firstTree,
			secondTree,
		},
	}
	return tree, nil, firstConsumed + secondConsumed
}
