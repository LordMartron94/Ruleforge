package composite

import (
	"fmt"
	lexshared "github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/lexing/shared"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/parsing/rules/internal"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/parsing/rules/shared"
	parseshared "github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/parsing/shared"
)

// NewChoiceRule creates a rule that matches any of the given subrules.
func NewChoiceRule[T lexshared.TokenTypeConstraint](symbol string, subrules []shared.ParsingRuleInterface[T]) shared.ParsingRuleInterface[T] {
	return &ChoiceRule[T]{
		BaseParsingRule: internal.BaseParsingRule[T]{SymbolString: symbol},
		subrules:        subrules,
	}
}

// ChoiceRule matches any of the given subrules.
type ChoiceRule[T lexshared.TokenTypeConstraint] struct {
	internal.BaseParsingRule[T]
	subrules []shared.ParsingRuleInterface[T]
}

func (r *ChoiceRule[T]) Match(tokens []*lexshared.Token[T], index int) (*parseshared.ParseTree[T], error, int) {
	for _, subrule := range r.subrules {
		nodes, err, consumed := subrule.Match(tokens, index)
		if err == nil {
			return nodes, err, consumed
		}
	}

	return nil, fmt.Errorf("no matched subrule"), 0
}
