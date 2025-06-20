package model

import (
	"fmt"
	"log"
	"slices"
	"sort"
)

var conditionIdentifierToCompiledIdentifier = map[string]string{
	"@area_level":   "AreaLevel",
	"@stack_size":   "StackSize",
	"@item_type":    "BaseType",
	"@item_class":   "Class",
	"@rarity":       "Rarity",
	"@sockets":      "Sockets",
	"@socket_group": "SocketGroup",
}

type Condition struct {
	Identifier string
	Operator   string
	Value      []string
}

func debugMap(m map[string][]string) {
	keys := make([]string, 0, len(m))
	for k := range m {
		keys = append(keys, k)
	}
	sort.Strings(keys)

	// Print in order
	for _, k := range keys {
		fmt.Printf("%s => %v\n", k, m[k])
	}
}

func (c *Condition) ConstructCompiledCondition(variables *map[string][]string, validBaseTypes []string) string {
	compiledIdentifier := compileIdentifier(c.Identifier)
	var compiledValues []string

	for _, value := range c.Value {
		if value[0] != '$' {
			if compiledIdentifier == "BaseType" {
				c.validateBaseType(value, validBaseTypes)
			}

			compiledValues = append(compiledValues, value)
			continue
		}

		variableValues, ok := (*variables)[value[1:]]

		if !ok {
			debugMap(*variables)
			panic(fmt.Sprintf("variable Value not found in compiled Condition: %s -> %s", c.Identifier, c.Value))
		}

		if compiledIdentifier == "BaseType" {
			c.validateBaseType(variableValues[0], validBaseTypes)
		}

		for _, variableValue := range variableValues {
			compiledValues = append(compiledValues, variableValue)
		}
	}

	return c.constructString(compiledIdentifier, c.Operator, compiledValues)
}

func (c *Condition) validateBaseType(baseType string, validBaseTypes []string) {
	if !slices.Contains(validBaseTypes, baseType) {
		log.Printf("WARNING: %s is not a valid BaseType (this could be due to it not being extracted from PoB yet, your game might run fine)", baseType)
	}
}

func (c *Condition) constructString(identifier, operator string, values []string) string {
	valueString := ""

	for _, value := range values {
		valueString += fmt.Sprintf("\"%s\" ", value)
	}

	return fmt.Sprintf("%s %s %s", identifier, operator, valueString)
}

func compileIdentifier(identifier string) string {
	compiled, ok := conditionIdentifierToCompiledIdentifier[identifier]

	if !ok {
		panic("invalid Identifier: " + identifier)
	}

	return compiled
}
