package provider

import (
	"context"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func fakeNinja(t *testing.T) *httptest.Server {
	mux := http.NewServeMux()
	mux.HandleFunc("/leagues", func(w http.ResponseWriter, r *http.Request) {
		_, _ = w.Write([]byte(`[{"id":"Standard","name":"Standard"},{"id":"Settlers","name":"Settlers"}]`))
	})
	mux.HandleFunc("/stash/current/item/overview", func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Query().Get("league") != "Settlers" {
			http.Error(w, "no", http.StatusBadRequest)
			return
		}
		_, _ = w.Write([]byte(`{"lines":[{"name":"Mageblood","baseType":"Heavy Belt","chaosValue":55000.5,"listingCount":4,"detailsId":"mageblood"},{"name":"Unlisted","baseType":"Unlisted Base","chaosValue":0,"listingCount":0}]}`))
	})
	mux.HandleFunc("/exchange/item/overview", func(w http.ResponseWriter, r *http.Request) {
		_, _ = w.Write([]byte(`{"core":{"primary":"chaos"},"items":[{"id":"divine","name":"Divine Orb"}],"lines":[{"id":"divine","primaryValue":210.5,"volumePrimaryValue":1200}]}`))
	})
	return httptest.NewServer(mux)
}

func TestResolveLeagueCurrentSkipsPermanentLeagues(t *testing.T) {
	server := fakeNinja(t)
	defer server.Close()
	ninja := &Ninja{Base: server.URL}
	id, err := ninja.ResolveLeague(context.Background(), "current")
	if err != nil || id != "Settlers" {
		t.Fatalf("id=%q err=%v", id, err)
	}
	if _, err := ninja.ResolveLeague(context.Background(), "Nope"); err == nil || !strings.Contains(err.Error(), "available") {
		t.Fatalf("an unknown league must list the available ones: %v", err)
	}
}

func TestSnapshotIsDeterministicAndKeepsUnpricedEmpty(t *testing.T) {
	server := fakeNinja(t)
	defer server.Close()
	ninja := &Ninja{Base: server.URL}
	rows, err := ninja.Stash(context.Background(), "Settlers", "UniqueAccessory")
	if err != nil {
		t.Fatal(err)
	}
	more, err := ninja.Exchange(context.Background(), "Settlers", "Currency")
	if err != nil {
		t.Fatal(err)
	}
	rows = append(rows, more...)
	dir := t.TempDir()
	first, err := WriteCSV(filepath.Join(dir, "a.csv"), rows)
	if err != nil {
		t.Fatal(err)
	}
	reversed := []Row{rows[2], rows[1], rows[0]}
	second, err := WriteCSV(filepath.Join(dir, "b.csv"), reversed)
	if err != nil || first != second {
		t.Fatalf("the same market must give the same bytes: %s vs %s (%v)", first, second, err)
	}
	data, _ := os.ReadFile(filepath.Join(dir, "a.csv"))
	if !strings.Contains(string(data), "Unlisted Base,Unlisted,,,,0,") {
		t.Errorf("an unpriced item must have an empty price, not zero:\n%s", data)
	}
}

func TestServerErrorKeepsSnapshot(t *testing.T) {
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) { http.Error(w, "busy", http.StatusTooManyRequests) }))
	defer server.Close()
	if _, err := (&Ninja{Base: server.URL}).Stash(context.Background(), "x", "y"); err == nil {
		t.Fatal("a rate limit must be an error, never an empty market")
	}
}
