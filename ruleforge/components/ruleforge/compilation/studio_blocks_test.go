package compilation

import (
	"reflect"
	"testing"

	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/compilation/model"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/config"
)

func TestBuildStudioBlocksAttachesRecordsAndParsesConditions(t *testing.T) {
	factory := &RuleFactory{}
	factory.SetSection("Currency")
	factory.SetSource(`MACRO handle_csv (category = "Orbs")`)
	style := config.Style{
		Name:            "Final/Orbs/T2",
		TextColor:       &config.Color{Red: u8(1), Green: u8(2), Blue: u8(3), Alpha: u8(255), Var: "rad-t1-text"},
		BackgroundColor: &config.Color{Red: u8(255), Green: u8(202), Blue: u8(73), Alpha: u8(200), Var: "rad-t1-bg"},
		FontSize:        intp(45),
		Beam:            &config.Beam{Color: strp("Yellow")},
		Minimap:         &config.Minimap{Size: intp(0), Color: strp("Yellow"), Shape: strp("Circle")},
	}
	rule := factory.ConstructRule(model.ShowRule, style, []string{`AreaLevel < "68" `, `BaseType == "Divine Orb" "Sacred Orb" `, `Corrupted False`})

	lines := []string{"# header", "", `# >>>>>>>>>>>>>>>> SECTION: Currency (Campaign currency)`}
	lines = append(lines, rule...)
	lines = append(lines, "Hide", "\tClass \"Tinctures\"", "\tSetFontSize 18", "")

	blocks := BuildStudioBlocks(lines, factory.Records())
	if len(blocks) != 2 {
		t.Fatalf("expected 2 blocks, got %d", len(blocks))
	}

	shown := blocks[0]
	if shown.Line != 4 || shown.Action != "Show" || shown.Section != "Currency" || shown.Style != "Final/Orbs/T2" {
		t.Errorf("block identity: %+v", shown)
	}
	if shown.Source != `MACRO handle_csv (category = "Orbs")` {
		t.Errorf("source: %q", shown.Source)
	}
	wantConditions := []StudioCondition{
		{Key: "AreaLevel", Operator: "<", Values: []string{"68"}},
		{Key: "BaseType", Operator: "==", Values: []string{"Divine Orb", "Sacred Orb"}},
		{Key: "Corrupted", Values: []string{"False"}},
	}
	if !reflect.DeepEqual(shown.Conditions, wantConditions) {
		t.Errorf("conditions:\n got %+v\nwant %+v", shown.Conditions, wantConditions)
	}
	if shown.Look.Text.Hex != "#010203" || shown.Look.Text.Var != "rad-t1-text" {
		t.Errorf("text colour: %+v", shown.Look.Text)
	}
	if shown.Look.Background.Hex != "#FFCA49C8" || shown.Look.Background.Var != "rad-t1-bg" {
		t.Errorf("background colour: %+v", shown.Look.Background)
	}
	if shown.Look.FontSize != 45 || shown.Look.Beam != "Yellow" || shown.Look.MinimapKind != "Circle" || *shown.Look.MinimapSize != 0 {
		t.Errorf("look: %+v", shown.Look)
	}

	hidden := blocks[1]
	if hidden.Action != "Hide" || hidden.Line != 14 || hidden.Source != "" || hidden.Look.FontSize != 18 {
		t.Errorf("unrecorded block: %+v", hidden)
	}
	if !reflect.DeepEqual(hidden.Conditions, []StudioCondition{{Key: "Class", Values: []string{"Tinctures"}}}) {
		t.Errorf("unrecorded conditions: %+v", hidden.Conditions)
	}
}
