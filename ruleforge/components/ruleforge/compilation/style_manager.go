package compilation

import (
	"fmt"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/config"
	"strings"
)

type StyleManager struct {
	styles map[string]*config.Style
}

func NewStyleManager(path string) (*StyleManager, error) {
	styles, err := config.LoadStyles(path)
	if err != nil {
		return nil, fmt.Errorf("failed to load styles: %w", err)
	}
	return &StyleManager{styles: styles}, nil
}

// GetStyle resolves a style value, which could be a direct key or a variable.
func (sm *StyleManager) GetStyle(styleValue string, variables map[string][]string) (*config.Style, error) {
	if !isVariableRef(styleValue) {
		return sm.lookupStyle(styleValue)
	}
	return sm.resolveVariableStyle(styleValue, variables)
}

func (sm *StyleManager) lookupStyle(key string) (*config.Style, error) {
	style, ok := sm.styles[key]
	if !ok {
		return nil, fmt.Errorf("style %q not found", key)
	}

	return style.Clone(), nil
}

func (sm *StyleManager) resolveVariableStyle(styleValue string, variables map[string][]string) (*config.Style, error) {
	varName := stripVarPrefix(styleValue)
	refs, ok := variables[varName]
	if !ok || len(refs) == 0 {
		return nil, fmt.Errorf("no variable style reference found for %q", varName)
	}

	var merged *config.Style
	for i, ref := range refs {
		// Recursively call GetStyle to handle nested variables
		toMerge, err := sm.GetStyle(ref, variables)
		if err != nil {
			return nil, err
		}

		if i == 0 {
			merged = toMerge
		} else {
			//goland:noinspection GoDfaNilDereference
			merged, err = merged.MergeStyles(toMerge)
			if err != nil {
				return nil, fmt.Errorf("error merging style %q: %w", ref, err)
			}
		}
	}
	return merged, nil
}

func isVariableRef(value string) bool {
	return strings.HasPrefix(value, "$")
}

func stripVarPrefix(value string) string {
	return strings.TrimPrefix(value, "$")
}
