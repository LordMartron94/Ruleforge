package internal

// BaseParsingRule provides common fields that most parsing rules will need.
// It is unexported and intended to be embedded in other rule structs.
type BaseParsingRule[T any] struct {
	SymbolString string
}

func (b *BaseParsingRule[T]) Symbol() string {
	return b.SymbolString
}
