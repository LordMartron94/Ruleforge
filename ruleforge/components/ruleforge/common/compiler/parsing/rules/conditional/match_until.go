package conditional

import (
	"fmt"
	lexshared "github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/lexing/shared"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/parsing/rules/internal"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/parsing/rules/shared"
	parseshared "github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/parsing/shared"
)

// NewMatchUntilRule creates a rule that consumes tokens until a terminator token is found.
func NewMatchUntilRule[T lexshared.TokenTypeConstraint](symbol, childSymbol string, terminator T) shared.ParsingRuleInterface[T] {
	return &MatchUntilRule[T]{
		BaseParsingRule: internal.BaseParsingRule[T]{SymbolString: symbol},
		childSymbol:     childSymbol,
		terminator:      terminator,
	}
}

// MatchUntilRule consumes tokens and creates child nodes until it finds a terminator.
type MatchUntilRule[T lexshared.TokenTypeConstraint] struct {
	internal.BaseParsingRule[T]
	childSymbol string
	terminator  T
}

func (r *MatchUntilRule[T]) Match(tokens []*lexshared.Token[T], index int) (*parseshared.ParseTree[T], error, int) {
	children := make([]*parseshared.ParseTree[T], 0)
	currentIndex := index

	for currentIndex < len(tokens) {
		token := tokens[currentIndex]
		if token.Type == r.terminator {
			break // Found the end of the sequence.
		}
		childNode := &parseshared.ParseTree[T]{
			Symbol: r.childSymbol,
			Token:  token,
		}
		children = append(children, childNode)
		currentIndex++
	}

	if len(children) == 0 {
		return nil, fmt.Errorf("rule %s expected at least one token before terminator %v", r.SymbolString, r.terminator), 0
	}

	tree := &parseshared.ParseTree[T]{
		Symbol:   r.Symbol(),
		Children: children,
	}
	return tree, nil, len(children)
}

// NewTokenSetRepetitionRule creates a rule that consumes a sequence of tokens as long as they belong to an allowed set.
func NewTokenSetRepetitionRule[T lexshared.TokenTypeConstraint](symbol string, allowedTypes []T, childSymbols []string) shared.ParsingRuleInterface[T] {
	if len(allowedTypes) != len(childSymbols) {
		panic("allowedTypes and childSymbols must have the same length")
	}
	// Create a map for fast lookups
	typeMap := make(map[T]string, len(allowedTypes))
	for i, t := range allowedTypes {
		typeMap[t] = childSymbols[i]
	}

	return &TokenSetRepetitionRule[T]{
		BaseParsingRule: internal.BaseParsingRule[T]{SymbolString: symbol},
		allowedTypes:    typeMap,
	}
}

// TokenSetRepetitionRule consumes tokens that are members of a specific set of types.
type TokenSetRepetitionRule[T lexshared.TokenTypeConstraint] struct {
	internal.BaseParsingRule[T]
	allowedTypes map[T]string
}

func (r *TokenSetRepetitionRule[T]) Match(tokens []*lexshared.Token[T], index int) (*parseshared.ParseTree[T], error, int) {
	children := make([]*parseshared.ParseTree[T], 0)
	currentIndex := index

	for currentIndex < len(tokens) {
		token := tokens[currentIndex]
		childSymbol, ok := r.allowedTypes[token.Type]
		if !ok {
			break // Token is not in the allowed set, stop consuming.
		}

		childNode := &parseshared.ParseTree[T]{
			Symbol: childSymbol,
			Token:  token,
		}
		children = append(children, childNode)
		currentIndex++
	}

	if len(children) == 0 {
		return nil, fmt.Errorf("rule %s expected at least one token from the allowed set", r.Symbol()), 0
	}

	tree := &parseshared.ParseTree[T]{
		Symbol:   r.Symbol(),
		Children: children,
	}
	return tree, nil, len(children)
}
