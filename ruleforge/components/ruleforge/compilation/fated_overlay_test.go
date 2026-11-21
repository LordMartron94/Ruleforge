package compilation

import (
	"strings"
	"testing"

	model "github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/compilation/model"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/config"
)

func TestFatedDecorationCannotChangeVisibilityOrOverrideStrongerProperties(t *testing.T) {
	style := config.Style{Name: "Equipment", FontSize: intp(33), BorderColor: overlayColor(1, 2, 3, "bd-rare"), TextColor: overlayColor(4, 5, 6, "t5")}
	factory := &RuleFactory{fated: &config.Style{BorderColor: overlayColor(7, 8, 9, "bd-fated")}, gearBases: map[string]bool{"Royal Plate": true}}
	for _, test := range []struct {
		action     model.RuleType
		conditions []string
	}{
		{model.HideRule, []string{`Rarity == "Rare"`}},
		{model.ShowRule, []string{`Rarity <= "Magic"`}},
		{model.ShowRule, []string{`Class == "Stackable Currency"`}},
		{model.ShowRule, []string{`Rarity == "Unique"`}},
	} {
		if got := blocks(factory.ConstructRule(test.action, style, test.conditions)); len(got) != 1 {
			t.Fatalf("Fated broadened an incompatible rule: %v", got)
		}
	}
	out := blocks(factory.ConstructRule(model.ShowRule, style, []string{`Rarity == "Rare"`, `BaseType == "Royal Plate"`}))
	if len(out) != 2 || !has(out[0], `Fated "True"`) || !has(out[0], "SetTextColor 4 5 6 255") || !has(out[0], "SetFontSize 33") {
		t.Fatalf("Fated changed priority or failed to decorate: %v", out)
	}
	if strings.Count(strings.Join(out[0], "\n"), "Rarity") != 1 {
		t.Fatal("duplicate rarity condition in Fated variant")
	}
	for _, property := range []string{"bd-max-rolled", "bd-fractured", "bd-corrupted", "bd-veiled", "rad-t1-border"} {
		protected := style
		protected.BorderColor = overlayColor(1, 2, 3, property)
		if got := blocks(factory.ConstructRule(model.ShowRule, protected, []string{`Rarity == "Rare"`})); len(got) != 1 {
			t.Fatalf("Fated replaced %s", property)
		}
	}
}

func TestMapModCountCompilesAndRemainsNumericInStudio(t *testing.T) {
	condition := model.Condition{Identifier: "@explicit_mod_count", Operator: ">=", Value: []string{"7"}}
	text := condition.ConstructCompiledCondition(&map[string][]string{}, nil)
	parsed := parseCondition(tokenizeFilterLine(text))
	if parsed.Key != "HasExplicitMod" || parsed.Operator != ">=" || len(parsed.Values) != 7 || parsed.Values[0] != "7" {
		t.Fatalf("invalid explicit count predicate: %q => %+v", text, parsed)
	}
}
