package special

import (
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/lexing/internal/util"

	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/lexing/rules"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/lexing/scanning"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/lexing/shared"
)

// NewUnquotedValueRule creates a rule that recognizes unquoted identifiers.
func NewUnquotedValueRule[T shared.TokenTypeConstraint](
	symbol string,
	tokenType T,
	isValidCharacterRule rules.LexingRuleInterface[T],
	mustStartWith *rune,
) rules.LexingRuleInterface[T] {
	return &UnquotedValueRule[T]{
		SymbolString:         symbol,
		TokenType:            tokenType,
		IsValidCharacterRule: isValidCharacterRule,
		MustStartWith:        mustStartWith,
	}
}

// UnquotedValueRule scans a run of valid characters without requiring surrounding quotes.
type UnquotedValueRule[T shared.TokenTypeConstraint] struct {
	SymbolString         string
	TokenType            T
	IsValidCharacterRule rules.LexingRuleInterface[T]
	MustStartWith        *rune
}

func (u *UnquotedValueRule[T]) Symbol() string {
	return u.SymbolString
}

// IsMatch checks if the current character is a valid start for this unquoted value.
func (u *UnquotedValueRule[T]) IsMatch(scanner scanning.PeekInterface) bool {
	current := scanner.Current()

	// If a specific starting character is required, check for it.
	if u.MustStartWith != nil {
		return current == *u.MustStartWith
	}

	// Otherwise, check if the character is valid according to the general rule.
	stub := &util.SingleRuneScanner{R: current}
	return u.IsValidCharacterRule.IsMatch(stub)
}

// ExtractToken consumes all consecutive valid characters to form the token.
func (u *UnquotedValueRule[T]) ExtractToken(
	scanner scanning.PeekInterface,
) (*shared.Token[T], error, int) {
	// The lexer already called IsMatch, so we start consuming immediately.
	// This is essentially the `scanWhile` pattern.
	content := []rune{scanner.Current()}
	peekIndex := 1

	for {
		peekedRunes, err := scanner.Peek(peekIndex)
		if err != nil {
			break // End of input.
		}

		ch := peekedRunes[len(peekedRunes)-1]
		stub := &util.SingleRuneScanner{R: ch}

		if !u.IsValidCharacterRule.IsMatch(stub) {
			break // The sequence of valid characters has ended.
		}

		content = append(content, ch)
		peekIndex++
	}

	// Build and return the token
	tok := shared.Token[T]{
		Type:  u.TokenType,
		Value: []byte(string(content)),
	}
	return &tok, nil, len(content)
}
