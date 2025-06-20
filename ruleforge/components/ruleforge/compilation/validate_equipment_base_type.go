package compilation

import (
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/data_generation/model"
	"log"
	"slices"
)

type BuildType string

const (
	Templar  BuildType = "TEMPLAR"
	Marauder BuildType = "MARAUDER"
	Shadow   BuildType = "SHADOW"
	Ranger   BuildType = "RANGER"
	Witch    BuildType = "WITCH"
	Duelist  BuildType = "DUELIST"
)

var allBuilds = []BuildType{Templar, Marauder, Shadow, Ranger, Witch, Duelist}

func (b *BuildType) String() string {
	return string(*b)
}

type WeaponClass string
type ArmorClass string

//goland:noinspection GoCommentStart
const (
	// Weapons
	Bow            WeaponClass = "Bows"
	Claw           WeaponClass = "Claws"
	Dagger         WeaponClass = "Daggers"
	OneHandedAxe   WeaponClass = "One Hand Axes"
	OneHandedMace  WeaponClass = "One Hand Maces"
	OneHandedSword WeaponClass = "One Hand Swords"
	Quiver         WeaponClass = "Quivers"
	RuneDagger     WeaponClass = "Rune Daggers"
	Sceptre        WeaponClass = "Sceptres"
	Staff          WeaponClass = "Staves"
	TwoHandedAxe   WeaponClass = "Two Hand Axes"
	TwoHandedMace  WeaponClass = "Two Hand Maces"
	TwoHandedSword WeaponClass = "Two Hand Swords"
	Wand           WeaponClass = "Wands"
	Warstaff       WeaponClass = "Warstaves"

	// Armors
	Belt       ArmorClass = "Belts"
	BodyArmors ArmorClass = "Body Armours"
	Boot       ArmorClass = "Boots"
	Glove      ArmorClass = "Gloves"
	Helmet     ArmorClass = "Helmets"
	Shield     ArmorClass = "Shields"
)

var allWeaponClasses = []WeaponClass{
	Bow, Claw, Dagger,
	OneHandedAxe, OneHandedMace, OneHandedSword,
	Quiver, RuneDagger, Sceptre,
	Staff,
	TwoHandedAxe, TwoHandedMace, TwoHandedSword,
	Wand, Warstaff,
}

type ArmorType string

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

var buildWeaponry = map[BuildType][]WeaponClass{
	Templar: {
		Sceptre,
		Staff,
		Wand,
		OneHandedMace,
		OneHandedSword,
		Warstaff,
	},
	Marauder: {
		TwoHandedAxe,
		TwoHandedMace,
		TwoHandedSword,
		OneHandedAxe,
		OneHandedMace,
		OneHandedSword,
		Sceptre,
	},
	Shadow: {
		Dagger,
		Claw,
		RuneDagger,
		OneHandedSword,
		Wand,
		Bow,
	},
	Ranger: {
		Bow,
		Quiver,
		OneHandedSword,
		TwoHandedSword,
		Claw,
		OneHandedAxe,
	},
	Witch: {
		Wand,
		RuneDagger,
		Sceptre,
		Staff,
	},
	Duelist: {
		OneHandedSword,
		TwoHandedSword,
		OneHandedAxe,
		TwoHandedAxe,
		Bow,
		Claw,
	},
}
var buildArmor = map[BuildType][]ArmorType{
	Templar: {
		Armor,
		EnergyShield,
		ArmourEnergy,
		ArmourEvasionEnergy,
		Ward,
	},
	Marauder: {
		Armor,
		ArmourEvasionEnergy,
	},
	Shadow: {
		Evasion,
		EnergyShield,
		EvasionEnergy,
		ArmourEvasionEnergy,
		Ward,
	},
	Ranger: {
		Evasion,
		ArmourEvasionEnergy,
	},
	Witch: {
		EnergyShield,
		ArmourEvasionEnergy,
		Ward,
	},
	Duelist: {
		Armor,
		Evasion,
		ArmourEvasion,
		ArmourEvasionEnergy,
	},
}
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
	"Evasion":                      Armor,
	"Energy Shield":                EnergyShield,
	"Armour/Evasion":               ArmourEvasion,
	"Armour/Energy Shield":         ArmourEnergy,
	"Evasion/Energy Shield":        EnergyShield,
	"Armour/Evasion/Energy Shield": ArmourEvasionEnergy,
	"Ward":                         Ward,
}

func GetBuildType(build string) BuildType {
	for _, buildType := range allBuilds {
		if buildType.String() == build {
			return buildType
		}
	}

	panic("unknown build type: " + build)
}

func GetUnassociatedWeaponClasses(characterBuild BuildType) []string {
	unassociatedWeaponClasses := make([]string, 0)

	associatedWeaponry, ok := buildWeaponry[characterBuild]
	if !ok {
		panic("unknown build type: " + characterBuild)
	}

	for _, weaponClass := range allWeaponClasses {
		if !slices.Contains(associatedWeaponry, weaponClass) {
			unassociatedWeaponClasses = append(unassociatedWeaponClasses, string(weaponClass))
		}
	}

	return unassociatedWeaponClasses
}

func GetAssociatedWeaponClasses(characterBuild BuildType) []string {
	associatedWeaponClasses := make([]string, 0)

	associatedWeaponry, ok := buildWeaponry[characterBuild]
	if !ok {
		panic("unknown build type: " + characterBuild)
	}

	for _, weaponClass := range allWeaponClasses {
		if slices.Contains(associatedWeaponry, weaponClass) {
			associatedWeaponClasses = append(associatedWeaponClasses, string(weaponClass))
		}
	}

	return associatedWeaponClasses
}

func IsWeaponAssociatedWithBuild(weapon model.ItemBase, characterBuild BuildType) bool {
	associatedWeaponry, ok := buildWeaponry[characterBuild]

	if !ok {
		panic("unknown build type: " + characterBuild.String())
	}

	weaponClass, ok := pobTypeToWeaponClass[weapon.Type]

	if !ok {
		panic("unknown weapon type: " + weapon.Type)
	}

	if slices.Contains(associatedWeaponry, weaponClass) {
		return true
	}

	return false
}

func IsArmorAssociatedWithBuild(armor model.ItemBase, characterBuild BuildType) bool {
	associatedArmorTypes, ok := buildArmor[characterBuild]

	if !ok {
		panic("unknown build type: " + characterBuild.String())
	}

	armorClass, ok := pobTypeToArmorClass[armor.Type]

	if !ok {
		panic("unknown armor class: " + armor.Type)
	}

	if armorClass == Belt {
		return true
	}

	if armor.SubType == "" {
		log.Println("WARNING: Empty armor type. Including for build!")
		return true
	}

	armorType, ok := pobArmorTypeToArmorType[armor.SubType]
	if !ok {
		panic("unknown armor type: " + armor.SubType)
	}

	if slices.Contains(associatedArmorTypes, armorType) {
		return true
	}

	return false
}
