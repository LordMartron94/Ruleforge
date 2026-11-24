package provider

import (
	"context"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"log"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"time"

	"github.com/LordMartron94/Ruleforge/studio/internal/config"
	lua "github.com/yuin/gopher-lua"
)

// GameRow is the common dataset shape consumed by std.items, progression and the loot preview.
// Unknown drop levels remain null; they never silently become level one.
type GameRow struct {
	ID            string   `json:"id"`
	Name          string   `json:"name"`
	BaseType      string   `json:"baseType"`
	Class         string   `json:"class"`
	DropLevel     *int     `json:"dropLevel"`
	RequiredLevel *int     `json:"requiredLevel"`
	SocketLimit   int      `json:"socketLimit"`
	Tags          []string `json:"tags"`
}

func GamePaths(project *config.Project) map[string]string {
	result := map[string]string{}
	if project.GameData == nil {
		return result
	}
	root := filepath.Join(project.Dir, ".ruleforge", "game")
	var manifest struct {
		Generation string `json:"generation"`
	}
	if data, err := os.ReadFile(filepath.Join(root, "current.json")); err == nil && json.Unmarshal(data, &manifest) == nil && len(manifest.Generation) == 64 {
		if _, err := hex.DecodeString(manifest.Generation); err == nil {
			root = filepath.Join(root, manifest.Generation)
		}
	}
	for _, kind := range []string{"bases", "gems", "uniques"} {
		result["game."+kind] = filepath.Join(root, kind+".json")
	}
	return result
}

// PrepareGameData installs complete normalized snapshots, keeping a previous snapshot on failure.
// A supplied PoB path is read only. With no path, the managed downloader pins every file to one commit.
func PrepareGameData(ctx context.Context, project *config.Project) error {
	if project.GameData == nil {
		return nil
	}
	cfg := project.GameData
	dir := cfg.PoBPath
	if dir == "" {
		dir = filepath.Join(project.Dir, ".ruleforge", "pob")
		source := defaultPoBSource(log.Default())
		source.commit = cfg.Commit
		if _, err := source.ensure(ctx, dir, false); err != nil {
			return err
		}
	} else if !filepath.IsAbs(dir) {
		dir = filepath.Join(project.Dir, dir)
	}
	bases, err := readPoBBases(ctx, dir)
	if err != nil {
		return err
	}
	gems, err := readPoBGems(ctx, dir)
	if err != nil {
		return err
	}
	uniques, err := readPoBUniques(ctx, dir, bases)
	if err != nil {
		return err
	}
	levels := map[string]int{}
	if cfg.DropLevelsPath != "" {
		path := cfg.DropLevelsPath
		if !filepath.IsAbs(path) {
			path = filepath.Join(project.Dir, path)
		}
		data, err := os.ReadFile(path)
		if err != nil {
			return err
		}
		if err := json.Unmarshal(data, &levels); err != nil {
			return fmt.Errorf("drop levels: %w", err)
		}
	} else if cfg.FetchDropLevels {
		names := make([]string, 0, len(bases))
		for _, row := range bases {
			names = append(names, row.BaseType)
		}
		levels, err = GetBaseTypeDropLevels(names)
		if err != nil {
			return err
		}
	}
	for name, level := range levels {
		if level < 1 || level > 100 {
			return fmt.Errorf("invalid drop level %d for %s", level, name)
		}
	}
	for i := range bases {
		if level, ok := levels[bases[i].BaseType]; ok {
			value := level
			bases[i].DropLevel = &value
		}
	}
	products := map[string][]byte{}
	hash := sha256.New()
	for _, entry := range []struct {
		kind string
		rows []GameRow
	}{{"bases", bases}, {"gems", gems}, {"uniques", uniques}} {
		sort.Slice(entry.rows, func(i, j int) bool { return entry.rows[i].ID < entry.rows[j].ID })
		data, err := json.Marshal(entry.rows)
		if err != nil {
			return err
		}
		products[entry.kind] = data
		hash.Write([]byte(entry.kind))
		hash.Write(data)
	}
	generation := hex.EncodeToString(hash.Sum(nil))
	root := filepath.Join(project.Dir, ".ruleforge", "game")
	// Immutable generations keep a compile's three datasets consistent during a refresh.
	for _, kind := range []string{"bases", "gems", "uniques"} {
		if err := writeSnapshot(filepath.Join(root, generation, kind+".json"), products[kind]); err != nil {
			return err
		}
	}
	manifest, err := json.Marshal(map[string]string{"generation": generation})
	if err != nil {
		return err
	}
	if err := writeSnapshot(filepath.Join(root, "current.json"), manifest); err != nil {
		return err
	}

	return nil
}

func writeSnapshot(path string, data []byte) error {
	if err := os.MkdirAll(filepath.Dir(path), 0o755); err != nil {
		return err
	}
	file, err := os.CreateTemp(filepath.Dir(path), ".snapshot-*")
	if err != nil {
		return err
	}
	defer os.Remove(file.Name())
	if _, err := file.Write(data); err != nil {
		file.Close()
		return err
	}
	if err := file.Close(); err != nil {
		return err
	}
	return os.Rename(file.Name(), path)
}

// Lua data executes without filesystem, process, network, package or debug libraries.
// Every value is copied to Go before the VM closes, and each file has a deadline.
func readPoBTable(ctx context.Context, path string, populate bool, read func(*lua.LTable) error) error {
	ctx, cancel := context.WithTimeout(ctx, 10*time.Second)
	defer cancel()
	state := lua.NewState(lua.Options{SkipOpenLibs: true})
	defer state.Close()
	state.SetContext(ctx)
	lua.OpenBase(state)
	lua.OpenTable(state)
	lua.OpenString(state)
	lua.OpenMath(state)
	for _, name := range []string{"dofile", "loadfile", "load", "loadstring", "collectgarbage"} {
		state.SetGlobal(name, lua.LNil)
	}
	fn, err := state.LoadFile(path)
	if err != nil {
		return err
	}
	table := state.NewTable()
	state.Push(fn)
	args := 0
	if populate {
		state.Push(table)
		args = 1
	}
	if err := state.PCall(args, 1, nil); err != nil {
		return fmt.Errorf("%s: %w", path, err)
	}
	value := state.Get(-1)
	if populate {
		if fn, ok := value.(*lua.LFunction); ok {
			state.Pop(1)
			state.Push(fn)
			state.Push(table)
			if err := state.PCall(1, 0, nil); err != nil {
				return err
			}
		}
		return read(table)
	}
	result, ok := value.(*lua.LTable)
	if !ok {
		return fmt.Errorf("%s did not return a table", path)
	}
	return read(result)
}

func readPoBBases(ctx context.Context, dir string) ([]GameRow, error) {
	paths, err := filepath.Glob(filepath.Join(dir, "Bases", "*.lua"))
	if err != nil || len(paths) == 0 {
		return nil, fmt.Errorf("no PoB Bases/*.lua files at %s", dir)
	}
	rows := []GameRow{}
	for _, path := range paths {
		err := readPoBTable(ctx, path, true, func(table *lua.LTable) error {
			var failure error
			table.ForEach(func(key, value lua.LValue) {
				entry, ok := value.(*lua.LTable)
				if !ok {
					failure = fmt.Errorf("invalid PoB base %s", key)
					return
				}
				kind := entry.RawGetString("type").String()
				row := GameRow{ID: key.String(), Name: key.String(), BaseType: key.String(), Class: filterClass(kind), Tags: []string{}}
				if n, ok := entry.RawGetString("socketLimit").(lua.LNumber); ok {
					row.SocketLimit = int(n)
				}
				if req, ok := entry.RawGetString("req").(*lua.LTable); ok {
					if n, ok := req.RawGetString("level").(lua.LNumber); ok {
						v := int(n)
						row.RequiredLevel = &v
					}
				}
				if n, ok := entry.RawGetString("dropLevel").(lua.LNumber); ok {
					v := int(n)
					row.DropLevel = &v
				}
				if tags, ok := entry.RawGetString("tags").(*lua.LTable); ok {
					tags.ForEach(func(k, v lua.LValue) {
						if lua.LVAsBool(v) {
							row.Tags = append(row.Tags, k.String())
						}
					})
					sort.Strings(row.Tags)
				}
				rows = append(rows, row)
			})
			return failure
		})
		if err != nil {
			return nil, err
		}
	}
	if len(rows) == 0 {
		return nil, fmt.Errorf("PoB returned no bases")
	}
	return rows, nil
}
func filterClass(kind string) string {
	aliases := map[string]string{"One Handed Sword": "One Hand Swords", "Two Handed Sword": "Two Hand Swords", "One Handed Axe": "One Hand Axes", "Two Handed Axe": "Two Hand Axes", "One Handed Mace": "One Hand Maces", "Two Handed Mace": "Two Hand Maces", "Body Armour": "Body Armours", "Staff": "Staves", "Warstaff": "Warstaves", "Sceptre": "Sceptres", "Claw": "Claws", "Dagger": "Daggers", "Rune Dagger": "Rune Daggers", "Wand": "Wands", "Bow": "Bows", "Shield": "Shields", "Helmet": "Helmets", "Gloves": "Gloves", "Boots": "Boots", "Ring": "Rings", "Amulet": "Amulets", "Belt": "Belts", "Jewel": "Jewels", "Quiver": "Quivers", "Flask": "Utility Flasks", "Life Flask": "Life Flasks", "Mana Flask": "Mana Flasks", "Hybrid Flask": "Hybrid Flasks"}
	if value, ok := aliases[kind]; ok {
		return value
	}
	return kind
}
func readPoBGems(ctx context.Context, dir string) ([]GameRow, error) {
	rows := []GameRow{}
	err := readPoBTable(ctx, filepath.Join(dir, "Gems.lua"), false, func(table *lua.LTable) error {
		table.ForEach(func(key, value lua.LValue) {
			entry, ok := value.(*lua.LTable)
			if !ok {
				return
			}
			name := entry.RawGetString("name")
			if name == lua.LNil {
				return
			}
			base := entry.RawGetString("baseTypeName")
			if base == lua.LNil {
				base = name
			}
			row := GameRow{ID: key.String(), Name: name.String(), BaseType: base.String(), Class: "Skill Gems", Tags: []string{}}
			if tags, ok := entry.RawGetString("tags").(*lua.LTable); ok {
				tags.ForEach(func(k, v lua.LValue) {
					if lua.LVAsBool(v) {
						row.Tags = append(row.Tags, k.String())
					}
				})
				if lua.LVAsBool(tags.RawGetString("support")) {
					row.Class = "Support Gems"
				}
				sort.Strings(row.Tags)
			}
			rows = append(rows, row)
		})
		return nil
	})
	return rows, err
}
func readPoBUniques(ctx context.Context, dir string, bases []GameRow) ([]GameRow, error) {
	paths, err := filepath.Glob(filepath.Join(dir, "Uniques", "*.lua"))
	if err != nil || len(paths) == 0 {
		return nil, fmt.Errorf("no PoB Uniques/*.lua files at %s", dir)
	}
	baseIndex := map[string]GameRow{}
	for _, row := range bases {
		baseIndex[row.BaseType] = row
	}
	rows := []GameRow{}
	for _, path := range paths {
		// Generated uniques require extra PoB modules; the standard named uniques are the bundled provider's scope.
		if strings.EqualFold(filepath.Base(path), "Generated.lua") {
			continue
		}
		err := readPoBTable(ctx, path, false, func(table *lua.LTable) error {
			table.ForEach(func(_, value lua.LValue) {
				text, ok := value.(lua.LString)
				if !ok {
					return
				}
				lines := strings.Split(strings.TrimSpace(string(text)), "\n")
				if len(lines) < 2 {
					return
				}
				name := strings.TrimSpace(lines[0])
				base := strings.TrimSpace(lines[1])
				row := baseIndex[base]
				row.ID = name
				row.Name = name
				row.BaseType = base
				if row.Tags == nil {
					row.Tags = []string{}
				}
				rows = append(rows, row)
			})
			return nil
		})
		if err != nil {
			return nil, err
		}
	}
	return rows, nil
}
