package compilation

import (
	"bytes"
	"log"
	"strings"
	"testing"

	model2 "github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/compilation/model"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/config"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/data_generation/model"
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
