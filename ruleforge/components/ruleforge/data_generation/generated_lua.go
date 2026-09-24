package data_generation

import (
	"fmt"
	"path/filepath"
	"sort"
	"strings"

	lua "github.com/yuin/gopher-lua"
)

// ExecuteGeneratedUniques executes PoB's generator using data files from the
// same PoB snapshot. All returned strings are copied before the Lua VM closes.
func (e *LuaExecutor) ExecuteGeneratedUniques(generatedPath string) ([]string, error) {
	dataDir := filepath.Clean(filepath.Join(filepath.Dir(generatedPath), "..", ".."))
	keys, err := referencedExclusiveKeys(generatedPath)
	if err != nil {
		return nil, err
	}
	L := lua.NewState()
	defer L.Close()
	setupEnvironment(L)

	// These are the exact Common.lua / CalcTools.lua operations consumed by
	// Generated.lua. An upstream change to its module requests fails loudly.
	if err := L.DoString(`
        function isValueInTable(tbl, value)
            for key, candidate in pairs(tbl) do
                if candidate == value then return key end
            end
            return nil
        end
        function isValueInArray(tbl, value)
            return isValueInTable(tbl, value) ~= nil
        end
        calcLib = {}
        function calcLib.gemIsType(gem, kind, includeTransfigured)
            if kind == "active skill" then
                return gem.tags.grants_active_skill and not gem.tags.support
            elseif kind == "non-vaal" then
                return not gem.tags.vaal
            end
            error("unsupported gem type: " .. tostring(kind))
        end
    `); err != nil {
		return nil, fmt.Errorf("initialize PoB generator helpers: %w", err)
	}

	// PoB's data modules used here each return one table; no empty-table stubs.
	load := func(relative string) (*lua.LTable, error) {
		path := filepath.Join(dataDir, filepath.FromSlash(relative)+".lua")
		fn, err := L.LoadFile(path)
		if err != nil {
			return nil, fmt.Errorf("load PoB module %s: %w", relative, err)
		}
		L.Push(fn)
		if err := L.PCall(0, 1, nil); err != nil {
			return nil, fmt.Errorf("execute PoB module %s: %w", relative, err)
		}
		value := L.Get(-1)
		L.Pop(1)
		table, ok := value.(*lua.LTable)
		if !ok {
			return nil, fmt.Errorf("PoB module %s returned %s, expected table", relative, value.Type())
		}
		return table, nil
	}

	// Support only the three module requests performed by Generated.lua.
	L.SetGlobal("LoadModule", L.NewFunction(func(L *lua.LState) int {
		name := L.CheckString(1)
		if name == "Modules/CalcTools" {
			L.Push(L.GetGlobal("calcLib"))
			return 1
		}
		if name != "Data/Uniques/Special/BoundByDestiny" && name != "Data/ModJewelCharm" {
			L.RaiseError("unsupported module request %q from Generated.lua", name)
			return 0
		}
		table, err := load(strings.TrimPrefix(name, "Data/"))
		if err != nil {
			L.RaiseError("%s", err)
			return 0
		}
		L.Push(table)
		return 1
	}))

	data := L.GetGlobal("data").(*lua.LTable)
	for _, field := range []struct{ key, file string }{
		{"veiledMods", "ModVeiled"},
		{"clusterJewels", "ClusterJewels"},
		{"gems", "Gems"},
	} {
		value, err := load(field.file)
		if err != nil {
			return nil, err
		}
		data.RawSetString(field.key, value)
	}

	exclusive, err := loadRequiredExclusiveMods(L, filepath.Join(dataDir, "ModItemExclusive.lua"), keys)
	if err != nil {
		return nil, err
	}
	itemMods := L.NewTable()
	itemMods.RawSetString("ItemExclusive", exclusive)
	data.RawSetString("itemMods", itemMods)

	watchers, err := load("Uniques/Special/WatchersEye")
	if err != nil {
		return nil, err
	}
	var ids []string
	watchers.ForEach(func(key, _ lua.LValue) { ids = append(ids, key.String()) })
	sort.Strings(ids)
	ordered := L.NewTable()
	for _, id := range ids {
		entry := L.NewTable()
		entry.RawSetString("Id", lua.LString(id))
		entry.RawSetString("mod", watchers.RawGetString(id))
		ordered.Append(entry)
	}
	uniqueMods := L.NewTable()
	uniqueMods.RawSetString("Watcher's Eye", ordered)
	data.RawSetString("uniqueMods", uniqueMods)

	// Gems.lua is the authoritative game-data source for names and gem tags.
	// Generated.lua needs only name, support and plusVersionOf on the effect.
	gems := data.RawGetString("gems").(*lua.LTable)
	var gemErr error
	gems.ForEach(func(key, value lua.LValue) {
		if gemErr != nil {
			return
		}
		gem, ok := value.(*lua.LTable)
		if !ok {
			gemErr = fmt.Errorf("gem %s is not a table", key)
			return
		}
		tags, ok := gem.RawGetString("tags").(*lua.LTable)
		if !ok {
			gemErr = fmt.Errorf("gem %s has no tags", key)
			return
		}
		name, ok := gem.RawGetString("name").(lua.LString)
		if !ok {
			gemErr = fmt.Errorf("gem %s has no name", key)
			return
		}
		effect := L.NewTable()
		effect.RawSetString("name", name)
		effect.RawSetString("support", tags.RawGetString("support"))
		effect.RawSetString("plusVersionOf", tags.RawGetString("awakened"))
		gem.RawSetString("grantedEffect", effect)
	})
	if gemErr != nil {
		return nil, gemErr
	}

	if err := L.DoFile(generatedPath); err != nil {
		return nil, fmt.Errorf("execute PoB generated uniques %s: %w", generatedPath, err)
	}
	uniques := data.RawGetString("uniques").(*lua.LTable)
	generated, ok := uniques.RawGetString("generated").(*lua.LTable)
	if !ok || generated.Len() == 0 {
		return nil, fmt.Errorf("PoB generator produced no uniques: %s", generatedPath)
	}
	result := make([]string, 0, generated.Len())
	for i := 1; i <= generated.Len(); i++ {
		value, ok := generated.RawGetInt(i).(lua.LString)
		if !ok {
			return nil, fmt.Errorf("generated unique %d is not a string", i)
		}
		result = append(result, string(value))
	}
	return result, nil
}
