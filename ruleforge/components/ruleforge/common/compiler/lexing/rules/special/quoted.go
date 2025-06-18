package special

import (
	"fmt"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/lexing/internal/util"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/lexing/rules"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/lexing/scanning"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/lexing/shared"
)

// NewQuotedValueRule creates a rule that recognizes identifiers wrapped in double-quotes.
func NewQuotedValueRule[T shared.TokenTypeConstraint](
	symbol string,
	tokenType T,
	includeQuotes bool,
	isValidCharacterRule rules.LexingRuleInterface[T],
) rules.LexingRuleInterface[T] {
	return &QuotedValueRule[T]{
		SymbolString:         symbol,
		TokenType:            tokenType,
		IncludeQuotes:        includeQuotes,
		IsValidCharacterRule: isValidCharacterRule,
	}
}

// QuotedValueRule implements a rule for matching values enclosed in double quotes.
type QuotedValueRule[T shared.TokenTypeConstraint] struct {
	SymbolString         string
	TokenType            T
	IncludeQuotes        bool
	IsValidCharacterRule rules.LexingRuleInterface[T]
}

func (q *QuotedValueRule[T]) Symbol() string {
	return q.SymbolString
}

// IsMatch now performs a fast check on only the first character.
// The lexer calls this to quickly determine if this rule applies.
func (q *QuotedValueRule[T]) IsMatch(scanner scanning.PeekInterface) bool {
	return scanner.Current() == '"'
}

// ExtractToken performs the full scan for the quoted string.
// It's only called by the lexer after IsMatch returns true.
func (q *QuotedValueRule[T]) ExtractToken(
	scanner scanning.PeekInterface,
) (*shared.Token[T], error, int) {
	runes, err := q.scanQuoted(scanner)
	if err != nil {
		return &shared.Token[T]{}, err, 0
	}

	// Decide what text to emit based on the IncludeQuotes flag.
	var emit []rune
	if q.IncludeQuotes || len(runes) < 2 {
		emit = runes
	} else {
		// Strip the opening and closing quotes.
		emit = runes[1 : len(runes)-1]
	}

	// Build and return the token.
	tok := shared.Token[T]{
		Type:  q.TokenType,
		Value: []byte(string(emit)),
	}
	return &tok, nil, len(runes)
}

// scanQuoted handles the logic of scanning through the string, handling escape characters.
func (q *QuotedValueRule[T]) scanQuoted(scanner scanning.PeekInterface) ([]rune, error) {
	content := []rune{scanner.Current()}
	peekIndex := 1
	isEscaped := false

	for {
		peekedRunes, err := scanner.Peek(peekIndex)
		if err != nil {
			return nil, fmt.Errorf("unterminated quoted value starting at position %d", scanner.Position())
		}

		ch := peekedRunes[len(peekedRunes)-1]
		content = append(content, ch)
		peekIndex++

		if isEscaped {
			isEscaped = false
			continue
		}

		if ch == '\\' {
			isEscaped = true
			continue
		}

		if ch == '"' {
			break
		}

		// For normal characters, validate them using the provided rule.
		stub := &util.SingleRuneScanner{R: ch}
		if !q.IsValidCharacterRule.IsMatch(stub) {
			return nil, fmt.Errorf("invalid character %q in quoted value", ch)
		}
	}
	return content, nil
}
