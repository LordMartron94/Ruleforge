package data_generation

import (
	"path/filepath"
	"testing"
	"time"

	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/config"
)

func TestMemoryRejectsWrongRegimeAndStaleItemsAndBucketsRepeatedPolls(t *testing.T) {
	now := time.Date(2026, 11, 19, 12, 0, 0, 0, time.UTC)
	policy := *config.DefaultEconomyMemory()
	row := EconomyCacheItem{Source: "exchange", Class: "Currency", Name: "A", BaseType: "A", ChaosValue: 10, VolumeChaos: 1000, TradedUnits: 100}
	var snapshots []EconomySnapshot
	for i := 0; i < 40; i++ {
		snapshots = append(snapshots, EconomySnapshot{GameVersion: "3.30", FetchedAt: now.Add(-time.Duration(i) * time.Minute), Items: []EconomyCacheItem{row}})
	}
	stale := row
	stale.Name = "Stale"
	stale.BaseType = "Stale"
	snapshots = append(snapshots, EconomySnapshot{FetchedAt: now.Add(-96 * time.Hour), Items: []EconomyCacheItem{stale}})
	bad := row
	bad.Name = "Wrong patch"
	bad.BaseType = "Wrong patch"
	snapshots = append(snapshots, EconomySnapshot{GameVersion: "3.29", FetchedAt: now, Items: []EconomyCacheItem{bad}})
	prepared, evidence := PrepareEconomyHistory(map[string][]EconomySnapshot{"Covens": snapshots}, policy, now)
	if len(prepared["Covens"]) != 2 {
		t.Fatalf("40 nearby polls should be two buckets, got %d", len(prepared["Covens"]))
	}
	for _, s := range prepared["Covens"] {
		for _, v := range s.Items {
			if v.BaseType != "A" {
				t.Fatalf("inadmissible observation: %+v", v)
			}
		}
	}
	for _, e := range evidence {
		if e.BaseType == "A" && (e.State != "thin" || e.Buckets != 2) {
			t.Fatalf("poll frequency created confidence: %+v", e)
		}
		if e.BaseType == "Stale" && e.State != "stale" {
			t.Fatalf("stale observation not marked: %+v", e)
		}
	}
}

func TestExchangeTurnoverCannotEarnAScarcityBonus(t *testing.T) {
	now := time.Now()
	a := currency("Thin", 10, 1)
	b := currency("Liquid", 10, 100000)
	a.Source = "exchange"
	a.TradedUnits = 0.1
	b.Source = "exchange"
	b.TradedUnits = 100000
	params := standardTieringParams(0)
	params.ValueWeight = 0
	params.RarityWeight = 1
	agg := aggregateAndBlendUniques(map[string][]EconomySnapshot{"Standard": {{FetchedAt: now, Items: []EconomyCacheItem{a, b}}}}, params)
	normalized, _, err := normalizeData(agg, params)
	if err != nil {
		t.Fatal(err)
	}
	if normalized["Standard"]["Thin"].rarityZScore != 0 || normalized["Standard"]["Liquid"].rarityZScore != 0 {
		t.Fatal("exchange turnover entered the rarity score")
	}
	// Old exchange history was serialized without a source; it must also be neutral.
	a.Source = ""
	b.Source = ""
	agg = aggregateAndBlendUniques(map[string][]EconomySnapshot{"Standard": {{FetchedAt: now, Items: []EconomyCacheItem{a, b}}}}, params)
	if agg["Standard"]["Thin"].supplyKnown {
		t.Fatal("legacy currency counts treated as stash supply")
	}
}

func TestPricePercentileNeedsLiquidDistinctBuckets(t *testing.T) {
	params := standardTieringParams(0)
	params.MinListingsForPercentile = 3
	at := time.Now().Truncate(4 * time.Hour)
	values := []weightedValue{{value: 10, weight: 1, reliable: true, bucket: at}, {value: 10, weight: 1, reliable: true, bucket: at}, {value: 1000, weight: 1, reliable: true, bucket: at}}
	if got := calculateRobustValue(values, params); got != 10 {
		t.Fatalf("same-bucket quotes promoted an outlier: %v", got)
	}
	values[1].bucket = at.Add(-4 * time.Hour)
	values[2].bucket = at.Add(-8 * time.Hour)
	if got := calculateRobustValue(values, params); got != 1000 {
		t.Fatalf("liquid independent buckets did not enable configured percentile: %v", got)
	}
	values[2].reliable = false
	if got := calculateRobustValue(values, params); got != 10 {
		t.Fatalf("thin quote passed confidence gate: %v", got)
	}
}

func TestTierMemoryNeedsNewEvidenceAndSurvivesRestart(t *testing.T) {
	path := filepath.Join(t.TempDir(), "history.db")
	store, err := NewEconomyHistoryStore(path)
	if err != nil {
		t.Fatal(err)
	}
	policy := *config.DefaultEconomyMemory()
	at := time.Date(2026, 11, 19, 0, 0, 0, 0, time.UTC)
	options := &TierStateOptions{Store: store, Policy: policy, Persist: true, Evidence: []EconomyEvidence{{Class: "Uniques", BaseType: "Base", State: "reliable", LastReliableSeen: at}}}
	apply := func(want, proposed int) {
		t.Helper()
		got, err := options.StabilizeTiers("test", "Uniques", map[int][]string{proposed: {"Base"}})
		if err != nil || len(got[want]) != 1 {
			t.Fatalf("wanted tier %d, got %v: %v", want, got, err)
		}
	}
	apply(3, 3)
	options.Evidence[0].LastReliableSeen = at.Add(4 * time.Hour)
	apply(3, 1)
	for i := 0; i < 10; i++ {
		apply(3, 1)
	}
	store.Close()
	store, err = NewEconomyHistoryStore(path)
	if err != nil {
		t.Fatal(err)
	}
	defer store.Close()
	options.Store = store
	options.Evidence[0].LastReliableSeen = at.Add(8 * time.Hour)
	apply(1, 1)
	options.Evidence[0].State = "thin"
	options.Evidence[0].LastReliableSeen = at.Add(12 * time.Hour)
	apply(1, 3)
	options.Evidence[0].State = "reliable"
	for i := 3; i <= 5; i++ {
		options.Evidence[0].LastReliableSeen = at.Add(time.Duration(i) * 4 * time.Hour)
		want := 1
		if i == 5 {
			want = 3
		}
		apply(want, 3)
	}
	// A preview must not commit its candidate evidence.
	options.Persist = false
	options.Evidence[0].LastReliableSeen = at.Add(24 * time.Hour)
	apply(3, 1)
	options.Persist = true
	options.Evidence[0].LastReliableSeen = at.Add(28 * time.Hour)
	apply(3, 1)
}

func TestFlatEconomyDoesNotInventTopTierPriority(t *testing.T) {
	got, centres := assignTiersWithCentres([]finalScoredItem{{baseType: "One", score: 0}}, 5)
	if got["One"] != 5 || len(centres) != 5 {
		t.Fatalf("one observation became top-tier: %v %v", got, centres)
	}
	got, _ = assignTiersWithCentres([]finalScoredItem{{baseType: "A", score: 0}, {baseType: "B", score: 0}}, 3)
	if got["A"] != 3 || got["B"] != 3 {
		t.Fatalf("flat population promoted: %v", got)
	}
}
