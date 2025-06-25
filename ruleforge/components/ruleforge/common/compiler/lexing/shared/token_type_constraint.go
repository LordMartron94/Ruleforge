package shared

import "fmt"

type TokenTypeConstraint interface {
	comparable
	fmt.Stringer
}
