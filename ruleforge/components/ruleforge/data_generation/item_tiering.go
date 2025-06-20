package data_generation

import (
	"fmt"
	"github.com/muesli/clusters"
	"github.com/muesli/kmeans"
	"log"
	"math"
	"sort"
)

// TieringParameters holds the configurable weights for the scoring algorithm.
type TieringParameters struct {
	// ValueWeight determines the importance of an item's chaos value.
	ValueWeight float64
	// RarityWeight determines the importance of an item's rarity (low listing count).
	RarityWeight float64
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
	baseType     string
	maxChaos     float64
	listingCount int
}

// normalizedItem holds the Z-scores for an aggregated item.
type normalizedItem struct {
	baseType           string
	chaosZScore        float64
	rarityZScore       float64 // Higher score means more rare (fewer listings)
	contributedLeagues int
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
	if len(leagueData) == 0 {
		return nil, fmt.Errorf("league data cannot be empty")
	}
	if numTiers <= 0 {
		return nil, fmt.Errorf("number of tiers must be greater than 0")
	}

	allNormalizedData := make(map[string]map[string]normalizedItem)

	for league, items := range leagueData {
		log.Printf("Processing league: %s", league)
		aggregatedData := aggregateByBaseType(items)
		if len(aggregatedData) == 0 {
			log.Printf("WARN: No data to process for league %s after aggregation. Skipping.", league)
			continue
		}

		normalizedData, err := normalizeLeagueData(aggregatedData)
		if err != nil {
			log.Printf("WARN: Could not normalize data for league %s: %v. Skipping.", league, err)
			continue
		}
		allNormalizedData[league] = normalizedData
	}

	if len(allNormalizedData) == 0 {
		return nil, fmt.Errorf("no valid league data could be processed")
	}

	unifiedScores := calculateUnifiedScores(allNormalizedData, params)

	// assignTiers now returns a map of [baseType] -> tierNumber
	basetypeToTierMap, err := assignTiers(unifiedScores, numTiers)
	if err != nil {
		return nil, fmt.Errorf("failed to assign tiers: %w", err)
	}

	tierToBasetypesMap := make(map[int][]string)
	for baseType, tier := range basetypeToTierMap {
		tierToBasetypesMap[tier] = append(tierToBasetypesMap[tier], baseType)
	}

	// For deterministic output, sort the base types within each tier alphabetically.
	for tier := range tierToBasetypesMap {
		sort.Strings(tierToBasetypesMap[tier])
	}

	return tierToBasetypesMap, nil
}

// --- Pipeline Helper Functions ---

// aggregateByBaseType performs max-value aggregation for items within a single league.
func aggregateByBaseType(items []EconomyCacheItem) map[string]aggregatedItem {
	aggMap := make(map[string]aggregatedItem)
	for _, item := range items {
		if item.BaseType == "" {
			continue
		}
		existing, ok := aggMap[item.BaseType]
		if !ok {
			// First time seeing this base type
			aggMap[item.BaseType] = aggregatedItem{
				baseType:     item.BaseType,
				maxChaos:     item.ChaosValue,
				listingCount: item.ListingCount,
			}
		} else {
			// Update with max chaos value and sum the listings
			if item.ChaosValue > existing.maxChaos {
				existing.maxChaos = item.ChaosValue
			}
			existing.listingCount += item.ListingCount
			aggMap[item.BaseType] = existing
		}
	}
	return aggMap
}

// normalizeLeagueData applies log scaling and Z-Score normalization.
func normalizeLeagueData(aggData map[string]aggregatedItem) (map[string]normalizedItem, error) {
	if len(aggData) < 2 {
		return nil, fmt.Errorf("cannot normalize with less than 2 data points")
	}

	var values []float64
	var rarities []float64
	var baseTypes []string

	for bt, data := range aggData {
		// Log-scale the chaos value. Add 1 to handle items with 0 chaos value.
		logValue := math.Log(data.maxChaos + 1)
		values = append(values, logValue)
		// We use listing count directly for rarity normalization.
		rarities = append(rarities, float64(data.listingCount))
		baseTypes = append(baseTypes, bt)
	}

	// Calculate Z-Scores for both metrics
	valueZScores, err := calculateZScores(values)
	if err != nil {
		return nil, err
	}
	rarityZScores, err := calculateZScores(rarities)
	if err != nil {
		return nil, err
	}

	normalizedMap := make(map[string]normalizedItem)
	for i, bt := range baseTypes {
		normalizedMap[bt] = normalizedItem{
			baseType:           bt,
			chaosZScore:        valueZScores[i],
			rarityZScore:       -rarityZScores[i], // Invert so higher score = more rare
			contributedLeagues: 1,
		}
	}
	return normalizedMap, nil
}

// calculateUnifiedScores combines normalized data from all leagues into a final score.
func calculateUnifiedScores(allNormData map[string]map[string]normalizedItem, params TieringParameters) []finalScoredItem {
	unified := make(map[string]normalizedItem)

	// Average the Z-Scores for each base type across all leagues it appears in.
	for _, leagueData := range allNormData {
		for bt, normItem := range leagueData {
			uItem, ok := unified[bt]
			if !ok {
				unified[bt] = normItem
			} else {
				uItem.chaosZScore += normItem.chaosZScore
				uItem.rarityZScore += normItem.rarityZScore
				uItem.contributedLeagues++
				unified[bt] = uItem
			}
		}
	}

	var finalScores []finalScoredItem
	for bt, uItem := range unified {
		// Calculate the average Z-score
		avgChaosZ := uItem.chaosZScore / float64(uItem.contributedLeagues)
		avgRarityZ := uItem.rarityZScore / float64(uItem.contributedLeagues)

		// Apply the weighted sum to get the final score
		score := (params.ValueWeight * avgChaosZ) + (params.RarityWeight * avgRarityZ)
		finalScores = append(finalScores, finalScoredItem{baseType: bt, score: score})
	}
	return finalScores
}

// assignTiers uses K-Means to cluster the scored items into N tiers.
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

	// We need to map the cluster index (0, 1, 2...) to a tier number (1, 2, 3...).
	// The cluster with the highest center score is Tier 1.
	clusterCenters := make([]float64, len(determinedCluster))
	for i, cluster := range determinedCluster {
		clusterCenters[i] = cluster.Center[0]
	}

	// Create a map from the raw cluster index to the final tier number.
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

	// Assign each base type to its final tier.
	finalTiers := make(map[string]int)
	for _, item := range scoredItems {
		clusterIndex := determinedCluster.Nearest(TieringDataPoint{Score: item.score})
		finalTiers[item.baseType] = tierMap[clusterIndex]
	}

	return finalTiers, nil
}

// --- STATISTICAL HELPERS ---

// calculateZScores computes the Z-score for each value in a slice.
func calculateZScores(data []float64) ([]float64, error) {
	count := float64(len(data))
	if count < 2 {
		return nil, fmt.Errorf("at least two data points required to calculate Z-score")
	}

	mean := 0.0
	for _, v := range data {
		mean += v
	}
	mean /= count

	stdDev := 0.0
	for _, v := range data {
		stdDev += math.Pow(v-mean, 2)
	}
	// Use population standard deviation
	stdDev = math.Sqrt(stdDev / count)

	if stdDev == 0 {
		// All values are the same; Z-scores are all 0.
		return make([]float64, len(data)), nil
	}

	scores := make([]float64, len(data))
	for i, v := range data {
		scores[i] = (v - mean) / stdDev
	}
	return scores, nil
}
