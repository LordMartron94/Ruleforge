package provider

import (
	"encoding/csv"
	"encoding/json"
	"os"
	"strings"
	"testing"
	"time"

	"github.com/LordMartron94/Ruleforge/studio/internal/config"
	"github.com/LordMartron94/Ruleforge/studio/internal/history"
)

func TestQuoteSeriesRetainsEveryObservationAcrossRecordBoundaries(t *testing.T) {
	p := &config.Project{Dir: t.TempDir()}
	snapshots := []history.EconomySnapshot{}
	for i := 0; i < 600; i++ {
		snapshots = append(snapshots, history.EconomySnapshot{
			FetchedAt: time.Unix(int64(100000+i*14400), 0),
			Items:     []history.EconomyCacheItem{{BaseType: "Quoted, \"base\"\nname", Name: "Variant", Class: "Gems", Source: "stash", ChaosValue: float64(i) + 0.5, ListingCount: i + 5, TradedUnits: float64(i * 2)}},
		})
	}
	evidence := []history.EconomyEvidence{{League: "Standard", Class: "Gems", BaseType: "Quoted, \"base\"\nname", State: "reliable"}}
	if err := writeQuoteSeries(p, map[string][]history.EconomySnapshot{"Standard": snapshots}, evidence); err != nil {
		t.Fatal(err)
	}
	raw, err := os.ReadFile(HistoryQuotesPath(p))
	if err != nil {
		t.Fatal(err)
	}
	rows, err := csv.NewReader(strings.NewReader(string(raw))).ReadAll()
	if err != nil {
		t.Fatal(err)
	}
	if len(rows) != 4 {
		t.Fatalf("got %d rows", len(rows))
	}
	seen := 0
	for _, row := range rows[1:] {
		if row[0] != evidence[0].BaseType || row[5] != "reliable" {
			t.Fatalf("identity changed: %v", row[:6])
		}
		var prices, listings, traded []float64
		var buckets []int64
		for _, field := range []struct {
			text   string
			target any
		}{{row[6], &prices}, {row[7], &buckets}, {row[8], &listings}, {row[9], &traded}} {
			if err := json.Unmarshal([]byte(field.text), field.target); err != nil {
				t.Fatal(err)
			}
		}
		if len(prices) > 256 || len(prices) != len(buckets) || len(prices) != len(listings) || len(prices) != len(traded) {
			t.Fatal("unaligned series")
		}
		for i, price := range prices {
			if price != float64(seen)+0.5 || buckets[i] != int64(100000+seen*14400) || listings[i] != float64(seen+5) || traded[i] != float64(seen*2) {
				t.Fatalf("lost observation %d", seen)
			}
			seen++
		}
	}
	if seen != 600 {
		t.Fatalf("got %d observations", seen)
	}
}
