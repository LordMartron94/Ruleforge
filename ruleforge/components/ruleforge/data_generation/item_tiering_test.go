package data_generation

import (
	"math"
	"testing"
	"time"

	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/config"
)

func standardTieringParams(trendWeight float64) TieringParameters {
	return TieringParameters{
		ValueWeight:              1 - trendWeight,
		RarityWeight:             0,
		TrendWeight:              trendWeight,
		LeagueWeights:            []config.LeagueWeights{{League: "Standard", Weight: 1}},
		NormStrategy:             Global,
		ChaosOutlierPercentile:   0.95,
		MinListingsForPercentile: 20,
		ChasePotentialWeight:     0,
	}
}

func item(name, baseType string, chaos float64) EconomyCacheItem {
	// ListingCount varies with ChaosValue so the "rarity" dimension always has
	// nonzero variance across the fixture's items; the tests below drive
	// tiering purely off ChaosValue/trend, not listing count.
	return EconomyCacheItem{Class: "Uniques", Name: name, BaseType: baseType, ChaosValue: chaos, ListingCount: 5 + int(chaos)}
}

// --- Weighted statistics helpers ---

func TestWeightedMeanAndMedian(t *testing.T) {
	values := []weightedValue{{value: 10, weight: 1}, {value: 20, weight: 3}}
	if got := weightedMean(values); math.Abs(got-17.5) > 1e-9 {
		t.Fatalf("expected weighted mean 17.5, got %v", got)
	}

	// A heavily-weighted value should dominate the median even if it's not
	// the middle element by raw count.
	skewed := []weightedValue{{value: 1, weight: 1}, {value: 2, weight: 1}, {value: 100, weight: 10}}
	if got := weightedMedian(skewed); got != 100 {
		t.Fatalf("expected the heavily-weighted value to dominate the median, got %v", got)
	}
}

func TestRecencyWeightDecaysAtHalfLife(t *testing.T) {
	halfLife := 14 * 24 * time.Hour
	if w := recencyWeight(0, halfLife); w != 1 {
		t.Fatalf("expected weight 1 at age 0, got %v", w)
	}
	if w := recencyWeight(halfLife, halfLife); math.Abs(w-0.5) > 1e-9 {
		t.Fatalf("expected weight 0.5 at exactly one half-life, got %v", w)
	}
	if w := recencyWeight(2*halfLife, halfLife); math.Abs(w-0.25) > 1e-9 {
		t.Fatalf("expected weight 0.25 at two half-lives, got %v", w)
	}
	if w := recencyWeight(365*24*time.Hour, 0); w != 1 {
		t.Fatalf("expected no decay when half-life <= 0, got %v", w)
	}
}

func TestCalculateRobustValueRecencyWeightingOverridesStaleBulk(t *testing.T) {
	// Many old, heavily-decayed observations at 10 chaos; a couple of
	// full-weight recent observations at 100. The robust value should track
	// the recent price, not be dragged toward the old bulk.
	var values []weightedValue
	for i := 0; i < 50; i++ {
		values = append(values, weightedValue{value: 10, weight: 0.01})
	}
	values = append(values, weightedValue{value: 100, weight: 1}, weightedValue{value: 100, weight: 1})

	params := TieringParameters{ChaosOutlierPercentile: 0.95, MinListingsForPercentile: 1000}
	got := calculateRobustValue(values, params)
	if got != 100 {
		t.Fatalf("expected recency weighting to favor the recent price of 100, got %v", got)
	}
}

// --- Aggregation ---

func TestAggregateListingCountIsAveragedNotSummed(t *testing.T) {
	// A base type with a stable listing count of 5, seen across many
	// snapshots, must aggregate to ~5 - not 5*N. Summing would make an item's
	// apparent "rarity" a function of how long Ruleforge has tracked it
	// rather than actual current supply.
	now := time.Now()
	var snapshots []EconomySnapshot
	for i := 0; i < 20; i++ {
		snapshots = append(snapshots, EconomySnapshot{
			FetchedAt: now.Add(-time.Duration(i) * time.Hour),
			Items:     []EconomyCacheItem{{Class: "Uniques", Name: "A", BaseType: "Base A", ChaosValue: 10, ListingCount: 5}},
		})
	}
	data := map[string][]EconomySnapshot{"Standard": snapshots}

	params := TieringParameters{ChaosOutlierPercentile: 0.95, MinListingsForPercentile: 1000, ChasePotentialWeight: 0}
	agg := aggregateAndBlendUniques(data, params)
	got := agg["Standard"]["Base A"].avgListingCount
	if math.Abs(got-5) > 1e-6 {
		t.Fatalf("expected averaged listing count ~5 regardless of snapshot count, got %v", got)
	}
}

// --- Theil-Sen / Kendall's tau ---

func TestTheilSenMonotonicSeries(t *testing.T) {
	now := time.Now()
	rising := []seriesPoint{
		{at: now, log: math.Log(10)},
		{at: now.Add(24 * time.Hour), log: math.Log(20)},
		{at: now.Add(48 * time.Hour), log: math.Log(40)},
		{at: now.Add(72 * time.Hour), log: math.Log(80)},
	}
	slope, tau, pairs := theilSenSlopeAndKendallTau(rising)
	if tau != 1 {
		t.Fatalf("expected perfectly concordant series to have tau=1, got %v", tau)
	}
	if slope <= 0 {
		t.Fatalf("expected a positive slope for a rising series, got %v", slope)
	}
	if pairs != 6 { // C(4,2)
		t.Fatalf("expected 6 pairs from 4 points, got %d", pairs)
	}

	falling := []seriesPoint{
		{at: now, log: math.Log(80)},
		{at: now.Add(24 * time.Hour), log: math.Log(40)},
		{at: now.Add(48 * time.Hour), log: math.Log(20)},
		{at: now.Add(72 * time.Hour), log: math.Log(10)},
	}
	slope, tau, _ = theilSenSlopeAndKendallTau(falling)
	if tau != -1 {
		t.Fatalf("expected perfectly discordant series to have tau=-1, got %v", tau)
	}
	if slope >= 0 {
		t.Fatalf("expected a negative slope for a falling series, got %v", slope)
	}
}

func TestTheilSenNoisySeriesHasWeakTau(t *testing.T) {
	now := time.Now()
	// Alternating up/down: no consistent direction.
	noisy := []seriesPoint{
		{at: now, log: math.Log(10)},
		{at: now.Add(24 * time.Hour), log: math.Log(20)},
		{at: now.Add(48 * time.Hour), log: math.Log(10)},
		{at: now.Add(72 * time.Hour), log: math.Log(20)},
	}
	_, tau, _ := theilSenSlopeAndKendallTau(noisy)
	if math.Abs(tau) >= 1 {
		t.Fatalf("expected a noisy alternating series to have a weak tau, got %v", tau)
	}
}

// --- Trend z-scores ---

func fourEvenSnapshots(itemsAt func(i int) []EconomyCacheItem) []EconomySnapshot {
	now := time.Now()
	snapshots := make([]EconomySnapshot, 4)
	for i := 0; i < 4; i++ {
		snapshots[i] = EconomySnapshot{FetchedAt: now.Add(-time.Duration(3-i) * 24 * time.Hour), Items: itemsAt(i)}
	}
	return snapshots
}

func TestComputeTrendZScoresRequiresMinimumSamples(t *testing.T) {
	values := []float64{10, 10, 10}
	now := time.Now()
	var tooFew []EconomySnapshot
	for i, v := range values {
		tooFew = append(tooFew, EconomySnapshot{
			FetchedAt: now.Add(-time.Duration(len(values)-i) * 24 * time.Hour),
			Items:     []EconomyCacheItem{item("A", "Base A", v), item("B", "Base B", v)},
		})
	}
	data := map[string][]EconomySnapshot{"Standard": tooFew}
	if trend := computeTrendZScores(data, standardTieringParams(0.5)); len(trend) != 0 {
		t.Fatalf("expected no trend signal below trendMinSamples, got %#v", trend)
	}

	sameInstant := time.Now()
	duplicateTimes := map[string][]EconomySnapshot{
		"Standard": {
			{FetchedAt: sameInstant, Items: []EconomyCacheItem{item("A", "Base A", 10)}},
			{FetchedAt: sameInstant, Items: []EconomyCacheItem{item("A", "Base A", 10)}},
			{FetchedAt: sameInstant, Items: []EconomyCacheItem{item("A", "Base A", 10)}},
			{FetchedAt: sameInstant, Items: []EconomyCacheItem{item("A", "Base A", 10)}},
		},
	}
	if trend := computeTrendZScores(duplicateTimes, standardTieringParams(0.5)); len(trend) != 0 {
		t.Fatalf("expected no trend signal without distinct fetch times, got %#v", trend)
	}
}

func TestComputeTrendZScoresDirectionMatchesRealMovement(t *testing.T) {
	risingValues := []float64{10, 20, 40, 80}
	fallingValues := []float64{80, 40, 20, 10}
	snapshots := fourEvenSnapshots(func(i int) []EconomyCacheItem {
		return []EconomyCacheItem{
			item("Rising", "Base Rising", risingValues[i]),
			item("Falling", "Base Falling", fallingValues[i]),
		}
	})
	data := map[string][]EconomySnapshot{"Standard": snapshots}

	trend := computeTrendZScores(data, standardTieringParams(0.5))
	league, ok := trend["Standard"]
	if !ok {
		t.Fatalf("expected a trend signal once enough consistent history exists")
	}
	if league["Base Rising"] <= 0 {
		t.Fatalf("expected a positive trend z-score for a consistently rising base type, got %v", league["Base Rising"])
	}
	if league["Base Falling"] >= 0 {
		t.Fatalf("expected a negative trend z-score for a consistently falling base type, got %v", league["Base Falling"])
	}
	if league["Base Rising"] <= league["Base Falling"] {
		t.Fatalf("expected the rising base type to outrank the falling one: rising=%v falling=%v", league["Base Rising"], league["Base Falling"])
	}
}

// --- End-to-end tiering ---

func TestGenerateTiersSingleSnapshotBehavesAsBeforeTrendWeight(t *testing.T) {
	data := map[string][]EconomySnapshot{
		"Standard": {
			{FetchedAt: time.Now(), Items: []EconomyCacheItem{
				item("Cheap", "Base Cheap", 1),
				item("Expensive", "Base Expensive", 1000),
			}},
		},
	}
	tiers, err := GenerateTiers(data, 2, standardTieringParams(0))
	if err != nil {
		t.Fatal(err)
	}
	if len(tiers[1]) != 1 || tiers[1][0] != "Base Expensive" {
		t.Fatalf("expected the expensive base in tier 1, got %#v", tiers)
	}
	if len(tiers[2]) != 1 || tiers[2][0] != "Base Cheap" {
		t.Fatalf("expected the cheap base in tier 2, got %#v", tiers)
	}
}

func TestTrendWeightRewardsRisingOverFallingRegardlessOfAbsoluteValue(t *testing.T) {
	// Both base types cover almost the same value range; only their direction
	// over time differs (values are nudged apart so the value/rarity
	// dimensions - which TrendWeight=1 gives zero say over anyway - have
	// nonzero variance to normalize). If trend scoring ever reintroduces a
	// sign error (confidence carrying its own sign instead of being a pure
	// magnitude), a consistently *falling* item would wrongly outrank a
	// rising one here.
	risingValues := []float64{10, 22, 44, 88}
	fallingValues := []float64{90, 45, 23, 11}
	snapshots := fourEvenSnapshots(func(i int) []EconomyCacheItem {
		return []EconomyCacheItem{
			item("Rising", "Base Rising", risingValues[i]),
			item("Falling", "Base Falling", fallingValues[i]),
		}
	})
	data := map[string][]EconomySnapshot{"Standard": snapshots}

	tiers, err := GenerateTiers(data, 2, standardTieringParams(1.0))
	if err != nil {
		t.Fatal(err)
	}
	if len(tiers[1]) != 1 || tiers[1][0] != "Base Rising" {
		t.Fatalf("expected the rising base type in tier 1 when trend dominates score, got %#v", tiers)
	}
	if len(tiers[2]) != 1 || tiers[2][0] != "Base Falling" {
		t.Fatalf("expected the falling base type in tier 2 when trend dominates score, got %#v", tiers)
	}
}

func TestValueHalfLifeAffectsGeneratedTiers(t *testing.T) {
	// Base type's price crashed from 1000 to 10 a long time ago (60 days) and
	// has stayed at 10 ever since (recent, dense history). Base type B has
	// been a steady 500 throughout. Without decay, the stale 1000 values pool
	// in and can outweigh B; with a short half-life, only the current price
	// of 10 should matter, putting A well below B.
	now := time.Now()
	var snapshots []EconomySnapshot
	snapshots = append(snapshots, EconomySnapshot{
		FetchedAt: now.Add(-60 * 24 * time.Hour),
		Items:     []EconomyCacheItem{item("A", "Base A", 1000), item("B", "Base B", 500)},
	})
	for i := 0; i < 30; i++ {
		snapshots = append(snapshots, EconomySnapshot{
			FetchedAt: now.Add(-time.Duration(i) * 24 * time.Hour),
			Items:     []EconomyCacheItem{item("A", "Base A", 10), item("B", "Base B", 500)},
		})
	}
	data := map[string][]EconomySnapshot{"Standard": snapshots}

	params := standardTieringParams(0)
	params.MinListingsForPercentile = 1000 // force median, not percentile, to keep this test's arithmetic simple
	params.ValueHalfLifeDays = 7
	params.Now = now

	tiers, err := GenerateTiers(data, 2, params)
	if err != nil {
		t.Fatal(err)
	}
	if len(tiers[1]) != 1 || tiers[1][0] != "Base B" {
		t.Fatalf("expected recency weighting to rank the currently-pricier Base B above the stale-crashed Base A, got %#v", tiers)
	}
}
