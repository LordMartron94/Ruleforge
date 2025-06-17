package common_transformers

import (
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/parsing/shared"
	shared2 "github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/transforming/shared"
)

func CombineCallbacks[T comparable](callbacks ...shared2.TransformCallback[T]) shared2.TransformCallback[T] {
	return func(node *shared.ParseTree[T]) {
		for _, callback := range callbacks {
			callback(node)
		}
	}
}
