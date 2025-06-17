package validation

import (
	"fmt"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/parsing/shared"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/extensions"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/rules/definitions"
)

type ParseTreeValidator struct {
	parseTree *shared.ParseTree[definitions.LexingTokenType]
}

func NewParseTreeValidator(parseTree *shared.ParseTree[definitions.LexingTokenType]) *ParseTreeValidator {
	return &ParseTreeValidator{parseTree: parseTree}
}

func (p *ParseTreeValidator) Validate() error {
	metadataNode := p.parseTree.Children[0]
	err := p.ensureFirstMetadataBlock(metadataNode)

	if err != nil {
		return err
	}

	err = p.ensureRequiredBlocksWithinMetadata(metadataNode)

	if err != nil {
		return err
	}

	err = p.ensureCorrectStrictnessAssignment(metadataNode)

	if err != nil {
		return err
	}

	return nil
}

func (p *ParseTreeValidator) ensureFirstMetadataBlock(firstNode *shared.ParseTree[definitions.LexingTokenType]) error {
	if firstNode == nil {
		return fmt.Errorf("did you pass an empty Ruleforge file? The first node is emtpy: %v", firstNode)
	}

	if firstNode.Symbol != definitions.ParseSymbolMetadataSection.String() {
		return fmt.Errorf("your Ruleforge file must start with a metadata block")
	}

	return nil
}

func (p *ParseTreeValidator) ensureRequiredBlocksWithinMetadata(metadataNode *shared.ParseTree[definitions.LexingTokenType]) error {
	symbols := metadataNode.GetNthGenDescendantSymbols(2)

	hasName := extensions.FindNumberOfMatchesInSlice(symbols, []string{definitions.ParseSymbolNameAssignment.String()}, false) > 0
	hasVersion := extensions.FindNumberOfMatchesInSlice(symbols, []string{definitions.ParseSymbolVersionAssignment.String()}, false) > 0
	hasStrictness := extensions.FindNumberOfMatchesInSlice(symbols, []string{definitions.ParseSymbolStrictnessAssignment.String()}, false) > 0

	if !hasName {
		return fmt.Errorf("the metadata block must have a name")
	}

	if !hasVersion {
		return fmt.Errorf("the metadata block must have a version")
	}

	if !hasStrictness {
		return fmt.Errorf("the metadata block must have a strictness")
	}

	return nil
}

func (p *ParseTreeValidator) ensureCorrectStrictnessAssignment(metadataNode *shared.ParseTree[definitions.LexingTokenType]) error {
	strictnessAssignmentNode := metadataNode.FindSymbolNode(definitions.ParseSymbolStrictnessAssignment.String())

	assignmentIdentifierNode := strictnessAssignmentNode.Children[4]
	assignmentIdentifierValueNode := assignmentIdentifierNode.Children[0]

	valueNodeString := assignmentIdentifierValueNode.Token.ValueToString()
	numMatches := extensions.FindNumberOfMatchesInSlice([]string{
		definitions.ParseSymbolAll.String(),
		definitions.ParseSymbolSoft.String(),
		definitions.ParseSymbolSemiStrict.String(),
		definitions.ParseSymbolStrict.String(),
		definitions.ParseSymbolSuperStrict.String(),
	}, []string{valueNodeString}, false)

	if numMatches == 0 {
		tokens := make([]rune, 0)
		for _, child := range assignmentIdentifierNode.Children {
			if child.Token != nil {
				tokens = append(tokens, child.Token.ValueToRunes()...)
			}
		}

		completeString := string(tokens)

		return fmt.Errorf("the strictness assignment has an invalid value: %s", completeString)
	}

	return nil
}
