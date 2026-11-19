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
		`const pool = { equipment: [{ name: 'Light Brigandine', class: 'Body Armours', drop: 8, w: 2, h: 3, armour: 63, armourMin: 48, evasion: 63, evasionMin: 48 }], gems: [], cards: [], tattoos: [], stackables: [], uniques: [] };`,
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

func TestPastedTattooKeepsItsNameAsTheBaseType(t *testing.T) {
	node, err := exec.LookPath("node")
	if err != nil {
		t.Skip("node is not installed")
	}
	script := strings.Join([]string{
		`const state = { area: 40 };`,
		`const STACKABLE_CLASS = {};`,
		`const pool = { equipment: [], gems: [], cards: [], tattoos: [{ name: 'Tattoo of the Valako Shieldbearer', value: 494 }], stackables: [], uniques: [] };`,
		section(t, "// Classes whose base defences roll within a range", "function rollDefences("),
		section(t, "/* ---------- pasted Path of Exile items ---------- */", "/* ---------- rendering: shared ---------- */"),
		`const paste = name => parseGameItem('Item Class: Stackable Currency\nRarity: Currency\n' + name + '\n--------\nStack Size: 1/10');`,
		`const items = ['Tattoo of the Valako Shieldbearer', 'Honoured Tattoo of the Flood'].map(paste);`,
		`console.log(JSON.stringify(items.map(i => ({ base: i.base, cls: i.cls, value: i.value || 0 }))));`,
	}, "\n")
	out, err := exec.Command(node, "-e", script).CombinedOutput()
	if err != nil {
		t.Fatalf("node: %v\n%s", err, out)
	}
	var got []struct {
		Base, Cls string
		Value     float64
	}
	if err := json.Unmarshal(out, &got); err != nil {
		t.Fatalf("parsing node output %q: %v", out, err)
	}
	if len(got) != 2 || got[0].Base != "Tattoo of the Valako Shieldbearer" || got[0].Cls != "Stackable Currency" || got[0].Value != 494 {
		t.Errorf("priced tattoo: %+v", got)
	}
	if len(got) == 2 && (got[1].Base != "Honoured Tattoo of the Flood" || got[1].Cls != "Stackable Currency") {
		t.Errorf("a tattoo missing from the pool must still match filter rules by name: %+v", got[1])
	}
}

func TestCorruptedModsIsUnknownOnlyForPastedCorruptedItems(t *testing.T) {
	node, err := exec.LookPath("node")
	if err != nil {
		t.Skip("node is not installed")
	}
	script := strings.Join([]string{
		section(t, "/* ---------- filter evaluation ---------- */", "/* ---------- pasted Path of Exile items ---------- */"),
		`const c = { k: 'CorruptedMods', op: '>=', v: ['1'] };`,
		`console.log(JSON.stringify([`,
		`  evalCondition({ corrupted: false }, c),`,
		`  evalCondition({ corrupted: true, corruptedMods: 1 }, c),`,
		`  evalCondition({ corrupted: true, corruptedMods: 0 }, c),`,
		`  evalCondition({ corrupted: true }, c),`,
		`]));`,
	}, "\n")
	out, err := exec.Command(node, "-e", script).CombinedOutput()
	if err != nil {
		t.Fatalf("node: %v\n%s", err, out)
	}
	if got := strings.TrimSpace(string(out)); got != "[false,true,false,null]" {
		t.Fatalf("uncorrupted, one implicit, none, pasted = %s; want [false,true,false,null]", got)
	}
}

func TestTieredUnidentifiedPasteAndExplicitModifierCount(t *testing.T) {
	node, err := exec.LookPath("node")
	if err != nil {
		t.Skip("node not installed")
	}
	script := strings.Join([]string{
		`const state={area:80}; const STACKABLE_CLASS={}; const pool={equipment:[{name:'Helical Ring',class:'Rings',drop:1,w:1,h:1}],gems:[],cards:[],tattoos:[],stackables:[],uniques:[]};`,
		section(t, "// Classes whose base defences roll within a range", "function rollDefences("),
		section(t, "/* ---------- filter evaluation ---------- */", "/* ---------- pasted Path of Exile items ---------- */"),
		section(t, "/* ---------- pasted Path of Exile items ---------- */", "/* ---------- rendering: shared ---------- */"),
		`const item=parseGameItem('Item Class: Rings\nRarity: Magic\nHelical Ring\n--------\nItem Level: 80\n--------\nUnidentified (Tier 5)\nFated');`,
		`const results=[item.identified,item.itemTier,item.fated,evalCondition(item,{k:'UnidentifiedItemTier',op:'>=',v:['4']})];`,
		`const map=parseGameItem('Item Class: Maps\nRarity: Rare\nMap\n--------\nItem Level: 83\n--------\n{ Suffix Modifier "of Zarokh" (Tier: 1) }\nEncounter text\nMore text');`,
		`results.push(evalCondition(map,{k:'HasExplicitMod',op:'==',v:['of Zarokh']}),evalCondition(map,{k:'HasExplicitMod',op:'>=',v:['7','a','e','i','o','u','y']}));`,
		`console.log(JSON.stringify(results));`,
	}, "\n")
	output, err := exec.Command(node, "-e", script).CombinedOutput()
	if err != nil {
		t.Fatalf("node: %v\n%s", err, output)
	}
	if strings.TrimSpace(string(output)) != `[false,5,true,true,true,false]` {
		t.Fatalf("incorrect tier or explicit-mod semantics: %s", output)
	}
}

func TestStackableClassesDistinguishVisceraThreadsAndMapEnchantments(t *testing.T) {
	cases := map[string]string{"Wickerman|Warden's Arm": "Viscera", "Threads|Abyss Thread": "Map Fragments", "Threads|Frayed Thread": "Stackable Currency", "Map Enchantments|Divine Vessel": "Stackable Currency"}
	for key, want := range cases {
		parts := strings.SplitN(key, "|", 2)
		if got := StackableClass(parts[0], parts[1]); got != want {
			t.Errorf("%s: %s, wanted %s", key, got, want)
		}
	}
}
