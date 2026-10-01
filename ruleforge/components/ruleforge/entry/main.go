package main

import (
	"context"
	"errors"
	"flag"
	"fmt"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/parsing/shared"
	"io"
	"log"
	"os"
	"path/filepath"
	"slices"
	"sort"
	"strings"

	common_compiler "github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/postprocessor"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/compilation"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/config"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/data_generation"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/data_generation/model"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/entry/studio"
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
	refreshPoB      bool
	refreshEconomy  bool
	studioServe     bool
	studioAddr      string
	studioOpen      bool

	// Core Components
	log      *log.Logger
	config   *config.ConfigurationModel
	styles   []config.StyleDefinition
	exporter *data_generation.PathOfBuildingExporter

	// Loaded Data
	baseTypes   []string
	itemBases   []model.ItemBase
	essences    []model.Essence
	gems        []model.Gem
	uniques     []model.Unique
	economyData map[string][]data_generation.EconomySnapshot
	// currency is reloaded on every compile, so an edited profile table
	// shows in the next Studio compile.
	currency compilation.CurrencySettings

	// Compiled filters, kept for the studio.
	studioFilters []studio.Filter
}

func main() {
	app := &App{
		log: log.New(os.Stdout, "", log.LstdFlags),
	}

	// Define command-line flags. The exporter handles local caches automatically.
	flag.StringVar(&app.configPath, "config", "config.toml", "Path to the configuration file.")
	flag.BoolVar(&app.verbose, "verbose", false, "Enable verbose output for debugging.")
	flag.BoolVar(&app.updateCacheOnly, "update-cache-only", false, "Fetch/update data and save to cache without running compilation.")
	flag.BoolVar(&app.refreshPoB, "refresh-pob", false, "Check the official Path of Building revision now and download it if changed (error if offline).")
	flag.BoolVar(&app.refreshEconomy, "refresh-economy", false, "Record fresh poe.ninja prices now, even if the last snapshot is younger than EconomyHistoryRefreshHours.")
	flag.BoolVar(&app.studioServe, "studio", false, "After compiling, serve the Ruleforge Studio with its theme, stylesheet and script editors until stopped.")
	flag.StringVar(&app.studioAddr, "studio-addr", "127.0.0.1:7878", "Address the Studio server listens on. Keep it on localhost: the Studio writes your files.")
	flag.BoolVar(&app.studioOpen, "open", false, "Open the Studio server in the default browser.")
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

	// Load the styles before fetching data, so a broken style file fails fast.
	if err := a.loadStyles(); err != nil {
		return err
	}

	// Initialize the exporter, which will attempt to load from cache.
	a.log.Println("Initializing data exporter (will use cache if available and valid)...")
	var err error
	a.exporter, err = data_generation.NewPathOfBuildingExporter(
		filepath.Join(filepath.Dir(a.config.PathOfBuildingDataPath), "data-cache"),
		a.config.EconomyHistoryDBPath,
	)
	if err != nil {
		return fmt.Errorf("initializing data exporter: %w", err)
	}
	defer func() {
		if closeErr := a.exporter.Close(); closeErr != nil {
			a.log.Printf("WARN: closing data exporter: %v", closeErr)
		}
	}()

	changed, err := defaultPoBSource(a.log).ensure(context.Background(), a.config.PathOfBuildingDataPath, a.refreshPoB)
	if err != nil {
		return err
	}
	if changed {
		a.exporter.InvalidateItemCache()
	}

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
	if err := a.compileRules(); err != nil {
		return fmt.Errorf("error during rule compilation: %w", err)
	}

	if err := a.writeStudio(); err != nil {
		return fmt.Errorf("writing the studio: %w", err)
	}

	a.log.Println("Compilation finished successfully.")
	if a.studioServe {
		return a.serveStudio()
	}
	return nil
}

// loadStyles reads the style definitions and keeps the enabled ones.
func (a *App) loadStyles() error {
	definitions, err := config.LoadStyleDefinitions(a.config.StylesDir)
	if err != nil {
		return err
	}
	a.styles = nil
	for _, style := range definitions {
		if !style.IsEnabled() {
			a.log.Printf("Style %q is disabled; skipping it.", style.Name)
			continue
		}
		a.styles = append(a.styles, style)
	}
	if len(a.styles) == 0 {
		return fmt.Errorf("every style in %s is disabled", a.config.StylesDir)
	}
	a.log.Printf("Loaded %d style(s) from %s.", len(a.styles), a.config.StylesDir)
	return nil
}

// loadConfig loads and validates the TOML configuration file.
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
	a.log.Println("Item data loaded; fetching economy data (may require network access)...")
	err = a.fetchEconomyData(a.refreshEconomy)
	if err != nil {
		return fmt.Errorf("fetchEconomyData: %w", err)
	}

	a.log.Printf("Data loaded: %d item bases, %d essences, %d gems, %d uniques.", len(a.itemBases), len(a.essences), len(a.gems), len(a.uniques))
	return nil
}

// fetchEconomyData refreshes any stale league snapshots (recording them into
// the economy history database, which is never pruned) and then loads each
// league's full recorded history for tiering. regenerate forces a refetch
// even for leagues with a recent snapshot.
func (a *App) fetchEconomyData(regenerate bool) error {
	leagues := a.config.GetLeaguesToRetrieve()
	if err := a.exporter.RefreshEconomyData(leagues, a.config.GetEconomyHistoryRefreshInterval(), regenerate); err != nil {
		return fmt.Errorf("RefreshEconomyData: %w", err)
	}

	history, err := a.exporter.EconomyHistory(leagues)
	if err != nil {
		return fmt.Errorf("EconomyHistory: %w", err)
	}
	a.economyData = history
	return nil
}

// saveCaches persists the fetched item cache. Economy snapshots are already
// recorded as soon as they're fetched and are kept forever, so there's no
// separate economy "save" step.
func (a *App) saveCaches() error {
	if err := a.exporter.SaveItemCache(a.itemBases, a.essences, a.gems, a.uniques); err != nil {
		return fmt.Errorf("exporter.SaveItemCache: %w", err)
	}
	return nil
}

// prepareBaseTypes aggregates all base types from the loaded data.
func (a *App) prepareBaseTypes() {
	baseTypes := []string{"Gold"} // Manually include Gold
	baseTypes = append(baseTypes, data_generation.GetBaseTypes(a.itemBases)...)
	baseTypes = append(baseTypes, data_generation.GetBaseTypes(a.essences)...)
	baseTypes = append(baseTypes, data_generation.GetBaseTypes(a.gems)...)
	// Divination cards, tattoos and currency aren't tracked by Path of
	// Building, so their names come from whatever poe.ninja has actually
	// priced instead of a bundled list.
	baseTypes = append(baseTypes, pricedOnlyBaseTypes(a.economyData)...)
	a.baseTypes = baseTypes
	a.log.Printf("Total number of base types prepared for compiler: %d", len(a.baseTypes))
}

// pricedOnlyClasses are the economy classes whose base types Path of Building
// doesn't know.
var pricedOnlyClasses = map[string]bool{"Divination Cards": true, "Tattoos": true, "Currency": true}

// pricedOnlyBaseTypes collects every divination card, tattoo and currency name
// seen across all recorded economy history, deduplicated.
func pricedOnlyBaseTypes(economyData map[string][]data_generation.EconomySnapshot) []string {
	seen := make(map[string]bool)
	for _, snapshots := range economyData {
		for _, snapshot := range snapshots {
			for _, item := range snapshot.Items {
				if pricedOnlyClasses[item.Class] {
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

// compileRules compiles every Ruleforge script with every enabled style and
// writes the filters.
func (a *App) compileRules() error {
	filters, err := a.compile(compileOptions{write: true})
	if err != nil {
		return err
	}
	a.studioFilters = filters
	return nil
}

// compileOptions changes what a compile reads and writes, so the Studio can
// check an edit before saving it.
type compileOptions struct {
	// write writes the filters to the output folders.
	write bool
	// scripts replaces the contents of these script paths; a path that does
	// not exist yet is compiled as a new script.
	scripts map[string]string
	// only limits the compile to these script paths.
	only map[string]bool
	// styles replaces the enabled styles.
	styles []config.StyleDefinition
}

// compile compiles the scripts once per style.
func (a *App) compile(options compileOptions) ([]studio.Filter, error) {
	paths, err := listFilesWithExtension(a.config.RuleforgeInputDir, ".rf")
	if err != nil {
		return nil, fmt.Errorf("could not list Ruleforge scripts in %s: %w", a.config.RuleforgeInputDir, err)
	}
	for path := range options.scripts {
		if !slices.Contains(paths, path) {
			paths = append(paths, path)
		}
	}
	sort.Strings(paths)
	if len(paths) == 0 {
		a.log.Printf("No .rf scripts found in %s. Nothing to compile.", a.config.RuleforgeInputDir)
		return nil, nil
	}
	styles := options.styles
	if styles == nil {
		styles = a.styles
	}
	if err := a.loadCurrencyProfiles(styles); err != nil {
		return nil, err
	}
	var filters []studio.Filter
	for _, path := range paths {
		if options.only != nil && !options.only[path] {
			continue
		}
		source, overridden := options.scripts[path]
		if !overridden {
			contents, err := os.ReadFile(path)
			if err != nil {
				return nil, fmt.Errorf("opening file %s: %w", path, err)
			}
			source = string(contents)
		}
		compiled, err := a.processRuleforgeScript(path, source, styles, options.write)
		if err != nil {
			return nil, fmt.Errorf("failed to process script %s: %w", filepath.Base(path), err)
		}
		filters = append(filters, compiled...)
	}
	return filters, nil
}

// processRuleforgeScript parses one script and compiles it once per style.
func (a *App) processRuleforgeScript(path, source string, styles []config.StyleDefinition, write bool) ([]studio.Filter, error) {
	a.log.Printf("Processing: %s", path)

	tree, err := a.lexAndParse(strings.NewReader(source))
	if err != nil {
		return nil, err
	}

	tree = a.postProcess(tree)
	if a.verbose {
		a.logParseTree(tree, path)
	}

	if err := a.validateTree(tree); err != nil {
		return nil, err
	}

	var filters []studio.Filter
	for _, style := range styles {
		a.log.Printf("Compiling %s with style %q...", filepath.Base(path), style.Name)
		baseTypeData, err := a.loadBaseTypeData(style.BaseTypes)
		if err != nil {
			return nil, fmt.Errorf("style %q: %w", style.Name, err)
		}

		compiler, lines, name, err := a.compileTree(tree, *baseTypeData, style)
		if err != nil {
			return nil, fmt.Errorf("style %q: %w", style.Name, err)
		}

		fileName := style.FilterFileName(name)
		if write {
			if err := a.writeOutputs(lines, fileName); err != nil {
				return nil, err
			}
		}
		filters = append(filters, studio.Filter{
			Script:     name,
			ScriptFile: filepath.Base(path),
			StyleID:    style.ID,
			FileName:   fileName,
			Blocks:     compiler.StudioBlocks(lines),
		})
	}
	return filters, nil
}

func (a *App) openScript(path string) (*os.File, error) {
	file, err := os.Open(path)
	if err != nil {
		return nil, fmt.Errorf("opening file %s: %w", path, err)
	}
	return file, nil
}

func (a *App) lexAndParse(file io.Reader) (tree *shared.ParseTree[symbols.LexingTokenType], err error) {
	defer func() {
		if r := recover(); r != nil {
			err = fmt.Errorf("the script could not be read: %v", r)
		}
	}()
	handler := common_compiler.NewFileHandler(
		file,
		rules.GetLexingRules(),
		rules.GetParsingRules(),
		symbols.IgnoreToken,
	)
	if _, err := handler.Lex(); err != nil {
		return nil, fmt.Errorf("lexing failed: %w", err)
	}
	tree, err = handler.Parse()
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

// loadCurrencyProfiles reads the currency_tiering profiles and reports what
// they miss: priced currency that neither they nor any style's base type
// categories list, and profiled currency without a price yet. A missing
// profile file only matters to a script that uses currency_tiering.
func (a *App) loadCurrencyProfiles(styles []config.StyleDefinition) error {
	settings := a.config.CurrencyTiering
	a.currency = compilation.CurrencySettings{CraftingWeight: settings.CraftingWeight, Source: settings.Profiles}
	if _, err := os.Stat(settings.Profiles); errors.Is(err, os.ErrNotExist) {
		return nil
	}
	profiles, err := config.LoadCurrencyProfiles(settings.Profiles)
	if err != nil {
		return err
	}
	a.currency.Profiles = profiles

	listed := make(map[string]bool)
	for _, profile := range profiles {
		listed[profile.BaseType] = true
	}
	for _, style := range styles {
		entries, err := a.loadBaseTypeData(style.BaseTypes)
		if err != nil {
			return fmt.Errorf("style %q: %w", style.Name, err)
		}
		for _, entry := range *entries {
			listed[entry.BaseType] = true
		}
	}
	priced := make(map[string]bool)
	for _, snapshots := range a.economyData {
		for _, snapshot := range snapshots {
			for _, item := range snapshot.Items {
				if item.Class == "Currency" {
					priced[item.BaseType] = true
				}
			}
		}
	}
	var unlisted, unpriced []string
	for baseType := range priced {
		if !listed[baseType] {
			unlisted = append(unlisted, baseType)
		}
	}
	for _, profile := range profiles {
		if !priced[profile.BaseType] {
			unpriced = append(unpriced, profile.BaseType)
		}
	}
	sort.Strings(unlisted)
	if len(unlisted) > 0 {
		a.log.Printf("WARNING: poe.ninja prices currency that no currency profile or base type category lists, so it drops with the fallback look: %s", strings.Join(unlisted, ", "))
	}
	if len(unpriced) > 0 && len(priced) > 0 {
		a.log.Printf("Currency profiles without a price yet, tiered on their crafting rating alone: %s", strings.Join(unpriced, ", "))
	}
	return nil
}

func (a *App) loadBaseTypeData(path string) (*[]config.BaseTypeAutomationEntry, error) {
	loader := config.NewBaseTypeAutomationLoader(path)
	data, err := loader.Load()
	if err != nil {
		return nil, fmt.Errorf("loading base type automation data: %w", err)
	}
	return data, nil
}

func (a *App) compileTree(
	tree *shared.ParseTree[symbols.LexingTokenType],
	baseTypeData []config.BaseTypeAutomationEntry,
	style config.StyleDefinition,
) (*compilation.Compiler, []string, string, error) {
	compiler, err := compilation.NewCompiler(
		tree,
		compilation.CompilerConfiguration{
			StylesheetPath: style.Stylesheet,
			Palette:        style.Palette,
		},
		a.baseTypes,
		a.itemBases,
		a.economyData,
		*a.config.EconomyWeights,
		a.config.GetLeagueWeights(),
		a.config.EconomyNormalizationStrategy,
		a.config.ChaseVSGeneralPotentialFactor,
		a.config.EconomyValueHalfLifeDays,
		baseTypeData,
		a.config.CustomEquipmentPresets,
		a.currency,
	)
	if err != nil {
		return nil, nil, "", fmt.Errorf("compiler initialization failed: %w", err)
	}
	lines, err, name := compiler.CompileIntoFilter()

	if err != nil {
		return nil, nil, "", fmt.Errorf("compile failed: %w", err)
	}

	return compiler, lines, name, nil
}

func (a *App) writeOutputs(lines []string, fileName string) error {
	for _, dir := range a.config.FilterOutputDirs {
		path := filepath.Join(dir, fileName)
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
	if cached, ok := a.exporter.CachedUniques(); ok {
		a.log.Printf("Using %d cached standard/upcoming uniques (generated uniques disabled in low-memory mode)", len(cached))
		return cached, nil
	}

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
	newUniqueFile := filepath.Join(pobDataPath, "Uniques", "Special", "New.lua")
	newUniques, err := a.exporter.LoadUpcomingUniqueItems(newUniqueFile)
	if err != nil {
		// Log as warning, as this file might not always exist
		a.log.Printf("Warning: could not load upcoming uniques (file might be missing): %v", err)
	} else {
		allUniques = append(allUniques, newUniques...)
	}

	// Deliberately do not execute PoB Generated.lua. Its full Lua data graph
	// uses excessive RAM on memory-constrained Linux installations.
	// Standard and upcoming uniques remain available; generator-only items
	// are omitted until a separate resource-bounded implementation exists.
	a.log.Printf("Low-memory mode: loaded %d standard/upcoming uniques; skipping PoB Generated.lua", len(allUniques))

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

		importFilePath := filepath.Join(a.config.RuleforgeInputDir, importFileName)

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
