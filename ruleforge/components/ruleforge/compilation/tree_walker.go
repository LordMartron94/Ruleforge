package compilation

import (
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/parsing/shared"
	model2 "github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/compilation/model"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/rules/symbols"
)

// --- Intermediate Data Structures ---
// These structs hold the raw data extracted from the parse tree.

// ExtractedMetadata holds the raw values from the script's metadata block.
type ExtractedMetadata struct {
	Name       string
	Version    string
	Strictness string
	Build      string
}

// ExtractedSection holds the raw data for a single section block.
type ExtractedSection struct {
	Name        string
	Description string
	Conditions  []model2.Condition
	// We pass the raw nodes to the RuleGenerator to handle.
	RuleNodes []*shared.ParseTree[symbols.LexingTokenType]
}

// --- TreeWalker ---

// TreeWalker is responsible for navigating the parse tree and extracting raw data.
type TreeWalker struct {
	parseTree *shared.ParseTree[symbols.LexingTokenType]
}

// NewTreeWalker creates a new TreeWalker.
func NewTreeWalker(tree *shared.ParseTree[symbols.LexingTokenType]) *TreeWalker {
	return &TreeWalker{parseTree: tree}
}

// ExtractMetadata finds the metadata block and extracts its key-value pairs.
func (tw *TreeWalker) ExtractMetadata() ExtractedMetadata {
	meta := ExtractedMetadata{
		Version:    "<unknown>",
		Strictness: "<unknown>",
	}
	metadataNode := tw.parseTree.FindSymbolNode(symbols.ParseSymbolRootMetadata.String())
	if metadataNode == nil {
		return meta // Return default if not found
	}

	assignments := metadataNode.FindAllSymbolNodes(symbols.ParseSymbolAssignment.String())
	for _, assignment := range assignments {
		key, value := extractAssignmentKeyAndValue(assignment)
		switch key {
		case "NAME":
			meta.Name = value
		case "VERSION":
			meta.Version = value
		case "STRICTNESS":
			meta.Strictness = value
		case "BUILD":
			meta.Build = value
		}
	}
	return meta
}

// ExtractVariables finds all variable declarations and returns them as a map.
func (tw *TreeWalker) ExtractVariables() map[string][]string {
	variables := make(map[string][]string)
	variableNodes := tw.parseTree.FindAllSymbolNodes(symbols.ParseSymbolVariable.String())

	for _, variableNode := range variableNodes {
		assignmentNodes := variableNode.FindAllSymbolNodes(symbols.ParseSymbolAssignment.String())
		for _, assignmentNode := range assignmentNodes {
			identifier := assignmentNode.Children[1].Token.ValueToString()
			valueNodes := assignmentNode.FindAllSymbolNodes(symbols.ParseSymbolValue.String())
			assignments := make([]string, 0, len(valueNodes))
			for _, valueNode := range valueNodes {
				assignments = append(assignments, valueNode.Token.ValueToString())
			}
			variables[identifier] = assignments
		}
	}
	return variables
}

// ExtractSections finds all section blocks and extracts their data into a slice.
func (tw *TreeWalker) ExtractSections() []ExtractedSection {
	var extracted []ExtractedSection
	sectionNodes := tw.parseTree.FindAllSymbolNodes(symbols.ParseSymbolSection.String())

	for _, sectionNode := range sectionNodes {
		sectionMetadataNode := sectionNode.FindSymbolNode(symbols.ParseSymbolSectionMetadata.String())
		assignments := sectionMetadataNode.FindAllSymbolNodes(symbols.ParseSymbolAssignment.String())

		sectionName := "<unknown>"
		sectionDescription := "<unknown>"
		for _, assignment := range assignments {
			key, value := extractAssignmentKeyAndValue(assignment)
			switch key {
			case "NAME":
				sectionName = value
			case "DESCRIPTION":
				sectionDescription = value
			}
		}

		conditionListNode := sectionNode.FindSymbolNode(symbols.ParseSymbolConditionList.String())
		sectionConditions := retrieveConditions(conditionListNode)

		ruleListNode := sectionNode.FindSymbolNode(symbols.ParseSymbolRules.String())
		var ruleNodes []*shared.ParseTree[symbols.LexingTokenType]
		if ruleListNode != nil {
			ruleNodes = ruleListNode.Children
		}

		extracted = append(extracted, ExtractedSection{
			Name:        sectionName,
			Description: sectionDescription,
			Conditions:  sectionConditions,
			RuleNodes:   ruleNodes,
		})
	}
	return extracted
}

// --- Unexported Helpers ---

// extractAssignmentKeyAndValue is a low-level helper to get a key and value from an assignment node.
func extractAssignmentKeyAndValue(assignment *shared.ParseTree[symbols.LexingTokenType]) (string, string) {
	if len(assignment.Children) < 3 {
		return "", ""
	}
	key := assignment.Children[0].Token.ValueToString()
	value := assignment.Children[2].Token.ValueToString()
	return key, value
}

// retrieveConditions extracts all raw condition expressions from a given node.
func retrieveConditions(node *shared.ParseTree[symbols.LexingTokenType]) []model2.Condition {
	if node == nil {
		return nil
	}
	conditionNodes := node.FindAllSymbolNodes(symbols.ParseSymbolConditionExpression.String())
	conditions := make([]model2.Condition, len(conditionNodes))

	for i, conditionNode := range conditionNodes {
		if len(conditionNode.Children) < 4 {
			continue
		}
		identifier := conditionNode.Children[1].Token.ValueToString()
		operator := conditionNode.Children[2].Token.ValueToString()
		value := conditionNode.Children[3].Token.ValueToString()

		conditions[i] = model2.Condition{
			Identifier: identifier,
			Operator:   operator,
			Value:      []string{value},
		}
	}
	return conditions
}
