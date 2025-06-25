package atomic

import (
	"fmt"
	lexshared "github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/lexing/shared"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/parsing/rules/internal"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/parsing/rules/shared"
	parseshared "github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/parsing/shared"
)

// NewSequenceRule creates a rule that matches a fixed sequence of token types.
func NewSequenceRule[T lexshared.TokenTypeConstraint](symbol string, sequence []T, childSymbols []string) shared.ParsingRuleInterface[T] {
	if len(sequence) != len(childSymbols) {
		panic("sequence and childSymbols must have the same length")
	}
	return &SequenceRule[T]{
		BaseParsingRule: internal.BaseParsingRule[T]{SymbolString: symbol},
		sequence:        sequence,
		childSymbols:    childSymbols,
	}
}

// SequenceRule matches a fixed sequence of token types.
type SequenceRule[T lexshared.TokenTypeConstraint] struct {
	internal.BaseParsingRule[T]
	sequence     []T
	childSymbols []string
}

func (r *SequenceRule[T]) Match(tokens []*lexshared.Token[T], index int) (*parseshared.ParseTree[T], error, int) {
	if index+len(r.sequence) > len(tokens) {
		return nil, fmt.Errorf("not enough tokens for sequence %s", r.Symbol()), 0
	}

	children := make([]*parseshared.ParseTree[T], len(r.sequence))
	for i, expectedType := range r.sequence {
		token := tokens[index+i]
		if token.Type != expectedType {
			return nil, fmt.Errorf("token mismatch in sequence %s at pos %d: expected %v, got %v", r.Symbol(), i, expectedType, token.Type), 0
		}
		children[i] = &parseshared.ParseTree[T]{
			Symbol: r.childSymbols[i],
			Token:  token,
		}
	}

	tree := &parseshared.ParseTree[T]{
		Symbol:   r.SymbolString,
		Children: children,
	}
	return tree, nil, len(r.sequence) // Consumes N tokens
}
