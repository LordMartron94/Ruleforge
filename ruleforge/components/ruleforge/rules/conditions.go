package rules

import "github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/rules/symbols"

// ConditionSpec describes a script condition for editors: what its values
// are and how they are usually written.
type ConditionSpec struct {
	ID    string `json:"id"`
	Label string `json:"label"`
	// Kind is the value type: "number", "choice", "baseType", "itemClass" or "sockets".
	Kind string `json:"kind"`
	// Choices are the values of a "choice" condition.
	Choices []string `json:"choices,omitempty"`
	// Quoted says whether values are written in quotes.
	Quoted bool   `json:"quoted"`
	Help   string `json:"help"`
}

// ConditionSpecs lists every condition a script can use.
var ConditionSpecs = []ConditionSpec{
	{ID: "@area_level", Label: "Area level", Kind: "number", Help: "The level of the area the item drops in."},
	{ID: "@rarity", Label: "Rarity", Kind: "choice", Choices: []string{"Normal", "Magic", "Rare", "Unique"}, Quoted: true, Help: "The item's rarity; < and > compare in that order."},
	{ID: "@item_type", Label: "Base type", Kind: "baseType", Quoted: true, Help: "The item's base type, such as Chaos Orb or Light Brigandine."},
	{ID: "@item_class", Label: "Item class", Kind: "itemClass", Quoted: true, Help: "The item's class, such as Body Armours or Stackable Currency."},
	{ID: "@stack_size", Label: "Stack size", Kind: "number", Help: "How many items are in the stack."},
	{ID: "@class_use", Label: "Build uses it", Kind: "choice", Choices: []string{"true", "false"}, Quoted: true, Help: "Whether the script's BUILD uses this weapon class or armour base. Expands into the matching classes and base types."},
	{ID: "@socket_group", Label: "Linked sockets", Kind: "sockets", Quoted: true, Help: "A linked group, such as 5 (five links), RGB or 4RGB."},
	{ID: "@sockets", Label: "Sockets", Kind: "sockets", Quoted: true, Help: "All sockets, linked or not, such as 6 or 5RGB."},
	{ID: "@height", Label: "Height", Kind: "number", Help: "Inventory height in cells."},
	{ID: "@width", Label: "Width", Kind: "number", Help: "Inventory width in cells."},
	{ID: "@map_tier", Label: "Map tier", Kind: "number", Help: "A map's tier."},
	{ID: "@quality", Label: "Quality", Kind: "number", Help: "Quality in percent."},
	{ID: "@corrupted", Label: "Corrupted", Kind: "choice", Choices: []string{"True", "False"}, Quoted: true, Help: "Whether the item is corrupted."},
	{ID: "@corrupted_mods", Label: "Corruption implicits", Kind: "number", Help: "How many implicits the item's corruption added."},
	{ID: "@fractured", Label: "Fractured", Kind: "choice", Choices: []string{"True", "False"}, Quoted: true, Help: "Whether the item has fractured modifiers."},
	{ID: "@base_defence_percentile", Label: "Base defence roll", Kind: "number", Help: "How high the base armour, evasion and energy shield rolled; 100 is max-rolled."},
	{ID: "@item_level", Label: "Item level", Kind: "number", Help: "The item's level, which caps the modifier tiers it can roll."},
	{ID: "@drop_level", Label: "Base drop level", Kind: "number", Help: "The level its base type starts dropping at; low means an early, weaker base."},
}

// ConditionKeywords lists the condition identifiers the lexer knows.
func ConditionKeywords() []string {
	var keywords []string
	for _, def := range keywordDefs {
		if def.token == symbols.ConditionKeywordToken {
			keywords = append(keywords, def.literal)
		}
	}
	return keywords
}

// Operators lists the comparison operators a condition can use.
var Operators = []string{"==", "!=", "<", "<=", ">", ">="}

// Strictnesses lists the STRICTNESS values.
var Strictnesses = []string{"ALL", "SOFT", "SEMI-STRICT", "STRICT", "SUPER-STRICT"}

// BuildKeywords lists the built-in BUILD values; configured equipment presets
// are written as quoted names.
var BuildKeywords = []string{"MARAUDER", "RANGER", "WITCH", "TEMPLAR", "DUELIST", "SHADOW"}
