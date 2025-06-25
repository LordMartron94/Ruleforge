package data_generation

import (
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/data_generation/model"
	"log"
	"strconv"
	"strings"
)

// parseUniqueItemString takes a raw multi-line string from the Lua file
// and parses it into a structured UniqueItem.
func parseUniqueItemString(data string) *model.Unique {
	lines := strings.Split(strings.TrimSpace(data), "\n")
	if len(lines) < 2 {
		return nil // Not a valid item block
	}

	item := &model.Unique{
		Name:      strings.TrimSpace(lines[0]),
		BaseType:  strings.TrimSpace(lines[1]),
		Variants:  make([]string, 0),
		Modifiers: make([]string, 0),
	}

	// Start parsing from the third line
	for i := 2; i < len(lines); i++ {
		line := strings.TrimSpace(lines[i])
		if line == "" {
			continue
		}

		// Use SplitN to handle keys and values
		parts := strings.SplitN(line, ":", 2)
		key := parts[0]
		var value string
		if len(parts) > 1 {
			value = strings.TrimSpace(parts[1])
		}

		switch key {
		case "Variant":
			item.Variants = append(item.Variants, value)
		case "League":
			item.League = value
		case "Source":
			item.Source = value
		case "LevelReq":
			level, err := strconv.Atoi(value)
			if err != nil {
				log.Printf("WARN: Could not parse LevelReq '%s' for item '%s'", value, item.Name)
				continue
			}
			item.LevelReq = level
		case "Implicits":
			implicits, err := strconv.Atoi(value)
			if err != nil {
				log.Printf("WARN: Could not parse Implicits '%s' for item '%s'", value, item.Name)
				continue
			}
			item.Implicits = implicits
		default:
			item.Modifiers = append(item.Modifiers, line)
		}
	}

	return item
}
