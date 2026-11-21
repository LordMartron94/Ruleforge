package document

import (
	"encoding/json"
	"fmt"
	"regexp"
	"strings"
)

// quotedText is what the lexer reads inside double quotes: letters, digits,
// spaces and . _ [ ] - / '.
var quotedText = regexp.MustCompile(`^[\p{L}\p{N} \t._\[\]\-/']*$`)

var (
	bareWord  = regexp.MustCompile(`^[\p{L}\p{N}._-]+$`)
	number    = regexp.MustCompile(`^-?[0-9]+(\.[0-9]+)?$`)
	reference = regexp.MustCompile(`^\$[\p{L}\p{N}._]+$`)
	condition = regexp.MustCompile(`^@[a-z_]+$`)
)

// Validate reports values the script syntax cannot hold, naming the item
// they are in, so that an edited document is never written in a form that
// would read back differently.
func Validate(doc *Document) error {
	return validateItems(doc.Items, "")
}

func validateItems(items []*Item, where string) error {
	for i, item := range items {
		place := describe(item, i, where)
		if err := validateItem(item, place); err != nil {
			return err
		}
	}
	return nil
}

func describe(item *Item, index int, where string) string {
	label := fmt.Sprintf("%s %d", item.Kind, index+1)
	switch item.Kind {
	case KindSection:
		for _, child := range item.Items {
			if child.Kind == KindMetadata {
				for _, field := range child.Items {
					if field.Kind == KindField && field.Key == "NAME" && field.Value != nil {
						label = fmt.Sprintf("section %q", field.Value.Text)
					}
				}
			}
		}
	case KindMacro:
		label = fmt.Sprintf("macro %q", item.Name)
	}
	if where == "" {
		return label
	}
	return where + ", " + label
}

func validateValue(value *Value, place, what string) error {
	if value == nil {
		return fmt.Errorf("%s: %s is missing", place, what)
	}
	ok := false
	switch value.Kind {
	case ValueString:
		ok = quotedText.MatchString(value.Text)
		if !ok {
			return fmt.Errorf("%s: %s %q has a character scripts cannot hold in quotes (allowed: letters, digits, spaces and . _ [ ] - / ')", place, what, value.Text)
		}
	case ValueVar:
		ok = reference.MatchString(value.Text)
	case ValueNumber:
		ok = number.MatchString(value.Text)
	case ValueKeyword:
		ok = bareWord.MatchString(value.Text)
	}
	if !ok {
		return fmt.Errorf("%s: %s %q is not a valid %s", place, what, value.Text, value.Kind)
	}
	return nil
}

func validateComment(text, place string) error {
	if strings.HasPrefix(text, "!![") {
		if !strings.HasSuffix(text, "]!!") || strings.Contains(text[3:len(text)-3], "]!!") {
			return fmt.Errorf("%s: a block comment must end with ]!!", place)
		}
		return nil
	}
	if !strings.HasPrefix(text, "!!") || strings.ContainsAny(text, "\r\n") {
		return fmt.Errorf("%s: a comment is one line starting with !!", place)
	}
	return nil
}

func validateTrailing(text, place string) error {
	trimmed := strings.TrimLeft(text, " \t")
	if trimmed == "" {
		return nil
	}
	if strings.HasPrefix(trimmed, "!![") {
		return validateComment(strings.TrimRight(trimmed, " \t"), place)
	}
	return validateComment(trimmed, place)
}

func validateConditions(conditions []Condition, place string) error {
	if len(conditions) == 0 {
		return fmt.Errorf("%s: needs at least one condition", place)
	}
	for _, c := range conditions {
		if !condition.MatchString(c.Identifier) {
			return fmt.Errorf("%s: %q is not a condition", place, c.Identifier)
		}
		switch c.Operator {
		case "==", "!=", "<", "<=", ">", ">=":
		default:
			return fmt.Errorf("%s: %q is not a comparison", place, c.Operator)
		}
		value := c.Value
		if err := validateValue(&value, place, c.Identifier+"'s value"); err != nil {
			return err
		}
	}
	return nil
}

func validateItem(item *Item, place string) error {
	if item.Raw != "" && item.Kind != KindBlank && item.Kind != KindSource {
		// Unchanged source text is written back as it was.
		return validateTrailing(item.Trailing, place)
	}
	if err := validateTrailing(item.Trailing, place); err != nil {
		return err
	}
	if err := validateTrailing(item.OpenTrailing, place); err != nil {
		return err
	}
	switch item.Kind {
	case KindBlank:
		if item.Raw != "" && strings.Trim(item.Raw, " \t\r\n") != "" {
			return fmt.Errorf("%s: an empty line holds text", place)
		}
	case KindComment:
		return validateComment(item.Text, place)
	case KindSource:
		if strings.TrimSpace(item.Raw) == "" {
			return fmt.Errorf("%s: the source text is empty", place)
		}
	case KindMetadata, KindSection, KindConditions, KindRules:
		return validateItems(item.Items, place)
	case KindField:
		switch item.Key {
		case "NAME", "VERSION", "STRICTNESS", "DESCRIPTION", "BUILD", "THEMES":
		default:
			return fmt.Errorf("%s: %q is not a METADATA field", place, item.Key)
		}
		return validateValue(item.Value, place, item.Key)
	case KindImport:
		return validateValue(item.Value, place, "the imported file")
	case KindVar:
		if len(item.Assignments) == 0 {
			return fmt.Errorf("%s: a var needs a name and a style", place)
		}
		for _, assignment := range item.Assignments {
			if !bareWord.MatchString(assignment.Name) || strings.ContainsAny(assignment.Name[:1], "0123456789") {
				return fmt.Errorf("%s: %q is not a valid variable name", place, assignment.Name)
			}
			if len(assignment.Values) == 0 {
				return fmt.Errorf("%s: var %s needs at least one style", place, assignment.Name)
			}
			for i := range assignment.Values {
				if err := validateValue(&assignment.Values[i], place, "var "+assignment.Name+"'s style"); err != nil {
					return err
				}
			}
			for _, block := range assignment.Overrides {
				if len(block) == 0 {
					return fmt.Errorf("%s: an !override block of var %s is empty", place, assignment.Name)
				}
				for i := range block {
					if err := validateValue(&block[i].Source, place, "an override's style"); err != nil {
						return err
					}
					if !quotedText.MatchString(block[i].Property) || block[i].Property == "" {
						return fmt.Errorf("%s: %q is not a property name", place, block[i].Property)
					}
				}
			}
		}
	case KindWhere:
		return validateConditions(item.Conditions, place)
	case KindRule:
		if err := validateConditions(item.Conditions, place); err != nil {
			return err
		}
		if err := validateValue(item.Style, place, "the style"); err != nil {
			return err
		}
		if err := validateValue(item.Action, place, "the action"); err != nil {
			return err
		}
		if item.Strictness != "" && !bareWord.MatchString(item.Strictness) {
			return fmt.Errorf("%s: %q is not a strictness", place, item.Strictness)
		}
	case KindMacro:
		if !quotedText.MatchString(item.Name) || item.Name == "" {
			return fmt.Errorf("%s: %q is not a macro name", place, item.Name)
		}
		return validateItems(item.Items, place)
	case KindParam:
		if !reference.MatchString(item.Key) {
			return fmt.Errorf("%s: %q is not a $parameter", place, item.Key)
		}
		return validateValue(item.Value, place, item.Key)
	default:
		return fmt.Errorf("%s: unknown item kind %q", place, item.Kind)
	}
	return nil
}

// Strip removes all source text, so that the whole document prints in the
// canonical layout. Source items keep their text; it is all they have.
func Strip(doc *Document) {
	stripItems(doc.Items)
}

func stripItems(items []*Item) {
	for _, item := range items {
		if item.Kind != KindSource {
			item.Raw, item.Indent, item.CloseIndent = "", "", ""
		}
		stripItems(item.Items)
	}
}

// Equivalent reports the first place where two documents differ in anything
// but layout. Source items are skipped, since their text is only understood
// once it is read again.
func Equivalent(a, b *Document) error {
	return equivalentItems(a.Items, b.Items, "")
}

func equivalentItems(a, b []*Item, where string) error {
	a, b = withoutBlanks(a), withoutBlanks(b)
	if len(a) != len(b) {
		return fmt.Errorf("%s: %d items instead of %d", orTop(where), len(b), len(a))
	}
	for i := range a {
		place := describe(a[i], i, where)
		if a[i].Kind == KindSource || b[i].Kind == KindSource {
			continue
		}
		if a[i].Kind != b[i].Kind {
			return fmt.Errorf("%s: reads back as a %s", place, b[i].Kind)
		}
		ka, kb := key(a[i]), key(b[i])
		if ka != kb {
			return fmt.Errorf("%s: reads back as %s instead of %s", place, kb, ka)
		}
		if err := equivalentItems(a[i].Items, b[i].Items, place); err != nil {
			return err
		}
	}
	return nil
}

func orTop(where string) string {
	if where == "" {
		return "the script"
	}
	return where
}

func withoutBlanks(items []*Item) []*Item {
	var out []*Item
	for _, item := range items {
		if item.Kind != KindBlank {
			out = append(out, item)
		}
	}
	return out
}

// key is an item's meaning without its layout or children.
func key(item *Item) string {
	shallow := *item
	shallow.Raw, shallow.Indent, shallow.CloseIndent, shallow.Items = "", "", "", nil
	shallow.Trailing = strings.TrimSpace(shallow.Trailing)
	shallow.OpenTrailing = strings.TrimSpace(shallow.OpenTrailing)
	if item.Kind == KindMacro {
		shallow.Inline = false
	}
	encoded, _ := json.Marshal(shallow)
	return string(encoded)
}
