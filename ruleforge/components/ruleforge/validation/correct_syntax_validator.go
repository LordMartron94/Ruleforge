package validation

import (
	"fmt"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/parsing/shared"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/rules/symbols"
)

type CorrectSyntaxValidator struct {
	blocks []*shared.ParseTree[symbols.LexingTokenType]
}

func (v CorrectSyntaxValidator) Validate() error {
	for i, block := range v.blocks {
		if block.Symbol == symbols.ParseSymbolAny.String() {
			return fmt.Errorf("block (%d) has incorrect syntax (search on the value and find out why!): %q", i, block.Token.String())
		}
	}
	return nil
}
