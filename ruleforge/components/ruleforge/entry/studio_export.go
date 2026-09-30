package main

import (
	"fmt"
	"time"

	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/config"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/entry/studio"
)

// writeStudio writes the Ruleforge Studio page for every compiled filter.
func (a *App) writeStudio() error {
	if a.config.StudioFile == "" {
		return nil
	}
	data, err := a.studioData()
	if err != nil {
		return err
	}
	if err := studio.Write(a.config.StudioFile, data); err != nil {
		return err
	}
	a.log.Printf("Wrote the Ruleforge Studio to %s", a.config.StudioFile)
	return nil
}

// studioData gathers what the Studio page shows.
func (a *App) studioData() (studio.Data, error) {
	data := studio.Data{
		GeneratedAt: time.Now().Format("2006-01-02 15:04"),
		Filters:     a.studioFilters,
	}
	var automation [][]config.BaseTypeAutomationEntry
	for _, definition := range a.styles {
		sheet, err := config.LoadStylesheet(definition.Stylesheet, definition.Palette)
		if err != nil {
			return data, fmt.Errorf("style %q: %w", definition.Name, err)
		}
		data.Styles = append(data.Styles, studio.NewStyle(definition, sheet))

		entries, err := a.loadBaseTypeData(definition.BaseTypes)
		if err != nil {
			return data, fmt.Errorf("style %q: %w", definition.Name, err)
		}
		automation = append(automation, *entries)
	}
	data.Items = studio.NewItemPool(a.itemBases, a.gems, a.uniques, a.economyData, automation)
	return data, nil
}
