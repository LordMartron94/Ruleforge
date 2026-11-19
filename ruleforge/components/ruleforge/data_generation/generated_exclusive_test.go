package data_generation

import (
	"fmt"
	"os"
	"path/filepath"
	"runtime"
	"strings"
	"testing"

	lua "github.com/yuin/gopher-lua"
)

func TestSelectiveExclusiveModsDoNotCompileUnusedDefinitions(t *testing.T) {
	path := filepath.Join(t.TempDir(), "ModItemExclusive.lua")
	var source strings.Builder
	source.WriteString("return {\n")
	for i := 0; i < 50000; i++ {
		fmt.Fprintf(&source, "  [\"Unused%05d\"] = { value = { 1, 2, 3 }, label = \"not needed { \\\" }\" },\n", i)
	}
	source.WriteString("  [\"StaffBlockPercentImplicitStaff2\"] = { \"Block { }\", { value = 2 } },\n")
	source.WriteString("  LocalVeiledModEffectUnique__1 = { [=[Effect { }]=] },\n")
	source.WriteString("}\n")
	if err := os.WriteFile(path, []byte(source.String()), 0644); err != nil {
		t.Fatal(err)
	}

	L := lua.NewState()
	defer L.Close()
	runtime.GC()
	var before, after runtime.MemStats
	runtime.ReadMemStats(&before)
	mods, err := loadRequiredExclusiveMods(L, path, []string{"LocalVeiledModEffectUnique__1", "StaffBlockPercentImplicitStaff2"})
	runtime.ReadMemStats(&after)
	if err != nil {
		t.Fatal(err)
	}
	if mods.Len() != 0 || mods.RawGetString("Unused00001") != lua.LNil {
		t.Fatal("unexpected unused entries loaded")
	}
	if got := mods.RawGetString("StaffBlockPercentImplicitStaff2").(*lua.LTable).RawGetInt(1).String(); got != "Block { }" {
		t.Fatalf("wrong unmodified block value: %q", got)
	}
	if got := mods.RawGetString("LocalVeiledModEffectUnique__1").(*lua.LTable).RawGetInt(1).String(); got != "Effect { }" {
		t.Fatalf("wrong unmodified effect value: %q", got)
	}
	// Includes reading the large source into Go memory. Lua should only compile
	// two entries, not the 50,000 unrelated modifier definitions.
	if allocated := after.TotalAlloc - before.TotalAlloc; allocated > 32<<20 {
		t.Fatalf("selective loading allocated %d MiB, expected <=32 MiB", allocated>>20)
	}
	t.Logf("selective loading allocated %d MiB", (after.TotalAlloc-before.TotalAlloc)>>20)
}

func TestSelectiveExclusiveModsFailOnMissingRequiredKey(t *testing.T) {
	path := filepath.Join(t.TempDir(), "ModItemExclusive.lua")
	if err := os.WriteFile(path, []byte("return { Other = {} }"), 0644); err != nil {
		t.Fatal(err)
	}
	L := lua.NewState()
	defer L.Close()
	if _, err := loadRequiredExclusiveMods(L, path, []string{"Missing"}); err == nil || !strings.Contains(err.Error(), "Missing") {
		t.Fatalf("expected explicit missing-key error, got %v", err)
	}
}

func TestExclusiveReferencesRejectDynamicAccess(t *testing.T) {
	path := filepath.Join(t.TempDir(), "Generated.lua")
	if err := os.WriteFile(path, []byte(`data.itemMods.ItemExclusive[dynamicKey]`), 0644); err != nil {
		t.Fatal(err)
	}
	if _, err := referencedExclusiveKeys(path); err == nil {
		t.Fatal("dynamic lookup must not silently discard dependencies")
	}
	if err := os.WriteFile(path, []byte(`data.itemMods.ItemExclusive['StaffBlockPercentImplicitStaff2']`), 0644); err != nil {
		t.Fatal(err)
	}
	keys, err := referencedExclusiveKeys(path)
	if err != nil || len(keys) != 1 || keys[0] != "StaffBlockPercentImplicitStaff2" {
		t.Fatalf("literal bracket lookup: keys=%q error=%v", keys, err)
	}
}
