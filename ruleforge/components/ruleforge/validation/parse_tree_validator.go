package validation

import (
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/parsing/shared"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/rules/symbols"
)

// Validator runs a check on the metadata metadataBlock.
type Validator interface {
	Validate() error
}

type ParseTreeValidator struct {
	validators []Validator
}

// NewParseTreeValidator composes all your validators in one place.
func NewParseTreeValidator(tree *shared.ParseTree[symbols.LexingTokenType]) *ParseTreeValidator {
	metadataBlock := tree.Children[0]
	documentBlocks := tree.Children[1 : len(metadataBlock.Children)-1]

	return &ParseTreeValidator{
		validators: []Validator{
			NewMetadataDiscoveryValidator(tree),
			CorrectSyntaxValidator{
				blocks:       documentBlocks,
				ignoreTokens: []symbols.LexingTokenType{symbols.NewLineToken, symbols.WhitespaceToken},
			},
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
