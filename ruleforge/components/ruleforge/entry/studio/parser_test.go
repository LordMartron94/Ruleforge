package studio

import (
	"encoding/json"
	"os/exec"
	"strings"
	"testing"
)

// pastedLightBrigandine is a real Ctrl+C copy (advanced mod view) of a
// max-rolled rare: 95 armour = (63 base + 14 flat) * 1.23.
const pastedLightBrigandine = `Item Class: Body Armours
Rarity: Rare
Woe Carapace
Light Brigandine
--------
Armour: 95 (augmented)
Evasion Rating: 111 (augmented)
--------
Requirements:
Level: 20
Str: 36
Dex: 50
--------
Sockets: W-W-W 
--------
Item Level: 19
--------
{ Prefix Modifier "Healthy" (Tier: 12) — Life }
+13(10-24) to maximum Life
{ Prefix Modifier "Pliant" (Tier: 7) — Defences, Armour, Evasion }
+14(10-27) to Armour
+27(10-27) to Evasion Rating
{ Prefix Modifier "Scrapper's" (Tier: 8) — Defences, Armour, Evasion }
23(15-26)% increased Armour and Evasion
{ Suffix Modifier "of the Seal" (Tier: 7) — Elemental, Cold, Resistance }
+13(12-17)% to Cold Resistance`

// section returns the template text between two markers.
func section(t *testing.T, from, to string) string {
	t.Helper()
	start := strings.Index(pageTemplate, from)
	end := strings.Index(pageTemplate[start+1:], to)
	if start < 0 || end < 0 {
		t.Fatalf("markers %q .. %q not found in the studio template", from, to)
	}
	return pageTemplate[start : start+1+end]
}

func TestPastedItemRecoversItsBaseDefenceRoll(t *testing.T) {
	node, err := exec.LookPath("node")
	if err != nil {
		t.Skip("node is not installed")
	}
	script := strings.Join([]string{
		`const state = { area: 19 };`,
		`const STACKABLE_CLASS = {};`,
		`const pool = { equipment: [{ name: 'Light Brigandine', class: 'Body Armours', drop: 8, w: 2, h: 3, armour: 63, armourMin: 48, evasion: 63, evasionMin: 48 }], gems: [], cards: [], stackables: [], uniques: [] };`,
		section(t, "// Classes whose base defences roll within a range", "function rollDefences("),
		section(t, "/* ---------- pasted Path of Exile items ---------- */", "/* ---------- rendering: shared ---------- */"),
		`const text = require('fs').readFileSync(0, 'utf8');`,
		`const item = parseGameItem(text);`,
		`console.log(JSON.stringify({ mods: item.mods, base: item.base, cls: item.cls, rarity: item.rarity, armour: item.armour, evasion: item.evasion, pct: item.defencePercentile, links: item.links, ilvl: item.ilvl }));`,
	}, "\n")
	cmd := exec.Command(node, "-e", script)
	cmd.Stdin = strings.NewReader(pastedLightBrigandine)
	out, err := cmd.CombinedOutput()
	if err != nil {
		t.Fatalf("node: %v\n%s", err, out)
	}
	var got struct {
		Mods              []string
		Base, Cls, Rarity string
		Armour, Evasion   int
		Pct               int
		Links             [][]string
		Ilvl              int
	}
	if err := json.Unmarshal(out, &got); err != nil {
		t.Fatalf("parsing node output %q: %v", out, err)
	}
	if got.Base != "Light Brigandine" || got.Cls != "Body Armours" || got.Rarity != "Rare" || got.Ilvl != 19 {
		t.Errorf("identity: %+v", got)
	}
	if got.Armour != 63 || got.Evasion != 63 || got.Pct != 100 {
		t.Errorf("base defences 63/63 at percentile 100 expected, got %d/%d at %d", got.Armour, got.Evasion, got.Pct)
	}
	for _, mod := range got.Mods {
		if strings.HasPrefix(mod, "Level:") || strings.HasPrefix(mod, "Str:") || strings.HasPrefix(mod, "Dex:") {
			t.Errorf("requirement line read as a mod: %q", mod)
		}
	}
	if len(got.Links) != 1 || len(got.Links[0]) != 3 {
		t.Errorf("sockets W-W-W should be one 3-link group, got %v", got.Links)
	}
}
