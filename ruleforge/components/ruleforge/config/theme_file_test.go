package config

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func configurationFiles(t *testing.T, pattern string) map[string]string {
	t.Helper()
	paths, err := filepath.Glob(filepath.Join(repositoryRoot(t), "configuration", pattern))
	if err != nil || len(paths) == 0 {
		t.Fatalf("no files match %s: %v", pattern, err)
	}
	files := map[string]string{}
	for _, path := range paths {
		contents, err := os.ReadFile(path)
		if err != nil {
			t.Fatal(err)
		}
		files[path] = string(contents)
	}
	return files
}

func changedLines(a, b string) (removed, added []string) {
	count := map[string]int{}
	for _, line := range strings.Split(a, "\n") {
		count[line]++
	}
	for _, line := range strings.Split(b, "\n") {
		count[line]--
	}
	for line, n := range count {
		for ; n > 0; n-- {
			removed = append(removed, line)
		}
		for ; n < 0; n++ {
			added = append(added, line)
		}
	}
	return
}

func TestEveryThemeFileWritesBackUnchanged(t *testing.T) {
	for path, contents := range configurationFiles(t, "styles/*.toml") {
		theme, err := ParseThemeFile(path, []byte(contents))
		if err != nil {
			t.Fatalf("%s: %v", path, err)
		}
		rendered, err := theme.Render(contents)
		if err != nil {
			t.Fatalf("%s: %v", path, err)
		}
		if rendered != contents {
			removed, added := changedLines(contents, rendered)
			t.Errorf("%s changed without an edit: -%q +%q", filepath.Base(path), removed, added)
		}
	}
}

func TestThemeEditsChangeOnlyTheirLines(t *testing.T) {
	path := filepath.Join(repositoryRoot(t), "configuration", "styles", "aryzel-1-stranded-prince.toml")
	contents, err := os.ReadFile(path)
	if err != nil {
		t.Fatal(err)
	}
	theme, err := ParseThemeFile(path, contents)
	if err != nil {
		t.Fatal(err)
	}
	if theme.Name != "Aryzel I: The Stranded Prince" || !theme.Enabled || len(theme.Groups) < 5 || theme.Groups[0].Title == "" {
		t.Fatalf("unexpected theme: %+v", theme)
	}
	var changedColour string
	for gi := range theme.Groups {
		for ci := range theme.Groups[gi].Colours {
			if theme.Groups[gi].Colours[ci].Name == "bg-ancestral" {
				changedColour = theme.Groups[gi].Colours[ci].Value
				theme.Groups[gi].Colours[ci].Value = "#123456"
			}
		}
	}
	if changedColour == "" {
		t.Fatal("bg-ancestral not found")
	}
	theme.Groups[1].Title = "Tier text, brightest first"
	theme.Groups = append(theme.Groups, PaletteSet{Title: "Studio test", Blank: 1, Colours: []PaletteColour{{Name: "bd-test", Value: "#ABCDEF"}}})
	theme.Enabled = false
	rendered, err := theme.Render(string(contents))
	if err != nil {
		t.Fatal(err)
	}
	removed, added := changedLines(string(contents), rendered)
	wantRemoved := []string{`bg-ancestral = "` + changedColour + `"`, "# Tier text, T2 (brightest) to T10."}
	wantAdded := []string{`bg-ancestral = "#123456"`, "# Tier text, brightest first", "Enabled = false", "", "# Studio test", `bd-test = "#ABCDEF"`}
	if strings.Join(sorted(removed), "|") != strings.Join(sorted(wantRemoved), "|") || strings.Join(sorted(added), "|") != strings.Join(sorted(wantAdded), "|") {
		t.Fatalf("unexpected changes: -%q +%q", removed, added)
	}
	definition, err := ParseStyleDefinition(path, []byte(rendered))
	if err != nil {
		t.Fatal(err)
	}
	if definition.IsEnabled() || definition.Palette["bd-test"] != "#ABCDEF" || definition.Palette["bg-ancestral"] != "#123456" {
		t.Fatalf("edited theme reads back wrong: %+v", definition)
	}
}

func TestANewThemeFileReadsBack(t *testing.T) {
	theme := &ThemeFile{Name: "Test theme", Suffix: "Test", Enabled: true, Stylesheet: "../stylesheets/new-theme.toml", BaseTypes: "../basetypes/x.csv",
		Groups: []PaletteSet{{Title: "Text", Colours: []PaletteColour{{Name: "t1", Value: "#FFFFFF"}}}, {Title: "Fields", Blank: 1, Colours: []PaletteColour{{Name: "bg", Value: "#000000CC"}}}}}
	rendered, err := theme.Render("")
	if err != nil {
		t.Fatal(err)
	}
	want := "# Test theme\nName = \"Test theme\"\nSuffix = \"Test\"\nStylesheet = \"../stylesheets/new-theme.toml\"\nBaseTypes = \"../basetypes/x.csv\"\n\n[Palette]\n# Text\nt1 = \"#FFFFFF\"\n\n# Fields\nbg = \"#000000CC\"\n"
	if rendered != want {
		t.Fatalf("new theme file:\n%s\nwant:\n%s", rendered, want)
	}
	again, err := ParseThemeFile("test.toml", []byte(rendered))
	if err != nil {
		t.Fatal(err)
	}
	if again.Name != theme.Name || len(again.Groups) != 2 || again.Groups[1].Title != "Fields" {
		t.Fatalf("new theme reads back wrong: %+v", again)
	}
}

func sorted(lines []string) []string {
	out := append([]string(nil), lines...)
	for i := range out {
		for j := i + 1; j < len(out); j++ {
			if out[j] < out[i] {
				out[i], out[j] = out[j], out[i]
			}
		}
	}
	return out
}

func TestEveryStylesheetWritesBackUnchanged(t *testing.T) {
	for path, contents := range configurationFiles(t, "stylesheets/*.toml") {
		sheet, err := ParseStylesheetFile(path, []byte(contents))
		if err != nil {
			t.Fatalf("%s: %v", path, err)
		}
		var changes []StyleChange
		for i := range sheet.Entries {
			changes = append(changes, StyleChange{Name: sheet.Entries[i].Name, Props: &sheet.Entries[i].Props})
		}
		var distinct []DistinctChange
		for _, group := range sheet.Distinct {
			distinct = append(distinct, DistinctChange{DistinctEntry: group})
		}
		rendered, err := ApplyStylesheetChanges(contents, changes, distinct)
		if err != nil {
			t.Fatalf("%s: %v", path, err)
		}
		if rendered != contents {
			removed, added := changedLines(contents, rendered)
			t.Errorf("%s changed without an edit: -%q +%q", filepath.Base(path), removed, added)
		}
	}
}

func TestStylesheetEditsChangeOnlyTheirLines(t *testing.T) {
	root := repositoryRoot(t)
	path := filepath.Join(root, "configuration", "stylesheets", "aryzel.toml")
	contents, err := os.ReadFile(path)
	if err != nil {
		t.Fatal(err)
	}
	sheet, err := ParseStylesheetFile(path, contents)
	if err != nil {
		t.Fatal(err)
	}
	var t2 *StyleEntry
	for i := range sheet.Entries {
		if sheet.Entries[i].Name == "Final/Tattoos/T2" {
			t2 = &sheet.Entries[i]
		}
	}
	if t2 == nil || t2.Props.Combination == nil || len(*t2.Props.Combination) != 2 {
		t.Fatalf("Final/Tattoos/T2 not read: %+v", t2)
	}
	props := t2.Props
	props.FontSize = intPtr(41)
	props.Minimap = &MinimapProps{Shape: stringPtr("Kite")}
	shape := "Star"
	newProps := StyleProps{Combination: &[]string{"Templates/Tiers/Mid/T6", "Templates/Leagues/Ancestors"}, BorderColor: stringPtr("#FF0000"), Minimap: &MinimapProps{Shape: &shape}}
	rendered, err := ApplyStylesheetChanges(string(contents), []StyleChange{
		{Name: "Final/Tattoos/T2", Props: &props},
		{Name: "Final/Tattoos/T7", Props: &newProps},
		{Name: "Final/Tattoos/Unpriced", Delete: true},
	}, []DistinctChange{{DistinctEntry: DistinctEntry{Name: "Tattoos", Min: 8, Vars: []string{"bg-ancestral", "bg-sea"}}}})
	if err != nil {
		t.Fatal(err)
	}
	removed, added := changedLines(string(contents), rendered)
	wantRemoved := []string{"[Final.Tattoos.Unpriced]", `Combination = ["Templates/Tiers/High/T4", "Templates/Leagues/Ancestors"]`}
	wantAdded := []string{"FontSize = 41", `Minimap = { Shape = "Kite" }`,
		"[Final.Tattoos.T7]", `Combination = ["Templates/Tiers/Mid/T6", "Templates/Leagues/Ancestors"]`, `BorderColor = "#FF0000"`, `Minimap = { Shape = "Star" }`,
		"", "", "[Studio.Distinct.Tattoos]", "Min = 8", `Vars = ["bg-ancestral", "bg-sea"]`}
	if strings.Join(sorted(removed), "|") != strings.Join(sorted(wantRemoved), "|") || strings.Join(sorted(added), "|") != strings.Join(sorted(wantAdded), "|") {
		t.Fatalf("unexpected changes:\n-%q\n+%q", removed, added)
	}
	if !strings.Contains(rendered, "[Final.Tattoos.T6]\nCombination = [\"Templates/Tiers/Mid/T6\", \"Templates/Leagues/Ancestors\"]\n\n[Final.Tattoos.T7]") {
		t.Fatal("the new style should sit after its siblings")
	}
	theme, err := ReadThemeFile(filepath.Join(root, "configuration", "styles", "aryzel-2-first-whisper.toml"))
	if err != nil {
		t.Fatal(err)
	}
	loaded, err := ParseStylesheet(path, []byte(rendered), theme.Palette())
	if err != nil {
		t.Fatal(err)
	}
	if style := loaded.Styles["Final/Tattoos/T2"]; style.FontSize == nil || *style.FontSize != 41 {
		t.Fatalf("edited style reads back wrong: %+v", style)
	}
	if _, ok := loaded.Styles["Final/Tattoos/Unpriced"]; ok {
		t.Fatal("deleted style still present")
	}
}

func repositoryRoot(t *testing.T) string {
	t.Helper()
	return filepath.Join("..", "..", "..", "..")
}
