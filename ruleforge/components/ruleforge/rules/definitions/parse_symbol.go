package definitions

// ParseSymbol is a string-backed enum of all parse-tree symbol names.
type ParseSymbol string

const (
	ParseSymbolMetadataSection      ParseSymbol = "MetadataSection"
	ParseSymbolMetadataKeyword      ParseSymbol = "MetadataKeyword"
	ParseSymbolWhitespace           ParseSymbol = "Whitespace"
	ParseSymbolOpenBrace            ParseSymbol = "OpenBrace"
	ParseSymbolCloseBrace           ParseSymbol = "CloseBrace"
	ParseSymbolAssignments          ParseSymbol = "Assignments"
	ParseSymbolAny                  ParseSymbol = "Any"
	ParseSymbolWhitespaceToken      ParseSymbol = "WhitespaceToken"
	ParseSymbolNewLineToken         ParseSymbol = "NewLineToken"
	ParseSymbolNameAssignment       ParseSymbol = "NameAssignment"
	ParseSymbolVersionAssignment    ParseSymbol = "VersionAssignment"
	ParseSymbolStrictnessAssignment ParseSymbol = "StrictnessAssignment"
	ParseSymbolGeneralVariable      ParseSymbol = "GeneralVariable"
	ParseSymbolKey                  ParseSymbol = "Key"
	ParseSymbolWhitespaceBeforeOp   ParseSymbol = "WhitespaceBeforeOp"
	ParseSymbolAssignmentOp         ParseSymbol = "AssignmentOp"
	ParseSymbolWhitespaceAfterOp    ParseSymbol = "WhitespaceAfterOp"
	ParseSymbolValue                ParseSymbol = "Value"
	ParseSymbolIdentifier           ParseSymbol = "Identifier"
	ParseSymbolAll                  ParseSymbol = "ALL"
	ParseSymbolSoft                 ParseSymbol = "SOFT"
	ParseSymbolSemiStrict           ParseSymbol = "SEMI-STRICT"
	ParseSymbolStrict               ParseSymbol = "STRICT"
	ParseSymbolSuperStrict          ParseSymbol = "SUPER-STRICT"
	ParseVariableAssignmentKey      ParseSymbol = "VariableAssignmentKey"
)

// String returns the literal string for the symbol.
func (p ParseSymbol) String() string {
	return string(p)
}
