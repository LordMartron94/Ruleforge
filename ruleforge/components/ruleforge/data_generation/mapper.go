package data_generation

import (
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/data_generation/model"
	"github.com/yuin/gopher-lua"
	"log"
)

// --- Internal Mapping & Factory Helpers ---

// mapItemBasesFromLua creates all item models from the Lua data.
func mapItemBasesFromLua(dataTable *lua.LTable) []model.ItemBase {
	var models []model.ItemBase
	dataTable.ForEach(func(key lua.LValue, value lua.LValue) {
		itemName := key.String()
		itemDataTable, ok := value.(*lua.LTable)
		if !ok {
			log.Printf("WARN: Value for key '%s' is not a table, skipping.", itemName)
			return
		}
		models = append(models, newItemBaseFromLuaTable(itemName, itemDataTable))
	})
	return models
}

// enrichItemBasesWithDropLevels handles the separate concern of fetching external data.
func enrichItemBasesWithDropLevels(models []model.ItemBase) {
	baseTypeNames := make([]string, 0, len(models))
	for _, itemModel := range models {
		baseTypeNames = append(baseTypeNames, itemModel.GetBaseType())
	}

	log.Printf("Fetching drop levels for %d base types concurrently...", len(baseTypeNames))
	dropLevelsMap := GetBaseTypeDropLevels(baseTypeNames, 150)
	log.Printf("Received %d drop levels. Mapping back to models...", len(dropLevelsMap))

	for i := range models {
		baseType := models[i].GetBaseType()
		if dropLevel, ok := dropLevelsMap[baseType]; ok {
			models[i].DropLevel = &dropLevel
		} else {
			log.Printf("WARN: Could not find a drop level for '%s'.", baseType)
		}
	}
}

// mapEssencesFromLua creates all essence models.
func mapEssencesFromLua(dataTable *lua.LTable) []model.Essence {
	var models []model.Essence
	dataTable.ForEach(func(key, value lua.LValue) {
		essenceId := key.String()
		essenceDataTable, ok := value.(*lua.LTable)
		if !ok {
			log.Printf("WARN: Value for essence key '%s' is not a table, skipping.", essenceId)
			return
		}
		models = append(models, newEssenceFromLuaTable(essenceId, essenceDataTable))
	})
	return models
}

// mapGemsFromLua creates all gem models.
func mapGemsFromLua(dataTable *lua.LTable) []model.Gem {
	var models []model.Gem
	dataTable.ForEach(func(key, value lua.LValue) {
		gemID := key.String()
		gemDataTable, ok := value.(*lua.LTable)
		if !ok {
			log.Printf("WARN: Value for gem key '%s' is not a table, skipping.", gemID)
			return
		}
		models = append(models, newGemFromLuaTable(gemID, gemDataTable))
	})
	return models
}

// newItemBaseFromLuaTable is the master factory, updated to handle all fields.
func newItemBaseFromLuaTable(name string, table *lua.LTable) model.ItemBase {
	itemModel := model.ItemBase{
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
		itemModel.Armour = newArmourProperties(armourTable)
	}
	if weaponTable, ok := table.RawGetString("weapon").(*lua.LTable); ok {
		itemModel.Weapon = newWeaponProperties(weaponTable)
	}
	if flaskTable, ok := table.RawGetString("flask").(*lua.LTable); ok {
		itemModel.Flask = newFlaskProperties(flaskTable)
	}
	if tinctureTable, ok := table.RawGetString("tincture").(*lua.LTable); ok {
		itemModel.Tincture = newTinctureProperties(tinctureTable)
	}

	return itemModel
}

func newEssenceFromLuaTable(id string, table *lua.LTable) model.Essence {
	return model.Essence{
		ID:   id,
		Name: getStringField(table, "name", ""),
		Type: getIntField(table, "type", 0),
		Tier: getIntField(table, "tier", 0),
		Mods: tableToStringMap(table, "mods"),
	}
}

func newGemFromLuaTable(id string, table *lua.LTable) model.Gem {
	return model.Gem{
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
		Requirements: model.GemRequirements{
			Str: getIntField(table, "reqStr", 0),
			Dex: getIntField(table, "reqDex", 0),
			Int: getIntField(table, "reqInt", 0),
		},
		NaturalMaxLevel: getIntField(table, "naturalMaxLevel", 0),
	}
}

// newArmourProperties creates an ArmourProperties struct from its Lua sub-table.
func newArmourProperties(table *lua.LTable) *model.ArmourProperties {
	return &model.ArmourProperties{
		WardBaseMin:         getIntField(table, "WardBaseMin", 0),
		WardBaseMax:         getIntField(table, "WardBaseMax", 0),
		EvasionBaseMin:      getIntField(table, "EvasionBaseMin", 0),
		EvasionBaseMax:      getIntField(table, "EvasionBaseMax", 0),
		ArmourBaseMin:       getIntField(table, "ArmourBaseMin", 0),
		ArmourBaseMax:       getIntField(table, "ArmourBaseMax", 0),
		EnergyShieldBaseMin: getIntField(table, "EnergyShieldBaseMin", 0),
		EnergyShieldBaseMax: getIntField(table, "EnergyShieldBaseMax", 0),
		MovementPenalty:     getNumberFieldFloat(table, "MovementPenalty", 0),
	}
}

// newWeaponProperties creates a WeaponProperties struct from its Lua sub-table.
func newWeaponProperties(table *lua.LTable) *model.WeaponProperties {
	return &model.WeaponProperties{
		PhysicalMin:    getIntField(table, "PhysicalMin", 0),
		PhysicalMax:    getIntField(table, "PhysicalMax", 0),
		CritChanceBase: getNumberFieldFloat(table, "CritChanceBase", 0),
		AttackRateBase: getNumberFieldFloat(table, "AttackRateBase", 0),
		Range:          getNumberFieldFloat(table, "Range", 0),
	}
}

// newFlaskProperties creates a FlaskProperties struct from its Lua sub-table.
func newFlaskProperties(table *lua.LTable) *model.FlaskProperties {
	return &model.FlaskProperties{
		Life:        getNumberFieldFloat(table, "life", 0),
		Mana:        getNumberFieldFloat(table, "mana", 0),
		Duration:    getNumberFieldFloat(table, "duration", 0),
		ChargesUsed: getNumberFieldFloat(table, "chargesUsed", 0),
		ChargesMax:  getNumberFieldFloat(table, "chargesMax", 0),
		Buff:        getListStringField(table, "buff"),
	}
}

func newTinctureProperties(table *lua.LTable) *model.TinctureProperties {
	return &model.TinctureProperties{
		ManaBurn: getNumberFieldFloat(table, "manaBurn", 0),
		Cooldown: getNumberFieldFloat(table, "cooldown", 0),
	}
}
