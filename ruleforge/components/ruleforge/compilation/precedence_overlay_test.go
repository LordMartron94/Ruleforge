package compilation

import (
	"strings"
	"testing"

	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/compilation/model"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/config"
)

func overlayColor(r, g, b uint8, name string) *config.Color {
	return &config.Color{Red: u8(r), Green: u8(g), Blue: u8(b), Alpha: u8(255), Var: name}
}

func maxRolledStyle() config.Style {
	return config.Style{
		Name:        "Final/Equipment/MaxRolled",
		TextColor:   overlayColor(1, 1, 1, "t5"),
		BorderColor: overlayColor(2, 2, 2, "bd-max-rolled"),
		FontSize:    intp(33),
		DropSound:   strp("click.mp3"),
		DropVolume:  intp(120),
		Minimap:     &config.Minimap{Size: intp(2), Color: strp("Cyan"), Shape: strp("Triangle")},
	}
}

func overlayFactory() *RuleFactory {
	factory := &RuleFactory{}
	factory.SetSource(`WHERE @socket_group >= "3"`)
	factory.SetMaxRollPrecedence(NewMaxRollPrecedence(maxRolledStyle(), []string{"Astral Plate", "Light Brigandine"}))
	return factory
}

// blocks splits constructed output into blocks of lines.
func blocks(lines []string) [][]string {
	var out [][]string
	for _, line := range lines {
		if line == "Show" || line == "Hide" {
			out = append(out, []string{line})
		} else if len(out) > 0 && line != "" {
			out[len(out)-1] = append(out[len(out)-1], strings.TrimSpace(line))
		}
	}
	return out
}

func has(block []string, line string) bool {
	for _, l := range block {
		if strings.TrimSpace(l) == strings.TrimSpace(line) {
			return true
		}
	}
	return false
}

func TestMaxRollCopyPrecedesShownLinkRuleAndTakesTheHigherTier(t *testing.T) {
	link := config.Style{
		Name:            "Final/Special/Sockets/3-link",
		TextColor:       overlayColor(9, 9, 9, "t10"),
		BorderColor:     overlayColor(8, 8, 8, "bd-3-link"),
		BackgroundColor: overlayColor(7, 7, 7, "bg-equipment"),
		FontSize:        intp(22),
		Minimap:         &config.Minimap{Size: intp(2), Color: strp("Grey"), Shape: strp("Triangle")},
	}
	factory := overlayFactory()
	out := blocks(factory.ConstructRule(model.ShowRule, link, []string{`AreaLevel <= "24" `, `SocketGroup >= "3" `}))
	if len(out) != 2 {
		t.Fatalf("expected the max-roll copy and the rule, got %d blocks: %v", len(out), out)
	}
	copyBlock, rule := out[0], out[1]
	for _, want := range []string{`AreaLevel <= "24"`, `SocketGroup >= "3"`, `Class == "Body Armours" "Boots" "Gloves" "Helmets" "Shields"`, `BaseDefencePercentile == "100"`,
		"SetTextColor 1 1 1 255", "SetBorderColor 2 2 2 255", "SetBackgroundColor 7 7 7 255", "SetFontSize 33", "MinimapIcon 2 Cyan Triangle"} {
		if !has(copyBlock, want) {
			t.Errorf("max-roll copy is missing %q:\n%s", want, strings.Join(copyBlock, "\n"))
		}
	}
	if has(rule, `BaseDefencePercentile == "100"`) || !has(rule, "SetBorderColor 8 8 8 255") {
		t.Errorf("the original rule must follow unchanged:\n%s", strings.Join(rule, "\n"))
	}
	records := factory.Records()
	if len(records) != 2 || records[0].Source != `Max-roll precedence over: WHERE @socket_group >= "3"` || records[0].Style.BorderColor.Var != "bd-max-rolled" {
		t.Errorf("records: %+v", records)
	}
}

func TestMaxRollPrecedenceNeverShowsAnythingNew(t *testing.T) {
	style := config.Style{Name: "x", FontSize: intp(22)}
	cases := map[string]struct {
		action     model.RuleType
		conditions []string
	}{
		"hide rule":          {model.HideRule, []string{`Rarity == "Normal" `}},
		"unique only":        {model.ShowRule, []string{`Rarity == "Unique" `}},
		"jewellery class":    {model.ShowRule, []string{`Class == "Rings" "Amulets" `}},
		"no armour bases":    {model.ShowRule, []string{`BaseType == "Coral Ring" `}},
		"already max-rolled": {model.ShowRule, []string{`BaseDefencePercentile == "100" `}},
	}
	for name, c := range cases {
		out := blocks(overlayFactory().ConstructRule(c.action, style, c.conditions))
		if len(out) != 1 {
			t.Errorf("%s: expected only the original block, got %d", name, len(out))
		}
	}
}

func TestMaxRollCopyNarrowsListsToArmour(t *testing.T) {
	style := config.Style{Name: "x", FontSize: intp(22)}
	out := blocks(overlayFactory().ConstructRule(model.ShowRule, style, []string{`Class == "Rings" "Body Armours" `, `BaseType == "Coral Ring" "Astral Plate" `}))
	if len(out) != 2 {
		t.Fatalf("expected a copy, got %d blocks", len(out))
	}
	if !has(out[0], `Class == "Body Armours"`) || !has(out[0], `BaseType == "Astral Plate"`) {
		t.Errorf("lists not narrowed to armour:\n%s", strings.Join(out[0], "\n"))
	}
}

func TestMaxRollCopyKeepsATiedRulesTierAndBeam(t *testing.T) {
	crafting := config.Style{
		Name:            "Final/Crafting/Normal",
		TextColor:       overlayColor(5, 5, 5, "t5"),
		BorderColor:     overlayColor(6, 6, 6, "bd-crafting"),
		BackgroundColor: overlayColor(4, 4, 4, "bg-crafting"),
		FontSize:        intp(33),
		Beam:            &config.Beam{Color: strp("Green"), Temp: func() *bool { v := true; return &v }()},
		Minimap:         &config.Minimap{Size: intp(2), Color: strp("Green"), Shape: strp("Moon")},
	}
	out := blocks(overlayFactory().ConstructRule(model.ShowRule, crafting, []string{`BaseType == "Astral Plate" `, `Rarity == "Normal" `}))
	copyBlock := out[0]
	for _, want := range []string{"SetBorderColor 2 2 2 255", "SetBackgroundColor 4 4 4 255", "SetTextColor 5 5 5 255", "PlayEffect Green Temp", "MinimapIcon 2 Green Moon"} {
		if !has(copyBlock, want) {
			t.Errorf("tied copy should keep the crafting look but take the max-roll border; missing %q:\n%s", want, strings.Join(copyBlock, "\n"))
		}
	}
}
