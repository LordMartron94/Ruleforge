package data_generation

import (
	"log"
	"path/filepath"
	"strings"

	"github.com/yuin/gopher-lua"
)

// --- Struct Definitions ---

// sanitizeBaseType strips anything between parentheses and any whitespace
// after the last character.
func sanitizeBaseType(baseType string) string {
	// Find the first opening parenthesis.
	startIndex := strings.Index(baseType, "(")
	if startIndex == -1 {
		return strings.TrimSpace(baseType)
	}

	// Find the last closing parenthesis.
	endIndex := strings.LastIndex(baseType, ")")
	if endIndex == -1 || endIndex < startIndex {
		return strings.TrimSpace(baseType)
	}

	result := baseType[:startIndex]

	if endIndex < len(baseType)-1 {
		result += baseType[endIndex+1:]
	}

	return strings.TrimSpace(result)
}

// GemRequirements holds the attribute requirements for a skill gem.
type GemRequirements struct {
	Str int `json:"str,omitempty"`
	Dex int `json:"dex,omitempty"`
	Int int `json:"int,omitempty"`
}

// Gem holds the data for a single skill gem.
type Gem struct {
	ID                       string          `json:"id"`
	Name                     string          `json:"name"`
	BaseTypeName             string          `json:"baseTypeName"`
	GameID                   string          `json:"gameId"`
	VariantID                string          `json:"variantId"`
	GrantedEffectID          string          `json:"grantedEffectId"`
	SecondaryGrantedEffectID string          `json:"secondaryGrantedEffectId,omitempty"`
	IsVaalGem                bool            `json:"isVaalGem,omitempty"`
	Tags                     map[string]bool `json:"tags"`
	TagString                string          `json:"tagString"`
	Requirements             GemRequirements `json:"requirements"`
	NaturalMaxLevel          int             `json:"naturalMaxLevel"`
}

func (g Gem) GetBaseType() string {
	return sanitizeBaseType(g.BaseTypeName)
}

// Essence holds data for a single Path of Exile essence.
type Essence struct {
	ID   string            `json:"id"`
	Name string            `json:"name"`
	Type int               `json:"type"`
	Tier int               `json:"tier"`
	Mods map[string]string `json:"mods"`
}

func (e Essence) GetBaseType() string {
	return sanitizeBaseType(e.Name)
}

// ArmourProperties holds data specific to armour pieces.
type ArmourProperties struct {
	EvasionBaseMin      float64 `json:"evasionBaseMin,omitempty"`
	EvasionBaseMax      float64 `json:"evasionBaseMax,omitempty"`
	ArmourBaseMin       float64 `json:"armourBaseMin,omitempty"`
	ArmourBaseMax       float64 `json:"armourBaseMax,omitempty"`
	EnergyShieldBaseMin float64 `json:"energyShieldBaseMin,omitempty"`
	EnergyShieldBaseMax float64 `json:"energyShieldBaseMax,omitempty"`
	MovementPenalty     float64 `json:"movementPenalty,omitempty"`
}

// WeaponProperties holds data specific to weapons.
type WeaponProperties struct {
	PhysicalMin    float64 `json:"physicalMin,omitempty"`
	PhysicalMax    float64 `json:"physicalMax,omitempty"`
	CritChanceBase float64 `json:"critChanceBase,omitempty"`
	AttackRateBase float64 `json:"attackRateBase,omitempty"`
	Range          float64 `json:"range,omitempty"`
}

// FlaskProperties holds data specific to flasks.
type FlaskProperties struct {
	Life        float64  `json:"life,omitempty"`
	Mana        float64  `json:"mana,omitempty"`
	Duration    float64  `json:"duration,omitempty"`
	ChargesUsed float64  `json:"chargesUsed,omitempty"`
	ChargesMax  float64  `json:"chargesMax,omitempty"`
	Buff        []string `json:"buff,omitempty"`
}

// TinctureProperties holds data specific to tinctures.
type TinctureProperties struct {
	ManaBurn float64 `json:"manaBurn,omitempty"`
	Cooldown float64 `json:"cooldown,omitempty"`
}

// ItemBase is the primary Go model, updated to handle all item types.
type ItemBase struct {
	Name             string            `json:"name"`
	Type             string            `json:"type"`
	SubType          string            `json:"subType,omitempty"`
	SocketLimit      int               `json:"socketLimit,omitempty"`
	Tags             map[string]bool   `json:"tags,omitempty"`
	InfluenceTags    map[string]string `json:"influenceTags,omitempty"`
	Implicit         string            `json:"implicit,omitempty"`
	ImplicitModTypes [][]string        `json:"implicitModTypes,omitempty"`
	Req              map[string]int    `json:"req,omitempty"`

	// --- Composition: Optional structs for specific data ---
	// Using pointers makes them optional. They will be nil if not present.
	Armour   *ArmourProperties   `json:"armour,omitempty"`
	Weapon   *WeaponProperties   `json:"weapon,omitempty"`
	Flask    *FlaskProperties    `json:"flask,omitempty"`
	Tincture *TinctureProperties `json:"tincture,omitempty"`
}

func (i ItemBase) GetBaseType() string {
	return sanitizeBaseType(i.Name)
}

// POBDataType is a generic constraint that lists all concrete structs
// that can be treated as an ItemInterface via their pointers.
type POBDataType interface {
	ItemBase | Essence | Gem

	GetBaseType() string
}

// --- Public Exporter Methods (Unchanged) ---

type PathOfBuildingExporter struct {
	luaExecutor *LuaExecutor
}

func NewPathOfBuildingExporter() *PathOfBuildingExporter {
	return &PathOfBuildingExporter{
		luaExecutor: NewLuaExecutor(),
	}
}

func GetBaseTypes[T POBDataType](data []T) []string {
	basetypes := make([]string, len(data))

	for i, dataItem := range data {
		basetypes[i] = dataItem.GetBaseType()
	}

	return basetypes
}

func (e *PathOfBuildingExporter) LoadItemBases(luaFilePath string) ([]ItemBase, error) {
	dataTable, err := e.luaExecutor.ExecuteScriptAsFunc(luaFilePath)
	if err != nil {
		return nil, err
	}

	var models []ItemBase
	dataTable.ForEach(func(key lua.LValue, value lua.LValue) {
		itemName := key.String()
		itemDataTable, ok := value.(*lua.LTable)
		if !ok {
			log.Printf("WARN: Value for key '%s' is not a table, skipping.", itemName)
			return
		}
		models = append(models, newItemBaseFromLuaTable(itemName, itemDataTable))
	})

	log.Printf("Successfully converted %d item base models from %s\n", len(models), filepath.Base(luaFilePath))
	return models, nil
}

func (e *PathOfBuildingExporter) LoadEssences(luaFilePath string) ([]Essence, error) {
	// 1. Delegate execution to our new executor method.
	dataTable, err := e.luaExecutor.ExecuteScriptWithReturn(luaFilePath)
	if err != nil {
		return nil, err
	}

	// 2. Map the results from the Lua table to our Go structs.
	var models []Essence
	dataTable.ForEach(func(key lua.LValue, value lua.LValue) {
		essenceID := key.String()
		essenceDataTable, ok := value.(*lua.LTable)
		if !ok {
			log.Printf("WARN: Value for key '%s' is not a table, skipping.", essenceID)
			return
		}
		models = append(models, newEssenceFromLuaTable(essenceID, essenceDataTable))
	})

	log.Printf("Successfully converted %d essence models from %s\n", len(models), filepath.Base(luaFilePath))
	return models, nil
}

func (e *PathOfBuildingExporter) LoadGems(luaFilePath string) ([]Gem, error) {
	dataTable, err := e.luaExecutor.ExecuteScriptWithReturn(luaFilePath)
	if err != nil {
		return nil, err
	}

	var models []Gem
	dataTable.ForEach(func(key lua.LValue, value lua.LValue) {
		gemID := key.String()
		gemDataTable, ok := value.(*lua.LTable)
		if !ok {
			log.Printf("WARN: Value for key '%s' is not a table, skipping.", gemID)
			return
		}
		models = append(models, newGemFromLuaTable(gemID, gemDataTable))
	})

	log.Printf("Successfully converted %d gem models from %s\n", len(models), filepath.Base(luaFilePath))
	return models, nil
}

// --- Internal Factory & Mapping Helpers (Updated) ---

// newItemBaseFromLuaTable is the master factory, updated to handle all fields.
func newItemBaseFromLuaTable(name string, table *lua.LTable) ItemBase {
	model := ItemBase{
		// Common fields
		Name:             name,
		Type:             getStringField(table, "type", ""),
		SubType:          getStringField(table, "subType", ""),
		SocketLimit:      getIntField(table, "socketLimit", 0),
		Implicit:         getStringField(table, "implicit", ""),
		Tags:             tableToBoolMap(table, "tags"),
		InfluenceTags:    tableToStringMap(table, "influenceTags"),
		ImplicitModTypes: tableToNestedStringSlice(table, "implicitModTypes"),
		Req:              tableToInterfaceMap(table, "req"),
	}

	// --- Check for optional sub-tables and map them if they exist ---
	if armourTable, ok := table.RawGetString("armour").(*lua.LTable); ok {
		model.Armour = newArmourProperties(armourTable)
	}
	if weaponTable, ok := table.RawGetString("weapon").(*lua.LTable); ok {
		model.Weapon = newWeaponProperties(weaponTable)
	}
	if flaskTable, ok := table.RawGetString("flask").(*lua.LTable); ok {
		model.Flask = newFlaskProperties(flaskTable)
	}
	if tinctureTable, ok := table.RawGetString("tincture").(*lua.LTable); ok {
		model.Tincture = newTinctureProperties(tinctureTable)
	}

	return model
}

func newEssenceFromLuaTable(id string, table *lua.LTable) Essence {
	return Essence{
		ID:   id,
		Name: getStringField(table, "name", ""),
		Type: getIntField(table, "type", 0),
		Tier: getIntField(table, "tier", 0),
		Mods: tableToStringMap(table, "mods"),
	}
}

func newGemFromLuaTable(id string, table *lua.LTable) Gem {
	return Gem{
		ID:                       id,
		Name:                     getStringField(table, "name", ""),
		BaseTypeName:             getStringField(table, "baseTypeName", ""),
		GameID:                   getStringField(table, "gameId", ""),
		VariantID:                getStringField(table, "variantId", ""),
		GrantedEffectID:          getStringField(table, "grantedEffectId", ""),
		SecondaryGrantedEffectID: getStringField(table, "secondaryGrantedEffectId", ""),
		IsVaalGem:                getBoolField(table, "vaalGem", false),
		Tags:                     tableToBoolMap(table, "tags"),
		TagString:                getStringField(table, "tagString", ""),
		Requirements: GemRequirements{
			Str: getIntField(table, "reqStr", 0),
			Dex: getIntField(table, "reqDex", 0),
			Int: getIntField(table, "reqInt", 0),
		},
		NaturalMaxLevel: getIntField(table, "naturalMaxLevel", 0),
	}
}

// newArmourProperties creates an ArmourProperties struct from its Lua sub-table.
func newArmourProperties(table *lua.LTable) *ArmourProperties {
	return &ArmourProperties{
		EvasionBaseMin:      getNumberField(table, "EvasionBaseMin", 0),
		EvasionBaseMax:      getNumberField(table, "EvasionBaseMax", 0),
		ArmourBaseMin:       getNumberField(table, "ArmourBaseMin", 0),
		ArmourBaseMax:       getNumberField(table, "ArmourBaseMax", 0),
		EnergyShieldBaseMin: getNumberField(table, "EnergyShieldBaseMin", 0),
		EnergyShieldBaseMax: getNumberField(table, "EnergyShieldBaseMax", 0),
		MovementPenalty:     getNumberField(table, "MovementPenalty", 0),
	}
}

// newWeaponProperties creates a WeaponProperties struct from its Lua sub-table.
func newWeaponProperties(table *lua.LTable) *WeaponProperties {
	return &WeaponProperties{
		PhysicalMin:    getNumberField(table, "PhysicalMin", 0),
		PhysicalMax:    getNumberField(table, "PhysicalMax", 0),
		CritChanceBase: getNumberField(table, "CritChanceBase", 0),
		AttackRateBase: getNumberField(table, "AttackRateBase", 0),
		Range:          getNumberField(table, "Range", 0),
	}
}

// newFlaskProperties creates a FlaskProperties struct from its Lua sub-table.
func newFlaskProperties(table *lua.LTable) *FlaskProperties {
	return &FlaskProperties{
		Life:        getNumberField(table, "life", 0),
		Mana:        getNumberField(table, "mana", 0),
		Duration:    getNumberField(table, "duration", 0),
		ChargesUsed: getNumberField(table, "chargesUsed", 0),
		ChargesMax:  getNumberField(table, "chargesMax", 0),
		Buff:        getListStringField(table, "buff"),
	}
}

func newTinctureProperties(table *lua.LTable) *TinctureProperties {
	return &TinctureProperties{
		ManaBurn: getNumberField(table, "manaBurn", 0),
		Cooldown: getNumberField(table, "cooldown", 0),
	}
}

// --- Generic Helper Functions ---

func tableToBoolMap(table *lua.LTable, key string) map[string]bool {
	result := make(map[string]bool)
	if subTable, ok := table.RawGetString(key).(*lua.LTable); ok {
		subTable.ForEach(func(k, v lua.LValue) {
			if boolVal, ok := v.(lua.LBool); ok {
				result[k.String()] = bool(boolVal)
			}
		})
	}
	return result
}

func tableToStringMap(table *lua.LTable, key string) map[string]string {
	result := make(map[string]string)
	if subTable, ok := table.RawGetString(key).(*lua.LTable); ok {
		subTable.ForEach(func(k, v lua.LValue) {
			result[k.String()] = v.String()
		})
	}
	return result
}

func tableToNestedStringSlice(table *lua.LTable, key string) [][]string {
	var result [][]string
	if subTable, ok := table.RawGetString(key).(*lua.LTable); ok {
		subTable.ForEach(func(_, value lua.LValue) {
			if innerTable, ok := value.(*lua.LTable); ok {
				var modGroup []string
				innerTable.ForEach(func(_, innerValue lua.LValue) {
					modGroup = append(modGroup, innerValue.String())
				})
				result = append(result, modGroup)
			}
		})
	}
	return result
}

func tableToInterfaceMap(table *lua.LTable, key string) map[string]int {
	result := make(map[string]int)
	if subTable, ok := table.RawGetString(key).(*lua.LTable); ok {
		subTable.ForEach(func(k, v lua.LValue) {
			if num, ok := v.(lua.LNumber); ok {
				result[k.String()] = int(num)
			}
		})
	}
	return result
}

func getBoolField(table *lua.LTable, key string, defaultValue bool) bool {
	val := table.RawGetString(key)
	if b, ok := val.(lua.LBool); ok {
		return bool(b)
	}
	return defaultValue
}

func getStringField(table *lua.LTable, key string, defaultValue string) string {
	val := table.RawGetString(key)
	if s, ok := val.(lua.LString); ok {
		return string(s)
	}
	return defaultValue
}

func getIntField(table *lua.LTable, key string, defaultValue int) int {
	val := table.RawGetString(key)
	if n, ok := val.(lua.LNumber); ok {
		return int(n)
	}
	return defaultValue
}

func getNumberField(table *lua.LTable, key string, defaultValue float64) float64 {
	val := table.RawGetString(key)
	if n, ok := val.(lua.LNumber); ok {
		return float64(n)
	}
	return defaultValue
}

func getListStringField(table *lua.LTable, key string) []string {
	result := make([]string, 0)
	if subTable, ok := table.RawGetString(key).(*lua.LTable); ok {
		subTable.ForEach(func(_, value lua.LValue) {
			result = append(result, value.String())
		})
	}
	return result
}
