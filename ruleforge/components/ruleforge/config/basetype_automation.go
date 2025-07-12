package config

import (
	"encoding/csv"
	"fmt"
	"io"
	"os"
	"strconv"
)

// BaseTypeAutomationEntry represents a single configuration entry for base type automation.
type BaseTypeAutomationEntry struct {
	Category     string
	BaseType     string
	MinStackSize *int
	Style        string
	Priority     int
	Rarity       *string
}

// BaseTypeAutomationLoader is responsible for loading automation entries from a CSV file.
type BaseTypeAutomationLoader struct {
	filePath string
}

// NewBaseTypeAutomationLoader creates a new loader for the given file path.
func NewBaseTypeAutomationLoader(filePath string) *BaseTypeAutomationLoader {
	return &BaseTypeAutomationLoader{filePath: filePath}
}

// Load reads and parses the CSV file, returning a slice of BaseTypeAutomationEntry.
func (loader *BaseTypeAutomationLoader) Load() (*[]BaseTypeAutomationEntry, error) {
	// Open the CSV file
	file, err := os.Open(loader.filePath)
	if err != nil {
		return nil, fmt.Errorf("failed to open file %s: %w", loader.filePath, err)
	}
	defer file.Close()

	// Create a new CSV reader
	reader := csv.NewReader(file)

	// Read the header row and discard it
	if _, err := reader.Read(); err != nil {
		if err == io.EOF {
			return &[]BaseTypeAutomationEntry{}, nil // Return empty slice for empty file
		}
		return nil, fmt.Errorf("failed to read header from csv: %w", err)
	}

	var entries []BaseTypeAutomationEntry

	// Read the remaining records
	for {
		record, err := reader.Read()
		if err != nil {
			if err == io.EOF {
				break // End of file
			}
			return nil, fmt.Errorf("error reading csv record: %w", err)
		}

		if len(record) != 6 {
			return nil, fmt.Errorf("invalid record length: expected 6, got %d for record %v", len(record), record)
		}

		// --- Parse MinStackSize ---
		var minStackSize *int
		if record[2] != "" {
			val, err := strconv.Atoi(record[2])
			if err != nil {
				return nil, fmt.Errorf("could not parse MinStackSize '%s' to int: %w", record[2], err)
			}
			minStackSize = &val
		}

		var rarity *string
		if record[5] != "" {
			rarity = &record[5]
		}

		// --- Parse Priority ---
		tier, err := strconv.Atoi(record[4])
		if err != nil {
			return nil, fmt.Errorf("could not parse Priority '%s' to int: %w", record[4], err)
		}

		// --- Create and append entry ---
		entry := BaseTypeAutomationEntry{
			Category:     record[0],
			BaseType:     record[1],
			MinStackSize: minStackSize,
			Style:        record[3],
			Priority:     tier,
			Rarity:       rarity,
		}
		entries = append(entries, entry)
	}

	return &entries, nil
}
