package common_transformers

import (
	shared3 "github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/lexing/shared"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/parsing/shared"
	shared2 "github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/transforming/shared"
)

func CollectNodesBySymbolRecursive[T shared3.TokenTypeConstraint](nodeSymbol string, target *[]*shared.ParseTree[T]) shared2.TransformCallback[T] {
	return func(node *shared.ParseTree[T]) {
		foundSymbols := node.FindAllSymbolNodes(nodeSymbol)
		*target = append(*target, foundSymbols...)
	}
}
