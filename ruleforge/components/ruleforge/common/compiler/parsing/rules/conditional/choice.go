package conditional

import (
	"fmt"
	lexshared "github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/lexing/shared"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/parsing/rules/internal"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/parsing/rules/shared"
	parseshared "github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/parsing/shared"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/extensions"
)

// NewChoiceTokenRule creates a rule that matches a single token in the specified types.
func NewChoiceTokenRule[T lexshared.TokenTypeConstraint](symbol string, tokenTypes []T) shared.ParsingRuleInterface[T] {
	return &ChoiceTokenRule[T]{
		BaseParsingRule: internal.BaseParsingRule[T]{SymbolString: symbol},
		tokenTypes:      tokenTypes,
	}
}

// ChoiceTokenRule matches one token of a specific type.
type ChoiceTokenRule[T lexshared.TokenTypeConstraint] struct {
	internal.BaseParsingRule[T]
	tokenTypes []T
}

func (r *ChoiceTokenRule[T]) Match(tokens []*lexshared.Token[T], index int) (*parseshared.ParseTree[T], error, int) {
	listFormatted := extensions.GetFormattedString(r.tokenTypes)

	if token := tokens[index]; extensions.Contains(r.tokenTypes, token.Type) {
		tree := &parseshared.ParseTree[T]{
			Symbol: r.SymbolString,
			Token:  token,
		}
		return tree, nil, 1
	}

	return nil, fmt.Errorf("token mismatch for %s: expected %v, got %v", r.Symbol(), listFormatted, tokens[index].Type), 0
}
