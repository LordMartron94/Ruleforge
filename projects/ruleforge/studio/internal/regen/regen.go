// Package regen is the scheduler: it refreshes data providers, compiles the project's filters when the inputs changed, and
// publishes them atomically. A failed refresh or compile never overwrites a working filter.
package regen

import (
	"context"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"sync"
	"time"

	"github.com/LordMartron94/Ruleforge/studio/internal/config"
	"github.com/LordMartron94/Ruleforge/studio/internal/engine"
	"github.com/LordMartron94/Ruleforge/studio/internal/provider"
)

// Regenerator owns one project's refresh-and-publish cycle.
type Regenerator struct {
	Project *config.Project
	Engine  engine.Engine
	Ninja   *provider.Ninja
	Log     func(format string, args ...any)

	mutex  sync.Mutex
	status Status
	last   map[string]time.Time // dataset source -> last refresh
}

// OutputStatus is the state of one published filter.
type OutputStatus struct {
	Name   string `json:"name"`
	Path   string `json:"path"`
	Hash   string `json:"sha256,omitempty"`
	Blocks int    `json:"blocks"`
	Error  string `json:"error,omitempty"`
}

// Status is what the last cycle did, for the studio and the manifest.
type Status struct {
	Diagnostics []engine.Diagnostic `json:"diagnostics"`
	Enabled     bool                `json:"enabled"`
	LastRun     time.Time           `json:"lastRun"`
	Inputs      string              `json:"inputs"`
	Outputs     []OutputStatus      `json:"outputs"`
	Messages    []string            `json:"messages"`
	Skipped     bool                `json:"skipped"`
}

func (r *Regenerator) logf(format string, args ...any) {
	if r.Log != nil {
		r.Log(format, args...)
	}
}

// Status returns the last cycle's report.
func (r *Regenerator) Status() any {
	r.mutex.Lock()
	defer r.mutex.Unlock()
	status := r.status
	status.Enabled = r.Project.Regeneration.Enabled
	return status
}

func (r *Regenerator) path(relative string) string {
	if filepath.IsAbs(relative) {
		return relative
	}
	return filepath.Join(r.Project.Dir, relative)
}

// Refresh fetches every provider dataset that is due and returns messages for those that failed (their old snapshot stays).
func (r *Regenerator) Refresh(ctx context.Context, force bool) []string {
	var messages []string
	if r.Project.Offline {
		return messages
	}
	if r.last == nil {
		r.last = map[string]time.Time{}
	}
	for _, dataset := range r.Project.Datasets {
		if dataset.Provider == "neversink-divination" {
			interval, err := config.Interval(dataset.Refresh, 24*time.Hour)
			if err != nil {
				messages = append(messages, fmt.Sprintf("dataset %s: %v", dataset.Source, err))
				continue
			}
			if !force && time.Since(r.last[dataset.Source]) < interval {
				continue
			}
			if err := provider.PrepareDivination(ctx, r.path(dataset.Path)); err != nil {
				messages = append(messages, fmt.Sprintf("dataset %s kept its previous snapshot: %v", dataset.Source, err))
			} else {
				r.last[dataset.Source] = time.Now()
				r.logf("refreshed %s from NeverSink", dataset.Source)
			}
			continue
		}
		if !strings.HasPrefix(dataset.Provider, "poe-ninja") {
			continue
		}
		interval, err := config.Interval(dataset.Refresh, time.Hour)
		if err != nil {
			messages = append(messages, fmt.Sprintf("dataset %s: bad refresh %q", dataset.Source, dataset.Refresh))
			continue
		}
		if !force && time.Since(r.last[dataset.Source]) < interval {
			continue
		}
		ninja := r.Ninja
		if ninja == nil {
			ninja = &provider.Ninja{}
		}
		league, err := ninja.ResolveLeague(ctx, dataset.League)
		var rows []provider.Row
		if err == nil {
			if dataset.Provider == "poe-ninja-exchange" {
				rows, err = ninja.Exchange(ctx, league, dataset.Type)
			} else {
				rows, err = ninja.Stash(ctx, league, dataset.Type)
			}
		}
		if err != nil {
			messages = append(messages, fmt.Sprintf("dataset %s kept its previous snapshot: %v", dataset.Source, err))
			continue
		}
		if _, err := provider.WriteCSV(r.path(dataset.Path), rows); err != nil {
			messages = append(messages, fmt.Sprintf("dataset %s: %v", dataset.Source, err))
			continue
		}
		r.last[dataset.Source] = time.Now()
		r.logf("refreshed %s (%d rows, league %s)", dataset.Source, len(rows), league)
	}
	return messages
}

// inputsHash fingerprints everything the filters depend on that is not the sources: the snapshots and the context values.
func (r *Regenerator) inputsHash() string {
	hash := sha256.New()
	paths := []string{}
	for _, dataset := range r.Project.Datasets {
		paths = append(paths, dataset.Path)
	}
	sort.Strings(paths)
	for _, path := range provider.GamePaths(r.Project) {
		paths = append(paths, path)
	}
	if r.Project.EconomyHistory != nil {
		paths = append(paths, provider.HistoryPath(r.Project), provider.HistoryQuotesPath(r.Project))
	}
	sort.Strings(paths)
	for _, path := range paths {
		data, err := os.ReadFile(r.path(path))
		fmt.Fprintf(hash, "%s:%d:%v\n", path, len(data), err == nil)
		hash.Write(data)
	}
	keys := make([]string, 0, len(r.Project.Context))
	for key := range r.Project.Context {
		keys = append(keys, key)
	}
	sort.Strings(keys)
	for _, key := range keys {
		fmt.Fprintf(hash, "%s=%s\n", key, r.Project.Context[key])
	}
	if r.Project.Character != nil {
		fmt.Fprintf(hash, "character=%s/%d\n", r.Project.Character.Name, r.Project.Character.Level)
	}
	// The sources too: a regeneration after an edit must not be skipped as "unchanged".
	_ = filepath.WalkDir(r.Project.Dir, func(path string, entry os.DirEntry, err error) error {
		if err == nil && !entry.IsDir() && strings.HasSuffix(path, ".rf") {
			data, _ := os.ReadFile(path)
			hash.Write(data)
		}
		return nil
	})
	return hex.EncodeToString(hash.Sum(nil))
}

func atomicWrite(path string, data []byte) error {
	if err := os.MkdirAll(filepath.Dir(path), 0o755); err != nil {
		return err
	}
	temp, err := os.CreateTemp(filepath.Dir(path), ".publish-*")
	if err != nil {
		return err
	}
	name := temp.Name()
	if _, err := temp.Write(data); err != nil {
		temp.Close()
		os.Remove(name)
		return err
	}
	if err := temp.Close(); err != nil {
		os.Remove(name)
		return err
	}
	return os.Rename(name, path)
}

// RunOnce performs one cycle. With force it refreshes every provider and compiles even when nothing changed.
func (r *Regenerator) RunOnce(ctx context.Context, force bool) error {
	r.mutex.Lock()
	defer r.mutex.Unlock()
	messages := r.Refresh(ctx, force)
	if err := provider.PrepareGameData(ctx, r.Project); err != nil {
		messages = append(messages, "game data kept its previous snapshot: "+err.Error())
	}
	if err := provider.PrepareHistory(ctx, r.Project); err != nil {
		messages = append(messages, "economy history kept its previous snapshot: "+err.Error())
	}
	for _, message := range messages {
		r.logf("%s", message)
	}
	inputs := r.inputsHash()
	status := Status{LastRun: time.Now().UTC(), Inputs: inputs, Messages: messages, Enabled: r.Project.Regeneration.Enabled}
	if !force && inputs == r.status.Inputs && len(r.status.Outputs) > 0 {
		status.Skipped = true
		status.Outputs = r.status.Outputs
		status.Diagnostics = r.status.Diagnostics
		r.status = status
		return nil
	}
	outputs := r.Project.Outputs
	if len(outputs) == 0 {
		outputs = []config.Output{{Name: "ruleforge", Path: "out/ruleforge.filter"}}
	}
	var firstError error
	var stateRequest engine.Request
	seenDiagnostics := map[engine.Diagnostic]bool{}
	for _, output := range outputs {
		request := engine.Request{Dir: r.Project.Dir, Entry: r.Project.Entry, Include: r.Project.Include, Name: output.Name, Set: map[string]string{}, Datasets: map[string]string{}}
		for key, value := range r.Project.Context {
			request.Set[key] = value
		}
		if r.Project.Character != nil && r.Project.Character.Level > 0 {
			request.Set["character.level"] = fmt.Sprint(r.Project.Character.Level)
		}
		for key, value := range output.Set {
			request.Set[key] = value
		}
		for _, dataset := range r.Project.Datasets {
			request.Datasets[dataset.Source] = dataset.Path
		}
		for source, path := range provider.GamePaths(r.Project) {
			request.Datasets[source] = path
		}
		if r.Project.EconomyHistory != nil {
			request.Datasets["economy.history"] = provider.HistoryPath(r.Project)
			request.Datasets["economy.quotes"] = provider.HistoryQuotesPath(r.Project)
		}
		stateRequest = request
		item := OutputStatus{Name: output.Name, Path: output.Path}
		result, err := r.Engine.Compile(ctx, request)
		for _, diagnostic := range result.Diagnostics {
			if !seenDiagnostics[diagnostic] {
				seenDiagnostics[diagnostic] = true
				status.Diagnostics = append(status.Diagnostics, diagnostic)
				r.logf("%s:%d:%d: %s %s: %s", diagnostic.File, diagnostic.Line, diagnostic.Column, diagnostic.Severity, diagnostic.Code, diagnostic.Message)
			}
		}
		switch {
		case err != nil:
			item.Error = err.Error()
		case !result.OK || strings.TrimSpace(result.Filter) == "":
			item.Error = "the compile failed; the previous filter is kept"
			for _, d := range result.Diagnostics {
				if d.Severity == "error" {
					item.Error = fmt.Sprintf("%s:%d: %s", d.File, d.Line, d.Message)
					break
				}
			}
		default:
			sum := sha256.Sum256([]byte(result.Filter))
			item.Hash = hex.EncodeToString(sum[:])
			var model struct {
				Blocks []json.RawMessage `json:"blocks"`
			}
			_ = json.Unmarshal(result.Model, &model)
			item.Blocks = len(model.Blocks)
			if err := atomicWrite(r.path(output.Path), []byte(result.Filter)); err != nil {
				item.Error = err.Error()
			}
		}
		if item.Error != "" && firstError == nil {
			firstError = fmt.Errorf("%s: %s", output.Name, item.Error)
		}
		status.Outputs = append(status.Outputs, item)
	}
	if firstError == nil {
		pending := map[string][]byte{}
		for _, state := range r.Project.StateExpressions {
			path := ""
			for _, dataset := range r.Project.Datasets {
				if dataset.Source == state.Source && dataset.Provider == "json" {
					path = r.path(dataset.Path)
					break
				}
			}
			if path == "" || state.Module == "" || state.Expression == "" {
				firstError = fmt.Errorf("state expression %q needs a configured JSON dataset, module and expression", state.Source)
				break
			}
			values, err := r.Engine.Eval(ctx, stateRequest, []string{state.Module}, []string{state.Expression})
			if err != nil {
				firstError = fmt.Errorf("state %s retained its previous value: %w", state.Source, err)
				break
			}
			pending[path] = append([]byte(values[0]), '\n')
		}
		if firstError == nil {
			for path, value := range pending {
				if err := atomicWrite(path, value); err != nil {
					firstError = err
					break
				}
			}
		}
		// A state write alone is not fresh market evidence and must not cause a
		// perpetual regeneration cycle with otherwise unchanged inputs.
		status.Inputs = r.inputsHash()
	}
	r.status = status
	manifest, _ := json.MarshalIndent(status, "", "  ")
	_ = atomicWrite(filepath.Join(r.Project.Dir, ".ruleforge", "manifest.json"), append(manifest, '\n'))
	if firstError != nil {
		r.logf("regeneration finished with errors: %v", firstError)
	}
	return firstError
}

// Loop runs cycles until the context ends, no more often than the configured minimum interval.
func (r *Regenerator) Loop(ctx context.Context) {
	interval, err := config.Interval(r.Project.Regeneration.MinimumInterval, 5*time.Minute)
	if err != nil || interval < time.Second {
		interval = 5 * time.Minute
	}
	timer := time.NewTimer(0)
	defer timer.Stop()
	for {
		select {
		case <-ctx.Done():
			return
		case <-timer.C:
			if err := r.RunOnce(ctx, false); err != nil {
				r.logf("%v", err)
			}
			timer.Reset(interval)
		}
	}
}
