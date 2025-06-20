package data_generation

import (
	"encoding/json"
	"fmt"
	"io"
	"log"
	"net/http"
	"net/url"
	"time"
)

// ResponseLine is a generic struct that fits the 'lines' array for most PoE Ninja items.
type ResponseLine struct {
	Name         string  `json:"name,omitempty"`
	BaseType     string  `json:"baseType,omitempty"`
	ChaosValue   float64 `json:"chaosValue"`
	DivineValue  float64 `json:"divineValue"`
	ExaltedValue float64 `json:"exaltedValue"`
	ListingCount int     `json:"listingCount"`
	Count        int     `json:"count"`
}

// ApiResponse is the top-level structure for a PoE Ninja API response.
type ApiResponse struct {
	Lines []ResponseLine `json:"lines"`
}

// --- API Client ---

// PoeNinjaClient handles requests to the PoE Ninja API.
type PoeNinjaClient struct {
	httpClient *http.Client
	baseURL    string
}

// NewPoeNinjaClient creates a new client for the API.
func NewPoeNinjaClient() *PoeNinjaClient {
	return &PoeNinjaClient{
		httpClient: &http.Client{Timeout: 20 * time.Second},
		baseURL:    "https://poe.ninja/api/data",
	}
}

// FetchData makes a request to a specific PoE Ninja endpoint and parses the response.
func (c *PoeNinjaClient) FetchData(endpoint, itemType, league string) ([]EconomyCacheItem, error) {
	requestURL, err := url.Parse(fmt.Sprintf("%s/%s", c.baseURL, endpoint))
	if err != nil {
		return nil, fmt.Errorf("failed to parse base URL: %w", err)
	}

	// Create a new query parameters map.
	params := url.Values{}
	params.Add("league", league)
	params.Add("type", itemType)

	// Encode the parameters and add them to the URL.
	requestURL.RawQuery = params.Encode()
	finalURL := requestURL.String()

	log.Printf("Fetching data from: %s", finalURL)

	req, err := http.NewRequest("GET", finalURL, nil)
	if err != nil {
		return nil, fmt.Errorf("failed to create request: %w", err)
	}

	req.Header.Set("User-Agent", "Ruleforge-Economy-Scraper/1.0")

	resp, err := c.httpClient.Do(req)
	if err != nil {
		return nil, fmt.Errorf("failed to execute request: %w", err)
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusOK {
		return nil, fmt.Errorf("api request failed with status: %s", resp.Status)
	}

	body, err := io.ReadAll(resp.Body)
	if err != nil {
		return nil, fmt.Errorf("failed to read response body: %w", err)
	}

	var apiResponse ApiResponse
	if err := json.Unmarshal(body, &apiResponse); err != nil {
		return nil, fmt.Errorf("failed to unmarshal JSON response: %w", err)
	}

	return c.transformResponse(apiResponse), nil
}

// transformResponse converts the API-specific structs into our generic EconomyCacheItem.
func (c *PoeNinjaClient) transformResponse(resp ApiResponse) []EconomyCacheItem {
	var items []EconomyCacheItem
	for _, line := range resp.Lines {
		item := EconomyCacheItem{
			ChaosValue: line.ChaosValue,
		}

		// Determine the name and listing count from various possible fields.
		if line.BaseType != "" {
			item.BaseType = line.BaseType
		}
		if line.Name != "" {
			item.Name = line.Name
		}

		if line.Count > 0 {
			item.Count = line.Count
		}
		if line.ListingCount > 0 {
			item.ListingCount = line.ListingCount
		}
		if line.ChaosValue > 0 {
			item.ChaosValue = line.ChaosValue
		}
		if line.DivineValue > 0 {
			item.DivineValue = line.DivineValue
		}
		if line.ExaltedValue > 0 {
			item.ExaltedValue = line.ExaltedValue
		}

		if item.BaseType != "" {
			items = append(items, item)
		}
	}
	return items
}
