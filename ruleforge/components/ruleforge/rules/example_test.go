package rules

import (
	"os"
	"path/filepath"
	"testing"

	compiler "github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/postprocessor"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/rules/symbols"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/validation"
)

func TestBundledRuleforgeExampleParsesAndValidates(t *testing.T) {
	path := filepath.Join("..", "..", "..", "..", "examples", "basic.rf")
	file, err := os.Open(path)
	if err != nil {
		t.Fatal(err)
	}
	defer file.Close()
	handler := compiler.NewFileHandler(file, GetLexingRules(), GetParsingRules(), symbols.IgnoreToken)
	if _, err := handler.Lex(); err != nil {
		t.Fatalf("lex: %v", err)
	}
	tree, err := handler.Parse()
	if err != nil {
		t.Fatalf("parse: %v", err)
	}
	pp := postprocessor.PostProcessor[symbols.LexingTokenType]{}
	tree = pp.FilterOutSymbols([]string{symbols.ParseSymbolWhitespace.String(), symbols.ParseSymbolBlockOperator.String()}, tree)
	tree = pp.RemoveEmptyNodes(tree)
	if err := validation.NewParseTreeValidator(tree).Validate(); err != nil {
		t.Fatalf("validate: %v", err)
	}
}
