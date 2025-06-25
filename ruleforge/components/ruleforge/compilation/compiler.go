package compilation

import (
	"fmt"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/parsing/shared"
	model2 "github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/compilation/model"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/config"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/data_generation"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/data_generation/model"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/rules/symbols"
	"slices"
)

// Compiler is now a lean orchestrator.
type Compiler struct {
	treeWalker    *TreeWalker
	ruleGenerator *RuleGenerator
	styleManager  *StyleManager
	ruleFactory   *RuleFactory
}

func NewCompiler(
	parseTree *shared.ParseTree[symbols.LexingTokenType],
	configuration CompilerConfiguration,
	validBaseTypes []string,
	itemBases []model.ItemBase,
	economyCache map[string][]data_generation.EconomyCacheItem,
	economyWeights config.EconomyWeights,
	leagueWeights []config.LeagueWeights,
	normalizationStrategy string,
	chasePotentialWeight float64,
	baseTypeData []config.BaseTypeAutomationEntry,
	cssVariables map[string]string,
) (*Compiler, error) {

	styleMgr, err := NewStyleManager(configuration.StyleJsonPath, parseTree, cssVariables)
	if err != nil {
		return nil, err
	}

	armorBases, weaponBases, flaskBases := prepareItemData(itemBases, validBaseTypes)

	return &Compiler{
		treeWalker:   NewTreeWalker(parseTree),
		styleManager: styleMgr,
		ruleFactory:  &RuleFactory{},
		ruleGenerator: NewRuleGenerator(
			&RuleFactory{},
			styleMgr,
			validBaseTypes,
			armorBases,
			weaponBases,
			flaskBases,
			economyCache,
			economyWeights,
			leagueWeights,
			normalizationStrategy,
			chasePotentialWeight,
			baseTypeData,
		),
	}, nil
}

// CompileIntoFilter is now clean and readable, delegating all work.
func (c *Compiler) CompileIntoFilter() ([]string, error, string) {
	var header, body, toc []string

	// 1. Extract raw data
	metadata := c.treeWalker.ExtractMetadata()
	variables := c.treeWalker.ExtractVariables()
	sections := c.treeWalker.ExtractSections()

	// 2. Construct the header
	header = c.constructHeader(metadata)
	buildType := GetBuildType(metadata.Build)

	// 3. Pre-calculate sizes to determine the starting line number
	tocSize := 1 + len(sections) + 1
	dividerSize := len(c.constructDivider())

	// The first section heading will appear after the header, the ToC, and the divider.
	lineCounter := len(header) + tocSize + dividerSize + 1

	sectionLineNumbers := make(map[string]int)

	// 4. Generate rules for each section and track final line numbers
	for _, section := range sections {
		// Store the definitive line number for this section
		sectionLineNumbers[section.Name] = lineCounter

		sectionHeading := c.constructSectionHeading(section.Name, section.Description)
		body = append(body, sectionHeading)
		lineCounter++ // Account for the section heading line

		compiledRules, err := c.ruleGenerator.GenerateRulesForSection(section, variables, buildType)
		if err != nil {
			return nil, err, metadata.Name
		}

		// Add each generated rule group and update the line counter
		for _, rule := range compiledRules {
			body = append(body, rule...)
			lineCounter += len(rule)
		}

		body = body[:len(body)-1] // Account for last empty line from the rule generator
		lineCounter--

		divider := c.constructDivider()
		body = append(body, divider...)
		lineCounter += len(divider)
	}

	// 5. The current value of lineCounter is now the exact line for the Fallback heading
	fallbackLineNumber := lineCounter
	fallbackSectionName := "Fallback"
	fallbackSectionDesc := "Shows anything that wasn't caught by upstream rules."

	// 6. Generate the complete Table of Contents with final line numbers
	toc = append(toc, c.constructComment("TABLE OF CONTENTS: "))
	for _, section := range sections {
		lineNumber := sectionLineNumbers[section.Name]
		tocEntry := fmt.Sprintf("\tLine %d: %s (%s)", lineNumber, section.Name, section.Description)
		toc = append(toc, c.constructComment(tocEntry))
	}
	toc = append(toc, c.constructComment(fmt.Sprintf("\tLine %d: %s (%s)", fallbackLineNumber, fallbackSectionName, fallbackSectionDesc)))

	// 7. Add the fallback rule content to the body
	fallbackStyle, _ := c.styleManager.GetStyle("Fallback")
	fallbackRule := c.ruleFactory.ConstructRule(model2.ShowRule, *fallbackStyle, []string{})
	fallbackHeading := c.constructSectionHeading(fallbackSectionName, fallbackSectionDesc)

	body = append(body, fallbackHeading, "")
	body = append(body, fallbackRule...)

	// 8. Assemble the final output
	var finalOutput []string
	finalOutput = append(finalOutput, header...)
	finalOutput = append(finalOutput, toc...)
	finalOutput = append(finalOutput, c.constructDivider()...)
	finalOutput = append(finalOutput, body...)

	return finalOutput, nil, metadata.Name
}

func (c *Compiler) constructHeader(metadata ExtractedMetadata) []string {
	output := make([]string, 0)
	lines := []string{
		"This filter is automatically generated through the Ruleforge program.",
		"Ruleforge metadata (from the user's script): ",
		fmt.Sprintf("Ruleforge \"%s\" @ %s (meant for: %s) -> strictness: %s", metadata.Name, metadata.Version, metadata.Build, metadata.Strictness),
		"",
		"For questions reach out to Mr. Hoorn (Ruleforge author):",
		"Discord: \"mr.hoornasp.learningexpert\" (without quotations)",
		"Email: md.career@protonmail.com",
	}

	for _, line := range lines {
		commented := c.constructComment(line)
		output = append(output, commented)
	}

	output = append(output, c.constructDivider()...)

	return output
}

func (c *Compiler) constructSectionHeading(sectionName, sectionDescription string) string {
	return c.constructComment(fmt.Sprintf(">>>>>>>>>>>>>>>> SECTION: %s (%s)", sectionName, sectionDescription))
}

func (c *Compiler) constructComment(content string) string {
	return fmt.Sprintf("# %s", content)
}

func (c *Compiler) constructDivider() []string {
	return []string{
		"",
		c.constructComment("============================================================================"),
		"",
	}
}

// ----------- HELPERS -----------
// prepareItemData filters and categorizes a raw list of item bases.
func prepareItemData(itemBases []model.ItemBase, validBaseTypes []string) ([]model.ItemBase, []model.ItemBase, []model.ItemBase) {
	var armorBases []model.ItemBase
	var weaponBases []model.ItemBase
	var flaskBases []model.ItemBase

	utils := NewPobUtils() // Assuming NewPobUtils() is available in this package

	for _, item := range itemBases {
		if !slices.Contains(validBaseTypes, item.GetBaseType()) {
			continue
		}

		if utils.IsArmor(item) {
			armorBases = append(armorBases, item)
		} else if utils.IsWeapon(item) {
			weaponBases = append(weaponBases, item)
		} else if utils.IsFlask(item) {
			flaskBases = append(flaskBases, item)
		}
	}

	return armorBases, weaponBases, flaskBases
}
