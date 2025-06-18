package composite

import (
	lexshared "github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/lexing/shared"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/parsing/rules/internal"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/parsing/rules/shared"
	parseshared "github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/parsing/shared"
)

// NewOptionalRule creates a rule that attempts to match its child rule.
// If the child rule matches, its parse tree is included and its consumed tokens are reported.
// If the child rule does not match, no error is returned, and 0 tokens are consumed.
func NewOptionalRule[T lexshared.TokenTypeConstraint](
	symbol string,
	childRule shared.ParsingRuleInterface[T],
) shared.ParsingRuleInterface[T] {
	return &OptionalRule[T]{
		BaseParsingRule: internal.BaseParsingRule[T]{SymbolString: symbol},
		childRule:       childRule,
	}
}

// OptionalRule attempts to match a child rule.
// If the child rule matches, its result is returned.
// If the child rule fails to match, it does not produce an error and consumes 0 tokens.
type OptionalRule[T lexshared.TokenTypeConstraint] struct {
	internal.BaseParsingRule[T]
	childRule shared.ParsingRuleInterface[T]
}

func (r *OptionalRule[T]) Match(tokens []*lexshared.Token[T], index int) (*parseshared.ParseTree[T], error, int) {
	// Attempt to match the child rule.
	childTree, err, consumed := r.childRule.Match(tokens, index)

	if err != nil {
		// If the child rule failed to match, it's not an error for an OptionalRule.
		// We return a nil tree, no error, and 0 consumed tokens.
		return nil, nil, 0
	}

	// If the child rule matched successfully, return its result.
	// Wrap it in a parse tree for this OptionalRule's symbol.
	tree := &parseshared.ParseTree[T]{
		Symbol:   r.Symbol(),
		Children: []*parseshared.ParseTree[T]{childTree},
	}
	return tree, nil, consumed
}
