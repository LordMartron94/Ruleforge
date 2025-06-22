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
) (*Compiler, error) {

	styleMgr, err := NewStyleManager(configuration.StyleJsonPath, parseTree)
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
	var output []string

	// Extract raw data using the TreeWalker
	metadata := c.treeWalker.ExtractMetadata()
	variables := c.treeWalker.ExtractVariables()
	sections := c.treeWalker.ExtractSections()

	// Construct header
	output = append(output, c.constructHeader(metadata)...)

	buildType := GetBuildType(metadata.Build)

	output = append(output, c.constructComment("TABLE OF CONTENTS: "))

	// Generate Table of Contents
	for _, section := range sections {
		tocEntry := fmt.Sprintf("\t%s (%s)", section.Name, section.Description)
		output = append(output, c.constructComment(tocEntry))
	}

	output = append(output, c.constructDivider()...)

	// Generate rules for each section using the RuleGenerator
	for _, section := range sections {
		output = append(output, c.constructSectionHeading(section.Name, section.Description))

		compiledRules, err := c.ruleGenerator.GenerateRulesForSection(section, variables, buildType)
		if err != nil {
			return nil, err, metadata.Name
		}
		for _, rule := range compiledRules {
			output = append(output, rule...)
		}

		output = output[:len(output)-1] // Account for last empty line
		output = append(output, c.constructDivider()...)
	}

	// Add fallback rule
	fallbackStyle, _ := c.styleManager.GetStyle("Fallback")
	fallbackRule := c.ruleFactory.ConstructRule(model2.ShowRule, *fallbackStyle, []string{})
	output = append(output, c.constructSectionHeading("Fallback", "Shows anything that wasn't caught by upstream rules."), "")
	output = append(output, fallbackRule...)

	return output, nil, metadata.Name
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
