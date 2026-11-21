package compilation

import (
	"fmt"
	"regexp"
	"strconv"
	"strings"

	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/config"
)

// StudioBlock is one block of a compiled filter, described for the Ruleforge Studio:
// where it is, what produced it, what it matches and how it looks.
type StudioBlock struct {
	Line       int               `json:"line"`
	Action     string            `json:"action"`
	Section    string            `json:"section"`
	Source     string            `json:"source,omitempty"`
	Style      string            `json:"style,omitempty"`
	Conditions []StudioCondition `json:"conditions"`
	Look       StudioLook        `json:"look"`
}

// StudioCondition is one filter condition such as `BaseType == "Chaos Orb"`.
type StudioCondition struct {
	Key      string   `json:"k"`
	Operator string   `json:"op,omitempty"`
	Values   []string `json:"v"`
}

// StudioColor is a colour with the palette name it came from, if any.
type StudioColor struct {
	Hex string `json:"hex"`
	Var string `json:"var,omitempty"`
}

// StudioLook is how a block draws its item label and what alerts it plays.
type StudioLook struct {
	Text        *StudioColor `json:"text,omitempty"`
	Border      *StudioColor `json:"border,omitempty"`
	Background  *StudioColor `json:"background,omitempty"`
	FontSize    int          `json:"fontSize,omitempty"`
	Beam        string       `json:"beam,omitempty"`
	BeamTemp    bool         `json:"beamTemp,omitempty"`
	MinimapSize *int         `json:"minimapSize,omitempty"`
	MinimapIcon string       `json:"minimapColor,omitempty"`
	MinimapKind string       `json:"minimapShape,omitempty"`
	Sound       string       `json:"sound,omitempty"`
	Volume      int          `json:"volume,omitempty"`
}

var sectionHeading = regexp.MustCompile(`^# >{8,} SECTION: (.*) \((.*)\)$`)

var actionKeywords = map[string]bool{
	"SetTextColor": true, "SetBorderColor": true, "SetBackgroundColor": true, "SetFontSize": true,
	"PlayAlertSound": true, "PlayAlertSoundPositional": true, "CustomAlertSound": true,
	"CustomAlertSoundOptional": true, "DisableDropSound": true, "EnableDropSound": true,
	"DisableDropSoundIfAlertSound": true, "EnableDropSoundIfAlertSound": true,
	"MinimapIcon": true, "PlayEffect": true, "Continue": true,
}

var conditionOperators = map[string]bool{"=": true, "==": true, "!": true, "!=": true, "<": true, "<=": true, ">": true, ">=": true}

// BuildStudioBlocks parses compiled filter lines into blocks and attaches the
// section, source rule and palette names recorded while the blocks were built.
// Blocks without a matching record (for example if a later step rewrote them)
// are still returned, with their look read from the filter text.
func BuildStudioBlocks(lines []string, records []BlockRecord) []StudioBlock {
	pending := make(map[string][]BlockRecord)
	for _, record := range records {
		key := blockKey(record.Lines)
		pending[key] = append(pending[key], record)
	}

	var blocks []StudioBlock
	section := ""
	for i := 0; i < len(lines); i++ {
		line := lines[i]
		if match := sectionHeading.FindStringSubmatch(line); match != nil {
			section = match[1]
			continue
		}
		if line != "Show" && line != "Hide" && line != "Minimal" {
			continue
		}

		blockLines := []string{line}
		for i+1 < len(lines) && strings.HasPrefix(lines[i+1], "\t") {
			i++
			blockLines = append(blockLines, lines[i])
		}

		block := StudioBlock{Line: i - len(blockLines) + 2, Action: line, Section: section}
		for _, body := range blockLines[1:] {
			fields := tokenizeFilterLine(strings.TrimSpace(body))
			if len(fields) == 0 || strings.HasPrefix(fields[0], "#") {
				continue
			}
			if actionKeywords[fields[0]] {
				applyAction(&block.Look, fields)
				continue
			}
			block.Conditions = append(block.Conditions, parseCondition(fields))
		}

		key := blockKey(blockLines)
		if queue := pending[key]; len(queue) > 0 {
			record := queue[0]
			pending[key] = queue[1:]
			block.Source = record.Source
			block.Style = record.Style.Name
			if record.Section != "" {
				block.Section = record.Section
			}
			attachPaletteNames(&block.Look, record.Style)
		}
		blocks = append(blocks, block)
	}
	return blocks
}

func blockKey(lines []string) string {
	trimmed := lines
	for len(trimmed) > 0 && strings.TrimSpace(trimmed[len(trimmed)-1]) == "" {
		trimmed = trimmed[:len(trimmed)-1]
	}
	return strings.Join(trimmed, "\n")
}

func parseCondition(fields []string) StudioCondition {
	condition := StudioCondition{Key: fields[0]}
	rest := fields[1:]
	if condition.Key == "HasExplicitMod" && len(rest) > 0 {
		if match := regexp.MustCompile(`^(>=|<=|>|<|=)([0-9]+)$`).FindStringSubmatch(rest[0]); match != nil {
			condition.Operator = match[1]
			rest = append([]string{match[2]}, rest[1:]...)
		}
	}
	if len(rest) > 0 && conditionOperators[rest[0]] {
		condition.Operator = rest[0]
		rest = rest[1:]
	}
	condition.Values = append([]string{}, rest...)
	return condition
}

func applyAction(look *StudioLook, fields []string) {
	switch fields[0] {
	case "SetTextColor":
		look.Text = colorFromFields(fields[1:])
	case "SetBorderColor":
		look.Border = colorFromFields(fields[1:])
	case "SetBackgroundColor":
		look.Background = colorFromFields(fields[1:])
	case "SetFontSize":
		if len(fields) > 1 {
			look.FontSize, _ = strconv.Atoi(fields[1])
		}
	case "PlayEffect":
		if len(fields) > 1 {
			look.Beam = fields[1]
			look.BeamTemp = len(fields) > 2 && fields[2] == "Temp"
		}
	case "MinimapIcon":
		if len(fields) > 3 {
			if size, err := strconv.Atoi(fields[1]); err == nil {
				look.MinimapSize = &size
			}
			look.MinimapIcon = fields[2]
			look.MinimapKind = fields[3]
		}
	case "CustomAlertSound", "CustomAlertSoundOptional", "PlayAlertSound", "PlayAlertSoundPositional":
		if len(fields) > 1 {
			look.Sound = fields[1]
		}
		if len(fields) > 2 {
			look.Volume, _ = strconv.Atoi(fields[2])
		}
	}
}

func colorFromFields(fields []string) *StudioColor {
	if len(fields) < 3 {
		return nil
	}
	channels := []int{0, 0, 0, 255}
	for i := 0; i < len(fields) && i < 4; i++ {
		channels[i], _ = strconv.Atoi(fields[i])
	}
	return &StudioColor{Hex: hexColor(channels[0], channels[1], channels[2], channels[3])}
}

func attachPaletteNames(look *StudioLook, style config.Style) {
	pairs := []struct {
		target *StudioColor
		source *config.Color
	}{{look.Text, style.TextColor}, {look.Border, style.BorderColor}, {look.Background, style.BackgroundColor}}
	for _, pair := range pairs {
		if pair.target != nil && pair.source != nil {
			pair.target.Var = pair.source.Var
		}
	}
}

func hexColor(r, g, b, a int) string {
	if a == 255 {
		return fmt.Sprintf("#%02X%02X%02X", r, g, b)
	}
	return fmt.Sprintf("#%02X%02X%02X%02X", r, g, b, a)
}

// tokenizeFilterLine splits a filter line on whitespace, keeping quoted strings
// together and dropping their quotes.
func tokenizeFilterLine(line string) []string {
	var fields []string
	var current strings.Builder
	inQuotes, hasToken := false, false
	for _, r := range line {
		switch {
		case r == '"':
			inQuotes = !inQuotes
			hasToken = true
		case (r == ' ' || r == '\t') && !inQuotes:
			if hasToken {
				fields = append(fields, current.String())
				current.Reset()
				hasToken = false
			}
		default:
			current.WriteRune(r)
			hasToken = true
		}
	}
	if hasToken {
		fields = append(fields, current.String())
	}
	return fields
}

// LookFromStyle describes a resolved style the way BuildStudioBlocks describes a block.
func LookFromStyle(style config.Style) StudioLook {
	look := StudioLook{
		Text:       studioColor(style.TextColor),
		Border:     studioColor(style.BorderColor),
		Background: studioColor(style.BackgroundColor),
	}
	if style.FontSize != nil {
		look.FontSize = *style.FontSize
	}
	if style.Beam != nil && style.Beam.Color != nil {
		look.Beam = *style.Beam.Color
		look.BeamTemp = style.Beam.Temp != nil && *style.Beam.Temp
	}
	if style.Minimap != nil && style.Minimap.Size != nil && style.Minimap.Color != nil && style.Minimap.Shape != nil {
		size := *style.Minimap.Size
		look.MinimapSize = &size
		look.MinimapIcon = *style.Minimap.Color
		look.MinimapKind = *style.Minimap.Shape
	}
	if style.DropSound != nil && style.DropVolume != nil {
		look.Sound = *style.DropSound
		look.Volume = *style.DropVolume
	}
	return look
}

func studioColor(color *config.Color) *StudioColor {
	if color == nil || color.Red == nil || color.Green == nil || color.Blue == nil {
		return nil
	}
	alpha := 255
	if color.Alpha != nil {
		alpha = int(*color.Alpha)
	}
	return &StudioColor{Hex: hexColor(int(*color.Red), int(*color.Green), int(*color.Blue), alpha), Var: color.Var}
}
