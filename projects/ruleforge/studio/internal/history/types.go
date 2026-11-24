package history

import "sort"

type EconomyCacheItem struct {
	Class        string  `json:"class"`
	Name         string  `json:"name"`
	BaseType     string  `json:"base_type"`
	Count        int     `json:"count"`
	ListingCount int     `json:"listing_count"`
	ChaosValue   float64 `json:"chaos_value"`
	DivineValue  float64 `json:"divine_value"`
	ExaltedValue float64 `json:"exalted_value"`
	// Source distinguishes stash listings from exchange turnover. Legacy
	// snapshots keep their original numbers, but are not liquidity evidence.
	Source      string  `json:"source,omitempty"`
	Category    string  `json:"category,omitempty"`
	ItemID      string  `json:"item_id,omitempty"`
	Variant     string  `json:"variant,omitempty"`
	ItemLevel   int     `json:"item_level,omitempty"`
	VolumeChaos float64 `json:"volume_chaos,omitempty"`
	TradedUnits float64 `json:"traded_units,omitempty"`
}

var economyCategories = []EconomyCategory{
	{Kind: "UniqueWeapon", Class: "Uniques", Endpoint: "stash"},
	{Kind: "UniqueArmour", Class: "Uniques", Endpoint: "stash"},
	{Kind: "UniqueAccessory", Class: "Uniques", Endpoint: "stash"},
	{Kind: "UniqueFlask", Class: "Uniques", Endpoint: "stash"},
	{Kind: "UniqueJewel", Class: "Uniques", Endpoint: "stash"},
	{Kind: "SkillGem", Class: "Gems", Endpoint: "stash"},
	{Kind: "DivinationCard", Class: "Divination Cards", Endpoint: "exchange"},
	{Kind: "Tattoo", Class: "Tattoos", Endpoint: "exchange"},
	{Kind: "Currency", Class: "Currency", Endpoint: "exchange"},
	{Kind: "DeliriumOrb", Class: "Currency", Endpoint: "exchange"},
	{Kind: "Omen", Class: "Currency", Endpoint: "exchange"},
}

func sortedKeys[V any](m map[string]V) []string {
	keys := make([]string, 0, len(m))
	for key := range m {
		keys = append(keys, key)
	}
	sort.Strings(keys)
	return keys
}
