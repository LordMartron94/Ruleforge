package engine

import (
	"bytes"
	"context"
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"strings"
)

// Item interprets the engine's Genesis grammar without requiring a compiled filter.
func (e Engine) Item(ctx context.Context, dir, text string) (json.RawMessage, error) {
	temp, err := os.MkdirTemp("", "ruleforge-item-")
	if err != nil {
		return nil, err
	}
	defer os.RemoveAll(temp)
	path := filepath.Join(temp, "item.txt")
	if err := os.WriteFile(path, []byte(text), 0o600); err != nil {
		return nil, err
	}
	out, stderr, status, err := e.run(ctx, dir, "item", path)
	if err != nil {
		return nil, err
	}
	if status != 0 {
		return nil, fmt.Errorf("item: %s", strings.TrimSpace(string(stderr)))
	}
	out = bytes.TrimSpace(out)
	if !json.Valid(out) {
		return nil, fmt.Errorf("invalid item product")
	}
	return out, nil
}
