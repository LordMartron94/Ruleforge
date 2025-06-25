package util

import (
	"errors"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/lexing/scanning"
)

// SingleRuneScanner is a stub scanner for checking a single rune boundary.
type SingleRuneScanner struct{ R rune }

func (s *SingleRuneScanner) Current() rune              { return s.R }
func (s *SingleRuneScanner) Peek(_ int) ([]rune, error) { return nil, errors.New("no more") }
func (s *SingleRuneScanner) Position() int              { return 0 }

// ScanWhile continuously peeks from the scanner and collects runes
// as long as the predicate function returns true. It assumes the first
// character has already been matched.
func ScanWhile(scanner scanning.PeekInterface, predicate func(rune) bool) []rune {
	runes := []rune{scanner.Current()}
	peekIndex := 1

	for {
		pRunes, err := scanner.Peek(peekIndex)
		if err != nil {
			break
		}

		peekedRune := pRunes[len(pRunes)-1]
		if predicate(peekedRune) {
			runes = append(runes, peekedRune)
			peekIndex++
		} else {
			break
		}
	}
	return runes
}
