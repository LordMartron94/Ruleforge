package rules

import (
	"os"
	"path/filepath"
	"strings"
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

func TestCorruptedModsLexesAsItsOwnCondition(t *testing.T) {
	handler := compiler.NewFileHandler(strings.NewReader(`WHERE @corrupted_mods >= 1 -> @corrupted == "True"`), GetLexingRules(), GetParsingRules(), symbols.IgnoreToken)
	tokens, err := handler.Lex()
	if err != nil {
		t.Fatalf("lex: %v", err)
	}
	var conditions []string
	for _, token := range tokens {
		if token.Type == symbols.ConditionKeywordToken {
			conditions = append(conditions, token.ValueToString())
		}
	}
	if len(conditions) != 2 || conditions[0] != "@corrupted_mods" || conditions[1] != "@corrupted" {
		t.Fatalf("conditions = %q, want [@corrupted_mods @corrupted]", conditions)
	}
}
