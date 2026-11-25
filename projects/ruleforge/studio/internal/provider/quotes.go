package provider

import (
	"bytes"
	"encoding/csv"
	"encoding/json"
	"fmt"
	"path/filepath"
	"sort"

	"github.com/LordMartron94/Ruleforge/studio/internal/config"
	"github.com/LordMartron94/Ruleforge/studio/internal/history"
)

// HistoryQuotesPath is a lossless projection for market scoring. Repeated
// identity strings live once per series; every quote and bucket remains present.
func HistoryQuotesPath(project *config.Project) string {
	return filepath.Join(project.Dir, ".ruleforge", "economy", "quotes.csv")
}

type quoteSeries struct {
	Base, Name, Class, League, Source, State string
	Prices, Listings, Traded                 []float64
	Buckets                                  []int64
}

func writeQuoteSeries(project *config.Project, buckets map[string][]history.EconomySnapshot, evidence []history.EconomyEvidence) error {
	states := map[string]string{}
	for _, e := range evidence {
		states[e.League+"\x00"+e.Class+"\x00"+e.BaseType] = e.State
	}
	series := map[string]*quoteSeries{}
	for league, snapshots := range buckets {
		for _, snapshot := range snapshots {
			for _, item := range snapshot.Items {
				key := league + "\x00" + item.Class + "\x00" + item.BaseType + "\x00" + item.Name + "\x00" + item.Source
				row := series[key]
				if row == nil {
					row = &quoteSeries{Base: item.BaseType, Name: item.Name, Class: item.Class, League: league, Source: item.Source, State: states[league+"\x00"+item.Class+"\x00"+item.BaseType]}
					series[key] = row
				}
				row.Prices = append(row.Prices, item.ChaosValue)
				row.Listings = append(row.Listings, float64(item.ListingCount))
				row.Traded = append(row.Traded, item.TradedUnits)
				row.Buckets = append(row.Buckets, snapshot.FetchedAt.Unix())
			}
		}
	}
	keys := make([]string, 0, len(series))
	for key := range series {
		keys = append(keys, key)
	}
	sort.Strings(keys)
	var buffer bytes.Buffer
	writer := csv.NewWriter(&buffer)
	_ = writer.Write([]string{"baseType", "name", "class", "league", "source", "state", "prices", "buckets", "listings", "tradedUnits"})
	for _, key := range keys {
		row := series[key]
		// Bound each record while retaining all observations, including variants.
		for start := 0; start < len(row.Prices); start += 256 {
			end := min(start+256, len(row.Prices))
			values := []string{row.Base, row.Name, row.Class, row.League, row.Source, row.State}
			for _, data := range []any{row.Prices[start:end], row.Buckets[start:end], row.Listings[start:end], row.Traded[start:end]} {
				encoded, err := json.Marshal(data)
				if err != nil {
					return fmt.Errorf("quote series %s: %w", key, err)
				}
				values = append(values, string(encoded))
			}
			if err := writer.Write(values); err != nil {
				return err
			}
		}
	}
	writer.Flush()
	if err := writer.Error(); err != nil {
		return err
	}
	return writeSnapshot(HistoryQuotesPath(project), buffer.Bytes())
}
