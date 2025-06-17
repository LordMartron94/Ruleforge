package common_transformers

import (
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/compiler/parsing/shared"
	shared2 "github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/compiler/transforming/shared"
)

func CollectNodesByType[T comparable](nodeType string, target *[]*shared.ParseTree[T]) shared2.TransformCallback[T] {
	return func(node *shared.ParseTree[T]) {
		if node.Symbol == nodeType {
			*target = append(*target, node)
		}
	}
}
