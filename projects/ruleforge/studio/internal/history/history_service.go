package history

import (
	"context"
	"crypto/subtle"
	"database/sql"
	"encoding/json"
	"fmt"
	"io"
	"log"
	"net/http"
	"net/url"
	"slices"
	"strconv"
	"strings"
	"time"
)

const HistoryProtocolVersion = 1

// HistoryPage uses a bounded high watermark, so one synchronization terminates
// even while new snapshots arrive. Cursors belong to a dataset, not a host name.
type HistoryPage struct {
	Version   int               `json:"version"`
	Dataset   string            `json:"dataset"`
	Cursor    int64             `json:"cursor"`
	Through   int64             `json:"through"`
	Snapshots []EconomySnapshot `json:"snapshots"`
}

type HistoryLeagueStatus struct {
	League     string            `json:"league"`
	Snapshots  int               `json:"snapshots"`
	Latest     time.Time         `json:"latest"`
	Categories map[string]string `json:"categories"`
}

type HistoryStatus struct {
	Version     int                   `json:"version"`
	Dataset     string                `json:"dataset"`
	Leagues     []HistoryLeagueStatus `json:"leagues"`
	LastAttempt string                `json:"last_attempt,omitempty"`
	LastError   string                `json:"last_error,omitempty"`
}

func (s *EconomyHistoryStore) RecordCollectionAttempt(err error) error {
	message := ""
	if err != nil {
		message = err.Error()
	}
	tx, e := s.db.Begin()
	if e != nil {
		return e
	}
	defer tx.Rollback()
	for key, value := range map[string]string{"last_attempt": time.Now().UTC().Format(time.RFC3339), "last_error": message} {
		if _, e := tx.Exec("INSERT INTO history_metadata(key,value) VALUES(?,?) ON CONFLICT(key) DO UPDATE SET value=excluded.value", key, value); e != nil {
			return e
		}
	}
	return tx.Commit()
}

func (s *EconomyHistoryStore) Status(ctx context.Context) (HistoryStatus, error) {
	status := HistoryStatus{Version: HistoryProtocolVersion, Leagues: []HistoryLeagueStatus{}}
	var err error
	status.Dataset, err = s.Dataset()
	if err != nil {
		return status, err
	}
	rows, err := s.db.QueryContext(ctx, `SELECT s.league,(SELECT count(*) FROM snapshots n WHERE n.league=s.league),s.fetched_at,s.categories FROM snapshots s WHERE s.id=(SELECT n.id FROM snapshots n WHERE n.league=s.league ORDER BY n.fetched_at DESC,n.id DESC LIMIT 1) ORDER BY s.league`)
	if err != nil {
		return status, err
	}
	for rows.Next() {
		var v HistoryLeagueStatus
		var categories string
		if err := rows.Scan(&v.League, &v.Snapshots, &v.Latest, &categories); err != nil {
			rows.Close()
			return status, err
		}
		if err := json.Unmarshal([]byte(categories), &v.Categories); err != nil {
			rows.Close()
			return status, err
		}
		status.Leagues = append(status.Leagues, v)
	}
	err = rows.Err()
	rows.Close()
	if err != nil {
		return status, err
	}
	for key, target := range map[string]*string{"last_attempt": &status.LastAttempt, "last_error": &status.LastError} {
		err = s.db.QueryRowContext(ctx, "SELECT value FROM history_metadata WHERE key=?", key).Scan(target)
		if err != nil && err != sql.ErrNoRows {
			return status, err
		}
	}
	return status, nil
}

func (s *EconomyHistoryStore) ExportPage(ctx context.Context, after, through int64, limit int, leagues []string) (HistoryPage, error) {
	page := HistoryPage{Version: HistoryProtocolVersion, Cursor: after, Through: through, Snapshots: []EconomySnapshot{}}
	var err error
	page.Dataset, err = s.Dataset()
	if err != nil {
		return page, err
	}
	if through == 0 {
		if err := s.db.QueryRowContext(ctx, "SELECT COALESCE(MAX(id),0) FROM snapshots").Scan(&page.Through); err != nil {
			return page, err
		}
	}
	if after < 0 || after > page.Through || limit < 1 || limit > 4 || len(leagues) > 16 {
		return page, fmt.Errorf("invalid history cursor or page size")
	}
	query := snapshotSelect + "WHERE id > ? AND id <= ?"
	args := []any{after, page.Through}
	if len(leagues) > 0 {
		query += " AND league IN (" + strings.TrimSuffix(strings.Repeat("?,", len(leagues)), ",") + ")"
		for _, l := range leagues {
			args = append(args, l)
		}
	}
	query += " ORDER BY id LIMIT ?"
	args = append(args, limit)
	page.Snapshots, err = s.readSnapshots(ctx, query, args...)
	if err != nil {
		return page, err
	}
	if len(page.Snapshots) > 0 {
		page.Cursor = page.Snapshots[len(page.Snapshots)-1].ID
	} else {
		page.Cursor = page.Through
	}
	return page, nil
}

// NewHistoryHandler exposes read-only endpoints. A token is required for every
// data request; the liveness endpoint discloses no history. Bind on a trusted
// interface or put TLS in front of the service for access across the internet.
func NewHistoryHandler(store *EconomyHistoryStore, token string) http.Handler {
	mux := http.NewServeMux()
	mux.HandleFunc("GET /healthz", func(w http.ResponseWriter, r *http.Request) {
		if _, err := store.Dataset(); err != nil {
			http.Error(w, "history database unavailable", 503)
			return
		}
		w.WriteHeader(200)
		io.WriteString(w, "ok\n")
	})
	auth := func(handler http.HandlerFunc) http.HandlerFunc {
		return func(w http.ResponseWriter, r *http.Request) {
			supplied := strings.TrimPrefix(r.Header.Get("Authorization"), "Bearer ")
			if !strings.HasPrefix(r.Header.Get("Authorization"), "Bearer ") || token == "" || subtle.ConstantTimeCompare([]byte(supplied), []byte(token)) != 1 {
				w.Header().Set("WWW-Authenticate", "Bearer")
				http.Error(w, "unauthorized", 401)
				return
			}
			w.Header().Set("Content-Type", "application/json")
			w.Header().Set("Cache-Control", "no-store")
			handler(w, r)
		}
	}
	mux.HandleFunc("GET /v1/status", auth(func(w http.ResponseWriter, r *http.Request) {
		v, err := store.Status(r.Context())
		if err != nil {
			http.Error(w, "history status unavailable", 500)
			return
		}
		json.NewEncoder(w).Encode(v)
	}))
	mux.HandleFunc("GET /v1/snapshots", auth(func(w http.ResponseWriter, r *http.Request) {
		query := r.URL.Query()
		after, err := parseCursor(query.Get("after"))
		if err != nil {
			http.Error(w, err.Error(), 400)
			return
		}
		through, err := parseCursor(query.Get("through"))
		if err != nil {
			http.Error(w, err.Error(), 400)
			return
		}
		limit := 1
		if query.Has("limit") {
			limit, err = strconv.Atoi(query.Get("limit"))
			if err != nil {
				http.Error(w, "invalid limit", 400)
				return
			}
		}
		if limit < 1 || limit > 4 || len(query["league"]) > 16 {
			http.Error(w, "invalid page size or league count", 400)
			return
		}
		dataset, err := store.Dataset()
		if err != nil {
			http.Error(w, "history unavailable", 500)
			return
		}
		if expected := query.Get("dataset"); expected != "" && expected != dataset {
			http.Error(w, "history dataset changed; restart synchronization", 409)
			return
		}
		page, err := store.ExportPage(r.Context(), after, through, limit, query["league"])
		if err != nil {
			http.Error(w, err.Error(), 400)
			return
		}
		json.NewEncoder(w).Encode(page)
	}))
	return mux
}

func parseCursor(value string) (int64, error) {
	if value == "" {
		return 0, nil
	}
	n, err := strconv.ParseInt(value, 10, 64)
	if err != nil || n < 0 {
		return 0, fmt.Errorf("invalid history cursor")
	}
	return n, nil
}

// SyncHistory imports each page and its checkpoint in one transaction. A failed
// page may be retried without duplicating snapshots or advancing past data.
func SyncHistory(ctx context.Context, store *EconomyHistoryStore, address, token string, leagues []string) error {
	base, err := url.Parse(address)
	if err != nil || base.Host == "" || (base.Scheme != "http" && base.Scheme != "https") || base.User != nil || base.RawQuery != "" || base.Fragment != "" {
		return fmt.Errorf("invalid economy history URL")
	}
	if len(leagues) == 0 || len(leagues) > 16 {
		return fmt.Errorf("history sync requires 1 to 16 league names")
	}
	leagues = append([]string(nil), leagues...)
	slices.Sort(leagues)
	peer := strings.TrimRight(base.String(), "/") + "|" + strings.Join(leagues, "\x1f")
	client := &http.Client{Timeout: 90 * time.Second, CheckRedirect: func(req *http.Request, via []*http.Request) error { return http.ErrUseLastResponse }}
	get := func(path string, target any) error {
		req, err := http.NewRequestWithContext(ctx, "GET", strings.TrimRight(address, "/")+path, nil)
		if err != nil {
			return err
		}
		if token != "" {
			req.Header.Set("Authorization", "Bearer "+token)
		}
		response, err := client.Do(req)
		if err != nil {
			return fmt.Errorf("history service request failed: %w", err)
		}
		defer response.Body.Close()
		if response.StatusCode != 200 {
			return fmt.Errorf("history service HTTP %d", response.StatusCode)
		}
		return json.NewDecoder(io.LimitReader(response.Body, 64<<20)).Decode(target)
	}
	var status HistoryStatus
	if err := get("/v1/status", &status); err != nil {
		return err
	}
	if status.LastError != "" {
		log.Printf("WARNING: economy collector reports a failed collection; synchronizing retained history: %s", status.LastError)
	}
	if status.Version != HistoryProtocolVersion || status.Dataset == "" {
		return fmt.Errorf("unsupported history service protocol")
	}
	var dataset string
	var cursor int64
	err = store.db.QueryRow("SELECT dataset,cursor FROM history_sync WHERE peer=?", peer).Scan(&dataset, &cursor)
	if err != nil && err != sql.ErrNoRows {
		return err
	}
	if dataset != status.Dataset {
		cursor = 0
	}
	through := int64(0)
	for pages := 0; pages < 100000; pages++ {
		query := url.Values{"after": {strconv.FormatInt(cursor, 10)}, "limit": {"1"}, "dataset": {status.Dataset}}
		if through > 0 {
			query.Set("through", strconv.FormatInt(through, 10))
		}
		for _, l := range leagues {
			query.Add("league", l)
		}
		var page HistoryPage
		if err := get("/v1/snapshots?"+query.Encode(), &page); err != nil {
			return err
		}
		if page.Version != HistoryProtocolVersion || page.Dataset != status.Dataset || page.Cursor < cursor || page.Cursor > page.Through || (through > 0 && page.Through != through) {
			return fmt.Errorf("invalid history page")
		}
		previous := cursor
		for _, snapshot := range page.Snapshots {
			if snapshot.ID <= previous || snapshot.ID > page.Cursor || !slices.Contains(leagues, snapshot.League) {
				return fmt.Errorf("invalid snapshot order or league")
			}
			previous = snapshot.ID
		}
		if len(page.Snapshots) > 0 && previous != page.Cursor {
			return fmt.Errorf("history cursor does not end at last snapshot")
		}
		if page.Cursor == cursor && page.Cursor != page.Through {
			return fmt.Errorf("history synchronization made no progress")
		}
		tx, err := store.db.BeginTx(ctx, nil)
		if err != nil {
			return err
		}
		for _, snapshot := range page.Snapshots {
			if err = insertSnapshot(tx, snapshot); err != nil {
				break
			}
		}
		if err == nil {
			_, err = tx.Exec("INSERT INTO history_sync(peer,dataset,cursor) VALUES(?,?,?) ON CONFLICT(peer) DO UPDATE SET dataset=excluded.dataset,cursor=excluded.cursor", peer, page.Dataset, page.Cursor)
		}
		if err != nil {
			tx.Rollback()
			return err
		}
		if err = tx.Commit(); err != nil {
			return err
		}
		cursor, through = page.Cursor, page.Through
		if cursor == through {
			return nil
		}
	}
	return fmt.Errorf("history synchronization exceeded page limit; rerun to resume")
}
