package main

import (
	"crypto/rand"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"net"
	"net/http"
	"os"
	"os/exec"
	"path/filepath"
	"regexp"
	"runtime"
	"sort"
	"strings"
	"sync"

	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/config"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/entry/studio"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/rules/document"
)

// studioServer serves the Studio with its editors. Every change is checked
// against the real compiler before it is written, and a file that changed on
// disk since the page read it is never overwritten.
type studioServer struct {
	app   *App
	token string
	addr  string

	mu   sync.Mutex // one check, save or recompile at a time
	page []byte
}

var (
	themeID    = regexp.MustCompile(`^[A-Za-z0-9][A-Za-z0-9._ -]*$`)
	scriptFile = regexp.MustCompile(`^[A-Za-z0-9][A-Za-z0-9._ -]*\.rf$`)
	sheetFile  = regexp.MustCompile(`^[A-Za-z0-9][A-Za-z0-9._ -]*\.toml$`)
)

// serveStudio serves the Studio until the process stops.
func (a *App) serveStudio() error {
	token := make([]byte, 24)
	if _, err := rand.Read(token); err != nil {
		return err
	}
	listener, err := net.Listen("tcp", a.studioAddr)
	if err != nil {
		return fmt.Errorf("the Studio cannot listen on %s: %w", a.studioAddr, err)
	}
	server := &studioServer{app: a, token: hex.EncodeToString(token), addr: listener.Addr().String()}
	if err := server.renderPage(); err != nil {
		return err
	}
	url := "http://" + server.addr + "/"
	a.log.Printf("Ruleforge Studio is running at %s (stop it with Ctrl+C)", url)
	if a.studioOpen {
		openBrowser(url)
	}
	return http.Serve(listener, server.routes())
}

func openBrowser(url string) {
	var command *exec.Cmd
	switch runtime.GOOS {
	case "darwin":
		command = exec.Command("open", url)
	case "windows":
		command = exec.Command("rundll32", "url.dll,FileProtocolHandler", url)
	default:
		command = exec.Command("xdg-open", url)
	}
	_ = command.Start()
}

func (s *studioServer) renderPage() error {
	data, err := s.app.studioData()
	if err != nil {
		return err
	}
	data.Server = &studio.ServerInfo{Token: s.token}
	page, err := studio.Render(data)
	if err != nil {
		return err
	}
	s.page = page
	return nil
}

func (s *studioServer) routes() http.Handler {
	mux := http.NewServeMux()
	mux.HandleFunc("GET /{$}", s.handlePage)
	mux.HandleFunc("GET /api/catalog", s.api(s.catalog))
	mux.HandleFunc("GET /api/themes", s.api(s.listThemes))
	mux.HandleFunc("GET /api/themes/{id}", s.api(s.getTheme))
	mux.HandleFunc("PUT /api/themes/{id}", s.api(s.putTheme))
	mux.HandleFunc("GET /api/stylesheets", s.api(s.listStylesheets))
	mux.HandleFunc("GET /api/stylesheets/{file}", s.api(s.getStylesheet))
	mux.HandleFunc("PUT /api/stylesheets/{file}", s.api(s.putStylesheet))
	mux.HandleFunc("GET /api/scripts", s.api(s.listScripts))
	mux.HandleFunc("GET /api/scripts/{file}", s.api(s.getScript))
	mux.HandleFunc("PUT /api/scripts/{file}", s.api(s.putScript))
	return s.guard(mux)
}

// guard only answers requests addressed to this server from its own page,
// so another website open in the browser cannot reach it.
func (s *studioServer) guard(next http.Handler) http.Handler {
	_, port, _ := net.SplitHostPort(s.addr)
	hosts := map[string]bool{s.addr: true, "localhost:" + port: true, "127.0.0.1:" + port: true, "[::1]:" + port: true}
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if !hosts[r.Host] {
			http.Error(w, "unknown host", http.StatusForbidden)
			return
		}
		if origin := r.Header.Get("Origin"); origin != "" && origin != "http://"+r.Host {
			http.Error(w, "cross-origin requests are not allowed", http.StatusForbidden)
			return
		}
		if strings.HasPrefix(r.URL.Path, "/api/") && r.Header.Get("X-Ruleforge-Token") != s.token {
			http.Error(w, "missing or wrong Studio token; reload the page", http.StatusForbidden)
			return
		}
		w.Header().Set("Cache-Control", "no-store")
		w.Header().Set("X-Content-Type-Options", "nosniff")
		w.Header().Set("Referrer-Policy", "no-referrer")
		next.ServeHTTP(w, r)
	})
}

func (s *studioServer) handlePage(w http.ResponseWriter, _ *http.Request) {
	s.mu.Lock()
	page := s.page
	s.mu.Unlock()
	w.Header().Set("Content-Type", "text/html; charset=utf-8")
	_, _ = w.Write(page)
}

// apiError is an error the page shows as is.
type apiError struct {
	status  int
	message string
}

func (e apiError) Error() string { return e.message }

func badRequest(format string, args ...any) error {
	return apiError{http.StatusBadRequest, fmt.Sprintf(format, args...)}
}

func conflict(format string, args ...any) error {
	return apiError{http.StatusConflict, fmt.Sprintf(format, args...)}
}

// api runs a handler under the server lock and writes its result as JSON.
func (s *studioServer) api(handler func(r *http.Request) (any, error)) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		s.mu.Lock()
		result, err := func() (result any, err error) {
			defer func() {
				if p := recover(); p != nil {
					err = fmt.Errorf("internal error: %v", p)
				}
			}()
			return handler(r)
		}()
		s.mu.Unlock()
		w.Header().Set("Content-Type", "application/json")
		if err != nil {
			status := http.StatusInternalServerError
			var known apiError
			if errors.As(err, &known) {
				status = known.status
			}
			w.WriteHeader(status)
			_ = json.NewEncoder(w).Encode(map[string]any{"ok": false, "error": err.Error()})
			return
		}
		_ = json.NewEncoder(w).Encode(result)
	}
}

func decodeBody(r *http.Request, into any) error {
	body := http.MaxBytesReader(nil, r.Body, 16<<20)
	decoder := json.NewDecoder(body)
	decoder.DisallowUnknownFields()
	if err := decoder.Decode(into); err != nil {
		return badRequest("could not read the request: %v", err)
	}
	return nil
}

// writeFileAtomically replaces a file in one step, so a failed write never leaves half a file.
func writeFileAtomically(path string, contents []byte) error {
	temp, err := os.CreateTemp(filepath.Dir(path), "."+filepath.Base(path)+".*")
	if err != nil {
		return err
	}
	defer os.Remove(temp.Name())
	if _, err := temp.Write(contents); err != nil {
		temp.Close()
		return err
	}
	if err := temp.Close(); err != nil {
		return err
	}
	if info, err := os.Stat(path); err == nil {
		_ = os.Chmod(temp.Name(), info.Mode().Perm())
	} else {
		_ = os.Chmod(temp.Name(), 0644)
	}
	return os.Rename(temp.Name(), path)
}

// fileHash is the hash of a file's current contents, or "" when it does not exist.
func fileHash(path string) (string, []byte, error) {
	contents, err := os.ReadFile(path)
	if errors.Is(err, os.ErrNotExist) {
		return "", nil, nil
	}
	if err != nil {
		return "", nil, err
	}
	return config.ContentHash(contents), contents, nil
}

// checkUnchanged refuses to overwrite a file that changed since the page read it.
func checkUnchanged(path, hash string, create bool) ([]byte, error) {
	current, contents, err := fileHash(path)
	if err != nil {
		return nil, err
	}
	switch {
	case create && current != "":
		return nil, conflict("%s already exists", filepath.Base(path))
	case !create && current == "":
		return nil, conflict("%s no longer exists", filepath.Base(path))
	case !create && current != hash:
		return nil, conflict("%s changed on disk since the Studio opened it. Reload it here to see the new version (this discards your unsaved changes here).", filepath.Base(path))
	}
	return contents, nil
}

// reload recompiles everything from disk after a save, writing the filters
// and the Studio file, and refreshes the served page.
func (s *studioServer) reload() error {
	if err := s.app.loadStyles(); err != nil {
		return err
	}
	filters, err := s.app.compile(compileOptions{write: true})
	if err != nil {
		return err
	}
	s.app.studioFilters = filters
	if err := s.app.writeStudio(); err != nil {
		return err
	}
	return s.renderPage()
}

// checkResult is what a check or save reports.
type checkResult struct {
	OK bool `json:"ok"`
	// Saved is true once the file is written and everything recompiled.
	Saved bool `json:"saved"`
	// Contents is the file as it would be written.
	Contents string `json:"contents"`
	// Original is the file as it is now, for showing what a save changes.
	Original string `json:"original"`
	// Hash identifies the saved contents.
	Hash string `json:"hash,omitempty"`
	// Filters counts the compiled blocks of each script and theme.
	Filters  []filterCount `json:"filters,omitempty"`
	Warnings []string      `json:"warnings,omitempty"`
}

type filterCount struct {
	Script string `json:"script"`
	Style  string `json:"style"`
	Blocks int    `json:"blocks"`
}

func countFilters(filters []studio.Filter) []filterCount {
	counts := make([]filterCount, len(filters))
	for i, filter := range filters {
		counts[i] = filterCount{Script: filter.ScriptFile, Style: filter.StyleID, Blocks: len(filter.Blocks)}
	}
	return counts
}

// finish writes a checked file unless it is a dry run.
func (s *studioServer) finish(path, contents, original string, dryRun bool, result *checkResult) (any, error) {
	result.OK, result.Contents, result.Original = true, contents, original
	if dryRun {
		return result, nil
	}
	if err := writeFileAtomically(path, []byte(contents)); err != nil {
		return nil, err
	}
	s.app.log.Printf("Studio saved %s", path)
	if err := s.reload(); err != nil {
		return nil, fmt.Errorf("saved %s, but recompiling failed: %w", filepath.Base(path), err)
	}
	result.Saved, result.Hash = true, config.ContentHash([]byte(contents))
	return result, nil
}

// --- Catalog ---

func (s *studioServer) catalog(_ *http.Request) (any, error) {
	a := s.app
	input := studio.CatalogInput{BaseTypes: a.baseTypes, SoundDirs: a.config.FilterOutputDirs}
	for name := range a.config.CustomEquipmentPresets {
		input.Presets = append(input.Presets, name)
	}
	for _, style := range a.styles {
		sheet, err := config.LoadStylesheet(style.Stylesheet, style.Palette)
		if err != nil {
			return nil, err
		}
		input.Stylesheets = append(input.Stylesheets, sheet)
		entries, err := a.loadBaseTypeData(style.BaseTypes)
		if err != nil {
			return nil, err
		}
		input.Automation = append(input.Automation, *entries)
	}
	return studio.NewCatalog(input), nil
}

// --- Themes (style files) ---

func (s *studioServer) themePath(id string) (string, error) {
	if !themeID.MatchString(id) || strings.Contains(id, "..") {
		return "", badRequest("%q is not a valid theme file name", id)
	}
	return filepath.Join(s.app.config.StylesDir, id+".toml"), nil
}

func (s *studioServer) listThemes(_ *http.Request) (any, error) {
	paths, err := filepath.Glob(filepath.Join(s.app.config.StylesDir, "*.toml"))
	if err != nil {
		return nil, err
	}
	sort.Strings(paths)
	themes := []*config.ThemeFile{}
	for _, path := range paths {
		theme, err := config.ReadThemeFile(path)
		if err != nil {
			return nil, err
		}
		themes = append(themes, theme)
	}
	return map[string]any{"themes": themes, "stylesheets": s.stylesheetFiles(), "baseTypes": s.baseTypeFiles()}, nil
}

func (s *studioServer) getTheme(r *http.Request) (any, error) {
	path, err := s.themePath(r.PathValue("id"))
	if err != nil {
		return nil, err
	}
	return config.ReadThemeFile(path)
}

type themeRequest struct {
	Theme  config.ThemeFile `json:"theme"`
	Hash   string           `json:"hash"`
	Create bool             `json:"create"`
	// From is the theme a new theme copies its comments and layout from.
	From   string `json:"from"`
	DryRun bool   `json:"dryRun"`
}

func (s *studioServer) putTheme(r *http.Request) (any, error) {
	var request themeRequest
	if err := decodeBody(r, &request); err != nil {
		return nil, err
	}
	id := r.PathValue("id")
	path, err := s.themePath(id)
	if err != nil {
		return nil, err
	}
	current, err := checkUnchanged(path, request.Hash, request.Create)
	if err != nil {
		return nil, err
	}
	theme := request.Theme
	if strings.TrimSpace(theme.Name) == "" {
		return nil, badRequest("a theme needs a name")
	}
	base := string(current)
	if request.Create && request.From != "" {
		fromPath, err := s.themePath(request.From)
		if err != nil {
			return nil, err
		}
		source, err := config.ReadThemeFile(fromPath)
		if err != nil {
			return nil, err
		}
		contents, err := os.ReadFile(fromPath)
		if err != nil {
			return nil, err
		}
		// The copy's first comment line names the theme it came from.
		base = strings.Replace(string(contents), "# "+source.Name+"\n", "# "+theme.Name+"\n", 1)
	}
	contents, err := theme.Render(base)
	if err != nil {
		return nil, badRequest("%v", err)
	}
	result, err := s.checkTheme(id, path, []byte(contents))
	if err != nil {
		return nil, err
	}
	return s.finish(path, contents, string(current), request.DryRun, result)
}

// checkTheme loads a proposed style file the way a compile would and, when it
// is enabled, compiles every script with it.
func (s *studioServer) checkTheme(id, path string, contents []byte) (*checkResult, error) {
	definition, err := config.ParseStyleDefinition(path, contents)
	if err != nil {
		return nil, badRequest("%v", err)
	}
	definition.ID = id
	if _, err := config.LoadStylesheet(definition.Stylesheet, definition.Palette); err != nil {
		return nil, badRequest("the stylesheet does not work with this palette: %v", err)
	}
	if _, err := s.app.loadBaseTypeData(definition.BaseTypes); err != nil {
		return nil, badRequest("%v", err)
	}
	others, err := config.LoadStyleDefinitions(s.app.config.StylesDir)
	if err != nil {
		return nil, err
	}
	result := &checkResult{}
	if !definition.IsEnabled() {
		result.Warnings = append(result.Warnings, "This theme is disabled, so no filter is compiled with it.")
		return result, nil
	}
	for _, other := range others {
		if other.ID != id && other.IsEnabled() && strings.EqualFold(other.Suffix, definition.Suffix) {
			return nil, badRequest("the suffix %q is already used by %s, so their filters would overwrite each other", definition.Suffix, other.ID)
		}
	}
	filters, err := s.app.compile(compileOptions{styles: []config.StyleDefinition{definition}})
	if err != nil {
		return nil, badRequest("%v", err)
	}
	result.Filters = countFilters(filters)
	return result, nil
}

func (s *studioServer) baseTypeFiles() []string {
	var files []string
	for _, style := range s.app.styles {
		files = append(files, style.BaseTypes)
	}
	relative := map[string]bool{}
	for _, file := range files {
		if rel, err := filepath.Rel(s.app.config.StylesDir, file); err == nil {
			relative[filepath.ToSlash(rel)] = true
		}
		matches, _ := filepath.Glob(filepath.Join(filepath.Dir(file), "*.csv"))
		for _, match := range matches {
			if rel, err := filepath.Rel(s.app.config.StylesDir, match); err == nil {
				relative[filepath.ToSlash(rel)] = true
			}
		}
	}
	return sortedKeys(relative)
}

func sortedKeys(set map[string]bool) []string {
	keys := make([]string, 0, len(set))
	for key := range set {
		keys = append(keys, key)
	}
	sort.Strings(keys)
	return keys
}

// --- Stylesheets ---

// stylesheetDirs are the folders of the stylesheets the style files use.
func (s *studioServer) stylesheetDirs() []string {
	dirs := map[string]bool{}
	definitions, _ := config.LoadStyleDefinitions(s.app.config.StylesDir)
	for _, definition := range definitions {
		dirs[filepath.Dir(definition.Stylesheet)] = true
	}
	return sortedKeys(dirs)
}

// stylesheetFiles lists stylesheets as paths relative to the styles folder,
// the way style files refer to them.
func (s *studioServer) stylesheetFiles() []string {
	files := map[string]bool{}
	for _, dir := range s.stylesheetDirs() {
		matches, _ := filepath.Glob(filepath.Join(dir, "*.toml"))
		for _, match := range matches {
			if rel, err := filepath.Rel(s.app.config.StylesDir, match); err == nil {
				files[filepath.ToSlash(rel)] = true
			}
		}
	}
	return sortedKeys(files)
}

// stylesheetPath resolves a stylesheet file name in the stylesheet folder.
func (s *studioServer) stylesheetPath(file string) (string, error) {
	dirs := s.stylesheetDirs()
	if !sheetFile.MatchString(file) || strings.Contains(file, "..") || len(dirs) == 0 {
		return "", badRequest("%q is not a valid stylesheet file name", file)
	}
	for _, dir := range dirs {
		if _, err := os.Stat(filepath.Join(dir, file)); err == nil {
			return filepath.Join(dir, file), nil
		}
	}
	return filepath.Join(dirs[0], file), nil
}

func (s *studioServer) listStylesheets(_ *http.Request) (any, error) {
	type sheet struct {
		File   string   `json:"file"`
		Path   string   `json:"path"`
		Themes []string `json:"themes"`
	}
	definitions, err := config.LoadStyleDefinitions(s.app.config.StylesDir)
	if err != nil {
		return nil, err
	}
	var sheets []sheet
	for _, rel := range s.stylesheetFiles() {
		path := filepath.Clean(filepath.Join(s.app.config.StylesDir, rel))
		entry := sheet{File: filepath.Base(path), Path: rel, Themes: []string{}}
		for _, definition := range definitions {
			if filepath.Clean(definition.Stylesheet) == path {
				entry.Themes = append(entry.Themes, definition.ID)
			}
		}
		sheets = append(sheets, entry)
	}
	return map[string]any{"stylesheets": sheets}, nil
}

func (s *studioServer) getStylesheet(r *http.Request) (any, error) {
	path, err := s.stylesheetPath(r.PathValue("file"))
	if err != nil {
		return nil, err
	}
	return config.ReadStylesheetFile(path)
}

type stylesheetRequest struct {
	Hash     string                  `json:"hash"`
	Changes  []config.StyleChange    `json:"changes"`
	Distinct []config.DistinctChange `json:"distinct"`
	Create   bool                    `json:"create"`
	// From is the stylesheet a new stylesheet starts as a copy of.
	From   string `json:"from"`
	DryRun bool   `json:"dryRun"`
}

func (s *studioServer) putStylesheet(r *http.Request) (any, error) {
	var request stylesheetRequest
	if err := decodeBody(r, &request); err != nil {
		return nil, err
	}
	path, err := s.stylesheetPath(r.PathValue("file"))
	if err != nil {
		return nil, err
	}
	current, err := checkUnchanged(path, request.Hash, request.Create)
	if err != nil {
		return nil, err
	}
	base := string(current)
	if request.Create {
		base = "# " + strings.TrimSuffix(filepath.Base(path), ".toml") + ": a Ruleforge stylesheet.\n"
		if request.From != "" {
			fromPath, err := s.stylesheetPath(request.From)
			if err != nil {
				return nil, err
			}
			contents, err := os.ReadFile(fromPath)
			if err != nil {
				return nil, err
			}
			base = string(contents)
		}
	}
	contents, err := config.ApplyStylesheetChanges(base, request.Changes, request.Distinct)
	if err != nil {
		return nil, badRequest("%v", err)
	}
	result, err := s.checkStylesheet(path, contents)
	if err != nil {
		return nil, err
	}
	return s.finish(path, contents, string(current), request.DryRun, result)
}

// checkStylesheet loads a proposed stylesheet with every palette that uses it
// and compiles every script with the enabled themes that use it.
func (s *studioServer) checkStylesheet(path, contents string) (*checkResult, error) {
	definitions, err := config.LoadStyleDefinitions(s.app.config.StylesDir)
	if err != nil {
		return nil, err
	}
	result := &checkResult{}
	temp, err := os.CreateTemp("", "ruleforge-stylesheet-*.toml")
	if err != nil {
		return nil, err
	}
	defer os.Remove(temp.Name())
	if _, err := temp.WriteString(contents); err != nil {
		temp.Close()
		return nil, err
	}
	temp.Close()
	var styles []config.StyleDefinition
	for _, definition := range definitions {
		if filepath.Clean(definition.Stylesheet) != filepath.Clean(path) {
			continue
		}
		if _, err := config.ParseStylesheet(path, []byte(contents), definition.Palette); err != nil {
			if definition.IsEnabled() {
				return nil, badRequest("theme %s: %v", definition.ID, err)
			}
			result.Warnings = append(result.Warnings, fmt.Sprintf("disabled theme %s would not load: %v", definition.ID, err))
			continue
		}
		if definition.IsEnabled() {
			definition.Stylesheet = temp.Name()
			styles = append(styles, definition)
		}
	}
	if len(styles) == 0 {
		result.Warnings = append(result.Warnings, "No enabled theme uses this stylesheet, so no filter was compiled with it.")
		return result, nil
	}
	filters, err := s.app.compile(compileOptions{styles: styles})
	if err != nil {
		return nil, badRequest("%v", err)
	}
	result.Filters = countFilters(filters)
	return result, nil
}

// --- Scripts ---

func (s *studioServer) scriptPath(file string) (string, error) {
	if !scriptFile.MatchString(file) || strings.Contains(file, "..") {
		return "", badRequest("%q is not a valid script file name (it must end in .rf)", file)
	}
	return filepath.Join(s.app.config.RuleforgeInputDir, file), nil
}

func (s *studioServer) listScripts(_ *http.Request) (any, error) {
	paths, err := listFilesWithExtension(s.app.config.RuleforgeInputDir, ".rf")
	if err != nil {
		return nil, err
	}
	files := make([]string, 0, len(paths))
	for _, path := range paths {
		files = append(files, filepath.Base(path))
	}
	sort.Strings(files)
	return map[string]any{"scripts": files}, nil
}

func (s *studioServer) getScript(r *http.Request) (any, error) {
	path, err := s.scriptPath(r.PathValue("file"))
	if err != nil {
		return nil, err
	}
	contents, err := os.ReadFile(path)
	if err != nil {
		return nil, err
	}
	doc, err := document.Parse(string(contents))
	if err != nil {
		return nil, badRequest("%s cannot be opened in the editor: %v", filepath.Base(path), err)
	}
	return map[string]any{"file": filepath.Base(path), "document": doc, "hash": config.ContentHash(contents)}, nil
}

type scriptRequest struct {
	Document *document.Document `json:"document"`
	Hash     string             `json:"hash"`
	Create   bool               `json:"create"`
	DryRun   bool               `json:"dryRun"`
}

func (s *studioServer) putScript(r *http.Request) (any, error) {
	var request scriptRequest
	if err := decodeBody(r, &request); err != nil {
		return nil, err
	}
	if request.Document == nil {
		return nil, badRequest("no script was sent")
	}
	path, err := s.scriptPath(r.PathValue("file"))
	if err != nil {
		return nil, err
	}
	current, err := checkUnchanged(path, request.Hash, request.Create)
	if err != nil {
		return nil, err
	}
	contents, err := scriptText(request.Document)
	if err != nil {
		return nil, badRequest("%v", err)
	}
	filters, err := s.app.compile(compileOptions{scripts: map[string]string{path: contents}, only: map[string]bool{path: true}})
	if err != nil {
		return nil, badRequest("%v", err)
	}
	return s.finish(path, contents, string(current), request.DryRun, &checkResult{Filters: countFilters(filters)})
}

// scriptText writes an edited document as script text and makes sure the
// text reads back as the same document.
func scriptText(doc *document.Document) (string, error) {
	document.Reconcile(doc)
	if err := document.Validate(doc); err != nil {
		return "", err
	}
	text := document.Print(doc)
	again, err := document.Parse(text)
	if err != nil {
		return "", fmt.Errorf("the script would not read back: %w", err)
	}
	if err := document.Equivalent(doc, again); err != nil {
		return "", fmt.Errorf("the script would read back differently: %w", err)
	}
	return text, nil
}
