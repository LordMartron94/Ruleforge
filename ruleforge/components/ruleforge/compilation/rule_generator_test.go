package compilation

import (
	"bytes"
	"log"
	"strings"
	"testing"
	"time"

	lexing "github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/lexing/shared"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/parsing/shared"
	model2 "github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/compilation/model"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/config"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/data_generation"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/data_generation/model"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/rules/symbols"
)

func u8(v uint8) *uint8     { return &v }
func strp(v string) *string { return &v }

func TestGetDropLevelPrefersEnrichedDropLevel(t *testing.T) {
	enriched := 12
	item := model.ItemBase{
		DropLevel: &enriched,
		Req:       map[string]int{"level": 8},
	}

	if got := getDropLevel(&item); got != enriched {
		t.Fatalf("getDropLevel() = %d, want enriched level %d", got, enriched)
	}
}

func TestDropLevelFallbackDoesNotHideStarterGearOrSpamLogs(t *testing.T) {
	var output bytes.Buffer
	previous := log.Writer()
	log.SetOutput(&output)
	t.Cleanup(func() { log.SetOutput(previous) })
	items := []*model.ItemBase{
		{Name: "Iron Greaves", Req: map[string]int{"str": 8}},
		{Name: "Small Life Flask"},
	}
	for i := 0; i < 20; i++ {
		buckets, outdated := groupProgressionBuckets(items, 1, 10)
		if len(outdated) != 0 || len(buckets) != 1 || buckets[0].startLevel != 1 || len(buckets[0].items) != 2 {
			t.Fatalf("starter gear classified as outdated: buckets=%+v outdated=%v", buckets, outdated)
		}
	}
	if output.Len() != 0 {
		t.Fatalf("repeated lookups logged warnings: %s", output.String())
	}
}

func TestDropLevelPreservesActualDropInsteadOfRequiredLevel(t *testing.T) {
	for _, test := range []struct {
		name  string
		level int
	}{
		{"Simple Robe", 3},
		{"Fishscale Gauntlets", 4},
		{"Golden Obi", 12},
	} {
		item := model.ItemBase{Name: test.name, DropLevel: &test.level}
		buckets, outdated := groupProgressionBuckets([]*model.ItemBase{&item}, 1, 20)
		if len(outdated) != 0 || len(buckets) != 1 || buckets[0].startLevel != test.level {
			t.Fatalf("%s lost its actual drop level: buckets=%+v outdated=%v", test.name, buckets, outdated)
		}
	}
}

func TestGetDropLevelFallsBackToRequiredLevel(t *testing.T) {
	item := model.ItemBase{Req: map[string]int{"level": 8}}

	if got := getDropLevel(&item); got != 8 {
		t.Fatalf("getDropLevel() = %d, want required level 8", got)
	}
}

func TestParseQualityKey(t *testing.T) {
	tests := []struct {
		key           string
		wantThreshold int
		wantOK        bool
	}{
		{"$quality_20", 20, true},
		{"$quality_1", 1, true},
		{"$quality_0", 0, true},
		{"$tier_1", 0, false},
		{"$quality_", 0, false},
		{"$quality_abc", 0, false},
	}
	for _, test := range tests {
		threshold, ok := parseQualityKey(test.key)
		if ok != test.wantOK || threshold != test.wantThreshold {
			t.Errorf("parseQualityKey(%q) = (%d, %v), want (%d, %v)", test.key, threshold, ok, test.wantThreshold, test.wantOK)
		}
	}
}

func TestGemPropertiesPreservePriorityChannelsAndBorderPrecedence(t *testing.T) {
	quality := &config.Style{
		Name: "Quality20", BorderColor: &config.Color{Red: u8(200)},
		TextColor: &config.Color{Red: u8(255)}, FontSize: intp(45),
		BackgroundColor: &config.Color{Blue: u8(255)},
		Minimap:         &config.Minimap{Color: strp("Red"), Size: intp(0), Shape: strp("Star")},
		Beam:            &config.Beam{Color: strp("Red")}, DropSound: strp("quality.mp3"), DropVolume: intp(300),
	}
	vaal := quality.Clone()
	vaal.Name = "Vaal"
	vaal.BorderColor = &config.Color{Green: u8(200)}
	for _, tier := range []*config.Style{
		{Name: "QuietTier", TextColor: &config.Color{Blue: u8(100)}, FontSize: intp(28)},
		{Name: "ValuableTier", TextColor: &config.Color{Red: u8(100)}, FontSize: intp(40),
			Beam: &config.Beam{Color: strp("Orange")}, DropSound: strp("tier.mp3"), DropVolume: intp(200)},
		{Name: "RadiantTier", BorderColor: &config.Color{Blue: u8(50)}, FontSize: intp(45)},
	} {
		t.Run(tier.Name, func(t *testing.T) {
			got := gemPropertyStyle(tier, quality, vaal)
			wantBorder := vaal.BorderColor
			if tier.BorderColor != nil {
				wantBorder = tier.BorderColor
			}
			if !got.BorderColor.IsEqual(wantBorder) {
				t.Fatal("tier > Vaal > quality border precedence lost")
			}
			got.BorderColor = tier.BorderColor.Clone()
			if !got.IsEqual(tier) {
				t.Fatal("a property annotation changed a priority channel")
			}
			if !gemPropertyStyle(tier, quality, nil).BorderColor.IsEqual(func() *config.Color {
				if tier.BorderColor != nil {
					return tier.BorderColor
				}
				return quality.BorderColor
			}()) {
				t.Fatal("quality border missing")
			}
		})
	}
}

func TestEmitTierRulesCombinesStylesAndOrdersBracketsByThreshold(t *testing.T) {
	rg := &RuleGenerator{ruleFactory: &RuleFactory{}}

	tierStyle := &config.Style{
		Name:      "Tier3",
		TextColor: &config.Color{Red: u8(10), Green: u8(20), Blue: u8(30), Alpha: u8(255)},
		FontSize:  intp(30),
	}
	quality20 := &config.Style{
		Name:        "Quality20",
		BorderColor: &config.Color{Red: u8(200), Green: u8(0), Blue: u8(0), Alpha: u8(255)},
		DropSound:   strp("q20.mp3"),
		DropVolume:  intp(150),
	}
	quality10 := &config.Style{
		Name:        "Quality10",
		BorderColor: &config.Color{Red: u8(100), Green: u8(0), Blue: u8(0), Alpha: u8(255)},
	}

	baseConditions := []model2.Condition{{Identifier: "@item_class", Operator: "==", Value: []string{"Skill Gems"}}}
	brackets := []qualityBracket{{threshold: 20, style: quality20}, {threshold: 10, style: quality10}}

	rules, err := rg.emitTierRules(tierStyle, baseConditions, brackets, nil, nil)
	if err != nil {
		t.Fatalf("emitTierRules returned an error: %v", err)
	}
	if len(rules) != 3 {
		t.Fatalf("expected 3 rules (2 quality brackets + 1 plain), got %d", len(rules))
	}

	joined := make([]string, len(rules))
	for i, rule := range rules {
		joined[i] = strings.Join(rule, "\n")
	}

	// Highest threshold must be checked first.
	if !strings.Contains(joined[0], `Quality >= "20"`) {
		t.Fatalf("expected the first rule to check quality >= 20, got:\n%s", joined[0])
	}
	if !strings.Contains(joined[1], `Quality >= "10"`) {
		t.Fatalf("expected the second rule to check quality >= 10, got:\n%s", joined[1])
	}
	if strings.Contains(joined[2], "Quality") {
		t.Fatalf("expected the final plain rule to have no quality condition, got:\n%s", joined[2])
	}

	// The combined style must carry the tier's own properties (not
	// overridden) alongside the quality bracket's "extra signal" properties.
	if !strings.Contains(joined[0], "SetTextColor 10 20 30 255") {
		t.Fatalf("expected the tier's TextColor to survive combination with quality, got:\n%s", joined[0])
	}
	if !strings.Contains(joined[0], "SetFontSize 30") {
		t.Fatalf("expected the tier's FontSize to survive combination with quality, got:\n%s", joined[0])
	}
	if !strings.Contains(joined[0], "SetBorderColor 200 0 0 255") {
		t.Fatalf("expected quality's BorderColor to be added on top of the tier style, got:\n%s", joined[0])
	}
	if strings.Contains(joined[0], "CustomAlertSound") {
		t.Fatalf("quality added a sound to a tier without one:\n%s", joined[0])
	}

	// The plain fallback rule must be the unmodified tier style: no border.
	if strings.Contains(joined[2], "SetBorderColor") {
		t.Fatalf("expected the plain rule to have no border colour from any quality bracket, got:\n%s", joined[2])
	}
}

func TestEmitTierRulesWithNoQualityBracketsMatchesPreviousBehavior(t *testing.T) {
	rg := &RuleGenerator{ruleFactory: &RuleFactory{}}
	tierStyle := &config.Style{Name: "T1", TextColor: &config.Color{Red: u8(1), Green: u8(2), Blue: u8(3), Alpha: u8(255)}}
	baseConditions := []model2.Condition{{Identifier: "@item_class", Operator: "==", Value: []string{"Uniques"}}}

	rules, err := rg.emitTierRules(tierStyle, baseConditions, nil, nil, nil)
	if err != nil {
		t.Fatalf("emitTierRules returned an error: %v", err)
	}
	if len(rules) != 1 {
		t.Fatalf("expected exactly 1 rule when no quality brackets are configured (unique/divination-card tiering), got %d", len(rules))
	}
}

func intp(v int) *int { return &v }

func TestSplitVaalItems(t *testing.T) {
	vaal, other := splitVaalItems([]string{"Vaal Cyclone", "Cyclone", "Vaal Spectral Throw", "Spectral Throw", "Empower Support"})
	wantVaal := []string{"Vaal Cyclone", "Vaal Spectral Throw"}
	wantOther := []string{"Cyclone", "Spectral Throw", "Empower Support"}
	if !equalSlices(vaal, wantVaal) {
		t.Fatalf("vaal = %v, want %v", vaal, wantVaal)
	}
	if !equalSlices(other, wantOther) {
		t.Fatalf("other = %v, want %v", other, wantOther)
	}
}

func equalSlices(a, b []string) bool {
	if len(a) != len(b) {
		return false
	}
	for i := range a {
		if a[i] != b[i] {
			return false
		}
	}
	return true
}

func TestEmitTierRulesVaalTakesPriorityOverQualityForBorderColorOnly(t *testing.T) {
	rg := &RuleGenerator{ruleFactory: &RuleFactory{}}

	tierStyle := &config.Style{Name: "Tier3", TextColor: &config.Color{Red: u8(10), Green: u8(20), Blue: u8(30), Alpha: u8(255)}}
	quality := &config.Style{
		Name:        "Quality20",
		BorderColor: &config.Color{Red: u8(200), Green: u8(0), Blue: u8(0), Alpha: u8(255)},
		DropSound:   strp("q20.mp3"),
		DropVolume:  intp(150),
	}
	vaal := &config.Style{
		Name:        "VaalIndicator",
		BorderColor: &config.Color{Red: u8(0), Green: u8(255), Blue: u8(0), Alpha: u8(255)},
	}

	baseConditions := []model2.Condition{{Identifier: "@item_class", Operator: "==", Value: []string{"Skill Gems"}}}
	brackets := []qualityBracket{{threshold: 20, style: quality}}

	rules, err := rg.emitTierRules(tierStyle, baseConditions, brackets, vaal, nil)
	if err != nil {
		t.Fatalf("emitTierRules returned an error: %v", err)
	}
	if len(rules) != 2 {
		t.Fatalf("expected 2 rules (1 quality bracket + 1 plain), got %d", len(rules))
	}

	qualityRule := strings.Join(rules[0], "\n")
	plainRule := strings.Join(rules[1], "\n")

	// Vaal must win BorderColor over quality, since it's merged in last.
	if !strings.Contains(qualityRule, "SetBorderColor 0 255 0 255") {
		t.Fatalf("expected Vaal's border colour to win over quality's, got:\n%s", qualityRule)
	}
	if strings.Contains(qualityRule, "SetBorderColor 200 0 0 255") {
		t.Fatalf("quality's border colour leaked through despite Vaal's priority, got:\n%s", qualityRule)
	}
	// Quality cannot claim sound even when the tier has none.
	if strings.Contains(qualityRule, "CustomAlertSound") {
		t.Fatalf("quality added a sound to the tier:\n%s", qualityRule)
	}
	// Vaal must also apply to the plain (no-quality) rule.
	if !strings.Contains(plainRule, "SetBorderColor 0 255 0 255") {
		t.Fatalf("expected the plain rule to also carry Vaal's border colour, got:\n%s", plainRule)
	}
}

func TestEmitTierRulesNilVaalStyleLeavesQualityUnaffected(t *testing.T) {
	rg := &RuleGenerator{ruleFactory: &RuleFactory{}}
	tierStyle := &config.Style{Name: "Tier3", TextColor: &config.Color{Red: u8(1), Green: u8(2), Blue: u8(3), Alpha: u8(255)}}
	quality := &config.Style{Name: "Quality20", BorderColor: &config.Color{Red: u8(200), Green: u8(0), Blue: u8(0), Alpha: u8(255)}}
	baseConditions := []model2.Condition{{Identifier: "@item_class", Operator: "==", Value: []string{"Skill Gems"}}}
	brackets := []qualityBracket{{threshold: 20, style: quality}}

	rules, err := rg.emitTierRules(tierStyle, baseConditions, brackets, nil, nil)
	if err != nil {
		t.Fatalf("emitTierRules returned an error: %v", err)
	}
	if !strings.Contains(strings.Join(rules[0], "\n"), "SetBorderColor 200 0 0 255") {
		t.Fatalf("expected quality's own border colour with no Vaal style given, got:\n%s", strings.Join(rules[0], "\n"))
	}
}

func TestGroupByPropertiesMergesRowsWithTheSameRarity(t *testing.T) {
	normal, magic, normalAgain := "Normal", "Magic", "Normal"
	manager := &StyleManager{styles: map[string]config.Style{"Final/Crafting/Normal": {Id: "n"}, "Final/Crafting/Magic": {Id: "m"}}}
	entries := []config.BaseTypeAutomationEntry{
		{BaseType: "Coral Ring", Style: "Final/Crafting/Normal", Priority: 5, Rarity: &normal},
		{BaseType: "Coral Ring", Style: "Final/Crafting/Magic", Priority: 5, Rarity: &magic},
		{BaseType: "Two-Stone Ring", Style: "Final/Crafting/Normal", Priority: 5, Rarity: &normalAgain},
	}
	groups, err := groupByProperties(entries, manager)
	if err != nil {
		t.Fatal(err)
	}
	if len(groups) != 2 {
		t.Fatalf("expected 2 groups (normal, magic), got %d", len(groups))
	}
	if got := groups[0].BaseTypes; len(got) != 2 || got[0] != "Coral Ring" || got[1] != "Two-Stone Ring" {
		t.Errorf("normal group keeps CSV order and merges both rings, got %v", got)
	}
}

func tieringParameter(key, style string) *shared.ParseTree[symbols.LexingTokenType] {
	leaf := func(value string) *shared.ParseTree[symbols.LexingTokenType] {
		return &shared.ParseTree[symbols.LexingTokenType]{Token: &lexing.Token[symbols.LexingTokenType]{Value: []byte(value)}}
	}
	return &shared.ParseTree[symbols.LexingTokenType]{Children: []*shared.ParseTree[symbols.LexingTokenType]{leaf("->"), leaf(key), leaf("=>"), leaf(style)}}
}

func tattooGenerator(items []data_generation.EconomyCacheItem) *RuleGenerator {
	var names []string
	for _, item := range items {
		names = append(names, item.BaseType)
	}
	return &RuleGenerator{
		ruleFactory: &RuleFactory{},
		styleManager: &StyleManager{styles: map[string]config.Style{
			"T1": {Id: "t1", FontSize: intp(45)}, "T2": {Id: "t2", FontSize: intp(40)}, "Unpriced": {Id: "unpriced", FontSize: intp(36)},
		}},
		validBaseTypes:        names,
		economyCache:          map[string][]data_generation.EconomySnapshot{"Allflame": {{FetchedAt: time.Unix(1, 0), Items: items}}},
		economyWeights:        config.EconomyWeights{Value: 1},
		leagueWeights:         []config.LeagueWeights{{League: "Allflame", Weight: 1}},
		normalizationStrategy: "Global",
	}
}

var tattooParameters = []*shared.ParseTree[symbols.LexingTokenType]{
	tieringParameter("$tier_1", "T1"), tieringParameter("$tier_2", "T2"), tieringParameter("$fallback", "Unpriced"),
}

func TestTattooTieringEndsWithAFallbackForUnpricedTattoos(t *testing.T) {
	rg := tattooGenerator([]data_generation.EconomyCacheItem{
		{Class: "Tattoos", Name: "Tattoo of the Arohongui Shaman", BaseType: "Tattoo of the Arohongui Shaman", ChaosValue: 700, Count: 18, ListingCount: 18},
		{Class: "Tattoos", Name: "Tattoo of the Valako Shieldbearer", BaseType: "Tattoo of the Valako Shieldbearer", ChaosValue: 500, Count: 10, ListingCount: 10},
		{Class: "Tattoos", Name: "Tattoo of the Tasalio Shaman", BaseType: "Tattoo of the Tasalio Shaman", ChaosValue: 0.3, Count: 38, ListingCount: 38},
		{Class: "Divination Cards", Name: "The Doctor", BaseType: "The Doctor", ChaosValue: 900, Count: 20, ListingCount: 20},
	})
	rules, err := rg.handleTattooTiering(&map[string][]string{}, tattooParameters)
	if err != nil {
		t.Fatal(err)
	}
	if len(rules) != 3 {
		t.Fatalf("expected two tier rules and the fallback, got %d rules", len(rules))
	}
	for _, rule := range rules[:2] {
		joined := strings.Join(rule, "\n")
		if !strings.Contains(joined, `Class == "Stackable Currency"`) || !strings.Contains(joined, `BaseType == "Tattoo of the`) {
			t.Fatalf("tier rule is not limited to priced tattoos:\n%s", joined)
		}
		if strings.Contains(joined, "The Doctor") {
			t.Fatalf("a divination card was tiered as a tattoo:\n%s", joined)
		}
	}
	fallback := strings.Join(rules[2], "\n")
	if !strings.Contains(fallback, `Class == "Stackable Currency"`) || !strings.Contains(fallback, `BaseType "Tattoo"`) || !strings.Contains(fallback, "SetFontSize 36") {
		t.Fatalf("expected the last rule to catch every other tattoo in the fallback style, got:\n%s", fallback)
	}
}

func TestTattooTieringWithoutPricesUsesOnlyTheFallback(t *testing.T) {
	rg := tattooGenerator([]data_generation.EconomyCacheItem{
		{Class: "Divination Cards", Name: "The Doctor", BaseType: "The Doctor", ChaosValue: 900, Count: 20, ListingCount: 20},
	})
	rules, err := rg.handleTattooTiering(&map[string][]string{}, tattooParameters)
	if err != nil {
		t.Fatalf("a kind without recorded prices must not fail the compile: %v", err)
	}
	if len(rules) != 1 {
		t.Fatalf("expected only the fallback rule, got %d rules", len(rules))
	}
	joined := strings.Join(rules[0], "\n")
	if !strings.Contains(joined, `BaseType "Tattoo"`) || !strings.Contains(joined, "SetFontSize 36") {
		t.Fatalf("expected the fallback rule for every tattoo, got:\n%s", joined)
	}
}

func TestEveryMacroHasASpecAndAHandler(t *testing.T) {
	specs := map[string]bool{}
	for _, spec := range MacroSpecs {
		if specs[spec.Name] {
			t.Errorf("macro %s is described twice", spec.Name)
		}
		specs[spec.Name] = true
		if _, ok := macroHandlers[spec.Name]; !ok {
			t.Errorf("macro %s is described but not compiled", spec.Name)
		}
	}
	for _, name := range MacroNames() {
		if !specs[name] {
			t.Errorf("macro %s has no MacroSpec, so the Studio cannot offer it", name)
		}
	}
}

func TestMacroRulesKeepTheirSectionsConditions(t *testing.T) {
	rg := &RuleGenerator{ruleFactory: &RuleFactory{}}
	rg.sectionConditions = []model2.Condition{{Identifier: "@area_level", Operator: "<", Value: []string{"73"}}}
	rules, err := rg.emitTierRules(&config.Style{Name: "T1"}, []model2.Condition{{Identifier: "@item_class", Operator: "==", Value: []string{"Life Flasks"}}}, nil, nil, nil)
	if err != nil {
		t.Fatal(err)
	}
	if joined := strings.Join(rules[0], "\n"); !strings.Contains(joined, `AreaLevel < "73"`) || !strings.Contains(joined, `Class == "Life Flasks"`) {
		t.Fatalf("a macro's rule lost its section's condition:\n%s", joined)
	}
}

func TestSectionClassUseNarrowsABaseTypeList(t *testing.T) {
	bases := []string{"Legion Sword", "Imperial Claw", "Astral Plate", "Vaal Regalia", "Two-Stone Ring"}
	rg := &RuleGenerator{
		ruleFactory:    &RuleFactory{},
		validBaseTypes: bases,
		build:          &Build{Preset: EquipmentPreset{WeaponClasses: []WeaponClass{OneHandedSword}, ArmorTypes: []ArmorType{Armor}}},
		weaponBases:    []model.ItemBase{{Name: "Legion Sword", Type: "One Handed Sword"}, {Name: "Imperial Claw", Type: "Claw"}},
		armorBases:     []model.ItemBase{{Name: "Astral Plate", Type: "Body Armour", SubType: "Armour"}, {Name: "Vaal Regalia", Type: "Body Armour", SubType: "Energy Shield"}},
	}
	section := []model2.Condition{
		{Identifier: "@class_use", Operator: "==", Value: []string{"true"}},
		{Identifier: "@area_level", Operator: ">=", Value: []string{"68"}},
	}
	compile := func(conditions ...model2.Condition) string {
		rule := &model2.ParsedRule{Style: &config.Style{Name: "Base"}, Action: model2.ShowRule, ValidBaseTypes: bases,
			Conditions: append([]model2.Condition{{Identifier: "@item_type", Operator: "==", Value: bases}}, conditions...)}
		var compiled []string
		for _, block := range rg.compileParsedRule(rule, section) {
			compiled = append(compiled, strings.Join(block, "\n"))
		}
		return strings.Join(compiled, "\n---\n")
	}

	if got := compile(); !strings.Contains(got, `BaseType == "Legion Sword" "Astral Plate"`) || !strings.Contains(got, `AreaLevel >= "68"`) ||
		strings.Contains(got, "Class") || strings.Contains(got, "---") {
		t.Fatalf("the section's class use did not narrow the list to one rule of the build's bases:\n%s", got)
	}
	if got := compile(model2.Condition{Identifier: "@class_use", Operator: "==", Value: []string{"false"}}); !strings.Contains(got, `BaseType == "Imperial Claw" "Vaal Regalia"`) {
		t.Fatalf("the rule's own class use did not win over the section's:\n%s", got)
	}
	rg.build.Preset.WeaponClasses, rg.build.Preset.ArmorTypes = nil, nil
	if got := compile(); got != "" {
		t.Fatalf("a list without any of the build's bases still produced a rule:\n%s", got)
	}
}

func TestRapiersAreThrustingSwords(t *testing.T) {
	build := &Build{Preset: EquipmentPreset{WeaponClasses: []WeaponClass{OneHandedSword}}}
	if build.IsWeaponAssociated(model.ItemBase{Name: "Jewelled Foil", Type: "One Handed Sword", SubType: "Thrusting"}) {
		t.Error("a one-hand sword build counts rapiers as its weapons")
	}
	if !build.IsWeaponAssociated(model.ItemBase{Name: "Legion Sword", Type: "One Handed Sword"}) {
		t.Error("a one-hand sword build does not count its swords")
	}
}

func TestCurrencyTierRulesPutBiggerStacksFirstAndHideTheBottomTier(t *testing.T) {
	rg := &RuleGenerator{ruleFactory: &RuleFactory{}, validBaseTypes: []string{"Chaos Orb", "Orb of Alteration", "Orb of Transmutation"}}
	rg.sectionConditions = []model2.Condition{{Identifier: "@area_level", Operator: ">=", Value: []string{"68"}}}
	tiers := []data_generation.CurrencyTier{
		{BaseType: "Chaos Orb", Tier: 1},
		{BaseType: "Orb of Alteration", Tier: 3, Stacks: []data_generation.StackTier{{MinStack: 3, Tier: 2}, {MinStack: 10, Tier: 1}}},
		{BaseType: "Orb of Transmutation", Tier: 3},
	}
	styles := []*config.Style{{Name: "T1"}, {Name: "T2"}}
	var blocks []string
	for _, rule := range rg.currencyTierRules(tiers, styles, &config.Style{Name: "Hidden"}, nil) {
		blocks = append(blocks, strings.Join(rule, "\n"))
	}
	want := []struct{ action, stack, baseTypes string }{
		{"Show", `StackSize >= "10"`, `"Orb of Alteration"`},
		{"Show", `StackSize >= "3"`, `"Orb of Alteration"`},
		{"Show", "", `"Chaos Orb"`},
		{"Hide", "", `"Orb of Alteration" "Orb of Transmutation"`},
	}
	if len(blocks) != len(want) {
		t.Fatalf("got %d rules, want %d:\n%s", len(blocks), len(want), strings.Join(blocks, "\n---\n"))
	}
	for i, w := range want {
		block := blocks[i]
		if !strings.HasPrefix(strings.TrimSpace(block), w.action) || !strings.Contains(block, w.baseTypes) || !strings.Contains(block, `AreaLevel >= "68"`) ||
			(w.stack != "" && !strings.Contains(block, w.stack)) || (w.stack == "" && strings.Contains(block, "StackSize")) {
			t.Fatalf("rule %d is not %s %s %s:\n%s", i, w.action, w.stack, w.baseTypes, block)
		}
	}
}
