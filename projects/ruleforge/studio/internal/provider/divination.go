package provider

import (
	"context"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"regexp"
	"strconv"
	"strings"
	"time"
)

const divinationURL = "https://raw.githubusercontent.com/NeverSinkDev/NeverSink-Filter/master/NeverSink%27s%20filter%20-%202-SEMI-STRICT.filter"

// DivinationTier retains NeverSink's named subtiers and explicit stack exception.
// Pickup strictness and appearances belong to the Ruleforge module consuming it.
type DivinationTier struct {
	BaseType  string `json:"baseType"`
	Tier      string `json:"tier"`
	StackSize int    `json:"stackSize"`
	Version   string `json:"upstreamVersion"`
	Source    string `json:"source"`
	SHA256    string `json:"sha256"`
}

func parseDivination(data []byte) ([]DivinationTier, error) {
	text := strings.ReplaceAll(string(data), "\r\n", "\n")
	version := regexp.MustCompile(`(?m)^# VERSION:\s+(\S+)`).FindStringSubmatch(text)
	if len(version) != 2 {
		return nil, fmt.Errorf("NeverSink filter has no version header")
	}
	sum := sha256.Sum256(data)
	digest := hex.EncodeToString(sum[:])
	marker := regexp.MustCompile(`\$type->divination\s+\$tier->([a-z0-9]+)\b`)
	names := regexp.MustCompile(`"(?:\\.|[^"\\])*"`)
	seen := map[string]bool{}
	groups := map[string]bool{}
	rows := []DivinationTier{}
	for _, block := range strings.Split(text, "\n\n") {
		lines := strings.Split(strings.TrimSpace(block), "\n")
		if len(lines) < 2 || (!strings.HasPrefix(lines[0], "Show ") && !strings.HasPrefix(lines[0], "Hide ")) {
			continue
		}
		match := marker.FindStringSubmatch(lines[0])
		if len(match) != 2 {
			continue
		}
		tier := match[1]
		if tier == "restex" || tier == "exstack" {
			continue
		}
		switch tier {
		case "t1", "t2", "t3", "t4", "t4c", "t5", "t5c", "excustomstack":
		default:
			return nil, fmt.Errorf("unknown NeverSink divination tier %q", tier)
		}
		baseLine := ""
		stack := 1
		validClass := false
		for _, line := range lines[1:] {
			line = strings.TrimSpace(line)
			if line == `Class == "Divination Cards"` {
				validClass = true
			}
			if strings.HasPrefix(line, "BaseType == ") {
				baseLine = strings.TrimPrefix(line, "BaseType == ")
			}
			if strings.HasPrefix(line, "StackSize >= ") {
				n, err := strconv.Atoi(strings.TrimPrefix(line, "StackSize >= "))
				if err != nil || n < 1 {
					return nil, fmt.Errorf("invalid divination stack condition")
				}
				stack = n
			}
		}
		if !validClass || baseLine == "" {
			return nil, fmt.Errorf("incomplete NeverSink divination group %s", tier)
		}
		for _, quoted := range names.FindAllString(baseLine, -1) {
			name, err := strconv.Unquote(quoted)
			if err != nil {
				return nil, err
			}
			key := fmt.Sprintf("%s/%d", name, stack)
			if seen[key] {
				return nil, fmt.Errorf("duplicate NeverSink card %s", key)
			}
			seen[key] = true
			rows = append(rows, DivinationTier{name, tier, stack, version[1], divinationURL, digest})
			groups[tier] = true
		}
	}
	for _, tier := range []string{"t1", "t2", "t3", "t4", "t4c", "t5", "t5c"} {
		if !groups[tier] {
			return nil, fmt.Errorf("NeverSink divination group %s is missing; snapshot retained", tier)
		}
	}
	return rows, nil
}

// PrepareDivination refreshes the standard NeverSink snapshot atomically. It
// imports upstream tier membership, never recalculates it from market prices.
func PrepareDivination(ctx context.Context, path string) error {
	raw, err := fetchNeverSink(ctx)
	if err != nil {
		return err
	}
	rows, err := parseDivination(raw)
	if err != nil {
		return err
	}
	encoded, err := json.MarshalIndent(rows, "", "  ")
	if err != nil {
		return err
	}
	return writeSnapshot(path, append(encoded, '\n'))
}

func fetchNeverSink(ctx context.Context) ([]byte, error) {
	request, err := http.NewRequestWithContext(ctx, http.MethodGet, divinationURL, nil)
	if err != nil {
		return nil, err
	}
	response, err := (&http.Client{Timeout: 30 * time.Second}).Do(request)
	if err != nil {
		return nil, err
	}
	defer response.Body.Close()
	if response.StatusCode != http.StatusOK {
		return nil, fmt.Errorf("NeverSink: HTTP %d", response.StatusCode)
	}
	raw, err := io.ReadAll(io.LimitReader(response.Body, (2<<20)+1))
	if err != nil {
		return nil, err
	}
	if len(raw) > 2<<20 {
		return nil, fmt.Errorf("NeverSink filter exceeds 2 MiB")
	}
	return raw, nil
}
