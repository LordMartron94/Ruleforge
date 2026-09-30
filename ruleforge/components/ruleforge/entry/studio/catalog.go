package studio

import (
	"os"
	"path/filepath"
	"sort"
	"strings"

	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/compilation"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/config"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/rules"
)

// Catalog is what the Studio's editors offer to choose from.
type Catalog struct {
	Conditions   []rules.ConditionSpec   `json:"conditions"`
	Operators    []string                `json:"operators"`
	Strictnesses []string                `json:"strictnesses"`
	Builds       []string                `json:"builds"`
	Presets      []string                `json:"presets"`
	Macros       []compilation.MacroSpec `json:"macros"`
	ItemClasses  []string                `json:"itemClasses"`
	BaseTypes    []string                `json:"baseTypes"`
	Categories   []string                `json:"categories"`
	// Styles are in every enabled theme's stylesheet; PartialStyles only in some.
	Styles        []string `json:"styles"`
	PartialStyles []string `json:"partialStyles"`
	Sounds        []string `json:"sounds"`
	MinimapShapes []string `json:"minimapShapes"`
	Colours       []string `json:"colours"`
}

// itemClasses are the game's item classes as of 3.29, from NeverSink's filter.
var itemClasses = []string{
	"Abyss Jewels", "Amulets", "Belts", "Blueprints", "Body Armours", "Boots", "Bows", "Chart", "Claws", "Contracts",
	"Corpses", "Cursed Ducat", "Daggers", "Delve Stackable Socketable Currency", "Divination Cards", "Fishing Rods",
	"Gloves", "Heist Brooches", "Heist Cloaks", "Heist Gear", "Heist Targets", "Heist Tools", "Helmets", "Hybrid Flasks",
	"Idols", "Incursion Items", "Jewels", "Labyrinth Items", "Labyrinth Trinkets", "Life Flasks", "Mana Flasks",
	"Map Fragments", "Maps", "Misc Map Items", "One Hand Axes", "One Hand Maces", "One Hand Swords", "Pantheon Souls",
	"Pieces", "Quest Items", "Quivers", "Relics", "Rings", "Rune Daggers", "Sanctum Research", "Sceptres", "Shields",
	"Skill Gems", "Stackable Currency", "Staves", "Support Gems", "Thrusting One Hand Swords", "Tinctures", "Trinkets",
	"Two Hand Axes", "Two Hand Maces", "Two Hand Swords", "Utility Flasks", "Vault Keys", "Wands", "Warstaves", "Wombgifts",
}

// CatalogInput is what NewCatalog builds the catalog from.
type CatalogInput struct {
	BaseTypes  []string
	Presets    []string
	Automation [][]config.BaseTypeAutomationEntry
	// Stylesheets are the enabled themes' loaded stylesheets.
	Stylesheets []*config.Stylesheet
	// SoundDirs are searched for sound files a style can play.
	SoundDirs []string
}

// NewCatalog gathers the editors' choices.
func NewCatalog(input CatalogInput) Catalog {
	catalog := Catalog{
		Conditions:    rules.ConditionSpecs,
		Operators:     rules.Operators,
		Strictnesses:  rules.Strictnesses,
		Builds:        rules.BuildKeywords,
		Presets:       sortedUnique(input.Presets),
		Macros:        compilation.MacroSpecs,
		ItemClasses:   sortedUnique(append(append([]string{}, itemClasses...), poolClasses()...)),
		BaseTypes:     sortedUnique(input.BaseTypes),
		MinimapShapes: config.MinimapShapes(),
		Colours:       config.ColourLiterals(),
	}
	var categories []string
	for _, entries := range input.Automation {
		for _, entry := range entries {
			categories = append(categories, entry.Category)
		}
	}
	catalog.Categories = sortedUnique(categories)

	count := map[string]int{}
	for _, sheet := range input.Stylesheets {
		for name := range sheet.Styles {
			count[name]++
		}
	}
	for name, n := range count {
		if n == len(input.Stylesheets) {
			catalog.Styles = append(catalog.Styles, name)
		} else {
			catalog.PartialStyles = append(catalog.PartialStyles, name)
		}
	}
	sort.Strings(catalog.Styles)
	sort.Strings(catalog.PartialStyles)

	var sounds []string
	for _, dir := range input.SoundDirs {
		entries, err := os.ReadDir(dir)
		if err != nil {
			continue
		}
		for _, entry := range entries {
			switch strings.ToLower(filepath.Ext(entry.Name())) {
			case ".mp3", ".ogg", ".wav":
				sounds = append(sounds, entry.Name())
			}
		}
	}
	catalog.Sounds = sortedUnique(sounds)
	return catalog
}

func poolClasses() []string {
	var classes []string
	for _, class := range classByPoBType {
		classes = append(classes, class)
	}
	for _, class := range classBySubType {
		if class != "" {
			classes = append(classes, class)
		}
	}
	return classes
}

func sortedUnique(values []string) []string {
	seen := map[string]bool{}
	out := []string{}
	for _, value := range values {
		if value != "" && !seen[value] {
			seen[value] = true
			out = append(out, value)
		}
	}
	sort.Strings(out)
	return out
}
