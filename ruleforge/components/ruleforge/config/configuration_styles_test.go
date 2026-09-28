package config

import (
	"path/filepath"
	"strings"
	"testing"
)

// Exercise the shipped combinations through the real resolver: changing an
// inheritance order must not silently change the meaning of a visual channel.
func TestEnabledThemesKeepGlobalPriorityChannels(t *testing.T) {
	definitions, err := LoadStyleDefinitions(filepath.Join("..", "..", "..", "..", "configuration", "styles"))
	if err != nil {
		t.Fatal(err)
	}
	for _, definition := range definitions {
		if !definition.IsEnabled() {
			continue
		}
		t.Run(definition.ID, func(t *testing.T) {
			styles, err := LoadStyles(definition.Stylesheet, definition.Palette)
			if err != nil {
				t.Fatal(err)
			}
			for name, final := range styles {
				if !(strings.HasPrefix(name, "Final/") || strings.Contains(name, "/Final/")) || final.FontSize == nil {
					continue // Border-only property annotations are not complete labels.
				}
				var tier *Style
				for tierName, candidate := range styles {
					if strings.HasPrefix(tierName, "Templates/Tiers/") && intPtrIsEqual(final.FontSize, candidate.FontSize) && final.TextColor.IsEqual(candidate.TextColor) {
						tier = &candidate
						break
					}
				}
				if tier == nil {
					t.Errorf("%s: text and size do not identify a global priority tier", name)
					continue
				}
				if final.Minimap == nil || tier.Minimap == nil || !intPtrIsEqual(final.Minimap.Size, tier.Minimap.Size) || !stringPtrIsEqual(final.Minimap.Color, tier.Minimap.Color) {
					t.Errorf("%s: minimap size/colour disagree with text priority", name)
				}
				if !strings.HasPrefix(name, "Final/Crafting/") && !final.Beam.IsEqual(tier.Beam) {
					t.Errorf("%s: beam disagrees with text priority", name)
				}
				if *tier.FontSize >= 40 && (!stringPtrIsEqual(final.DropSound, tier.DropSound) || !intPtrIsEqual(final.DropVolume, tier.DropVolume)) {
					t.Errorf("%s: category sound overrides a T0-T2 priority alert", name)
				}
			}
			normal, magic := styles["Final/Crafting/Normal"], styles["Final/Crafting/Magic"]
			if !normal.IsEqual(&magic) || normal.BorderColor == nil || normal.BorderColor.Var != "bd-crafting" {
				t.Error("crafting rarity changed its priority or displaced its distinctive border")
			}
			for _, rarity := range []string{"Normal", "Magic", "Rare"} {
				logbook := styles["Final/Leagues/Expedition/LogBook"+rarity]
				league := styles["Templates/Leagues/Expedition"]
				if !logbook.BorderColor.IsEqual(league.BorderColor) {
					t.Errorf("%s logbook: rarity overrides league identity", rarity)
				}
			}
		})
	}
}

func TestSocketStylesRemainSecondaryToHiddenItemState(t *testing.T) {
	definitions, err := LoadStyleDefinitions(filepath.Join("..", "..", "..", "..", "configuration", "styles"))
	if err != nil {
		t.Fatal(err)
	}
	wantFontSize := map[string]int{
		"Final/Special/RGB":              22,
		"Final/Special/RGB-4-socket":     22,
		"Final/Special/Sockets/3-link":   22,
		"Final/Special/Sockets/4-link":   24,
		"Final/Special/Sockets/5-link":   26,
		"Final/Special/Sockets/6-link":   28,
		"Final/Special/Sockets/4-socket": 24,
		"Final/Special/Sockets/5-socket": 26,
		"Final/Special/Sockets/6-socket": 28,
	}
	for _, definition := range definitions {
		if !definition.IsEnabled() {
			continue
		}
		t.Run(definition.ID, func(t *testing.T) {
			styles, err := LoadStyles(definition.Stylesheet, definition.Palette)
			if err != nil {
				t.Fatal(err)
			}
			corrupted := styles["Final/Equipment/Corrupted"]
			fractured := styles["Final/Equipment/Fractured"]
			maxRolled := styles["Final/Equipment/MaxRolled"]
			if corrupted.FontSize == nil || fractured.FontSize == nil {
				t.Fatal("hidden item state styles must define a priority size")
			}
			if maxRolled.FontSize == nil {
				t.Fatal("max-rolled style must define a priority size")
			}
			for name, want := range wantFontSize {
				style, ok := styles[name]
				if !ok {
					t.Fatalf("missing socket/link style %s", name)
				}
				if style.FontSize == nil || *style.FontSize != want {
					t.Errorf("%s: got font size %v, want %d", name, style.FontSize, want)
				}
				if style.BorderColor == nil {
					t.Errorf("%s: socket/link mark lost its border", name)
				}
				if style.Beam != nil || style.DropSound != nil || style.DropVolume != nil {
					t.Errorf("%s: visible socket/link data added a beam or alert", name)
				}
				if style.FontSize != nil && *style.FontSize >= *corrupted.FontSize {
					t.Errorf("%s: socket/link priority %d is not below corrupted state %d", name, *style.FontSize, *corrupted.FontSize)
				}
				if style.FontSize != nil && *style.FontSize >= *fractured.FontSize {
					t.Errorf("%s: socket/link priority %d is not below fractured state %d", name, *style.FontSize, *fractured.FontSize)
				}
				if style.FontSize != nil && *style.FontSize >= *maxRolled.FontSize {
					t.Errorf("%s: socket/link priority %d is not below max-rolled state %d", name, *style.FontSize, *maxRolled.FontSize)
				}
			}
		})
	}
}
