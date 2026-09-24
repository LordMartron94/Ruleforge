package data_generation

import (
	"fmt"
	"os"
	"regexp"
	"sort"
	"strconv"
	"strings"

	lua "github.com/yuin/gopher-lua"
)

// The upstream ModItemExclusive.lua is several megabytes of Lua table literals.
// Compiling the entire file with GopherLua to use two entries can consume
// hundreds of megabytes. Determine the exact entries requested by the pinned
// Generated.lua and compile only their unmodified Lua table literals.
var exclusiveReference = regexp.MustCompile(`data\.itemMods\.ItemExclusive(?:\.([A-Za-z_][A-Za-z_0-9]*)|\[(["'][A-Za-z_][A-Za-z_0-9]*["'])\])`)
var exclusivePrefix = regexp.MustCompile(`data\.itemMods\.ItemExclusive`)

func referencedExclusiveKeys(generatedPath string) ([]string, error) {
	source, err := os.ReadFile(generatedPath)
	if err != nil {
		return nil, fmt.Errorf("read unique generator: %w", err)
	}
	matches := exclusiveReference.FindAllSubmatchIndex(source, -1)
	prefixes := exclusivePrefix.FindAllIndex(source, -1)
	if len(matches) != len(prefixes) {
		return nil, fmt.Errorf("generated uniques use a dynamic ItemExclusive lookup; selective loading requires explicit literal modifier names")
	}
	seen := make(map[string]bool)
	for _, match := range matches {
		var key string
		if match[2] >= 0 {
			key = string(source[match[2]:match[3]])
		} else {
			quoted := string(source[match[4]:match[5]])
			key = quoted[1 : len(quoted)-1]
		}
		seen[key] = true
	}
	keys := make([]string, 0, len(seen))
	for key := range seen {
		keys = append(keys, key)
	}
	sort.Strings(keys)
	return keys, nil
}

// luaLongEnd skips Lua's [=[long strings]=] and --[=[long comments]=].
func luaLongEnd(source string, at int) (int, bool, error) {
	if at >= len(source) || source[at] != '[' {
		return 0, false, nil
	}
	i := at + 1
	for i < len(source) && source[i] == '=' {
		i++
	}
	if i >= len(source) || source[i] != '[' {
		return 0, false, nil
	}
	closing := "]" + strings.Repeat("=", i-at-1) + "]"
	end := strings.Index(source[i+1:], closing)
	if end < 0 {
		return 0, true, fmt.Errorf("unterminated Lua long string")
	}
	return i + 1 + end + len(closing), true, nil
}

// luaTableEnd finds the closing brace of a Lua table literal while ignoring
// braces in quoted strings, Lua long strings, and comments.
func luaTableEnd(source string, start int) (int, error) {
	if start >= len(source) || source[start] != '{' {
		return 0, fmt.Errorf("expected Lua table literal")
	}
	depth := 0
	for i := start; i < len(source); {
		switch source[i] {
		case '\'', '"':
			quote := source[i]
			i++
			closed := false
			for i < len(source) {
				if source[i] == '\\' {
					i += 2
				} else if source[i] == quote {
					i++
					closed = true
					break
				} else {
					i++
				}
			}
			if !closed {
				return 0, fmt.Errorf("unterminated quoted Lua string")
			}
		case '[':
			end, ok, err := luaLongEnd(source, i)
			if err != nil {
				return 0, err
			}
			if ok {
				i = end
			} else {
				i++
			}
		case '-':
			if i+1 >= len(source) || source[i+1] != '-' {
				i++
				continue
			}
			i += 2
			end, ok, err := luaLongEnd(source, i)
			if err != nil {
				return 0, err
			}
			if ok {
				i = end
			} else {
				for i < len(source) && source[i] != '\n' {
					i++
				}
			}
		case '{':
			depth++
			i++
		case '}':
			depth--
			i++
			if depth == 0 {
				return i, nil
			}
			if depth < 0 {
				return 0, fmt.Errorf("unbalanced Lua table literal")
			}
		default:
			i++
		}
	}
	return 0, fmt.Errorf("unterminated Lua table literal")
}

func loadRequiredExclusiveMods(L *lua.LState, modulePath string, keys []string) (*lua.LTable, error) {
	result := L.NewTable()
	if len(keys) == 0 {
		return result, nil
	}
	file, err := os.ReadFile(modulePath)
	if err != nil {
		return nil, fmt.Errorf("read PoB item-exclusive modifiers: %w", err)
	}
	source := string(file)
	var selected strings.Builder
	selected.WriteString("return {\n")
	for _, key := range keys {
		// Official PoB exports entries as top-level assignments, one per line.
		// Do not accept an unrecognized layout by silently returning nil modifiers.
		entry := regexp.MustCompile(`(?m)^[ \t]*(?:\[[ \t]*["']` + regexp.QuoteMeta(key) + `["'][ \t]*\]|` + regexp.QuoteMeta(key) + `)[ \t]*=[ \t]*\{`)
		match := entry.FindStringIndex(source)
		if match == nil {
			return nil, fmt.Errorf("required PoB item-exclusive modifier %q not found in %s", key, modulePath)
		}
		open := match[1] - 1
		end, err := luaTableEnd(source, open)
		if err != nil {
			return nil, fmt.Errorf("PoB item-exclusive modifier %q: %w", key, err)
		}
		selected.WriteString("[")
		selected.WriteString(strconv.Quote(key))
		selected.WriteString("] = ")
		selected.WriteString(source[open:end])
		selected.WriteString(",\n")
	}
	selected.WriteString("}")
	if err := L.DoString(selected.String()); err != nil {
		return nil, fmt.Errorf("evaluate required PoB item-exclusive modifiers: %w", err)
	}
	value := L.Get(-1)
	L.Pop(1)
	table, ok := value.(*lua.LTable)
	if !ok {
		return nil, fmt.Errorf("item-exclusive modifier subset did not return a table")
	}
	return table, nil
}
