package compilation

import "github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/data_generation"

type CompilerConfiguration struct {
	EconomyState *data_generation.TierStateOptions
	// StylesheetPath is the TOML stylesheet with the style templates and final styles.
	StylesheetPath string

	// Palette fills the stylesheet's colour names with "#RRGGBB" or "#RRGGBBAA" values.
	Palette map[string]string
}
