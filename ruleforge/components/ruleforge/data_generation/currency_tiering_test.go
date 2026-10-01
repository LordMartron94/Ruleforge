package data_generation

import (
	"reflect"
	"testing"
	"time"

	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/config"
)

func currency(name string, chaos float64, listings int) EconomyCacheItem {
	return EconomyCacheItem{Class: "Currency", Name: name, BaseType: name, ChaosValue: chaos, ListingCount: listings}
}

func currencyFixture() map[string][]EconomySnapshot {
	return map[string][]EconomySnapshot{"Standard": {{
		FetchedAt: time.Date(2026, 10, 1, 0, 0, 0, 0, time.UTC),
		Items: []EconomyCacheItem{
			currency("Mirror", 100000, 5),
			currency("Divine", 300, 500),
			currency("Chaos", 1, 50000),
			currency("Alteration", 0.05, 20000),
			currency("Transmutation", 0.01, 20000),
			currency("Not Profiled", 5000, 1),
		},
	}}}
}

func currencyParams(craftingWeight float64) CurrencyTieringParameters {
	economy := standardTieringParams(0)
	economy.ValueWeight, economy.RarityWeight = 0.65, 0.35
	return CurrencyTieringParameters{Economy: economy, CraftingWeight: craftingWeight, StackSizes: []int{3, 10}}
}

func tierOf(t *testing.T, tiers []CurrencyTier, baseType string) CurrencyTier {
	t.Helper()
	for _, tier := range tiers {
		if tier.BaseType == baseType {
			return tier
		}
	}
	t.Fatalf("%s was not tiered", baseType)
	return CurrencyTier{}
}

func TestCraftingUseLiftsAnEconomicallyEqualCurrency(t *testing.T) {
	profiles := []config.CurrencyProfile{
		{BaseType: "Mirror", Crafting: 2}, {BaseType: "Divine", Crafting: 8}, {BaseType: "Chaos", Crafting: 7},
		{BaseType: "Alteration", Crafting: 3}, {BaseType: "Transmutation", Crafting: 1},
	}
	economyOnly, err := GenerateCurrencyTiers(currencyFixture(), profiles, 3, currencyParams(0))
	if err != nil {
		t.Fatal(err)
	}
	if tierOf(t, economyOnly, "Mirror").Tier != 1 {
		t.Fatal("without crafting, the most valuable and rarest currency is not tier 1")
	}

	// The same economy, rated the other way round: crafting now decides.
	profiles[0].Crafting, profiles[4].Crafting = 0, 10
	crafted, err := GenerateCurrencyTiers(currencyFixture(), profiles, 3, currencyParams(0.9))
	if err != nil {
		t.Fatal(err)
	}
	if tierOf(t, crafted, "Transmutation").Tier >= tierOf(t, crafted, "Mirror").Tier {
		t.Fatal("a crafting weight of 0.9 did not lift the essential crafting currency above an unused one")
	}
	for _, tier := range crafted {
		if tier.BaseType == "Not Profiled" {
			t.Fatal("a currency without a profile was tiered")
		}
	}
}

func TestStacksScoreAsOneItemWorthTheWholeStack(t *testing.T) {
	profiles := []config.CurrencyProfile{
		{BaseType: "Mirror", Crafting: 5}, {BaseType: "Divine", Crafting: 5}, {BaseType: "Chaos", Crafting: 5},
		{BaseType: "Alteration", Crafting: 5}, {BaseType: "Transmutation", Crafting: 5},
	}
	params := currencyParams(0.4)
	params.StackSizes = []int{3, 10, 100, 1000}
	tiers, err := GenerateCurrencyTiers(currencyFixture(), profiles, 4, params)
	if err != nil {
		t.Fatal(err)
	}
	// Ten Transmutation Orbs are worth a tenth of a Chaos Orb: the tier of
	// the Alteration and Chaos Orbs, one above a single Transmutation Orb.
	transmutation := tierOf(t, tiers, "Transmutation")
	if transmutation.Tier != 4 || len(transmutation.Stacks) == 0 || transmutation.Stacks[0] != (StackTier{MinStack: 10, Tier: 3}) {
		t.Fatalf("a stack of cheap currency did not rank higher: %+v", transmutation)
	}
	for _, currency := range tiers {
		previous := currency.Tier
		for _, stack := range currency.Stacks {
			if stack.Tier >= previous {
				t.Fatalf("a bigger stack did not rank strictly higher: %+v", currency)
			}
			previous = stack.Tier
		}
	}
	if mirror := tierOf(t, tiers, "Mirror"); mirror.Tier != 1 || len(mirror.Stacks) != 0 {
		t.Fatalf("tier 1 cannot rank higher, yet: %+v", mirror)
	}
}

func TestUnpricedCurrencyTiersOnItsCraftingRating(t *testing.T) {
	profiles := []config.CurrencyProfile{
		{BaseType: "Mirror", Crafting: 5}, {BaseType: "Chaos", Crafting: 5}, {BaseType: "Transmutation", Crafting: 5},
		{BaseType: "New Essential Orb", Crafting: 10}, {BaseType: "New Useless Orb", Crafting: 0},
	}
	tiers, err := GenerateCurrencyTiers(currencyFixture(), profiles, 3, currencyParams(0.4))
	if err != nil {
		t.Fatal(err)
	}
	essential, useless := tierOf(t, tiers, "New Essential Orb"), tierOf(t, tiers, "New Useless Orb")
	if essential.Priced || useless.Priced || len(essential.Stacks) != 0 {
		t.Fatalf("unpriced currency was treated as priced: %+v %+v", essential, useless)
	}
	if essential.Tier >= useless.Tier {
		t.Fatalf("unpriced currency ignored its crafting rating: %+v %+v", essential, useless)
	}

	// No prices at all yet: crafting alone tiers everything.
	if tiers, err := GenerateCurrencyTiers(nil, profiles, 2, currencyParams(0.4)); err != nil || tierOf(t, tiers, "New Essential Orb").Tier != 1 {
		t.Fatalf("without any economy data the crafting rating did not decide: %v %+v", err, tiers)
	}
}

func TestCurrencyTiersRepeat(t *testing.T) {
	profiles := []config.CurrencyProfile{
		{BaseType: "Mirror", Crafting: 2}, {BaseType: "Divine", Crafting: 8}, {BaseType: "Chaos", Crafting: 7},
		{BaseType: "Alteration", Crafting: 3}, {BaseType: "Transmutation", Crafting: 1},
	}
	first, _ := GenerateCurrencyTiers(currencyFixture(), profiles, 3, currencyParams(0.4))
	again, _ := GenerateCurrencyTiers(currencyFixture(), profiles, 3, currencyParams(0.4))
	if !reflect.DeepEqual(first, again) {
		t.Fatal("the same economy and profiles gave different tiers")
	}
}
