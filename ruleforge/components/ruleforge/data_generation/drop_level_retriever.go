package data_generation

import (
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"net/url"
	"strconv"
	"strings"
	"sync"
	"time"
)

// --- Structs for API Response ---

// CargoQueryResponse defines the structure of the top-level JSON response from the PoeWiki Cargo query.
type CargoQueryResponse struct {
	CargoQuery []CargoQueryResult `json:"cargoquery"`
}

// CargoQueryResult holds the result for a single item.
type CargoQueryResult struct {
	Title ItemTitle `json:"title"`
}

// ItemTitle contains the actual item data, including the drop level.
// The DropLevel is a string because the API can return null, which unmarshals to an empty string.
type ItemTitle struct {
	Name      string `json:"name"`
	DropLevel string `json:"drop level"`
}

// --- Structs for Concurrent Processing ---

// job represents a single item base type to look up.
type job struct {
	ItemName string
}

// result holds the outcome of a single job.
type result struct {
	Job       job
	DropLevel int
	Err       error
}

// --- Core Logic ---

// getDropLevel fetches the drop level for a single Path of Exile base type.
// This version is robust against multiple variants and null/empty drop level values.
func getDropLevel(baseType string) (int, error) {
	baseURL := "https://www.poewiki.net/w/api.php"

	sanitizedBaseType := strings.ReplaceAll(baseType, "'", "\\'")

	params := url.Values{}
	params.Add("action", "cargoquery")
	params.Add("tables", "items")
	params.Add("fields", "name, drop_level")
	params.Add("where", fmt.Sprintf("name='%s'", sanitizedBaseType))
	params.Add("format", "json")

	fullURL := fmt.Sprintf("%s?%s", baseURL, params.Encode())
	req, err := http.NewRequest("GET", fullURL, nil)
	if err != nil {
		return 0, fmt.Errorf("failed to create request: %w", err)
	}
	req.Header.Set("User-Agent", "Go-PoE-DropLevel-Checker-Concurrent/1.0")

	client := &http.Client{Timeout: 10 * time.Second}
	resp, err := client.Do(req)
	if err != nil {
		return 0, fmt.Errorf("failed to perform request: %w", err)
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusOK {
		return 0, fmt.Errorf("received non-200 status code: %d", resp.StatusCode)
	}

	body, err := io.ReadAll(resp.Body)
	if err != nil {
		return 0, fmt.Errorf("failed to read response body: %w", err)
	}

	var responseData CargoQueryResponse
	if err := json.Unmarshal(body, &responseData); err != nil {
		return 0, fmt.Errorf("failed to unmarshal JSON: %w", err)
	}

	for _, itemResult := range responseData.CargoQuery {
		if itemResult.Title.DropLevel != "" {
			dropLevel, err := strconv.Atoi(itemResult.Title.DropLevel)
			if err == nil {
				return dropLevel, nil
			}
		}
	}

	return 0, fmt.Errorf("no valid drop level found for '%s'", baseType)
}

// worker is a goroutine that receives jobs, executes them, and sends results back.
func worker(id int, jobs <-chan job, results chan<- result, wg *sync.WaitGroup) {
	defer wg.Done()
	for j := range jobs {
		dropLevel, err := getDropLevel(j.ItemName)
		results <- result{Job: j, DropLevel: dropLevel, Err: err}
	}
}

// GetBaseTypeDropLevels takes a slice of base type names and fetches their drop levels concurrently.
// It returns a map of the successfully found item names to their drop levels.
// Any items that result in an error during fetching will be omitted from the result map.
func GetBaseTypeDropLevels(itemBaseTypes []string, numWorkers int) map[string]int {

	jobs := make(chan job, len(itemBaseTypes))
	results := make(chan result, len(itemBaseTypes))
	dropLevels := make(map[string]int)

	var wg sync.WaitGroup

	for w := 1; w <= numWorkers; w++ {
		wg.Add(1)
		go worker(w, jobs, results, &wg)
	}

	for _, item := range itemBaseTypes {
		jobs <- job{ItemName: item}
	}
	close(jobs)

	wg.Wait()
	close(results)

	for res := range results {
		if res.Err == nil {
			dropLevels[res.Job.ItemName] = res.DropLevel
		}
	}

	return dropLevels
}
