package model

type GemRequirements struct {
	Str int `json:"str,omitempty"`
	Dex int `json:"dex,omitempty"`
	Int int `json:"int,omitempty"`
}

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
