package history

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"log"
	"math"
	"net/http"
	"net/url"
	"sort"
	"strings"
	"time"
)

const poeNinjaEconomyAPI = "https://poe.ninja/poe1/api/economy"

// ResponseLine contains the PoE 1 stash-item pricing fields used by Ruleforge.
type ResponseLine struct {
	ID            int     `json:"id"`
	Name          string  `json:"name"`
	BaseType      string  `json:"baseType"`
	ChaosValue    float64 `json:"chaosValue"`
	DivineValue   float64 `json:"divineValue"`
	ExaltedValue  float64 `json:"exaltedValue"`
	ListingCount  int     `json:"listingCount"`
	Count         int     `json:"count"`
	ItemLevel     int     `json:"itemLevel"`
	RequiredLevel int     `json:"levelRequired"`
	Variant       string  `json:"variant"`
	Links         int     `json:"links"`
	GemLevel      int     `json:"gemLevel"`
	GemQuality    int     `json:"gemQuality"`
	Corrupted     bool    `json:"corrupted"`
	DetailsID     string  `json:"detailsId"`
}

type ApiResponse struct {
	Lines []ResponseLine `json:"lines"`
}

// ExchangeItem names one entry in an exchange overview's "items" array. Bulk
// exchange lines (see ExchangeLine) only carry a slug id; this is the only
// place poe.ninja gives the real display name for that id.
type ExchangeItem struct {
	ID   string `json:"id"`
	Name string `json:"name"`
}

// ExchangeLine is one bulk-exchange pricing entry (e.g. a divination card).
// Exchange-traded items aren't individually listed like stash items, so
// pricing is expressed as a value/volume pair in whatever currency
// ExchangeCore.Primary names, instead of chaosValue/listingCount.
type ExchangeLine struct {
	ID                 string  `json:"id"`
	PrimaryValue       float64 `json:"primaryValue"`
	VolumePrimaryValue float64 `json:"volumePrimaryValue"`
}

type ExchangeCore struct {
	Primary string `json:"primary"`
}

type ExchangeResponse struct {
	Core  ExchangeCore   `json:"core"`
	Items []ExchangeItem `json:"items"`
	Lines []ExchangeLine `json:"lines"`
}

type economyLeague struct {
	ID   string `json:"id"`
	Name string `json:"name"`
}

// PoeNinjaClient fetches PoE 1 economy data using poe.ninja's public economy API.
type PoeNinjaClient struct {
	ctx          context.Context
	httpClient   *http.Client
	baseURL      string
	requestDelay time.Duration
}

// EconomyHTTPError preserves the status for optional-category capability
// reporting. Authentication/rate-limit/server failures are never empty markets.
type EconomyHTTPError struct {
	Status  int
	Address string
}

func (e *EconomyHTTPError) Error() string { return fmt.Sprintf("GET %s: HTTP %d", e.Address, e.Status) }
func unsupportedCategory(err error) bool {
	var status *EconomyHTTPError
	return errors.As(err, &status) && (status.Status == 400 || status.Status == 404)
}

func NewPoeNinjaClient() *PoeNinjaClient {
	return &PoeNinjaClient{
		httpClient:   &http.Client{Timeout: 30 * time.Second},
		baseURL:      poeNinjaEconomyAPI,
		requestDelay: time.Second,
	}
}

// getJSON bounds each response and rejects unsuccessful HTTP statuses before decoding.
func (c *PoeNinjaClient) getJSON(path string, params url.Values, out interface{}) error {
	address := strings.TrimRight(c.baseURL, "/") + "/" + strings.TrimLeft(path, "/")
	if len(params) > 0 {
		address += "?" + params.Encode()
	}
	ctx := c.ctx
	if ctx == nil {
		ctx = context.Background()
	}
	req, err := http.NewRequestWithContext(ctx, http.MethodGet, address, nil)
	if err != nil {
		return fmt.Errorf("create request %s: %w", address, err)
	}
	req.Header.Set("Accept", "application/json")
	req.Header.Set("User-Agent", "Ruleforge/1.1 (PoE 1 economy data; https://github.com/LordMartron94/Ruleforge)")
	log.Printf("Fetching economy data from: %s", address)
	response, err := c.httpClient.Do(req)
	if err != nil {
		return fmt.Errorf("GET %s: %w", address, err)
	}
	defer response.Body.Close()
	if response.StatusCode != http.StatusOK {
		return &EconomyHTTPError{Status: response.StatusCode, Address: address}
	}
	decoder := json.NewDecoder(io.LimitReader(response.Body, 32<<20))
	if err := decoder.Decode(out); err != nil {
		return fmt.Errorf("decode %s: %w", address, err)
	}
	return nil
}

// ResolveLeagues resolves the user's configured league name or ID to the actual
// economy API ID. Preserve configured names as map keys for tiering weights.
// A configured league poe.ninja doesn't list (not started yet, already gone, or
// misspelled) is not an error: it is returned in unavailable so the caller can
// carry on without it.
func (c *PoeNinjaClient) ResolveLeagues(configured []string) (resolved map[string]string, unavailable []string, err error) {
	var available []economyLeague
	if err := c.getJSON("leagues", nil, &available); err != nil {
		return nil, nil, fmt.Errorf("retrieve active poe.ninja PoE 1 economy leagues: %w", err)
	}
	if len(available) == 0 {
		return nil, nil, fmt.Errorf("poe.ninja returned no active PoE 1 economy leagues")
	}
	resolved = make(map[string]string, len(configured))
	names := make([]string, 0, len(available))
	for _, league := range available {
		if league.ID == "" {
			return nil, nil, fmt.Errorf("poe.ninja returned a league without an id")
		}
		names = append(names, league.Name+" ("+league.ID+")")
	}
	sort.Strings(names)
	for _, requested := range configured {
		found := false
		for _, league := range available {
			if requested == league.ID || requested == league.Name {
				resolved[requested] = league.ID
				found = true
				break
			}
		}
		if !found {
			unavailable = append(unavailable, requested)
			log.Printf("WARNING: league %q is not in the active poe.ninja PoE 1 economy leagues (not started yet, ended, or misspelled); skipping it. Available: %s", requested, strings.Join(names, ", "))
		}
	}
	return resolved, unavailable, nil
}

// FetchData retrieves one category from the PoE 1 stash-item API. The league
// argument must be an ID returned by ResolveLeagues.
func (c *PoeNinjaClient) FetchData(endpoint, itemType, league, itemClass string) ([]EconomyCacheItem, error) {
	if endpoint != "itemoverview" {
		return nil, fmt.Errorf("unsupported economy endpoint %q", endpoint)
	}
	parameters := url.Values{"league": {league}, "type": {itemType}}
	var result ApiResponse
	if err := c.getJSON("stash/current/item/overview", parameters, &result); err != nil {
		return nil, err
	}
	if result.Lines == nil {
		return nil, fmt.Errorf("poe.ninja returned no lines array for league %q, type %q", league, itemType)
	}
	items := c.transformResponse(result, itemClass)
	for i := range items {
		items[i].Category = itemType
	}
	return items, nil
}

// FetchExchangeData retrieves one category from PoE 1's bulk-exchange
// overview. Unlike FetchData (individually stash-listed items), this covers
// items only traded through the bulk exchange - such as divination cards -
// which poe.ninja prices as a value/volume pair rather than per-listing.
// The league argument must be an ID returned by ResolveLeagues.
func (c *PoeNinjaClient) FetchExchangeData(itemType, league, itemClass string) ([]EconomyCacheItem, error) {
	parameters := url.Values{"league": {league}, "type": {itemType}}
	var result ExchangeResponse
	if err := c.getJSON("exchange/item/overview", parameters, &result); err != nil {
		return nil, err
	}
	if result.Lines == nil {
		return nil, fmt.Errorf("poe.ninja returned no lines array for league %q, type %q", league, itemType)
	}
	if len(result.Lines) > 0 && result.Core.Primary != "chaos" {
		return nil, fmt.Errorf("poe.ninja exchange overview for league %q, type %q is denominated in %q, not chaos", league, itemType, result.Core.Primary)
	}
	items := c.transformExchangeResponse(result, itemClass)
	for i := range items {
		items[i].Category = itemType
	}
	return items, nil
}

func (c *PoeNinjaClient) transformExchangeResponse(response ExchangeResponse, itemClass string) []EconomyCacheItem {
	names := make(map[string]string, len(response.Items))
	for _, item := range response.Items {
		names[item.ID] = item.Name
	}

	items := make([]EconomyCacheItem, 0, len(response.Lines))
	for _, line := range response.Lines {
		name, ok := names[line.ID]
		if !ok || name == "" {
			continue
		}
		if !validPrice(line.PrimaryValue) || line.VolumePrimaryValue < 0 || math.IsNaN(line.VolumePrimaryValue) || math.IsInf(line.VolumePrimaryValue, 0) {
			continue
		}
		units := line.VolumePrimaryValue / line.PrimaryValue
		if math.IsNaN(units) || math.IsInf(units, 0) {
			continue
		}
		// Turnover is not listing supply. Preserve the raw provider measure;
		// never feed an inferred listing count into the rarity score.
		items = append(items, EconomyCacheItem{
			Class: itemClass, Name: name, BaseType: name,
			ChaosValue: line.PrimaryValue, Source: "exchange", ItemID: line.ID,
			VolumeChaos: line.VolumePrimaryValue, TradedUnits: units,
		})
	}
	return items
}

func (c *PoeNinjaClient) transformResponse(response ApiResponse, itemClass string) []EconomyCacheItem {
	items := make([]EconomyCacheItem, 0, len(response.Lines))
	for _, line := range response.Lines {
		if line.Name == "" || !validPrice(line.ChaosValue) || line.ListingCount < 0 {
			continue
		}
		baseType := line.BaseType
		// Ordinary gem prices supply their base name in name, without baseType.
		if baseType == "" && itemClass != "Uniques" {
			baseType = line.Name
		}
		if baseType == "" {
			continue
		}
		items = append(items, EconomyCacheItem{
			Class: itemClass, Name: line.Name, BaseType: baseType,
			ChaosValue: line.ChaosValue, DivineValue: line.DivineValue,
			ExaltedValue: line.ExaltedValue, Count: line.Count,
			ListingCount: line.ListingCount,
			Source:       "stash", ItemID: fmt.Sprint(line.ID), ItemLevel: line.ItemLevel,
			Variant: stashVariant(line),
		})
	}
	return items
}

func validPrice(value float64) bool { return value > 0 && !math.IsNaN(value) && !math.IsInf(value, 0) }

func stashVariant(line ResponseLine) string {
	// Keep every identity dimension even when an unidentified-base policy
	// deliberately combines them later. Do not pool special rolls on ingest.
	return fmt.Sprintf("%s|%s|ilvl=%d|required=%d|links=%d|gem=%d/%d|corrupted=%t", line.DetailsID, line.Variant, line.ItemLevel, line.RequiredLevel, line.Links, line.GemLevel, line.GemQuality, line.Corrupted)
}

// ActiveTradeLeagues follows new leagues without rewriting a server config.
// SSF and Ruthless markets must not be blended into normal trade estimates.
func (c *PoeNinjaClient) ActiveTradeLeagues(ctx context.Context) ([]string, error) {
	scoped := *c
	scoped.ctx = ctx
	var available []economyLeague
	if err := scoped.getJSON("leagues", nil, &available); err != nil {
		return nil, err
	}
	var leagues []string
	for _, league := range available {
		name := league.Name
		upper := strings.ToUpper(name)
		if name != "" && !strings.Contains(upper, "RUTHLESS") && !strings.Contains(upper, "SSF") && !strings.Contains(upper, "SOLO SELF-FOUND") {
			leagues = append(leagues, name)
		}
	}
	sort.Strings(leagues)
	if len(leagues) == 0 {
		return nil, fmt.Errorf("no active trade leagues returned")
	}
	return leagues, nil
}
