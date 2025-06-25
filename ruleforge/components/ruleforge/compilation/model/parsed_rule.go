package model

import "github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/config"

type ParsedRule struct {
	Style          *config.Style
	Action         RuleType
	Conditions     []Condition
	Variables      *map[string][]string
	ValidBaseTypes []string
}
