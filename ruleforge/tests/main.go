package main

import (
	"fmt"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/postprocessor"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/compilation"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/config"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/data_generation"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/rules/symbols"
	"log"
	"os"
	"path/filepath"
	"strings"

	common_compiler "github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/lexing/shared"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/rules"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/validation"
)

func main() {
	if err := run(); err != nil {
		log.Println("If you struggle to understand the error, you can contact the developer on Discord (mr.hoornasp.learningexpert) or through e-mail: md.career@protonmail.com")
		log.Fatalf("fatal: %v", err)
	}
}

func run() error {
	configurationLoader := config.NewConfigurationLoader()
	configuration, err := configurationLoader.LoadConfiguration("config.json")

	if err != nil {
		return fmt.Errorf("configurationLoader.LoadConfiguration: %v", err)
	}

	exporter := data_generation.NewPathOfBuildingExporter()
	bases, err := extractItemBases(configuration, exporter)
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

	log.Println("Number of bases:", len(bases))
	log.Println("Number of essences:", len(essences))
	log.Println("Number of gems:", len(gems))

	baseTypes := []string{"Gold"} // Manually include Gold because it's not really an item, but still a valid basetype.
	baseTypes = append(baseTypes, data_generation.GetBaseTypes(bases)...)
	baseTypes = append(baseTypes, data_generation.GetBaseTypes(essences)...)
	baseTypes = append(baseTypes, data_generation.GetBaseTypes(gems)...)

	log.Println("Number of BaseTypes: ", len(baseTypes))

	ruleforgeScripts, err := listFilesWithExtension(configuration.RuleforgeInputDir, ".rf")

	if err != nil {
		return fmt.Errorf("listFilesWithExtension: %v", err)
	}

	for _, ruleforgeScript := range ruleforgeScripts {
		err = processRuleforgeScript(ruleforgeScript, configuration, baseTypes)
		if err != nil {
			return fmt.Errorf("processRuleforgeScript: %v", err)
		}
	}

	return nil
}

func extractItemBases(configuration *config.ConfigurationModel, exporter *data_generation.PathOfBuildingExporter) ([]data_generation.ItemBase, error) {
	luaFiles, err := listFilesWithExtension(filepath.Join(configuration.PathOfBuildingDataPath, "Bases"), ".lua")

	if err != nil {
		return nil, fmt.Errorf("listFilesWithExtension: %v", err)
	}

	bases := make([]data_generation.ItemBase, 0)

	for _, luaFile := range luaFiles {
		fileBases, err := exporter.LoadItemBases(luaFile)
		if err != nil {
			log.Fatalf("Error processing file: %v", err)
		}

		bases = append(bases, fileBases...)
	}

	return bases, nil
}

func extractEssenceBases(configuration *config.ConfigurationModel, exporter *data_generation.PathOfBuildingExporter) ([]data_generation.Essence, error) {
	file := filepath.Join(configuration.PathOfBuildingDataPath, "Essence.lua")

	essences, err := exporter.LoadEssences(file)

	if err != nil {
		return nil, fmt.Errorf("loadEssences: %v", err)
	}

	return essences, nil
}

func extractGemBases(configuration *config.ConfigurationModel, exporter *data_generation.PathOfBuildingExporter) ([]data_generation.Gem, error) {
	file := filepath.Join(configuration.PathOfBuildingDataPath, "Gems.lua")

	gems, err := exporter.LoadGems(file)

	if err != nil {
		return nil, fmt.Errorf("loadEssences: %v", err)
	}

	return gems, nil
}

func processRuleforgeScript(ruleforgeScriptPath string, configuration *config.ConfigurationModel, validBases []string) error {
	file, err := openFile(ruleforgeScriptPath)
	if err != nil {
		return err
	}
	defer closeFile(file)

	// 2) Build the compiler/file handler
	handler := newFileHandler(file)

	// 3) Lexing
	lexemes, err := handler.Lex()
	if err != nil {
		return fmt.Errorf("lexing file: %w", err)
	}
	printLexemes(lexemes)
	fmt.Println("----------------")

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

	// 6) Compilation
	compiler := compilation.NewCompiler(tree, compilation.CompilerConfiguration{
		StyleJsonPath: configuration.StyleJSONFile,
	}, validBases)
	outputStrings, err, outputName := compiler.CompileIntoFilter()

	if err != nil {
		return fmt.Errorf("compiler.CompileIntoFilter: %v", err)
	}

	// 7) Writing
	for _, outputDir := range configuration.FilterOutputDirs {
		outputFileName := filepath.Join(outputDir, outputName+".filter")
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
