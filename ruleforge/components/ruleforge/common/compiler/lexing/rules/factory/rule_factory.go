package factory

import (
	"errors"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/lexing/rules/special"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/lexing/shared"
	"slices"

	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/lexing/rules"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/lexing/scanning"
)

// RuleFactory provides an API for creating lexing rules.
type RuleFactory[T shared.TokenTypeConstraint] struct {
}

// NewLexingRule creates a new lexing rule with the given symbol, match function, and associated token.
func (r *RuleFactory[T]) NewLexingRule(symbol string, isMatchFunc func(scanning.PeekInterface) bool, associatedToken T, getContentFunc func(peekInterface scanning.PeekInterface) []rune) rules.LexingRuleInterface[T] {
	rule := &baseLexingRule[T]{
		SymbolString:    symbol,
		MatchFunc:       isMatchFunc,
		AssociatedToken: associatedToken,
		GetContentFunc:  getContentFunc,
	}

	return rule
}

// NewMatchAnyTokenRule creates a new default lexing rule with the given associated token.
func (r *RuleFactory[T]) NewMatchAnyTokenRule(unknownToken T) rules.LexingRuleInterface[T] {
	return &baseLexingRule[T]{
		SymbolString:    "UnknownTokenRuleLexer",
		MatchFunc:       func(scanning.PeekInterface) bool { return true },
		AssociatedToken: unknownToken,
		GetContentFunc: func(scanner scanning.PeekInterface) []rune {
			return []rune{scanner.Current()}
		},
	}
}

func (r *RuleFactory[T]) NewIgnoreTokenLexingRule(symbol string, associatedToken T, matchFunc func(scanning.PeekInterface) bool) rules.LexingRuleInterface[T] {
	return &baseLexingRule[T]{
		SymbolString:    symbol,
		MatchFunc:       matchFunc,
		AssociatedToken: associatedToken,
		GetContentFunc: func(scanner scanning.PeekInterface) []rune {
			return []rune("IGNORE")
		},
	}
}

type singleRuneScanner struct{ r rune }

func (s *singleRuneScanner) Current() rune              { return s.r }
func (s *singleRuneScanner) Peek(_ int) ([]rune, error) { return nil, errors.New("no more") }

func (r *RuleFactory[T]) NewKeywordLexingRule(
	keyword string,
	associatedToken T,
	symbol string,
	identifierRule rules.LexingRuleInterface[T],
) rules.LexingRuleInterface[T] {
	return &baseLexingRule[T]{
		SymbolString: symbol,
		MatchFunc: func(scanner scanning.PeekInterface) bool {
			runesInKeyword := []rune(keyword)

			// 1) match the keyword’s runes
			if scanner.Current() != runesInKeyword[0] {
				return false
			}
			peeked, err := scanner.Peek(len(runesInKeyword) - 1)
			if err != nil {
				return false
			}
			for i, r := range peeked {
				if r != runesInKeyword[i+1] {
					return false
				}
			}

			// 2) peek *one more* rune to enforce boundary:
			if nextRunes, err2 := scanner.Peek(len(runesInKeyword)); err2 == nil {
				next := nextRunes[len(nextRunes)-1]
				stub := &singleRuneScanner{r: next}
				// if that rune *would* start an identifier, cancel this as a keyword
				if identifierRule.IsMatch(stub) {
					return false
				}
			}

			return true
		},
		AssociatedToken: associatedToken,
		GetContentFunc: func(scanner scanning.PeekInterface) []rune {
			return []rune(keyword)
		},
	}
}

func (r *RuleFactory[T]) NewSpecificCharacterLexingRule(character rune, associatedToken T, symbol string) rules.LexingRuleInterface[T] {
	return &baseLexingRule[T]{
		SymbolString: symbol,
		MatchFunc: func(scanner scanning.PeekInterface) bool {
			currentRune := scanner.Current()

			if currentRune != character {
				return false
			}

			return true
		},
		AssociatedToken: associatedToken,
		GetContentFunc: func(scanner scanning.PeekInterface) []rune {
			return []rune{character}
		},
	}
}

func (r *RuleFactory[T]) NewCharacterOptionLexingRule(characters []rune, associatedToken T, symbol string) rules.LexingRuleInterface[T] {
	return &baseLexingRule[T]{
		SymbolString: symbol,
		MatchFunc: func(scanner scanning.PeekInterface) bool {
			currentRune := scanner.Current()

			if !slices.Contains(characters, currentRune) {
				return false
			}

			return true
		},
		AssociatedToken: associatedToken,
		GetContentFunc: func(scanner scanning.PeekInterface) []rune {
			return []rune{scanner.Current()}
		},
	}
}

func (r *RuleFactory[T]) NewNumberLexingRule(associatedToken T, symbol string) rules.LexingRuleInterface[T] {
	vFunc := func(r rune) bool {
		return '0' <= r && r <= '9'
	}
	mFunc := func(scanner scanning.PeekInterface) bool {
		currentRune := scanner.Current()

		if !vFunc(currentRune) {
			return false
		}

		return true
	}

	return &baseLexingRule[T]{
		SymbolString:    symbol,
		MatchFunc:       mFunc,
		AssociatedToken: associatedToken,
		GetContentFunc: func(scanner scanning.PeekInterface) []rune {
			runes := make([]rune, 0)
			runes = append(runes, scanner.Current())

			peekIndex := 1
			for {
				pRunes, err := scanner.Peek(peekIndex)

				if err != nil {
					break
				}

				peekedRune := pRunes[len(pRunes)-1]

				if vFunc(peekedRune) {
					runes = append(runes, peekedRune)
					peekIndex++
				} else {
					break
				}
			}

			return runes
		},
	}
}

func (r *RuleFactory[T]) NewWhitespaceLexingRule(tokenType T, symbol string) rules.LexingRuleInterface[T] {
	vFunc := func(r rune) bool {
		return r == '\t' || r == '\n' || r == '\r' || r == ' ' || r == '\f' || r == '\v'
	}
	mFunc := func(scanner scanning.PeekInterface) bool {
		currentRune := scanner.Current()

		if !vFunc(currentRune) {
			return false
		}

		return true
	}

	return &baseLexingRule[T]{
		SymbolString:    symbol,
		MatchFunc:       mFunc,
		AssociatedToken: tokenType,
		GetContentFunc: func(scanner scanning.PeekInterface) []rune {
			runes := make([]rune, 0)
			runes = append(runes, scanner.Current())

			peekIndex := 1
			for {
				pRunes, err := scanner.Peek(peekIndex)

				if err != nil {
					break
				}

				peekedRune := pRunes[len(pRunes)-1]

				if vFunc(peekedRune) {
					runes = append(runes, peekedRune)
					peekIndex++
				} else {
					break
				}
			}

			return runes
		},
	}
}

// NewAlphaNumericRuleSingle creates a new lexing rule for a single letter,
// optionally allowing digits as well.
func (r *RuleFactory[T]) NewAlphaNumericRuleSingle(
	tokenType T,
	symbol string,
	includeDigits bool,
) rules.LexingRuleInterface[T] {
	isLetter := func(ch rune) bool {
		return ('a' <= ch && ch <= 'z') || ('A' <= ch && ch <= 'Z')
	}
	isDigit := func(ch rune) bool {
		return '0' <= ch && ch <= '9'
	}

	// Validates based on includeDigits flag
	validRune := func(ch rune) bool {
		if isLetter(ch) {
			return true
		}
		if includeDigits && isDigit(ch) {
			return true
		}
		return false
	}

	matchFunc := func(scanner scanning.PeekInterface) bool {
		return validRune(scanner.Current())
	}

	getContent := func(scanner scanning.PeekInterface) []rune {
		return []rune{scanner.Current()}
	}

	return &baseLexingRule[T]{
		SymbolString:    symbol,
		MatchFunc:       matchFunc,
		AssociatedToken: tokenType,
		GetContentFunc:  getContent,
	}
}

// NewOrLexingRule creates a lexing rule that succeeds
// if any of the given sub-rules match.  It emits the
// single tokenType for all alternatives.
func (r *RuleFactory[T]) NewOrLexingRule(
	tokenType T,
	symbol string,
	subRules ...rules.LexingRuleInterface[T],
) rules.LexingRuleInterface[T] {
	// matchFunc succeeds as soon as any sub-rule matches
	matchFunc := func(scanner scanning.PeekInterface) bool {
		for _, rule := range subRules {
			if rule.IsMatch(scanner) {
				return true
			}
		}
		return false
	}

	// getContentFunc delegates to the first matching sub-rule
	getContentFunc := func(scanner scanning.PeekInterface) []rune {
		for _, rule := range subRules {
			if rule.IsMatch(scanner) {
				token, _, _ := rule.ExtractToken(scanner)
				return token.ValueToRunes()
			}
		}
		return nil
	}

	return &baseLexingRule[T]{
		SymbolString:    symbol,
		MatchFunc:       matchFunc,
		GetContentFunc:  getContentFunc,
		AssociatedToken: tokenType,
	}
}

// NewQuotedIdentifierLexingRule creates a lexing rule that only
// recognizes identifiers wrapped in double-quotes, and optionally
// strips those quotes from the returned content.
func (r *RuleFactory[T]) NewQuotedIdentifierLexingRule(
	associatedToken T,
	symbol string,
	includeQuotes bool,
	isValidCharacterRule rules.LexingRuleInterface[T],
) rules.LexingRuleInterface[T] {
	return &special.QuotedValueRule[T]{
		SymbolString:         symbol,
		TokenType:            associatedToken,
		IncludeQuotes:        includeQuotes,
		IsValidCharacterRule: isValidCharacterRule,
	}
}

// NewUnquotedIdentifierLexingRule creates a lexing rule that
// recognizes identifiers.
func (r *RuleFactory[T]) NewUnquotedIdentifierLexingRule(
	associatedToken T,
	symbol string,
	isValidCharacterRule rules.LexingRuleInterface[T],
) rules.LexingRuleInterface[T] {
	return &special.UnquotedValueRule[T]{
		SymbolString:         symbol,
		TokenType:            associatedToken,
		IsValidCharacterRule: isValidCharacterRule,
	}
}
