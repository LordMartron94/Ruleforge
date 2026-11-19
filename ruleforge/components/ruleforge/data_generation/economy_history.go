package data_generation

import (
	"context"
	"database/sql"
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"time"

	"github.com/google/uuid"
	_ "modernc.org/sqlite"
)

// EconomySnapshot is an immutable collection. Missing categories are recorded
// explicitly; they never mean that every item in that category is worth zero.
type EconomySnapshot struct {
	ID          int64              `json:"id,omitempty"`
	League      string             `json:"league,omitempty"`
	LeagueID    string             `json:"league_id,omitempty"`
	GameVersion string             `json:"game_version,omitempty"`
	FetchedAt   time.Time          `json:"fetched_at"`
	Categories  map[string]string  `json:"categories,omitempty"`
	Items       []EconomyCacheItem `json:"items"`
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
 class TEXT NOT NULL, name TEXT NOT NULL, base_type TEXT NOT NULL,
 chaos_value REAL NOT NULL, divine_value REAL NOT NULL, exalted_value REAL NOT NULL,
 count INTEGER NOT NULL, listing_count INTEGER NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_snapshot_items_snapshot ON snapshot_items(snapshot_id);
CREATE TABLE IF NOT EXISTS history_metadata (key TEXT PRIMARY KEY, value TEXT NOT NULL);
CREATE TABLE IF NOT EXISTS history_sync (peer TEXT PRIMARY KEY, dataset TEXT NOT NULL, cursor INTEGER NOT NULL);
`

// EconomyHistoryStore owns its SQLite connection. WAL permits consistent reads
// while a collector records a new snapshot. Raw observations are never pruned.
type EconomyHistoryStore struct{ db *sql.DB }

func NewEconomyHistoryStore(path string) (*EconomyHistoryStore, error) {
	if dir := filepath.Dir(path); dir != "." {
		if err := os.MkdirAll(dir, 0755); err != nil {
			return nil, fmt.Errorf("creating economy history directory: %w", err)
		}
	}
	db, err := sql.Open("sqlite", path)
	if err != nil {
		return nil, err
	}
	db.SetMaxOpenConns(1)
	s := &EconomyHistoryStore{db: db}
	if err := s.initialize(); err != nil {
		db.Close()
		return nil, fmt.Errorf("initializing economy history: %w", err)
	}
	return s, nil
}

func (s *EconomyHistoryStore) initialize() error {
	for _, statement := range []string{"PRAGMA foreign_keys = ON", "PRAGMA busy_timeout = 5000", "PRAGMA journal_mode = WAL", economyHistorySchema} {
		if _, err := s.db.Exec(statement); err != nil {
			return err
		}
	}
	// Migrate existing histories in place, retaining their original data. An old
	// source is unknown, not proof that its listing_count was actual supply.
	for table, additions := range map[string]map[string]string{
		"snapshots":      {"league_id": "TEXT NOT NULL DEFAULT ''", "game_version": "TEXT NOT NULL DEFAULT ''", "categories": "TEXT NOT NULL DEFAULT '{}'"},
		"snapshot_items": {"source": "TEXT NOT NULL DEFAULT ''", "category": "TEXT NOT NULL DEFAULT ''", "item_id": "TEXT NOT NULL DEFAULT ''", "variant": "TEXT NOT NULL DEFAULT ''", "item_level": "INTEGER NOT NULL DEFAULT 0", "volume_chaos": "REAL NOT NULL DEFAULT 0", "traded_units": "REAL NOT NULL DEFAULT 0"},
	} {
		rows, err := s.db.Query("PRAGMA table_info(" + table + ")")
		if err != nil {
			return err
		}
		existing := map[string]bool{}
		for rows.Next() {
			var id, notnull, primary int
			var name, kind string
			var defaultValue any
			if err := rows.Scan(&id, &name, &kind, &notnull, &defaultValue, &primary); err != nil {
				rows.Close()
				return err
			}
			existing[name] = true
		}
		err = rows.Err()
		rows.Close()
		if err != nil {
			return err
		}
		for column, definition := range additions {
			if !existing[column] {
				if _, err := s.db.Exec("ALTER TABLE " + table + " ADD COLUMN " + column + " " + definition); err != nil {
					return err
				}
			}
		}
	}
	_, err := s.db.Exec("INSERT OR IGNORE INTO history_metadata(key,value) VALUES ('dataset',?)", uuid.NewString())
	return err
}

func (s *EconomyHistoryStore) Close() error { return s.db.Close() }

func (s *EconomyHistoryStore) Dataset() (string, error) {
	var id string
	err := s.db.QueryRow("SELECT value FROM history_metadata WHERE key = 'dataset'").Scan(&id)
	return id, err
}

func (s *EconomyHistoryStore) RecordSnapshot(league string, at time.Time, items []EconomyCacheItem) error {
	return s.RecordCollection(EconomySnapshot{League: league, FetchedAt: at, Items: items})
}

func (s *EconomyHistoryStore) RecordCollection(snapshot EconomySnapshot) error {
	tx, err := s.db.Begin()
	if err != nil {
		return err
	}
	defer tx.Rollback()
	if err := insertSnapshot(tx, snapshot); err != nil {
		return err
	}
	return tx.Commit()
}

func insertSnapshot(tx *sql.Tx, snapshot EconomySnapshot) error {
	if snapshot.League == "" || snapshot.FetchedAt.IsZero() {
		return fmt.Errorf("snapshot requires league and timestamp")
	}
	categories, err := json.Marshal(snapshot.Categories)
	if err != nil {
		return err
	}
	result, err := tx.Exec(`INSERT INTO snapshots(league,fetched_at,league_id,game_version,categories) VALUES(?,?,?,?,?) ON CONFLICT(league,fetched_at) DO NOTHING`, snapshot.League, snapshot.FetchedAt.UTC(), snapshot.LeagueID, snapshot.GameVersion, string(categories))
	if err != nil {
		return err
	}
	changed, err := result.RowsAffected()
	if err != nil || changed == 0 {
		return err
	}
	id, err := result.LastInsertId()
	if err != nil {
		return err
	}
	stmt, err := tx.Prepare(`INSERT INTO snapshot_items(snapshot_id,class,name,base_type,chaos_value,divine_value,exalted_value,count,listing_count,source,category,item_id,variant,item_level,volume_chaos,traded_units) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)`)
	if err != nil {
		return err
	}
	defer stmt.Close()
	for _, item := range snapshot.Items {
		if !validPrice(item.ChaosValue) || item.Name == "" || item.BaseType == "" {
			return fmt.Errorf("invalid price observation for %q", item.Name)
		}
		if _, err := stmt.Exec(id, item.Class, item.Name, item.BaseType, item.ChaosValue, item.DivineValue, item.ExaltedValue, item.Count, item.ListingCount, item.Source, item.Category, item.ItemID, item.Variant, item.ItemLevel, item.VolumeChaos, item.TradedUnits); err != nil {
			return err
		}
	}
	return nil
}

func (s *EconomyHistoryStore) LatestSnapshotAt(league string) (time.Time, bool, error) {
	var at time.Time
	err := s.db.QueryRow("SELECT fetched_at FROM snapshots WHERE league = ? ORDER BY fetched_at DESC LIMIT 1", league).Scan(&at)
	if err == sql.ErrNoRows {
		return time.Time{}, false, nil
	}
	return at.UTC(), err == nil, err
}

const snapshotSelect = `SELECT id,league,fetched_at,league_id,game_version,categories FROM snapshots `

// readSnapshots completes the metadata query before loading items: the store's
// single connection must not be held by open rows while issuing another query.
func (s *EconomyHistoryStore) readSnapshots(ctx context.Context, query string, args ...any) ([]EconomySnapshot, error) {
	rows, err := s.db.QueryContext(ctx, query, args...)
	if err != nil {
		return nil, err
	}
	snapshots := []EconomySnapshot{}
	for rows.Next() {
		var v EconomySnapshot
		var categories string
		if err := rows.Scan(&v.ID, &v.League, &v.FetchedAt, &v.LeagueID, &v.GameVersion, &categories); err != nil {
			rows.Close()
			return nil, err
		}
		if err := json.Unmarshal([]byte(categories), &v.Categories); err != nil {
			rows.Close()
			return nil, err
		}
		snapshots = append(snapshots, v)
	}
	err = rows.Err()
	rows.Close()
	if err != nil {
		return nil, err
	}
	for i := range snapshots {
		v := &snapshots[i]
		items, err := s.db.QueryContext(ctx, `SELECT class,name,base_type,chaos_value,divine_value,exalted_value,count,listing_count,source,category,item_id,variant,item_level,volume_chaos,traded_units FROM snapshot_items WHERE snapshot_id = ? ORDER BY rowid`, v.ID)
		if err != nil {
			return nil, err
		}
		v.Items = []EconomyCacheItem{}
		for items.Next() {
			var item EconomyCacheItem
			if err := items.Scan(&item.Class, &item.Name, &item.BaseType, &item.ChaosValue, &item.DivineValue, &item.ExaltedValue, &item.Count, &item.ListingCount, &item.Source, &item.Category, &item.ItemID, &item.Variant, &item.ItemLevel, &item.VolumeChaos, &item.TradedUnits); err != nil {
				items.Close()
				return nil, err
			}
			v.Items = append(v.Items, item)
		}
		err = items.Err()
		items.Close()
		if err != nil {
			return nil, err
		}
	}
	return snapshots, nil
}

func (s *EconomyHistoryStore) Window(league string, since time.Time) ([]EconomySnapshot, error) {
	return s.readSnapshots(context.Background(), snapshotSelect+"WHERE league = ? AND fetched_at >= ? ORDER BY fetched_at,id", league, since.UTC())
}
