package common_transformers

import (
	"fmt"
	shared3 "github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/lexing/shared"

	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/parsing/shared"
	shared2 "github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/transforming/shared"
)

func AppendTokenValueToSlice[T any, U shared3.TokenTypeConstraint](slice *[]T, converter func(string) (T, error)) shared2.TransformCallback[U] {
	return func(node *shared.ParseTree[U]) {
		value, err := converter(string(node.Token.Value))
		if err != nil {
			fmt.Printf("error converting token value to desired type: %v\n", err)
			return
		}
		*slice = append(*slice, value)
	}
}

func AppendTokenValueToSliceSorted[T any, U shared3.TokenTypeConstraint](slice *[]T, converter func(string) (T, error), sortFunc func([]T)) shared2.TransformCallback[U] {
	return func(node *shared.ParseTree[U]) {
		value, err := converter(string(node.Token.Value))
		if err != nil {
			fmt.Printf("error converting token value to desired type: %v\n", err)
			return
		}
		*slice = append(*slice, value)
		sortFunc(*slice)
	}
}
