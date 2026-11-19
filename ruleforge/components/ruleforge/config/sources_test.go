package config

import (
	"path/filepath"
	"reflect"
	"testing"
)

func TestStylesheetSourcesFollowCombinationPrecedence(t *testing.T) {
	path := filepath.Join(t.TempDir(), "sheet.toml")
	writeFile(t, path, `
[Templates.Group]
BackgroundColor = "bg"
Minimap = { Shape = "Circle" }
DropSound = "group.mp3"

[Templates.Tier]
FontSize = 40
TextColor = "t2"
Minimap = { Color = "Orange", Size = 1 }
DropSound = "tier.mp3"

[Final.Orb]
Combination = ["Templates/Group", "Templates/Tier"]
BorderColor = "#FFFFFF"
`)
	sheet, err := LoadStylesheet(path, map[string]string{"bg": "#101010", "t2": "#FFD700"})
	if err != nil {
		t.Fatal(err)
	}
	source := sheet.Sources["Final/Orb"]
	if !reflect.DeepEqual(source.Combination, []string{"Templates/Group", "Templates/Tier"}) {
		t.Errorf("combination: %v", source.Combination)
	}
	if !reflect.DeepEqual(source.Own, []string{"border"}) {
		t.Errorf("own channels: %v", source.Own)
	}
	want := map[string]string{
		"background": "Templates/Group", "minimapShape": "Templates/Group",
		"text": "Templates/Tier", "fontSize": "Templates/Tier", "minimapColor": "Templates/Tier", "minimapSize": "Templates/Tier",
		"sound":  "Templates/Tier", // the later template wins the sound
		"border": "Final/Orb",
	}
	if !reflect.DeepEqual(source.Channels, want) {
		t.Errorf("channel owners:\n got %v\nwant %v", source.Channels, want)
	}
}

func TestPaletteGroupsFollowTheFilesCommentHeadings(t *testing.T) {
	contents := `# Style heading, outside the palette
Name = "X"

[Palette]
# Tier text, brightest first.
# (a second comment line is not a new group)
t2 = "#FFFFFF"
t3 = "#EEEEEE"

# Fields
bg-a = "#000000"
bg-b = "#111111"
`
	palette := map[string]string{"t2": "", "t3": "", "bg-a": "", "bg-b": "", "extra": ""}
	got := paletteGroups(contents, palette)
	want := []PaletteGroup{
		{Title: "Tier text, brightest first.", Vars: []string{"t2", "t3"}},
		{Title: "Fields", Vars: []string{"bg-a", "bg-b"}},
		{Title: "Other", Vars: []string{"extra"}},
	}
	if !reflect.DeepEqual(got, want) {
		t.Errorf("groups:\n got %+v\nwant %+v", got, want)
	}
}
