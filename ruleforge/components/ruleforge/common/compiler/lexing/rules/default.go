package rules

import (
	"bytes"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/lexing/scanning"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/lexing/shared"
)

type BaseLexingRule[T shared.TokenTypeConstraint] struct {
	buffer *bytes.Buffer

	AssociatedToken T
	SymbolString    string
	MatchFunc       func(scanner scanning.PeekInterface) bool
	GetContentFunc  func(scanner scanning.PeekInterface) []rune
}

func (b *BaseLexingRule[T]) Symbol() string {
	return b.SymbolString
}

func (b *BaseLexingRule[T]) IsMatch(peeker scanning.PeekInterface) bool {
	return b.MatchFunc(peeker)
}

func (b *BaseLexingRule[T]) ExtractToken(scanner scanning.PeekInterface) (*shared.Token[T], error, int) {
	runes := b.GetContentFunc(scanner)
	runeBytes := []byte(string(runes))

	t := &shared.Token[T]{
		Type:  b.AssociatedToken,
		Value: runeBytes,
	}

	return t, nil, len(runes)
}

// ================== MATCH ANY ====================

// NewMatchAnyTokenRule creates a default lexing rule that always matches one character.
// This is typically used as a fallback for unrecognized characters.
func NewMatchAnyTokenRule[T shared.TokenTypeConstraint](unknownToken T) LexingRuleInterface[T] {
	return &BaseLexingRule[T]{
		SymbolString:    "UnknownTokenRuleLexer",
		MatchFunc:       func(scanning.PeekInterface) bool { return true },
		AssociatedToken: unknownToken,
		GetContentFunc: func(scanner scanning.PeekInterface) []rune {
			return []rune{scanner.Current()}
		},
	}
}
