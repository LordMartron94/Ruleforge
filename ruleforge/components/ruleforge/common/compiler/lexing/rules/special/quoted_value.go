package special

import (
	"errors"
	"fmt"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/lexing/rules"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/lexing/scanning"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/lexing/shared"
)

type singleRuneScanner struct{ r rune }

func (s *singleRuneScanner) Current() rune              { return s.r }
func (s *singleRuneScanner) Peek(_ int) ([]rune, error) { return nil, errors.New("no more") }

// QuotedValueRule QuotedValueRule[T] implements rules.LexingRuleInterface[T].
type QuotedValueRule[T shared.TokenTypeConstraint] struct {
	SymbolString         string
	TokenType            T
	IncludeQuotes        bool
	IsValidCharacterRule rules.LexingRuleInterface[T]
}

func (q *QuotedValueRule[T]) Symbol() string {
	return q.SymbolString
}

func (q *QuotedValueRule[T]) scanQuoted(scanner scanning.PeekInterface) ([]rune, error) {
	all := []rune{scanner.Current()} // opening quote
	peek := 1
	escaped := false

	for {
		runes, err := scanner.Peek(peek)
		if err != nil {
			return nil, fmt.Errorf("unterminated string")
		}
		ch := runes[len(runes)-1]
		all = append(all, ch)
		peek++

		if escaped {
			// this char was escaped, treat literally and reset
			escaped = false
			continue
		}
		if ch == '\\' {
			// start escape sequence; next char will be taken literally
			escaped = true
			continue
		}
		if ch == '"' {
			// end of string
			break
		}
		// normal rune—validate it
		stub := &singleRuneScanner{r: ch}
		if !q.IsValidCharacterRule.IsMatch(stub) {
			return nil, fmt.Errorf("invalid character %q in string", ch)
		}
	}
	return all, nil
}

func (q *QuotedValueRule[T]) IsMatch(scanner scanning.PeekInterface) bool {
	if scanner.Current() != '"' {
		return false
	}
	_, err := q.scanQuoted(scanner)
	return err == nil
}

func (q *QuotedValueRule[T]) ExtractToken(
	scanner scanning.PeekInterface,
) (*shared.Token[T], error, int) {
	all, err := q.scanQuoted(scanner)
	if err != nil {
		return nil, err, 0
	}

	// 2) Decide what text to emit
	var emit []rune
	if q.IncludeQuotes || len(all) < 2 {
		emit = all
	} else {
		emit = all[1 : len(all)-1]
	}

	// 3) Build the token
	tok := &shared.Token[T]{
		Type:  q.TokenType,
		Value: []byte(string(emit)),
	}

	// 4) Return (token, no-error, number-of-runes-to-consume)
	return tok, nil, len(all)
}
