package validation

//
//import (
//	"fmt"
//	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/parsing/shared"
//	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/extensions"
//	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/rules/symbols"
//)
//
//type FirstBlockValidator struct {
//	node *shared.ParseTree[symbols.LexingTokenType]
//}
//
//func (v FirstBlockValidator) Validate() error {
//	if v.node == nil {
//		return fmt.Errorf("empty file: no metadata blocks")
//	}
//	if v.node.Symbol != symbols.ParseSymbolMetadataSection.String() {
//		return fmt.Errorf("file must start with metadata block")
//	}
//	return nil
//}
//
//// ---- Required Fields ----
//
//var required = []symbols.ParseSymbol{
//	symbols.ParseSymbolNameAssignment,
//	symbols.ParseSymbolVersionAssignment,
//	symbols.ParseSymbolStrictnessAssignment,
//}
//
//type RequiredFieldsValidator struct {
//	node *shared.ParseTree[symbols.LexingTokenType]
//}
//
//func (v RequiredFieldsValidator) Validate() error {
//	symbols := v.node.GetNthGenDescendantSymbols(2)
//	for _, sym := range required {
//		if !extensions.Contains(symbols, sym.String()) {
//			return fmt.Errorf("metadata missing %s", sym)
//		}
//	}
//	return nil
//}
//
//// ---- Strictness ----
//
//var allowed = map[string]bool{
//	symbols.ParseSymbolAll.String():         true,
//	symbols.ParseSymbolSoft.String():        true,
//	symbols.ParseSymbolSemiStrict.String():  true,
//	symbols.ParseSymbolStrict.String():      true,
//	symbols.ParseSymbolSuperStrict.String(): true,
//}
//
//type StrictnessValidator struct {
//	node *shared.ParseTree[symbols.LexingTokenType]
//}
//
//func (v StrictnessValidator) Validate() error {
//	val := StrictnessValue(v.node)
//	if !allowed[val] {
//		return fmt.Errorf("invalid strictness: %q", val)
//	}
//	return nil
//}
//
//func StrictnessValue(node *shared.ParseTree[symbols.LexingTokenType]) string {
//	assign := node.FindSymbolNode(symbols.ParseSymbolStrictnessAssignment.String())
//	return assign.Children[4].Children[0].Token.ValueToString()
//}
