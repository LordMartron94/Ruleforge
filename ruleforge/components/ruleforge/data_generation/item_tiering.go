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
	ChasePotentialWeight     float64
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

// GenerateTiers takes unique economy data from multiple leagues and generates a tier list.
// It assumes the input leagueData contains ONLY unique items.
func GenerateTiers(
	leagueData map[string][]EconomyCacheItem,
	numTiers int,
	params TieringParameters,
) (map[int][]string, error) {
	// --- Input Validation ---
	if err := validateParams(leagueData, numTiers, params); err != nil {
		return nil, err
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

	// --- Scoring Step ---
	unifiedScores := calculateUnifiedScores(allNormalizedData, params)

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

// --- Aggregation Core Logic ---

// aggregateAndBlendUniques is the new, powerful aggregation function.
// It calculates both the "general" and "chase" potential for each unique base type
// and blends them based on the ChasePotentialWeight.
func aggregateAndBlendUniques(
	leagueData map[string][]EconomyCacheItem,
	params TieringParameters,
) map[string]map[string]aggregatedItem {

	finalAggregatedData := make(map[string]map[string]aggregatedItem)

	for league, items := range leagueData {
		// Group all items by their BaseType to process one base at a time.
		itemsByBaseType := make(map[string][]EconomyCacheItem)
		for _, item := range items {
			if item.BaseType != "" && item.Name != "" {
				itemsByBaseType[item.BaseType] = append(itemsByBaseType[item.BaseType], item)
			}
		}

		leagueAggMap := make(map[string]aggregatedItem)
		for baseType, baseTypeItems := range itemsByBaseType {

			// --- Step 1: Calculate "General Potential" ---
			// This is the original logic: a single robust value for the entire base type.
			var allChaosValues []float64
			var allListingCount int
			for _, item := range baseTypeItems {
				allChaosValues = append(allChaosValues, item.ChaosValue)
				allListingCount += item.ListingCount
			}
			generalChaosValue := calculateRobustValue(allChaosValues, params)

			// --- Step 2: Calculate "Chase Potential" ---
			// This is the logic to find the single best unique on this base.

			// 2a: Sub-group items by their specific unique Name.
			itemsByName := make(map[string][]EconomyCacheItem)
			for _, item := range baseTypeItems {
				itemsByName[item.Name] = append(itemsByName[item.Name], item)
			}

			// 2b: Find robust stats for each individual named unique.
			type namedUniqueStats struct {
				chaosValue   float64
				listingCount int
			}
			var allNamedStats []namedUniqueStats
			for _, nameItems := range itemsByName {
				var chaosValues []float64
				var listingCount int
				for _, item := range nameItems {
					chaosValues = append(chaosValues, item.ChaosValue)
					listingCount += item.ListingCount
				}
				allNamedStats = append(allNamedStats, namedUniqueStats{
					chaosValue:   calculateRobustValue(chaosValues, params),
					listingCount: listingCount,
				})
			}

			// 2c: Identify the chase item (the one with the highest robust value).
			var chaseStats namedUniqueStats
			if len(allNamedStats) > 0 {
				chaseStats = allNamedStats[0]
				for _, stats := range allNamedStats {
					if stats.chaosValue > chaseStats.chaosValue {
						chaseStats = stats
					}
				}
			}

			// --- Step 3: Blend the two potentials using the weight ---
			chaseWeight := params.ChasePotentialWeight
			generalWeight := 1.0 - chaseWeight

			finalChaosValue := (chaseStats.chaosValue * chaseWeight) + (generalChaosValue * generalWeight)
			finalListingCount := (float64(chaseStats.listingCount) * chaseWeight) + (float64(allListingCount) * generalWeight)

			leagueAggMap[baseType] = aggregatedItem{
				baseType:          baseType,
				percentileChaos:   finalChaosValue,
				totalListingCount: int(finalListingCount),
			}
		}
		finalAggregatedData[league] = leagueAggMap
	}
	return finalAggregatedData
}

// --- Statistical and Utility Helpers ---

// calculateRobustValue extracts a single representative value from a slice of chaos values.
func calculateRobustValue(chaosValues []float64, params TieringParameters) float64 {
	if len(chaosValues) == 0 {
		return 0
	}
	sort.Float64s(chaosValues)

	if len(chaosValues) >= params.MinListingsForPercentile {
		percentileIndex := int(float64(len(chaosValues)-1) * params.ChaosOutlierPercentile)
		return chaosValues[percentileIndex]
	}

	medianIndex := (len(chaosValues) - 1) / 2
	return chaosValues[medianIndex]
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

func validateParams(leagueData map[string][]EconomyCacheItem, numTiers int, params TieringParameters) error {
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
