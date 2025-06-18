package validation

import (
	"fmt"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/parsing/shared"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/rules/symbols"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/validation/helpers"
)

type FilterMetadataValidator struct {
	metadataBlock *shared.ParseTree[symbols.LexingTokenType]
}

func NewMetadataDiscoveryValidator(metadataBlock *shared.ParseTree[symbols.LexingTokenType]) *FilterMetadataValidator {
	return &FilterMetadataValidator{metadataBlock: metadataBlock}
}

var requiredOrder = []symbols.LexingTokenType{
	symbols.NameKeywordToken,
	symbols.VersionKeywordToken,
	symbols.StrictnessKeywordToken,
}

func (m *FilterMetadataValidator) Validate() error {
	fv := helpers.NewMetadataFieldsValidator(m.metadataBlock, helpers.ValidationOptions{
		RequiredFields:     requiredOrder,
		OptionalFields:     nil,
		CheckRequiredOrder: true,
	})
	if err := fv.Validate(); err != nil {
		return fmt.Errorf("metadata block: %w", err)
	}
	sv := NewMetadataStrictnessValidator(m.metadataBlock)
	if err := sv.Validate(); err != nil {
		return fmt.Errorf("metadata block: %w", err)
	}

	return nil
}

// ——————————————————————————————————————————————————————
// 3) Validate STRICTNESS value is one of ALLOWED
// ——————————————————————————————————————————————————————

var allowedStrictness = map[symbols.LexingTokenType]bool{
	symbols.AllKeywordToken:         true,
	symbols.SoftKeywordToken:        true,
	symbols.SemiStrictKeywordToken:  true,
	symbols.StrictKeywordToken:      true,
	symbols.SuperStrictKeywordToken: true,
}

type MetadataStrictnessValidator struct {
	metadataSectionNode *shared.ParseTree[symbols.LexingTokenType]
}

func NewMetadataStrictnessValidator(node *shared.ParseTree[symbols.LexingTokenType]) *MetadataStrictnessValidator {
	return &MetadataStrictnessValidator{metadataSectionNode: node}
}

func (v *MetadataStrictnessValidator) Validate() error {
	// find the assignment whose Key is STRICTNESS, then dive into its Value.Keyword token
	assign := v.getStrictnessAssignment()
	valNode := assign.Children[2].Children[0]
	if !allowedStrictness[valNode.Token.Type] {
		return fmt.Errorf("invalid strictness value %q", valNode.Token.Value)
	}
	return nil
}

func (v *MetadataStrictnessValidator) getStrictnessAssignment() *shared.ParseTree[symbols.LexingTokenType] {
	assignmentList := v.metadataSectionNode.FindSymbolNode(symbols.ParseSymbolAssignments.String())

	for _, assign := range assignmentList.Children {
		keyTokenType := assign.Children[0].Token.Type

		if keyTokenType == symbols.StrictnessKeywordToken {
			return assign
		}
	}

	return nil
}
