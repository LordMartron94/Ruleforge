package data_generation

import (
	"fmt"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/config"
	"log"
	"math"
	"sort"
	"time"
)

type NormalizationStrategy string

const (
	// PerLeague normalizes data within each league before combining.
	PerLeague NormalizationStrategy = "PerLeague"
	// Global normalizes data across all leagues at once.
	Global NormalizationStrategy = "Global"
)

const (
	// trendMinSamples is the fewest distinct snapshots required before a
	// trend signal is computed at all. Below this, a slope (and especially a
	// "confidence" derived from it) isn't statistically meaningful - it's
	// just noise dressed up as an opinion.
	trendMinSamples = 4
	// trendMaxSamples bounds how many of the most recent snapshots feed the
	// trend calculation. Theil-Sen is O(n^2) in pairwise comparisons, and a
	// "trend" should reflect fairly recent dynamics rather than being
	// dominated by a multi-year-old permanent league's entire history; 180
	// daily snapshots is roughly six months.
	trendMaxSamples = 180
	// trendConfidenceScale converts a Kendall's-tau z-score (see
	// computeTrendZScores) into a bounded (-1,1) confidence multiplier via
	// tanh(z/trendConfidenceScale). Larger values demand a stronger or
	// longer-running trend before it's trusted.
	trendConfidenceScale = 3.0
)

// TieringParameters holds the configurable weights for the scoring algorithm.
type TieringParameters struct {
	ValueWeight              float64
	RarityWeight             float64
	TrendWeight              float64
	LeagueWeights            []config.LeagueWeights
	NormStrategy             NormalizationStrategy
	ChaosOutlierPercentile   float64
	MinListingsForPercentile int
	ChasePotentialWeight     float64
	// ValueHalfLifeDays exponentially decays how much each recorded snapshot
	// contributes to a base type's value/rarity estimate: a snapshot this
	// many days old counts half as much as today's, twice that age a
	// quarter, and so on (see recencyWeight). It never reaches exactly zero,
	// so a thin history is never discarded - but a long-lived permanent
	// league's oldest prices stop dominating what should be a "current
	// value" estimate. <= 0 disables decay (every snapshot counts equally).
	ValueHalfLifeDays float64
	// Now is the reference time for recency weighting. Left zero, GenerateTiers
	// uses the newest snapshot's time, so the same economy data always yields
	// the same tiers no matter when the filter is compiled.
	Now time.Time
}

func (p TieringParameters) now() time.Time {
	if p.Now.IsZero() {
		return time.Now()
	}
	return p.Now
}

// --- Intermediate Data Structs ---

// aggregatedItem holds the aggregated data for a single base type within one league.
type aggregatedItem struct {
	baseType        string
	percentileChaos float64
	avgListingCount float64
}

type itemDataForGlobalNorm struct {
	baseType    string
	league      string
	logChaos    float64
	logListings float64
}

// normalizedItem holds the Z-scores for an aggregated item.
type normalizedItem struct {
	baseType     string
	chaosZScore  float64
	rarityZScore float64
}

// finalScoredItem holds the final unified score for a base type across all leagues.
type finalScoredItem struct {
	baseType string
	score    float64
}

// --- Main Pipeline Function ---

// GenerateTiers takes economy history from multiple leagues and generates a tier list.
// It assumes the input leagueData contains ONLY unique items.
//
// leagueData holds every snapshot ever recorded for each league, not just the
// latest fetch - nothing is pruned. Recency-weighted aggregation (see
// aggregateAndBlendUniques) turns that into a value/rarity estimate that gets
// steadier as history accumulates while still tracking current reality, and
// a Theil-Sen trend fit (see computeTrendZScores) turns it into an optional,
// confidence-scaled signal for whether a base type is rising or falling.
func GenerateTiers(
	leagueData map[string][]EconomySnapshot,
	numTiers int,
	params TieringParameters,
) (map[int][]string, error) {
	// --- Input Validation ---
	if err := validateParams(leagueData, numTiers, params); err != nil {
		return nil, err
	}

	if params.Now.IsZero() {
		params.Now = newestSnapshotTime(leagueData)
	}

	// --- Aggregation Step ---
	// The complex aggregation logic is now cleanly separated into its own function.
	aggregatedData := aggregateAndBlendUniques(leagueData, params)
	if len(aggregatedData) == 0 {
		return nil, fmt.Errorf("no items remained after aggregation")
	}

	// --- Normalization Step ---
	allNormalizedData, err := normalizeData(aggregatedData, params.NormStrategy)
	if err != nil {
		return nil, fmt.Errorf("failed during normalization: %w", err)
	}
	if len(allNormalizedData) == 0 {
		return nil, fmt.Errorf("no valid league data could be processed")
	}

	// --- Trend Step ---
	// Neutral (zero) until enough history exists to fit a meaningful trend,
	// so a short history behaves exactly as before TrendWeight was introduced.
	trendByLeague := computeTrendZScores(leagueData, params)

	// --- Scoring Step ---
	unifiedScores := calculateUnifiedScores(allNormalizedData, trendByLeague, params)

	// --- Priority Assignment Step ---
	basetypeToTierMap, err := assignTiers(unifiedScores, numTiers)
	if err != nil {
		return nil, fmt.Errorf("failed to assign tiers: %w", err)
	}

	// --- Final Formatting ---
	tierToBasetypesMap := make(map[int][]string)
	for baseType, tier := range basetypeToTierMap {
		tierToBasetypesMap[tier] = append(tierToBasetypesMap[tier], baseType)
	}
	for tier := range tierToBasetypesMap {
		sort.Strings(tierToBasetypesMap[tier])
	}

	return tierToBasetypesMap, nil
}

// --- Pipeline Helper Functions ---

func normalizeData(aggregatedData map[string]map[string]aggregatedItem, strategy NormalizationStrategy) (map[string]map[string]normalizedItem, error) {
	switch strategy {
	case Global:
		return normalizeGlobally(aggregatedData)
	case PerLeague:
		return normalizePerLeague(aggregatedData)
	default:
		return nil, fmt.Errorf("unknown normalization strategy: %s", strategy)
	}
}

func normalizePerLeague(aggregatedData map[string]map[string]aggregatedItem) (map[string]map[string]normalizedItem, error) {
	allNormData := make(map[string]map[string]normalizedItem)
	for _, league := range sortedKeys(aggregatedData) {
		aggMap := aggregatedData[league]
		if len(aggMap) < 2 {
			log.Printf("WARN: Not enough data points in league %s to normalize, skipping.", league)
			continue
		}

		var values []float64
		var listings []float64
		var baseTypes []string
		for _, bt := range sortedKeys(aggMap) {
			data := aggMap[bt]
			values = append(values, math.Log(data.percentileChaos+1))
			listings = append(listings, math.Log(data.avgListingCount+1))
			baseTypes = append(baseTypes, bt)
		}

		valueZScores, err := calculateZScores(values)
		if err != nil {
			return nil, fmt.Errorf("z-score failed for %s values: %w", league, err)
		}
		listingZScores, err := calculateZScores(listings)
		if err != nil {
			return nil, fmt.Errorf("z-score failed for %s listings: %w", league, err)
		}

		normMap := make(map[string]normalizedItem)
		for i, bt := range baseTypes {
			normMap[bt] = normalizedItem{
				baseType:     bt,
				chaosZScore:  valueZScores[i],
				rarityZScore: -listingZScores[i],
			}
		}
		allNormData[league] = normMap
	}
	return allNormData, nil
}

func normalizeGlobally(aggregatedData map[string]map[string]aggregatedItem) (map[string]map[string]normalizedItem, error) {
	var allValueLogs, allListingLogs []float64
	var itemMetadata []itemDataForGlobalNorm

	// Pool all data first, in a fixed order so floating-point sums repeat exactly.
	for _, league := range sortedKeys(aggregatedData) {
		aggMap := aggregatedData[league]
		for _, bt := range sortedKeys(aggMap) {
			data := aggMap[bt]
			logChaos := math.Log(data.percentileChaos + 1)
			logListings := math.Log(data.avgListingCount + 1)
			allValueLogs = append(allValueLogs, logChaos)
			allListingLogs = append(allListingLogs, logListings)
			itemMetadata = append(itemMetadata, itemDataForGlobalNorm{
				baseType:    bt,
				league:      league,
				logChaos:    logChaos,
				logListings: logListings,
			})
		}
	}

	if len(allValueLogs) < 2 {
		return nil, fmt.Errorf("not enough data points globally to normalize")
	}

	// Calculate global mean and std dev
	valueMean, valueStdDev := calculateMeanStdDev(allValueLogs)
	listingMean, listingStdDev := calculateMeanStdDev(allListingLogs)

	if valueStdDev == 0 || listingStdDev == 0 {
		return nil, fmt.Errorf("global standard deviation is zero, cannot normalize")
	}

	// Calculate Z-scores and structure the output map
	allNormData := make(map[string]map[string]normalizedItem)
	for _, item := range itemMetadata {
		if _, ok := allNormData[item.league]; !ok {
			allNormData[item.league] = make(map[string]normalizedItem)
		}
		chaosZ := (item.logChaos - valueMean) / valueStdDev
		listingZ := (item.logListings - listingMean) / listingStdDev

		allNormData[item.league][item.baseType] = normalizedItem{
			baseType:     item.baseType,
			chaosZScore:  chaosZ,
			rarityZScore: -listingZ,
		}
	}

	return allNormData, nil
}

func calculateUnifiedScores(
	allNormData map[string]map[string]normalizedItem,
	trendByLeague map[string]map[string]float64,
	params TieringParameters,
) []finalScoredItem {
	type unifiedScoreTracker struct {
		weightedChaosSum  float64
		weightedRaritySum float64
		weightedTrendSum  float64
		totalWeight       float64
	}
	unified := make(map[string]unifiedScoreTracker)

	// Apply weighted sum, in a fixed order so floating-point sums repeat exactly.
	for _, league := range sortedKeys(allNormData) {
		leagueData := allNormData[league]
		weight := getWeightFromLeague(league, params.LeagueWeights)
		for _, bt := range sortedKeys(leagueData) {
			normItem := leagueData[bt]
			tracker := unified[bt]
			tracker.weightedChaosSum += normItem.chaosZScore * weight
			tracker.weightedRaritySum += normItem.rarityZScore * weight
			// A base type with no trend signal yet (not enough history, or
			// too inconsistent to trust) contributes 0 - neutral, not a
			// penalty.
			tracker.weightedTrendSum += trendByLeague[league][bt] * weight
			tracker.totalWeight += weight
			unified[bt] = tracker
		}
	}

	var finalScores []finalScoredItem
	for bt, tracker := range unified {
		if tracker.totalWeight == 0 {
			continue
		}
		// Calculate the weighted average Z-score
		avgChaosZ := tracker.weightedChaosSum / tracker.totalWeight
		avgRarityZ := tracker.weightedRaritySum / tracker.totalWeight
		avgTrendZ := tracker.weightedTrendSum / tracker.totalWeight

		// Apply final weights to get the score
		score := (params.ValueWeight * avgChaosZ) + (params.RarityWeight * avgRarityZ) + (params.TrendWeight * avgTrendZ)
		finalScores = append(finalScores, finalScoredItem{baseType: bt, score: score})
	}
	return finalScores
}

// assignTiers groups base types into numTiers tiers by score, tier 1 being the
// highest. It runs k-means on the one-dimensional scores, starting from evenly
// spaced quantiles instead of random centres, so the same economy data always
// produces the same tiers.
func assignTiers(scoredItems []finalScoredItem, numTiers int) (map[string]int, error) {
	if len(scoredItems) < numTiers {
		log.Printf("WARN: Number of items (%d) is less than number of tiers (%d). Reducing tiers.", len(scoredItems), numTiers)
		numTiers = len(scoredItems)
	}
	if numTiers == 0 {
		return make(map[string]int), nil
	}

	items := append([]finalScoredItem(nil), scoredItems...)
	sort.Slice(items, func(i, j int) bool {
		if items[i].score != items[j].score {
			return items[i].score > items[j].score
		}
		return items[i].baseType < items[j].baseType
	})

	centers := make([]float64, numTiers)
	for tier := range centers {
		// The middle of each equal-sized slice of the ranked scores.
		index := (2*tier + 1) * len(items) / (2 * numTiers)
		centers[tier] = items[index].score
	}

	assignment := make([]int, len(items))
	for iteration := 0; iteration < 200; iteration++ {
		changed := false
		for i, item := range items {
			if nearest := nearestCenter(centers, item.score); iteration == 0 || nearest != assignment[i] {
				assignment[i] = nearest
				changed = true
			}
		}
		if !changed {
			break
		}

		sums := make([]float64, numTiers)
		counts := make([]int, numTiers)
		for i, item := range items {
			sums[assignment[i]] += item.score
			counts[assignment[i]]++
		}
		for tier := range centers {
			// An empty tier keeps its centre rather than disappearing.
			if counts[tier] > 0 {
				centers[tier] = sums[tier] / float64(counts[tier])
			}
		}
	}

	// Tier 1 is the cluster with the highest centre.
	order := make([]int, numTiers)
	for i := range order {
		order[i] = i
	}
	sort.SliceStable(order, func(a, b int) bool { return centers[order[a]] > centers[order[b]] })
	tierOfCluster := make([]int, numTiers)
	for rank, cluster := range order {
		tierOfCluster[cluster] = rank + 1
	}

	finalTiers := make(map[string]int, len(items))
	for i, item := range items {
		finalTiers[item.baseType] = tierOfCluster[assignment[i]]
	}
	return finalTiers, nil
}

// newestSnapshotTime is the latest FetchedAt across every league, or zero when there is none.
func newestSnapshotTime(leagueData map[string][]EconomySnapshot) time.Time {
	var newest time.Time
	for _, snapshots := range leagueData {
		for _, snapshot := range snapshots {
			if snapshot.FetchedAt.After(newest) {
				newest = snapshot.FetchedAt
			}
		}
	}
	return newest
}

// sortedKeys returns a map's keys in ascending order.
func sortedKeys[V any](m map[string]V) []string {
	keys := make([]string, 0, len(m))
	for key := range m {
		keys = append(keys, key)
	}
	sort.Strings(keys)
	return keys
}

// nearestCenter returns the index of the centre closest to score, preferring
// the higher tier on an exact tie.
func nearestCenter(centers []float64, score float64) int {
	best, bestDistance := 0, math.Inf(1)
	for i, center := range centers {
		if distance := math.Abs(score - center); distance < bestDistance {
			best, bestDistance = i, distance
		}
	}
	return best
}

// --- Trend Analysis ---

// seriesPoint is one snapshot's robust log-value observation for a base type.
type seriesPoint struct {
	at  time.Time
	log float64
}

// computeTrendZScores measures, per league, whether each base type's value is
// rising or falling, using a Theil-Sen slope (the median of every pairwise
// slope between log-value observations) fitted across up to the most recent
// trendMaxSamples snapshots. Theil-Sen uses every available point instead of
// an arbitrary two-way split, and because it's a median rather than a mean of
// slopes, a single noisy or manipulated day can't skew the estimate the way
// it would a least-squares regression - up to (n-2)/2 outlier points can be
// wrong before the median slope itself moves.
//
// The raw slope alone doesn't say how much to trust it: three snapshots that
// happen to rise in a row look identical to a three-point sample of pure
// noise. Kendall's tau (the fraction of pairs that agree on direction, since
// time only moves forward within a series) measures how consistent the trend
// actually is, and its sampling variance under "no real trend" is well known:
//
//	Var(tau) ~= 2*(2n+5) / (9*n*(n-1))
//
// Converting tau to a z-score against that null and squashing |z| through
// tanh gives a confidence magnitude in [0,1) - direction comes entirely from
// the slope's own sign, never from confidence - that starts near zero for a
// handful of noisy points and only approaches 1 once a longer, consistent
// trend has accumulated. The slope is scaled by this confidence before it's
// ever compared across base types, so an unreliable trend can't outrank a
// reliable one just because its raw slope happens to be steeper.
func computeTrendZScores(leagueData map[string][]EconomySnapshot, params TieringParameters) map[string]map[string]float64 {
	trendByLeague := make(map[string]map[string]float64, len(leagueData))

	for league, snapshots := range leagueData {
		sorted := make([]EconomySnapshot, len(snapshots))
		copy(sorted, snapshots)
		sort.Slice(sorted, func(i, j int) bool { return sorted[i].FetchedAt.Before(sorted[j].FetchedAt) })
		if len(sorted) > trendMaxSamples {
			sorted = sorted[len(sorted)-trendMaxSamples:]
		}

		// Build a per-snapshot robust-value series per base type. Within a
		// single snapshot every listing is equally "fresh", so this uses the
		// unweighted robust value - decay is about weighing snapshots against
		// each other, not listings within one.
		seriesByBaseType := make(map[string][]seriesPoint)
		for _, snapshot := range sorted {
			valuesByBaseType := make(map[string][]float64)
			for _, item := range snapshot.Items {
				if item.BaseType == "" {
					continue
				}
				valuesByBaseType[item.BaseType] = append(valuesByBaseType[item.BaseType], item.ChaosValue)
			}
			for bt, values := range valuesByBaseType {
				robust := calculateRobustValue(unweighted(values), params)
				seriesByBaseType[bt] = append(seriesByBaseType[bt], seriesPoint{
					at:  snapshot.FetchedAt,
					log: math.Log(robust + 1),
				})
			}
		}

		type signal struct {
			baseType string
			value    float64
		}
		var signals []signal
		for bt, series := range seriesByBaseType {
			if len(series) < trendMinSamples {
				continue
			}
			slope, tau, pairs := theilSenSlopeAndKendallTau(series)
			if pairs == 0 {
				continue
			}
			n := float64(len(series))
			variance := 2 * (2*n + 5) / (9 * n * (n - 1))
			z := tau / math.Sqrt(variance)
			// confidence is a trust magnitude in [0,1), never negative: z (and
			// tau) already carry the trend's direction via slope's sign, so
			// confidence must not reintroduce a sign of its own - that would
			// flip a highly-consistent downward trend into a positive signal.
			confidence := math.Tanh(math.Abs(z) / trendConfidenceScale)
			signals = append(signals, signal{baseType: bt, value: slope * confidence})
		}

		if len(signals) < 2 {
			continue // not enough comparable base types to z-score meaningfully
		}
		raw := make([]float64, len(signals))
		for i, s := range signals {
			raw[i] = s.value
		}
		zScores, err := calculateZScores(raw)
		if err != nil {
			continue
		}
		leagueTrend := make(map[string]float64, len(signals))
		for i, s := range signals {
			leagueTrend[s.baseType] = zScores[i]
		}
		trendByLeague[league] = leagueTrend
	}

	return trendByLeague
}

// theilSenSlopeAndKendallTau computes the Theil-Sen slope (median pairwise
// slope, in log-value units per day) and a Kendall's-tau concordance measure
// from the same O(n^2) pairs. Ties on value are excluded from tau's
// denominator (this is tau-a restricted to non-tied pairs, not the
// tie-corrected tau-b - a reasonable simplification here since chaos values
// are continuous and exact ties are rare). Since time is strictly increasing
// within the series, concordance reduces to "did the later point have a
// higher value."
func theilSenSlopeAndKendallTau(series []seriesPoint) (slope, tau float64, pairs int) {
	var slopes []float64
	concordant, discordant := 0, 0
	for i := 0; i < len(series); i++ {
		for j := i + 1; j < len(series); j++ {
			days := series[j].at.Sub(series[i].at).Hours() / 24
			if days <= 0 {
				continue
			}
			diff := series[j].log - series[i].log
			slopes = append(slopes, diff/days)
			switch {
			case diff > 0:
				concordant++
			case diff < 0:
				discordant++
			}
		}
	}
	pairs = concordant + discordant
	if len(slopes) == 0 {
		return 0, 0, 0
	}
	sort.Float64s(slopes)
	slope = medianOf(slopes)
	if pairs > 0 {
		tau = float64(concordant-discordant) / float64(pairs)
	}
	return slope, tau, pairs
}

func medianOf(sorted []float64) float64 {
	n := len(sorted)
	if n == 0 {
		return 0
	}
	if n%2 == 1 {
		return sorted[n/2]
	}
	return (sorted[n/2-1] + sorted[n/2]) / 2
}

// --- Aggregation Core Logic ---

// aggregateAndBlendUniques computes both the "general" and "chase" potential
// for each base type and blends them based on ChasePotentialWeight ("general"
// is a robust value across every item sharing that base; "chase" is the
// robust value of just the single best-value named item on it - e.g. the one
// valuable unique sharing a base with several worthless ones).
//
// Every recorded snapshot contributes, weighted by recency (see
// recencyWeight), instead of being pooled flat. Flat pooling has two
// failure modes as history grows: a long-lived permanent league's
// month-old prices count exactly as much as today's, blurring "current
// value"; and summing listing counts across snapshots (rather than
// averaging) makes "rarity" grow with how long Ruleforge has been tracking
// an item instead of reflecting actual current supply. Recency-weighted
// aggregation fixes both while still smoothing day-to-day noise, and still
// gets steadier as more history accumulates.
func aggregateAndBlendUniques(
	leagueData map[string][]EconomySnapshot,
	params TieringParameters,
) map[string]map[string]aggregatedItem {
	now := params.now()
	halfLife := time.Duration(params.ValueHalfLifeDays * float64(24*time.Hour))

	finalAggregatedData := make(map[string]map[string]aggregatedItem)

	for league, snapshots := range leagueData {
		type nameAgg struct {
			chaosValues   []weightedValue
			listingValues []weightedValue
		}
		type baseTypeAgg struct {
			chaosValues   []weightedValue
			listingValues []weightedValue
			byName        map[string]*nameAgg
		}

		baseTypes := make(map[string]*baseTypeAgg)
		for _, snapshot := range snapshots {
			w := recencyWeight(now.Sub(snapshot.FetchedAt), halfLife)
			for _, item := range snapshot.Items {
				if item.BaseType == "" || item.Name == "" {
					continue
				}
				bt := baseTypes[item.BaseType]
				if bt == nil {
					bt = &baseTypeAgg{byName: make(map[string]*nameAgg)}
					baseTypes[item.BaseType] = bt
				}
				bt.chaosValues = append(bt.chaosValues, weightedValue{value: item.ChaosValue, weight: w})
				bt.listingValues = append(bt.listingValues, weightedValue{value: float64(item.ListingCount), weight: w})

				na := bt.byName[item.Name]
				if na == nil {
					na = &nameAgg{}
					bt.byName[item.Name] = na
				}
				na.chaosValues = append(na.chaosValues, weightedValue{value: item.ChaosValue, weight: w})
				na.listingValues = append(na.listingValues, weightedValue{value: float64(item.ListingCount), weight: w})
			}
		}

		leagueAggMap := make(map[string]aggregatedItem)
		for baseType, bt := range baseTypes {
			generalChaosValue := calculateRobustValue(bt.chaosValues, params)
			generalListingAvg := weightedMean(bt.listingValues)

			// The most valuable unique on the base is its chase item. Names are
			// visited in order so a tie always picks the same one.
			var chaseChaos, chaseListing float64
			first := true
			for _, name := range sortedKeys(bt.byName) {
				na := bt.byName[name]
				chaosVal := calculateRobustValue(na.chaosValues, params)
				if first || chaosVal > chaseChaos {
					chaseChaos = chaosVal
					chaseListing = weightedMean(na.listingValues)
					first = false
				}
			}

			chaseWeight := params.ChasePotentialWeight
			generalWeight := 1.0 - chaseWeight

			leagueAggMap[baseType] = aggregatedItem{
				baseType:        baseType,
				percentileChaos: chaseChaos*chaseWeight + generalChaosValue*generalWeight,
				avgListingCount: chaseListing*chaseWeight + generalListingAvg*generalWeight,
			}
		}
		finalAggregatedData[league] = leagueAggMap
	}
	return finalAggregatedData
}

// --- Statistical and Utility Helpers ---

// weightedValue is one observation with a recency weight (see recencyWeight).
type weightedValue struct {
	value  float64
	weight float64
}

// unweighted wraps plain values with an equal weight of 1, for callers (like
// the per-snapshot trend series) that want calculateRobustValue's percentile
// logic without any recency decay.
func unweighted(values []float64) []weightedValue {
	out := make([]weightedValue, len(values))
	for i, v := range values {
		out[i] = weightedValue{value: v, weight: 1}
	}
	return out
}

// recencyWeight exponentially decays an observation's influence with age: at
// exactly one halfLife old it counts half as much, at two half-lives a
// quarter, and so on. It never reaches zero, so a thin history never loses
// its only data point - but a long-lived permanent league's ancient prices
// stop dominating a base type's current value. halfLife <= 0 disables decay
// (every observation counts equally).
func recencyWeight(age, halfLife time.Duration) float64 {
	if halfLife <= 0 {
		return 1
	}
	return math.Pow(0.5, age.Hours()/halfLife.Hours())
}

// weightedPercentile returns the value at the given percentile (0..1) of a
// weighted sample: values are sorted, and the result is the smallest value at
// which the cumulative weight first reaches percentile*totalWeight.
func weightedPercentile(values []weightedValue, percentile float64) float64 {
	if len(values) == 0 {
		return 0
	}
	sorted := make([]weightedValue, len(values))
	copy(sorted, values)
	sort.Slice(sorted, func(i, j int) bool { return sorted[i].value < sorted[j].value })

	totalWeight := 0.0
	for _, v := range sorted {
		totalWeight += v.weight
	}
	if totalWeight <= 0 {
		return sorted[len(sorted)-1].value
	}

	target := percentile * totalWeight
	cumulative := 0.0
	for _, v := range sorted {
		cumulative += v.weight
		if cumulative >= target {
			return v.value
		}
	}
	return sorted[len(sorted)-1].value
}

func weightedMedian(values []weightedValue) float64 {
	return weightedPercentile(values, 0.5)
}

func weightedMean(values []weightedValue) float64 {
	if len(values) == 0 {
		return 0
	}
	var sumWeighted, sumWeight float64
	for _, v := range values {
		sumWeighted += v.value * v.weight
		sumWeight += v.weight
	}
	if sumWeight == 0 {
		return 0
	}
	return sumWeighted / sumWeight
}

// calculateRobustValue extracts a single representative value from a
// recency-weighted sample of chaos values. With enough distinct observations
// it uses the configured outlier percentile (prices are heavy-tailed, so an
// upper percentile - not the mean - best represents "what this is really
// worth"); with too few, it falls back to the weighted median.
func calculateRobustValue(values []weightedValue, params TieringParameters) float64 {
	if len(values) == 0 {
		return 0
	}
	if len(values) >= params.MinListingsForPercentile {
		return weightedPercentile(values, params.ChaosOutlierPercentile)
	}
	return weightedMedian(values)
}

func getWeightFromLeague(league string, leagueWeights []config.LeagueWeights) float64 {
	for _, leagueWeight := range leagueWeights {
		if leagueWeight.League == league {
			return leagueWeight.Weight
		}
	}

	panic("no weight found for league: " + league)
}

// calculateMeanStdDev is a helper for the global normalization.
func calculateMeanStdDev(data []float64) (mean, stdDev float64) {
	count := float64(len(data))
	if count == 0 {
		return 0, 0
	}
	for _, v := range data {
		mean += v
	}
	mean /= count
	for _, v := range data {
		stdDev += math.Pow(v-mean, 2)
	}
	// Population standard deviation
	stdDev = math.Sqrt(stdDev / count)
	return mean, stdDev
}

func calculateZScores(data []float64) ([]float64, error) {
	count := float64(len(data))
	if count < 2 {
		return nil, fmt.Errorf("at least two data points required to calculate Z-score")
	}

	mean, stdDev := calculateMeanStdDev(data)

	if stdDev == 0 {
		return make([]float64, len(data)), nil
	}

	scores := make([]float64, len(data))
	for i, v := range data {
		scores[i] = (v - mean) / stdDev
	}
	return scores, nil
}

func validateParams(leagueData map[string][]EconomySnapshot, numTiers int, params TieringParameters) error {
	total := 0.0
	for _, leagueWeight := range params.LeagueWeights {
		total += leagueWeight.Weight
	}
	// Use a small epsilon for float comparison
	if math.Abs(total-1.0) > 1e-9 {
		return fmt.Errorf("league weights do not sum to 1.0 (sum: %f)", total)
	}
	if len(leagueData) == 0 {
		return fmt.Errorf("league data cannot be empty")
	}
	if numTiers <= 0 {
		return fmt.Errorf("number of tiers must be greater than 0")
	}
	if params.ChaosOutlierPercentile <= 0 || params.ChaosOutlierPercentile > 1.0 {
		return fmt.Errorf("outlier percentile must be between 0 and 1")
	}
	if params.ChasePotentialWeight < 0 || params.ChasePotentialWeight > 1.0 {
		return fmt.Errorf("ChasePotentialWeight must be between 0 and 1")
	}
	return nil
}
