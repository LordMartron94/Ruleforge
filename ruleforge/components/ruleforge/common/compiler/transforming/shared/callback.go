package shared

import "github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/parsing/shared"

type TransformCallback[T comparable] func(node *shared.ParseTree[T])
