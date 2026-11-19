package document

import (
	"strings"
)

// indentUnit is one level of canonical indentation.
const indentUnit = "  "

// Print writes a document as script text. Items that still carry their Raw
// source text are written exactly as they were read; the rest are written in
// the canonical layout.
func Print(doc *Document) string {
	var b strings.Builder
	printItems(&b, doc.Items, "")
	out := b.String()
	if !doc.FinalNewline {
		out = strings.TrimSuffix(out, "\n")
	}
	return out
}

func printItems(b *strings.Builder, items []*Item, indent string) {
	fieldWidth := keyWidth(items, KindField)
	paramWidth := keyWidth(items, KindParam)
	varWidths := varRunWidths(items)
	for i, item := range items {
		if item.Kind == KindBlank {
			if item.Raw != "" {
				b.WriteString(item.Raw)
			} else {
				b.WriteString(strings.Repeat("\n", max(item.Count, 1)))
			}
			continue
		}
		if item.Raw != "" {
			b.WriteString(item.Indent)
			b.WriteString(item.Raw)
		} else {
			b.WriteString(indent)
			width := 0
			switch item.Kind {
			case KindField:
				width = fieldWidth
			case KindParam:
				width = paramWidth
			case KindVar:
				width = varWidths[i]
			}
			b.WriteString(canonical(item, indent, width))
		}
		b.WriteString(item.Trailing)
		b.WriteString("\n")
	}
}

func keyWidth(items []*Item, kind string) int {
	width := 0
	for _, item := range items {
		if item.Kind == kind {
			width = max(width, len(item.Key))
		}
	}
	return width
}

// varRunWidths aligns the arrows of consecutive single-assignment var lines.
func varRunWidths(items []*Item) []int {
	widths := make([]int, len(items))
	for start := 0; start < len(items); {
		if items[start].Kind != KindVar {
			start++
			continue
		}
		end, width := start, 0
		for end < len(items) && items[end].Kind == KindVar {
			if len(items[end].Assignments) == 1 {
				width = max(width, len(items[end].Assignments[0].Name))
			}
			end++
		}
		for i := start; i < end; i++ {
			widths[i] = width
		}
		start = end
	}
	return widths
}

func pad(s string, width int) string {
	if len(s) >= width {
		return s
	}
	return s + strings.Repeat(" ", width-len(s))
}

func blockKeyword(kind string) string {
	switch kind {
	case KindMetadata:
		return "METADATA"
	case KindSection:
		return "SECTION"
	case KindConditions:
		return "SECTION_CONDITIONS"
	case KindRules:
		return "RULES"
	}
	return ""
}

func canonical(item *Item, indent string, width int) string {
	switch item.Kind {
	case KindComment:
		return item.Text
	case KindSource:
		return item.Raw
	case KindMetadata, KindSection, KindConditions, KindRules:
		var b strings.Builder
		b.WriteString(blockKeyword(item.Kind) + " {" + item.OpenTrailing + "\n")
		printItems(&b, item.Items, indent+indentUnit)
		b.WriteString(indent + "}")
		return b.String()
	case KindField:
		return pad(item.Key, width) + " => " + formatValue(item.Value)
	case KindImport:
		return "IMPORT " + formatValue(item.Value)
	case KindVar:
		var b strings.Builder
		for i, assignment := range item.Assignments {
			if i == 0 {
				b.WriteString("var " + formatAssignment(assignment, width))
				continue
			}
			b.WriteString("\n" + indent + indentUnit + "-> " + formatAssignment(assignment, 0))
		}
		return b.String()
	case KindWhere:
		return "WHERE " + formatConditions(item.Conditions)
	case KindRule:
		text := "WHERE " + formatConditions(item.Conditions) + " => " + formatValue(item.Style) + " => " + formatValue(item.Action)
		if item.Strictness != "" {
			text += " # " + item.Strictness
		}
		return text
	case KindMacro:
		head := `MACRO["` + item.Name + `"`
		if macroIsInline(item) {
			var b strings.Builder
			b.WriteString(head)
			for _, param := range item.Items {
				b.WriteString(" -> " + param.Key + " => " + formatValue(param.Value))
			}
			return b.String() + "]"
		}
		var b strings.Builder
		b.WriteString(head + item.OpenTrailing + "\n")
		printItems(&b, item.Items, indent+indentUnit)
		b.WriteString(indent + "]")
		return b.String()
	case KindParam:
		return "-> " + pad(item.Key, width) + " => " + formatValue(item.Value)
	}
	return ""
}

// macroIsInline reports whether a macro is written on one line: when it was
// written that way, or when it has at most one parameter and nothing else.
func macroIsInline(item *Item) bool {
	for _, child := range item.Items {
		if child.Kind != KindParam {
			return false
		}
	}
	return item.Inline || len(item.Items) <= 1
}

func formatValue(value *Value) string {
	if value == nil {
		return `""`
	}
	if value.Kind == ValueString {
		return `"` + value.Text + `"`
	}
	return value.Text
}

func formatConditions(conditions []Condition) string {
	parts := make([]string, len(conditions))
	for i, condition := range conditions {
		value := condition.Value
		parts[i] = condition.Identifier + " " + condition.Operator + " " + formatValue(&value)
	}
	return strings.Join(parts, " -> ")
}

func formatAssignment(assignment Assignment, width int) string {
	values := make([]string, len(assignment.Values))
	for i := range assignment.Values {
		values[i] = formatValue(&assignment.Values[i])
	}
	text := pad(assignment.Name, width) + " => " + strings.Join(values, " + ")
	for _, block := range assignment.Overrides {
		targets := make([]string, len(block))
		for i := range block {
			targets[i] = formatValue(&block[i].Source) + ` => "` + block[i].Property + `"`
		}
		text += " !override[" + strings.Join(targets, " -> ") + "]"
	}
	return text
}
