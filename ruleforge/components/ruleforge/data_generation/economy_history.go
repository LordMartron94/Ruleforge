package data_generation

import (
	"database/sql"
	"fmt"
	"os"
	"path/filepath"
	"sort"
	"time"

	_ "modernc.org/sqlite"
)

// EconomySnapshot is one poe.ninja fetch for a single league, recorded at FetchedAt.
type EconomySnapshot struct {
	FetchedAt time.Time
	Items     []EconomyCacheItem
}

const economyHistorySchema = `
CREATE TABLE IF NOT EXISTS snapshots (
	id INTEGER PRIMARY KEY AUTOINCREMENT,
	league TEXT NOT NULL,
	fetched_at TIMESTAMP NOT NULL,
	UNIQUE(league, fetched_at)
);
CREATE INDEX IF NOT EXISTS idx_snapshots_league_time ON snapshots(league, fetched_at);

CREATE TABLE IF NOT EXISTS snapshot_items (
	snapshot_id INTEGER NOT NULL REFERENCES snapshots(id) ON DELETE CASCADE,
	class TEXT NOT NULL,
	name TEXT NOT NULL,
	base_type TEXT NOT NULL,
	chaos_value REAL NOT NULL,
	divine_value REAL NOT NULL,
	exalted_value REAL NOT NULL,
	count INTEGER NOT NULL,
	listing_count INTEGER NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_snapshot_items_snapshot ON snapshot_items(snapshot_id);
`

// EconomyHistoryStore persists poe.ninja economy snapshots over time in SQLite,
// so tiering can be computed from a rolling window instead of a single fetch.
type EconomyHistoryStore struct {
	db *sql.DB
}

// NewEconomyHistoryStore opens (creating if needed) the SQLite database at path
// and ensures its schema exists.
func NewEconomyHistoryStore(path string) (*EconomyHistoryStore, error) {
	if dir := filepath.Dir(path); dir != "." {
		if err := os.MkdirAll(dir, 0755); err != nil {
			return nil, fmt.Errorf("creating economy history directory %q: %w", dir, err)
		}
	}

	db, err := sql.Open("sqlite", path)
	if err != nil {
		return nil, fmt.Errorf("opening economy history database %q: %w", path, err)
	}
	// SQLite (via modernc's driver) does not handle concurrent writers well,
	// and Ruleforge only ever accesses the database from a single goroutine.
	// One connection also guarantees the foreign-key pragma below stays applied.
	db.SetMaxOpenConns(1)

	if _, err := db.Exec("PRAGMA foreign_keys = ON;"); err != nil {
		db.Close()
		return nil, fmt.Errorf("enabling foreign keys: %w", err)
	}
	if _, err := db.Exec(economyHistorySchema); err != nil {
		db.Close()
		return nil, fmt.Errorf("creating economy history schema: %w", err)
	}

	return &EconomyHistoryStore{db: db}, nil
}

// Close releases the underlying database handle.
func (s *EconomyHistoryStore) Close() error {
	return s.db.Close()
}

// RecordSnapshot stores one fetch's worth of economy items for a league.
func (s *EconomyHistoryStore) RecordSnapshot(league string, fetchedAt time.Time, items []EconomyCacheItem) error {
	tx, err := s.db.Begin()
	if err != nil {
		return fmt.Errorf("beginning transaction: %w", err)
	}
	defer tx.Rollback()

	result, err := tx.Exec(`INSERT INTO snapshots (league, fetched_at) VALUES (?, ?)`, league, fetchedAt.UTC())
	if err != nil {
		return fmt.Errorf("inserting snapshot for league %q: %w", league, err)
	}
	snapshotID, err := result.LastInsertId()
	if err != nil {
		return fmt.Errorf("reading snapshot id: %w", err)
	}

	stmt, err := tx.Prepare(`INSERT INTO snapshot_items
		(snapshot_id, class, name, base_type, chaos_value, divine_value, exalted_value, count, listing_count)
		VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)`)
	if err != nil {
		return fmt.Errorf("preparing snapshot item insert: %w", err)
	}
	defer stmt.Close()

	for _, item := range items {
		if _, err := stmt.Exec(
			snapshotID, item.Class, item.Name, item.BaseType,
			item.ChaosValue, item.DivineValue, item.ExaltedValue,
			item.Count, item.ListingCount,
		); err != nil {
			return fmt.Errorf("inserting snapshot item %q: %w", item.Name, err)
		}
	}

	return tx.Commit()
}

// LatestSnapshotAt returns the timestamp of the most recent snapshot recorded
// for league, or ok=false if none exist yet.
func (s *EconomyHistoryStore) LatestSnapshotAt(league string) (time.Time, bool, error) {
	var fetchedAt time.Time
	err := s.db.QueryRow(`SELECT fetched_at FROM snapshots WHERE league = ? ORDER BY fetched_at DESC LIMIT 1`, league).Scan(&fetchedAt)
	if err == sql.ErrNoRows {
		return time.Time{}, false, nil
	}
	if err != nil {
		return time.Time{}, false, fmt.Errorf("querying latest snapshot for league %q: %w", league, err)
	}
	return fetchedAt.UTC(), true, nil
}

// Window returns every snapshot recorded for league at or after since, ordered
// oldest first.
func (s *EconomyHistoryStore) Window(league string, since time.Time) ([]EconomySnapshot, error) {
	rows, err := s.db.Query(`
		SELECT s.id, s.fetched_at, i.class, i.name, i.base_type, i.chaos_value, i.divine_value, i.exalted_value, i.count, i.listing_count
		FROM snapshots s
		JOIN snapshot_items i ON i.snapshot_id = s.id
		WHERE s.league = ? AND s.fetched_at >= ?
		ORDER BY s.fetched_at ASC, s.id ASC`, league, since.UTC())
	if err != nil {
		return nil, fmt.Errorf("querying economy window for league %q: %w", league, err)
	}
	defer rows.Close()

	var snapshots []EconomySnapshot
	var currentID int64 = -1
	for rows.Next() {
		var id int64
		var fetchedAt time.Time
		var item EconomyCacheItem
		if err := rows.Scan(&id, &fetchedAt, &item.Class, &item.Name, &item.BaseType,
			&item.ChaosValue, &item.DivineValue, &item.ExaltedValue, &item.Count, &item.ListingCount); err != nil {
			return nil, fmt.Errorf("scanning economy window row: %w", err)
		}
		if id != currentID {
			snapshots = append(snapshots, EconomySnapshot{FetchedAt: fetchedAt.UTC()})
			currentID = id
		}
		last := &snapshots[len(snapshots)-1]
		last.Items = append(last.Items, item)
	}
	if err := rows.Err(); err != nil {
		return nil, fmt.Errorf("reading economy window rows: %w", err)
	}

	sort.Slice(snapshots, func(i, j int) bool { return snapshots[i].FetchedAt.Before(snapshots[j].FetchedAt) })
	return snapshots, nil
}
