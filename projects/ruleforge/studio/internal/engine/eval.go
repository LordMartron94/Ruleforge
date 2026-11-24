package engine

import (
	"bytes"
	"context"
	"encoding/json"
	"fmt"
	"strings"
)

// Eval asks the embedded Ruleforge modules for results; it does not duplicate their algorithms in the host.
func (e Engine) Eval(ctx context.Context, req Request, imports, expressions []string) ([]json.RawMessage, error) {
	if len(expressions) == 0 || len(expressions) > 512 {
		return nil, fmt.Errorf("eval accepts 1..512 expressions")
	}
	args := []string{"eval"}
	for _, module := range imports {
		args = append(args, "--import", module)
	}
	for _, expression := range expressions {
		args = append(args, "-e", expression)
	}
	args = append(args, req.arguments()...)
	stdout, stderr, status, err := e.run(ctx, req.Dir, args...)
	if err != nil {
		return nil, err
	}
	if status != 0 {
		return nil, fmt.Errorf("evaluation failed: %s", strings.TrimSpace(string(stderr)))
	}
	lines := bytes.Split(bytes.TrimSpace(stdout), []byte("\n"))
	if len(lines) != len(expressions) {
		return nil, fmt.Errorf("evaluation returned %d values, want %d", len(lines), len(expressions))
	}
	result := make([]json.RawMessage, len(lines))
	for i, line := range lines {
		if !json.Valid(line) {
			return nil, fmt.Errorf("invalid eval product")
		}
		result[i] = append(json.RawMessage(nil), line...)
	}
	return result, nil
}
