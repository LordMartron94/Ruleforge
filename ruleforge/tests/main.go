package main

import (
	"fmt"
	common_compiler "github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/lexing/shared"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/postprocessor"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/compilation"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/config"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/data_generation"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/data_generation/model"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/rules"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/rules/symbols"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/validation"
	"log"
	"os"
	"path/filepath"
	"strings"
)

func main() {
	if err := run(); err != nil {
		log.Println("If you struggle to understand the error, you can contact the developer on Discord (mr.hoornasp.learningexpert) or through e-mail: md.career@protonmail.com")
		log.Fatalf("fatal: %v", err)
	}
}

//goland:noinspection t
func run() error {
	configurationLoader := config.NewConfigurationLoader()
	configuration, err := configurationLoader.LoadConfiguration("config.toml")

	if err != nil {
		return fmt.Errorf("configurationLoader.LoadCache: %v", err)
	}

	err = configuration.Validate()
	if err != nil {
		return fmt.Errorf("configuration.Validate: %v", err)
	}

	styles, err := config.LoadStyleDefinitions(configuration.StylesDir)
	if err != nil {
		return fmt.Errorf("config.LoadStyleDefinitions: %v", err)
	}

	exporter, err := data_generation.NewPathOfBuildingExporter(
		filepath.Join(filepath.Dir(configuration.PathOfBuildingDataPath), "data-cache"),
		configuration.EconomyHistoryDBPath,
	)
	if err != nil {
		return fmt.Errorf("NewPathOfBuildingExporter: %w", err)
	}
	defer exporter.Close()

	itemBases, err := extractItemBases(configuration, exporter)
	if err != nil {
		return fmt.Errorf("extractItemBases: %v", err)
	}
	essences, err := extractEssenceBases(configuration, exporter)
	if err != nil {
		return fmt.Errorf("extractEssenceBases: %v", err)
	}
	gems, err := extractGemBases(configuration, exporter)
	if err != nil {
		return fmt.Errorf("extractGemBases: %v", err)
	}
	uniques, err := extractUniqueBases(configuration, exporter)
	if err != nil {
		return fmt.Errorf("extractUniqueBases: %v", err)
	}
	leagues := configuration.GetLeaguesToRetrieve()
	if err := exporter.RefreshEconomyData(leagues, configuration.GetEconomyHistoryRefreshInterval(), false); err != nil {
		return fmt.Errorf("RefreshEconomyData: %w", err)
	}
	economyData, err := exporter.EconomyHistory(leagues)
	if err != nil {
		return fmt.Errorf("EconomyHistory: %w", err)
	}

	log.Println("Number of itemBases:", len(itemBases))
	log.Println("Number of essences:", len(essences))
	log.Println("Number of gems:", len(gems))
	log.Println("Number of uniques:", len(uniques))

	err = exporter.SaveItemCache(itemBases, essences, gems, uniques)
	if err != nil {
		return fmt.Errorf("exporter.SaveItemCache: %v", err)
	}

	baseTypes := []string{"Gold"} // Manually include Gold because it's not really an item, but still a valid basetype.
	baseTypes = append(baseTypes, data_generation.GetBaseTypes(itemBases)...)
	baseTypes = append(baseTypes, data_generation.GetBaseTypes(essences)...)
	baseTypes = append(baseTypes, data_generation.GetBaseTypes(gems)...)
	baseTypes = append(baseTypes, divinationCardBaseTypes(economyData)...)

	log.Println("Number of BaseTypes: ", len(baseTypes))

	ruleforgeScripts, err := listFilesWithExtension(configuration.RuleforgeInputDir, ".rf")

	if err != nil {
		return fmt.Errorf("listFilesWithExtension: %v", err)
	}

	for _, ruleforgeScript := range ruleforgeScripts {
		for _, style := range styles {
			if !style.IsEnabled() {
				continue
			}
			err = processRuleforgeScript(ruleforgeScript, configuration, baseTypes, itemBases, economyData, style)
			if err != nil {
				return fmt.Errorf("processRuleforgeScript: %v", err)
			}
		}
	}

	return nil
}

func extractItemBases(configuration *config.ConfigurationModel, exporter *data_generation.PathOfBuildingExporter) ([]model.ItemBase, error) {
	luaFiles, err := listFilesWithExtension(filepath.Join(configuration.PathOfBuildingDataPath, "Bases"), ".lua")

	if err != nil {
		return nil, fmt.Errorf("listFilesWithExtension: %v", err)
	}
	bases, err := exporter.LoadItemBases(luaFiles)

	if err != nil {
		return nil, fmt.Errorf("loadItemBases: %v", err)
	}

	return bases, nil
}

func extractEssenceBases(configuration *config.ConfigurationModel, exporter *data_generation.PathOfBuildingExporter) ([]model.Essence, error) {
	file := filepath.Join(configuration.PathOfBuildingDataPath, "Essence.lua")

	essences, err := exporter.LoadEssences(file)

	if err != nil {
		return nil, fmt.Errorf("loadEssences: %v", err)
	}

	return essences, nil
}

func extractGemBases(configuration *config.ConfigurationModel, exporter *data_generation.PathOfBuildingExporter) ([]model.Gem, error) {
	file := filepath.Join(configuration.PathOfBuildingDataPath, "Gems.lua")

	gems, err := exporter.LoadGems(file)

	if err != nil {
		return nil, fmt.Errorf("loadEssences: %v", err)
	}

	return gems, nil
}

func extractUniqueBases(configuration *config.ConfigurationModel, exporter *data_generation.PathOfBuildingExporter) ([]model.Unique, error) {
	luaFiles, err := listFilesWithExtension(filepath.Join(configuration.PathOfBuildingDataPath, "Uniques"), ".lua")

	if err != nil {
		return nil, fmt.Errorf("listFilesWithExtension: %v", err)
	}

	bases, err := exporter.LoadUniques(luaFiles)
	if err != nil {
		return nil, fmt.Errorf("loadUniques: %v", err)
	}

	newUniqueFile := filepath.Join(configuration.PathOfBuildingDataPath, "Uniques", "Special", "New.Lua")
	newUniqueBases, err := exporter.LoadUpcomingUniqueItems(newUniqueFile)

	if err != nil {
		return nil, fmt.Errorf("loadUpcomingUniqueItems: %v", err)
	}

	bases = append(bases, newUniqueBases...)

	generatedUniqueFile := filepath.Join(configuration.PathOfBuildingDataPath, "Uniques", "Special", "Generated.Lua")
	generatedUniqueBases, err := exporter.LoadGeneratedUniques(generatedUniqueFile)

	if err != nil {
		return nil, fmt.Errorf("generatedUniqueBases: %v", err)
	}

	bases = append(bases, generatedUniqueBases...)

	return bases, nil
}

//goland:noinspection t
func processRuleforgeScript(
	ruleforgeScriptPath string,
	configuration *config.ConfigurationModel,
	validBases []string,
	itemBases []model.ItemBase,
	economyCache map[string][]data_generation.EconomySnapshot,
	style config.StyleDefinition) error {
	file, err := openFile(ruleforgeScriptPath)
	if err != nil {
		return err
	}
	defer closeFile(file)

	// 2) Build the compiler/file handler
	handler := newFileHandler(file)

	// 3) Lexing
	_, err = handler.Lex()
	if err != nil {
		return fmt.Errorf("lexing file: %w", err)
	}
	//printLexemes(lexemes)
	//fmt.Println("----------------")

	// 4) Parsing
	tree, err := handler.Parse()
	if err != nil {
		return fmt.Errorf("parsing file: %w", err)
	}
	fmt.Println("----------------")

	postProcessor := postprocessor.PostProcessor[symbols.LexingTokenType]{}
	tree = postProcessor.FilterOutSymbols([]string{
		symbols.ParseSymbolWhitespace.String(),
		symbols.ParseSymbolBlockOperator.String(),
	}, tree)
	tree = postProcessor.RemoveEmptyNodes(tree)

	tree.Print(2, []symbols.LexingTokenType{})

	// 5) Validation
	if err := validation.NewParseTreeValidator(tree).Validate(); err != nil {
		return fmt.Errorf("validating parse tree: %w", err)
	}

	baseTypeDataLoader := config.NewBaseTypeAutomationLoader(style.BaseTypes)
	baseTypeData, err := baseTypeDataLoader.Load()

	if err != nil {
		return fmt.Errorf("loading base type automation data: %w", err)
	}

	currency := compilation.CurrencySettings{CraftingWeight: configuration.CurrencyTiering.CraftingWeight, Source: configuration.CurrencyTiering.Profiles}
	if _, err := os.Stat(currency.Source); err == nil {
		if currency.Profiles, err = config.LoadCurrencyProfiles(currency.Source); err != nil {
			return err
		}
	}

	// 6) Compilation
	compiler, err := compilation.NewCompiler(tree, compilation.CompilerConfiguration{
		StylesheetPath: style.Stylesheet,
		Palette:        style.Palette,
	}, validBases, itemBases, economyCache, *configuration.EconomyWeights, configuration.GetLeagueWeights(), configuration.EconomyNormalizationStrategy, configuration.ChaseVSGeneralPotentialFactor, configuration.EconomyValueHalfLifeDays, *baseTypeData, configuration.CustomEquipmentPresets, currency)

	if err != nil {
		return fmt.Errorf("compilation.NewCompiler: %w", err)
	}

	outputStrings, err, outputName := compiler.CompileIntoFilter()

	if err != nil {
		return fmt.Errorf("compiler.CompileIntoFilter: %v", err)
	}

	// 7) Writing
	for _, outputDir := range configuration.FilterOutputDirs {
		outputFileName := filepath.Join(outputDir, style.FilterFileName(outputName))
		err = WriteLines(outputStrings, outputFileName)

		if err != nil {
			return fmt.Errorf("writing output file: %w", err)
		}
	}

	return nil
}

func openFile(path string) (*os.File, error) {
	f, err := os.Open(path)
	if err != nil {
		return nil, fmt.Errorf("opening %s: %w", path, err)
	}
	return f, nil
}

func closeFile(f *os.File) {
	if err := f.Close(); err != nil {
		log.Printf("warning: closing file: %v", err)
	}
}

func newFileHandler(f *os.File) common_compiler.FileHandler[symbols.LexingTokenType] {
	lexingRules := rules.GetLexingRules()
	parsingRules := rules.GetParsingRules()
	return *common_compiler.NewFileHandler(f, lexingRules, parsingRules, symbols.IgnoreToken)
}

func printLexemes(lexemes []*shared.Token[symbols.LexingTokenType]) {
	for i, lex := range lexemes {
		fmt.Printf("Lexeme (%d): %q\n", i, lex.String())
	}
}

// divinationCardBaseTypes collects every divination card name seen across all
// recorded economy history, deduplicated. Divination cards aren't tracked by
// Path of Building, so their names come from whatever poe.ninja has actually
// priced instead of a bundled list.
func divinationCardBaseTypes(economyData map[string][]data_generation.EconomySnapshot) []string {
	seen := make(map[string]bool)
	for _, snapshots := range economyData {
		for _, snapshot := range snapshots {
			for _, item := range snapshot.Items {
				if item.Class == "Divination Cards" {
					seen[item.BaseType] = true
				}
			}
		}
	}
	baseTypes := make([]string, 0, len(seen))
	for baseType := range seen {
		baseTypes = append(baseTypes, baseType)
	}
	return baseTypes
}

// listFilesWithExtension returns all files (not directories) in dir
// whose names end with the given extension ext. ext may be supplied
// with or without the leading dot (e.g. "txt" or ".txt").
func listFilesWithExtension(dir, ext string) ([]string, error) {
	// Normalize ext to start with a dot
	if !strings.HasPrefix(ext, ".") {
		ext = "." + ext
	}

	entries, err := os.ReadDir(dir)
	if err != nil {
		return nil, err
	}

	var matches []string
	for _, entry := range entries {
		if entry.IsDir() {
			continue
		}
		if filepath.Ext(entry.Name()) == ext {
			// build the full path
			matches = append(matches, filepath.Join(dir, entry.Name()))
		}
	}
	return matches, nil
}

// WriteLines writes the provided lines to the file at path.
// It creates or truncates the file, writing each string on its own line.
func WriteLines(lines []string, path string) error {
	content := strings.Join(lines, "\n") + "\n"
	return os.WriteFile(path, []byte(content), 0644)
}
