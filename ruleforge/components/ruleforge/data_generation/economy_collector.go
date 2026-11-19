package data_generation

import (
	"context"
	"errors"
	"fmt"
	"log"
	"time"
)

const CurrentEconomyVersion = "3.30"

// EconomyCategory makes experimental/new feeds explicit. Only 400/404 on an
// optional category is a capability limitation; other failures fail collection.
type EconomyCategory struct {
	Kind     string `toml:"Kind"`
	Class    string `toml:"Class"`
	Endpoint string `toml:"Endpoint"`
	Optional bool   `toml:"Optional"`
}

func DefaultEconomyCategories() []EconomyCategory {
	categories := append([]EconomyCategory(nil), economyCategories...)
	return append(categories,
		EconomyCategory{Kind: "Fragment", Class: "Map Fragments", Endpoint: "exchange", Optional: true},
		EconomyCategory{Kind: "AllflameEmber", Class: "Allflame Embers", Endpoint: "exchange", Optional: true},
		EconomyCategory{Kind: "Essence", Class: "Essences", Endpoint: "exchange", Optional: true},
		EconomyCategory{Kind: "Oil", Class: "Oils", Endpoint: "exchange", Optional: true},
		EconomyCategory{Kind: "Fossil", Class: "Delve", Endpoint: "exchange", Optional: true},
		EconomyCategory{Kind: "Viscera", Class: "Viscera", Endpoint: "stash", Optional: true},
	)
}

func waitContext(ctx context.Context, delay time.Duration) error {
	timer := time.NewTimer(delay)
	defer timer.Stop()
	select {
	case <-ctx.Done():
		return ctx.Err()
	case <-timer.C:
		return nil
	}
}

// CollectEconomy records only complete league fetches; raw observations from
// previous successful fetches survive a failed or unsupported category.
func CollectEconomy(ctx context.Context, client *PoeNinjaClient, store *EconomyHistoryStore, leagues []string, feeds []EconomyCategory, maxAge time.Duration, force bool, version string) error {
	if len(feeds) == 0 {
		return fmt.Errorf("no economy categories configured")
	}
	now := time.Now().UTC()
	var stale []string
	for _, league := range leagues {
		if !force && store != nil {
			at, ok, err := store.LatestSnapshotAt(league)
			if err != nil {
				return err
			}
			if ok && now.Sub(at) < maxAge {
				continue
			}
		}
		stale = append(stale, league)
	}
	if len(stale) == 0 {
		return nil
	}
	scoped := *client
	scoped.ctx = ctx
	resolved, _, err := scoped.ResolveLeagues(stale)
	if err != nil {
		return err
	}
	requests := 0
	var failures []error
	for _, league := range stale {
		id, ok := resolved[league]
		if !ok {
			continue
		}
		snapshot := EconomySnapshot{League: league, LeagueID: id, GameVersion: version, FetchedAt: now, Categories: map[string]string{}}
		failed := false
		for _, feed := range feeds {
			if requests > 0 {
				if err := waitContext(ctx, scoped.requestDelay); err != nil {
					return err
				}
			}
			requests++
			var items []EconomyCacheItem
			var err error
			switch feed.Endpoint {
			case "stash":
				items, err = scoped.FetchData("itemoverview", feed.Kind, id, feed.Class)
			case "exchange":
				items, err = scoped.FetchExchangeData(feed.Kind, id, feed.Class)
			default:
				err = fmt.Errorf("invalid economy endpoint %q", feed.Endpoint)
			}
			key := feed.Endpoint + ":" + feed.Kind
			if err != nil {
				if feed.Optional && unsupportedCategory(err) {
					snapshot.Categories[key] = "unsupported"
					log.Printf("Economy category %s unavailable for %s: %v", key, league, err)
					continue
				}
				failures = append(failures, fmt.Errorf("league %q category %q: %w", league, feed.Kind, err))
				failed = true
				break
			}
			snapshot.Categories[key] = "available"
			if len(items) == 0 {
				snapshot.Categories[key] = "empty"
			}
			snapshot.Items = append(snapshot.Items, items...)
		}
		if failed {
			continue
		}
		if len(snapshot.Items) == 0 {
			log.Printf("Economy league %q has no prices yet; retaining history", league)
			continue
		}
		if store == nil {
			return fmt.Errorf("economy history store is required")
		}
		if err := store.RecordCollection(snapshot); err != nil {
			return err
		}
		log.Printf("Recorded %d economy observations for %s", len(snapshot.Items), league)
	}
	return errors.Join(failures...)
}
