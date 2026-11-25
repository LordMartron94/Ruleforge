package provider

import (
	"context"
	"encoding/json"
	"github.com/LordMartron94/Ruleforge/studio/internal/config"
	"os"
	"path/filepath"
	"testing"
)

func TestPoBPathSnapshotsAndFailedRefresh(t *testing.T) {
	dir := t.TempDir()
	pob := filepath.Join(dir, "pob")
	os.MkdirAll(filepath.Join(pob, "Bases"), 0o755)
	os.MkdirAll(filepath.Join(pob, "Uniques"), 0o755)
	files := map[string]string{
		"Bases/wand.lua":   `return function(itemBases) itemBases["Driftwood Wand"]={type="Wand",socketLimit=3,req={level=1},tags={wand=true}} end`,
		"Gems.lua":         `return {Fireball={name="Fireball",tags={intelligence=true}}}`,
		"Uniques/wand.lua": "return {[[Lifesprig\nDriftwood Wand\nVariant: test]]}",
	}
	for path, text := range files {
		if err := os.WriteFile(filepath.Join(pob, path), []byte(text), 0o644); err != nil {
			t.Fatal(err)
		}
	}
	levels := filepath.Join(dir, "levels.json")
	os.WriteFile(levels, []byte(`{"Driftwood Wand":3}`), 0o644)
	project := &config.Project{Dir: dir, GameData: &config.GameData{PoBPath: "pob", DropLevelsPath: "levels.json"}}
	if err := PrepareGameData(context.Background(), project); err != nil {
		t.Fatal(err)
	}
	paths := GamePaths(project)
	first, err := os.ReadFile(paths["game.bases"])
	if err != nil {
		t.Fatal(err)
	}
	var rows []GameRow
	if err := json.Unmarshal(first, &rows); err != nil {
		t.Fatal(err)
	}
	if len(rows) != 1 || rows[0].Class != "Wands" || rows[0].DropLevel == nil || *rows[0].DropLevel != 3 {
		t.Fatalf("normalized bases: %+v", rows)
	}
	for _, kind := range []string{"game.gems", "game.uniques"} {
		data, err := os.ReadFile(paths[kind])
		if err != nil {
			t.Fatal(err)
		}
		if json.Unmarshal(data, &rows) != nil || len(rows) != 1 {
			t.Fatalf("bad %s: %s", kind, data)
		}
	}
	os.WriteFile(filepath.Join(pob, "Gems.lua"), []byte(`return false`), 0o644)
	if err := PrepareGameData(context.Background(), project); err == nil {
		t.Fatal("invalid refresh succeeded")
	}
	if GamePaths(project)["game.bases"] != paths["game.bases"] {
		t.Fatal("failed refresh replaced generation")
	}
	after, _ := os.ReadFile(paths["game.bases"])
	if string(first) != string(after) {
		t.Fatal("failed refresh destroyed last valid snapshot")
	}
}
func TestLuaProviderCannotUseFileOrProcessAPIs(t *testing.T) {
	path := filepath.Join(t.TempDir(), "data.lua")
	os.WriteFile(path, []byte(`return os.execute("false")`), 0o644)
	if err := readPoBTable(context.Background(), path, false, nil); err == nil {
		t.Fatal("Lua process API was available")
	}
}

func TestPoBEquipmentSubtypesAndSupportNames(t *testing.T) {
	dir := t.TempDir()
	os.MkdirAll(filepath.Join(dir, "Bases"), 0o755)
	source := `return function(b)
      b["Small Life Flask"]={type="Flask",subType="Life",req={level=1}}
      b["Small Mana Flask"]={type="Flask",subType="Mana",req={level=1}}
      b["Small Hybrid Flask"]={type="Flask",subType="Hybrid",req={level=1}}
      b["Quicksilver Flask"]={type="Flask",req={level=4}}
      b["Rusted Spike"]={type="One Handed Sword",subType="Thrusting",req={level=3}}
      b["Plate Vest"]={type="Body Armour",subType="Armour",req={level=1}}
    end`
	os.WriteFile(filepath.Join(dir, "Bases/test.lua"), []byte(source), 0o644)
	rows, err := readPoBBases(context.Background(), dir)
	if err != nil {
		t.Fatal(err)
	}
	wanted := map[string]string{"Small Life Flask": "Life Flasks", "Small Mana Flask": "Mana Flasks", "Small Hybrid Flask": "Hybrid Flasks", "Quicksilver Flask": "Utility Flasks", "Rusted Spike": "Thrusting One Hand Swords", "Plate Vest": "Body Armours"}
	for _, row := range rows {
		if row.Class != wanted[row.BaseType] {
			t.Fatalf("incorrect item class: %+v", row)
		}
		delete(wanted, row.BaseType)
		if row.BaseType == "Plate Vest" && row.SubType != "Armour" {
			t.Fatal("armour subtype lost")
		}
	}
	if len(wanted) != 0 {
		t.Fatal("missing bases", wanted)
	}
	os.WriteFile(filepath.Join(dir, "Gems.lua"), []byte(`return {AddedFireDamage={name="Added Fire Damage",baseTypeName="",support=true,tags={support=true}},Fireball={name="Fireball",baseTypeName="Fireball",tags={spell=true}}}`), 0o644)
	rows, err = readPoBGems(context.Background(), dir)
	if err != nil {
		t.Fatal(err)
	}
	wanted = map[string]string{"Added Fire Damage Support": "Support Gems", "Fireball": "Skill Gems"}
	for _, row := range rows {
		if row.Class != wanted[row.BaseType] {
			t.Fatalf("incorrect gem: %+v", row)
		}
		delete(wanted, row.BaseType)
	}
	if len(wanted) != 0 {
		t.Fatal("missing gems", wanted)
	}
}

func TestPoBSubtypeNamesAreNotEmittedAsGameBaseNames(t *testing.T) {
	dir := t.TempDir()
	os.MkdirAll(filepath.Join(dir, "Bases"), 0o755)
	os.MkdirAll(filepath.Join(dir, "Uniques"), 0o755)
	os.WriteFile(filepath.Join(dir, "Bases/boots.lua"), []byte(`return function(b)
      b["Two-Toned Boots (Armour/Energy Shield)"]={type="Boots",subType="Armour/Energy Shield",req={level=70}}
      b["Two-Toned Boots (Armour/Evasion)"]={type="Boots",subType="Armour/Evasion",req={level=70}}
    end`), 0o644)
	os.WriteFile(filepath.Join(dir, "Uniques/boots.lua"), []byte("return {[[Fixture Unique\nTwo-Toned Boots (Armour/Energy Shield)]]}"), 0o644)
	bases, err := readPoBBases(context.Background(), dir)
	if err != nil {
		t.Fatal(err)
	}
	if len(bases) != 2 {
		t.Fatal(bases)
	}
	for _, base := range bases {
		if base.BaseType != "Two-Toned Boots" || base.Name != "Two-Toned Boots" || base.ID == base.BaseType || base.SubType == "" {
			t.Fatalf("PoB qualifier leaked into game name: %+v", base)
		}
	}
	uniques, err := readPoBUniques(context.Background(), dir, bases)
	if err != nil {
		t.Fatal(err)
	}
	if len(uniques) != 1 || uniques[0].BaseType != "Two-Toned Boots" || uniques[0].SubType != "Armour/Energy Shield" || uniques[0].Class != "Boots" {
		t.Fatalf("unique lost normalized base metadata: %+v %v", uniques, err)
	}
}
