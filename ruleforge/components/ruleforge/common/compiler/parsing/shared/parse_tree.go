package shared

import (
	"fmt"
	"slices"
	"strings"

	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/lexing/shared"
)

type ParseTree[T shared.TokenTypeConstraint] struct {
	Symbol   string
	Token    *shared.Token[T]
	Children []*ParseTree[T]
}

// Print prints the parse tree with indentation
func (pt *ParseTree[T]) Print(indent int, ignoreTokens []T) {
	if pt == nil {
		return
	}

	for _, ignoreToken := range ignoreTokens {
		if pt.Token == nil {
			continue
		}

		if pt.Token.Type == ignoreToken {
			return
		}
	}

	fmt.Println(strings.Repeat("  ", indent) + pt.Symbol)

	if pt.Token != nil {
		fmt.Println(strings.Repeat("  ", indent+1) + "Token: " + fmt.Sprintf("%s (%v)", pt.Token.Value, pt.Token.Type))
	}

	for _, child := range pt.Children {
		child.Print(indent+1, ignoreTokens)
	}
}

func (pt *ParseTree[T]) GetNumberOfTokens() int {
	if pt == nil {
		panic("ParseTree[T] is nil")
	}

	count := 0
	if pt.Token != nil {
		count++
	}

	for _, child := range pt.Children {
		// child may be nil, but child.GetNumberOfTokens handles that too
		count += child.GetNumberOfTokens()
	}

	return count
}

// GetNthGenDescendantSymbols returns the symbols of all descendants at the given generation depth n.
// Generation 1 are the immediate children, generation 2 are grandchildren, and so on.
func (pt *ParseTree[T]) GetNthGenDescendantSymbols(n int) []string {
	if n < 1 {
		return nil
	}
	var symbols []string
	for _, child := range pt.Children {
		if n == 1 {
			symbols = append(symbols, child.Symbol)
		} else {
			symbols = append(symbols, child.GetNthGenDescendantSymbols(n-1)...)
		}
	}
	return symbols
}

// GetNthGenDescendantTokens returns the tokens of all descendants at the given generation depth n.
// Generation 1 are the immediate children, generation 2 are grandchildren, and so on.
func (pt *ParseTree[T]) GetNthGenDescendantTokens(n int) []*shared.Token[T] {
	if n < 1 {
		return nil
	}
	var tokens []*shared.Token[T]
	for _, child := range pt.Children {
		if n == 1 {
			tokens = append(tokens, child.Token)
		} else {
			tokens = append(tokens, child.GetNthGenDescendantTokens(n-1)...)
		}
	}
	return tokens
}

// FindSymbolNode searches the parse tree for the first node whose Symbol equals searchSymbol.
// It panics if no such node is found in the tree.
func (pt *ParseTree[T]) FindSymbolNode(searchSymbol string) *ParseTree[T] {
	if node := pt.findSymbolNode(searchSymbol); node != nil {
		return node
	}

	panic(fmt.Sprintf("symbol %q not found in parse tree", searchSymbol))
}

// findSymbolNode is a helper that returns the node matching searchSymbol, or nil if not found.
func (pt *ParseTree[T]) findSymbolNode(searchSymbol string) *ParseTree[T] {
	if pt.Symbol == searchSymbol {
		return pt
	}
	for _, child := range pt.Children {
		if result := child.findSymbolNode(searchSymbol); result != nil {
			return result
		}
	}
	return nil
}

// FindAllSymbolNodes searches the parse tree for all nodes whose Symbol equals searchSymbol.
// It returns a slice of pointers to each matching node (which will be empty if there are none).
func (pt *ParseTree[T]) FindAllSymbolNodes(searchSymbol string) []*ParseTree[T] {
	var matches []*ParseTree[T]
	pt.collectSymbolNodes(searchSymbol, &matches)
	return matches
}

// collectSymbolNodes is a helper that appends matching nodes to the provided slice.
func (pt *ParseTree[T]) collectSymbolNodes(searchSymbol string, matches *[]*ParseTree[T]) {
	if pt.Symbol == searchSymbol {
		*matches = append(*matches, pt)
	}
	for _, child := range pt.Children {
		child.collectSymbolNodes(searchSymbol, matches)
	}
}

// FindAllSymbolAndTokenTypes searches the parse tree for all nodes
// whose Symbol equals searchSymbol and whose TokenType is in tokenTypes.
// It returns a slice of matching nodes (empty if none).
func (pt *ParseTree[T]) FindAllSymbolAndTokenTypes(
	searchSymbol string,
	tokenTypes []T,
) []*ParseTree[T] {
	var matches []*ParseTree[T]
	pt.collectSymbolAndTokenTypes(searchSymbol, tokenTypes, &matches)
	return matches
}

// collectSymbolAndTokenTypes is a helper that does the DFS and appends
// to matches whenever both symbol and token-type criteria are met.
func (pt *ParseTree[T]) collectSymbolAndTokenTypes(
	searchSymbol string,
	tokenTypes []T,
	matches *[]*ParseTree[T],
) {
	if pt.Symbol == searchSymbol && (pt.Token != nil && slices.Contains(tokenTypes, pt.Token.Type)) {
		*matches = append(*matches, pt)
	}
	for _, child := range pt.Children {
		child.collectSymbolAndTokenTypes(searchSymbol, tokenTypes, matches)
	}
}
