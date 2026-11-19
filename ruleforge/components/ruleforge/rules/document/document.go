// Package document reads and writes Ruleforge scripts for the Studio's script
// editor. A Document holds a script's structure together with its comments,
// blank lines and the source text of every construct, so that everything the
// editor did not change is written back exactly as it was.
package document

// Item kinds.
const (
	// KindComment is a "!!" line comment or a "!![ ... ]!!" block comment.
	KindComment = "comment"
	// KindBlank is one or more empty lines.
	KindBlank = "blank"
	// KindMetadata is a METADATA block: the script's at the top level, a
	// section's inside a section. Its items are fields, comments and blanks.
	KindMetadata = "metadata"
	// KindField is one METADATA assignment, such as NAME => "Gold".
	KindField = "field"
	// KindImport is IMPORT "other.rf".
	KindImport = "import"
	// KindVar is a var declaration with one or more chained assignments.
	KindVar = "var"
	// KindSection is a SECTION block. Its items are one metadata block, one
	// conditions block, one rules block, comments and blanks.
	KindSection = "section"
	// KindConditions is a SECTION_CONDITIONS block of where items.
	KindConditions = "conditions"
	// KindWhere is one section condition line: WHERE a -> b.
	KindWhere = "where"
	// KindRules is a RULES block of rules and macros.
	KindRules = "rules"
	// KindRule is WHERE conditions => style => action.
	KindRule = "rule"
	// KindMacro is MACRO["name" -> $key => value ...]. Its items are params,
	// comments and blanks.
	KindMacro = "macro"
	// KindParam is one macro parameter: -> $key => value.
	KindParam = "param"
	// KindSource is a construct kept as source text because the editor cannot
	// show its layout as a form. Raw holds the text, which may be edited.
	KindSource = "source"
)

// Value kinds.
const (
	ValueString  = "string"  // written in double quotes
	ValueVar     = "var"     // a $reference; Text includes the "$"
	ValueNumber  = "number"  // an unquoted number
	ValueKeyword = "keyword" // an unquoted keyword such as ALL or MARAUDER
)

// Document is a parsed Ruleforge script.
type Document struct {
	Items []*Item `json:"items"`
	// FinalNewline is false when the source did not end with a line break.
	FinalNewline bool `json:"finalNewline"`
}

// Item is one line-level construct of a script. Which fields are used depends on Kind.
type Item struct {
	Kind string `json:"kind"`

	// Raw is the construct's exact source text. The printer writes it
	// unchanged while it is set, so an editor must clear it (and the Raw of
	// every item that contains this one) when it changes the item.
	Raw string `json:"raw,omitempty"`
	// Indent is the whitespace before the item on its first line. It is only
	// used while Raw is set; otherwise the printer indents canonically.
	Indent string `json:"indent,omitempty"`
	// Trailing is whatever follows the item on its last line: spaces and an
	// optional comment.
	Trailing string `json:"trailing,omitempty"`
	// OpenTrailing is whatever follows a block's opening bracket on its line.
	OpenTrailing string `json:"openTrailing,omitempty"`
	// CloseIndent is the whitespace before a block's closing bracket.
	CloseIndent string `json:"closeIndent,omitempty"`

	// Text is a comment's full text, including its "!!" marker.
	Text string `json:"text,omitempty"`
	// Count is the number of empty lines of a blank item.
	Count int `json:"count,omitempty"`

	// Key is a field's keyword (NAME, VERSION, STRICTNESS, DESCRIPTION,
	// BUILD) or a param's $key.
	Key string `json:"key,omitempty"`
	// Value is a field's, param's or import's value.
	Value *Value `json:"value,omitempty"`

	// Name is a macro's name.
	Name string `json:"name,omitempty"`
	// Inline keeps a macro's parameters on the macro's own line.
	Inline bool `json:"inline,omitempty"`

	// Items are a block's or macro's contents.
	Items []*Item `json:"items,omitempty"`

	// Conditions are a rule's or where line's chained conditions.
	Conditions []Condition `json:"conditions,omitempty"`
	// Style is a rule's style: a stylesheet path or a $variable.
	Style *Value `json:"style,omitempty"`
	// Action is a rule's action: $Show or $Hide.
	Action *Value `json:"action,omitempty"`
	// Strictness is a rule's optional "# STRICTNESS" suffix.
	Strictness string `json:"strictness,omitempty"`

	// Assignments are a var declaration's assignments.
	Assignments []Assignment `json:"assignments,omitempty"`
}

// Value is a literal or reference as written in the script.
type Value struct {
	Text string `json:"text"`
	Kind string `json:"kind"`
}

// Condition is one "@identifier operator value" test.
type Condition struct {
	Identifier string `json:"id"`
	Operator   string `json:"op"`
	Value      Value  `json:"value"`
}

// Assignment is one "name => style + style !override[...]" assignment of a var declaration.
type Assignment struct {
	Name   string  `json:"name"`
	Values []Value `json:"values"`
	// Overrides are the !override[...] blocks, each a list of targets.
	Overrides [][]Override `json:"overrides,omitempty"`
}

// Override says which combined style wins a property when styles are merged.
type Override struct {
	Source   Value  `json:"source"`
	Property string `json:"property"`
}
