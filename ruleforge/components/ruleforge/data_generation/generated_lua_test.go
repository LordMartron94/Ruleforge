package data_generation

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func TestGeneratedUniquesLoadsRealModules(t *testing.T) {
	root := t.TempDir()
	fixtures := map[string]string{
		"ModVeiled.lua":     `return { TestVeiled = { weightKey = { "weapon" }, weightVal = { 10 }, type = "Prefix", "Test stat" } }`,
		"ClusterJewels.lua": `return { notableSortOrder = { TestNotable = 1 } }`,
		"Gems.lua": `return {
			["active"] = { name = "Active Gem", grantedEffectId = "Active", tags = { grants_active_skill = true } },
			["support"] = { name = "Support Gem", grantedEffectId = "Support", tags = { support = true } },
		}`,
		"ModItemExclusive.lua": `return {
    StaffBlockPercentImplicitStaff2 = { "Block" },
    LocalVeiledModEffectUnique__1 = { "Effect" },
}`,
		"Uniques/Special/WatchersEye.lua":    `return { B = { "Mod B" }, A = { "Mod A" } }`,
		"Uniques/Special/BoundByDestiny.lua": `return { Test = { type = "2Elder", "Destiny stat" } }`,
		"ModJewelCharm.lua":                  `return { Charm2 = { "Charm stat" } }`,
		"Uniques/Special/Generated.lua": `
			data.uniques.generated = {}
			local d = LoadModule("Data/Uniques/Special/BoundByDestiny")
			local c = LoadModule("Data/ModJewelCharm")
			LoadModule("Modules/CalcTools")
			assert(data.veiledMods.TestVeiled[1] == "Test stat")
			assert(data.clusterJewels.notableSortOrder.TestNotable == 1)
			assert(data.itemMods.ItemExclusive.StaffBlockPercentImplicitStaff2[1] == "Block")
			assert(data.uniqueMods["Watcher's Eye"][1].Id == "A")
			assert(d.Test[1] == "Destiny stat" and c.Charm2[1] == "Charm stat")
			assert(isValueInTable({"a", "b"}, "b") == 2)
			assert(isValueInArray({"a", "b"}, "b"))
			for _, gem in pairs(data.gems) do
				if calcLib.gemIsType(gem, "active skill", false) and calcLib.gemIsType(gem, "non-vaal", false) then
					table.insert(data.uniques.generated, "Test Generated\nTest Base\n" .. gem.name)
				end
			end
		`,
	}
	for relative, source := range fixtures {
		path := filepath.Join(root, filepath.FromSlash(relative))
		if err := os.MkdirAll(filepath.Dir(path), 0755); err != nil {
			t.Fatal(err)
		}
		if err := os.WriteFile(path, []byte(source), 0644); err != nil {
			t.Fatal(err)
		}
	}
	items, err := NewLuaExecutor().ExecuteGeneratedUniques(filepath.Join(root, "Uniques", "Special", "Generated.lua"))
	if err != nil {
		t.Fatal(err)
	}
	if len(items) != 1 || !strings.Contains(items[0], "Active Gem") {
		t.Fatalf("unexpected generated uniques: %q", items)
	}
}
