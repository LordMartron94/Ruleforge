package data_generation

import (
	"fmt"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"
)

func TestPoeNinjaEconomyAPIAndConfiguredLeagueNames(t *testing.T) {
	requests := 0
	itemRequests := 0
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		requests++
		w.Header().Set("Content-Type", "application/json")
		switch r.URL.Path {
		case "/leagues":
			if r.URL.RawQuery != "" {
				t.Errorf("leagues request unexpectedly has query: %s", r.URL.RawQuery)
			}
			fmt.Fprint(w, `[{"id":"allflame-2026","name":"Allflame"},{"id":"hc-allflame-2026","name":"Hardcore Allflame"}]`)
		case "/stash/current/item/overview":
			itemRequests++
			if r.URL.Query().Get("league") != "allflame-2026" {
				t.Errorf("expected resolved id; got %q", r.URL.Query().Get("league"))
			}
			if r.URL.Query().Get("type") == "" {
				t.Error("missing category")
			}
			if !strings.Contains(r.Header.Get("User-Agent"), "Ruleforge") {
				t.Error("missing user agent")
			}
			fmt.Fprint(w, `{"lines":[{"name":"Test unique","baseType":"Test base","chaosValue":25.5,"divineValue":0.2,"listingCount":12}]}`)
		default:
			t.Errorf("unexpected API path %s", r.URL.Path)
			http.NotFound(w, r)
		}
	}))
	defer server.Close()
	client := NewPoeNinjaClient()
	client.baseURL = server.URL
	client.requestDelay = 0
	exporter := &PathOfBuildingExporter{economyScraper: client}
	fetched, err := exporter.GetEconomyData([]string{"Allflame"}, true)
	if err != nil {
		t.Fatal(err)
	}
	if requests != 7 || itemRequests != 6 {
		t.Fatalf("unexpected request counts total=%d item=%d", requests, itemRequests)
	}
	if len(fetched["Allflame"]) != 6 {
		t.Fatalf("expected six priced items under configured league name, got %#v", fetched)
	}
	first := fetched["Allflame"][0]
	if first.BaseType != "Test base" || first.ChaosValue != 25.5 || first.DivineValue != 0.2 || first.ListingCount != 12 {
		t.Fatalf("incorrect economy fields: %#v", first)
	}
	exporter.economyCache = &EconomyCacheModel{EconomyCacheItems: fetched}
	again, err := exporter.GetEconomyData([]string{"Allflame"}, false)
	if err != nil || requests != 7 || len(again["Allflame"]) != 6 {
		t.Fatalf("valid cache was not reused: %d requests, err=%v", requests, err)
	}
}

func TestPoeNinjaUnknownLeagueFailsBeforeItemRequests(t *testing.T) {
	requests := 0
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		requests++
		if r.URL.Path != "/leagues" {
			t.Errorf("unexpected item request %q", r.URL.Path)
		}
		fmt.Fprint(w, `[{"id":"Standard","name":"Standard"}]`)
	}))
	defer server.Close()
	client := NewPoeNinjaClient()
	client.baseURL = server.URL
	client.requestDelay = 0
	_, err := (&PathOfBuildingExporter{economyScraper: client}).GetEconomyData([]string{"Expired league"}, true)
	if err == nil || !strings.Contains(err.Error(), `"Expired league"`) || !strings.Contains(err.Error(), "Standard") || requests != 1 {
		t.Fatalf("expected clear unavailable-league error and no wasted requests; error=%v requests=%d", err, requests)
	}
}

func TestPoeNinjaHTTP404StopsImmediately(t *testing.T) {
	requests := 0
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		requests++
		if r.URL.Path == "/leagues" {
			fmt.Fprint(w, `[{"id":"Standard","name":"Standard"}]`)
			return
		}
		http.NotFound(w, r)
	}))
	defer server.Close()
	client := NewPoeNinjaClient()
	client.baseURL = server.URL
	client.requestDelay = 0
	_, err := (&PathOfBuildingExporter{economyScraper: client}).GetEconomyData([]string{"Standard"}, true)
	if err == nil || !strings.Contains(err.Error(), "HTTP 404") || !strings.Contains(err.Error(), "UniqueWeapon") || requests != 2 {
		t.Fatalf("expected first 404 to stop fetch; error=%v requests=%d", err, requests)
	}
}

func TestPoeNinjaRejectsInvalidResponse(t *testing.T) {
	for _, body := range []string{`{}`, `{"lines":null}`} {
		t.Run(body, func(t *testing.T) {
			server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) { fmt.Fprint(w, body) }))
			defer server.Close()
			client := NewPoeNinjaClient()
			client.baseURL = server.URL
			if _, err := client.FetchData("itemoverview", "SkillGem", "Standard", "Gems"); err == nil {
				t.Fatal("expected invalid response rejection")
			}
		})
	}
}

func TestOldEconomyCacheIsInvalidated(t *testing.T) {
	dir := t.TempDir()
	itemPath := filepath.Join(dir, "item.json")
	economyPath := filepath.Join(dir, "economy.json")
	old := []byte(`{"expiry_date":"2099-01-01T00:00:00Z","items":{"Standard":[{"name":"stale"}]}}`)
	if err := os.WriteFile(economyPath, old, 0600); err != nil {
		t.Fatal(err)
	}
	repo := NewCacheRepository(itemPath, economyPath)
	_, cached, err := repo.LoadCache()
	if err != nil || cached != nil {
		t.Fatalf("old economy schema reused: %#v %v", cached, err)
	}
	fresh := map[string][]EconomyCacheItem{"Standard": {{BaseType: "Test", ChaosValue: 3}}}
	if err := repo.SaveEconomyCache(fresh); err != nil {
		t.Fatal(err)
	}
	_, cached, err = repo.LoadCache()
	if err != nil || cached == nil || cached.Version != economyCacheSchemaVersion || time.Now().After(cached.ExpiryDate) || cached.EconomyCacheItems["Standard"][0].ChaosValue != 3 {
		t.Fatalf("new economy schema failed round-trip: %#v %v", cached, err)
	}
}
