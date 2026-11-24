package regen

import (
	"context"
	"os"
	"path/filepath"
	"runtime"
	"strings"
	"testing"

	"github.com/LordMartron94/Ruleforge/studio/internal/config"
	"github.com/LordMartron94/Ruleforge/studio/internal/engine"
)

// fakeEngine writes a shell script that behaves like `ruleforge compile`: it copies a fixed filter to -o and a model to
// --model, or fails with a diagnostic, depending on whether the marker file exists.
func fakeEngine(t *testing.T, dir string) engine.Engine {
	if runtime.GOOS == "windows" {
		t.Skip("the fake engine is a shell script")
	}
	script := `#!/bin/sh
out=""; model=""
while [ $# -gt 0 ]; do
  case "$1" in -o) out="$2"; shift;; --model) model="$2"; shift;; esac
  shift
done
if [ -f "` + filepath.Join(dir, "fail") + `" ]; then
  echo '[{"severity":"error","code":"RF0001","message":"broken","file":"entry.rf","line":3,"column":1,"endLine":3,"endColumn":2}]'
  exit 1
fi
printf 'Show\n\tBaseType == "A"\n' > "$out"
printf '{"blocks":[{"id":1}]}' > "$model"
`
	path := filepath.Join(dir, "fake-ruleforge")
	if err := os.WriteFile(path, []byte(script), 0o755); err != nil {
		t.Fatal(err)
	}
	return engine.Engine{Path: path}
}

func TestFailedCompileKeepsLastValidFilter(t *testing.T) {
	dir := t.TempDir()
	project := &config.Project{Dir: dir, Entry: "entry.rf", Outputs: []config.Output{{Name: "f", Path: "out/f.filter"}}}
	if err := os.WriteFile(filepath.Join(dir, "entry.rf"), []byte("version 1.0.0;"), 0o644); err != nil {
		t.Fatal(err)
	}
	r := &Regenerator{Project: project, Engine: fakeEngine(t, dir)}
	if err := r.RunOnce(context.Background(), true); err != nil {
		t.Fatal(err)
	}
	published := filepath.Join(dir, "out", "f.filter")
	good, err := os.ReadFile(published)
	if err != nil || !strings.Contains(string(good), "BaseType") {
		t.Fatalf("first publish: %q %v", good, err)
	}
	if err := os.WriteFile(filepath.Join(dir, "fail"), nil, 0o644); err != nil {
		t.Fatal(err)
	}
	if err := r.RunOnce(context.Background(), true); err == nil {
		t.Fatal("a failing compile must be reported")
	}
	after, _ := os.ReadFile(published)
	if string(after) != string(good) {
		t.Fatalf("the last valid filter was overwritten: %q", after)
	}
	status := r.Status().(Status)
	if len(status.Outputs) != 1 || !strings.Contains(status.Outputs[0].Error, "broken") {
		t.Errorf("status should carry the diagnostic: %+v", status.Outputs)
	}
}

func TestUnchangedInputsSkipRecompile(t *testing.T) {
	dir := t.TempDir()
	project := &config.Project{Dir: dir, Entry: "entry.rf", Outputs: []config.Output{{Name: "f", Path: "out/f.filter"}}}
	if err := os.WriteFile(filepath.Join(dir, "entry.rf"), []byte("version 1.0.0;"), 0o644); err != nil {
		t.Fatal(err)
	}
	r := &Regenerator{Project: project, Engine: fakeEngine(t, dir)}
	if err := r.RunOnce(context.Background(), false); err != nil {
		t.Fatal(err)
	}
	if err := r.RunOnce(context.Background(), false); err != nil {
		t.Fatal(err)
	}
	if !r.Status().(Status).Skipped {
		t.Error("the second run with identical inputs must be skipped")
	}
	if err := os.WriteFile(filepath.Join(dir, "entry.rf"), []byte("version 1.0.0; // edited"), 0o644); err != nil {
		t.Fatal(err)
	}
	if err := r.RunOnce(context.Background(), false); err != nil || r.Status().(Status).Skipped {
		t.Errorf("a source edit must trigger a regeneration (err=%v)", err)
	}
}

func TestProjectReportsWarningsOnceAcrossOutputs(t *testing.T) {
	dir := t.TempDir()
	os.WriteFile(filepath.Join(dir, "entry.rf"), []byte("version 1.0.0;"), 0o644)
	eng := fakeEngine(t, dir)
	data, err := os.ReadFile(eng.Path)
	if err != nil {
		t.Fatal(err)
	}
	script := strings.Replace(string(data), "printf 'Show", `echo '[{"severity":"warning","code":"RF0598","message":"shared warning","file":"entry.rf","line":1}]'
printf 'Show`, 1)
	if err := os.WriteFile(eng.Path, []byte(script), 0o755); err != nil {
		t.Fatal(err)
	}
	project := &config.Project{Dir: dir, Entry: "entry.rf", Outputs: []config.Output{{Name: "one", Path: "one.filter"}, {Name: "two", Path: "two.filter"}}}
	messages := 0
	regenerator := &Regenerator{Project: project, Engine: eng, Log: func(format string, args ...any) {
		if strings.Contains(format, "%s:%d:%d") {
			messages++
		}
	}}
	if err := regenerator.RunOnce(context.Background(), true); err != nil {
		t.Fatal(err)
	}
	status := regenerator.Status().(Status)
	if len(status.Diagnostics) != 1 || messages != 1 || len(status.Outputs) != 2 {
		t.Fatalf("duplicate diagnostics: %+v, log count %d", status, messages)
	}
}
