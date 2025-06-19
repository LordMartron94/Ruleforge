package compilation

import (
	"fmt"
	"sort"
)

var conditionIdentifierToCompiledIdentifier = map[string]string{
	"@area_level": "AreaLevel",
	"@stack_size": "StackSize",
	"@item_type":  "BaseType",
}

type condition struct {
	identifier string
	operator   string
	value      string
}

func debugMap(m map[string][]string) {
	keys := make([]string, 0, len(m))
	for k := range m {
		keys = append(keys, k)
	}
	sort.Strings(keys) // sort import "sort"

	// Print in order
	for _, k := range keys {
		fmt.Printf("%s => %v\n", k, m[k])
	}
}

func (c *condition) ConstructCompiledCondition(variables *map[string][]string) string {
	compiledIdentifier := compileIdentifier(c.identifier)

	if c.value[0] != '$' {
		return c.constructString(compiledIdentifier, c.operator, c.value)
	}

	debugMap(*variables)

	variableValue, ok := (*variables)[c.value[1:]]

	if !ok {
		debugMap(*variables)
		panic(fmt.Sprintf("variable value not found in compiled condition: %s -> %s", c.identifier, c.value))
	}

	return c.constructString(compiledIdentifier, c.operator, variableValue[0])
}

func (c *condition) constructString(identifier, operator, value string) string {
	return fmt.Sprintf("%s %s \"%s\"", identifier, operator, value)
}

func compileIdentifier(identifier string) string {
	compiled, ok := conditionIdentifierToCompiledIdentifier[identifier]

	if !ok {
		panic("invalid identifier: " + identifier)
	}

	return compiled
}
