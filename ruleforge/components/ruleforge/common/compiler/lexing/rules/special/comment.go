package special

import (
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/lexing/rules"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/lexing/scanning"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/lexing/shared"
)

// LineCommentLexingRule creates a rule that skips everything from the given prefix up to (but not including) the next newline.
type LineCommentLexingRule[T shared.TokenTypeConstraint] struct {
	rules.BaseLexingRule[T]
	prefix []rune
}

// NewLineCommentLexingRule creates a rule that skips everything from the given prefix up to (but not including) the next newline.
func NewLineCommentLexingRule[T shared.TokenTypeConstraint](symbol string, associatedToken T, prefix string) rules.LexingRuleInterface[T] {
	rule := &LineCommentLexingRule[T]{
		prefix: []rune(prefix),
	}
	rule.SymbolString = symbol
	rule.AssociatedToken = associatedToken
	rule.MatchFunc = rule.isMatch
	rule.GetContentFunc = rule.getContent
	return rule
}

func (nlcr *LineCommentLexingRule[T]) isMatch(scanner scanning.PeekInterface) bool {
	if scanner.Current() != nlcr.prefix[0] {
		return false
	}
	peeked, err := scanner.Peek(len(nlcr.prefix) - 1)
	if err != nil {
		return false
	}
	for i, r := range peeked {
		if r != nlcr.prefix[i+1] {
			return false
		}
	}
	return true
}

func (nlcr *LineCommentLexingRule[T]) getContent(scanner scanning.PeekInterface) []rune {
	var runes []rune

	// Capture prefix
	runes = append(runes, nlcr.prefix...)

	// Consume until newline or carriage return
	offset := len(nlcr.prefix)
	for {
		peeked, err := scanner.Peek(offset)
		if err != nil {
			break
		}
		nextRune := peeked[len(peeked)-1]
		if nextRune == '\n' || nextRune == '\r' {
			break
		}
		runes = append(runes, nextRune)
		offset++
	}
	return runes
}

// DelimitedContentLexingRule creates a rule that matches everything between a start and end delimiter.
type DelimitedContentLexingRule[T shared.TokenTypeConstraint] struct {
	rules.BaseLexingRule[T]
	startDelimiter []rune
	endDelimiter   []rune
}

// NewDelimitedContentLexingRule creates a lexing rule for content enclosed by start and end delimiters.
func NewDelimitedContentLexingRule[T shared.TokenTypeConstraint](symbol string, associatedToken T, startDelimiter, endDelimiter string) rules.LexingRuleInterface[T] {
	rule := &DelimitedContentLexingRule[T]{
		startDelimiter: []rune(startDelimiter),
		endDelimiter:   []rune(endDelimiter),
	}
	rule.SymbolString = symbol
	rule.AssociatedToken = associatedToken
	rule.MatchFunc = rule.isMatch
	rule.GetContentFunc = rule.getContent
	return rule
}

func (d *DelimitedContentLexingRule[T]) isMatch(scanner scanning.PeekInterface) bool {
	// Check if the current input starts with the start delimiter
	if scanner.Current() != d.startDelimiter[0] {
		return false
	}
	peeked, err := scanner.Peek(len(d.startDelimiter) - 1)
	if err != nil {
		return false
	}
	for i, r := range peeked {
		if r != d.startDelimiter[i+1] {
			return false
		}
	}
	return true
}

func (d *DelimitedContentLexingRule[T]) getContent(scanner scanning.PeekInterface) []rune {
	var runes []rune

	// Capture the start delimiter
	runes = append(runes, d.startDelimiter...)

	// Consume until the end delimiter is found
	offset := len(d.startDelimiter)
	for {
		// Check for end delimiter at the current offset
		endDelimiterFound := true
		for i, r := range d.endDelimiter {
			peeked, err := scanner.Peek(offset + i)
			if err != nil {
				// Reached end of input before finding end delimiter
				return runes
			}
			if peeked[len(peeked)-1] != r {
				endDelimiterFound = false
				break
			}
		}

		if endDelimiterFound {
			// Found the end delimiter, capture it and break
			runes = append(runes, d.endDelimiter...)
			break
		}

		// If end delimiter not found, consume one more character
		peeked, err := scanner.Peek(offset)
		if err != nil {
			// Reached end of input without finding end delimiter
			break
		}
		runes = append(runes, peeked[len(peeked)-1])
		offset++
	}
	return runes
}
