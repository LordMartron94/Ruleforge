package validation

import (
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/parsing/shared"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/rules/symbols"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/validation/helpers"
)

type SectionValidator struct {
	documentTree []*shared.ParseTree[symbols.LexingTokenType]
}

func NewSectionValidator(documentTree []*shared.ParseTree[symbols.LexingTokenType]) *SectionValidator {
	return &SectionValidator{documentTree: documentTree}
}

var requiredFields = []symbols.LexingTokenType{
	symbols.NameKeywordToken,
	symbols.DescriptionAssignmentKeywordToken,
}

var optionalFields = map[symbols.LexingTokenType]struct{}{
	symbols.StrictnessKeywordToken: {},
}

func (m *SectionValidator) Validate() error {
	for _, node := range m.documentTree {
		sections := node.FindAllSymbolNodes(symbols.ParseSymbolSection.String())

		for _, section := range sections {
			content := section.FindSymbolNode(symbols.ParseSymbolSectionContent.String())
			sectionMetadata := content.FindSymbolNode(symbols.ParseSymbolMetadata.String())

			err := helpers.NewMetadataFieldsValidator(sectionMetadata, helpers.ValidationOptions{
				RequiredFields:     requiredFields,
				OptionalFields:     optionalFields,
				CheckRequiredOrder: false,
			}).Validate()

			if err != nil {
				return err
			}

			if len(sectionMetadata.FindAllSymbolAndTokenTypes(symbols.ParseSymbolKey.String(), []symbols.LexingTokenType{symbols.StrictnessKeywordToken})) == 1 {
				return NewMetadataStrictnessValidator(sectionMetadata).Validate()
			}
		}
	}

	return nil
}
