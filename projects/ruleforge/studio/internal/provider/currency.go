package provider

import (
	"context"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"regexp"
	"strconv"
	"strings"
)

// CurrencyTier preserves the regular currency tiers and explicit stack upgrades
// published by NeverSink. The consuming module owns its palette and strictness.
type CurrencyTier struct {
	BaseType  string `json:"baseType"`
	Tier      int    `json:"tier"`
	Group     string `json:"group"`
	StackSize int    `json:"stackSize"`
	Version   string `json:"upstreamVersion"`
	Source    string `json:"source"`
	SHA256    string `json:"sha256"`
}

func parseCurrency(data []byte) ([]CurrencyTier, error) {
	text := strings.ReplaceAll(string(data), "\r\n", "\n")
	version := regexp.MustCompile(`(?m)^# VERSION:\s+(\S+)`).FindStringSubmatch(text)
	if len(version) != 2 {
		return nil, fmt.Errorf("NeverSink filter has no version header")
	}
	sum := sha256.Sum256(data)
	digest := hex.EncodeToString(sum[:])
	marker := regexp.MustCompile(`\$type->(currency(?:->stackedthree|->stackedsix)?)\s+\$tier->(\S+)`)
	tierNumber := regexp.MustCompile(`^t([1-9])[a-z]*$`)
	names := regexp.MustCompile(`"(?:\\.|[^"\\])*"`)
	rows := []CurrencyTier{}
	seen := map[string]bool{}
	groups := map[string]bool{}
	for _, block := range strings.Split(text, "\n\n") {
		lines := strings.Split(strings.TrimSpace(block), "\n")
		if len(lines) < 2 || (!strings.HasPrefix(lines[0], "Show ") && !strings.HasPrefix(lines[0], "Hide ")) {
			continue
		}
		match := marker.FindStringSubmatch(lines[0])
		if len(match) != 3 {
			continue
		}
		if match[2] == "restex" || match[2] == "untiereditems" || match[2] == "scrollfragments" || match[2] == "tportal" || match[2] == "twisdom" {
			continue // Generic and supply rules are separate from the core tier list.
		}
		tierMatch := tierNumber.FindStringSubmatch(match[2])
		if len(tierMatch) != 2 {
			return nil, fmt.Errorf("unknown NeverSink currency tier %q", match[2])
		}
		tier, _ := strconv.Atoi(tierMatch[1])
		group, stack := "single", 1
		if match[1] == "currency->stackedthree" {
			group, stack = "stacked-three", 3
		}
		if match[1] == "currency->stackedsix" {
			group, stack = "stacked-six", 6
		}
		baseLine, validClass, validStack := "", false, stack == 1
		for _, line := range lines[1:] {
			line = strings.TrimSpace(line)
			switch {
			case line == `Class == "Stackable Currency"`:
				validClass = true
			case strings.HasPrefix(line, "BaseType == "):
				if baseLine != "" {
					return nil, fmt.Errorf("repeated NeverSink currency base condition")
				}
				baseLine = strings.TrimPrefix(line, "BaseType == ")
			case strings.HasPrefix(line, "StackSize "):
				validStack = line == fmt.Sprintf("StackSize >= %d", stack)
			case strings.HasPrefix(line, "AreaLevel ") || strings.HasPrefix(line, "BaseType ") || strings.HasPrefix(line, "Class "):
				return nil, fmt.Errorf("unsupported NeverSink currency condition %q", line)
			}
		}
		if !validClass || !validStack || baseLine == "" {
			return nil, fmt.Errorf("incomplete NeverSink currency group %s/%s", group, match[2])
		}
		for _, quoted := range names.FindAllString(baseLine, -1) {
			name, err := strconv.Unquote(quoted)
			if err != nil {
				return nil, err
			}
			key := fmt.Sprintf("%s/%d", name, stack)
			if seen[key] {
				return nil, fmt.Errorf("duplicate NeverSink currency %s", key)
			}
			seen[key] = true
			rows = append(rows, CurrencyTier{name, tier, group, stack, version[1], divinationURL, digest})
			groups[fmt.Sprintf("%s/%d", group, tier)] = true
		}
	}
	for tier := 1; tier <= 9; tier++ {
		if !groups[fmt.Sprintf("single/%d", tier)] {
			return nil, fmt.Errorf("NeverSink currency tier %d is missing; snapshot retained", tier)
		}
	}
	if !groups["stacked-three/1"] || !groups["stacked-six/1"] {
		return nil, fmt.Errorf("NeverSink currency stack tiers are missing; snapshot retained")
	}
	return rows, nil
}

// PrepareCurrency refreshes NeverSink's core currency and stack tier membership.
func PrepareCurrency(ctx context.Context, path string) error {
	raw, err := fetchNeverSink(ctx)
	if err != nil {
		return err
	}
	rows, err := parseCurrency(raw)
	if err != nil {
		return err
	}
	encoded, err := json.MarshalIndent(rows, "", "  ")
	if err != nil {
		return err
	}
	return writeSnapshot(path, append(encoded, '\n'))
}
