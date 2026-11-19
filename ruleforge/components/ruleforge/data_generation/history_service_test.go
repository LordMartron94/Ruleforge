package data_generation

import (
	"context"
	"database/sql"
	"fmt"
	"net/http"
	"net/http/httptest"
	"path/filepath"
	"testing"
	"time"
)

func TestHistoryMigratesLegacyDatabaseWithoutDiscardingObservations(t *testing.T) {
	path := filepath.Join(t.TempDir(), "legacy.db")
	db, err := sql.Open("sqlite", path)
	if err != nil {
		t.Fatal(err)
	}
	if _, err = db.Exec(economyHistorySchema); err != nil {
		t.Fatal(err)
	}
	at := time.Date(2026, 11, 18, 0, 0, 0, 0, time.UTC)
	if _, err = db.Exec("INSERT INTO snapshots(league,fetched_at) VALUES(?,?)", "Covens", at); err != nil {
		t.Fatal(err)
	}
	if _, err = db.Exec("INSERT INTO snapshot_items VALUES(1,'Currency','Old','Old',10,0,0,123,123)"); err != nil {
		t.Fatal(err)
	}
	db.Close()
	store, err := NewEconomyHistoryStore(path)
	if err != nil {
		t.Fatal(err)
	}
	defer store.Close()
	snapshots, err := store.Window("Covens", time.Time{})
	if err != nil {
		t.Fatal(err)
	}
	if len(snapshots) != 1 || len(snapshots[0].Items) != 1 || snapshots[0].Items[0].ListingCount != 123 {
		t.Fatalf("legacy raw data lost: %+v", snapshots)
	}
	if supplyKnown(snapshots[0].Items[0]) {
		t.Fatal("legacy exchange approximation trusted as supply")
	}
}

func TestHistorySyncResumesAfterPageFailureAndRejectsUnauthenticatedWrites(t *testing.T) {
	ctx := context.Background()
	source, err := NewEconomyHistoryStore(filepath.Join(t.TempDir(), "source.db"))
	if err != nil {
		t.Fatal(err)
	}
	defer source.Close()
	local, err := NewEconomyHistoryStore(filepath.Join(t.TempDir(), "local.db"))
	if err != nil {
		t.Fatal(err)
	}
	defer local.Close()
	at := time.Now().UTC().Add(-24 * time.Hour)
	for i := 0; i < 3; i++ {
		v := EconomyCacheItem{Class: "Currency", Name: "A", BaseType: "A", ChaosValue: float64(10 + i), Source: "exchange", ItemID: "a", TradedUnits: 100, VolumeChaos: 1000}
		if err := source.RecordCollection(EconomySnapshot{League: "Covens", LeagueID: "covens", GameVersion: "3.30", FetchedAt: at.Add(time.Duration(i) * 4 * time.Hour), Items: []EconomyCacheItem{v}}); err != nil {
			t.Fatal(err)
		}
	}
	handler := NewHistoryHandler(source, "test-secret")
	fail := true
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Path == "/v1/snapshots" && r.URL.Query().Get("after") == "1" && fail {
			http.Error(w, "temporary outage", 503)
			return
		}
		handler.ServeHTTP(w, r)
	}))
	defer server.Close()
	if err := SyncHistory(ctx, local, server.URL, "test-secret", []string{"Covens"}); err == nil {
		t.Fatal("page failure hidden")
	}
	snapshots, err := local.Window("Covens", time.Time{})
	if err != nil || len(snapshots) != 1 {
		t.Fatalf("first page wasn't committed: %v %v", snapshots, err)
	}
	fail = false
	for i := 0; i < 2; i++ {
		if err := SyncHistory(ctx, local, server.URL, "test-secret", []string{"Covens"}); err != nil {
			t.Fatal(err)
		}
	}
	snapshots, err = local.Window("Covens", time.Time{})
	if err != nil || len(snapshots) != 3 || snapshots[2].Items[0].TradedUnits != 100 || snapshots[0].GameVersion != "3.30" {
		t.Fatalf("resumed sync lost or duplicated data: %v %v", snapshots, err)
	}
	r := httptest.NewRequest("GET", "/v1/status", nil)
	w := httptest.NewRecorder()
	handler.ServeHTTP(w, r)
	if w.Code != 401 {
		t.Fatalf("unauthenticated request: %d", w.Code)
	}
	r = httptest.NewRequest("POST", "/v1/snapshots", nil)
	r.Header.Set("Authorization", "Bearer test-secret")
	w = httptest.NewRecorder()
	handler.ServeHTTP(w, r)
	if w.Code != 405 {
		t.Fatalf("write endpoint exposed: %d", w.Code)
	}
	if err := SyncHistory(ctx, local, server.URL, "wrong", []string{"Covens"}); err == nil {
		t.Fatal("wrong token accepted")
	}
}

func TestCollectorRecordsUnsupportedCapabilityAndNeverCommitsPartialLeague(t *testing.T) {
	store, err := NewEconomyHistoryStore(filepath.Join(t.TempDir(), "history.db"))
	if err != nil {
		t.Fatal(err)
	}
	defer store.Close()
	status := 404
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Path == "/leagues" {
			fmt.Fprint(w, `[{"name":"Covens","id":"covens"}]`)
			return
		}
		if r.URL.Query().Get("type") == "NewFeed" {
			http.Error(w, "unavailable", status)
			return
		}
		fmt.Fprint(w, `{"core":{"primary":"chaos"},"items":[{"id":"a","name":"A"}],"lines":[{"id":"a","primaryValue":10,"volumePrimaryValue":100}]}`)
	}))
	defer server.Close()
	client := NewPoeNinjaClient()
	client.baseURL = server.URL
	client.requestDelay = 0
	feeds := []EconomyCategory{{Kind: "Currency", Class: "Currency", Endpoint: "exchange"}, {Kind: "NewFeed", Class: "Viscera", Endpoint: "stash", Optional: true}}
	if err := CollectEconomy(context.Background(), client, store, []string{"Covens"}, feeds, time.Hour, true, "3.30"); err != nil {
		t.Fatal(err)
	}
	snapshots, err := store.Window("Covens", time.Time{})
	if err != nil || len(snapshots) != 1 || snapshots[0].Categories["stash:NewFeed"] != "unsupported" {
		t.Fatalf("capability state missing: %+v %v", snapshots, err)
	}
	status = 503
	if err := CollectEconomy(context.Background(), client, store, []string{"Covens"}, feeds, time.Hour, true, "3.30"); err == nil {
		t.Fatal("transient failure treated as empty/unsupported")
	}
	snapshots, _ = store.Window("Covens", time.Time{})
	if len(snapshots) != 1 {
		t.Fatal("partial league committed")
	}
}
