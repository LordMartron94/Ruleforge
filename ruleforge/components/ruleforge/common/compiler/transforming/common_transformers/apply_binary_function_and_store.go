package common_transformers

import (
	"fmt"
	"strconv"

	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/parsing/shared"
	shared2 "github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/transforming/shared"
)

// ApplyBinaryOperationToChildren applies a binary operation to the children of a given node and appends the result to the provided slice.
func ApplyBinaryOperationToChildren[T comparable](operation func(left, right int) int, result *[]int) shared2.TransformCallback[T] {
	return func(node *shared.ParseTree[T]) {
		if len(node.Children) < 2 {
			fmt.Println("Invalid binary operation node. Too few children for binary operation. Expected 2, got", len(node.Children))
			return
		} else if len(node.Children) > 2 {
			fmt.Println("Invalid binary operation node. Too many children for binary operation. Expected 2, got", len(node.Children))
			return
		}

		leftNum, err := strconv.Atoi(string(node.Children[0].Token.Value))
		if err != nil {
			fmt.Printf("error converting token value to desired type: %v\n", err)
			return
		}

		rightNum, err := strconv.Atoi(string(node.Children[1].Token.Value))
		if err != nil {
			fmt.Printf("error converting token value to desired type: %v\n", err)
			return
		}

		resultValue := operation(leftNum, rightNum)

		//fmt.Println("Applying binary operation to children:", leftNum, "DISTANCE FUNC ->", rightNum, "=>", resultValue)

		*result = append(*result, resultValue)
	}
}
