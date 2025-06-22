package compilation

import (
	"fmt"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/parsing/shared"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/compilation/model"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/config"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/rules/symbols"
	"strings"
)

type StyleManager struct {
	styles       map[string]config.Style
	rootNode     *shared.ParseTree[symbols.LexingTokenType]
	varNodeCache map[string]*shared.ParseTree[symbols.LexingTokenType]
}

func NewStyleManager(path string, rootNode *shared.ParseTree[symbols.LexingTokenType]) (*StyleManager, error) {
	styles, err := config.LoadStyles(path)
	if err != nil {
		return nil, fmt.Errorf("failed to load styles: %w", err)
	}
	return &StyleManager{
		styles:       styles,
		rootNode:     rootNode,
		varNodeCache: make(map[string]*shared.ParseTree[symbols.LexingTokenType]),
	}, nil
}

// GetStyle resolves a style value, which could be a direct key or a variable.
func (sm *StyleManager) GetStyle(styleValue string) (*config.Style, error) {
	if !isVariableRef(styleValue) {
		return sm.lookupStyle(styleValue)
	}
	return sm.resolveVariableStyle(styleValue)
}

// resolveVariableStyle is the core of the style resolution logic. It finds a
// variable's definition in the parse tree and recursively merges its base styles,
// using any defined overrides to resolve merge conflicts.
func (sm *StyleManager) resolveVariableStyle(styleValue string) (*config.Style, error) {
	varName := stripVarPrefix(styleValue)

	// 1. Find the ParseTree node for this variable's definition.
	defNode, err := sm.findVariableDeclarationNode(varName)
	if err != nil {
		return nil, err
	}

	// 2. Extract base style values AND override instructions from the node.
	baseStyleRefs := extractBaseStyles(defNode)
	if len(baseStyleRefs) == 0 {
		return nil, fmt.Errorf("variable %q has no base styles to resolve", varName)
	}

	overrides := extractStyleOverrides(defNode)
	overrideMap := make(config.OverrideMap)
	for _, o := range overrides {
		winnerName := o.SourceStylePath
		if isVariableRef(winnerName) {
			winnerName = stripVarPrefix(winnerName)
		}
		overrideMap[o.PropertyName] = winnerName
	}

	// 3. Recursively resolve the first base style. This is the start of our merge chain.
	mergedStyle, err := sm.GetStyle(baseStyleRefs[0])
	if err != nil {
		return nil, fmt.Errorf("failed to resolve base style %q for variable %q: %w", baseStyleRefs[0], varName, err)
	}
	if mergedStyle == nil {
		return nil, fmt.Errorf("internal error: resolved base style %q for variable %q is nil but no error was returned", baseStyleRefs[0], varName)
	}

	// 4. Iteratively merge later styles using the override map.
	for i := 1; i < len(baseStyleRefs); i++ {
		nextStyle, err := sm.GetStyle(baseStyleRefs[i])
		if err != nil {
			return nil, fmt.Errorf("failed to resolve style component %q for variable %q: %w", baseStyleRefs[i], varName, err)
		}
		if nextStyle == nil {
			return nil, fmt.Errorf("internal error: resolved style component %q for variable %q is nil but no error was returned", baseStyleRefs[i], varName)
		}

		mergedStyle, err = mergedStyle.MergeStyles(nextStyle, overrideMap)
		if err != nil {
			return nil, fmt.Errorf("failed to merge styles for variable %q: %w", varName, err)
		}
	}

	return mergedStyle, nil
}

// findVariableDeclarationNode finds a variable's definition node in the parse tree, with caching.
func (sm *StyleManager) findVariableDeclarationNode(varName string) (*shared.ParseTree[symbols.LexingTokenType], error) {
	if node, found := sm.varNodeCache[varName]; found {
		return node, nil
	}

	allVarDecls := sm.rootNode.FindAllSymbolNodes(symbols.ParseSymbolVariable.String())
	for _, declNode := range allVarDecls {
		assignments := declNode.FindAllSymbolNodes(symbols.ParseSymbolAssignment.String())
		for _, assignmentNode := range assignments {
			idNode := assignmentNode.FindSymbolNode(symbols.ParseSymbolIdentifier.String())
			if idNode != nil && idNode.Token.ValueToString() == varName {
				sm.varNodeCache[varName] = assignmentNode
				return assignmentNode, nil
			}
		}
	}

	return nil, fmt.Errorf("variable declaration for %q not found in script", varName)
}

// lookupStyle retrieves a style directly from the loaded JSON and sets its name.
func (sm *StyleManager) lookupStyle(key string) (*config.Style, error) {
	style, ok := sm.styles[key]
	if !ok {
		return nil, fmt.Errorf("style %q not found", key)
	}

	cloned := style.Clone()
	cloned.Name = key
	return cloned, nil
}

// extractBaseStyles safely extracts the base style references from a variable's assignment node.
// It specifically targets the FullValueExpression to avoid including override values.
func extractBaseStyles(assignmentNode *shared.ParseTree[symbols.LexingTokenType]) []string {
	baseStyles := make([]string, 0)
	valueExpression := assignmentNode.FindSymbolNode(symbols.ParseSymbolFullValueExpression.String())
	if valueExpression != nil {
		values := valueExpression.FindAllSymbolNodes(symbols.ParseSymbolValue.String())
		for _, value := range values {
			baseStyles = append(baseStyles, value.Token.ValueToString())
		}
	}
	return baseStyles
}

// extractStyleOverrides extracts the style overrides from the given node if any.
func extractStyleOverrides(assignmentNode *shared.ParseTree[symbols.LexingTokenType]) []model.StyleOverride {
	overrides := make([]model.StyleOverride, 0)
	for _, styleOverride := range assignmentNode.FindAllSymbolNodes(symbols.ParseSymbolOverrideTarget.String()) {
		if len(styleOverride.Children) >= 3 {
			sourcePathNode := styleOverride.Children[0]
			propNameNode := styleOverride.Children[2]

			overrides = append(overrides, model.StyleOverride{
				SourceStylePath: sourcePathNode.Token.ValueToString(),
				PropertyName:    propNameNode.Token.ValueToString(),
			})
		}
	}
	return overrides
}

func isVariableRef(value string) bool {
	return strings.HasPrefix(value, "$")
}

func stripVarPrefix(value string) string {
	return strings.TrimPrefix(value, "$")
}
