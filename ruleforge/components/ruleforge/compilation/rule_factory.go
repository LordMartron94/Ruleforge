package compilation

import (
	"fmt"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/compilation/model"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/config"
)

type RuleFactory struct{}

func (r *RuleFactory) ConstructRule(ruleType model.RuleType, style config.Style, conditions []string) []string {
	output := []string{string(ruleType)}

	for _, condition := range conditions {
		output = append(output, r.prefixLineWithTab(condition))
	}

	output = append(output, r.transformStyleIntoText(style)...)
	output = append(output, "")

	return output
}

func (r *RuleFactory) transformStyleIntoText(style config.Style) []string {
	rawOutput := make([]string, 0)

	if style.TextColor != nil {
		rawOutput = append(rawOutput, r.retrieveColorString("SetTextColor", *style.TextColor))
	}

	if style.BorderColor != nil {
		rawOutput = append(rawOutput, r.retrieveColorString("SetBorderColor", *style.BorderColor))
	}

	if style.BackgroundColor != nil {
		rawOutput = append(rawOutput, r.retrieveColorString("SetBackgroundColor", *style.BackgroundColor))
	}

	if style.FontSize != nil {
		rawOutput = append(rawOutput, fmt.Sprintf("SetFontSize %d", *style.FontSize))
	}

	if style.DropSound != nil && style.DropVolume != nil {
		rawOutput = append(rawOutput, fmt.Sprintf("CustomAlertSound \"%s\" %d", *style.DropSound, *style.DropVolume))
	}

	if style.Minimap != nil {
		if style.Minimap.Size != nil && style.Minimap.Shape != nil && style.Minimap.Color != nil {
			rawOutput = append(rawOutput, r.retrieveMinimapIconString(*style.Minimap))
		} else {
			errorComment := fmt.Sprintf("# WARNING: Style '%s' has an incomplete Minimap. All three properties (Size, Shape, Color) are required to render the icon. This can obviously be because of a tier that does not use minimaps.", style.Name)

			size := valOrNil(style.Minimap.Size)
			color := valOrNil(style.Minimap.Color)
			shape := valOrNil(style.Minimap.Shape)

			additional := fmt.Sprintf("# MM: [size %v, color %v, shape %v]", size, color, shape)
			rawOutput = append(rawOutput, errorComment)
			rawOutput = append(rawOutput, additional)
		}
	}

	if style.Beam != nil {
		if style.Beam.Color != nil {
			rawOutput = append(rawOutput, r.retrieveBeamString(*style.Beam))
		}
	}

	output := make([]string, len(rawOutput))
	for i, raw := range rawOutput {
		output[i] = r.prefixLineWithTab(raw)
	}

	return output
}

func (r *RuleFactory) retrieveBeamString(beam config.Beam) string {
	if beam.Temp != nil && *beam.Temp {
		return fmt.Sprintf("PlayEffect %s Temp", *beam.Color)
	}

	return fmt.Sprintf("PlayEffect %s", *beam.Color)
}

func (r *RuleFactory) retrieveMinimapIconString(minimap config.Minimap) string {
	return fmt.Sprintf("MinimapIcon %d %s %s", *minimap.Size, *minimap.Color, *minimap.Shape)
}

func (r *RuleFactory) retrieveColorString(element string, color config.Color) string {
	return fmt.Sprintf("%s %d %d %d %d", element, *color.Red, *color.Green, *color.Blue, *color.Alpha)
}

func (r *RuleFactory) prefixLineWithTab(line string) string {
	return "\t" + line
}

func valOrNil[T any](p *T) interface{} {
	if p != nil {
		return *p
	}
	return "<nil>"
}
