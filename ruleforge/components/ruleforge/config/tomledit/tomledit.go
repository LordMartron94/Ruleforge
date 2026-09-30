// Package tomledit changes single-line TOML files the way a person would:
// one key or table at a time, leaving every other line, comment and blank
// line exactly as it was. It supports what Ruleforge's style files use:
// [table] headers and one-line key = value pairs.
package tomledit

import (
	"fmt"
	"strconv"
	"strings"

	"github.com/BurntSushi/toml"
)

type lineKind int

const (
	blankLine lineKind = iota
	commentLine
	headerLine
	keyLine
)

type line struct {
	text  string
	kind  lineKind
	table string // a header's table, or the table a key belongs to
	key   string
	value string // a key's value text, without a trailing comment
}

// Doc is an editable TOML file.
type Doc struct {
	lines []line
	// finalNewline records whether the file ended with a line break.
	finalNewline bool
}

// Parse reads a TOML file. Multi-line values and arrays of tables are not supported.
func Parse(src string) (*Doc, error) {
	if _, err := toml.Decode(src, new(map[string]any)); err != nil {
		return nil, err
	}
	doc := &Doc{finalNewline: src == "" || strings.HasSuffix(src, "\n")}
	texts := strings.Split(strings.TrimSuffix(src, "\n"), "\n")
	if src == "" {
		texts = nil
	}
	table := ""
	for i, text := range texts {
		trimmed := strings.TrimSpace(text)
		l := line{text: text}
		switch {
		case trimmed == "":
			l.kind = blankLine
		case strings.HasPrefix(trimmed, "#"):
			l.kind = commentLine
		case strings.HasPrefix(trimmed, "[["):
			return nil, fmt.Errorf("line %d: arrays of tables are not supported", i+1)
		case strings.HasPrefix(trimmed, "["):
			name, err := headerName(trimmed)
			if err != nil {
				return nil, fmt.Errorf("line %d: %w", i+1, err)
			}
			table = name
			l.kind, l.table = headerLine, name
		default:
			key, value, err := splitKeyLine(trimmed)
			if err != nil {
				return nil, fmt.Errorf("line %d: %w", i+1, err)
			}
			if _, err := toml.Decode("x = "+value, new(map[string]any)); err != nil {
				return nil, fmt.Errorf("line %d: only one-line values are supported: %w", i+1, err)
			}
			l.kind, l.table, l.key, l.value = keyLine, table, key, value
		}
		doc.lines = append(doc.lines, l)
	}
	return doc, nil
}

// String writes the file.
func (d *Doc) String() string {
	var b strings.Builder
	for _, l := range d.lines {
		b.WriteString(l.text)
		b.WriteString("\n")
	}
	out := b.String()
	if !d.finalNewline {
		out = strings.TrimSuffix(out, "\n")
	}
	return out
}

// headerName returns a [table] header's dotted name, with quoted parts unquoted.
func headerName(header string) (string, error) {
	end := closingBracket(header)
	if end < 0 {
		return "", fmt.Errorf("unterminated table header")
	}
	if rest := strings.TrimSpace(header[end+1:]); rest != "" && !strings.HasPrefix(rest, "#") {
		return "", fmt.Errorf("text after a table header")
	}
	parts, err := splitDotted(strings.TrimSpace(header[1:end]))
	if err != nil {
		return "", err
	}
	return strings.Join(parts, "."), nil
}

func closingBracket(header string) int {
	inString := byte(0)
	for i := 1; i < len(header); i++ {
		c := header[i]
		switch {
		case inString != 0 && c == '\\' && inString == '"':
			i++
		case inString != 0 && c == inString:
			inString = 0
		case inString == 0 && (c == '"' || c == '\''):
			inString = c
		case inString == 0 && c == ']':
			return i
		}
	}
	return -1
}

// splitDotted splits a dotted key into its parts.
func splitDotted(key string) ([]string, error) {
	var parts []string
	for key != "" {
		key = strings.TrimSpace(key)
		var part string
		switch key[0] {
		case '"', '\'':
			end := strings.IndexByte(key[1:], key[0])
			if end < 0 {
				return nil, fmt.Errorf("unterminated quoted key")
			}
			raw := key[:end+2]
			if key[0] == '"' {
				unquoted, err := strconv.Unquote(raw)
				if err != nil {
					return nil, fmt.Errorf("bad quoted key %s", raw)
				}
				part = unquoted
			} else {
				part = raw[1 : len(raw)-1]
			}
			key = strings.TrimSpace(key[end+2:])
		default:
			end := strings.IndexByte(key, '.')
			if end < 0 {
				end = len(key)
			}
			part = strings.TrimSpace(key[:end])
			key = key[end:]
		}
		parts = append(parts, part)
		if strings.HasPrefix(key, ".") {
			key = key[1:]
		} else if key != "" {
			return nil, fmt.Errorf("bad key")
		}
	}
	return parts, nil
}

// splitKeyLine splits "key = value # comment" into the key and the value text.
func splitKeyLine(text string) (string, string, error) {
	equals := strings.IndexByte(text, '=')
	if equals < 0 {
		return "", "", fmt.Errorf("expected key = value")
	}
	parts, err := splitDotted(strings.TrimSpace(text[:equals]))
	if err != nil {
		return "", "", err
	}
	if len(parts) != 1 {
		return "", "", fmt.Errorf("dotted keys are not supported")
	}
	value := strings.TrimSpace(stripComment(text[equals+1:]))
	return parts[0], value, nil
}

// stripComment removes a trailing # comment outside strings.
func stripComment(text string) string {
	inString := byte(0)
	for i := 0; i < len(text); i++ {
		c := text[i]
		switch {
		case inString == '"' && c == '\\':
			i++
		case inString != 0 && c == inString:
			inString = 0
		case inString == 0 && (c == '"' || c == '\''):
			inString = c
		case inString == 0 && c == '#':
			return text[:i]
		}
	}
	return text
}

// Tables lists the file's tables in order.
func (d *Doc) Tables() []string {
	var tables []string
	for _, l := range d.lines {
		if l.kind == headerLine {
			tables = append(tables, l.table)
		}
	}
	return tables
}

// HasTable reports whether the file declares the table.
func (d *Doc) HasTable(table string) bool {
	return d.header(table) >= 0
}

func (d *Doc) header(table string) int {
	for i, l := range d.lines {
		if l.kind == headerLine && l.table == table {
			return i
		}
	}
	return -1
}

// KeyValue is a key and its value as TOML text.
type KeyValue struct {
	Key   string
	Value string
}

// Keys lists a table's keys and value texts in order; "" is the top level.
func (d *Doc) Keys(table string) []KeyValue {
	var keys []KeyValue
	for _, l := range d.lines {
		if l.kind == keyLine && l.table == table {
			keys = append(keys, KeyValue{Key: l.key, Value: l.value})
		}
	}
	return keys
}

// Get returns a key's value text.
func (d *Doc) Get(table, key string) (string, bool) {
	for _, l := range d.lines {
		if l.kind == keyLine && l.table == table && l.key == key {
			return l.value, true
		}
	}
	return "", false
}

// Comments returns the comment lines directly above a table's header.
func (d *Doc) Comments(table string) []string {
	h := d.header(table)
	if h < 0 {
		return nil
	}
	start := h
	for start > 0 && d.lines[start-1].kind == commentLine {
		start--
	}
	var comments []string
	for _, l := range d.lines[start:h] {
		comments = append(comments, l.text)
	}
	return comments
}

// tableEnd is the index after a table's last key line; for the top level,
// after the last key before the first header.
func (d *Doc) tableEnd(table string) int {
	start := 0
	if table != "" {
		start = d.header(table) + 1
	}
	end, found := start, false
	for i := start; i < len(d.lines); i++ {
		if d.lines[i].kind == headerLine {
			break
		}
		if d.lines[i].kind == keyLine {
			end, found = i+1, true
		}
	}
	if !found && table == "" {
		// No top-level keys yet: after the file's opening comments.
		for end < len(d.lines) && d.lines[end].kind == commentLine {
			end++
		}
	}
	return end
}

// Set sets a key to a TOML value text, rewriting only its line. A new key
// goes after the table's last key.
func (d *Doc) Set(table, key, value string) error {
	if _, err := toml.Decode("x = "+value, new(map[string]any)); err != nil {
		return fmt.Errorf("%s.%s: %w", table, key, err)
	}
	for i, l := range d.lines {
		if l.kind == keyLine && l.table == table && l.key == key {
			if l.value == value {
				return nil
			}
			equals := strings.IndexByte(l.text, '=')
			at := equals + strings.Index(l.text[equals:], l.value)
			d.lines[i].text = l.text[:at] + value + l.text[at+len(l.value):]
			d.lines[i].value = value
			return nil
		}
	}
	if table != "" && !d.HasTable(table) {
		return fmt.Errorf("no table [%s]", table)
	}
	at := d.tableEnd(table)
	d.insert(at, line{text: FormatKey(key) + " = " + value, kind: keyLine, table: table, key: key, value: value})
	return nil
}

// Delete removes a key's line.
func (d *Doc) Delete(table, key string) {
	for i, l := range d.lines {
		if l.kind == keyLine && l.table == table && l.key == key {
			d.lines = append(d.lines[:i], d.lines[i+1:]...)
			return
		}
	}
}

func (d *Doc) insert(at int, lines ...line) {
	d.lines = append(d.lines[:at], append(lines, d.lines[at:]...)...)
}

// AddTable adds a table with its keys after the table named after, separated
// by a blank line. With after empty or unknown it goes at the end of the file.
func (d *Doc) AddTable(table, after string, keys []KeyValue) error {
	if d.HasTable(table) {
		return fmt.Errorf("[%s] already exists", table)
	}
	block := []line{{kind: blankLine}, {text: "[" + FormatTableName(table) + "]", kind: headerLine, table: table}}
	for _, kv := range keys {
		if _, err := toml.Decode("x = "+kv.Value, new(map[string]any)); err != nil {
			return fmt.Errorf("%s.%s: %w", table, kv.Key, err)
		}
		block = append(block, line{text: FormatKey(kv.Key) + " = " + kv.Value, kind: keyLine, table: table, key: kv.Key, value: kv.Value})
	}
	at := len(d.lines)
	if after != "" && d.HasTable(after) {
		at = d.tableEnd(after)
	} else {
		for at > 0 && d.lines[at-1].kind == blankLine {
			at--
		}
	}
	d.insert(at, block...)
	return nil
}

// RemoveTable removes a table's header and keys. Comments and blank lines
// stay: a comment above a table often describes the tables after it too.
func (d *Doc) RemoveTable(table string) {
	h := d.header(table)
	if h < 0 {
		return
	}
	end := d.tableEnd(table)
	kept := append([]line{}, d.lines[:h]...)
	for _, l := range d.lines[h+1 : end] {
		if l.kind != keyLine {
			kept = append(kept, l)
		}
	}
	d.lines = append(kept, d.lines[end:]...)
	// Two blank lines left behind collapse into one.
	if h > 0 && h < len(d.lines) && d.lines[h-1].kind == blankLine && d.lines[h].kind == blankLine {
		d.lines = append(d.lines[:h], d.lines[h+1:]...)
	}
}

// Body returns the text lines between a table's header and the next header.
func (d *Doc) Body(table string) []string {
	h := d.header(table)
	if h < 0 {
		return nil
	}
	var body []string
	for _, l := range d.lines[h+1:] {
		if l.kind == headerLine {
			break
		}
		body = append(body, l.text)
	}
	return body
}

// ReplaceBody replaces everything between a table's header and the next header.
func (d *Doc) ReplaceBody(table string, body []string) error {
	h := d.header(table)
	if h < 0 {
		return fmt.Errorf("no table [%s]", table)
	}
	end := h + 1
	for end < len(d.lines) && d.lines[end].kind != headerLine {
		end++
	}
	parsed, err := Parse("[" + FormatTableName(table) + "]\n" + strings.Join(body, "\n") + "\n")
	if err != nil {
		return err
	}
	d.lines = append(d.lines[:h+1], append(parsed.lines[1:], d.lines[end:]...)...)
	return nil
}

// AppendTable adds a table at the end of the file with the given body lines.
func (d *Doc) AppendTable(table string, body []string) error {
	if d.HasTable(table) {
		return fmt.Errorf("[%s] already exists", table)
	}
	if n := len(d.lines); n > 0 && d.lines[n-1].kind != blankLine {
		d.lines = append(d.lines, line{kind: blankLine})
	}
	d.lines = append(d.lines, line{text: "[" + FormatTableName(table) + "]", kind: headerLine, table: table})
	return d.ReplaceBody(table, body)
}

// --- Writing values ---

// String formats a TOML basic string.
func String(s string) string {
	var b strings.Builder
	b.WriteByte('"')
	for _, r := range s {
		switch r {
		case '"':
			b.WriteString(`\"`)
		case '\\':
			b.WriteString(`\\`)
		case '\n':
			b.WriteString(`\n`)
		case '\t':
			b.WriteString(`\t`)
		case '\r':
			b.WriteString(`\r`)
		default:
			if r < 0x20 || r == 0x7f {
				fmt.Fprintf(&b, `\u%04X`, r)
			} else {
				b.WriteRune(r)
			}
		}
	}
	b.WriteByte('"')
	return b.String()
}

// Int formats an integer.
func Int(n int) string { return strconv.Itoa(n) }

// Bool formats a boolean.
func Bool(v bool) string { return strconv.FormatBool(v) }

// Float formats a number the way the style files write thresholds.
func Float(f float64) string { return strconv.FormatFloat(f, 'f', -1, 64) }

// StringArray formats an array of strings on one line.
func StringArray(values []string) string {
	parts := make([]string, len(values))
	for i, v := range values {
		parts[i] = String(v)
	}
	return "[" + strings.Join(parts, ", ") + "]"
}

// InlineTable formats { K = v, ... } in the given order.
func InlineTable(pairs []KeyValue) string {
	if len(pairs) == 0 {
		return "{}"
	}
	parts := make([]string, len(pairs))
	for i, kv := range pairs {
		parts[i] = FormatKey(kv.Key) + " = " + kv.Value
	}
	return "{ " + strings.Join(parts, ", ") + " }"
}

// FormatKey writes a key bare when it can be, quoted otherwise.
func FormatKey(key string) string {
	if key == "" {
		return `""`
	}
	for _, r := range key {
		if !(r >= 'A' && r <= 'Z' || r >= 'a' && r <= 'z' || r >= '0' && r <= '9' || r == '_' || r == '-') {
			return String(key)
		}
	}
	return key
}

// FormatTableName writes a dotted table name, quoting parts that need it.
func FormatTableName(table string) string {
	parts := strings.Split(table, ".")
	for i, part := range parts {
		parts[i] = FormatKey(part)
	}
	return strings.Join(parts, ".")
}
