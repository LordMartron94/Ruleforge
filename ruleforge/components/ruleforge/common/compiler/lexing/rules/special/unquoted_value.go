package special

import (
	"fmt"

	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/lexing/rules"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/lexing/scanning"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/lexing/shared"
)

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

func (u *UnquotedValueRule[T]) IsMatch(scanner scanning.PeekInterface) bool {
	current := scanner.Current()

	if u.MustStartWith != nil && current == *u.MustStartWith {
		return true
	} else if u.MustStartWith != nil && current != *u.MustStartWith {
		return false
	}

	stub := &singleRuneScanner{r: current}

	return u.IsValidCharacterRule.IsMatch(stub)
}

func (u *UnquotedValueRule[T]) ExtractToken(
	scanner scanning.PeekInterface,
) (*shared.Token[T], error, int) {
	// Ensure there's at least one valid character to start
	if !u.IsMatch(scanner) {
		return nil, fmt.Errorf("no valid unquoted token start"), 0
	}

	// Consume runes while they satisfy the validity rule
	all := []rune{scanner.Current()}

	peek := 1
	for {
		runes, err := scanner.Peek(peek)
		if err != nil {
			// End of input
			break
		}
		ch := runes[len(runes)-1]
		stub := &singleRuneScanner{r: ch}
		if !u.IsValidCharacterRule.IsMatch(stub) {
			// Reached a delimiter or invalid character
			break
		}
		all = append(all, ch)
		peek++
	}

	// Build and return the token
	tok := &shared.Token[T]{
		Type:  u.TokenType,
		Value: []byte(string(all)),
	}
	return tok, nil, len(all)
}
