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
	"strconv"
	"strings"
)

// RuleGenerator is the engine for compiling rules. It contains all complex game logic.
type RuleGenerator struct {
	ruleFactory           *RuleFactory
	styleManager          *StyleManager
	validBaseTypes        []string
	armorBases            []model.ItemBase
	weaponBases           []model.ItemBase
	flaskBases            []model.ItemBase
	economyCache          map[string][]data_generation.EconomySnapshot
	economyWeights        config.EconomyWeights
	leagueWeights         []config.LeagueWeights
	normalizationStrategy string
	chasePotentialWeight  float64
	valueHalfLifeDays     float64
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
	economyCache map[string][]data_generation.EconomySnapshot,
	economyWeights config.EconomyWeights,
	leagueWeights []config.LeagueWeights,
	normalizationStrategy string,
	chasePotentialWeight float64,
	valueHalfLifeDays float64,
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
		valueHalfLifeDays:     valueHalfLifeDays,
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
	case "divination_card_tiering":
		return rg.handleDivinationCardTiering(variables, parameters)
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

		return nil, fmt.Errorf("unknown variable: %s", parameterKey)
	}

	if styleString == "" {
		return nil, fmt.Errorf("no style parameter found")
	}

	style, err := rg.styleManager.GetStyle(styleString)
	if err != nil {
		return nil, fmt.Errorf("error extracting style: %w", err)
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

// handleDivinationCardTiering generates tiered rules for divination cards
// based on economy data, the same way handleUniqueTiering does for uniques.
func (rg *RuleGenerator) handleDivinationCardTiering(variables *map[string][]string, parameters []*shared.ParseTree[symbols.LexingTokenType]) ([][]string, error) {
	divinationConfig := TieringConfig{
		InitialCondition: model2.Condition{
			Identifier: "@item_class",
			Operator:   "==",
			Value:      []string{"Divination Card"},
		},
		ItemClassToFilter: "Divination Cards",
		TieredIdentifier:  "@item_type",
	}
	return rg.generateTieredRules(variables, parameters, divinationConfig)
}

func (rg *RuleGenerator) generateTieredRules(
	variables *map[string][]string,
	parameters []*shared.ParseTree[symbols.LexingTokenType],
	tieringConfiguration TieringConfig,
) ([][]string, error) {
	generatedRules := make([][]string, 0)

	// 1. Split parameters into ordered tier styles and optional quality
	// brackets (Common Logic). A "$quality_N" parameter (e.g. $quality_20)
	// doesn't occupy a tier slot - every tier gets combined with every
	// quality bracket (see emitTierRules), so quality is never shadowed by
	// economy tiering the way it used to be for skill_gem_tiering, and
	// tiering macros that don't pass any quality_ parameter (uniques,
	// divination cards) behave exactly as before.
	var tierStyles []*config.Style
	var qualityBrackets []qualityBracket
	var vaalStyle *config.Style
	for _, parameter := range parameters {
		key, value := rg.getKeyAndValueFromParameter(parameter)
		style, err := rg.styleManager.GetStyle(value)
		if err != nil {
			panic(err)
		}
		if threshold, ok := parseQualityKey(key); ok {
			qualityBrackets = append(qualityBrackets, qualityBracket{threshold: threshold, style: style})
			continue
		}
		if key == "$vaal" {
			vaalStyle = style
			continue
		}
		tierStyles = append(tierStyles, style)
	}
	// Highest threshold first, so e.g. a 25%-quality item matches ">= 20"
	// before it ever gets the chance to fall through to ">= 10".
	sort.Slice(qualityBrackets, func(i, j int) bool {
		return qualityBrackets[i].threshold > qualityBrackets[j].threshold
	})

	// 2. Handle the case with only one tier (Common Logic). There's no
	// enumerated item_type list to split by Vaal-ness here (the single style
	// covers the whole class unconditionally), so $vaal is a no-op in this
	// degenerate case.
	if len(tierStyles) == 1 {
		rules, err := rg.emitTierRules(tierStyles[0], []model2.Condition{tieringConfiguration.InitialCondition}, qualityBrackets, nil, variables)
		if err != nil {
			return generatedRules, err
		}
		return rules, nil
	}

	// 3. Filter items from the economy cache based on the provided class (Customizable Logic),
	// preserving each snapshot's timing so GenerateTiers can still compute trends.
	itemsToCheck := make(map[string][]data_generation.EconomySnapshot)
	for league, snapshots := range rg.economyCache {
		filteredSnapshots := make([]data_generation.EconomySnapshot, 0, len(snapshots))
		totalValid := 0
		for _, snapshot := range snapshots {
			validItems := make([]data_generation.EconomyCacheItem, 0)
			for _, item := range snapshot.Items {
				if item.Class != tieringConfiguration.ItemClassToFilter {
					continue
				}
				if !slices.Contains(rg.validBaseTypes, item.BaseType) {
					continue
				}
				validItems = append(validItems, item)
			}
			totalValid += len(validItems)
			filteredSnapshots = append(filteredSnapshots, data_generation.EconomySnapshot{FetchedAt: snapshot.FetchedAt, Items: validItems})
		}
		log.Printf("Valid %s for League %s: %d (across %d snapshot(s))\n", tieringConfiguration.ItemClassToFilter, league, totalValid, len(filteredSnapshots))
		itemsToCheck[league] = filteredSnapshots
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
		TrendWeight:              rg.economyWeights.Trend,
		LeagueWeights:            rg.leagueWeights,
		NormStrategy:             normStrategy,
		ChaosOutlierPercentile:   0.95,
		MinListingsForPercentile: 20,
		ChasePotentialWeight:     rg.chasePotentialWeight,
		ValueHalfLifeDays:        rg.valueHalfLifeDays,
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

		// Split this tier's items into Vaal and non-Vaal subsets so each can
		// get its own item_type condition - a plain style/quality-bracket
		// merge can't distinguish them (both subsets fall under the same
		// tier and the same InitialCondition), so the split has to happen at
		// the condition level, not the style level. Order between the two
		// doesn't matter for correctness (the item_type lists are disjoint),
		// but Vaal is emitted first for readability.
		vaalItems, otherItems := splitVaalItems(tieredItems)

		if vaalStyle != nil && len(vaalItems) > 0 {
			condition := model2.Condition{Identifier: tieringConfiguration.TieredIdentifier, Operator: "==", Value: vaalItems}
			rules, err := rg.emitTierRules(style, []model2.Condition{tieringConfiguration.InitialCondition, condition}, qualityBrackets, vaalStyle, variables)
			if err != nil {
				return generatedRules, err
			}
			generatedRules = append(generatedRules, rules...)
		}
		if vaalStyle == nil {
			otherItems = tieredItems
		}
		if len(otherItems) > 0 {
			condition := model2.Condition{Identifier: tieringConfiguration.TieredIdentifier, Operator: "==", Value: otherItems}
			rules, err := rg.emitTierRules(style, []model2.Condition{tieringConfiguration.InitialCondition, condition}, qualityBrackets, nil, variables)
			if err != nil {
				return generatedRules, err
			}
			generatedRules = append(generatedRules, rules...)
		}
	}

	return generatedRules, nil
}

// splitVaalItems partitions base type names into Vaal and non-Vaal subsets.
// Real Vaal skill/support gems are always named "Vaal <base gem name>", and
// this only ever runs against an already class-filtered (Class == "Gems")
// tiered item list, so a plain prefix check can't misfire on an unrelated
// "Vaal ..." item from another category (e.g. Vaal Orb, a currency, is never
// part of a gem tier's item_type list).
func splitVaalItems(items []string) (vaal, other []string) {
	for _, item := range items {
		if strings.HasPrefix(item, "Vaal ") {
			vaal = append(vaal, item)
		} else {
			other = append(other, item)
		}
	}
	return vaal, other
}

// qualityBracket pairs a minimum @quality threshold with the style a tiering
// macro should blend in for items that clear it.
type qualityBracket struct {
	threshold int
	style     *config.Style
}

// parseQualityKey reports whether key names a quality-bracket macro parameter
// (e.g. "$quality_20") and, if so, its minimum quality threshold.
func parseQualityKey(key string) (int, bool) {
	const prefix = "$quality_"
	if !strings.HasPrefix(key, prefix) {
		return 0, false
	}
	threshold, err := strconv.Atoi(strings.TrimPrefix(key, prefix))
	if err != nil {
		return 0, false
	}
	return threshold, true
}

// qualityOverrides decides which side wins when a tier style and a quality
// style both set the same property. The quality bracket gets the "extra
// signal" properties (border colour, alert sound, beam) added on top; the
// tier keeps the "primary identity" properties (text colour, background,
// font size, minimap) it would have had on its own. This way neither axis
// gets silently discarded: glancing at an item, its tier still says how
// economically valuable it is, and a distinct border/sound/beam says it also
// cleared a quality bracket.
func qualityOverrides(qualityStyle *config.Style) config.OverrideMap {
	return config.OverrideMap{
		"BorderColor": qualityStyle.Name,
		"DropSound":   qualityStyle.Name,
		"DropVolume":  qualityStyle.Name,
		"Beam.Color":  qualityStyle.Name,
		"Beam.Temp":   qualityStyle.Name,
	}
}

// emitTierRules produces one Show rule per quality bracket (highest
// threshold first, so e.g. a 25%-quality item matches ">= 20" before it can
// fall through to ">= 10"), each styled with tierStyle merged with that
// bracket's style (see qualityOverrides), followed by one final rule using
// the plain tierStyle with no quality requirement for anything that doesn't
// clear a bracket. With no quality brackets configured this emits exactly
// the single rule generateTieredRules always used to, so unique_tiering and
// divination_card_tiering (which never pass quality_ parameters) are
// unaffected.
// emitTierRules produces one Show rule per quality bracket plus one final
// plain rule, as described above. If vaalStyle is non-nil, it's merged in
// last - after quality - onto every rule this call produces, with
// vaalOverrides making it win any conflicting property. Applying it last
// means it takes priority over both the tier's own style and quality's
// contribution: a style merge only ever resolves a conflict in the "other"
// side's favour when the override map for *that* merge call says so, so
// whichever style is merged in last has the final say over its claimed
// properties. Callers are expected to have already restricted baseConditions
// to a Vaal-only or Vaal-free item_type subset (see splitVaalItems) - this
// function itself does not filter by name.
func (rg *RuleGenerator) emitTierRules(
	tierStyle *config.Style,
	baseConditions []model2.Condition,
	qualityBrackets []qualityBracket,
	vaalStyle *config.Style,
	variables *map[string][]string,
) ([][]string, error) {
	var generatedRules [][]string

	finalize := func(style *config.Style) (*config.Style, error) {
		if vaalStyle == nil {
			return style, nil
		}
		combined, err := style.MergeStyles(vaalStyle, vaalOverrides(vaalStyle))
		if err != nil {
			return nil, fmt.Errorf("combining style with Vaal indicator: %w", err)
		}
		return combined, nil
	}

	for _, bracket := range qualityBrackets {
		combinedStyle, err := tierStyle.MergeStyles(bracket.style, qualityOverrides(bracket.style))
		if err != nil {
			return nil, fmt.Errorf("combining tier style with quality bracket %d: %w", bracket.threshold, err)
		}
		combinedStyle, err = finalize(combinedStyle)
		if err != nil {
			return nil, err
		}
		conditions := append(append([]model2.Condition{}, baseConditions...), model2.Condition{
			Identifier: "@quality",
			Operator:   ">=",
			Value:      []string{strconv.Itoa(bracket.threshold)},
		})
		rule := &model2.ParsedRule{
			Style:          combinedStyle,
			Action:         model2.ShowRule,
			Conditions:     conditions,
			Variables:      variables,
			ValidBaseTypes: rg.validBaseTypes,
		}
		generatedRules = append(generatedRules, rg.compileParsedRule(rule, []model2.Condition{})...)
	}

	plainStyle, err := finalize(tierStyle)
	if err != nil {
		return nil, err
	}
	rule := &model2.ParsedRule{
		Style:          plainStyle,
		Action:         model2.ShowRule,
		Conditions:     baseConditions,
		Variables:      variables,
		ValidBaseTypes: rg.validBaseTypes,
	}
	generatedRules = append(generatedRules, rg.compileParsedRule(rule, []model2.Condition{})...)

	return generatedRules, nil
}

// vaalOverrides makes the Vaal indicator style win BorderColor whenever it's
// merged onto a style that already has one (from the tier or from a quality
// bracket) - see emitTierRules for why merge order, not this map, is what
// gives Vaal its priority over quality.
func vaalOverrides(vaalStyle *config.Style) config.OverrideMap {
	return config.OverrideMap{
		"BorderColor": vaalStyle.Name,
	}
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
			return allGeneratedRules, fmt.Errorf("unsupported parameter: %s", key)
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

// getDropLevel prefers verified drop data. Required level is only an estimate
// for bases absent from the wiki; PoB omits req.level when it is 1. Keep this
// helper free of logging because sorting and progression call it repeatedly.
func getDropLevel(item *model.ItemBase) int {
	if item.DropLevel != nil && *item.DropLevel > 0 {
		return *item.DropLevel
	}
	if requiredLevel := item.Req["level"]; requiredLevel > 0 {
		return requiredLevel
	}
	return 1
}
