package factory

import (
	"bytes"

	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/lexing/scanning"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/lexing/shared"
)

type baseLexingRule[T shared.TokenTypeConstraint] struct {
	buffer *bytes.Buffer

	AssociatedToken T
	SymbolString    string
	MatchFunc       func(scanner scanning.PeekInterface) bool
	GetContentFunc  func(scanner scanning.PeekInterface) []rune
}

func (b *baseLexingRule[T]) Symbol() string {
	return b.SymbolString
}

func (b *baseLexingRule[T]) IsMatch(peeker scanning.PeekInterface) bool {
	return b.MatchFunc(peeker)
}

func (b *baseLexingRule[T]) ExtractToken(scanner scanning.PeekInterface) (*shared.Token[T], error, int) {
	runes := b.GetContentFunc(scanner)
	runeBytes := []byte(string(runes))

	t := &shared.Token[T]{
		Type:  b.AssociatedToken,
		Value: runeBytes,
	}

	return t, nil, len(runes)
}
