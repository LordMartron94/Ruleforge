package engine

import (
	"context"
	"os"
	"path/filepath"
	"runtime"
	"strings"
	"testing"
	"time"
)

type progressSignal struct{ seen chan string }

func (p progressSignal) Write(data []byte) (int, error) {
	select {
	case p.seen <- string(data):
	default:
	}
	return len(data), nil
}

func TestProgressArrivesBeforeProcessExitAndLeavesJSONIntact(t *testing.T) {
	if runtime.GOOS == "windows" {
		t.Skip("shell fixture")
	}
	dir := t.TempDir()
	release := filepath.Join(dir, "release")
	path := filepath.Join(dir, "engine")
	script := "#!/bin/sh\necho 'phase-start' >&2\nwhile [ ! -f '" + release + "' ]; do sleep 0.01; done\nprintf '[1,2,3]\\n'\n"
	if err := os.WriteFile(path, []byte(script), 0o755); err != nil {
		t.Fatal(err)
	}
	signal := progressSignal{make(chan string, 2)}
	e := Engine{Path: path, Progress: signal}
	done := make(chan error, 1)
	ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
	defer cancel()
	go func() {
		out, _, status, err := e.run(ctx, dir, "compile")
		if err == nil && (status != 0 || strings.TrimSpace(string(out)) != "[1,2,3]") {
			err = os.ErrInvalid
		}
		done <- err
	}()
	select {
	case text := <-signal.seen:
		if !strings.Contains(text, "phase-start") {
			t.Fatal(text)
		}
	case <-ctx.Done():
		t.Fatal("stderr was buffered until exit")
	}
	if err := os.WriteFile(release, nil, 0o644); err != nil {
		t.Fatal(err)
	}
	if err := <-done; err != nil {
		t.Fatal(err)
	}
}
