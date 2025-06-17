package factory

import (
	"fmt"

	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/lexing/shared"
	shared2 "github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/parsing/shared"
)

type BaseParsingRule[T shared.TokenTypeConstraint] struct {
	SymbolString string

	matchFunc      func(tokens []*shared.Token[T], currentIndex int) (bool, string)
	getContentFunc func(tokens []*shared.Token[T], currentIndex int) *shared2.ParseTree[T]
	consumeExtra   int
}

func (b *BaseParsingRule[T]) Symbol() string {
	return b.SymbolString
}

func (b *BaseParsingRule[T]) Match(tokens []*shared.Token[T], currentIndex int) (*shared2.ParseTree[T], error, int) {
	matched, errorMessage := b.matchFunc(tokens, currentIndex)

	if !matched {
		return nil, fmt.Errorf(errorMessage), 0
	}

	tree := b.getContentFunc(tokens, currentIndex)

	if tree == nil {
		panic("getContentFunc returned nil for rule " + b.SymbolString)
	}

	return tree, nil, tree.GetNumberOfTokens() + b.consumeExtra
}
