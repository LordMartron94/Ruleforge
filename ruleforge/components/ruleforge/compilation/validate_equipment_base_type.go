package compilation

import (
	"fmt"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/config"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/data_generation/model"
	"log"
	"slices"
	"strings"
)

// WeaponClass represents the class of a weapon.
type WeaponClass string

type ArmorClass string

type ArmorType string

// Predefined weapon classes
const (
	Bow                     WeaponClass = "Bows"
	Claw                    WeaponClass = "Claws"
	Dagger                  WeaponClass = "Daggers"
	OneHandedAxe            WeaponClass = "One Hand Axes"
	OneHandedMace           WeaponClass = "One Hand Maces"
	OneHandedSword          WeaponClass = "One Hand Swords"
	ThrustingOneHandedSword WeaponClass = "Thrusting One Hand Swords"
	Quiver                  WeaponClass = "Quivers"
	RuneDagger              WeaponClass = "Rune Daggers"
	Sceptre                 WeaponClass = "Sceptres"
	Staff                   WeaponClass = "Staves"
	TwoHandedAxe            WeaponClass = "Two Hand Axes"
	TwoHandedMace           WeaponClass = "Two Hand Maces"
	TwoHandedSword          WeaponClass = "Two Hand Swords"
	Wand                    WeaponClass = "Wands"
	Warstaff                WeaponClass = "Warstaves"
)

// Predefined armor types
const (
	Belt       ArmorClass = "Belts"
	BodyArmors ArmorClass = "Body Armours"
	Boot       ArmorClass = "Boots"
	Glove      ArmorClass = "Gloves"
	Helmet     ArmorClass = "Helmets"
	Shield     ArmorClass = "Shields"
)

// Predefined armor subtype categories
const (
	Armor               ArmorType = "Armour"
	Evasion             ArmorType = "Evasion"
	EnergyShield        ArmorType = "Energy Shield"
	ArmourEvasion       ArmorType = "Armour/Evasion"
	ArmourEnergy        ArmorType = "Armour/Energy Shield"
	EvasionEnergy       ArmorType = "Evasion/Energy Shield"
	ArmourEvasionEnergy ArmorType = "Armour/Evasion/Energy Shield"
	Ward                ArmorType = "Ward"
)

// All possible weapon classes
var allWeaponClasses = []WeaponClass{
	Bow, Claw, Dagger,
	OneHandedAxe, OneHandedMace, OneHandedSword, ThrustingOneHandedSword,
	Quiver, RuneDagger, Sceptre,
	Staff,
	TwoHandedAxe, TwoHandedMace, TwoHandedSword,
	Wand, Warstaff,
}

// allArmorTypes is the list of valid ArmorType values.
var allArmorTypes = []ArmorType{
	Armor, Evasion, EnergyShield,
	ArmourEvasion, ArmourEnergy, EvasionEnergy,
	ArmourEvasionEnergy, Ward,
}

// Mappings from Path of Building strings to our enums
var pobTypeToWeaponClass = map[string]WeaponClass{
	"Bow":              Bow,
	"Claw":             Claw,
	"Dagger":           Dagger,
	"One Handed Axe":   OneHandedAxe,
	"One Handed Mace":  OneHandedMace,
	"One Handed Sword": OneHandedSword,
	"Quiver":           Quiver,
	"Rune Dagger":      RuneDagger,
	"Sceptre":          Sceptre,
	"Staff":            Staff,
	"Two Handed Axe":   TwoHandedAxe,
	"Two Handed Mace":  TwoHandedMace,
	"Two Handed Sword": TwoHandedSword,
	"Wand":             Wand,
	"Warstaff":         Warstaff,
}

var pobTypeToArmorClass = map[string]ArmorClass{
	"Belt":        Belt,
	"Body Armour": BodyArmors,
	"Boots":       Boot,
	"Gloves":      Glove,
	"Helmet":      Helmet,
	"Shield":      Shield,
}

var pobArmorTypeToArmorType = map[string]ArmorType{
	"Armour":                       Armor,
	"Evasion":                      Evasion,
	"Energy Shield":                EnergyShield,
	"Armour/Evasion":               ArmourEvasion,
	"Armour/Energy Shield":         ArmourEnergy,
	"Evasion/Energy Shield":        EvasionEnergy,
	"Armour/Evasion/Energy Shield": ArmourEvasionEnergy,
	"Ward":                         Ward,
}

// EquipmentPreset groups the allowed weapon classes and armor types for a build.
type EquipmentPreset struct {
	WeaponClasses []WeaponClass
	ArmorTypes    []ArmorType
}

// NewEquipmentPresetFromConfig converts a config.EquipmentPreset into a compilation.EquipmentPreset.
// It validates that every entry exists in our known enums.
func NewEquipmentPresetFromConfig(cp config.EquipmentPreset) (EquipmentPreset, error) {
	var wc []WeaponClass
	for _, w := range cp.DesiredWeaponClasses {
		candidate := WeaponClass(w)
		if !slices.Contains(allWeaponClasses, candidate) {
			return EquipmentPreset{}, fmt.Errorf("invalid weapon class %q", w)
		}
		wc = append(wc, candidate)
	}

	var at []ArmorType
	for _, a := range cp.DesiredArmourTypes {
		candidate := ArmorType(a)
		if !slices.Contains(allArmorTypes, candidate) {
			return EquipmentPreset{}, fmt.Errorf("invalid armor type %q", a)
		}
		at = append(at, candidate)
	}

	return EquipmentPreset{
		WeaponClasses: wc,
		ArmorTypes:    at,
	}, nil
}

// Build represents a character archetype with its equipment preset.
type Build struct {
	Name   string
	Preset EquipmentPreset
}

// DefaultBuilds holds all built-in presets.
var DefaultBuilds = []*Build{
	{
		Name: "TEMPLAR",
		Preset: EquipmentPreset{
			WeaponClasses: []WeaponClass{Sceptre, Staff, Wand, OneHandedMace, OneHandedSword, Warstaff},
			ArmorTypes:    []ArmorType{Armor, EnergyShield, ArmourEnergy, ArmourEvasionEnergy, Ward},
		},
	},
	{
		Name: "MARAUDER",
		Preset: EquipmentPreset{
			WeaponClasses: []WeaponClass{TwoHandedAxe, TwoHandedMace, TwoHandedSword, OneHandedAxe, OneHandedMace, OneHandedSword, Sceptre},
			ArmorTypes:    []ArmorType{Armor, ArmourEvasionEnergy},
		},
	},
	{
		Name: "SHADOW",
		Preset: EquipmentPreset{
			WeaponClasses: []WeaponClass{Dagger, Claw, RuneDagger, OneHandedSword, ThrustingOneHandedSword, Wand, Bow},
			ArmorTypes:    []ArmorType{Evasion, EnergyShield, EvasionEnergy, ArmourEvasionEnergy, Ward},
		},
	},
	{
		Name: "RANGER",
		Preset: EquipmentPreset{
			WeaponClasses: []WeaponClass{Bow, Quiver, OneHandedSword, TwoHandedSword, ThrustingOneHandedSword, Claw, OneHandedAxe},
			ArmorTypes:    []ArmorType{Evasion, ArmourEvasionEnergy},
		},
	},
	{
		Name: "WITCH",
		Preset: EquipmentPreset{
			WeaponClasses: []WeaponClass{Wand, RuneDagger, Sceptre, Staff},
			ArmorTypes:    []ArmorType{EnergyShield, ArmourEvasionEnergy, Ward},
		},
	},
	{
		Name: "DUELIST",
		Preset: EquipmentPreset{
			WeaponClasses: []WeaponClass{OneHandedSword, TwoHandedSword, OneHandedAxe, TwoHandedAxe, ThrustingOneHandedSword, Bow, Claw},
			ArmorTypes:    []ArmorType{Armor, Evasion, ArmourEvasion, ArmourEvasionEnergy},
		},
	},
}

// GetDefaultBuild retrieves one of the built-in presets by its name.
// Name matching is case-insensitive.
func GetDefaultBuild(name string) (*Build, error) {
	for _, b := range DefaultBuilds {
		if strings.EqualFold(b.Name, name) {
			return b, nil
		}
	}
	return nil, fmt.Errorf("default build preset %q not found", name)
}

// UnassociatedWeaponClasses returns all weapon classes not in the preset.
func (b *Build) UnassociatedWeaponClasses() []string {
	var list []string
	for _, wc := range allWeaponClasses {
		if !slices.Contains(b.Preset.WeaponClasses, wc) {
			list = append(list, string(wc))
		}
	}
	return list
}

// AssociatedWeaponClasses returns all weapon classes in the preset.
func (b *Build) AssociatedWeaponClasses() []string {
	var list []string
	for _, wc := range allWeaponClasses {
		if slices.Contains(b.Preset.WeaponClasses, wc) {
			list = append(list, string(wc))
		}
	}
	return list
}

// IsWeaponAssociated checks if an ItemBase's weapon type matches the build preset.
func (b *Build) IsWeaponAssociated(item model.ItemBase) bool {
	wc, ok := pobTypeToWeaponClass[item.Type]
	if item.Type == string(OneHandedSword) && item.SubType == "Thrusting" {
		wc = ThrustingOneHandedSword
	}
	if !ok {
		panic("unknown weapon type: " + item.Type)
	}
	return slices.Contains(b.Preset.WeaponClasses, wc)
}

// IsArmorAssociated checks if an ItemBase's armor type matches the build preset.
func (b *Build) IsArmorAssociated(item model.ItemBase) bool {
	ac, ok := pobTypeToArmorClass[item.Type]
	if !ok {
		panic("unknown armor class: " + item.Type)
	}
	// Belts are always included.
	if ac == Belt {
		return true
	}
	if item.SubType == "" {
		log.Printf("WARNING: Empty armor subtype, including by default: %s (%s)\n", item.Name, item.Type)
		return true
	}
	at, ok := pobArmorTypeToArmorType[item.SubType]
	if !ok {
		panic("unknown armor subtype: " + item.SubType)
	}
	return slices.Contains(b.Preset.ArmorTypes, at)
}
