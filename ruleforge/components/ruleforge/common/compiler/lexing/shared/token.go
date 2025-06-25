package shared

import (
	"bytes"
	"fmt"
)

// Token represents a lexical token
type Token[T TokenTypeConstraint] struct {
	Type  T
	Value []byte
}

func (t Token[T]) Equals(other Token[T]) bool {
	return t.Type == other.Type && bytes.Equal(t.Value, other.Value)
}

func (t Token[T]) ValueToRunes() []rune {
	return []rune(string(t.Value))
}

func (t Token[T]) String() string {
	return fmt.Sprintf("[(%v) - '%s']", t.Type, t.Value)
}

func (t Token[T]) ValueToString() string {
	return string(t.Value)
}

func TokensToStrings[T TokenTypeConstraint](tokens []Token[T]) []string {
	stringsToReturn := make([]string, 0)
	for _, token := range tokens {
		stringsToReturn = append(stringsToReturn, fmt.Sprintf("%s ", token.String()))
	}
	return stringsToReturn
}
