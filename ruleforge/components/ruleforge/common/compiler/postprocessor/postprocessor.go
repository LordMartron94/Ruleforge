package postprocessor

import (
	shared2 "github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/lexing/shared"
	parseShared "github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/parsing/shared"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/extensions"
)

type PostProcessor[T shared2.TokenTypeConstraint] struct {
}

func (p *PostProcessor[T]) FilterOutSymbols(filterSymbols []string, node *parseShared.ParseTree[T]) *parseShared.ParseTree[T] {
	// 1. Check if the current node represents a symbol to filter.
	if node == nil || extensions.Contains(filterSymbols, node.Symbol) {
		return nil
	}

	// 2. This is a node we want to keep. Create a copy.
	filteredNode := &parseShared.ParseTree[T]{
		Symbol:   node.Symbol,
		Token:    node.Token,
		Children: make([]*parseShared.ParseTree[T], 0),
	}

	// 3. Recursively filter the children of the current node.
	for _, child := range node.Children {
		filteredChild := p.FilterOutSymbols(filterSymbols, child)
		if filteredChild != nil {
			filteredNode.Children = append(filteredNode.Children, filteredChild)
		}
	}

	return filteredNode
}

func (p *PostProcessor[T]) RemoveEmptyNodes(node *parseShared.ParseTree[T]) *parseShared.ParseTree[T] {
	if node.Token == nil && len(node.Children) == 0 {
		return nil
	}

	// 2. This is a node we want to keep. Create a copy.
	filteredNode := &parseShared.ParseTree[T]{
		Symbol:   node.Symbol,
		Token:    node.Token,
		Children: make([]*parseShared.ParseTree[T], 0),
	}

	// 3. Recursively filter the children of the current node.
	for _, child := range node.Children {
		filteredChild := p.RemoveEmptyNodes(child)
		if filteredChild != nil {
			filteredNode.Children = append(filteredNode.Children, filteredChild)
		}
	}

	return filteredNode
}
