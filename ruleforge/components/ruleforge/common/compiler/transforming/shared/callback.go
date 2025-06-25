package shared

import (
	shared3 "github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/lexing/shared"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/parsing/shared"
)

type TransformCallback[T shared3.TokenTypeConstraint] func(node *shared.ParseTree[T])
