package helpers

import (
	"fmt"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/parsing/shared"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/rules/symbols"
)

// ValidationOptions allows clients to define validation rules for metadata fields.
type ValidationOptions struct {
	// RequiredFields specifies fields that *must* be present.
	// Their order dependency is determined by CheckRequiredOrder.
	RequiredFields []symbols.LexingTokenType

	// OptionalFields defines fields that are allowed but not required to be present.
	// These fields are not subject to order validation.
	OptionalFields map[symbols.LexingTokenType]struct{} // Using struct{} for a set

	// CheckRequiredOrder indicates whether the order of RequiredFields must be enforced.
	// If true, RequiredFields must appear in the exact order specified.
	// If false, RequiredFields must be present but can be in any order.
	CheckRequiredOrder bool
}

// MetadataFieldsValidator validates metadata fields based on provided options.
type MetadataFieldsValidator struct {
	metadataSectionNode *shared.ParseTree[symbols.LexingTokenType]
	options             ValidationOptions
}

// NewMetadataFieldsValidator creates a new MetadataFieldsValidator.
func NewMetadataFieldsValidator(node *shared.ParseTree[symbols.LexingTokenType], options ValidationOptions) *MetadataFieldsValidator {
	// Initialize OptionalFields if it's nil to prevent nil pointer panics
	if options.OptionalFields == nil {
		options.OptionalFields = make(map[symbols.LexingTokenType]struct{})
	}
	return &MetadataFieldsValidator{
		metadataSectionNode: node,
		options:             options,
	}
}

// Validate checks the metadata fields against the configured validation options.
func (v *MetadataFieldsValidator) Validate() error {
	assignmentListNode := v.metadataSectionNode.FindSymbolNode(symbols.ParseSymbolAssignments.String())
	if assignmentListNode == nil {
		// If there are no assignments, and we expect some required fields, it's an error.
		if len(v.options.RequiredFields) > 0 {
			return fmt.Errorf("expected metadata assignments but found none")
		}
		return nil // No assignments and no required fields, so valid.
	}

	seenRequiredFields := map[symbols.LexingTokenType]bool{}
	seenOptionalFields := map[symbols.LexingTokenType]bool{}
	var actualFieldOrder []symbols.LexingTokenType // To track the order of *all* fields encountered

	for _, assign := range assignmentListNode.Children {
		// Ensure the child is an assignment node and has a key token
		if len(assign.Children) < 1 || assign.Children[0].Token == nil {
			return fmt.Errorf("malformed metadata assignment: missing field key")
		}
		keyTok := assign.Children[0].Token.Type
		actualFieldOrder = append(actualFieldOrder, keyTok) // Keep track of the actual order

		// Check for duplicates across all fields (required and optional)
		if seenRequiredFields[keyTok] || seenOptionalFields[keyTok] {
			return fmt.Errorf("duplicate metadata field '%s'", keyTok)
		}

		// Determine if the field is optional or a required one
		if _, isOptional := v.options.OptionalFields[keyTok]; isOptional {
			seenOptionalFields[keyTok] = true
		} else {
			// Check if it's one of our expected required fields
			isExpectedRequired := false
			for _, reqField := range v.options.RequiredFields {
				if keyTok == reqField {
					isExpectedRequired = true
					break
				}
			}
			if isExpectedRequired {
				seenRequiredFields[keyTok] = true
			} else {
				// This field is neither in optional nor in required fields
				return fmt.Errorf("unknown metadata field '%s'", keyTok)
			}
		}
	}

	// 1. Validate all required fields are present
	for _, requiredField := range v.options.RequiredFields {
		if !seenRequiredFields[requiredField] {
			return fmt.Errorf("missing required metadata field '%s'", requiredField)
		}
	}

	// 2. Validate the order of required fields, ONLY if CheckRequiredOrder is true
	if v.options.CheckRequiredOrder {
		requiredIndex := 0
		for _, field := range actualFieldOrder {
			// If it's a required field (and not optional), check its position
			if _, isRequired := seenRequiredFields[field]; isRequired {
				if requiredIndex >= len(v.options.RequiredFields) {
					// This should ideally not happen if all required fields are found and checked above
					return fmt.Errorf("internal validation error: unexpected required field '%s' encountered during order check", field)
				}
				if field != v.options.RequiredFields[requiredIndex] {
					return fmt.Errorf("expected '%s' at position %d, got '%s'", v.options.RequiredFields[requiredIndex], requiredIndex+1, field)
				}
				requiredIndex++
			}
		}
	}

	return nil
}
