package conditional

import (
	"fmt"
	lexshared "github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/lexing/shared"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/parsing/rules/internal"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/parsing/rules/shared"
	parseshared "github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/parsing/shared"
)

// NewAnyTokenRule creates a rule that matches any single token.
// This is useful as a default or fallback rule.
func NewAnyTokenRule[T lexshared.TokenTypeConstraint](symbol string) shared.ParsingRuleInterface[T] {
	return &AnyTokenRule[T]{
		BaseParsingRule: internal.BaseParsingRule[T]{SymbolString: symbol},
	}
}

// AnyTokenRule implements a rule that matches any single token.
type AnyTokenRule[T lexshared.TokenTypeConstraint] struct {
	internal.BaseParsingRule[T]
}

func (r *AnyTokenRule[T]) Match(tokens []*lexshared.Token[T], index int) (*parseshared.ParseTree[T], error, int) {
	// This rule always succeeds as long as there is a token to consume.
	if index >= len(tokens) {
		return nil, fmt.Errorf("no tokens left to match for %s", r.Symbol()), 0
	}

	tree := &parseshared.ParseTree[T]{
		Symbol: r.SymbolString,
		Token:  tokens[index],
	}
	return tree, nil, 1 // Consumes 1 token
}
