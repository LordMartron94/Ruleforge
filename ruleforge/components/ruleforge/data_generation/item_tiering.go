package data_generation

import (
	"fmt"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/config"
	"github.com/muesli/clusters"
	"github.com/muesli/kmeans"
	"log"
	"math"
	"sort"
)

type NormalizationStrategy string

const (
	// PerLeague normalizes data within each league before combining.
	PerLeague NormalizationStrategy = "PerLeague"
	// Global normalizes data across all leagues at once.
	Global NormalizationStrategy = "Global"
)

// TieringParameters holds the configurable weights for the scoring algorithm.
type TieringParameters struct {
	ValueWeight              float64
	RarityWeight             float64
	LeagueWeights            []config.LeagueWeights
	NormStrategy             NormalizationStrategy
	ChaosOutlierPercentile   float64
	MinListingsForPercentile int
}

// TieringDataPoint is a one-dimensional data point for K-Means clustering.
// It satisfies the clusters.Observation interface.
type TieringDataPoint struct {
	Score float64
}

// Coordinates returns the point's position in n-dimensional space.
func (p TieringDataPoint) Coordinates() clusters.Coordinates {
	return clusters.Coordinates{p.Score}
}

// Distance returns the Euclidean distance between this point and another set of coordinates.
func (p TieringDataPoint) Distance(other clusters.Coordinates) float64 {
	if len(other) == 0 {
		return math.Inf(1)
	}
	return math.Abs(p.Coordinates()[0] - other[0])
}

// --- Intermediate Data Structs ---

// aggregatedItem holds the aggregated data for a single base type within one league.
type aggregatedItem struct {
	baseType          string
	percentileChaos   float64
	totalListingCount int
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

// GenerateTiers takes economy data from multiple leagues and generates a tier list.
// It returns a map where the key is the tier number (1 is best) and the value is a slice of base types.
func GenerateTiers(
	leagueData map[string][]EconomyCacheItem,
	numTiers int,
	params TieringParameters,
) (map[int][]string, error) {
	// --- Input Validation ---
	total := 0.0
	for _, leagueWeight := range params.LeagueWeights {
		total += leagueWeight.Weight
	}
	if total != 1.0 {
		return nil, fmt.Errorf("league weights don't sum to 1")
	}

	if len(leagueData) == 0 {
		return nil, fmt.Errorf("league data cannot be empty")
	}
	if numTiers <= 0 {
		return nil, fmt.Errorf("number of tiers must be greater than 0")
	}
	if params.ChaosOutlierPercentile <= 0 || params.ChaosOutlierPercentile > 1.0 {
		return nil, fmt.Errorf("outlier percentile must be between 0 and 1")
	}

	// --- Aggregation Step (Now more robust) ---
	// First, group all chaos values per basetype per league.
	type itemGroup struct {
		chaosValues  []float64
		listingCount int
	}
	leagueAggGroups := make(map[string]map[string]itemGroup)
	for league, items := range leagueData {
		groups := make(map[string]itemGroup)
		for _, item := range items {
			if item.BaseType == "" {
				continue
			}
			g := groups[item.BaseType]
			g.chaosValues = append(g.chaosValues, item.ChaosValue)
			g.listingCount += item.ListingCount
			groups[item.BaseType] = g
		}
		leagueAggGroups[league] = groups
	}

	aggregatedData := make(map[string]map[string]aggregatedItem)
	for league, groups := range leagueAggGroups {
		aggMap := make(map[string]aggregatedItem)
		for bt, group := range groups {
			if len(group.chaosValues) == 0 {
				continue
			}

			// Always sort the values first, as both median and percentile need it.
			sort.Float64s(group.chaosValues)

			var chaosValueToUse float64

			if len(group.chaosValues) >= params.MinListingsForPercentile {
				percentileIndex := int(float64(len(group.chaosValues)-1) * params.ChaosOutlierPercentile)
				chaosValueToUse = group.chaosValues[percentileIndex]
			} else {
				medianIndex := (len(group.chaosValues) - 1) / 2
				chaosValueToUse = group.chaosValues[medianIndex]
			}

			aggMap[bt] = aggregatedItem{
				baseType:          bt,
				percentileChaos:   chaosValueToUse,
				totalListingCount: group.listingCount,
			}
		}
		aggregatedData[league] = aggMap
	}

	allNormalizedData := make(map[string]map[string]normalizedItem)
	var err error
	switch params.NormStrategy {
	case Global:
		allNormalizedData, err = normalizeGlobally(aggregatedData)
	case PerLeague:
		allNormalizedData, err = normalizePerLeague(aggregatedData)
	default:
		return nil, fmt.Errorf("unknown normalization strategy: %s", params.NormStrategy)
	}
	if err != nil {
		return nil, fmt.Errorf("failed during normalization: %w", err)
	}

	if len(allNormalizedData) == 0 {
		return nil, fmt.Errorf("no valid league data could be processed")
	}

	// --- Scoring Step (Now with league weighting) ---
	unifiedScores := calculateUnifiedScores(allNormalizedData, params)

	// --- Tier Assignment Step (Unchanged) ---
	basetypeToTierMap, err := assignTiers(unifiedScores, numTiers)
	if err != nil {
		return nil, fmt.Errorf("failed to assign tiers: %w", err)
	}

	// Invert map for final output
	tierToBasetypesMap := make(map[int][]string)
	for baseType, tier := range basetypeToTierMap {
		tierToBasetypesMap[tier] = append(tierToBasetypesMap[tier], baseType)
	}

	// Sort for deterministic output
	for tier := range tierToBasetypesMap {
		sort.Strings(tierToBasetypesMap[tier])
	}

	return tierToBasetypesMap, nil
}

// --- Pipeline Helper Functions ---

func normalizePerLeague(aggregatedData map[string]map[string]aggregatedItem) (map[string]map[string]normalizedItem, error) {
	allNormData := make(map[string]map[string]normalizedItem)
	for league, aggMap := range aggregatedData {
		if len(aggMap) < 2 {
			log.Printf("WARN: Not enough data points in league %s to normalize, skipping.", league)
			continue
		}

		var values []float64
		var listings []float64
		var baseTypes []string
		for bt, data := range aggMap {
			values = append(values, math.Log(data.percentileChaos+1))
			listings = append(listings, math.Log(float64(data.totalListingCount+1)))
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

	// Pool all data first
	for league, aggMap := range aggregatedData {
		for bt, data := range aggMap {
			logChaos := math.Log(data.percentileChaos + 1)
			logListings := math.Log(float64(data.totalListingCount + 1))
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
	params TieringParameters,
) []finalScoredItem {
	type unifiedScoreTracker struct {
		weightedChaosSum  float64
		weightedRaritySum float64
		totalWeight       float64
	}
	unified := make(map[string]unifiedScoreTracker)

	// Apply weighted sum
	for league, leagueData := range allNormData {
		weight := getWeightFromLeague(league, params.LeagueWeights)
		for bt, normItem := range leagueData {
			tracker := unified[bt]
			tracker.weightedChaosSum += normItem.chaosZScore * weight
			tracker.weightedRaritySum += normItem.rarityZScore * weight
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

		// Apply final weights to get the score
		score := (params.ValueWeight * avgChaosZ) + (params.RarityWeight * avgRarityZ)
		finalScores = append(finalScores, finalScoredItem{baseType: bt, score: score})
	}
	return finalScores
}

func getWeightFromLeague(league string, leagueWeights []config.LeagueWeights) float64 {
	for _, leagueWeight := range leagueWeights {
		if leagueWeight.League == league {
			return leagueWeight.Weight
		}
	}

	panic("no weight found for league: " + league)
}

func assignTiers(scoredItems []finalScoredItem, numTiers int) (map[string]int, error) {
	if len(scoredItems) < numTiers {
		log.Printf("WARN: Number of items (%d) is less than number of tiers (%d). Reducing tiers.", len(scoredItems), numTiers)
		numTiers = len(scoredItems)
	}
	if numTiers == 0 {
		return make(map[string]int), nil
	}

	var observations clusters.Observations
	for _, item := range scoredItems {
		observations = append(observations, TieringDataPoint{Score: item.score})
	}

	km := kmeans.New()
	determinedCluster, err := km.Partition(observations, numTiers)
	if err != nil {
		return nil, fmt.Errorf("failed to partition with kmeans: %w", err)
	}

	clusterCenters := make([]float64, len(determinedCluster))
	for i, cluster := range determinedCluster {
		clusterCenters[i] = cluster.Center[0]
	}

	tierMap := make(map[int]int)
	sortedCenters := append([]float64{}, clusterCenters...)
	sort.Sort(sort.Reverse(sort.Float64Slice(sortedCenters)))

	for tier, centerValue := range sortedCenters {
		for i, rawCenter := range clusterCenters {
			if centerValue == rawCenter {
				tierMap[i] = tier + 1
				break
			}
		}
	}

	finalTiers := make(map[string]int)
	for _, item := range scoredItems {
		clusterIndex := determinedCluster.Nearest(TieringDataPoint{Score: item.score})
		finalTiers[item.baseType] = tierMap[clusterIndex]
	}

	return finalTiers, nil
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
