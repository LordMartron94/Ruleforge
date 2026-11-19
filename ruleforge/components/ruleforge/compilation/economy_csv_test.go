package compilation

import (
	"path/filepath"
	"testing"
	"time"

	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/config"
	data "github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/data_generation"
)

func TestCSVStackPromotionNeedsNewMarketBuckets(t *testing.T) {
	store, err := data.NewEconomyHistoryStore(filepath.Join(t.TempDir(), "history.db"))
	if err != nil {
		t.Fatal(err)
	}
	defer store.Close()
	at := time.Date(2026, 11, 19, 0, 0, 0, 0, time.UTC)
	state := &data.TierStateOptions{Store: store, Policy: *config.DefaultEconomyMemory(), Persist: true, Evidence: []data.EconomyEvidence{{Class: "Allflame Embers", BaseType: "Ember", State: "reliable", LastReliableSeen: at}}}
	rg := &RuleGenerator{economyState: state, normalizationStrategy: "Global"}
	profiles := []config.CurrencyProfile{{BaseType: "Ember"}}
	apply := func(proposed, want int) {
		t.Helper()
		tiers := []data.CurrencyTier{{BaseType: "Ember", Tier: 3, Stacks: []data.StackTier{{MinStack: 5, Tier: proposed}}}}
		if err := rg.stabilizeCurrencyTiers("csv:MapEnchantments", 3, tiers, profiles); err != nil {
			t.Fatal(err)
		}
		if tiers[0].Tier != 3 || len(tiers[0].Stacks) != 1 || tiers[0].Stacks[0].Tier != want {
			t.Fatalf("stack bypassed tier memory: %+v", tiers)
		}
	}
	apply(2, 2)
	state.Evidence[0].LastReliableSeen = at.Add(4 * time.Hour)
	apply(1, 2)
	apply(1, 2) // Recompiling a different theme is not more market evidence.
	state.Evidence[0].LastReliableSeen = at.Add(8 * time.Hour)
	apply(1, 1)
}
