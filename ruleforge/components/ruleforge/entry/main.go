package main

import (
	"flag"
	"fmt"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/parsing/shared"
	"log"
	"os"
	"path"
	"path/filepath"
	"strings"

	common_compiler "github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/postprocessor"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/compilation"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/config"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/data_generation"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/data_generation/model"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/rules"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/rules/symbols"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/validation"
)

// App holds the application's state, configuration, and dependencies.
type App struct {
	// CLI Flags
	configPath      string
	verbose         bool
	updateCacheOnly bool

	// Core Components
	log      *log.Logger
	config   *config.ConfigurationModel
	exporter *data_generation.PathOfBuildingExporter

	// Loaded Data
	baseTypes   []string
	itemBases   []model.ItemBase
	essences    []model.Essence
	gems        []model.Gem
	uniques     []model.Unique
	economyData map[string][]data_generation.EconomyCacheItem
}

func main() {
	app := &App{
		log: log.New(os.Stdout, "", log.LstdFlags),
	}

	// Define command-line flags. The exporter handles caching automatically.
	// To force a refresh, the user can delete the './cache' directory.
	flag.StringVar(&app.configPath, "config", "config.json", "Path to the configuration file.")
	flag.BoolVar(&app.verbose, "verbose", false, "Enable verbose output for debugging.")
	flag.BoolVar(&app.updateCacheOnly, "update-cache-only", false, "Fetch/update data and save to cache without running compilation.")
	flag.Parse()

	if err := app.Run(); err != nil {
		log.Println("If you struggle to understand the error, you can contact the developer on Discord (mr.hoornasp.learningexpert) or through e-mail: md.career@protonmail.com")
		log.Fatalf("fatal: %v", err)
	}
}

// Run orchestrates the main application flow.
func (a *App) Run() error {
	// Load and validate configuration.
	if err := a.loadConfig(); err != nil {
		return err
	}

	cssParser, err := config.NewCSSParserFromFile(a.config.StyleColorCSSFile)

	if err != nil {
		return fmt.Errorf("NewCSSParserFromFile: %v", err)
	}

	props, err := cssParser.Parse()

	if err != nil {
		return fmt.Errorf("parse: %v", err)
	}

	// Initialize the exporter, which will attempt to load from cache.
	a.log.Println("Initializing data exporter (will use cache if available and valid)...")
	a.exporter = data_generation.NewPathOfBuildingExporter()

	// Fetch data. The exporter will return cached data or re-parse files as needed.
	if err := a.fetchData(); err != nil {
		return fmt.Errorf("could not fetch data: %w", err)
	}

	// Save data back to cache. The exporter will skip if the cache is still valid.
	if err := a.saveCaches(); err != nil {
		return fmt.Errorf("could not save data to cache: %w", err)
	}

	// If the user only wants to update the cache, we're done.
	if a.updateCacheOnly {
		a.log.Println("Cache update process complete. Exiting.")
		return nil
	}

	// Prepare shared data for compilation.
	a.prepareBaseTypes()

	// Compile Ruleforge scripts.
	if err := a.compileRules(props); err != nil {
		return fmt.Errorf("error during rule compilation: %w", err)
	}

	a.log.Println("Compilation finished successfully.")
	return nil
}

// loadConfig loads and validates the JSON configuration file.
func (a *App) loadConfig() error {
	loader := config.NewConfigurationLoader()
	configuration, err := loader.LoadConfiguration(a.configPath)
	if err != nil {
		return fmt.Errorf("could not load configuration from %s: %w", a.configPath, err)
	}

	if err := configuration.Validate(); err != nil {
		return fmt.Errorf("configuration validation failed: %w", err)
	}
	a.config = configuration
	a.log.Println("Configuration loaded and validated successfully.")
	return nil
}

// fetchData uses the exporter to load all required data.
// The exporter's methods will internally decide whether to use cached data or re-fetch from source.
func (a *App) fetchData() error {
	var err error
	pobDataPath := a.config.PathOfBuildingDataPath

	// The exporter's Load* methods will return cached data if available.
	a.itemBases, err = a.extractItemBases(pobDataPath)
	if err != nil {
		return fmt.Errorf("extractItemBases: %w", err)
	}
	a.essences, err = a.extractEssenceBases(pobDataPath)
	if err != nil {
		return fmt.Errorf("extractEssenceBases: %w", err)
	}
	a.gems, err = a.extractGemBases(pobDataPath)
	if err != nil {
		return fmt.Errorf("extractGemBases: %w", err)
	}
	a.uniques, err = a.extractUniqueBases(pobDataPath)
	if err != nil {
		return fmt.Errorf("extractUniqueBases: %w", err)
	}
	a.economyData, err = a.exporter.GetEconomyData(a.config.GetLeaguesToRetrieve())
	if err != nil {
		return fmt.Errorf("GetEconomyData: %w", err)
	}

	a.log.Printf("Data loaded: %d item bases, %d essences, %d gems, %d uniques.", len(a.itemBases), len(a.essences), len(a.gems), len(a.uniques))
	return nil
}

// saveCaches persists the fetched data. The exporter's Save* methods
// will internally decide whether to overwrite the cache based on expiry dates.
func (a *App) saveCaches() error {
	if err := a.exporter.SaveItemCache(a.itemBases, a.essences, a.gems, a.uniques); err != nil {
		return fmt.Errorf("exporter.SaveItemCache: %w", err)
	}
	if err := a.exporter.SaveEconomyCache(a.economyData); err != nil {
		return fmt.Errorf("exporter.SaveEconomyCache: %w", err)
	}
	return nil
}

// prepareBaseTypes aggregates all base types from the loaded data.
func (a *App) prepareBaseTypes() {
	baseTypes := []string{"Gold"} // Manually include Gold
	baseTypes = append(baseTypes, data_generation.GetBaseTypes(a.itemBases)...)
	baseTypes = append(baseTypes, data_generation.GetBaseTypes(a.essences)...)
	baseTypes = append(baseTypes, data_generation.GetBaseTypes(a.gems)...)
	a.baseTypes = baseTypes
	a.log.Printf("Total number of base types prepared for compiler: %d", len(a.baseTypes))
}

// compileRules finds and processes all Ruleforge scripts.
func (a *App) compileRules(cssVariables map[string]string) error {
	ruleforgeScripts, err := listFilesWithExtension(a.config.RuleforgeInputDir, ".rf")
	if err != nil {
		return fmt.Errorf("could not list Ruleforge scripts in %s: %w", a.config.RuleforgeInputDir, err)
	}

	if len(ruleforgeScripts) == 0 {
		a.log.Printf("No .rf scripts found in %s. Nothing to compile.", a.config.RuleforgeInputDir)
		return nil
	}
	a.log.Printf("Found %d Ruleforge scripts to process...", len(ruleforgeScripts))

	for _, scriptPath := range ruleforgeScripts {
		if err := a.processRuleforgeScript(scriptPath, cssVariables); err != nil {
			return fmt.Errorf("failed to process script %s: %w", scriptPath, err)
		}
	}
	return nil
}

func (a *App) processRuleforgeScript(path string, cssVariables map[string]string) error {
	a.log.Printf("Processing: %s", path)

	file, err := a.openScript(path)
	if err != nil {
		return err
	}
	defer file.Close()

	tree, err := a.lexAndParse(file)
	if err != nil {
		return err
	}

	tree = a.postProcess(tree)
	if a.verbose {
		a.logParseTree(tree, path)
	}

	if err := a.validateTree(tree); err != nil {
		return err
	}

	baseTypeData, err := a.loadBaseTypeData()
	if err != nil {
		return err
	}

	lines, name, err := a.compileTree(tree, *baseTypeData, cssVariables)
	if err != nil {
		return err
	}

	return a.writeOutputs(lines, name)
}

func (a *App) openScript(path string) (*os.File, error) {
	file, err := os.Open(path)
	if err != nil {
		return nil, fmt.Errorf("opening file %s: %w", path, err)
	}
	return file, nil
}

func (a *App) lexAndParse(file *os.File) (*shared.ParseTree[symbols.LexingTokenType], error) {
	handler := common_compiler.NewFileHandler(
		file,
		rules.GetLexingRules(),
		rules.GetParsingRules(),
		symbols.IgnoreToken,
	)
	if _, err := handler.Lex(); err != nil {
		return nil, fmt.Errorf("lexing failed: %w", err)
	}
	tree, err := handler.Parse()
	if err != nil {
		return nil, fmt.Errorf("parsing failed: %w", err)
	}
	resolvedImportsTree, err := a.ResolveImports(tree)

	if err != nil {
		return nil, fmt.Errorf("resolving imports failed: %w", err)
	}

	return resolvedImportsTree, nil
}

func (a *App) postProcess(tree *shared.ParseTree[symbols.LexingTokenType]) *shared.ParseTree[symbols.LexingTokenType] {
	pp := postprocessor.PostProcessor[symbols.LexingTokenType]{}
	tree = pp.FilterOutSymbols([]string{
		symbols.ParseSymbolWhitespace.String(),
		symbols.ParseSymbolBlockOperator.String(),
	}, tree)
	return pp.RemoveEmptyNodes(tree)
}

func (a *App) logParseTree(tree *shared.ParseTree[symbols.LexingTokenType], path string) {
	a.log.Printf("Parse tree for %s:", path)
	tree.Print(2, []symbols.LexingTokenType{})
}

func (a *App) validateTree(tree *shared.ParseTree[symbols.LexingTokenType]) error {
	if err := validation.NewParseTreeValidator(tree).Validate(); err != nil {
		return fmt.Errorf("parse tree validation failed: %w", err)
	}
	return nil
}

func (a *App) loadBaseTypeData() (*[]config.BaseTypeAutomationEntry, error) {
	loader := config.NewBaseTypeAutomationLoader(a.config.BaseTypeCSVFile)
	data, err := loader.Load()
	if err != nil {
		return nil, fmt.Errorf("loading base type automation data: %w", err)
	}
	return data, nil
}

func (a *App) compileTree(
	tree *shared.ParseTree[symbols.LexingTokenType],
	baseTypeData []config.BaseTypeAutomationEntry,
	cssVariables map[string]string,
) ([]string, string, error) {
	compiler, err := compilation.NewCompiler(
		tree,
		compilation.CompilerConfiguration{
			StyleJsonPath: a.config.StyleJSONFile,
		},
		a.baseTypes,
		a.itemBases,
		a.economyData,
		*a.config.EconomyWeights,
		a.config.GetLeagueWeights(),
		a.config.EconomyNormalizationStrategy,
		a.config.ChaseVSGeneralPotentialFactor,
		baseTypeData,
		cssVariables,
	)
	if err != nil {
		return nil, "", fmt.Errorf("compiler initialization failed: %w", err)
	}
	lines, err, name := compiler.CompileIntoFilter()

	if err != nil {
		return nil, "", fmt.Errorf("compile failed: %w", err)
	}

	return lines, name, nil
}

func (a *App) writeOutputs(lines []string, name string) error {
	for _, dir := range a.config.FilterOutputDirs {
		path := filepath.Join(dir, name+".filter")
		if err := writeLines(lines, path); err != nil {
			return fmt.Errorf("writing output file %s failed: %w", path, err)
		}
		a.log.Printf("Successfully wrote filter to %s", path)
	}
	return nil
}

// --- Data Extraction Helpers ---

func (a *App) extractItemBases(pobDataPath string) ([]model.ItemBase, error) {
	luaFiles, err := listFilesWithExtension(filepath.Join(pobDataPath, "Bases"), ".lua")
	if err != nil {
		return nil, fmt.Errorf("listing .lua files in Bases: %w", err)
	}
	return a.exporter.LoadItemBases(luaFiles)
}

func (a *App) extractEssenceBases(pobDataPath string) ([]model.Essence, error) {
	file := filepath.Join(pobDataPath, "Essence.lua")
	return a.exporter.LoadEssences(file)
}

func (a *App) extractGemBases(pobDataPath string) ([]model.Gem, error) {
	file := filepath.Join(pobDataPath, "Gems.lua")
	return a.exporter.LoadGems(file)
}

func (a *App) extractUniqueBases(pobDataPath string) ([]model.Unique, error) {
	var allUniques []model.Unique

	// Standard uniques
	luaFiles, err := listFilesWithExtension(filepath.Join(pobDataPath, "Uniques"), ".lua")
	if err != nil {
		return nil, fmt.Errorf("listing .lua files in Uniques: %w", err)
	}
	bases, err := a.exporter.LoadUniques(luaFiles)
	if err != nil {
		return nil, fmt.Errorf("loading uniques: %w", err)
	}
	allUniques = append(allUniques, bases...)

	// New uniques
	newUniqueFile := filepath.Join(pobDataPath, "Uniques", "Special", "New.Lua")
	newUniques, err := a.exporter.LoadUpcomingUniqueItems(newUniqueFile)
	if err != nil {
		// Log as warning, as this file might not always exist
		a.log.Printf("Warning: could not load upcoming uniques (file might be missing): %v", err)
	} else {
		allUniques = append(allUniques, newUniques...)
	}

	// Generated uniques
	generatedUniqueFile := filepath.Join(pobDataPath, "Uniques", "Special", "Generated.Lua")
	generatedUniques, err := a.exporter.LoadGeneratedUniques(generatedUniqueFile)
	if err != nil {
		return nil, fmt.Errorf("loading generated uniques: %w", err)
	}
	allUniques = append(allUniques, generatedUniques...)

	return allUniques, nil
}

// --- Imports ---

//goland:noinspection t
func (a *App) ResolveImports(node *shared.ParseTree[symbols.LexingTokenType]) (*shared.ParseTree[symbols.LexingTokenType], error) {
	if node.Symbol != symbols.ParseSymbolImport.String() && len(node.Children) == 0 {
		return node, nil
	} else if node.Symbol != symbols.ParseSymbolImport.String() && len(node.Children) > 0 {
		resolvedNode := &shared.ParseTree[symbols.LexingTokenType]{
			Symbol:   node.Symbol,
			Token:    node.Token,
			Children: make([]*shared.ParseTree[symbols.LexingTokenType], 0),
		}

		for _, child := range node.Children {
			resolvedChild, err := a.ResolveImports(child)

			if err != nil {
				return nil, err
			}

			resolvedNode.Children = append(resolvedNode.Children, resolvedChild)
		}

		return resolvedNode, nil
	}

	if node.Symbol == symbols.ParseSymbolImport.String() {
		importFileNameNode := node.FindSymbolNode(symbols.ParseSymbolValue.String())
		importFileName := importFileNameNode.Token.ValueToString()

		importFilePath := path.Join(a.config.RuleforgeInputDir, importFileName)

		file, err := a.openScript(importFilePath)
		if err != nil {
			return nil, err
		}
		defer file.Close()

		handler := common_compiler.NewFileHandler(
			file,
			rules.GetLexingRules(),
			rules.GetParsingRules(),
			symbols.IgnoreToken,
		)

		_, err = handler.Lex()
		if err != nil {
			return nil, err
		}

		parsed, err := handler.Parse()
		if err != nil {
			return nil, err
		}

		return a.ResolveImports(parsed)
	}

	return nil, fmt.Errorf("something went wrong when importing symbol '%s'", node.Symbol)
}

// --- File I/O Helpers ---

func listFilesWithExtension(dir, ext string) ([]string, error) {
	if !strings.HasPrefix(ext, ".") {
		ext = "." + ext
	}
	var matches []string
	entries, err := os.ReadDir(dir)
	if err != nil {
		return nil, fmt.Errorf("reading directory %s: %w", dir, err)
	}
	for _, entry := range entries {
		if !entry.IsDir() && filepath.Ext(entry.Name()) == ext {
			matches = append(matches, filepath.Join(dir, entry.Name()))
		}
	}
	return matches, nil
}

func writeLines(lines []string, path string) error {
	content := strings.Join(lines, "\n") + "\n"
	if err := os.MkdirAll(filepath.Dir(path), 0755); err != nil {
		return fmt.Errorf("creating output directory %s: %w", filepath.Dir(path), err)
	}
	return os.WriteFile(path, []byte(content), 0644)
}
