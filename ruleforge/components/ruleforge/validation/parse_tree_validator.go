package validation

import (
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/parsing/shared"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/rules/symbols"
)

// Validator runs a check on the metadata documentTree.
type Validator interface {
	Validate() error
}

type ParseTreeValidator struct {
	validators []Validator
}

// NewParseTreeValidator composes all your validators in one place.
func NewParseTreeValidator(tree *shared.ParseTree[symbols.LexingTokenType]) *ParseTreeValidator {
	metadataBlock := tree.Children[0]
	documentBlocks := tree.Children[1:]

	return &ParseTreeValidator{
		validators: []Validator{
			NewMetadataDiscoveryValidator(metadataBlock),
			CorrectSyntaxValidator{
				blocks: documentBlocks,
			},
			NewSectionValidator(documentBlocks),
			NewVariableValidator(documentBlocks),
		},
	}
}

func (p *ParseTreeValidator) Validate() error {
	for _, v := range p.validators {
		if err := v.Validate(); err != nil {
			return err
		}
	}
	return nil
}
