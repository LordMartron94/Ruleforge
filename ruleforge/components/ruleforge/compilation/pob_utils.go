package compilation

import (
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/data_generation"
	"slices"
)

type PobUtils struct {
	armorClassesPoB []string
}

func NewPobUtils() *PobUtils {
	armorClassesPoB := make([]string, 0)
	for k := range pobTypeToArmorClass {
		armorClassesPoB = append(armorClassesPoB, k)
	}

	return &PobUtils{
		armorClassesPoB: armorClassesPoB,
	}
}

func (p *PobUtils) IsArmor(pobItem data_generation.ItemBase) bool {
	return slices.Contains(p.armorClassesPoB, pobItem.Type)
}
