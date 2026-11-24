package provider

import (
	"encoding/json"
	"fmt"
	"html"
	"net/http"
	"net/http/httptest"

	"strconv"
	"strings"
	"testing"
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
