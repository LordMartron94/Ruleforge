package model

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

func (e Essence) GetVariantID() string {
	return ""
}
