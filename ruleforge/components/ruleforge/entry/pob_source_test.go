package main

import (
	"context"
	"encoding/json"
	"fmt"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"strings"
	"sync/atomic"
	"testing"
	"time"
)

const firstCommit = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
const secondCommit = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"

func mockPoB(t *testing.T, revision *string, broken *bool, downloads *atomic.Int32) (*httptest.Server, pobSource) {
	t.Helper()
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if *broken {
			http.Error(w, "mock upstream unavailable", http.StatusServiceUnavailable)
			return
		}
		switch {
		case r.URL.Path == "/api/commits/master":
			_ = json.NewEncoder(w).Encode(map[string]string{"sha": *revision})
		case strings.HasPrefix(r.URL.Path, "/api/contents/src/Data/"):
			dir := strings.TrimPrefix(r.URL.Path, "/api/contents/src/Data/")
			var names []string
			switch dir {
			case "Bases":
				names = []string{"axe.lua"}
			case "Uniques":
				names = []string{"axe.lua"}
			case "Uniques/Special":
				names = []string{"New.lua", "Generated.lua"}
			default:
				http.NotFound(w, r)
				return
			}
			entries := make([]map[string]string, 0, len(names))
			for _, name := range names {
				entries = append(entries, map[string]string{"name": name, "type": "file"})
			}
			_ = json.NewEncoder(w).Encode(entries)
		case strings.HasPrefix(r.URL.Path, "/raw/"):
			downloads.Add(1)
			_, _ = fmt.Fprintln(w, "-- Lua source for "+r.URL.Path)
		default:
			http.NotFound(w, r)
		}
	}))
	return server, pobSource{client: server.Client(), apiBase: server.URL + "/api", rawBase: server.URL + "/raw"}
}

func TestPoBSnapshotFirstDownloadReuseAndRefresh(t *testing.T) {
	revision := firstCommit
	broken := false
	var downloads atomic.Int32
	server, source := mockPoB(t, &revision, &broken, &downloads)
	defer server.Close()
	dir := filepath.Join(t.TempDir(), "pob")
	ctx := context.Background()
	changed, err := source.ensure(ctx, dir, false)
	if err != nil || !changed {
		t.Fatalf("initial download: changed=%v, err=%v", changed, err)
	}
	count := downloads.Load()
	if count != 6 {
		t.Fatalf("expected 6 files, got %d", count)
	}
	snap, err := readPoBSnapshot(dir)
	if err != nil || snap.Commit != revision || !completePoBSnapshot(dir, snap) {
		t.Fatalf("invalid initial snapshot: %+v %v", snap, err)
	}
	changed, err = source.ensure(ctx, dir, false)
	if err != nil || changed || downloads.Load() != count {
		t.Fatalf("fresh-cache reuse failed: %v %v", changed, err)
	}
	snap.CheckedAt = time.Now().Add(-8 * 24 * time.Hour)
	bytes, _ := json.Marshal(snap)
	if err := os.WriteFile(filepath.Join(dir, ".ruleforge-source.json"), bytes, 0644); err != nil {
		t.Fatal(err)
	}
	changed, err = source.ensure(ctx, dir, false)
	if err != nil || changed || downloads.Load() != count {
		t.Fatalf("unchanged remote commit redownloaded: %v %v", changed, err)
	}
	revision = secondCommit
	changed, err = source.ensure(ctx, dir, true)
	if err != nil || !changed || downloads.Load() != 2*count {
		t.Fatalf("changed remote commit not downloaded: %v %v", changed, err)
	}
	updated, err := readPoBSnapshot(dir)
	if err != nil || updated.Commit != secondCommit || !completePoBSnapshot(dir, updated) {
		t.Fatalf("invalid refreshed snapshot: %+v %v", updated, err)
	}
}

func TestPoBSnapshotOfflineFallbackAndForcedFailure(t *testing.T) {
	revision := firstCommit
	broken := false
	var downloads atomic.Int32
	server, source := mockPoB(t, &revision, &broken, &downloads)
	defer server.Close()
	dir := filepath.Join(t.TempDir(), "pob")
	if _, err := source.ensure(context.Background(), dir, false); err != nil {
		t.Fatal(err)
	}
	snap, _ := readPoBSnapshot(dir)
	snap.CheckedAt = time.Now().Add(-8 * 24 * time.Hour)
	bytes, _ := json.Marshal(snap)
	if err := os.WriteFile(filepath.Join(dir, ".ruleforge-source.json"), bytes, 0644); err != nil {
		t.Fatal(err)
	}
	broken = true
	changed, err := source.ensure(context.Background(), dir, false)
	if changed || err != nil {
		t.Fatalf("offline fallback: %v %v", changed, err)
	}
	if _, err := source.ensure(context.Background(), dir, true); err == nil || !strings.Contains(err.Error(), "HTTP 503") {
		t.Fatalf("forced refresh should surface HTTP 503: %v", err)
	}
	if !completePoBSnapshot(dir, snap) {
		t.Fatal("offline fallback damaged cached data")
	}
}

func TestPoBSnapshotRefusesExistingUnmanagedDirectory(t *testing.T) {
	dir := filepath.Join(t.TempDir(), "existing")
	if err := os.MkdirAll(dir, 0755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(dir, "personal.txt"), []byte("keep"), 0644); err != nil {
		t.Fatal(err)
	}
	source := pobSource{}
	_, err := source.ensure(context.Background(), dir, false)
	if err == nil || !strings.Contains(err.Error(), "not a complete Ruleforge-managed") {
		t.Fatalf("unmanaged data not refused: %v", err)
	}
	if _, err := os.Stat(filepath.Join(dir, "personal.txt")); err != nil {
		t.Fatal("personal data deleted:", err)
	}
}
