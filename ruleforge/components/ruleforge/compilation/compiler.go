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

// Compiler is now a lean orchestrator without prewired build.
type Compiler struct {
	treeWalker   *TreeWalker
	styleManager *StyleManager
	ruleFactory  *RuleFactory

	validBaseTypes        []string
	armorBases            []model.ItemBase
	weaponBases           []model.ItemBase
	flaskBases            []model.ItemBase
	economyCache          map[string][]data_generation.EconomyCacheItem
	economyWeights        config.EconomyWeights
	leagueWeights         []config.LeagueWeights
	normalizationStrategy string
	chasePotentialWeight  float64
	baseTypeData          []config.BaseTypeAutomationEntry
	cssVariables          map[string]string
	customPresets         map[string]config.EquipmentPreset
}

// NewCompiler constructs a Compiler and captures all dependencies except the Build.
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
	customPresets map[string]config.EquipmentPreset,
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

		validBaseTypes:        validBaseTypes,
		armorBases:            armorBases,
		weaponBases:           weaponBases,
		flaskBases:            flaskBases,
		economyCache:          economyCache,
		economyWeights:        economyWeights,
		leagueWeights:         leagueWeights,
		normalizationStrategy: normalizationStrategy,
		chasePotentialWeight:  chasePotentialWeight,
		baseTypeData:          baseTypeData,
		cssVariables:          cssVariables,
		customPresets:         customPresets,
	}, nil
}

// CompileIntoFilter orchestrates compilation, wiring the correct Build based on metadata.
//
//goland:noinspection t
func (c *Compiler) CompileIntoFilter() ([]string, error, string) {
	var header, body, toc []string

	// 1. Extract raw data
	metadata := c.treeWalker.ExtractMetadata()
	variables := c.treeWalker.ExtractVariables()
	sections := c.treeWalker.ExtractSections()

	// 1b. Determine Build: default first, then custom preset
	buildName := metadata.Build
	var buildInstance *Build
	if b, err := GetDefaultBuild(buildName); err == nil {
		buildInstance = b
	} else if cp, ok := c.customPresets[buildName]; ok {
		ep, err := NewEquipmentPresetFromConfig(cp)
		if err != nil {
			return nil, err, metadata.Name
		}
		buildInstance = &Build{Name: buildName, Preset: ep}
	} else {
		return nil, fmt.Errorf("build preset %q not found", buildName), metadata.Name
	}

	// 2. Construct the header
	header = c.constructHeader(metadata)

	// 3. Pre-calculate sizes to determine the starting line number
	tocSize := 1 + len(sections) + 1
	dividerSize := len(c.constructDivider())

	// The first section heading appears after header, ToC, divider
	lineCounter := len(header) + tocSize + dividerSize + 1
	sectionLineNumbers := make(map[string]int)

	// 4. Instantiate a RuleGenerator with the resolved build
	ruleGenerator := NewRuleGenerator(
		c.ruleFactory,
		c.styleManager,
		c.validBaseTypes,
		c.armorBases,
		c.weaponBases,
		c.flaskBases,
		c.economyCache,
		c.economyWeights,
		c.leagueWeights,
		c.normalizationStrategy,
		c.chasePotentialWeight,
		c.baseTypeData,
		buildInstance,
	)

	// 5. Generate rules for each section and track final line numbers
	for _, section := range sections {
		sectionLineNumbers[section.Name] = lineCounter

		body = append(body, c.constructSectionHeading(section.Name, section.Description))
		lineCounter++

		compiledRules, err := ruleGenerator.GenerateRulesForSection(section, variables)
		if err != nil {
			return nil, err, metadata.Name
		}

		for _, rule := range compiledRules {
			body = append(body, rule...)
			lineCounter += len(rule)
		}

		body = body[:len(body)-1]
		lineCounter--

		divider := c.constructDivider()
		body = append(body, divider...)
		lineCounter += len(divider)
	}

	// 6. Fallback section
	fallbackLineNumber := lineCounter
	fallbackName := "Fallback"
	fallbackDesc := "Shows anything that wasn't caught by upstream rules."

	// 7. Build Table of Contents
	toc = append(toc, c.constructComment("TABLE OF CONTENTS: "))
	for _, section := range sections {
		line := sectionLineNumbers[section.Name]
		toc = append(toc, c.constructComment(fmt.Sprintf("\tLine %d: %s (%s)", line, section.Name, section.Description)))
	}
	toc = append(toc, c.constructComment(fmt.Sprintf("\tLine %d: %s (%s)", fallbackLineNumber, fallbackName, fallbackDesc)))

	// 8. Add fallback rules
	fallbackStyle, _ := c.styleManager.GetStyle("Fallback")
	fallbackRule := c.ruleFactory.ConstructRule(model2.ShowRule, *fallbackStyle, []string{})
	fallbackHeading := c.constructSectionHeading(fallbackName, fallbackDesc)

	body = append(body, fallbackHeading, "")
	body = append(body, fallbackRule...)

	// 9. Assemble and return
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
		output = append(output, c.constructComment(line))
	}
	output = append(output, c.constructDivider()...)
	return output
}

func (c *Compiler) constructSectionHeading(name, desc string) string {
	return c.constructComment(fmt.Sprintf(">>>>>>>>>>>>>>>> SECTION: %s (%s)", name, desc))
}

func (c *Compiler) constructComment(content string) string {
	return fmt.Sprintf("# %s", content)
}

func (c *Compiler) constructDivider() []string {
	return []string{"", c.constructComment("============================================================================"), ""}
}

// prepareItemData filters and categorizes a raw list of item bases.
func prepareItemData(itemBases []model.ItemBase, validBaseTypes []string) ([]model.ItemBase, []model.ItemBase, []model.ItemBase) {
	var armorBases, weaponBases, flaskBases []model.ItemBase
	utils := NewPobUtils()

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
