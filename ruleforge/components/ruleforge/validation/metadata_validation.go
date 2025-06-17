package validation

import (
	"fmt"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/parsing/shared"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/extensions"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/rules/definitions"
)

type FirstBlockValidator struct {
	node *shared.ParseTree[definitions.LexingTokenType]
}

func (v FirstBlockValidator) Validate() error {
	if v.node == nil {
		return fmt.Errorf("empty file: no metadata blocks")
	}
	if v.node.Symbol != definitions.ParseSymbolMetadataSection.String() {
		return fmt.Errorf("file must start with metadata block")
	}
	return nil
}

// ---- Required Fields ----

var required = []definitions.ParseSymbol{
	definitions.ParseSymbolNameAssignment,
	definitions.ParseSymbolVersionAssignment,
	definitions.ParseSymbolStrictnessAssignment,
}

type RequiredFieldsValidator struct {
	node *shared.ParseTree[definitions.LexingTokenType]
}

func (v RequiredFieldsValidator) Validate() error {
	symbols := v.node.GetNthGenDescendantSymbols(2)
	for _, sym := range required {
		if !extensions.Contains(symbols, sym.String()) {
			return fmt.Errorf("metadata missing %s", sym)
		}
	}
	return nil
}

// ---- Strictness ----

var allowed = map[string]bool{
	definitions.ParseSymbolAll.String():         true,
	definitions.ParseSymbolSoft.String():        true,
	definitions.ParseSymbolSemiStrict.String():  true,
	definitions.ParseSymbolStrict.String():      true,
	definitions.ParseSymbolSuperStrict.String(): true,
}

type StrictnessValidator struct {
	node *shared.ParseTree[definitions.LexingTokenType]
}

func (v StrictnessValidator) Validate() error {
	val := StrictnessValue(v.node)
	if !allowed[val] {
		return fmt.Errorf("invalid strictness: %q", val)
	}
	return nil
}

func StrictnessValue(node *shared.ParseTree[definitions.LexingTokenType]) string {
	assign := node.FindSymbolNode(definitions.ParseSymbolStrictnessAssignment.String())
	return assign.Children[4].Children[0].Token.ValueToString()
}
