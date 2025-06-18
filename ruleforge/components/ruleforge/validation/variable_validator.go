package validation

import (
	"fmt"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/parsing/shared"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/rules/symbols"
	"slices"
)

type VariableValidator struct {
	documentTree []*shared.ParseTree[symbols.LexingTokenType]
}

func NewVariableValidator(documentTree []*shared.ParseTree[symbols.LexingTokenType]) *VariableValidator {
	return &VariableValidator{documentTree: documentTree}
}

func (m *VariableValidator) Validate() error {
	knownVariables := make([]string, 0)

	for _, node := range m.documentTree {
		variableDeclarations := node.FindAllSymbolNodes(symbols.ParseSymbolVariable.String())
		variableReferences := node.FindAllSymbolAndTokenTypes(symbols.ParseSymbolIdentifier.String(), []symbols.LexingTokenType{
			symbols.VariableReferenceToken,
		})

		for _, variableDeclaration := range variableDeclarations {
			assignments := variableDeclaration.FindAllSymbolNodes(symbols.ParseSymbolAssignment.String())
			for _, assignment := range assignments {
				variableIdentifier := assignment.Children[1].Token.ValueToString()
				knownVariables = append(knownVariables, variableIdentifier)
			}
		}

		for _, variableReference := range variableReferences {
			referenceValue := variableReference.Token.ValueToString()
			referenceValue = referenceValue[1:] // Remove the dollar prefix.
			if !slices.Contains(knownVariables, referenceValue) {
				return fmt.Errorf(`unknown variable: %s`, referenceValue)
			}
		}
	}

	return nil
}
