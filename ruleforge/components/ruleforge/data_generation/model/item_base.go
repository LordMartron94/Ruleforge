package model

// ArmourProperties holds data specific to armour pieces.
type ArmourProperties struct {
	EvasionBaseMin      int     `json:"evasionBaseMin,omitempty"`
	EvasionBaseMax      int     `json:"evasionBaseMax,omitempty"`
	ArmourBaseMin       int     `json:"armourBaseMin,omitempty"`
	ArmourBaseMax       int     `json:"armourBaseMax,omitempty"`
	EnergyShieldBaseMin int     `json:"energyShieldBaseMin,omitempty"`
	EnergyShieldBaseMax int     `json:"energyShieldBaseMax,omitempty"`
	MovementPenalty     float64 `json:"movementPenalty,omitempty"`
}

// WeaponProperties holds data specific to weapons.
type WeaponProperties struct {
	PhysicalMin    int     `json:"physicalMin,omitempty"`
	PhysicalMax    int     `json:"physicalMax,omitempty"`
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
	DropLevel        *int              `json:"dropLevel,omitempty"`

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

func (i ItemBase) GetVariantID() string {
	return ""
}
