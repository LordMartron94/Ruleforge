package compilation

import (
	"fmt"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/parsing/shared"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/transforming"
	shared2 "github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/transforming/shared"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/rules/symbols"
)

type Compiler struct {
	parseTree             *shared.ParseTree[symbols.LexingTokenType]
	compilerConfiguration CompilerConfiguration
}

func NewCompiler(parseTree *shared.ParseTree[symbols.LexingTokenType], configuration CompilerConfiguration) *Compiler {
	return &Compiler{
		parseTree:             parseTree,
		compilerConfiguration: configuration,
	}
}

func (c *Compiler) CompileIntoFilter() ([]string, error, string) {
	output := make([]string, 0)

	filterName := "UNKNOWN_THIS_SHOULD_NOT_HAPPEN"

	callbackFinder := func(node *shared.ParseTree[symbols.LexingTokenType]) (shared2.TransformCallback[symbols.LexingTokenType], int) {
		switch node.Symbol {
		case symbols.ParseSymbolRootMetadata.String():
			return c.extractMetadataText(&output, &filterName), 0
		}
		return nil, 0
	}

	transformer := transforming.NewTransformer(callbackFinder)
	transformer.Transform(c.parseTree)

	return output, nil, filterName
}

func (c *Compiler) extractMetadataText(metadataText *[]string, filterName *string) shared2.TransformCallback[symbols.LexingTokenType] {
	return func(node *shared.ParseTree[symbols.LexingTokenType]) {
		assignments := node.FindAllSymbolNodes(symbols.ParseSymbolAssignment.String())

		filterVersion := "<unknown>"
		filterStrictness := "<unknown>"

		for _, assignment := range assignments {
			key := assignment.Children[0].Token.ValueToString()
			value := assignment.Children[2].Token.ValueToString()

			switch key {
			case "NAME":
				*filterName = value
				break
			case "VERSION":
				filterVersion = value
				break
			case "STRICTNESS":
				filterStrictness = value
				break
			}
		}

		lines := []string{
			"This filter is automatically generated through the Ruleforge program.",
			"Ruleforge metadata (from the user's script): ",
			fmt.Sprintf("Ruleforge \"%s\" @ %s -> strictness: %s", *filterName, filterVersion, filterStrictness),
			"",
			"For questions reach out to Mr. Hoorn (Ruleforge author):",
			"Discord: \"mr.hoornasp.learningexpert\" (without quotations)",
			"Email: md.career@protonmail.com",
			"-----------------------------------------------------------------------",
		}

		for _, line := range lines {
			commented := c.constructComment(line)
			*metadataText = append(*metadataText, commented)
		}

	}
}

func (c *Compiler) constructComment(content string) string {
	return fmt.Sprintf("# %s", content)
}
