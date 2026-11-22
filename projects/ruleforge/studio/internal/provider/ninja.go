// Package provider fetches external data and turns it into the snapshot files the engine reads.
//
// Ruleforge scripts never reach the network: a provider runs here, in the host, writes a snapshot file (CSV) atomically and
// returns its hash, and only then does the engine compile. A failed fetch leaves the previous snapshot in place.
package provider

import (
	"context"
	"crypto/sha256"
	"encoding/csv"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net/http"
	"net/url"
	"os"
	"path/filepath"
	"sort"
	"strconv"
	"strings"
	"time"
)

// Row is one priced item of a poe.ninja overview.
type Row struct {
	Name         string
	BaseType     string
	Variant      string
	ChaosValue   float64
	DivineValue  float64
	ListingCount int
	Links        int
	GemLevel     int
	GemQuality   int
	Corrupted    bool
	Source       string
	ID           string
}

// League is one economy league poe.ninja lists.
type League struct {
	ID   string `json:"id"`
	Name string `json:"name"`
}

// Ninja is a poe.ninja PoE 1 economy client.
type Ninja struct {
	HTTP  *http.Client
	Base  string // default https://poe.ninja/poe1/api/economy
	Delay time.Duration
}

func (n *Ninja) base() string {
	if n.Base != "" {
		return strings.TrimRight(n.Base, "/")
	}
	return "https://poe.ninja/poe1/api/economy"
}

func (n *Ninja) client() *http.Client {
	if n.HTTP != nil {
		return n.HTTP
	}
	return &http.Client{Timeout: 30 * time.Second}
}

func (n *Ninja) getJSON(ctx context.Context, path string, params url.Values, out any) error {
	address := n.base() + "/" + strings.TrimLeft(path, "/")
	if len(params) > 0 {
		address += "?" + params.Encode()
	}
	request, err := http.NewRequestWithContext(ctx, http.MethodGet, address, nil)
	if err != nil {
		return err
	}
	request.Header.Set("Accept", "application/json")
	request.Header.Set("User-Agent", "Ruleforge (PoE 1 economy data)")
	response, err := n.client().Do(request)
	if err != nil {
		return fmt.Errorf("GET %s: %w", address, err)
	}
	defer response.Body.Close()
	// Rate limits and server errors are never an empty market: they fail the refresh and keep the old snapshot.
	if response.StatusCode != http.StatusOK {
		return fmt.Errorf("GET %s: HTTP %d", address, response.StatusCode)
	}
	return json.NewDecoder(io.LimitReader(response.Body, 32<<20)).Decode(out)
}

// Leagues lists the active economy leagues.
func (n *Ninja) Leagues(ctx context.Context) ([]League, error) {
	var leagues []League
	if err := n.getJSON(ctx, "leagues", nil, &leagues); err != nil {
		return nil, err
	}
	if len(leagues) == 0 {
		return nil, errors.New("poe.ninja returned no active leagues")
	}
	return leagues, nil
}

// ResolveLeague maps a league name or id to its id. "current" is the first league that is not a permanent one.
func (n *Ninja) ResolveLeague(ctx context.Context, requested string) (string, error) {
	leagues, err := n.Leagues(ctx)
	if err != nil {
		return "", err
	}
	if requested == "" || strings.EqualFold(requested, "current") {
		for _, league := range leagues {
			name := strings.ToLower(league.Name)
			if !strings.Contains(name, "standard") && !strings.Contains(name, "hardcore") && !strings.Contains(name, "ssf") && !strings.Contains(name, "solo") {
				return league.ID, nil
			}
		}
		return leagues[0].ID, nil
	}
	names := []string{}
	for _, league := range leagues {
		if requested == league.ID || requested == league.Name {
			return league.ID, nil
		}
		names = append(names, league.Name+" ("+league.ID+")")
	}
	sort.Strings(names)
	return "", fmt.Errorf("league %q is not active on poe.ninja; available: %s", requested, strings.Join(names, ", "))
}

type stashLine struct {
	ID           int     `json:"id"`
	Name         string  `json:"name"`
	BaseType     string  `json:"baseType"`
	ChaosValue   float64 `json:"chaosValue"`
	DivineValue  float64 `json:"divineValue"`
	ListingCount int     `json:"listingCount"`
	Variant      string  `json:"variant"`
	Links        int     `json:"links"`
	GemLevel     int     `json:"gemLevel"`
	GemQuality   int     `json:"gemQuality"`
	Corrupted    bool    `json:"corrupted"`
	DetailsID    string  `json:"detailsId"`
	Currency     string  `json:"currencyTypeName"`
}

// Stash fetches an item type that is listed item by item (uniques, gems, ...), or currency overviews with the older shape.
func (n *Ninja) Stash(ctx context.Context, league, itemType string) ([]Row, error) {
	var response struct {
		Lines []stashLine `json:"lines"`
	}
	if err := n.getJSON(ctx, "stash/current/item/overview", url.Values{"league": {league}, "type": {itemType}}, &response); err != nil {
		return nil, err
	}
	if response.Lines == nil {
		return nil, fmt.Errorf("poe.ninja returned no lines for league %q type %q", league, itemType)
	}
	rows := make([]Row, 0, len(response.Lines))
	for _, line := range response.Lines {
		base := line.BaseType
		if base == "" {
			base = line.Name
		}
		rows = append(rows, Row{Name: line.Name, BaseType: base, Variant: line.Variant, ChaosValue: line.ChaosValue, DivineValue: line.DivineValue,
			ListingCount: line.ListingCount, Links: line.Links, GemLevel: line.GemLevel, GemQuality: line.GemQuality, Corrupted: line.Corrupted,
			Source: "stash", ID: line.DetailsID})
	}
	return rows, nil
}

// Exchange fetches an item type traded on the bulk exchange (currency, fragments, divination cards).
func (n *Ninja) Exchange(ctx context.Context, league, itemType string) ([]Row, error) {
	var response struct {
		Core struct {
			Primary string `json:"primary"`
		} `json:"core"`
		Items []struct {
			ID   string `json:"id"`
			Name string `json:"name"`
		} `json:"items"`
		Lines []struct {
			ID           string  `json:"id"`
			PrimaryValue float64 `json:"primaryValue"`
			Volume       float64 `json:"volumePrimaryValue"`
		} `json:"lines"`
	}
	if err := n.getJSON(ctx, "exchange/item/overview", url.Values{"league": {league}, "type": {itemType}}, &response); err != nil {
		return nil, err
	}
	if response.Core.Primary != "" && response.Core.Primary != "chaos" {
		return nil, fmt.Errorf("the exchange overview of %q is priced in %q, not chaos", itemType, response.Core.Primary)
	}
	names := map[string]string{}
	for _, item := range response.Items {
		names[item.ID] = item.Name
	}
	rows := make([]Row, 0, len(response.Lines))
	for _, line := range response.Lines {
		name := names[line.ID]
		if name == "" {
			name = line.ID
		}
		rows = append(rows, Row{Name: name, BaseType: name, ChaosValue: line.PrimaryValue, ListingCount: int(line.Volume), Source: "exchange", ID: line.ID})
	}
	return rows, nil
}

func formatFloat(value float64) string {
	return strconv.FormatFloat(value, 'f', -1, 64)
}

// WriteCSV writes rows as the snapshot the engine reads: sorted, so the same market gives byte-identical output; an item with
// no price and no listings gets an empty price (none), never zero.
func WriteCSV(path string, rows []Row) (string, error) {
	sorted := append([]Row(nil), rows...)
	sort.SliceStable(sorted, func(i, j int) bool {
		a, b := sorted[i], sorted[j]
		if a.BaseType != b.BaseType {
			return a.BaseType < b.BaseType
		}
		if a.Name != b.Name {
			return a.Name < b.Name
		}
		if a.Variant != b.Variant {
			return a.Variant < b.Variant
		}
		if a.Links != b.Links {
			return a.Links < b.Links
		}
		return a.ID < b.ID
	})
	if err := os.MkdirAll(filepath.Dir(path), 0o755); err != nil {
		return "", err
	}
	temp, err := os.CreateTemp(filepath.Dir(path), ".snapshot-*")
	if err != nil {
		return "", err
	}
	name := temp.Name()
	hash := sha256.New()
	writer := csv.NewWriter(io.MultiWriter(temp, hash))
	_ = writer.Write([]string{"baseType", "name", "variant", "chaosValue", "divineValue", "listingCount", "links", "gemLevel", "gemQuality", "corrupted", "source", "id"})
	for _, row := range sorted {
		chaos, divine := formatFloat(row.ChaosValue), formatFloat(row.DivineValue)
		if row.ChaosValue == 0 && row.ListingCount == 0 {
			chaos, divine = "", ""
		}
		_ = writer.Write([]string{row.BaseType, row.Name, row.Variant, chaos, divine, strconv.Itoa(row.ListingCount), strconv.Itoa(row.Links),
			strconv.Itoa(row.GemLevel), strconv.Itoa(row.GemQuality), strconv.FormatBool(row.Corrupted), row.Source, row.ID})
	}
	writer.Flush()
	if err := writer.Error(); err != nil {
		temp.Close()
		os.Remove(name)
		return "", err
	}
	if err := temp.Close(); err != nil {
		os.Remove(name)
		return "", err
	}
	if err := os.Rename(name, path); err != nil {
		os.Remove(name)
		return "", err
	}
	return hex.EncodeToString(hash.Sum(nil)), nil
}
