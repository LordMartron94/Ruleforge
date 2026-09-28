package studio

import (
	"sort"
	"strings"

	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/config"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/data_generation"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/data_generation/model"
)

// ItemPool is what the loot simulator draws random drops from.
type ItemPool struct {
	Equipment  []EquipmentBase `json:"equipment"`
	Gems       []Gem           `json:"gems"`
	Uniques    []Unique        `json:"uniques"`
	Cards      []Card          `json:"cards"`
	Stackables []Stackable     `json:"stackables"`
}

// EquipmentBase is a wearable, flask or jewel base type.
type EquipmentBase struct {
	Name         string `json:"name"`
	Class        string `json:"class"`
	DropLevel    int    `json:"drop"`
	Width        int    `json:"w"`
	Height       int    `json:"h"`
	MaxSockets   int    `json:"sockets,omitempty"`
	// Base defences roll between the Min and the plain (maximum) value.
	Armour          int `json:"armour,omitempty"`
	ArmourMin       int `json:"armourMin,omitempty"`
	Evasion         int `json:"evasion,omitempty"`
	EvasionMin      int `json:"evasionMin,omitempty"`
	EnergyShield    int `json:"es,omitempty"`
	EnergyShieldMin int `json:"esMin,omitempty"`
	Ward            int `json:"ward,omitempty"`
	WardMin         int `json:"wardMin,omitempty"`
	Str          int    `json:"str,omitempty"`
	Dex          int    `json:"dex,omitempty"`
	Int          int    `json:"int,omitempty"`
}

// Gem is a skill or support gem.
type Gem struct {
	Name  string `json:"name"`
	Class string `json:"class"`
	Vaal  bool   `json:"vaal,omitempty"`
}

// Unique is a unique item and the base type it drops as.
type Unique struct {
	Name     string `json:"name"`
	BaseType string `json:"base"`
}

// Card is a divination card with its most recent price in chaos.
type Card struct {
	Name  string  `json:"name"`
	Value float64 `json:"value"`
}

// Stackable is a currency-like base type from a base type automation CSV.
type Stackable struct {
	Category string `json:"category"`
	BaseType string `json:"base"`
	MinStack int    `json:"minStack,omitempty"`
	Rarity   string `json:"rarity,omitempty"`
	Priority int    `json:"priority"`
}

// classByPoBType maps Path of Building base types to Path of Exile item classes.
var classByPoBType = map[string]string{
	"Amulet": "Amulets", "Belt": "Belts", "Body Armour": "Body Armours", "Boots": "Boots",
	"Gloves": "Gloves", "Helmet": "Helmets", "Shield": "Shields", "Bow": "Bows", "Claw": "Claws",
	"Dagger": "Daggers", "Jewel": "Jewels", "One Handed Axe": "One Hand Axes",
	"One Handed Mace": "One Hand Maces", "One Handed Sword": "One Hand Swords", "Quiver": "Quivers",
	"Ring": "Rings", "Sceptre": "Sceptres", "Staff": "Staves", "Tincture": "Tinctures",
	"Two Handed Axe": "Two Hand Axes", "Two Handed Mace": "Two Hand Maces",
	"Two Handed Sword": "Two Hand Swords", "Wand": "Wands",
}

// classBySubType covers the base types whose class depends on the PoB sub type.
var classBySubType = map[string]string{
	"Dagger/Rune Dagger":         "Rune Daggers",
	"One Handed Sword/Thrusting": "Thrusting One Hand Swords",
	"Staff/Warstaff":             "Warstaves",
	"Jewel/Abyss":                "Abyss Jewels",
	"Flask/Life":                 "Life Flasks",
	"Flask/Mana":                 "Mana Flasks",
	"Flask/Hybrid":               "Hybrid Flasks",
	"Flask/Utility":              "Utility Flasks",
	"Jewel/Charm":                "",
	"Jewel/Timeless":             "",
}

// inventorySize approximates each class's footprint; PoB does not record item sizes.
var inventorySize = map[string][2]int{
	"Amulets": {1, 1}, "Rings": {1, 1}, "Belts": {2, 1}, "Body Armours": {2, 3}, "Boots": {2, 2},
	"Gloves": {2, 2}, "Helmets": {2, 2}, "Shields": {2, 3}, "Bows": {2, 3}, "Claws": {2, 2},
	"Daggers": {1, 3}, "Rune Daggers": {1, 3}, "One Hand Axes": {2, 3}, "One Hand Maces": {2, 3},
	"One Hand Swords": {1, 3}, "Thrusting One Hand Swords": {1, 4}, "Quivers": {2, 3},
	"Sceptres": {2, 3}, "Staves": {2, 4}, "Warstaves": {2, 4}, "Two Hand Axes": {2, 4},
	"Two Hand Maces": {2, 4}, "Two Hand Swords": {2, 4}, "Wands": {1, 3}, "Jewels": {1, 1},
	"Abyss Jewels": {1, 1}, "Life Flasks": {1, 2}, "Mana Flasks": {1, 2}, "Hybrid Flasks": {1, 2},
	"Utility Flasks": {1, 2}, "Tinctures": {1, 2},
}

// ClassForBase returns the Path of Exile item class of a PoB base, or "" when the
// simulator does not generate that kind of item.
func ClassForBase(base model.ItemBase) string {
	if class, ok := classBySubType[base.Type+"/"+base.SubType]; ok {
		return class
	}
	return classByPoBType[base.Type]
}

// NewItemPool gathers the simulator's item pool from the loaded game data and
// the base type automation CSVs of every style.
func NewItemPool(
	bases []model.ItemBase,
	gems []model.Gem,
	uniques []model.Unique,
	economy map[string][]data_generation.EconomySnapshot,
	automation [][]config.BaseTypeAutomationEntry,
) ItemPool {
	var pool ItemPool
	known := make(map[string]bool)

	for _, base := range bases {
		class := ClassForBase(base)
		if class == "" || base.DropLevel == nil {
			continue
		}
		size := inventorySize[class]
		equipment := EquipmentBase{
			Name:       base.GetBaseType(),
			Class:      class,
			DropLevel:  *base.DropLevel,
			Width:      size[0],
			Height:     size[1],
			MaxSockets: base.SocketLimit,
			Str:        base.Req["str"],
			Dex:        base.Req["dex"],
			Int:        base.Req["int"],
		}
		if base.Armour != nil {
			equipment.Armour, equipment.ArmourMin = base.Armour.ArmourBaseMax, base.Armour.ArmourBaseMin
			equipment.Evasion, equipment.EvasionMin = base.Armour.EvasionBaseMax, base.Armour.EvasionBaseMin
			equipment.EnergyShield, equipment.EnergyShieldMin = base.Armour.EnergyShieldBaseMax, base.Armour.EnergyShieldBaseMin
			equipment.Ward, equipment.WardMin = base.Armour.WardBaseMax, base.Armour.WardBaseMin
		}
		pool.Equipment = append(pool.Equipment, equipment)
		known[equipment.Name] = true
	}

	seenGems := make(map[string]bool)
	for _, gem := range gems {
		name := gem.GetBaseType()
		if name == "" || seenGems[name] {
			continue
		}
		seenGems[name] = true
		class := "Skill Gems"
		if strings.HasSuffix(name, " Support") {
			class = "Support Gems"
		}
		pool.Gems = append(pool.Gems, Gem{Name: name, Class: class, Vaal: gem.IsVaalGem})
	}

	seenUniques := make(map[string]bool)
	for _, unique := range uniques {
		if !known[unique.BaseType] || seenUniques[unique.Name] {
			continue
		}
		seenUniques[unique.Name] = true
		pool.Uniques = append(pool.Uniques, Unique{Name: unique.Name, BaseType: unique.BaseType})
	}

	pool.Cards = latestCardValues(economy)

	seenStackables := make(map[Stackable]bool)
	for _, entries := range automation {
		for _, entry := range entries {
			stackable := Stackable{Category: entry.Category, BaseType: entry.BaseType, Priority: entry.Priority}
			if entry.MinStackSize != nil {
				stackable.MinStack = *entry.MinStackSize
			}
			if entry.Rarity != nil {
				stackable.Rarity = *entry.Rarity
			}
			if !seenStackables[stackable] {
				seenStackables[stackable] = true
				pool.Stackables = append(pool.Stackables, stackable)
			}
		}
	}
	return pool
}

// latestCardValues takes each divination card's price from the newest snapshot
// of any league, keeping the highest price when leagues disagree.
func latestCardValues(economy map[string][]data_generation.EconomySnapshot) []Card {
	values := make(map[string]float64)
	for _, snapshots := range economy {
		if len(snapshots) == 0 {
			continue
		}
		latest := snapshots[0]
		for _, snapshot := range snapshots[1:] {
			if snapshot.FetchedAt.After(latest.FetchedAt) {
				latest = snapshot
			}
		}
		for _, item := range latest.Items {
			if item.Class != "Divination Cards" {
				continue
			}
			if current, seen := values[item.BaseType]; !seen || item.ChaosValue > current {
				values[item.BaseType] = item.ChaosValue
			}
		}
	}
	cards := make([]Card, 0, len(values))
	for name, value := range values {
		cards = append(cards, Card{Name: name, Value: value})
	}
	sort.Slice(cards, func(i, j int) bool { return cards[i].Name < cards[j].Name })
	return cards
}
