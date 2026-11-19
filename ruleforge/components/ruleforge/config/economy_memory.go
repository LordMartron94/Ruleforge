package config

import (
	"fmt"
	"math"
	"time"
)

// EconomyMemory governs evidence, independently of the user's league weights.
type EconomyMemory struct {
	GameVersion        string  `toml:"GameVersion"`
	Since              string  `toml:"Since"`
	MaxAgeHours        float64 `toml:"MaxAgeHours"`
	BucketHours        float64 `toml:"BucketHours"`
	MinimumListings    int     `toml:"MinimumListings"`
	MinimumTradedUnits float64 `toml:"MinimumTradedUnits"`
	ReliableBuckets    int     `toml:"ReliableBuckets"`
	PromotionBuckets   int     `toml:"PromotionBuckets"`
	DemotionBuckets    int     `toml:"DemotionBuckets"`
}

func DefaultEconomyMemory() *EconomyMemory {
	return &EconomyMemory{GameVersion: "3.30", Since: "2026-11-13T19:00:00Z", MaxAgeHours: 72, BucketHours: 4, MinimumListings: 5, MinimumTradedUnits: 20, ReliableBuckets: 3, PromotionBuckets: 2, DemotionBuckets: 3}
}

func (m EconomyMemory) Validate() error {
	if m.GameVersion == "" {
		return fmt.Errorf("EconomyMemory.GameVersion is required")
	}
	if _, err := time.Parse(time.RFC3339, m.Since); err != nil {
		return fmt.Errorf("EconomyMemory.Since must be RFC3339: %w", err)
	}
	if math.IsNaN(m.MaxAgeHours) || math.IsInf(m.MaxAgeHours, 0) || m.MaxAgeHours <= 0 || m.MaxAgeHours > 24*365 || math.IsNaN(m.BucketHours) || math.IsInf(m.BucketHours, 0) || m.BucketHours < 0.25 || m.BucketHours > m.MaxAgeHours {
		return fmt.Errorf("invalid EconomyMemory age or bucket duration")
	}
	if m.MinimumListings < 1 || m.MinimumTradedUnits <= 0 || math.IsNaN(m.MinimumTradedUnits) || math.IsInf(m.MinimumTradedUnits, 0) || m.ReliableBuckets < 1 || m.PromotionBuckets < 1 || m.DemotionBuckets < 1 {
		return fmt.Errorf("EconomyMemory evidence thresholds must be positive")
	}
	return nil
}
