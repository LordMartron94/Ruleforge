package config

import (
	"fmt"
	"strings"
)

// ThemeNames splits a script's THEMES value into theme names. Names are
// separated by spaces, and matching ignores case.
func ThemeNames(value string) []string {
	return strings.Fields(strings.ToLower(value))
}

// themeMatches reports whether a THEMES name picks the style: the name is the
// style's ID, or the ID starts with the name and a hyphen, so "aryzel" picks
// every "aryzel-*" style.
func themeMatches(name string, style StyleDefinition) bool {
	id := strings.ToLower(style.ID)
	return id == name || strings.HasPrefix(id, name+"-")
}

// SelectStyles returns the styles a script asks for with THEMES, in the order
// of styles. No names means every style. A name that matches none of the
// styles is an error when allIDs (every theme ID, enabled or not) has no match
// either, so a typo is caught while a disabled theme is just skipped.
func SelectStyles(styles []StyleDefinition, names []string, allIDs []string) ([]StyleDefinition, error) {
	if len(names) == 0 {
		return styles, nil
	}
	used := make([]bool, len(names))
	var selected []StyleDefinition
	for _, style := range styles {
		picked := false
		for i, name := range names {
			if themeMatches(name, style) {
				used[i], picked = true, true
			}
		}
		if picked {
			selected = append(selected, style)
		}
	}
	for i, name := range names {
		if used[i] {
			continue
		}
		known := false
		for _, id := range allIDs {
			if themeMatches(name, StyleDefinition{ID: id}) {
				known = true
				break
			}
		}
		if !known {
			return nil, fmt.Errorf("THEMES names %q, but no theme has that name (themes: %s)", name, strings.Join(allIDs, ", "))
		}
	}
	return selected, nil
}
