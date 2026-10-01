package data_generation

import (
	"fmt"
	"math"
	"sort"

	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/config"
)

// currencyValueOffset keeps fractions of a chaos apart on the log value scale:
// most basic currency trades well below one chaos, where the default offset of
// 1 would flatten it into a single value.
const currencyValueOffset = 0.01

// CurrencyTieringParameters configures GenerateCurrencyTiers.
type CurrencyTieringParameters struct {
	// Economy weighs value, rarity and trend exactly as for every other
	// economy tiering.
	Economy TieringParameters
	// CraftingWeight is the share of the final score taken from the crafting
	// rating; the economy score gets 1 - CraftingWeight.
	CraftingWeight float64
	// StackSizes are the stack sizes worth a rule of their own, smallest first.
	StackSizes []int
}

// StackTier is the tier a stack of at least MinStack reaches.
type StackTier struct {
	MinStack int
	Tier     int
}

// CurrencyTier is one currency's tier, plus the stack sizes that reach a
// better tier than a single one, smallest stack first.
type CurrencyTier struct {
	BaseType string
	Tier     int
	Stacks   []StackTier
	// Priced says whether the economy had a price for it; an unpriced
	// currency is tiered on its crafting rating against an average economy
	// score.
	Priced bool
}

// GenerateCurrencyTiers tiers the profiled currencies into numTiers tiers,
// tier 1 the best. A currency's score is
//
//	(1 - CraftingWeight) * economy score + CraftingWeight * crafting z-score
//
// where the economy score is the same weighted value, rarity and trend
// z-score every other economy tiering uses, and the crafting z-score is the
// profile's rating against the other profiles' ratings. A stack is scored as
// one item worth its whole stack, and takes the tier whose centre is nearest.
func GenerateCurrencyTiers(
	leagueData map[string][]EconomySnapshot,
	profiles []config.CurrencyProfile,
	numTiers int,
	params CurrencyTieringParameters,
) ([]CurrencyTier, error) {
	if len(profiles) == 0 {
		return nil, fmt.Errorf("no currency profiles to tier")
	}
	if numTiers <= 0 {
		return nil, fmt.Errorf("number of tiers must be greater than 0")
	}
	if !(params.CraftingWeight >= 0 && params.CraftingWeight <= 1) {
		return nil, fmt.Errorf("crafting weight must be between 0 and 1")
	}
	economy := params.Economy
	if economy.ValueOffset <= 0 {
		economy.ValueOffset = currencyValueOffset
	}

	profiled := make(map[string]bool, len(profiles))
	for _, profile := range profiles {
		profiled[profile.BaseType] = true
	}
	currencyData := make(map[string][]EconomySnapshot, len(leagueData))
	for league, snapshots := range leagueData {
		var kept []EconomySnapshot
		for _, snapshot := range snapshots {
			var items []EconomyCacheItem
			for _, item := range snapshot.Items {
				if profiled[item.BaseType] {
					items = append(items, item)
				}
			}
			if len(items) > 0 {
				kept = append(kept, EconomySnapshot{FetchedAt: snapshot.FetchedAt, Items: items})
			}
		}
		if len(kept) > 0 {
			currencyData[league] = kept
		}
	}

	economyScore := func(string, float64) (float64, bool) { return 0, false }
	if len(currencyData) > 0 {
		if economy.Now.IsZero() {
			economy.Now = newestSnapshotTime(currencyData)
		}
		normalized, stats, err := normalizeData(aggregateAndBlendUniques(currencyData, economy), economy)
		if err != nil {
			return nil, fmt.Errorf("normalizing currency prices: %w", err)
		}
		trend := computeTrendZScores(currencyData, economy)
		economyScore = func(baseType string, stack float64) (float64, bool) {
			var sum, totalWeight float64
			for _, league := range sortedKeys(normalized) {
				item, ok := normalized[league][baseType]
				if !ok {
					continue
				}
				weight := getWeightFromLeague(league, economy.LeagueWeights)
				valueZ := stats[league].zScore(economy.valueLog(item.chaos * stack))
				sum += weight * (economy.ValueWeight*valueZ + economy.RarityWeight*item.rarityZScore + economy.TrendWeight*trend[league][baseType])
				totalWeight += weight
			}
			if totalWeight == 0 {
				return 0, false
			}
			return sum / totalWeight, true
		}
	}

	ratings := make([]float64, len(profiles))
	for i, profile := range profiles {
		ratings[i] = profile.Crafting
	}
	craftingZ := make([]float64, len(profiles))
	if len(profiles) > 1 {
		var err error
		if craftingZ, err = calculateZScores(ratings); err != nil {
			return nil, err
		}
	}

	score := func(i int, stack float64) (float64, bool) {
		economic, priced := economyScore(profiles[i].BaseType, stack)
		return (1-params.CraftingWeight)*economic + params.CraftingWeight*craftingZ[i], priced
	}

	scored := make([]finalScoredItem, len(profiles))
	for i, profile := range profiles {
		single, _ := score(i, 1)
		scored[i] = finalScoredItem{baseType: profile.BaseType, score: single}
	}
	tiers, centres := assignTiersWithCentres(scored, numTiers)

	stackSizes := append([]int(nil), params.StackSizes...)
	sort.Ints(stackSizes)
	result := make([]CurrencyTier, len(profiles))
	for i, profile := range profiles {
		_, priced := score(i, 1)
		currency := CurrencyTier{BaseType: profile.BaseType, Tier: tiers[profile.BaseType], Priced: priced}
		best := currency.Tier
		for _, stack := range stackSizes {
			if stack <= 1 || !priced {
				continue
			}
			stackScore, _ := score(i, float64(stack))
			if tier := nearestTier(centres, stackScore); tier < best {
				currency.Stacks = append(currency.Stacks, StackTier{MinStack: stack, Tier: tier})
				best = tier
			}
		}
		result[i] = currency
	}
	return result, nil
}

// nearestTier is the tier (1 = best) whose centre is closest to score; the
// centres are ordered best first, and a tie goes to the better tier.
func nearestTier(centres []float64, score float64) int {
	best, bestDistance := 1, math.Inf(1)
	for i, centre := range centres {
		if distance := math.Abs(score - centre); distance < bestDistance {
			best, bestDistance = i+1, distance
		}
	}
	return best
}
