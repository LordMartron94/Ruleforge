package config

import (
	"fmt"
	"os"
	"path/filepath"
	"reflect"
	"sort"
	"strings"

	"github.com/BurntSushi/toml"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/config/tomledit"
)

// StylesheetFile is a stylesheet as the Studio's style editor shows it.
type StylesheetFile struct {
	File     string          `json:"file"`
	Entries  []StyleEntry    `json:"entries"`
	Distinct []DistinctEntry `json:"distinct"`
	Hash     string          `json:"hash"`
}

// StyleEntry is one style of a stylesheet: a template or a final style.
type StyleEntry struct {
	// Name is the style's path, such as "Final/Tattoos/T1".
	Name string `json:"name"`
	// Comments are the comment lines directly above the style.
	Comments []string   `json:"comments,omitempty"`
	Props    StyleProps `json:"props"`
}

// StyleProps are the properties a style sets itself.
type StyleProps struct {
	Comment     *string   `json:"Comment,omitempty"`
	Combination *[]string `json:"Combination,omitempty"`
	// Colours are palette names or "#RRGGBB(AA)".
	TextColor       *string       `json:"TextColor,omitempty"`
	BorderColor     *string       `json:"BorderColor,omitempty"`
	BackgroundColor *string       `json:"BackgroundColor,omitempty"`
	FontSize        *int          `json:"FontSize,omitempty"`
	Minimap         *MinimapProps `json:"Minimap,omitempty"`
	Beam            *BeamProps    `json:"Beam,omitempty"`
	DropSound       *string       `json:"DropSound,omitempty"`
	DropVolume      *int          `json:"DropVolume,omitempty"`
	// Other are keys the editor does not know, as TOML text. They are kept.
	Other map[string]string `json:"other,omitempty"`
}

// MinimapProps is a minimap icon.
type MinimapProps struct {
	Shape *string `json:"Shape,omitempty"`
	Color *string `json:"Color,omitempty"`
	Size  *int    `json:"Size,omitempty"`
}

// BeamProps is a light beam.
type BeamProps struct {
	Color *string `json:"Color,omitempty"`
	Temp  *bool   `json:"Temp,omitempty"`
}

// DistinctEntry is a [Studio.Distinct] group.
type DistinctEntry struct {
	Name string   `json:"name"`
	Min  float64  `json:"min"`
	Vars []string `json:"vars"`
}

// styleKeyOrder is the order new keys are written in.
var styleKeyOrder = []string{"Comment", "Combination", "TextColor", "BorderColor", "BackgroundColor", "FontSize", "Minimap", "Beam", "DropSound", "DropVolume"}

const distinctPrefix = studioKey + ".Distinct."

func styleTable(name string) string { return strings.ReplaceAll(name, "/", ".") }
func styleName(table string) string { return strings.ReplaceAll(table, ".", "/") }

// ReadStylesheetFile reads a stylesheet for editing.
func ReadStylesheetFile(path string) (*StylesheetFile, error) {
	contents, err := os.ReadFile(path)
	if err != nil {
		return nil, err
	}
	return ParseStylesheetFile(path, contents)
}

// ParseStylesheetFile reads a stylesheet's contents for editing.
func ParseStylesheetFile(path string, contents []byte) (*StylesheetFile, error) {
	doc, err := tomledit.Parse(string(contents))
	if err != nil {
		return nil, fmt.Errorf("stylesheet %s: %w", path, err)
	}
	sheet := &StylesheetFile{File: filepath.Base(path), Hash: ContentHash(contents)}
	for _, table := range doc.Tables() {
		switch {
		case strings.HasPrefix(table, distinctPrefix):
			entry := DistinctEntry{Name: strings.TrimPrefix(table, distinctPrefix)}
			for _, kv := range doc.Keys(table) {
				value := decodeValue(kv.Value)
				switch kv.Key {
				case "Min":
					switch n := value.(type) {
					case int64:
						entry.Min = float64(n)
					case float64:
						entry.Min = n
					}
				case "Vars":
					entry.Vars = toStrings(value)
				}
			}
			sheet.Distinct = append(sheet.Distinct, entry)
		case table == studioKey || strings.HasPrefix(table, studioKey+"."):
		default:
			props, err := readProps(doc.Keys(table))
			if err != nil {
				return nil, fmt.Errorf("stylesheet %s, [%s]: %w", path, table, err)
			}
			sheet.Entries = append(sheet.Entries, StyleEntry{Name: styleName(table), Comments: doc.Comments(table), Props: props})
		}
	}
	return sheet, nil
}

func decodeValue(text string) any {
	var decoded map[string]any
	if _, err := toml.Decode("x = "+text, &decoded); err != nil {
		return nil
	}
	return decoded["x"]
}

func toStrings(value any) []string {
	list, _ := value.([]any)
	out := make([]string, 0, len(list))
	for _, item := range list {
		if s, ok := item.(string); ok {
			out = append(out, s)
		}
	}
	return out
}

func stringPtr(s string) *string { return &s }
func intPtr(n int) *int          { return &n }

// colourText turns any stylesheet colour form into a palette name or hex text.
func colourText(value any) (string, bool) {
	switch v := value.(type) {
	case string:
		return v, true
	case map[string]any:
		if name, ok := v["var"].(string); ok {
			return name, true
		}
		channel := func(key string) (int64, bool) { n, ok := v[key].(int64); return n, ok }
		r, okR := channel("red")
		g, okG := channel("green")
		b, okB := channel("blue")
		if !okR || !okG || !okB {
			return "", false
		}
		a, okA := channel("alpha")
		if !okA {
			return fmt.Sprintf("#%02X%02X%02X", r, g, b), true
		}
		return fmt.Sprintf("#%02X%02X%02X%02X", r, g, b, a), true
	}
	return "", false
}

func readProps(keys []tomledit.KeyValue) (StyleProps, error) {
	var props StyleProps
	other := func(kv tomledit.KeyValue) {
		if props.Other == nil {
			props.Other = map[string]string{}
		}
		props.Other[kv.Key] = kv.Value
	}
	for _, kv := range keys {
		value := decodeValue(kv.Value)
		switch kv.Key {
		case "Comment":
			if s, ok := value.(string); ok {
				props.Comment = stringPtr(s)
			} else {
				other(kv)
			}
		case "Combination":
			list := toStrings(value)
			props.Combination = &list
		case "TextColor", "BorderColor", "BackgroundColor":
			text, ok := colourText(value)
			if !ok {
				other(kv)
				continue
			}
			switch kv.Key {
			case "TextColor":
				props.TextColor = stringPtr(text)
			case "BorderColor":
				props.BorderColor = stringPtr(text)
			default:
				props.BackgroundColor = stringPtr(text)
			}
		case "FontSize", "DropVolume":
			n, ok := value.(int64)
			if !ok {
				other(kv)
				continue
			}
			if kv.Key == "FontSize" {
				props.FontSize = intPtr(int(n))
			} else {
				props.DropVolume = intPtr(int(n))
			}
		case "DropSound":
			if s, ok := value.(string); ok {
				props.DropSound = stringPtr(s)
			} else {
				other(kv)
			}
		case "Minimap":
			table, ok := value.(map[string]any)
			if !ok {
				other(kv)
				continue
			}
			minimap := &MinimapProps{}
			if s, ok := table["Shape"].(string); ok {
				minimap.Shape = stringPtr(s)
			}
			if s, ok := table["Color"].(string); ok {
				minimap.Color = stringPtr(s)
			}
			if n, ok := table["Size"].(int64); ok {
				minimap.Size = intPtr(int(n))
			}
			props.Minimap = minimap
		case "Beam":
			table, ok := value.(map[string]any)
			if !ok {
				other(kv)
				continue
			}
			beam := &BeamProps{}
			if s, ok := table["Color"].(string); ok {
				beam.Color = stringPtr(s)
			}
			if b, ok := table["Temp"].(bool); ok {
				beam.Temp = &b
			}
			props.Beam = beam
		default:
			other(kv)
		}
	}
	return props, nil
}

// propTexts writes each property the editor knows as TOML value text; nil
// means the property is not set.
func propTexts(props StyleProps) map[string]*string {
	text := func(s string) *string { return &s }
	out := map[string]*string{}
	for _, key := range styleKeyOrder {
		out[key] = nil
	}
	if props.Comment != nil {
		out["Comment"] = text(tomledit.String(*props.Comment))
	}
	if props.Combination != nil {
		out["Combination"] = text(tomledit.StringArray(*props.Combination))
	}
	for key, value := range map[string]*string{"TextColor": props.TextColor, "BorderColor": props.BorderColor, "BackgroundColor": props.BackgroundColor, "DropSound": props.DropSound} {
		if value != nil {
			out[key] = text(tomledit.String(*value))
		}
	}
	if props.FontSize != nil {
		out["FontSize"] = text(tomledit.Int(*props.FontSize))
	}
	if props.DropVolume != nil {
		out["DropVolume"] = text(tomledit.Int(*props.DropVolume))
	}
	if m := props.Minimap; m != nil {
		var pairs []tomledit.KeyValue
		if m.Shape != nil {
			pairs = append(pairs, tomledit.KeyValue{Key: "Shape", Value: tomledit.String(*m.Shape)})
		}
		if m.Color != nil {
			pairs = append(pairs, tomledit.KeyValue{Key: "Color", Value: tomledit.String(*m.Color)})
		}
		if m.Size != nil {
			pairs = append(pairs, tomledit.KeyValue{Key: "Size", Value: tomledit.Int(*m.Size)})
		}
		if len(pairs) > 0 {
			out["Minimap"] = text(tomledit.InlineTable(pairs))
		}
	}
	if b := props.Beam; b != nil {
		var pairs []tomledit.KeyValue
		if b.Color != nil {
			pairs = append(pairs, tomledit.KeyValue{Key: "Color", Value: tomledit.String(*b.Color)})
		}
		if b.Temp != nil {
			pairs = append(pairs, tomledit.KeyValue{Key: "Temp", Value: tomledit.Bool(*b.Temp)})
		}
		if len(pairs) > 0 {
			out["Beam"] = text(tomledit.InlineTable(pairs))
		}
	}
	return out
}

// StyleChange creates, changes or deletes one style. Props are the style's
// complete own properties; Other keys that are left out are kept.
type StyleChange struct {
	Name   string      `json:"name"`
	Delete bool        `json:"delete,omitempty"`
	Props  *StyleProps `json:"props,omitempty"`
	// After places a new style after this one; by default it goes after its
	// closest relative.
	After string `json:"after,omitempty"`
}

// DistinctChange creates, changes or deletes a [Studio.Distinct] group.
type DistinctChange struct {
	DistinctEntry
	Delete bool `json:"delete,omitempty"`
}

// ApplyStylesheetChanges writes style and distinct-group changes into a
// stylesheet's contents, rewriting only the lines whose values change.
func ApplyStylesheetChanges(contents string, changes []StyleChange, distinct []DistinctChange) (string, error) {
	doc, err := tomledit.Parse(contents)
	if err != nil {
		return "", err
	}
	for _, change := range changes {
		table := styleTable(change.Name)
		if change.Name == "" || strings.Contains(change.Name, ".") || table == studioKey || strings.HasPrefix(table, studioKey+".") {
			return "", fmt.Errorf("%q is not a valid style name", change.Name)
		}
		if change.Delete {
			doc.RemoveTable(table)
			continue
		}
		if change.Props == nil {
			return "", fmt.Errorf("style %s: no properties", change.Name)
		}
		wanted := propTexts(*change.Props)
		if !doc.HasTable(table) {
			var keys []tomledit.KeyValue
			for _, key := range styleKeyOrder {
				if wanted[key] != nil {
					keys = append(keys, tomledit.KeyValue{Key: key, Value: *wanted[key]})
				}
			}
			for _, key := range sortedKeys(change.Props.Other) {
				keys = append(keys, tomledit.KeyValue{Key: key, Value: change.Props.Other[key]})
			}
			after := change.After
			if after == "" {
				after = closestTable(doc.Tables(), table)
			} else {
				after = styleTable(after)
			}
			if err := doc.AddTable(table, after, keys); err != nil {
				return "", err
			}
			continue
		}
		for _, key := range styleKeyOrder {
			current, exists := doc.Get(table, key)
			switch {
			case wanted[key] == nil && exists:
				doc.Delete(table, key)
			case wanted[key] != nil && (!exists || !reflect.DeepEqual(decodeValue(current), decodeValue(*wanted[key]))):
				if err := doc.Set(table, key, *wanted[key]); err != nil {
					return "", err
				}
			}
		}
		for key, value := range change.Props.Other {
			if current, exists := doc.Get(table, key); !exists || current != value {
				if err := doc.Set(table, key, value); err != nil {
					return "", err
				}
			}
		}
	}
	for _, change := range distinct {
		table := distinctPrefix + change.Name
		if change.Name == "" || strings.Contains(change.Name, ".") {
			return "", fmt.Errorf("%q is not a valid group name", change.Name)
		}
		if change.Delete {
			doc.RemoveTable(table)
			continue
		}
		minText, varsText := tomledit.Float(change.Min), tomledit.StringArray(change.Vars)
		if !doc.HasTable(table) {
			after := ""
			for _, existing := range doc.Tables() {
				if strings.HasPrefix(existing, distinctPrefix) {
					after = existing
				}
			}
			if err := doc.AddTable(table, after, []tomledit.KeyValue{{Key: "Min", Value: minText}, {Key: "Vars", Value: varsText}}); err != nil {
				return "", err
			}
			continue
		}
		if current, _ := doc.Get(table, "Min"); !reflect.DeepEqual(numberValue(decodeValue(current)), change.Min) {
			if err := doc.Set(table, "Min", minText); err != nil {
				return "", err
			}
		}
		if current, _ := doc.Get(table, "Vars"); !reflect.DeepEqual(toStrings(decodeValue(current)), append([]string{}, change.Vars...)) {
			if err := doc.Set(table, "Vars", varsText); err != nil {
				return "", err
			}
		}
	}
	return doc.String(), nil
}

func numberValue(value any) float64 {
	switch n := value.(type) {
	case int64:
		return float64(n)
	case float64:
		return n
	}
	return -1
}

func sortedKeys(m map[string]string) []string {
	keys := make([]string, 0, len(m))
	for key := range m {
		keys = append(keys, key)
	}
	sort.Strings(keys)
	return keys
}

// closestTable picks where a new style goes: among the styles sharing the
// longest name prefix with it, after the last one that sorts before it, so a
// new Final.Tattoos.T7 lands after Final.Tattoos.T6. It returns "" for the
// end of the styles.
func closestTable(tables []string, table string) string {
	parts := strings.Split(table, ".")
	var styles []string
	for _, existing := range tables {
		if existing != studioKey && !strings.HasPrefix(existing, studioKey+".") {
			styles = append(styles, existing)
		}
	}
	shared := func(existing string) int {
		n := 0
		for i, part := range strings.Split(existing, ".") {
			if i >= len(parts) || parts[i] != part {
				break
			}
			n++
		}
		return n
	}
	best := 0
	for _, existing := range styles {
		best = max(best, shared(existing))
	}
	if best == 0 {
		if len(styles) == 0 {
			return ""
		}
		return styles[len(styles)-1]
	}
	after, first := "", -1
	for i, existing := range styles {
		if shared(existing) != best {
			continue
		}
		if first < 0 {
			first = i
		}
		if naturalLess(existing, table) {
			after = existing
		}
	}
	if after != "" {
		return after
	}
	if first > 0 {
		return styles[first-1]
	}
	return styles[len(styles)-1]
}

// naturalLess compares names with runs of digits compared as numbers.
func naturalLess(a, b string) bool {
	for a != "" && b != "" {
		ra, rb := digitRun(a), digitRun(b)
		if ra != "" && rb != "" {
			na, nb := strings.TrimLeft(ra, "0"), strings.TrimLeft(rb, "0")
			if len(na) != len(nb) {
				return len(na) < len(nb)
			}
			if na != nb {
				return na < nb
			}
			a, b = a[len(ra):], b[len(rb):]
			continue
		}
		if a[0] != b[0] {
			return a[0] < b[0]
		}
		a, b = a[1:], b[1:]
	}
	return len(a) < len(b)
}

func digitRun(s string) string {
	i := 0
	for i < len(s) && s[i] >= '0' && s[i] <= '9' {
		i++
	}
	return s[:i]
}
