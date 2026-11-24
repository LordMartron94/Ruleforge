package provider

import (
	"encoding/json"
	"fmt"
	"html"
	"io"
	"net/http"
	"net/url"
	"sort"
	"strconv"
	"strings"
	"time"
)

const dropLevelBatchSize = 50
const dropLevelResultLimit = 500

type cargoQueryResponse struct {
	CargoQuery []struct {
		Title struct {
			Name      string `json:"name"`
			DropLevel string `json:"drop level"`
		} `json:"title"`
	} `json:"cargoquery"`
	Error *struct {
		Code string `json:"code"`
		Info string `json:"info"`
	} `json:"error"`
}

type dropLevelClient struct {
	client       *http.Client
	baseURL      string
	requestDelay time.Duration
}

func newDropLevelClient() *dropLevelClient {
	return &dropLevelClient{
		client:       &http.Client{Timeout: 30 * time.Second},
		baseURL:      "https://www.poewiki.net/w/api.php",
		requestDelay: time.Second,
	}
}

// GetBaseTypeDropLevels fetches deduplicated names in paced batches. A failed
// request returns an error, never a partial result that could poison the cache.
// Names absent from a successful wiki response are omitted from the result.
func GetBaseTypeDropLevels(itemBaseTypes []string) (map[string]int, error) {
	return newDropLevelClient().fetch(itemBaseTypes)
}

func (c *dropLevelClient) fetch(itemBaseTypes []string) (map[string]int, error) {
	requested := make(map[string]bool, len(itemBaseTypes))
	var names []string
	for _, name := range itemBaseTypes {
		if !requested[name] {
			requested[name] = true
			names = append(names, name)
		}
	}
	sort.Strings(names)
	levels := make(map[string]int, len(names))
	for start := 0; start < len(names); start += dropLevelBatchSize {
		batch := names[start:min(start+dropLevelBatchSize, len(names))]
		quoted := make([]string, len(batch))
		for i, name := range batch {
			// Cargo's SQL string literals escape apostrophes by doubling them.
			quoted[i] = "'" + strings.ReplaceAll(strings.ReplaceAll(name, "\\", "\\\\"), "'", "''") + "'"
		}
		params := url.Values{
			"action": {"cargoquery"}, "tables": {"items"},
			"fields": {"name, drop_level"}, "format": {"json"},
			"where": {"name IN (" + strings.Join(quoted, ",") + ")"},
			"limit": {strconv.Itoa(dropLevelResultLimit)}, "order_by": {"name,_pageName"},
		}
		for offset := 0; ; offset += dropLevelResultLimit {
			if start > 0 || offset > 0 {
				time.Sleep(c.requestDelay)
			}
			params.Set("offset", strconv.Itoa(offset))
			response, err := c.query(params)
			if err != nil {
				return nil, fmt.Errorf("fetch drop levels for batch starting at %q: %w", batch[0], err)
			}
			for _, row := range response.CargoQuery {
				name := html.UnescapeString(row.Title.Name)
				if !requested[name] || row.Title.DropLevel == "" {
					continue
				}
				level, err := strconv.Atoi(row.Title.DropLevel)
				if err != nil || level < 1 {
					return nil, fmt.Errorf("invalid wiki drop level %q for %q", row.Title.DropLevel, name)
				}
				// Use the earliest drop level when the wiki lists several variants.
				if old, ok := levels[name]; !ok || level < old {
					levels[name] = level
				}
			}
			if len(response.CargoQuery) < dropLevelResultLimit {
				break
			}
		}
	}
	return levels, nil
}

// query retries transient HTTP and MediaWiki errors with bounded backoff.
// MediaWiki can return an error object with HTTP 200; it is not an empty result.
func (c *dropLevelClient) query(params url.Values) (*cargoQueryResponse, error) {
	var lastErr error
	for attempt := 0; attempt < 3; attempt++ {
		request, err := http.NewRequest(http.MethodGet, c.baseURL+"?"+params.Encode(), nil)
		if err != nil {
			return nil, err
		}
		request.Header.Set("User-Agent", "Ruleforge/1.0 (base item drop levels)")
		response, err := c.client.Do(request)
		if err != nil {
			return nil, fmt.Errorf("wiki request: %w", err)
		}
		var result cargoQueryResponse
		decodeErr := json.NewDecoder(io.LimitReader(response.Body, 2<<20)).Decode(&result)
		response.Body.Close()
		retry := false
		switch {
		case response.StatusCode != http.StatusOK:
			lastErr = fmt.Errorf("wiki returned HTTP %d", response.StatusCode)
			retry = response.StatusCode == http.StatusTooManyRequests || response.StatusCode >= 500
		case decodeErr != nil:
			return nil, fmt.Errorf("decode wiki drop levels: %w", decodeErr)
		case result.Error != nil:
			lastErr = fmt.Errorf("wiki API error %s: %s", result.Error.Code, result.Error.Info)
			retry = result.Error.Code == "ratelimited" || result.Error.Code == "maxlag"
		case result.CargoQuery == nil:
			return nil, fmt.Errorf("wiki response is missing cargoquery")
		default:
			return &result, nil
		}
		if !retry || attempt == 2 {
			return nil, lastErr
		}
		delay := c.requestDelay * time.Duration(1<<attempt)
		if value := response.Header.Get("Retry-After"); value != "" {
			if seconds, err := strconv.Atoi(value); err == nil && seconds > 0 {
				delay = max(delay, time.Duration(seconds)*time.Second)
			} else if date, err := http.ParseTime(value); err == nil {
				delay = max(delay, time.Until(date))
			}
		}
		if delay > 30*time.Second {
			return nil, fmt.Errorf("%w; retry after %s", lastErr, delay.Round(time.Second))
		}
		time.Sleep(delay)
	}
	return nil, lastErr
}
