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

func TestEveryConditionKeywordHasASpec(t *testing.T) {
	specs := map[string]bool{}
	for _, spec := range ConditionSpecs {
		specs[spec.ID] = true
	}
	keywords := ConditionKeywords()
	for _, keyword := range keywords {
		if !specs[keyword] {
			t.Errorf("condition %s has no ConditionSpec, so the Studio cannot offer it", keyword)
		}
		delete(specs, keyword)
	}
	for id := range specs {
		t.Errorf("ConditionSpec %s is not a condition keyword", id)
	}
}

func TestBuildAndStrictnessListsMatchTheKeywords(t *testing.T) {
	literals := map[symbols.LexingTokenType][]string{}
	for _, def := range keywordDefs {
		literals[def.token] = append(literals[def.token], def.literal)
	}
	for _, strictness := range Strictnesses {
		found := false
		for _, typ := range []symbols.LexingTokenType{symbols.AllKeywordToken, symbols.SoftKeywordToken, symbols.SemiStrictKeywordToken, symbols.StrictKeywordToken, symbols.SuperStrictKeywordToken} {
			for _, literal := range literals[typ] {
				found = found || literal == strictness
			}
		}
		if !found {
			t.Errorf("strictness %s is not a keyword", strictness)
		}
	}
	for _, build := range BuildKeywords {
		found := false
		for _, typ := range []symbols.LexingTokenType{symbols.MeleeBuildToken, symbols.DexBuildToken, symbols.SpellBuildToken, symbols.MeleeSpellHybridBuildToken, symbols.MeleeDexHybridBuildToken, symbols.SpellDexHybridBuildToken} {
			for _, literal := range literals[typ] {
				found = found || literal == build
			}
		}
		if !found {
			t.Errorf("build %s is not a keyword", build)
		}
	}
}

// validateExampleWith validates examples/basic.rf with its BUILD line replaced.
func validateExampleWith(t *testing.T, replacement string) error {
	t.Helper()
	content, err := os.ReadFile(filepath.Join("..", "..", "..", "..", "examples", "basic.rf"))
	if err != nil {
		t.Fatal(err)
	}
	source := strings.Replace(string(content), "BUILD      => MARAUDER", replacement, 1)
	if source == string(content) {
		t.Fatal("examples/basic.rf has no BUILD line to replace")
	}
	handler := compiler.NewFileHandler(strings.NewReader(source), GetLexingRules(), GetParsingRules(), symbols.IgnoreToken)
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
	return validation.NewParseTreeValidator(tree).Validate()
}

func TestThemesIsAnOptionalMetadataField(t *testing.T) {
	const build = "BUILD      => MARAUDER"
	if err := validateExampleWith(t, build+"\n  THEMES     => \"aryzel original\""); err != nil {
		t.Errorf("THEMES after BUILD should validate: %v", err)
	}
	if err := validateExampleWith(t, "THEMES     => \"aryzel\"\n  "+build); err != nil {
		t.Errorf("THEMES before BUILD should validate: %v", err)
	}
	err := validateExampleWith(t, build+"\n  THEMES => \"a\"\n  THEMES => \"b\"")
	if err == nil || !strings.Contains(err.Error(), "duplicate") {
		t.Errorf("a second THEMES should be a duplicate field error, got %v", err)
	}
}
