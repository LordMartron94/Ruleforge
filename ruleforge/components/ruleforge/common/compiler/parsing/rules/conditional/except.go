package conditional

import (
	"fmt"
	lexshared "github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/lexing/shared"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/parsing/rules/internal"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/parsing/rules/shared"
	parseshared "github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/parsing/shared"
)

// NewExceptTokenRule creates a rule that matches any token *except* for a specific type.
func NewExceptTokenRule[T lexshared.TokenTypeConstraint](symbol string, excludedType T) shared.ParsingRuleInterface[T] {
	return &ExceptTokenRule[T]{
		BaseParsingRule: internal.BaseParsingRule[T]{SymbolString: symbol},
		excludedType:    excludedType,
	}
}

// ExceptTokenRule matches any single token not of the excluded type.
type ExceptTokenRule[T lexshared.TokenTypeConstraint] struct {
	internal.BaseParsingRule[T]
	excludedType T
}

func (r *ExceptTokenRule[T]) Match(tokens []*lexshared.Token[T], index int) (*parseshared.ParseTree[T], error, int) {
	if index >= len(tokens) {
		return nil, fmt.Errorf("not enough tokens for %s", r.SymbolString), 0
	}

	if token := tokens[index]; token.Type != r.excludedType {
		tree := &parseshared.ParseTree[T]{
			Symbol: r.Symbol(),
			Token:  token,
		}
		return tree, nil, 1 // Consumes 1 token
	}

	return nil, fmt.Errorf("token matched excluded type %v for rule %s", r.excludedType, r.Symbol()), 0
}
