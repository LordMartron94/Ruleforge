// Package engine runs the Ruleforge C engine (the `ruleforge` executable) and reads its JSON products.
//
// The engine is the single authority on what a script means: parsing, checking, evaluation, lowering and simulation all
// happen there. This package only starts it, passes the project's inputs, and returns what it wrote, so the studio can never
// disagree with the compiler about a filter.
package engine

import (
	"bytes"
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"sort"
	"strings"
)

// Diagnostic is one problem the engine reported, with a source location.
type Diagnostic struct {
	Severity  string `json:"severity"`
	Code      string `json:"code"`
	Message   string `json:"message"`
	File      string `json:"file"`
	Line      int    `json:"line"`
	Column    int    `json:"column"`
	EndLine   int    `json:"endLine"`
	EndColumn int    `json:"endColumn"`
	Note      string `json:"note,omitempty"`
}

// Request names the inputs of one engine run. Paths are relative to Dir.
type Request struct {
	Dir      string            // the project folder; the engine runs there
	Entry    string            // the entry .rf file
	Include  []string          // folders searched for imports
	Settings string            // an engine settings file (TOML), or empty
	Set      map[string]string // compile-time values, readable as @key
	Datasets map[string]string // dataset source -> snapshot path
	Name     string            // the filter's name
}

// Result is what a compile produced.
type Result struct {
	OK          bool            `json:"ok"`
	Filter      string          `json:"filter,omitempty"`
	Model       json.RawMessage `json:"model,omitempty"`
	Diagnostics []Diagnostic    `json:"diagnostics"`
}

// Engine locates and runs the ruleforge executable.
type Engine struct {
	Progress io.Writer // optional live Echo stderr stream; JSON products remain on stdout
	LogDir   string
	LogKeep  int
	Path     string
}

// Locate finds the engine: an explicit path, then next to the running executable, then PATH.
func Locate(explicit string) (Engine, error) {
	name := "ruleforge"
	if runtime.GOOS == "windows" {
		name += ".exe"
	}
	candidates := []string{}
	if explicit != "" {
		candidates = append(candidates, explicit)
	}
	if self, err := os.Executable(); err == nil {
		candidates = append(candidates, filepath.Join(filepath.Dir(self), name))
	}
	for _, candidate := range candidates {
		if info, err := os.Stat(candidate); err == nil && !info.IsDir() {
			return Engine{Path: candidate}, nil
		}
	}
	if found, err := exec.LookPath(name); err == nil {
		return Engine{Path: found}, nil
	}
	return Engine{}, fmt.Errorf("the ruleforge engine was not found (looked next to this program and on PATH); pass --engine PATH")
}

func (r Request) arguments() []string {
	args := []string{}
	for _, include := range r.Include {
		args = append(args, "-I", include)
	}
	if r.Settings != "" {
		args = append(args, "--settings", r.Settings)
	}
	keys := make([]string, 0, len(r.Set))
	for key := range r.Set {
		keys = append(keys, key)
	}
	sort.Strings(keys)
	for _, key := range keys {
		args = append(args, "--set", key+"="+r.Set[key])
	}
	sources := make([]string, 0, len(r.Datasets))
	for source := range r.Datasets {
		sources = append(sources, source)
	}
	sort.Strings(sources)
	for _, source := range sources {
		args = append(args, "--dataset", source+"="+r.Datasets[source])
	}
	if r.Name != "" {
		args = append(args, "--name", r.Name)
	}
	return args
}

func (e Engine) run(ctx context.Context, dir string, args ...string) (stdout, stderr []byte, status int, err error) {
	if e.LogDir != "" && len(args) > 0 && args[0] != "version" {
		keep := e.LogKeep
		if keep == 0 {
			keep = 10
		}
		args = append(args, "--log-dir", e.LogDir, "--log-keep", fmt.Sprint(keep))
	}
	if e.Progress != nil {
		args = append(args, "--progress")
	}
	command := exec.CommandContext(ctx, e.Path, args...)
	command.Dir = dir
	var out, errors_ bytes.Buffer
	command.Stdout = &out
	command.Stderr = &errors_
	if e.Progress != nil {
		command.Stderr = io.MultiWriter(&errors_, e.Progress)
	}
	runErr := command.Run()
	var exit *exec.ExitError
	if errors.As(runErr, &exit) {
		return out.Bytes(), errors_.Bytes(), exit.ExitCode(), nil
	}
	if runErr != nil {
		return nil, nil, -1, runErr
	}
	return out.Bytes(), errors_.Bytes(), 0, nil
}

func parseDiagnostics(stdout []byte) []Diagnostic {
	trimmed := bytes.TrimSpace(stdout)
	if len(trimmed) == 0 || trimmed[0] != '[' {
		return []Diagnostic{}
	}
	end := bytes.LastIndexByte(trimmed, ']')
	diagnostics := []Diagnostic{}
	if end >= 0 {
		_ = json.Unmarshal(trimmed[:end+1], &diagnostics)
	}
	return diagnostics
}

// Compile runs the entry's main and returns the filter, the structured model and any diagnostics.
func (e Engine) Compile(ctx context.Context, req Request) (Result, error) {
	temp, err := os.MkdirTemp("", "ruleforge-compile-")
	if err != nil {
		return Result{}, err
	}
	defer os.RemoveAll(temp)
	filter := filepath.Join(temp, "out.filter")
	model := filepath.Join(temp, "model.json")
	args := append([]string{"compile", req.Entry, "--json", "-o", filter, "--model", model}, req.arguments()...)
	stdout, stderr, status, err := e.run(ctx, req.Dir, args...)
	if err != nil {
		return Result{}, err
	}
	result := Result{Diagnostics: parseDiagnostics(stdout)}
	if status == 0 {
		filterBytes, readErr := os.ReadFile(filter)
		modelBytes, modelErr := os.ReadFile(model)
		if readErr != nil || modelErr != nil {
			return Result{}, fmt.Errorf("the engine reported success but wrote no product: %v %v", readErr, modelErr)
		}
		result.OK = true
		result.Filter = string(filterBytes)
		result.Model = modelBytes
		warnings, err := validateCatalog(req, result.Model)
		if err != nil {
			return Result{}, err
		}
		result.Diagnostics = append(result.Diagnostics, warnings...)
		return result, nil
	}
	if len(result.Diagnostics) == 0 && len(bytes.TrimSpace(stderr)) > 0 {
		result.Diagnostics = []Diagnostic{{Severity: "error", Code: "RF9000", Message: strings.TrimSpace(string(stderr))}}
	}
	return result, nil
}

// Check parses the sources and reports diagnostics without evaluating.
func (e Engine) Check(ctx context.Context, req Request) ([]Diagnostic, error) {
	args := append([]string{"check", req.Entry, "--json"}, req.arguments()...)
	stdout, stderr, status, err := e.run(ctx, req.Dir, args...)
	if err != nil {
		return nil, err
	}
	diagnostics := parseDiagnostics(stdout)
	if status != 0 && len(diagnostics) == 0 && len(bytes.TrimSpace(stderr)) > 0 {
		diagnostics = []Diagnostic{{Severity: "error", Code: "RF9000", Message: strings.TrimSpace(string(stderr))}}
	}
	return diagnostics, nil
}

// AST returns the syntax trees of the entry and its imports as the engine's JSON interchange form.
func (e Engine) AST(ctx context.Context, req Request) (json.RawMessage, []Diagnostic, error) {
	args := append([]string{"ast", req.Entry, "--json"}, req.arguments()...)
	stdout, _, status, err := e.run(ctx, req.Dir, args...)
	if err != nil {
		return nil, nil, err
	}
	if status != 0 {
		return nil, parseDiagnostics(stdout), nil
	}
	trimmed := bytes.TrimSpace(stdout)
	// Diagnostics (warnings) precede the document when both are present.
	if index := bytes.Index(trimmed, []byte("{\"format\":\"ruleforge-syntax\"")); index >= 0 {
		return json.RawMessage(trimmed[index:]), parseDiagnostics(trimmed[:index]), nil
	}
	return nil, parseDiagnostics(stdout), nil
}

// Themes returns the themes and color sets of the entry and its imports, interpreted by the engine, with the byte span of every color.
func (e Engine) Themes(ctx context.Context, req Request) (json.RawMessage, []Diagnostic, error) {
	args := append([]string{"themes", req.Entry, "--json"}, req.arguments()...)
	stdout, _, status, err := e.run(ctx, req.Dir, args...)
	if err != nil {
		return nil, nil, err
	}
	if status != 0 {
		return nil, parseDiagnostics(stdout), nil
	}
	trimmed := bytes.TrimSpace(stdout)
	if index := bytes.Index(trimmed, []byte("{\"format\":\"ruleforge-themes\"")); index >= 0 {
		return json.RawMessage(trimmed[index:]), parseDiagnostics(trimmed[:index]), nil
	}
	return nil, parseDiagnostics(stdout), nil
}

// Simulate compiles and runs the items (a JSON array or object) through the filter.
func (e Engine) Simulate(ctx context.Context, req Request, items json.RawMessage) (json.RawMessage, []Diagnostic, error) {
	temp, err := os.MkdirTemp("", "ruleforge-simulate-")
	if err != nil {
		return nil, nil, err
	}
	defer os.RemoveAll(temp)
	itemsPath := filepath.Join(temp, "items.json")
	if err := os.WriteFile(itemsPath, items, 0o600); err != nil {
		return nil, nil, err
	}
	args := append([]string{"simulate", req.Entry, "--json", "--items", itemsPath}, req.arguments()...)
	stdout, stderr, status, err := e.run(ctx, req.Dir, args...)
	if err != nil {
		return nil, nil, err
	}
	trimmed := bytes.TrimSpace(stdout)
	if status != 0 {
		diagnostics := parseDiagnostics(stdout)
		if len(diagnostics) == 0 && len(bytes.TrimSpace(stderr)) > 0 {
			diagnostics = []Diagnostic{{Severity: "error", Code: "RF9000", Message: strings.TrimSpace(string(stderr))}}
		}
		return nil, diagnostics, nil
	}
	// The results follow any warnings: find the last top-level array.
	if index := bytes.LastIndex(trimmed, []byte("\n[")); index >= 0 {
		return json.RawMessage(trimmed[index+1:]), parseDiagnostics(trimmed[:index]), nil
	}
	return json.RawMessage(trimmed), nil, nil
}

// Version asks the engine for its version line.
func (e Engine) Version(ctx context.Context) string {
	stdout, _, _, err := e.run(ctx, "", "version")
	if err != nil {
		return ""
	}
	return strings.TrimSpace(string(stdout))
}

// Tokens lexes text with the Ruleforge grammar and returns [[start,end,"kind"],...] for highlighting. The text need not parse.
func (e Engine) Tokens(ctx context.Context, text []byte) (json.RawMessage, error) {
	temp, err := os.MkdirTemp("", "ruleforge-tokens-")
	if err != nil {
		return nil, err
	}
	defer os.RemoveAll(temp)
	path := filepath.Join(temp, "source.rf")
	if err := os.WriteFile(path, text, 0o600); err != nil {
		return nil, err
	}
	stdout, stderr, status, err := e.run(ctx, temp, "tokens", path)
	if err != nil {
		return nil, err
	}
	if status != 0 {
		return nil, fmt.Errorf("tokens: %s", strings.TrimSpace(string(stderr)))
	}
	return json.RawMessage(bytes.TrimSpace(stdout)), nil
}

// Manifest returns everything the engine can express (conditions, style properties, library functions).
func (e Engine) Manifest(ctx context.Context) (json.RawMessage, error) {
	stdout, stderr, status, err := e.run(ctx, "", "manifest")
	if err != nil {
		return nil, err
	}
	if status != 0 {
		return nil, fmt.Errorf("manifest: %s", strings.TrimSpace(string(stderr)))
	}
	return json.RawMessage(bytes.TrimSpace(stdout)), nil
}
