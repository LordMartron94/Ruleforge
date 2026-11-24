package provider

import (
	"context"
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"time"

	"github.com/LordMartron94/Ruleforge/studio/internal/config"
	"github.com/LordMartron94/Ruleforge/studio/internal/history"
)

func HistoryPath(project *config.Project) string {
	return filepath.Join(project.Dir, ".ruleforge", "economy", "history.json")
}

// PrepareHistory synchronizes the existing protocol and exposes patch-filtered buckets as a Ruleforge dataset.
func PrepareHistory(ctx context.Context, project *config.Project) error {
	if project.EconomyHistory == nil {
		return nil
	}
	cfg := project.EconomyHistory
	if len(cfg.Leagues) == 0 {
		return fmt.Errorf("economyHistory.leagues is required")
	}
	policy := cfg.Memory
	if policy == nil {
		policy = config.DefaultEconomyMemory()
	}
	if err := policy.Validate(); err != nil {
		return err
	}
	dbPath := cfg.Database
	if dbPath == "" {
		dbPath = ".ruleforge/economy/history.db"
	}
	if !filepath.IsAbs(dbPath) {
		dbPath = filepath.Join(project.Dir, dbPath)
	}
	store, err := history.NewEconomyHistoryStore(dbPath)
	if err != nil {
		return err
	}
	defer store.Close()
	if cfg.URL != "" {
		if cfg.TokenEnv == "" || os.Getenv(cfg.TokenEnv) == "" {
			return fmt.Errorf("economyHistory.tokenEnv must name a populated environment variable")
		}
		if err := history.SyncHistory(ctx, store, cfg.URL, os.Getenv(cfg.TokenEnv), cfg.Leagues); err != nil {
			return err
		}
	}
	since, _ := time.Parse(time.RFC3339, policy.Since)
	raw := map[string][]history.EconomySnapshot{}
	for _, league := range cfg.Leagues {
		rows, err := store.Window(league, since)
		if err != nil {
			return err
		}
		raw[league] = rows
	}
	buckets, evidence := history.PrepareEconomyHistory(raw, *policy, time.Now().UTC())
	index := map[string]history.EconomyEvidence{}
	for _, entry := range evidence {
		index[entry.League+"\x00"+entry.Class+"\x00"+entry.BaseType] = entry
	}
	rows := []map[string]any{}
	for _, league := range cfg.Leagues {
		for _, snapshot := range buckets[league] {
			for _, item := range snapshot.Items {
				ev := index[league+"\x00"+item.Class+"\x00"+item.BaseType]
				rows = append(rows, map[string]any{"id": item.ItemID, "name": item.Name, "baseType": item.BaseType, "class": item.Class, "variant": item.Variant, "league": league, "gameVersion": snapshot.GameVersion, "bucket": snapshot.FetchedAt.Unix(), "chaosValue": item.ChaosValue, "divineValue": item.DivineValue, "listingCount": item.ListingCount, "tradedUnits": item.TradedUnits, "source": item.Source, "state": ev.State, "reliableBuckets": ev.ReliableBuckets})
			}
		}
	}
	data, err := json.Marshal(rows)
	if err != nil {
		return err
	}
	return writeSnapshot(HistoryPath(project), data)
}
