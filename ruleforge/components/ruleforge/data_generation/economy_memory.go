package data_generation

import (
	"database/sql"
	"encoding/json"
	"errors"
	"fmt"
	"sort"
	"strings"
	"time"

	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/config"
)

// EconomyEvidence describes what was actually seen, without asserting that
// snapshots are independent transactions. ReliableBuckets are time buckets
// meeting provider-specific liquidity thresholds, per league, never summed
// across leagues to manufacture confidence in a thin current-league market.
type EconomyEvidence struct {
	League           string    `json:"league"`
	Class            string    `json:"class"`
	BaseType         string    `json:"base"`
	LastSeen         time.Time `json:"lastSeen"`
	LastReliableSeen time.Time `json:"lastReliableSeen,omitempty"`
	Buckets          int       `json:"buckets"`
	ReliableBuckets  int       `json:"reliableBuckets"`
	Source           string    `json:"source"`
	State            string    `json:"state"`
	Listings         int       `json:"listings,omitempty"`
	TradedUnits      float64   `json:"tradedUnits,omitempty"`
}

func supplyKnown(item EconomyCacheItem) bool {
	return item.ListingCount > 0 && (item.Source == "stash" || (item.Source == "" && (item.Class == "Uniques" || item.Class == "Gems")))
}
func reliableObservation(item EconomyCacheItem, policy config.EconomyMemory) bool {
	if item.Source == "exchange" {
		return item.TradedUnits >= policy.MinimumTradedUnits
	}
	return supplyKnown(item) && item.ListingCount >= policy.MinimumListings
}

// PrepareEconomyHistory keeps the latest observation of each exact variant in
// a time bucket. It filters patch regimes and stale bases without deleting raw
// history, and retains older observations for fresh bases as a price prior.
func PrepareEconomyHistory(history map[string][]EconomySnapshot, policy config.EconomyMemory, now time.Time) (map[string][]EconomySnapshot, []EconomyEvidence) {
	since, _ := time.Parse(time.RFC3339, policy.Since)
	bucket := time.Duration(policy.BucketHours * float64(time.Hour))
	maxAge := time.Duration(policy.MaxAgeHours * float64(time.Hour))
	type observed struct {
		at   time.Time
		item EconomyCacheItem
	}
	result := map[string][]EconomySnapshot{}
	var evidence []EconomyEvidence
	for _, league := range sortedKeys(history) {
		// Key includes identity and provider. Different variants are never merged
		// into one observation just because they share the same base name.
		buckets := map[time.Time]map[string]observed{}
		for _, snapshot := range history[league] {
			if snapshot.FetchedAt.Before(since) || snapshot.FetchedAt.After(now.Add(5*time.Minute)) || (snapshot.GameVersion != "" && snapshot.GameVersion != policy.GameVersion) {
				continue
			}
			at := snapshot.FetchedAt.UTC().Truncate(bucket)
			if buckets[at] == nil {
				buckets[at] = map[string]observed{}
			}
			for _, item := range snapshot.Items {
				if !validPrice(item.ChaosValue) || item.BaseType == "" || item.Name == "" {
					continue
				}
				key := item.Class + "\x00" + item.Name + "\x00" + item.BaseType + "\x00" + item.Source + "\x00" + item.ItemID + "\x00" + item.Variant
				previous, ok := buckets[at][key]
				if !ok || snapshot.FetchedAt.After(previous.at) {
					buckets[at][key] = observed{snapshot.FetchedAt, item}
				}
			}
		}
		byBase := map[string]*EconomyEvidence{}
		reliableTimes := map[string]map[time.Time]bool{}
		for at, rows := range buckets {
			seen := map[string]bool{}
			for _, obs := range rows {
				item := obs.item
				key := item.Class + "\x00" + item.BaseType
				v := byBase[key]
				if v == nil {
					v = &EconomyEvidence{League: league, Class: item.Class, BaseType: item.BaseType}
					byBase[key] = v
					reliableTimes[key] = map[time.Time]bool{}
				}
				if !seen[key] {
					v.Buckets++
					seen[key] = true
				}
				if obs.at.After(v.LastSeen) {
					v.LastSeen = obs.at
					v.Source = item.Source
					if v.Source == "" {
						v.Source = "legacy"
					}
					if supplyKnown(item) {
						v.Listings = item.ListingCount
					}
					v.TradedUnits = item.TradedUnits
				}
				if reliableObservation(item, policy) && now.Sub(obs.at) <= maxAge {
					reliableTimes[key][at] = true
					if obs.at.After(v.LastReliableSeen) {
						v.LastReliableSeen = obs.at
					}
				}
			}
		}
		for _, key := range sortedKeys(byBase) {
			v := byBase[key]
			v.ReliableBuckets = len(reliableTimes[key])
			v.State = "thin"
			if now.Sub(v.LastSeen) > maxAge {
				v.State = "stale"
			} else if v.ReliableBuckets >= policy.ReliableBuckets && now.Sub(v.LastReliableSeen) <= maxAge {
				v.State = "reliable"
			}
			evidence = append(evidence, *v)
		}
		times := make([]time.Time, 0, len(buckets))
		for at := range buckets {
			times = append(times, at)
		}
		sort.Slice(times, func(i, j int) bool { return times[i].Before(times[j]) })
		for _, at := range times {
			snapshot := EconomySnapshot{League: league, GameVersion: policy.GameVersion, FetchedAt: at}
			for _, key := range sortedKeys(buckets[at]) {
				obs := buckets[at][key]
				v := byBase[obs.item.Class+"\x00"+obs.item.BaseType]
				if v.State != "stale" {
					snapshot.Items = append(snapshot.Items, obs.item)
				}
			}
			if len(snapshot.Items) > 0 {
				result[league] = append(result[league], snapshot)
			}
		}
	}
	return result, evidence
}

type TierStateOptions struct {
	Store    *EconomyHistoryStore
	Evidence []EconomyEvidence
	Policy   config.EconomyMemory
	Persist  bool
}
type tierDecision struct {
	Tier          int       `json:"tier"`
	Candidate     int       `json:"candidate"`
	Confirmations int       `json:"confirmations"`
	Bucket        time.Time `json:"bucket"`
}

// StabilizeTiers persists decisions separately from immutable observations.
// Recompiling a theme, previewing a script or restarting the desktop cannot
// count as additional market evidence. Missing/stale items use caller fallback.
func (o *TierStateOptions) StabilizeTiers(scope, class string, proposed map[int][]string) (map[int][]string, error) {
	if o == nil || o.Store == nil {
		return proposed, nil
	}
	key := "tiers:" + scope
	decisions := map[string]tierDecision{}
	var raw string
	err := o.Store.db.QueryRow("SELECT value FROM history_metadata WHERE key=?", key).Scan(&raw)
	if err == nil {
		if err = json.Unmarshal([]byte(raw), &decisions); err != nil {
			return nil, fmt.Errorf("reading economy tier memory: %w", err)
		}
	}
	if err != nil && !errors.Is(err, sql.ErrNoRows) {
		return nil, err
	}
	evidence := map[string]EconomyEvidence{}
	for _, v := range o.Evidence {
		if v.Class != class && !strings.HasPrefix(class, "csv:") {
			continue
		}
		previous, ok := evidence[v.BaseType]
		// Prefer a credible observation for the stability gate, without adding
		// counts from different leagues. Scores still use the user's league mix.
		if !ok || (v.State == "reliable" && previous.State != "reliable") || (v.State == previous.State && v.LastSeen.After(previous.LastSeen)) {
			evidence[v.BaseType] = v
		}
	}
	output := map[int][]string{}
	bucketSize := time.Duration(o.Policy.BucketHours * float64(time.Hour))
	for tier, names := range proposed {
		for _, name := range names {
			state, exists := decisions[name]
			base, _, _ := strings.Cut(name, "\x1e")
			v := evidence[base]
			at := v.LastReliableSeen.Truncate(bucketSize)
			if !exists {
				state = tierDecision{Tier: tier, Bucket: at}
			} else if tier != state.Tier && v.State == "reliable" && at.After(state.Bucket) {
				if state.Candidate != tier {
					state.Candidate = tier
					state.Confirmations = 0
				}
				state.Confirmations++
				state.Bucket = at
				required := o.Policy.DemotionBuckets
				if tier < state.Tier {
					required = o.Policy.PromotionBuckets
				}
				if state.Confirmations >= required {
					state.Tier = tier
					state.Candidate = 0
					state.Confirmations = 0
				}
			} else if tier == state.Tier {
				state.Candidate = 0
				state.Confirmations = 0
				if at.After(state.Bucket) {
					state.Bucket = at
				}
			}
			decisions[name] = state
			output[state.Tier] = append(output[state.Tier], name)
		}
	}
	for tier := range output {
		sort.Strings(output[tier])
	}
	if o.Persist {
		encoded, err := json.Marshal(decisions)
		if err != nil {
			return nil, err
		}
		_, err = o.Store.db.Exec("INSERT INTO history_metadata(key,value) VALUES(?,?) ON CONFLICT(key) DO UPDATE SET value=excluded.value", key, string(encoded))
		if err != nil {
			return nil, err
		}
	}
	return output, nil
}
