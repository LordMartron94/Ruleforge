package server

import (
	"context"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"regexp"
	"time"
)

var opaqueColor = regexp.MustCompile(`^#[0-9A-Fa-f]{6}$`)
var inputColor = regexp.MustCompile(`^#[0-9A-Fa-f]{6}([0-9A-Fa-f]{2})?$`)

func (s *Server) handleColor(w http.ResponseWriter, r *http.Request) {
	var body struct {
		Pairs []struct {
			Text       string `json:"text"`
			Background string `json:"background"`
			Backdrop   string `json:"backdrop"`
		} `json:"pairs"`
	}
	if err := json.NewDecoder(io.LimitReader(r.Body, 1<<20)).Decode(&body); err != nil || len(body.Pairs) < 1 || len(body.Pairs) > 160 {
		writeError(w, 400, fmt.Errorf("send 1..160 sRGB colour pairs"))
		return
	}
	expressions := []string{}
	for _, pair := range body.Pairs {
		if !inputColor.MatchString(pair.Text) || !inputColor.MatchString(pair.Background) {
			writeError(w, 400, fmt.Errorf("colours must be #RRGGBB or #RRGGBBAA values"))
			return
		}
		backdrop := pair.Backdrop
		if backdrop == "" {
			backdrop = "#000000"
		}
		if !opaqueColor.MatchString(backdrop) {
			writeError(w, 400, fmt.Errorf("backdrop must be opaque #RRGGBB"))
			return
		}
		background := fmt.Sprintf("color.Composite(%s,%s)", pair.Background, backdrop)
		text := fmt.Sprintf("color.Composite(%s,%s)", pair.Text, background)
		for _, function := range []string{"WCAG", "APCA", "DeltaE2000"} {
			expressions = append(expressions, fmt.Sprintf("color.%s(%s,%s)", function, text, background))
		}
	}
	ctx, cancel := context.WithTimeout(r.Context(), 60*time.Second)
	defer cancel()
	values, err := s.Engine.Eval(ctx, s.request(), []string{"std.color"}, expressions)
	if err != nil {
		writeError(w, 500, err)
		return
	}
	rows := []map[string]json.RawMessage{}
	for i := 0; i < len(values); i += 3 {
		rows = append(rows, map[string]json.RawMessage{"wcag": values[i], "apca": values[i+1], "deltaE2000": values[i+2]})
	}
	writeJSON(w, 200, map[string]any{"results": rows, "apcaVersion": "0.0.98G-4g", "whitePoint": "D65"})
}
