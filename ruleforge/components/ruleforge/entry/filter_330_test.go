package main

import (
	"bytes"
	"encoding/json"
	"io"
	"log"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/config"
	data "github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/data_generation"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/data_generation/model"
)

// Compile the actual shipped scripts and resolve their first matching block
// with Studio's actual evaluator. All outputs go to the test directory.
func TestShippedFiltersProtectTieredDropsAndClassify330Content(t *testing.T) {
	node, err := exec.LookPath("node")
	if err != nil {
		t.Skip("node not installed")
	}
	root, err := filepath.Abs("../../../..")
	if err != nil {
		t.Fatal(err)
	}
	cfg, err := config.NewConfigurationLoader().LoadConfiguration(filepath.Join(root, "config.toml"))
	if err != nil {
		t.Fatal(err)
	}
	cfg.FilterOutputDirs = []string{t.TempDir()}
	cfg.StudioFile = ""
	a := &App{config: cfg, log: log.New(io.Discard, "", 0), leagueWeights: cfg.GetLeagueWeights()}
	if err := a.loadStyles(); err != nil {
		t.Fatal(err)
	}
	// Real cached bases improve local coverage; the small fixture keeps CI
	// self-contained and includes the builds' weapon classes and a low base.
	var cached data.ItemCacheModel
	if cachePath := os.Getenv("RULEFORGE_TEST_ITEM_CACHE"); cachePath != "" {
		raw, err := os.ReadFile(cachePath)
		if err != nil {
			t.Fatal(err)
		}
		if err := json.Unmarshal(raw, &cached); err != nil {
			t.Fatal(err)
		}
	}

	a.itemBases = cached.Items
	a.gems = cached.Gems
	a.essences = cached.Essences
	if len(a.itemBases) == 0 {
		level := 8
		for _, base := range []struct{ name, kind, sub string }{{"Iron Greaves", "Boots", "Armour"}, {"Driftwood Sceptre", "Sceptre", ""}, {"Driftwood Wand", "Wand", ""}, {"Rusted Sword", "One Handed Sword", ""}} {
			a.itemBases = append(a.itemBases, model.ItemBase{Name: base.name, Type: base.kind, SubType: base.sub, DropLevel: &level})
		}
	}
	now := time.Now().UTC()
	a.economyData = map[string][]data.EconomySnapshot{}
	for league := range cfg.LeagueWeights {
		a.economyData[league] = []data.EconomySnapshot{{FetchedAt: now, Items: []data.EconomyCacheItem{
			{Class: "Divination Cards", Name: "The Doctor", BaseType: "The Doctor", ChaosValue: 500, Source: "exchange"},
			{Class: "Divination Cards", Name: "The Gambler", BaseType: "The Gambler", ChaosValue: 0.1, Source: "exchange"},
			{Class: "Currency", Name: "Chaos Orb", BaseType: "Chaos Orb", ChaosValue: 1, Source: "exchange"},
			{Class: "Currency", Name: "Divine Orb", BaseType: "Divine Orb", ChaosValue: 200, Source: "exchange"},
		}}}
	}
	a.prepareBaseTypes()
	// CSV names are authoritative for configured non-PoB drops in this fixture.
	for _, style := range a.styles {
		rows, err := config.NewBaseTypeAutomationLoader(style.BaseTypes).Load()
		if err != nil {
			t.Fatal(err)
		}
		for _, row := range *rows {
			a.baseTypes = append(a.baseTypes, row.BaseType)
		}
	}
	previous := log.Writer()
	log.SetOutput(io.Discard)
	t.Cleanup(func() { log.SetOutput(previous) })
	filters, err := a.compile(compileOptions{write: true})
	if err != nil {
		t.Fatal(err)
	}
	if len(filters) != 5 {
		t.Fatalf("expected four Aryzel phases and General, got %d filters", len(filters))
	}
	type sample struct {
		Base       string `json:"base"`
		Class      string `json:"cls"`
		Rarity     string `json:"rarity"`
		Area       int    `json:"area"`
		Level      int    `json:"ilvl"`
		Drop       int    `json:"drop"`
		Tier       int    `json:"itemTier"`
		Corrupted  bool   `json:"corrupted"`
		Fractured  bool   `json:"fractured"`
		Fated      bool   `json:"fated"`
		Identified bool   `json:"identified"`
		Stack      int    `json:"stack"`
		Want       string `json:"want"`
	}
	cases := []sample{
		{Base: "Iron Greaves", Class: "Boots", Rarity: "Magic", Area: 50, Level: 50, Drop: 8, Tier: 4, Want: "Tier4"},
		{Base: "Helical Ring", Class: "Rings", Rarity: "Magic", Area: 80, Level: 80, Drop: 1, Tier: 5, Want: "Tier5"},
		{Base: "Simplex Amulet", Class: "Amulets", Rarity: "Magic", Area: 80, Level: 80, Drop: 1, Tier: 5, Want: "Tier5"},
		{Base: "Driftwood Sceptre", Class: "Sceptres", Rarity: "Rare", Area: 80, Level: 80, Drop: 1, Tier: 5, Fated: true, Want: "Fated property over Final/Equipment/Tier"},
		{Base: "Helical Ring", Class: "Rings", Rarity: "Rare", Area: 80, Level: 80, Drop: 1, Tier: 5, Fractured: true, Fated: true, Want: "Fractured property over Final/Equipment/Tier5"},
		{Base: "New Unknown Arm", Class: "Viscera", Rarity: "Normal", Area: 50, Level: 50, Want: "Wickerman/Unclassified"},
		{Base: "Essence of Trickery", Class: "Stackable Currency", Rarity: "Normal", Area: 80, Stack: 1, Want: "EssenceOfTrickery"},
		{Base: "Essence of Desolation", Class: "Stackable Currency", Rarity: "Normal", Area: 80, Stack: 1, Want: "EssenceOfDesolation"},
		{Base: "Allflame Ember of the Divine", Class: "Stackable Currency", Rarity: "Normal", Area: 80, Stack: 1, Want: "MapEnchantments/T1"},
		{Base: "Cursed Poppet", Class: "Map Fragments", Rarity: "Normal", Area: 80, Stack: 1, Want: "MapFragments/Final/"},
	}
	payload, _ := json.Marshal(map[string]any{"filters": filters, "cases": cases})
	html, err := os.ReadFile(filepath.Join(root, "ruleforge/components/ruleforge/entry/studio/studio.html"))
	if err != nil {
		t.Fatal(err)
	}
	text := string(html)
	start := strings.Index(text, "/* ---------- filter evaluation ---------- */")
	end := strings.Index(text, "/* ---------- pasted Path of Exile items ---------- */")
	script := text[start:end] + `
 const input=JSON.parse(require('fs').readFileSync(0,'utf8'));
 for(const filter of input.filters) for(const sample of input.cases){
  const item={...sample,w:2,h:2,quality:0,corruptedMods:0,defencePercentile:50,sockets:[],links:[],mods:[]};
  const first=matchingBlocks(item,filter.blocks).matches[0];
  if(!first||first.action!=='Show'||!first.style.includes(sample.want)) throw new Error(filter.file+': '+sample.base+' => '+JSON.stringify(first&&{action:first.action,style:first.style,section:first.section}));
 }
 console.log('Verified '+input.cases.length+' drops across '+input.filters.length+' filters');`
	cmd := exec.Command(node, "-e", script)
	cmd.Stdin = bytes.NewReader(payload)
	output, err := cmd.CombinedOutput()
	if err != nil {
		t.Fatalf("filter behavior: %v\n%s", err, output)
	}
	t.Log(string(output))
}
