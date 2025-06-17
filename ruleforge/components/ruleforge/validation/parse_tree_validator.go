package validation

import (
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/parsing/shared"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/rules/definitions"
)

// Validator runs a check on the metadata blocks.
type Validator interface {
	Validate() error
}

type ParseTreeValidator struct {
	validators []Validator
}

func NewParseTreeValidator(tree *shared.ParseTree[definitions.LexingTokenType]) *ParseTreeValidator {
	// The metadata blocks
	md := tree.Children[0]
	blocks := tree.Children[1 : len(tree.Children)-1]

	return &ParseTreeValidator{
		validators: []Validator{
			FirstBlockValidator{node: md},
			RequiredFieldsValidator{node: md},
			StrictnessValidator{node: md},
			CorrectSyntaxValidator{blocks: blocks, ignoreTokens: []definitions.LexingTokenType{definitions.NewLineToken}},
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
