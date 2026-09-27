package compilation

type CompilerConfiguration struct {
	// StylesheetPath is the TOML stylesheet with the style templates and final styles.
	StylesheetPath string

	// Palette fills the stylesheet's colour names with "#RRGGBB" or "#RRGGBBAA" values.
	Palette map[string]string
}
