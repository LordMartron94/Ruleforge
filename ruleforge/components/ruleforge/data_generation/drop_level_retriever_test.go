package data_generation

import (
	"encoding/json"
	"fmt"
	"html"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"testing"

	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/data_generation/model"
)

func TestDropLevelsBatchAndDecodeNames(t *testing.T) {
	names := []string{"Charan's Sword", "Iron Greaves"}
	for i := 0; i < dropLevelBatchSize-1; i++ {
		names = append(names, fmt.Sprintf("Base %02d", i))
	}
	requests := 0
	seen := make(map[string]int)
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		requests++
		q := r.URL.Query()
		if q.Get("action") != "cargoquery" || q.Get("limit") != "500" || q.Get("fields") != "name, drop_level" {
			t.Errorf("unexpected query: %v", q)
		}
		if !strings.Contains(r.Header.Get("User-Agent"), "Ruleforge") {
			t.Error("missing Ruleforge user agent")
		}
		var rows []map[string]any
		for _, name := range names {
			if strings.Contains(q.Get("where"), "'"+strings.ReplaceAll(name, "'", "''")+"'") {
				seen[name]++
				rows = append(rows, map[string]any{"title": map[string]string{
					"name": html.EscapeString(name), "drop level": "2",
				}})
			}
		}
		if len(rows) == 0 || len(rows) > dropLevelBatchSize {
			t.Errorf("invalid batch size: %d", len(rows))
		}
		json.NewEncoder(w).Encode(map[string]any{"cargoquery": rows})
	}))
	defer server.Close()
	client := &dropLevelClient{client: server.Client(), baseURL: server.URL}
	levels, err := client.fetch(append(append([]string{}, names...), names...))
	if err != nil {
		t.Fatal(err)
	}
	if requests != 2 || len(levels) != len(names) {
		t.Fatalf("requests=%d levels=%d, want 2 requests and %d levels", requests, len(levels), len(names))
	}
	for _, name := range names {
		if seen[name] != 1 || levels[name] != 2 {
			t.Errorf("%q: requested %d times, level=%d", name, seen[name], levels[name])
		}
	}
}

func TestDropLevelsHandleMissingAndVariantData(t *testing.T) {
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		fmt.Fprint(w, `{"cargoquery":[
			{"title":{"name":"Golden Obi","drop level":"12"}},
			{"title":{"name":"Golden Obi","drop level":"20"}},
			{"title":{"name":"Simple Robe","drop level":"3"}},
			{"title":{"name":"Ethereal Bow","drop level":null}},
			{"title":{"name":"Unrequested base","drop level":"1"}}
		]}`)
	}))
	defer server.Close()
	client := &dropLevelClient{client: server.Client(), baseURL: server.URL}
	levels, err := client.fetch([]string{"Golden Obi", "Simple Robe", "Ethereal Bow", "Missing base"})
	if err != nil || len(levels) != 2 || levels["Golden Obi"] != 12 || levels["Simple Robe"] != 3 {
		t.Fatalf("unexpected levels=%v, err=%v", levels, err)
	}
}

func TestDropLevelsPaginateFullResponses(t *testing.T) {
	requests := 0
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		wantOffset := strconv.Itoa(requests * dropLevelResultLimit)
		if got := r.URL.Query().Get("offset"); got != wantOffset {
			t.Errorf("offset=%s, want %s", got, wantOffset)
		}
		requests++
		row := `{"title":{"name":"Variant Base","drop level":"12"}}`
		if requests == 1 {
			fmt.Fprint(w, `{"cargoquery":[`+strings.Repeat(row+",", dropLevelResultLimit-1)+row+`]}`)
		} else {
			fmt.Fprint(w, `{"cargoquery":[{"title":{"name":"Variant Base","drop level":"3"}}]}`)
		}
	}))
	defer server.Close()
	client := &dropLevelClient{client: server.Client(), baseURL: server.URL}
	levels, err := client.fetch([]string{"Variant Base"})
	if err != nil || requests != 2 || levels["Variant Base"] != 3 {
		t.Fatalf("pagination lost data: requests=%d levels=%v err=%v", requests, levels, err)
	}
}

func TestDropLevelsRejectFailures(t *testing.T) {
	for _, test := range []struct {
		name     string
		status   int
		body     string
		attempts int
		message  string
	}{
		{"HTTP forbidden", 403, `denied`, 1, "HTTP 403"},
		{"HTTP throttled", 429, `slow down`, 3, "HTTP 429"},
		{"HTTP unavailable", 503, `unavailable`, 3, "HTTP 503"},
		{"API throttled", 200, `{"error":{"code":"ratelimited","info":"slow down"}}`, 3, "ratelimited"},
		{"API bad query", 200, `{"error":{"code":"badquery","info":"invalid query"}}`, 1, "badquery"},
		{"missing results", 200, `{}`, 1, "missing cargoquery"},
		{"null results", 200, `{"cargoquery":null}`, 1, "missing cargoquery"},
		{"invalid JSON", 200, `<html>error</html>`, 1, "decode wiki"},
		{"bad level", 200, `{"cargoquery":[{"title":{"name":"Iron Greaves","drop level":"unknown"}}]}`, 1, "invalid wiki drop level"},
		{"zero level", 200, `{"cargoquery":[{"title":{"name":"Iron Greaves","drop level":"0"}}]}`, 1, "invalid wiki drop level"},
	} {
		t.Run(test.name, func(t *testing.T) {
			requests := 0
			server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
				requests++
				w.WriteHeader(test.status)
				fmt.Fprint(w, test.body)
			}))
			defer server.Close()
			client := &dropLevelClient{client: server.Client(), baseURL: server.URL}
			levels, err := client.fetch([]string{"Iron Greaves"})
			if err == nil || !strings.Contains(err.Error(), test.message) || levels != nil || requests != test.attempts {
				t.Fatalf("requests=%d levels=%v err=%v", requests, levels, err)
			}
		})
	}
}

func TestDropLevelsRecoverFromThrottling(t *testing.T) {
	requests := 0
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		requests++
		if requests == 1 {
			w.Header().Set("Retry-After", "0")
			w.WriteHeader(http.StatusTooManyRequests)
			return
		}
		fmt.Fprint(w, `{"cargoquery":[{"title":{"name":"Iron Greaves","drop level":"1"}}]}`)
	}))
	defer server.Close()
	client := &dropLevelClient{client: server.Client(), baseURL: server.URL}
	levels, err := client.fetch([]string{"Iron Greaves"})
	if err != nil || requests != 2 || levels["Iron Greaves"] != 1 {
		t.Fatalf("retry failed: requests=%d levels=%v err=%v", requests, levels, err)
	}
}

func TestDropLevelsDoNotApplyPartialEnrichment(t *testing.T) {
	items := make([]model.ItemBase, dropLevelBatchSize+1)
	for i := range items {
		items[i].Name = fmt.Sprintf("Base %02d", i)
	}
	requests := 0
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		requests++
		if requests == 1 {
			fmt.Fprint(w, `{"cargoquery":[{"title":{"name":"Base 00","drop level":"12"}}]}`)
		} else {
			w.WriteHeader(http.StatusForbidden)
		}
	}))
	defer server.Close()
	client := &dropLevelClient{client: server.Client(), baseURL: server.URL}
	if err := enrichItemBasesWithDropLevels(items, client); err == nil {
		t.Fatal("failed batch silently treated as successful enrichment")
	}
	for _, item := range items {
		if item.DropLevel != nil {
			t.Fatalf("partial enrichment applied: %+v", item)
		}
	}
}

func TestItemDropLevelsRebuiltAndReused(t *testing.T) {
	root := t.TempDir()
	cachePath := filepath.Join(root, "basetypes-standard-only.json")
	old := `{"version":3,"expiry_date":"2099-01-01T00:00:00Z","items":[{"name":"Simple Robe"}]}`
	if err := os.WriteFile(cachePath, []byte(old), 0644); err != nil {
		t.Fatal(err)
	}
	luaPath := filepath.Join(root, "body.lua")
	if err := os.WriteFile(luaPath, []byte(`local bases = ...; bases["Simple Robe"] = { type = "Body Armour", req = {} }`), 0644); err != nil {
		t.Fatal(err)
	}
	requests := 0
	fail := true
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		requests++
		if fail {
			fmt.Fprint(w, `{"error":{"code":"badquery","info":"fixture failure"}}`)
			return
		}
		fmt.Fprint(w, `{"cargoquery":[{"title":{"name":"Simple Robe","drop level":"3"}}]}`)
	}))
	defer server.Close()
	exporter, err := NewPathOfBuildingExporter(root, filepath.Join(root, "economy.db"))
	if err != nil {
		t.Fatal(err)
	}
	exporter.dropLevels = &dropLevelClient{client: server.Client(), baseURL: server.URL}
	if items, err := exporter.LoadItemBases([]string{luaPath}); err == nil || items != nil {
		t.Fatalf("lookup failure must abort loading: items=%v err=%v", items, err)
	}
	if data, err := os.ReadFile(cachePath); err != nil || string(data) != old {
		t.Fatalf("failed refresh changed the cache: %v", err)
	}
	fail = false
	items, err := exporter.LoadItemBases([]string{luaPath})
	if err != nil || len(items) != 1 || items[0].DropLevel == nil || *items[0].DropLevel != 3 {
		t.Fatalf("old incomplete cache not repaired: items=%v err=%v", items, err)
	}
	if err := exporter.SaveItemCache(items, nil, nil, nil); err != nil {
		t.Fatal(err)
	}
	// A valid cache must work without Lua files or any wiki request.
	reloaded, err := NewPathOfBuildingExporter(root, filepath.Join(root, "economy.db"))
	if err != nil {
		t.Fatal(err)
	}
	reloaded.dropLevels = &dropLevelClient{client: server.Client(), baseURL: server.URL}
	items, err = reloaded.LoadItemBases([]string{"does-not-exist.lua"})
	if err != nil || requests != 2 || len(items) != 1 || items[0].DropLevel == nil || *items[0].DropLevel != 3 {
		t.Fatalf("repaired drop level was not cached: requests=%d items=%v err=%v", requests, items, err)
	}
}
