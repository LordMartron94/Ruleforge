package lexing

import "github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/lexing/shared"

type LexerInterface[T shared.TokenTypeConstraint] interface {
	// GetToken returns the next token from the input stream.
	GetToken() *shared.Token[T]

	// GetTokens returns all tokens from the input stream.
	GetTokens() ([]*shared.Token[T], error)

	// Reset resets the lexer to the beginning of the input stream.
	Reset()
}
