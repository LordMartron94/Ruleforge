package main

import (
	"flag"
	"fmt"
	"log"
	"os"
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
	// 1. Load and validate configuration.
	if err := a.loadConfig(); err != nil {
		return err
	}

	// 2. Initialize the exporter, which will attempt to load from cache.
	a.log.Println("Initializing data exporter (will use cache if available and valid)...")
	a.exporter = data_generation.NewPathOfBuildingExporter()

	// 3. Fetch data. The exporter will return cached data or re-parse files as needed.
	if err := a.fetchData(); err != nil {
		return fmt.Errorf("could not fetch data: %w", err)
	}

	// 4. Save data back to cache. The exporter will skip if the cache is still valid.
	if err := a.saveCaches(); err != nil {
		return fmt.Errorf("could not save data to cache: %w", err)
	}

	// If the user only wants to update the cache, we're done.
	if a.updateCacheOnly {
		a.log.Println("Cache update process complete. Exiting.")
		return nil
	}

	// 5. Prepare shared data for compilation.
	a.prepareBaseTypes()

	// 6. Compile Ruleforge scripts.
	if err := a.compileRules(); err != nil {
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
func (a *App) compileRules() error {
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
		if err := a.processRuleforgeScript(scriptPath); err != nil {
			return fmt.Errorf("failed to process script %s: %w", scriptPath, err)
		}
	}
	return nil
}

// processRuleforgeScript handles the full compilation pipeline for a single script file.
func (a *App) processRuleforgeScript(path string) error {
	a.log.Printf("Processing: %s", path)
	file, err := os.Open(path)
	if err != nil {
		return fmt.Errorf("opening file: %w", err)
	}
	defer file.Close()

	// Lexing & Parsing
	handler := common_compiler.NewFileHandler(file, rules.GetLexingRules(), rules.GetParsingRules(), symbols.IgnoreToken)
	if _, err := handler.Lex(); err != nil {
		return fmt.Errorf("lexing failed: %w", err)
	}
	tree, err := handler.Parse()
	if err != nil {
		return fmt.Errorf("parsing failed: %w", err)
	}

	// Post-processing
	postProcessor := postprocessor.PostProcessor[symbols.LexingTokenType]{}
	tree = postProcessor.FilterOutSymbols([]string{
		symbols.ParseSymbolWhitespace.String(),
		symbols.ParseSymbolBlockOperator.String(),
	}, tree)
	tree = postProcessor.RemoveEmptyNodes(tree)

	if a.verbose {
		a.log.Printf("Parse tree for %s:", path)
		tree.Print(2, []symbols.LexingTokenType{})
	}

	// Validation
	if err := validation.NewParseTreeValidator(tree).Validate(); err != nil {
		return fmt.Errorf("parse tree validation failed: %w", err)
	}

	// Load additional compiler-specific configuration
	baseTypeDataLoader := config.NewBaseTypeAutomationLoader("./basetype_automation_config.csv")
	baseTypeData, err := baseTypeDataLoader.Load()
	if err != nil {
		return fmt.Errorf("loading base type automation data: %w", err)
	}

	// Compilation
	compiler, err := compilation.NewCompiler(tree, compilation.CompilerConfiguration{
		StyleJsonPath: a.config.StyleJSONFile,
	}, a.baseTypes, a.itemBases, a.economyData, *a.config.EconomyWeights, a.config.GetLeagueWeights(), a.config.EconomyNormalizationStrategy, a.config.ChaseVSGeneralPotentialFactor, *baseTypeData)
	if err != nil {
		return fmt.Errorf("compiler initialization failed: %w", err)
	}

	outputStrings, err, outputName := compiler.CompileIntoFilter()
	if err != nil {
		return fmt.Errorf("compilation failed: %w", err)
	}

	// Writing output files
	for _, outputDir := range a.config.FilterOutputDirs {
		outputFileName := filepath.Join(outputDir, outputName+".filter")
		if err := writeLines(outputStrings, outputFileName); err != nil {
			return fmt.Errorf("writing output file to %s failed: %w", outputFileName, err)
		}
		a.log.Printf("Successfully wrote filter to %s", outputFileName)
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
