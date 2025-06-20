package compilation

import (
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/data_generation/model"
	"slices"
)

type PobUtils struct {
	armorClassesPoB  []string
	weaponClassesPoB []string
}

func NewPobUtils() *PobUtils {
	armorClassesPoB := make([]string, 0)
	weaponClassesPoB := make([]string, 0)
	for k := range pobTypeToArmorClass {
		armorClassesPoB = append(armorClassesPoB, k)
	}

	for k := range pobTypeToWeaponClass {
		weaponClassesPoB = append(weaponClassesPoB, k)
	}

	return &PobUtils{
		armorClassesPoB:  armorClassesPoB,
		weaponClassesPoB: weaponClassesPoB,
	}
}

func (p *PobUtils) IsArmor(pobItem model.ItemBase) bool {
	return slices.Contains(p.armorClassesPoB, pobItem.Type)
}

func (p *PobUtils) IsWeapon(pobItem model.ItemBase) bool {
	return slices.Contains(p.weaponClassesPoB, pobItem.Type)
}

func (p *PobUtils) IsFlask(pobItem model.ItemBase) bool {
	return pobItem.Type == "Flask"
}
