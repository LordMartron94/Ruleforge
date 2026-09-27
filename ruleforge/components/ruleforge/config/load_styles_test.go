package config

import (
	"path/filepath"
	"strings"
	"testing"
)

const testStylesheet = `
[Templates.Tier]
FontSize = 40
TextColor = "t2"
BackgroundColor = { var = "bg" }
Beam = { Color = "Orange", Temp = true }

[Templates.Group]
BackgroundColor = "#10203080"
BorderColor = { red = 1, green = 2, blue = 3, alpha = 4 }
Minimap = { Shape = "Circle", Color = "Orange", Size = 1 }

[Final.Plain]
Combination = ["Templates/Group", "Templates/Tier"]

[Studio.Distinct.Fields]
Min = 10
Vars = ["bg"]
`

func TestLoadStylesheetResolvesColoursAndCombinations(t *testing.T) {
	path := filepath.Join(t.TempDir(), "sheet.toml")
	writeFile(t, path, testStylesheet)
	sheet, err := LoadStylesheet(path, map[string]string{"t2": "#FFD700", "bg": "#112233"})
	if err != nil {
		t.Fatal(err)
	}
	final, ok := sheet.Styles["Final/Plain"]
	if !ok {
		t.Fatalf("Final/Plain missing; have %v", len(sheet.Styles))
	}
	if final.TextColor.Var != "t2" || *final.TextColor.Red != 0xFF || *final.TextColor.Alpha != 0xFF {
		t.Errorf("text colour: %+v", final.TextColor)
	}
	// Later combinations win, so the tier's palette background replaces the group's literal one.
	if final.BackgroundColor.Var != "bg" || *final.BackgroundColor.Blue != 0x33 {
		t.Errorf("background colour: %+v", final.BackgroundColor)
	}
	if final.BorderColor.Var != "" || *final.BorderColor.Alpha != 4 {
		t.Errorf("border colour: %+v", final.BorderColor)
	}
	if *final.FontSize != 40 || *final.Beam.Color != "Orange" || *final.Minimap.Size != 1 {
		t.Errorf("non-colour properties: %+v", final)
	}
	group := sheet.Styles["Templates/Group"]
	if *group.BackgroundColor.Alpha != 0x80 {
		t.Errorf("8-digit hex alpha: %+v", group.BackgroundColor)
	}
	if g := sheet.Studio.Distinct["Fields"]; g.Min != 10 || len(g.Vars) != 1 {
		t.Errorf("studio settings: %+v", sheet.Studio)
	}
	if _, isStyle := sheet.Styles["Studio/Distinct/Fields"]; isStyle {
		t.Errorf("studio settings were parsed as a style")
	}
}

func TestLoadStylesheetRejectsUnknownPaletteName(t *testing.T) {
	path := filepath.Join(t.TempDir(), "sheet.toml")
	writeFile(t, path, "[Final.X]\nTextColor = 'missing'\n")
	_, err := LoadStylesheet(path, map[string]string{})
	if err == nil || !strings.Contains(err.Error(), `"missing"`) {
		t.Fatalf("expected missing palette name error, got %v", err)
	}
}
