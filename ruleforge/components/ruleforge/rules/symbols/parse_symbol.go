package symbols

// ParseSymbol is a string-backed enum of all parse-tree symbol names.
// These represent the non-terminal nodes in the grammar.
type ParseSymbol string

//goland:noinspection GoCommentStart
const (
	ParseSymbolRootMetadata        ParseSymbol = "RootMetadataSection"
	ParseSymbolSectionMetadata     ParseSymbol = "SectionMetadata"
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
	ParseSymbolCombinedValue       ParseSymbol = "CombinedValue"
	ParseSymbolChainedValues       ParseSymbol = "ChainedValues"
	ParseSymbolFullValueExpression ParseSymbol = "FullValueExpression"
	ParseSymbolImport              ParseSymbol = "Import"

	// --- Common Grammatical Roles ---
	ParseSymbolKey           ParseSymbol = "Key"
	ParseSymbolValue         ParseSymbol = "Value"
	ParseSymbolIdentifier    ParseSymbol = "Identifier"
	ParseSymbolOperator      ParseSymbol = "Operator"
	ParseSymbolKeyword       ParseSymbol = "Keyword"
	ParseSymbolWhitespace    ParseSymbol = "Whitespace"
	ParseSymbolAssignments   ParseSymbol = "AssignmentList"
	ParseSymbolBlockOperator ParseSymbol = "BlockOperator"

	ParseSymbolMacroExpression ParseSymbol = "MacroExpression"
	ParseSymbolParameterList   ParseSymbol = "ParameterList"
	ParseSymbolParameter       ParseSymbol = "Parameter"

	ParseSymbolOverrideTarget         ParseSymbol = "OverrideTarget"
	ParseSymbolOverrideTargetList     ParseSymbol = "OverrideTargetList"
	ParseSymbolChainedOverrideTargets ParseSymbol = "ChainedOverrideTargets"
	ParseSymbolStyleOverride          ParseSymbol = "StyleOverride"
	ParseSymbolOptionalOverrides      ParseSymbol = "OptionalOverrides"

	// --- Fallback ---
	ParseSymbolAny ParseSymbol = "Any"
)

// String returns the literal string for the symbol.
func (p ParseSymbol) String() string {
	return string(p)
}
