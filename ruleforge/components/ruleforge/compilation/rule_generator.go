package compilation

import (
	"fmt"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/parsing/shared"
	model2 "github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/compilation/model"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/config"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/data_generation"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/data_generation/model"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/rules/symbols"
	"log"
	"slices"
	"sort"
)

// RuleGenerator is the engine for compiling rules. It contains all complex game logic.
type RuleGenerator struct {
	ruleFactory           *RuleFactory
	styleManager          *StyleManager
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
	build                 *Build
}

// NewRuleGenerator creates the rule generation engine.
func NewRuleGenerator(
	factory *RuleFactory,
	styleMgr *StyleManager,
	validBases []string,
	armors []model.ItemBase,
	weapons []model.ItemBase,
	flasks []model.ItemBase,
	economyCache map[string][]data_generation.EconomyCacheItem,
	economyWeights config.EconomyWeights,
	leagueWeights []config.LeagueWeights,
	normalizationStrategy string,
	chasePotentialWeight float64,
	baseTypeData []config.BaseTypeAutomationEntry,
	build *Build,
) *RuleGenerator {
	sort.Slice(armors, func(i, j int) bool {
		itemA := armors[i]
		itemB := armors[j]
		dropLevelA := getDropLevel(&itemA)
		dropLevelB := getDropLevel(&itemB)
		if dropLevelA == dropLevelB {
			return itemA.Name < itemB.Name
		}
		return dropLevelA < dropLevelB
	})

	sort.Slice(weapons, func(i, j int) bool {
		itemA := weapons[i]
		itemB := weapons[j]
		dropLevelA := getDropLevel(&itemA)
		dropLevelB := getDropLevel(&itemB)
		if dropLevelA == dropLevelB {
			return itemA.Name < itemB.Name
		}
		return dropLevelA < dropLevelB
	})

	sort.Slice(flasks, func(i, j int) bool {
		itemA := flasks[i]
		itemB := flasks[j]
		dropLevelA := getDropLevel(&itemA)
		dropLevelB := getDropLevel(&itemB)
		if dropLevelA == dropLevelB {
			return itemA.Name < itemB.Name
		}
		return dropLevelA < dropLevelB
	})

	return &RuleGenerator{
		ruleFactory:           factory,
		styleManager:          styleMgr,
		validBaseTypes:        validBases,
		armorBases:            armors,
		weaponBases:           weapons,
		flaskBases:            flasks,
		economyCache:          economyCache,
		economyWeights:        economyWeights,
		leagueWeights:         leagueWeights,
		normalizationStrategy: normalizationStrategy,
		chasePotentialWeight:  chasePotentialWeight,
		baseTypeData:          baseTypeData,
		build:                 build,
	}
}

// GenerateRulesForSection compiles all rules within a single logical section.
func (rg *RuleGenerator) GenerateRulesForSection(
	section ExtractedSection,
	variables map[string][]string,
) ([][]string, error) {
	var allGeneratedRules [][]string

	for _, childNode := range section.RuleNodes {
		var generatedRules [][]string
		var err error

		switch childNode.Symbol {
		case symbols.ParseSymbolRuleExpression.String():
			generatedRules, err = rg.handleRuleExpression(childNode, &variables, section.Conditions)
		case symbols.ParseSymbolMacroExpression.String():
			generatedRules, err = rg.handleMacroExpression(childNode, &variables, section.Conditions)
		default:
			return nil, fmt.Errorf("unsupported symbol in rule list: %s", childNode.Symbol)
		}

		if err != nil {
			return nil, err
		}
		allGeneratedRules = append(allGeneratedRules, generatedRules...)
	}
	return allGeneratedRules, nil
}

func (rg *RuleGenerator) handleRuleExpression(
	ruleExpressionNode *shared.ParseTree[symbols.LexingTokenType],
	variables *map[string][]string,
	sectionConditions []model2.Condition,
) ([][]string, error) {
	styleValue := ruleExpressionNode.Children[2].Token.ValueToString()
	style, err := rg.styleManager.GetStyle(styleValue)
	if err != nil {
		return nil, err
	}

	showOrHideStr := ruleExpressionNode.Children[4].Token.ValueToString()[1:]
	var action model2.RuleType
	if showOrHideStr == "Show" {
		action = model2.ShowRule
	} else {
		action = model2.HideRule
	}

	ruleSpecificConditions := retrieveConditions(ruleExpressionNode)

	rule := &model2.ParsedRule{
		Style:          style,
		Action:         action,
		Conditions:     ruleSpecificConditions,
		Variables:      variables,
		ValidBaseTypes: rg.validBaseTypes,
	}

	return rg.compileParsedRule(rule, sectionConditions), nil
}

func (rg *RuleGenerator) handleMacroExpression(
	macroExpressionNode *shared.ParseTree[symbols.LexingTokenType],
	variables *map[string][]string,
	sectionConditions []model2.Condition,
) ([][]string, error) {
	macroType := macroExpressionNode.Children[1].Token.ValueToString()
	parameters := macroExpressionNode.FindAllSymbolNodes(symbols.ParseSymbolParameter.String())

	switch macroType {
	case "item_progression-equipment-leveling":
		return rg.handleEquipmentProgression(variables, parameters, 0, 67)
	case "item_progression-equipment-mapping":
		return rg.handleEquipmentProgression(variables, parameters, 68, 84) // 84 is max zone level for maps (T17)
	case "item_progression-flasks":
		return rg.handleFlaskProgression(variables, parameters)
	case "unique_tiering":
		return rg.handleUniqueTiering(variables, parameters)
	case "skill_gem_tiering":
		return rg.handleGemTiering(variables, parameters)
	case "handle_csv":
		return rg.handleCSVMacro(variables, parameters, sectionConditions)
	case "veiled":
		return rg.handleVeiledEquipment(variables, parameters)
	default:
		return nil, fmt.Errorf("unsupported macro type: %s", macroType)
	}
}

//goland:noinspection t
func (rg *RuleGenerator) handleVeiledEquipment(
	variables *map[string][]string,
	parameters []*shared.ParseTree[symbols.LexingTokenType],
) ([][]string, error) {
	var allGeneratedRules [][]string

	style, err := rg.extractStyle(parameters)

	if err != nil {
		return nil, err
	}

	identifiedCondition := model2.Condition{
		Identifier: "@identified",
		Operator:   "==",
		Value:      []string{"True"},
	}

	modCondition := model2.Condition{
		Identifier: "@has_explicit_mod",
		Operator:   "",
		Value:      []string{"Veiled", "of the Veil"},
	}

	rule := &model2.ParsedRule{
		Style:          style,
		Action:         model2.ShowRule,
		Conditions:     []model2.Condition{identifiedCondition, modCondition},
		Variables:      variables,
		ValidBaseTypes: rg.validBaseTypes,
	}

	allGeneratedRules = append(allGeneratedRules, rg.compileParsedRule(rule, []model2.Condition{})...)

	return allGeneratedRules, nil
}

func (rg *RuleGenerator) extractStyle(parameters []*shared.ParseTree[symbols.LexingTokenType]) (*config.Style, error) {
	var styleString string

	for _, childNode := range parameters {
		parameterKey, parameterValue := rg.getKeyAndValueFromParameter(childNode)

		if parameterKey == "$style" {
			styleString = parameterValue
			continue
		}

		return nil, fmt.Errorf("unknown variable: " + parameterKey)
	}

	if styleString == "" {
		return nil, fmt.Errorf("no style parameter found")
	}

	style, err := rg.styleManager.GetStyle(styleString)
	if err != nil {
		return nil, fmt.Errorf("Error extracting style: " + err.Error())
	}

	return style, nil
}

func (rg *RuleGenerator) handleEquipmentProgression(
	variables *map[string][]string,
	parameters []*shared.ParseTree[symbols.LexingTokenType],
	minAreaLevel int,
	maxAreaLevel int,
) ([][]string, error) {
	var allGeneratedRules [][]string
	itemsByCategory := make(map[string][]*model.ItemBase)
	for i := range rg.weaponBases {
		weapon := rg.weaponBases[i]
		if rg.build.IsWeaponAssociated(weapon) {
			itemsByCategory[weapon.Type] = append(itemsByCategory[weapon.Type], &rg.weaponBases[i])
		}
	}
	for i := range rg.armorBases {
		armor := rg.armorBases[i]
		if rg.build.IsArmorAssociated(armor) {
			itemsByCategory[armor.Type] = append(itemsByCategory[armor.Type], &rg.armorBases[i])
		}
	}

	required := []string{"$hidden_normal", "$hidden_magic", "$hidden_rare", "$show_normal", "$show_magic", "$show_rare"}

	styleMap, err := rg.extractStyleParameters(parameters, required, []string{"$max_roll"})
	if err != nil {
		return allGeneratedRules, err
	}

	rg.produceProgression(
		itemsByCategory, variables,
		styleMap["$show_normal"], styleMap["$show_magic"], styleMap["$show_rare"],
		styleMap["$hidden_normal"], styleMap["$hidden_magic"], styleMap["$hidden_rare"],
		styleMap["$max_roll"],
		&allGeneratedRules, false, minAreaLevel, maxAreaLevel)

	return allGeneratedRules, nil
}

func (rg *RuleGenerator) handleFlaskProgression(
	variables *map[string][]string,
	parameters []*shared.ParseTree[symbols.LexingTokenType],
) ([][]string, error) {
	var allGeneratedRules [][]string
	itemsByCategory := make(map[string][]*model.ItemBase)
	for i := range rg.flaskBases {
		flask := rg.flaskBases[i]
		key := flask.GetBaseType()
		if flask.SubType == "Life" || flask.SubType == "Mana" || flask.SubType == "Hybrid" {
			key = flask.Type + flask.SubType
		}
		itemsByCategory[key] = append(itemsByCategory[key], &rg.flaskBases[i])
	}

	hiddenStyle, shownStyle, err := rg.getHiddenAndShownStyleFromParameters(parameters)

	if err != nil {
		return allGeneratedRules, err
	}

	rg.produceProgression(itemsByCategory, variables, shownStyle, shownStyle, shownStyle, hiddenStyle, hiddenStyle, hiddenStyle, nil, &allGeneratedRules, true, 0, 100)

	return allGeneratedRules, nil
}

func (rg *RuleGenerator) getHiddenAndShownStyleFromParameters(
	parameters []*shared.ParseTree[symbols.LexingTokenType],
) (*config.Style, *config.Style, error) {
	required := []string{"$hidden", "$show"}

	styleMap, err := rg.extractStyleParameters(parameters, required, []string{})
	if err != nil {
		return nil, nil, err
	}

	return styleMap["$hidden"], styleMap["$show"], nil
}

//goland:noinspection t
func (rg *RuleGenerator) extractStyleParameters(
	parameters []*shared.ParseTree[symbols.LexingTokenType],
	requiredKeys []string,
	optionalKeys []string,
) (map[string]*config.Style, error) {
	// Build lookup sets
	requiredSet := make(map[string]struct{}, len(requiredKeys))
	for _, key := range requiredKeys {
		requiredSet[key] = struct{}{}
	}
	optionalSet := make(map[string]struct{}, len(optionalKeys))
	for _, key := range optionalKeys {
		optionalSet[key] = struct{}{}
	}

	// Single result map for both required and optional styles
	foundStyles := make(map[string]*config.Style)

	// Extract styles for any key in either set
	for _, parameter := range parameters {
		key, value := rg.getKeyAndValueFromParameter(parameter)

		if _, isRequired := requiredSet[key]; isRequired ||
			func() bool { _, ok := optionalSet[key]; return ok }() {

			style, err := rg.styleManager.GetStyle(value)
			if err != nil {
				kind := "optional"
				if _, isRequired := requiredSet[key]; isRequired {
					kind = "required"
				}
				return nil, fmt.Errorf(
					"failed to get style for %s key %q: %w",
					kind, key, err,
				)
			}
			foundStyles[key] = style
		}
	}

	// Ensure all required styles were provided
	for _, key := range requiredKeys {
		if _, got := foundStyles[key]; !got {
			return nil, fmt.Errorf(
				"required style parameter %q is absent",
				key,
			)
		}
	}

	return foundStyles, nil
}

func (rg *RuleGenerator) getKeyAndValueFromParameter(parameter *shared.ParseTree[symbols.LexingTokenType]) (string, string) {
	key := parameter.Children[1].Token.ValueToString()
	value := parameter.Children[3].Token.ValueToString()

	return key, value
}

type progressionBucket struct {
	items          []*model.ItemBase
	startLevel     int
	showEndLevel   int
	hideStartLevel int
	isLastTier     bool
}

//goland:noinspection t
func groupProgressionBuckets(
	items []*model.ItemBase,
	minLevel, maxLevel int,
) (buckets []progressionBucket, outdatedTypes []string) {
	for i := 0; i < len(items); {
		item := items[i]
		lvl := getDropLevel(item)

		if lvl < minLevel {
			outdatedTypes = append(outdatedTypes, item.GetBaseType())
			i++
			continue
		}
		if lvl > maxLevel {
			i++
			continue
		}

		// find end of this tier
		end := i
		for end+1 < len(items) && getDropLevel(items[end+1]) == lvl {
			end++
		}
		tierItems := items[i : end+1]
		nextIdx := end + 1
		isLast := nextIdx >= len(items)

		// compute show/hide boundaries
		showEnd := maxLevel
		hideStart := maxLevel
		if !isLast {
			nextLvl := getDropLevel(items[nextIdx])
			showEnd = nextLvl - 1
			hideStart = nextLvl
		}
		if showEnd > maxLevel {
			showEnd = maxLevel
		}

		buckets = append(buckets, progressionBucket{
			items:          tierItems,
			startLevel:     lvl,
			showEndLevel:   showEnd,
			hideStartLevel: hideStart,
			isLastTier:     isLast,
		})

		i = end + 1
	}
	return buckets, outdatedTypes
}

//goland:noinspection t
func (rg *RuleGenerator) produceProgression(
	itemsByCategory map[string][]*model.ItemBase,
	variables *map[string][]string,
	shownNormal, shownMagic, shownRare,
	hiddenNormal, hiddenMagic, hiddenRare, maxRoll *config.Style,
	allGeneratedRules *[][]string,
	disableRare bool,
	minAreaLevel, maxAreaLevel int,
) {
	for _, categoryItems := range itemsByCategory {
		if len(categoryItems) == 0 {
			continue
		}

		buckets, outdated := groupProgressionBuckets(categoryItems, minAreaLevel, maxAreaLevel)
		for _, b := range buckets {
			for _, item := range b.items {
				if item.Armour != nil && maxRoll != nil {
					rg.constructMaxRolledGearRule(variables, model2.ShowRule, *item, allGeneratedRules, maxRoll, fmt.Sprintf("%d", b.showEndLevel))
				}

				rg.constructItemProgressionRule(variables, model2.ShowRule, *item, allGeneratedRules, shownNormal, fmt.Sprintf("%d", b.showEndLevel), "Normal")
				rg.constructItemProgressionRule(variables, model2.ShowRule, *item, allGeneratedRules, shownMagic, fmt.Sprintf("%d", b.showEndLevel), "Magic")
				if !disableRare {
					rg.constructItemProgressionRule(variables, model2.ShowRule, *item, allGeneratedRules, shownRare, fmt.Sprintf("%d", b.showEndLevel), "Rare")
				}

				if !b.isLastTier && b.hideStartLevel <= maxAreaLevel {
					if item.Armour != nil && maxRoll != nil {
						rg.constructMaxRolledGearRule(variables, model2.HideRule, *item, allGeneratedRules, maxRoll, fmt.Sprintf("%d", b.showEndLevel))
					}

					rg.constructItemProgressionRule(variables, model2.HideRule, *item, allGeneratedRules, hiddenNormal, fmt.Sprintf("%d", maxAreaLevel), "Normal")
					rg.constructItemProgressionRule(variables, model2.HideRule, *item, allGeneratedRules, hiddenMagic, fmt.Sprintf("%d", maxAreaLevel), "Magic")
					if !disableRare {
						rg.constructItemProgressionRule(variables, model2.HideRule, *item, allGeneratedRules, hiddenRare, fmt.Sprintf("%d", maxAreaLevel), "Rare")
					}
				}
			}
		}

		if len(outdated) > 0 {
			rg.appendOutdatedHideRules(outdated, hiddenNormal, hiddenMagic, hiddenRare, variables, allGeneratedRules, minAreaLevel)
		}
	}
}

func (rg *RuleGenerator) constructMaxRolledGearRule(
	variables *map[string][]string,
	ruleType model2.RuleType,
	item model.ItemBase,
	allGeneratedRules *[][]string,
	maxRolledStyle *config.Style,
	maxAreaLevel string,
) {
	// Always-present conditions
	areaCondition := model2.Condition{
		Identifier: "@area_level",
		Operator:   "<=",
		Value:      []string{maxAreaLevel},
	}
	baseTypeCondition := model2.Condition{
		Identifier: "@item_type",
		Operator:   "==",
		Value:      []string{item.GetBaseType()},
	}

	// Start with the mandatory conditions
	conds := []model2.Condition{areaCondition, baseTypeCondition}

	// Only add each stat condition if its max is > 0
	if item.Armour.ArmourBaseMax > 0 {
		conds = append(conds, model2.Condition{
			Identifier: "@base_armour",
			Operator:   "==",
			Value:      []string{fmt.Sprintf("%d", item.Armour.ArmourBaseMax)},
		})
	}
	if item.Armour.EvasionBaseMax > 0 {
		conds = append(conds, model2.Condition{
			Identifier: "@base_evasion",
			Operator:   "==",
			Value:      []string{fmt.Sprintf("%d", item.Armour.EvasionBaseMax)},
		})
	}
	if item.Armour.EnergyShieldBaseMax > 0 {
		conds = append(conds, model2.Condition{
			Identifier: "@base_energy_shield",
			Operator:   "==",
			Value:      []string{fmt.Sprintf("%d", item.Armour.EnergyShieldBaseMax)},
		})
	}
	if item.Armour.WardBaseMax > 0 {
		conds = append(conds, model2.Condition{
			Identifier: "@base_ward",
			Operator:   "==",
			Value:      []string{fmt.Sprintf("%d", item.Armour.WardBaseMax)},
		})
	}

	// Build the parsed rule with only the relevant conditions
	rule := &model2.ParsedRule{
		Style:          maxRolledStyle,
		Action:         ruleType,
		Conditions:     conds,
		Variables:      variables,
		ValidBaseTypes: rg.validBaseTypes,
	}

	*allGeneratedRules = append(
		*allGeneratedRules,
		rg.compileParsedRule(rule, []model2.Condition{})...,
	)
}

func (rg *RuleGenerator) appendOutdatedHideRules(
	outdated []string,
	hiddenNormal, hiddenMagic, hiddenRare *config.Style,
	variables *map[string][]string,
	allGeneratedRules *[][]string,
	minAreaLevel int,
) {
	areaCond := model2.Condition{Identifier: "@area_level", Operator: ">=", Value: []string{fmt.Sprintf("%d", minAreaLevel)}}
	typeCond := model2.Condition{Identifier: "@item_type", Operator: "==", Value: outdated}
	for _, rc := range []struct {
		rarity string
		style  *config.Style
	}{
		{"Normal", hiddenNormal},
		{"Magic", hiddenMagic},
		{"Rare", hiddenRare},
	} {
		cond := model2.Condition{Identifier: "@rarity", Operator: "==", Value: []string{rc.rarity}}
		pr := &model2.ParsedRule{
			Style:          rc.style,
			Action:         model2.HideRule,
			Conditions:     []model2.Condition{areaCond, typeCond, cond},
			Variables:      variables,
			ValidBaseTypes: rg.validBaseTypes,
		}
		*allGeneratedRules = append(*allGeneratedRules, rg.compileParsedRule(pr, nil)...)
	}
}

func (rg *RuleGenerator) constructItemProgressionRule(
	variables *map[string][]string,
	ruleType model2.RuleType,
	item model.ItemBase,
	allGeneratedRules *[][]string,
	style *config.Style,
	maxAreaLevel string,
	rarity string) {
	areaCondition := model2.Condition{
		Identifier: "@area_level",
		Operator:   "<=",
		Value:      []string{maxAreaLevel},
	}
	baseTypeCondition := model2.Condition{
		Identifier: "@item_type",
		Operator:   "==",
		Value:      []string{item.GetBaseType()},
	}
	rarityCondition := model2.Condition{
		Identifier: "@rarity",
		Operator:   "==",
		Value:      []string{rarity},
	}

	rule := &model2.ParsedRule{
		Style:          style,
		Action:         ruleType,
		Conditions:     []model2.Condition{areaCondition, baseTypeCondition, rarityCondition},
		Variables:      variables,
		ValidBaseTypes: rg.validBaseTypes,
	}

	*allGeneratedRules = append(*allGeneratedRules, rg.compileParsedRule(rule, []model2.Condition{})...)
}

type TieringConfig struct {
	InitialCondition  model2.Condition
	ItemClassToFilter string
	TieredIdentifier  string
}

// handleUniqueTiering generates tiered rules for unique items based on economy data.
func (rg *RuleGenerator) handleUniqueTiering(variables *map[string][]string, parameters []*shared.ParseTree[symbols.LexingTokenType]) ([][]string, error) {
	uniqueConfig := TieringConfig{
		InitialCondition: model2.Condition{
			Identifier: "@rarity",
			Operator:   "==",
			Value:      []string{"Unique"},
		},
		ItemClassToFilter: "Uniques",
		TieredIdentifier:  "@item_type",
	}
	return rg.generateTieredRules(variables, parameters, uniqueConfig)
}

func (rg *RuleGenerator) handleGemTiering(variables *map[string][]string, parameters []*shared.ParseTree[symbols.LexingTokenType]) ([][]string, error) {
	gemConfig := TieringConfig{
		InitialCondition: model2.Condition{
			Identifier: "@item_class",
			Operator:   "==",
			Value:      []string{"Skill Gems", "Support Gems"},
		},
		ItemClassToFilter: "Gems",
		TieredIdentifier:  "@item_type",
	}
	return rg.generateTieredRules(variables, parameters, gemConfig)
}

func (rg *RuleGenerator) generateTieredRules(
	variables *map[string][]string,
	parameters []*shared.ParseTree[symbols.LexingTokenType],
	tieringConfiguration TieringConfig,
) ([][]string, error) {
	generatedRules := make([][]string, 0)

	// 1. Get tier styles from parameters (Common Logic)
	tierStyles := make([]*config.Style, len(parameters))
	for i, parameter := range parameters {
		_, value := rg.getKeyAndValueFromParameter(parameter)
		style, err := rg.styleManager.GetStyle(value)
		if err != nil {
			panic(err)
		}
		tierStyles[i] = style
	}

	// 2. Handle the case with only one tier (Common Logic)
	if len(tierStyles) == 1 {
		style := tierStyles[0]

		rule := &model2.ParsedRule{
			Style:          style,
			Action:         model2.ShowRule,
			Conditions:     []model2.Condition{tieringConfiguration.InitialCondition},
			Variables:      variables,
			ValidBaseTypes: rg.validBaseTypes,
		}

		generatedRules = append(generatedRules, rg.compileParsedRule(rule, []model2.Condition{})...)
		return generatedRules, nil
	}

	// 3. Filter items from the economy cache based on the provided class (Customizable Logic)
	itemsToCheck := make(map[string][]data_generation.EconomyCacheItem)
	for league, items := range rg.economyCache {
		validItems := make([]data_generation.EconomyCacheItem, 0)
		for _, item := range items {
			if item.Class != tieringConfiguration.ItemClassToFilter {
				continue
			}
			if !slices.Contains(rg.validBaseTypes, item.BaseType) {
				continue
			}
			validItems = append(validItems, item)
		}
		log.Printf("Valid %s for League %s: %d\n", tieringConfiguration.ItemClassToFilter, league, len(validItems))
		itemsToCheck[league] = validItems
	}

	var normStrategy data_generation.NormalizationStrategy

	switch rg.normalizationStrategy {
	case "Global":
		normStrategy = data_generation.Global
	case "Per-League":
		normStrategy = data_generation.PerLeague
	default:
		return generatedRules, fmt.Errorf("unsupported normalization strategy")
	}

	// 4. Generate tiers using economy data (Common Logic)
	tiered, err := data_generation.GenerateTiers(itemsToCheck, len(tierStyles), data_generation.TieringParameters{
		ValueWeight:              rg.economyWeights.Value,
		RarityWeight:             rg.economyWeights.Rarity,
		LeagueWeights:            rg.leagueWeights,
		NormStrategy:             normStrategy,
		ChaosOutlierPercentile:   0.95,
		MinListingsForPercentile: 20,
		ChasePotentialWeight:     rg.chasePotentialWeight,
	})
	if err != nil {
		return generatedRules, err
	}

	// 5. Sort tiers and construct rules (Common Logic)
	var sortedTiers []int
	for tier := range tiered {
		sortedTiers = append(sortedTiers, tier)
	}
	sort.Slice(sortedTiers, func(i, j int) bool {
		return sortedTiers[i] > sortedTiers[j]
	})

	for _, tier := range sortedTiers {
		tieredItems := tiered[tier]
		if len(tieredItems) == 0 {
			continue
		}

		style := tierStyles[tier-1]
		tieredItemsCondition := model2.Condition{
			Identifier: tieringConfiguration.TieredIdentifier,
			Operator:   "==",
			Value:      tieredItems,
		}

		rule := &model2.ParsedRule{
			Style:          style,
			Action:         model2.ShowRule,
			Conditions:     []model2.Condition{tieringConfiguration.InitialCondition, tieredItemsCondition},
			Variables:      variables,
			ValidBaseTypes: rg.validBaseTypes,
		}

		generatedRules = append(generatedRules, rg.compileParsedRule(rule, []model2.Condition{})...)
	}

	return generatedRules, nil
}

type AutomationGroup struct {
	MinStackSize *int
	Style        config.Style
	Tier         int
	BaseTypes    []string
	Rarity       *string
	Hide         bool
}

// GroupByProperties takes a slice of BaseTypeAutomationEntry and groups them
// by StyleID, Priority, and MinStackSize.
// The final result is a slice of
// AutomationGroup, sorted by Priority in ascending order.
//
//goland:noinspection t
func groupByProperties(entries []config.BaseTypeAutomationEntry, styleManager *StyleManager) ([]AutomationGroup, error) {
	if len(entries) == 0 {
		return []AutomationGroup{}, nil
	}

	type groupKey struct {
		StyleID      string
		MinStackSize int
		Rarity       *string
		Hide         bool
	}

	groupsMap := make(map[groupKey]*AutomationGroup)

	for _, entry := range entries {
		mssValue := -1
		if entry.MinStackSize != nil {
			mssValue = *entry.MinStackSize
		}
		style, err := styleManager.GetStyle(entry.Style)

		if err != nil {
			return nil, err
		}

		key := groupKey{
			StyleID:      style.Id,
			MinStackSize: mssValue,
			Rarity:       entry.Rarity,
			Hide:         entry.Hide,
		}

		if existingGroup, found := groupsMap[key]; found {
			existingGroup.BaseTypes = append(existingGroup.BaseTypes, entry.BaseType)
		} else {
			newGroup := &AutomationGroup{
				MinStackSize: entry.MinStackSize,
				Style:        *style,
				Tier:         entry.Priority,
				BaseTypes:    []string{entry.BaseType},
				Rarity:       entry.Rarity,
				Hide:         entry.Hide,
			}
			groupsMap[key] = newGroup
		}
	}

	groupedResult := make([]AutomationGroup, 0, len(groupsMap))
	for _, group := range groupsMap {
		groupedResult = append(groupedResult, *group)
	}

	sort.Slice(groupedResult, func(i, j int) bool {
		return groupedResult[i].Tier > groupedResult[j].Tier
	})

	return groupedResult, nil
}

//goland:noinspection t
func (rg *RuleGenerator) handleCSVMacro(variables *map[string][]string, parameters []*shared.ParseTree[symbols.LexingTokenType], sectionConditions []model2.Condition) ([][]string, error) {
	allGeneratedRules := make([][]string, 0)

	category := ""

	for _, parameter := range parameters {
		key, value := rg.getKeyAndValueFromParameter(parameter)

		switch key[1:] {
		case "category":
			category = value
		default:
			return allGeneratedRules, fmt.Errorf("unsupported parameter: " + key)
		}
	}

	if category == "" {
		return allGeneratedRules, fmt.Errorf("ensure you have 'category' specified")
	}

	toBeProcessed := make([]config.BaseTypeAutomationEntry, 0)

	for _, entry := range rg.baseTypeData {
		if entry.Category == category {
			toBeProcessed = append(toBeProcessed, entry)
		}
	}

	ruleGroups, err := groupByProperties(toBeProcessed, rg.styleManager)
	if err != nil {
		return allGeneratedRules, fmt.Errorf("unable to get rule groups: %w", err)
	}

	for _, ruleGroup := range ruleGroups {
		baseTypes := ruleGroup.BaseTypes
		minStackSize := ruleGroup.MinStackSize
		rarity := ruleGroup.Rarity
		style := ruleGroup.Style

		conditions := []model2.Condition{
			{
				Identifier: "@item_type",
				Operator:   "==",
				Value:      baseTypes,
			},
		}

		if minStackSize != nil {
			conditions = append(conditions, model2.Condition{
				Identifier: "@stack_size",
				Operator:   ">=",
				Value:      []string{fmt.Sprintf("%d", *minStackSize)},
			})
		}

		if rarity != nil {
			conditions = append(conditions, model2.Condition{
				Identifier: "@rarity",
				Operator:   "==",
				Value:      []string{*rarity},
			})
		}

		var ruleAction model2.RuleType
		if ruleGroup.Hide {
			ruleAction = model2.HideRule
		} else {
			ruleAction = model2.ShowRule
		}

		rule := &model2.ParsedRule{
			Style:          &style,
			Action:         ruleAction,
			Conditions:     conditions,
			Variables:      variables,
			ValidBaseTypes: rg.validBaseTypes,
		}

		allGeneratedRules = append(allGeneratedRules, rg.compileParsedRule(rule, sectionConditions)...)
	}

	return allGeneratedRules, nil
}

var conditionOrder = map[string]int{
	// Booleans
	"@quality":    1,
	"@corrupted":  2,
	"@fractured":  3,
	"@identified": 4,

	// Numeric
	"@base_armour":        5,
	"@base_evasion":       6,
	"@base_energy_shield": 7,
	"@base_ward":          8,
	"@stack_size":         9,
	"@height":             10,
	"@width":              11,
	"@area_level":         12,
	"@map_tier":           13,

	// Hash Set
	"@sockets":      14,
	"@socket_group": 15,

	// Arrays
	"@item_class": 16,
	"@item_type":  17,
	"@rarity":     18,

	// Mods
	"@has_explicit_mod": 19,
}

//goland:noinspection t
func (rg *RuleGenerator) compileParsedRule(rule *model2.ParsedRule, sectionConditions []model2.Condition) [][]string {
	var macroConditions []model2.Condition
	var standardRuleConditions []model2.Condition
	macros := []string{"@class_use"}

	for _, cond := range rule.Conditions {
		if slices.Contains(macros, cond.Identifier) {
			macroConditions = append(macroConditions, cond)
		} else {
			standardRuleConditions = append(standardRuleConditions, cond)
		}
	}

	ruleConditionsMap := make(map[string]model2.Condition, len(standardRuleConditions))
	for _, ruleCond := range standardRuleConditions {
		key := ruleCond.Identifier + ":" + ruleCond.Operator
		ruleConditionsMap[key] = ruleCond
	}

	finalStandardConditions := make([]model2.Condition, 0, len(standardRuleConditions)+len(sectionConditions))
	for _, sectionCond := range sectionConditions {
		key := sectionCond.Identifier + ":" + sectionCond.Operator
		if _, isOverridden := ruleConditionsMap[key]; !isOverridden {
			finalStandardConditions = append(finalStandardConditions, sectionCond)
		}
	}
	finalStandardConditions = append(finalStandardConditions, standardRuleConditions...)

	if len(macroConditions) == 0 {
		sort.Slice(finalStandardConditions, func(i, j int) bool {
			orderI, okI := conditionOrder[finalStandardConditions[i].Identifier]
			if !okI {
				orderI = 999
			}
			orderJ, okJ := conditionOrder[finalStandardConditions[j].Identifier]
			if !okJ {
				orderJ = 999
			}
			return orderI < orderJ
		})

		compiledFinalConditions := make([]string, len(finalStandardConditions))
		for i, cond := range finalStandardConditions {
			compiledFinalConditions[i] = cond.ConstructCompiledCondition(rule.Variables, rule.ValidBaseTypes)
		}

		if rule.Style == nil {
			panic(fmt.Errorf("rule style is nil, rule: %v", rule.Conditions))
		}

		finalRule := rg.ruleFactory.ConstructRule(rule.Action, *rule.Style, compiledFinalConditions)
		return [][]string{finalRule}
	}

	var allGeneratedRules [][]string
	for _, macro := range macroConditions {
		var generatedForMacro [][]string
		switch macro.Identifier {
		case "@class_use":
			generatedForMacro = rg.handleClassUseMacro(rule.Action, rule.Style, finalStandardConditions, macro, rule.Variables)
		default:
			panic("unknown macro: " + macro.Identifier)
		}
		allGeneratedRules = append(allGeneratedRules, generatedForMacro...)
	}
	return allGeneratedRules
}

//goland:noinspection t
func (rg *RuleGenerator) handleClassUseMacro(
	action model2.RuleType, style *config.Style, baseConditions []model2.Condition,
	macro model2.Condition, variables *map[string][]string,
) [][]string {
	generateRule := func(newCond model2.Condition) []string {
		finalConditions := make([]model2.Condition, 0, len(baseConditions)+1)
		finalConditions = append(finalConditions, baseConditions...)
		finalConditions = append(finalConditions, newCond)

		sort.Slice(finalConditions, func(i, j int) bool {
			orderI, okI := conditionOrder[finalConditions[i].Identifier]
			if !okI {
				orderI = 999
			}
			orderJ, okJ := conditionOrder[finalConditions[j].Identifier]
			if !okJ {
				orderJ = 999
			}
			return orderI < orderJ
		})

		compiledConditions := make([]string, len(finalConditions))
		for i, cond := range finalConditions {
			compiledConditions[i] = cond.ConstructCompiledCondition(variables, rg.validBaseTypes)
		}
		return rg.ruleFactory.ConstructRule(action, *style, compiledConditions)
	}

	var weaponClasses []string
	var armorClasses []string
	switch macro.Value[0] {
	case "true":
		weaponClasses = rg.build.AssociatedWeaponClasses()
		for _, item := range rg.armorBases {
			if rg.build.IsArmorAssociated(item) {
				armorClasses = append(armorClasses, item.GetBaseType())
			}
		}
	case "false":
		weaponClasses = rg.build.UnassociatedWeaponClasses()
		for _, item := range rg.armorBases {
			if !rg.build.IsArmorAssociated(item) {
				armorClasses = append(armorClasses, item.GetBaseType())
			}
		}
	default:
		panic(fmt.Sprintf("invalid value for @class_use: %s", macro.Value[0]))
	}

	weaponryCond := model2.Condition{Identifier: "@item_class", Operator: macro.Operator, Value: weaponClasses}
	weaponryRule := generateRule(weaponryCond)

	armorCond := model2.Condition{Identifier: "@item_type", Operator: macro.Operator, Value: armorClasses}
	armorRule := generateRule(armorCond)

	return [][]string{weaponryRule, armorRule}
}

// getDropLevel helper remains the same.
func getDropLevel(item *model.ItemBase) int {
	if item.DropLevel != nil {
		return *item.DropLevel
	}
	log.Printf("WARNING: Item '%s' has a nil DropLevel; defaulting to level 0.", item.Name)
	return 0
}
