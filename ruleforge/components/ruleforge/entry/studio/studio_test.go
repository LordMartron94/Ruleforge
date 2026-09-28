package studio

import (
	"os"
	"path/filepath"
	"strings"
	"testing"

	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/compilation"
)

func TestWriteSharesConditionsAndKeepsScriptTagClosed(t *testing.T) {
	shared := []compilation.StudioCondition{{Key: "BaseType", Operator: "==", Values: []string{"</script><b>"}}}
	data := Data{
		Styles: []Style{{ID: "a", Name: "A"}},
		Filters: []Filter{
			{Script: "S", StyleID: "a", Blocks: []compilation.StudioBlock{{Line: 3, Action: "Show", Conditions: shared}}},
			{Script: "S", StyleID: "b", Blocks: []compilation.StudioBlock{{Line: 3, Action: "Show", Conditions: shared}, {Line: 9, Action: "Hide"}}},
		},
	}
	compacted, err := compact(data)
	if err != nil {
		t.Fatal(err)
	}
	if len(compacted.Conditions) != 2 {
		t.Fatalf("expected 2 distinct condition sets, got %d", len(compacted.Conditions))
	}
	if compacted.Filters[0].Blocks[0].Condition != compacted.Filters[1].Blocks[0].Condition || len(compacted.Looks) != 1 || len(compacted.Sources) != 1 {
		t.Errorf("identical conditions, looks or sources were not shared: %+v", compacted)
	}

	path := filepath.Join(t.TempDir(), "out", "studio.html")
	if err := Write(path, data); err != nil {
		t.Fatal(err)
	}
	page, err := os.ReadFile(path)
	if err != nil {
		t.Fatal(err)
	}
	if strings.Contains(string(page), "/*STUDIO_DATA*/") {
		t.Errorf("placeholder was not replaced")
	}
	if strings.Count(string(page), "</script>") != 2 {
		t.Errorf("data closed a script tag early")
	}
}

func TestTemplateIncludesPastedItemInspectorAndResolvedStyling(t *testing.T) {
	for _, want := range []string{
		`id="item-import"`,
		`id="import-inspector"`,
		`clipboardData.getData('text/plain')`,
		`Applied styling`,
		`styleFacts(active.look, P)`,
	} {
		if !strings.Contains(pageTemplate, want) {
			t.Errorf("studio template is missing %q", want)
		}
	}
}
