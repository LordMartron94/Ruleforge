package symbols

// ParseSymbol is a string-backed enum of all parse-tree symbol names.
// These represent the non-terminal nodes in the grammar.
type ParseSymbol string

//goland:noinspection GoCommentStart
const (
	ParseSymbolMetadata            ParseSymbol = "MetadataSection"
	ParseSymbolSection             ParseSymbol = "Section"
	ParseSymbolVariable            ParseSymbol = "VariableDeclaration"
	ParseSymbolConditionList       ParseSymbol = "ConditionList"
	ParseSymbolCondition           ParseSymbol = "Condition"
	ParseSymbolConditionExpression ParseSymbol = "ConditionExpression"
	ParseSymbolAssignment          ParseSymbol = "Assignment"
	ParseSymbolSectionContent      ParseSymbol = "SectionContent"
	ParseSymbolConditions          ParseSymbol = "Conditions"
	ParseSymbolChainedAssignments  ParseSymbol = "ChainedAssignments"
	ParseSymbolChainedConditions   ParseSymbol = "ChainedConditions"
	ParseSymbolRuleExpression      ParseSymbol = "RuleExpression"
	ParseSymbolRuleSection         ParseSymbol = "RuleSection"
	ParseSymbolRules               ParseSymbol = "Rules"

	// --- Common Grammatical Roles ---
	ParseSymbolKey           ParseSymbol = "Key"
	ParseSymbolValue         ParseSymbol = "Value"
	ParseSymbolIdentifier    ParseSymbol = "Identifier"
	ParseSymbolOperator      ParseSymbol = "Operator"
	ParseSymbolKeyword       ParseSymbol = "Keyword"
	ParseSymbolWhitespace    ParseSymbol = "Whitespace"
	ParseSymbolAssignments   ParseSymbol = "AssignmentList"
	ParseSymbolBlockOperator ParseSymbol = "BlockOperator"

	// --- Fallback ---
	ParseSymbolAny ParseSymbol = "Any"
)

// String returns the literal string for the symbol.
func (p ParseSymbol) String() string {
	return string(p)
}
