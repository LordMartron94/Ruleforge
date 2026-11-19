package config

import (
	"encoding/csv"
	"fmt"
	"io"
	"os"
	"strconv"
	"strings"
)

// CurrencyProfile is one currency that currency_tiering tiers, with how useful
// it is for crafting in solo self-found hardcore. The economy supplies value,
// rarity and trend; Crafting is the part no price list knows.
type CurrencyProfile struct {
	BaseType string
	// Crafting rates crafting use from 0 (none) to 10 (essential).
	Crafting float64
	// Note says why, for whoever edits the rating next.
	Note string
}

// CurrencyTiering configures the currency_tiering macro.
type CurrencyTiering struct {
	// Profiles is the CSV of currency profiles (BaseType,Crafting,Note).
	Profiles string `toml:"Profiles"`
	// CraftingWeight is the share of a currency's score that comes from its
	// crafting rating; the economy score (EconomyWeights) gets the rest.
	CraftingWeight float64 `toml:"CraftingWeight"`
}

const maxCraftingRating = 10

// LoadCurrencyProfiles reads a currency profile CSV with the header
// BaseType,Crafting,Note. Lines starting with # are comments.
func LoadCurrencyProfiles(path string) ([]CurrencyProfile, error) {
	file, err := os.Open(path)
	if err != nil {
		return nil, fmt.Errorf("opening currency profiles %s: %w", path, err)
	}
	defer file.Close()

	reader := csv.NewReader(file)
	reader.FieldsPerRecord = -1
	reader.Comment = '#'
	header, err := reader.Read()
	if err == io.EOF {
		return nil, nil
	}
	if err != nil {
		return nil, fmt.Errorf("reading currency profiles header: %w", err)
	}
	if len(header) < 2 || header[0] != "BaseType" || header[1] != "Crafting" {
		return nil, fmt.Errorf("currency profiles %s: the header must start with BaseType,Crafting", path)
	}

	var profiles []CurrencyProfile
	seen := make(map[string]bool)
	for line := 2; ; line++ {
		record, err := reader.Read()
		if err == io.EOF {
			break
		}
		if err != nil {
			return nil, fmt.Errorf("currency profiles line %d: %w", line, err)
		}
		if len(record) < 2 || len(record) > 3 {
			return nil, fmt.Errorf("currency profiles line %d: expected BaseType,Crafting[,Note], got %d fields", line, len(record))
		}
		baseType := strings.TrimSpace(record[0])
		if baseType == "" {
			return nil, fmt.Errorf("currency profiles line %d: empty base type", line)
		}
		if seen[baseType] {
			return nil, fmt.Errorf("currency profiles line %d: %s is listed twice", line, baseType)
		}
		seen[baseType] = true
		crafting, err := strconv.ParseFloat(strings.TrimSpace(record[1]), 64)
		if err != nil || !(crafting >= 0 && crafting <= maxCraftingRating) {
			return nil, fmt.Errorf("currency profiles line %d: crafting rating %q must be a number from 0 to %d", line, record[1], maxCraftingRating)
		}
		profile := CurrencyProfile{BaseType: baseType, Crafting: crafting}
		if len(record) == 3 {
			profile.Note = strings.TrimSpace(record[2])
		}
		profiles = append(profiles, profile)
	}
	return profiles, nil
}
