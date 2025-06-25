package atomic

import (
	"fmt"
	lexshared "github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/lexing/shared"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/parsing/rules/internal"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/parsing/rules/shared"
	parseshared "github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/parsing/shared"
)

// NewSingleTokenRule creates a rule that matches a single token of a specific type.
func NewSingleTokenRule[T lexshared.TokenTypeConstraint](symbol string, tokenType T) shared.ParsingRuleInterface[T] {
	return &SingleTokenRule[T]{
		BaseParsingRule: internal.BaseParsingRule[T]{SymbolString: symbol},
		tokenType:       tokenType,
	}
}

// SingleTokenRule matches one token of a specific type.
type SingleTokenRule[T lexshared.TokenTypeConstraint] struct {
	internal.BaseParsingRule[T]
	tokenType T
}

func (r *SingleTokenRule[T]) Match(tokens []*lexshared.Token[T], index int) (*parseshared.ParseTree[T], error, int) {
	if index >= len(tokens) {
		return nil, fmt.Errorf("not enough tokens for %s, expected %v", r.Symbol(), r.tokenType), 0
	}

	if token := tokens[index]; token.Type == r.tokenType {
		tree := &parseshared.ParseTree[T]{
			Symbol: r.SymbolString,
			Token:  token,
		}
		return tree, nil, 1
	}

	return nil, fmt.Errorf("token mismatch for %s: expected %v, got %v", r.Symbol(), r.tokenType, tokens[index].Type), 0
}
