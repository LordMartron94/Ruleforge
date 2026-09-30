package document

import (
	"os"
	"path/filepath"
	"strings"
	"testing"

	compiler "github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/parsing/shared"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/postprocessor"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/rules"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/rules/symbols"
)

const repoRoot = "../../../../.."

func scripts(t *testing.T) map[string]string {
	t.Helper()
	found := map[string]string{}
	for _, pattern := range []string{"__filters__/*.rf", "examples/*.rf"} {
		paths, err := filepath.Glob(filepath.Join(repoRoot, pattern))
		if err != nil {
			t.Fatal(err)
		}
		for _, path := range paths {
			content, err := os.ReadFile(path)
			if err != nil {
				t.Fatal(err)
			}
			found[filepath.Base(path)] = string(content)
		}
	}
	if len(found) < 2 {
		t.Fatalf("expected the campaign script and the example, found %d scripts", len(found))
	}
	return found
}

// officialTree flattens the compiler's own parse tree, without layout.
func officialTree(t *testing.T, src string) []string {
	t.Helper()
	handler := compiler.NewFileHandler(strings.NewReader(src), rules.GetLexingRules(), rules.GetParsingRules(), symbols.IgnoreToken)
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
	var flat []string
	var walk func(n *shared.ParseTree[symbols.LexingTokenType], depth int)
	walk = func(n *shared.ParseTree[symbols.LexingTokenType], depth int) {
		line := strings.Repeat(" ", depth) + n.Symbol
		if n.Token != nil {
			line += " " + n.Token.ValueToString()
		}
		flat = append(flat, line)
		for _, child := range n.Children {
			walk(child, depth+1)
		}
	}
	walk(tree, 0)
	return flat
}

func TestEveryScriptPrintsBackExactly(t *testing.T) {
	for name, src := range scripts(t) {
		doc, err := Parse(src)
		if err != nil {
			t.Fatalf("%s: %v", name, err)
		}
		if got := Print(doc); got != src {
			t.Errorf("%s changed when printed back:\n%s", name, firstDifference(src, got))
		}
	}
}

func TestCanonicalLayoutMeansTheSameScript(t *testing.T) {
	for name, src := range scripts(t) {
		doc, err := Parse(src)
		if err != nil {
			t.Fatalf("%s: %v", name, err)
		}
		Strip(doc)
		if err := Validate(doc); err != nil {
			t.Fatalf("%s: %v", name, err)
		}
		canonical := Print(doc)
		want, got := officialTree(t, src), officialTree(t, canonical)
		if strings.Join(want, "\n") != strings.Join(got, "\n") {
			t.Fatalf("%s: the canonical layout parses differently:\n%s", name, firstDifference(strings.Join(want, "\n"), strings.Join(got, "\n")))
		}
		again, err := Parse(canonical)
		if err != nil {
			t.Fatalf("%s: canonical layout: %v", name, err)
		}
		original, _ := Parse(src)
		if err := Equivalent(original, again); err != nil {
			t.Fatalf("%s: %v", name, err)
		}
		if Print(again) != canonical {
			t.Fatalf("%s: the canonical layout is not stable", name)
		}
	}
}

func TestCanonicalLayoutMatchesTheHouseStyle(t *testing.T) {
	src := `METADATA {
  NAME       => "Example"
  VERSION    => "1.0"
  STRICTNESS => ALL
  BUILD      => MARAUDER
}

var shown  => "Final/A"
var hidden => "Final/B" + $shown !override["Final/B" => "BorderColor"]

SECTION {
  METADATA {
    NAME        => "Gold"
    DESCRIPTION => "Gold stacks"
  }

  SECTION_CONDITIONS {
    WHERE @area_level < 68
  }

  RULES {
    !! A comment
    WHERE @item_type == "Gold" -> @stack_size >= 250 => "Final/Gold" => $Show
    MACRO["handle_csv" -> $category => "Orbs"]
    MACRO["unique_tiering"
      -> $tier_1   => "Final/Uniques/T1"
      -> $fallback => "Final/Uniques/T2"
    ]
  }
}
`
	doc, err := Parse(src)
	if err != nil {
		t.Fatal(err)
	}
	Strip(doc)
	if got := Print(doc); got != src {
		t.Fatalf("canonical layout differs from the house style:\n%s", firstDifference(src, got))
	}
}

func findItem(items []*Item, match func(*Item) bool) *Item {
	for _, item := range items {
		if match(item) {
			return item
		}
		if found := findItem(item.Items, match); found != nil {
			return found
		}
	}
	return nil
}

func TestReconcileKeepsAnEditWhoseSourceTextWasNotCleared(t *testing.T) {
	src := scripts(t)["martron_arada_campaign.rf"]
	doc, err := Parse(src)
	if err != nil {
		t.Fatal(err)
	}
	rule := findItem(doc.Items, func(i *Item) bool {
		return i.Kind == KindRule && i.Style != nil && i.Style.Text == "Final/Leagues/Gold_High"
	})
	if rule == nil {
		t.Fatal("gold rule not found")
	}
	rule.Style.Text = "Final/Leagues/Gold_Mid"
	if unchanged := Print(doc); unchanged != src {
		t.Fatal("expected the stale source text to hide the edit before reconciling")
	}
	if reset := Reconcile(doc); reset != 3 {
		t.Fatalf("expected the rule, its rules block, its section and nothing else to be reset; reset %d", reset)
	}
	printed := Print(doc)
	if !strings.Contains(printed, `WHERE @item_type == "Gold" -> @stack_size >= 1000 => "Final/Leagues/Gold_Mid" => $Show`) {
		t.Fatalf("edit lost:\n%s", firstDifference(src, printed))
	}
	if removed, added := lineDiff(src, printed); removed != 1 || added != 1 {
		t.Fatalf("expected exactly one changed line, got -%d +%d", removed, added)
	}
}

func lineDiff(a, b string) (removed, added int) {
	count := map[string]int{}
	for _, line := range strings.Split(a, "\n") {
		count[line]++
	}
	for _, line := range strings.Split(b, "\n") {
		count[line]--
	}
	for _, n := range count {
		if n > 0 {
			removed += n
		} else {
			added -= n
		}
	}
	return
}

func TestValidateRejectsValuesTheLexerCannotReadBack(t *testing.T) {
	for _, text := range []string{`Say "hi"`, "Wealth, and Power", "a:b"} {
		doc := &Document{Items: []*Item{{Kind: KindImport, Value: &Value{Text: text, Kind: ValueString}}}}
		if err := Validate(doc); err == nil {
			t.Errorf("%q should be rejected", text)
		}
	}
	ok := &Document{Items: []*Item{{Kind: KindImport, Value: &Value{Text: "Hinekora's Lock [T1] - a/b", Kind: ValueString}}}}
	if err := Validate(ok); err != nil {
		t.Errorf("valid text rejected: %v", err)
	}
}

func TestACompactSectionStaysEditableAsSource(t *testing.T) {
	src := "METADATA {\n  NAME => \"X\"\n}\n\nSECTION { METADATA { NAME => \"A\" DESCRIPTION => \"B\" } RULES { WHERE @rarity == \"Rare\" => \"S\" => $Show } }\n"
	doc, err := Parse(src)
	if err != nil {
		t.Fatal(err)
	}
	if len(doc.Items) != 3 || doc.Items[2].Kind != KindSource {
		t.Fatalf("expected metadata, blank and a source section; got %d items", len(doc.Items))
	}
	if Print(doc) != src {
		t.Fatal("source section did not print back exactly")
	}
}

func firstDifference(want, got string) string {
	for i := 0; i < len(want) && i < len(got); i++ {
		if want[i] != got[i] {
			from := max(0, i-60)
			return "want …" + want[from:min(len(want), i+60)] + "…\ngot  …" + got[from:min(len(got), i+60)] + "…"
		}
	}
	return "one is a prefix of the other"
}
