package data_generation

import (
	"encoding/json"
	"fmt"
	"io"
	"log"
	"net/http"
	"net/url"
	"sort"
	"strings"
	"time"
)

const poeNinjaEconomyAPI = "https://poe.ninja/poe1/api/economy"

// ResponseLine contains the PoE 1 stash-item pricing fields used by Ruleforge.
type ResponseLine struct {
	Name         string  `json:"name"`
	BaseType     string  `json:"baseType"`
	ChaosValue   float64 `json:"chaosValue"`
	DivineValue  float64 `json:"divineValue"`
	ExaltedValue float64 `json:"exaltedValue"`
	ListingCount int     `json:"listingCount"`
	Count        int     `json:"count"`
}

type ApiResponse struct {
	Lines []ResponseLine `json:"lines"`
}

type economyLeague struct {
	ID   string `json:"id"`
	Name string `json:"name"`
}

// PoeNinjaClient fetches PoE 1 economy data using poe.ninja's public economy API.
type PoeNinjaClient struct {
	httpClient   *http.Client
	baseURL      string
	requestDelay time.Duration
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
	req, err := http.NewRequest(http.MethodGet, address, nil)
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
		// HTTP failures are not empty price datasets. Fail with the request context.
		excerpt, _ := io.ReadAll(io.LimitReader(response.Body, 256))
		return fmt.Errorf("GET %s: HTTP %s: %s", address, response.Status, strings.TrimSpace(string(excerpt)))
	}
	decoder := json.NewDecoder(io.LimitReader(response.Body, 32<<20))
	if err := decoder.Decode(out); err != nil {
		return fmt.Errorf("decode %s: %w", address, err)
	}
	return nil
}

// ResolveLeagues resolves the user's configured league name or ID to the actual
// economy API ID. Preserve configured names as map keys for tiering weights.
func (c *PoeNinjaClient) ResolveLeagues(configured []string) (map[string]string, error) {
	var available []economyLeague
	if err := c.getJSON("leagues", nil, &available); err != nil {
		return nil, fmt.Errorf("retrieve active poe.ninja PoE 1 economy leagues: %w", err)
	}
	if len(available) == 0 {
		return nil, fmt.Errorf("poe.ninja returned no active PoE 1 economy leagues")
	}
	resolved := make(map[string]string, len(configured))
	names := make([]string, 0, len(available))
	for _, league := range available {
		if league.ID == "" {
			return nil, fmt.Errorf("poe.ninja returned a league without an id")
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
			return nil, fmt.Errorf("configured league %q is not in the active poe.ninja PoE 1 economy leagues; available: %s", requested, strings.Join(names, ", "))
		}
	}
	return resolved, nil
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
	return c.transformResponse(result, itemClass), nil
}

func (c *PoeNinjaClient) transformResponse(response ApiResponse, itemClass string) []EconomyCacheItem {
	items := make([]EconomyCacheItem, 0, len(response.Lines))
	for _, line := range response.Lines {
		if line.BaseType == "" {
			continue
		}
		items = append(items, EconomyCacheItem{
			Class: itemClass, Name: line.Name, BaseType: line.BaseType,
			ChaosValue: line.ChaosValue, DivineValue: line.DivineValue,
			ExaltedValue: line.ExaltedValue, Count: line.Count,
			ListingCount: line.ListingCount,
		})
	}
	return items
}
