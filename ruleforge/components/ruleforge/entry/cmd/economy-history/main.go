// economy-history collects prices without loading PoB or compiling filters.
package main

import (
	"context"
	"errors"
	"flag"
	"fmt"
	"log"
	"math/rand/v2"
	"net/http"
	"os"
	"os/signal"
	"slices"
	"strings"
	"syscall"
	"time"

	"github.com/BurntSushi/toml"
	data "github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/data_generation"
)

type feedsConfig struct {
	Categories []data.EconomyCategory `toml:"Categories"`
}

func main() {
	if err := run(); err != nil {
		log.Fatal(err)
	}
}
func run() error {
	dbPath := flag.String("db", "/data/economy.db", "Persistent SQLite history")
	listen := flag.String("listen", ":8087", "Read-only HTTP service address")
	leagues := flag.String("leagues", "auto", "Comma-separated league names, or auto for active non-Ruthless trade leagues")
	interval := flag.Duration("interval", 4*time.Hour, "Minimum interval between snapshots of a league (at least 15m)")
	version := flag.String("game-version", data.CurrentEconomyVersion, "Patch regime recorded with observations")
	configFile := flag.String("feeds", "", "Optional TOML replacing the default category list")
	healthcheck := flag.String("healthcheck", "", "Check a service's /healthz and exit")
	flag.Parse()
	if *healthcheck != "" {
		c := http.Client{Timeout: 5 * time.Second}
		r, err := c.Get(strings.TrimRight(*healthcheck, "/") + "/healthz")
		if err != nil {
			return err
		}
		r.Body.Close()
		if r.StatusCode != 200 {
			return fmt.Errorf("health check: HTTP %d", r.StatusCode)
		}
		return nil
	}
	if *interval < 15*time.Minute {
		return fmt.Errorf("collection interval must be at least 15m")
	}
	if strings.TrimSpace(*version) == "" {
		return fmt.Errorf("game-version cannot be empty")
	}
	token := strings.TrimSpace(os.Getenv("RULEFORGE_HISTORY_TOKEN"))
	if path := os.Getenv("RULEFORGE_HISTORY_TOKEN_FILE"); path != "" {
		content, err := os.ReadFile(path)
		if err != nil {
			return fmt.Errorf("reading service token: %w", err)
		}
		token = strings.TrimSpace(string(content))
	}
	if len(token) < 24 {
		return fmt.Errorf("set RULEFORGE_HISTORY_TOKEN or RULEFORGE_HISTORY_TOKEN_FILE to a secret with at least 24 characters")
	}
	feeds := data.DefaultEconomyCategories()
	if *configFile != "" {
		var cfg feedsConfig
		meta, err := toml.DecodeFile(*configFile, &cfg)
		if err != nil {
			return err
		}
		if len(meta.Undecoded()) > 0 {
			return fmt.Errorf("unknown feed configuration keys: %v", meta.Undecoded())
		}
		feeds = cfg.Categories
	}
	if err := validateFeeds(feeds); err != nil {
		return err
	}
	store, err := data.NewEconomyHistoryStore(*dbPath)
	if err != nil {
		return err
	}
	defer store.Close()
	ctx, cancel := signal.NotifyContext(context.Background(), os.Interrupt, syscall.SIGTERM)
	defer cancel()
	server := &http.Server{Addr: *listen, Handler: data.NewHistoryHandler(store, token), ReadHeaderTimeout: 5 * time.Second, ReadTimeout: 15 * time.Second, WriteTimeout: 90 * time.Second, IdleTimeout: 60 * time.Second, MaxHeaderBytes: 16 << 10}
	serverErrors := make(chan error, 1)
	go func() { serverErrors <- server.ListenAndServe() }()
	done := make(chan struct{})
	go func() { defer close(done); collectLoop(ctx, store, *leagues, feeds, *interval, *version) }()
	log.Printf("Economy history listening on %s; interval %s; patch %s", *listen, *interval, *version)
	select {
	case <-ctx.Done():
	case err = <-serverErrors:
		cancel()
	}
	shutdownCtx, stop := context.WithTimeout(context.Background(), 10*time.Second)
	defer stop()
	shutdownErr := server.Shutdown(shutdownCtx)
	cancel()
	<-done
	if err != nil && !errors.Is(err, http.ErrServerClosed) {
		return err
	}
	return shutdownErr
}

func validateFeeds(feeds []data.EconomyCategory) error {
	if len(feeds) == 0 {
		return fmt.Errorf("no economy feeds configured")
	}
	seen := map[string]bool{}
	for _, feed := range feeds {
		key := feed.Endpoint + ":" + feed.Kind
		if feed.Kind == "" || feed.Class == "" || !slices.Contains([]string{"stash", "exchange"}, feed.Endpoint) || seen[key] {
			return fmt.Errorf("invalid or duplicate feed %q", key)
		}
		seen[key] = true
	}
	return nil
}

func collectLoop(ctx context.Context, store *data.EconomyHistoryStore, selected string, feeds []data.EconomyCategory, interval time.Duration, version string) {
	client := data.NewPoeNinjaClient()
	retry := 5 * time.Minute
	for ctx.Err() == nil {
		leagues := strings.Split(selected, ",")
		for i := range leagues {
			leagues[i] = strings.TrimSpace(leagues[i])
		}
		var err error
		if selected == "auto" {
			leagues, err = client.ActiveTradeLeagues(ctx)
		}
		if err == nil {
			err = data.CollectEconomy(ctx, client, store, leagues, feeds, interval, false, version)
		}
		if ctx.Err() != nil {
			return
		}
		if recordErr := store.RecordCollectionAttempt(err); recordErr != nil {
			log.Printf("Record collector status: %v", recordErr)
		}
		delay := interval
		if err != nil {
			log.Printf("Collection failed; retaining history: %v", err)
			delay = retry
			retry = min(retry*2, time.Hour)
		} else {
			retry = 5 * time.Minute
		}
		// Poll a little after the eligibility time; never jitter into overpolling.
		delay += time.Duration(rand.Int64N(max(int64(delay/10), 1)))
		timer := time.NewTimer(delay)
		select {
		case <-ctx.Done():
			timer.Stop()
			return
		case <-timer.C:
		}
	}
}
