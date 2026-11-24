package history

import (
	"context"
	"fmt"
	"net/http"
	"net/http/httptest"
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
			if r.URL.Query().Get("type") == "SkillGem" {
				fmt.Fprint(w, `{"lines":[
					{"name":"Holy Strike","chaosValue":38.98,"listingCount":59},
					{"name":"Hallow Support","chaosValue":67.78,"listingCount":33},
					{"name":"Spectral Throw","chaosValue":5,"listingCount":35},
					{"name":"Vaal Spectral Throw (Spectral Throw of Trarthus)","baseType":"Vaal Spectral Throw","chaosValue":25,"listingCount":16},
					{"chaosValue":1}
				]}`)
				return
			}
			fmt.Fprint(w, `{"lines":[{"name":"Test unique","baseType":"Test base","chaosValue":25.5,"divineValue":0.2,"listingCount":12},{"name":"Unique without base","chaosValue":1}]}`)
		case "/exchange/item/overview":
			itemRequests++
			switch r.URL.Query().Get("type") {
			case "Tattoo":
				fmt.Fprint(w, `{
				"core": {"primary": "chaos"},
				"items": [{"id":"tattoo-of-the-valako-shieldbearer","name":"Tattoo of the Valako Shieldbearer"}],
				"lines": [{"id":"tattoo-of-the-valako-shieldbearer","primaryValue":494,"volumePrimaryValue":4940}]
			}`)
				return
			case "Currency":
				fmt.Fprint(w, `{
				"core": {"primary": "chaos"},
				"items": [{"id":"ancient-orb","name":"Ancient Orb"}],
				"lines": [{"id":"ancient-orb","primaryValue":10,"volumePrimaryValue":2000}]
			}`)
				return
			case "DeliriumOrb", "Omen":
				fmt.Fprint(w, `{"core": {"primary": "chaos"}, "items": [], "lines": []}`)
				return
			case "DivinationCard":
			default:
				t.Errorf("expected an exchange category; got %q", r.URL.Query().Get("type"))
			}
			fmt.Fprint(w, `{
				"core": {"primary": "chaos"},
				"items": [{"id":"the-doctor","name":"The Doctor"},{"id":"no-price","name":"Unpriced Card"}],
				"lines": [{"id":"the-doctor","primaryValue":500,"volumePrimaryValue":1500},{"id":"missing-name","primaryValue":1,"volumePrimaryValue":1}]
			}`)
		default:
			t.Errorf("unexpected API path %s", r.URL.Path)
			http.NotFound(w, r)
		}
	}))
	defer server.Close()
	client := NewPoeNinjaClient()
	client.baseURL = server.URL
	client.requestDelay = 0
	store, err := NewEconomyHistoryStore(filepath.Join(t.TempDir(), "economy.db"))
	if err != nil {
		t.Fatal(err)
	}
	defer store.Close()
	if err := CollectEconomy(context.Background(), client, store, []string{"Allflame"}, economyCategories, time.Hour, true, CurrentEconomyVersion); err != nil {
		t.Fatal(err)
	}
	if requests != 12 || itemRequests != 11 {
		t.Fatalf("unexpected request counts total=%d item=%d", requests, itemRequests)
	}
	snapshots, err := store.Window("Allflame", time.Time{})
	window := map[string][]EconomySnapshot{"Allflame": snapshots}
	if err != nil {
		t.Fatal(err)
	}
	if len(window["Allflame"]) != 1 {
		t.Fatalf("expected exactly one recorded snapshot, got %#v", window)
	}
	fetched := window["Allflame"][0].Items
	if len(fetched) != 12 {
		t.Fatalf("expected five uniques, four gems, one priced div card, one tattoo and one currency, got %#v", fetched)
	}
	first := fetched[0]
	if first.BaseType != "Test base" || first.ChaosValue != 25.5 || first.DivineValue != 0.2 || first.ListingCount != 12 {
		t.Fatalf("incorrect economy fields: %#v", first)
	}
	for i, base := range []string{"Holy Strike", "Hallow Support", "Spectral Throw", "Vaal Spectral Throw"} {
		gem := fetched[5+i]
		if gem.Class != "Gems" || gem.BaseType != base || gem.Name == "" || gem.ChaosValue <= 0 || gem.ListingCount <= 0 {
			t.Fatalf("incorrect gem economy record for %q: %#v", base, gem)
		}
	}
	divCard := fetched[9]
	if divCard.Class != "Divination Cards" || divCard.BaseType != "The Doctor" || divCard.ChaosValue != 500 || divCard.ListingCount != 0 || divCard.TradedUnits != 3 || divCard.Source != "exchange" {
		t.Fatalf("incorrect divination card economy record (name resolved via the exchange items lookup, unpriced/unnamed lines dropped): %#v", divCard)
	}
	if tattoo := fetched[10]; tattoo.Class != "Tattoos" || tattoo.BaseType != "Tattoo of the Valako Shieldbearer" || tattoo.ChaosValue != 494 || tattoo.ListingCount != 0 || tattoo.TradedUnits != 10 {
		t.Fatalf("incorrect tattoo economy record: %#v", tattoo)
	}
	if orb := fetched[11]; orb.Class != "Currency" || orb.BaseType != "Ancient Orb" || orb.ChaosValue != 10 || orb.ListingCount != 0 || orb.TradedUnits != 200 {
		t.Fatalf("incorrect currency economy record: %#v", orb)
	}
	if err := CollectEconomy(context.Background(), client, store, []string{"Allflame"}, economyCategories, time.Hour, false, CurrentEconomyVersion); err != nil {
		t.Fatal(err)
	}
	if requests != 12 {
		t.Fatalf("recent-enough snapshot triggered network access: %d requests", requests)
	}
}

func TestPoeNinjaUnknownLeagueIsSkippedWithoutItemRequests(t *testing.T) {
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
	err := CollectEconomy(context.Background(), client, nil, []string{"Expired league"}, economyCategories, time.Hour, true, CurrentEconomyVersion)
	if err != nil || requests != 1 {
		t.Fatalf("expected the unavailable league to be skipped without item requests; error=%v requests=%d", err, requests)
	}
}

func TestPoeNinjaLeagueWithoutPricesIsSkippedAndLeavesNoHistory(t *testing.T) {
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		switch {
		case r.URL.Path == "/leagues":
			fmt.Fprint(w, `[{"id":"Covens","name":"Covens"}]`)
		case strings.Contains(r.URL.Path, "exchange"):
			fmt.Fprint(w, `{"core":{"items":[],"rates":{}},"lines":[],"items":[]}`)
		default:
			fmt.Fprint(w, `{"lines":[]}`)
		}
	}))
	defer server.Close()
	client := NewPoeNinjaClient()
	client.baseURL = server.URL
	client.requestDelay = 0
	store, err := NewEconomyHistoryStore(filepath.Join(t.TempDir(), "economy.db"))
	if err != nil {
		t.Fatal(err)
	}
	defer store.Close()
	if err := CollectEconomy(context.Background(), client, store, []string{"Covens"}, economyCategories, time.Hour, true, CurrentEconomyVersion); err != nil {
		t.Fatalf("a league without prices yet must not fail the refresh: %v", err)
	}
	if _, ok, err := store.LatestSnapshotAt("Covens"); err != nil || ok {
		t.Fatalf("empty league must not create a snapshot: %v %v", ok, err)
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
	err := CollectEconomy(context.Background(), client, nil, []string{"Standard"}, economyCategories, time.Hour, true, CurrentEconomyVersion)
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

func TestEconomyHistoryPersistsAcrossSnapshots(t *testing.T) {
	dbPath := filepath.Join(t.TempDir(), "economy.db")
	store, err := NewEconomyHistoryStore(dbPath)
	if err != nil {
		t.Fatal(err)
	}
	defer store.Close()

	if _, ok, err := store.LatestSnapshotAt("Standard"); err != nil || ok {
		t.Fatalf("expected no snapshot yet: ok=%v err=%v", ok, err)
	}

	older := time.Now().Add(-48 * time.Hour)
	newer := time.Now()
	if err := store.RecordSnapshot("Standard", older, []EconomyCacheItem{{Name: "Test", BaseType: "Test", ChaosValue: 3}}); err != nil {
		t.Fatal(err)
	}
	if err := store.RecordSnapshot("Standard", newer, []EconomyCacheItem{{Name: "Test", BaseType: "Test", ChaosValue: 5}}); err != nil {
		t.Fatal(err)
	}

	latest, ok, err := store.LatestSnapshotAt("Standard")
	if err != nil || !ok {
		t.Fatalf("expected a latest snapshot: ok=%v err=%v", ok, err)
	}
	if diff := latest.Sub(newer); diff > time.Second || diff < -time.Second {
		t.Fatalf("unexpected latest snapshot time: got %v, want ~%v", latest, newer)
	}

	// A window that only reaches back 1 hour should miss the 48h-old snapshot.
	recentOnly, err := store.Window("Standard", time.Now().Add(-time.Hour))
	if err != nil {
		t.Fatal(err)
	}
	if len(recentOnly) != 1 || recentOnly[0].Items[0].ChaosValue != 5 {
		t.Fatalf("expected only the recent snapshot in a 1h window: %#v", recentOnly)
	}

	// A wide window should return both, oldest first.
	full, err := store.Window("Standard", time.Now().Add(-72*time.Hour))
	if err != nil {
		t.Fatal(err)
	}
	if len(full) != 2 || full[0].Items[0].ChaosValue != 3 || full[1].Items[0].ChaosValue != 5 {
		t.Fatalf("expected both snapshots ordered oldest first: %#v", full)
	}

	// The zero time means "everything ever recorded" - nothing is pruned, so
	// this is how EconomyHistory loads a league's full history.
	everything, err := store.Window("Standard", time.Time{})
	if err != nil {
		t.Fatal(err)
	}
	if len(everything) != 2 {
		t.Fatalf("expected the zero time to return every snapshot: %#v", everything)
	}
}
