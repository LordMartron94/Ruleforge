package server

import (
	"context"
	"encoding/json"
	"fmt"
	"io"
	"math/rand"
	"net/http"
	"os"
	"strings"
	"time"

	"github.com/LordMartron94/Ruleforge/studio/internal/provider"
)

func (s *Server) handleItem(w http.ResponseWriter, r *http.Request) {
	var body struct {
		Text      string `json:"text"`
		AreaLevel int    `json:"areaLevel"`
	}
	if err := json.NewDecoder(io.LimitReader(r.Body, 1<<20)).Decode(&body); err != nil || body.Text == "" {
		writeError(w, 400, fmt.Errorf("paste an English copied item"))
		return
	}
	ctx, cancel := context.WithTimeout(r.Context(), 30*time.Second)
	defer cancel()
	raw, err := s.Engine.Item(ctx, s.Project.Dir, body.Text)
	if err != nil {
		writeError(w, 400, err)
		return
	}
	var item map[string]any
	if err := json.Unmarshal(raw, &item); err != nil {
		writeError(w, 500, err)
		return
	}
	warnings := []string{"Copied text cannot reveal drop-only metadata or internal mod identifiers; absent fields are not inferred."}
	if body.AreaLevel > 0 && body.AreaLevel <= 100 {
		item["areaLevel"] = body.AreaLevel
	}
	if item["rarity"] == "Magic" {
		name, _ := item["baseType"].(string)
		var match string
		if path := provider.GamePaths(s.Project)["game.bases"]; path != "" {
			if data, err := os.ReadFile(path); err == nil {
				var rows []provider.GameRow
				if json.Unmarshal(data, &rows) == nil {
					for _, row := range rows {
						if (strings.HasSuffix(name, row.BaseType) || strings.Contains(name, row.BaseType+" of ")) && len(row.BaseType) > len(match) {
							match = row.BaseType
						}
					}
				}
			}
		}
		if match != "" {
			item["baseType"] = match
		} else {
			warnings = append(warnings, "Magic base type could not be resolved; configure gameData or edit baseType before simulating.")
		}
	}
	if lines, ok := item["_unparsed"].([]any); ok && len(lines) > 0 {
		warnings = append(warnings, fmt.Sprintf("%d lines are retained as _unparsed and do not supply filter conditions.", len(lines)))
	}
	writeJSON(w, 200, map[string]any{"item": item, "warnings": warnings})
}

// Samples are uniform draws from available catalogue entries, not a model of PoE drop probabilities.
func (s *Server) handleLoot(w http.ResponseWriter, r *http.Request) {
	var body struct {
		Count     int    `json:"count"`
		Seed      int64  `json:"seed"`
		AreaLevel int    `json:"areaLevel"`
		Source    string `json:"source"`
		Class     string `json:"class"`
		Rarity    string `json:"rarity"`
	}
	if err := json.NewDecoder(io.LimitReader(r.Body, 1<<20)).Decode(&body); err != nil || body.Count < 1 || body.Count > 200 || body.AreaLevel < 1 || body.AreaLevel > 100 {
		writeError(w, 400, fmt.Errorf("count 1..200 and areaLevel 1..100 are required"))
		return
	}
	if body.Source == "" {
		body.Source = "game.bases"
	}
	path, ok := provider.GamePaths(s.Project)[body.Source]
	if !ok {
		writeError(w, 400, fmt.Errorf("configure gameData and run Regenerate to load the PoB catalogue"))
		return
	}
	data, err := os.ReadFile(path)
	if err != nil {
		writeError(w, 400, fmt.Errorf("PoB catalogue is unavailable; run Regenerate"))
		return
	}
	var rows []provider.GameRow
	if err := json.Unmarshal(data, &rows); err != nil {
		writeError(w, 500, err)
		return
	}
	candidates := []provider.GameRow{}
	for _, row := range rows {
		if (body.Class == "" || row.Class == body.Class) && (row.DropLevel == nil || *row.DropLevel <= body.AreaLevel) {
			candidates = append(candidates, row)
		}
	}
	if len(candidates) == 0 {
		writeError(w, 400, fmt.Errorf("no catalogue entries match this class and area level"))
		return
	}
	rarity := body.Rarity
	if rarity == "" {
		rarity = "Normal"
	}
	if rarity != "Normal" && rarity != "Magic" && rarity != "Rare" && rarity != "Unique" {
		writeError(w, 400, fmt.Errorf("unknown rarity"))
		return
	}
	if body.Source == "game.uniques" {
		rarity = "Unique"
	}
	random := rand.New(rand.NewSource(body.Seed))
	items := make([]map[string]any, body.Count)
	unknown := 0
	for i := range items {
		row := candidates[random.Intn(len(candidates))]
		items[i] = map[string]any{"name": row.Name, "baseType": row.BaseType, "class": row.Class, "rarity": rarity, "itemLevel": body.AreaLevel, "areaLevel": body.AreaLevel, "identified": true, "corrupted": false}
		if row.DropLevel == nil {
			unknown++
		}
	}
	raw, err := json.Marshal(items)
	if err != nil {
		writeError(w, 500, err)
		return
	}
	ctx, cancel := context.WithTimeout(r.Context(), 120*time.Second)
	defer cancel()
	result, diagnostics, err := s.Engine.Simulate(ctx, s.request(), raw)
	if err != nil {
		writeError(w, 500, err)
		return
	}
	writeJSON(w, 200, map[string]any{"ok": result != nil, "items": items, "results": result, "diagnostics": diagnostics, "seed": body.Seed, "sampling": "uniform catalogue samples; not game drop probabilities", "unknownDropLevels": unknown})
}
