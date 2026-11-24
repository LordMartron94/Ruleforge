// Package server serves the web editor and inspector and the JSON API behind them.
//
// Every question about a script (does it parse, what does it compile to, why did this rule appear, what would this item do)
// is answered by the C engine; the handlers here move text and JSON between the browser and the engine and keep the file
// operations inside the project folder.
package server

import (
	"context"
	"embed"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"io/fs"
	"net/http"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"sync"
	"time"

	"github.com/LordMartron94/Ruleforge/studio/internal/config"
	"github.com/LordMartron94/Ruleforge/studio/internal/engine"
	"github.com/LordMartron94/Ruleforge/studio/internal/provider"
	"github.com/LordMartron94/Ruleforge/studio/internal/update"
)

//go:embed web
var webFiles embed.FS

// Server holds one project and its engine.
type Server struct {
	Project *config.Project
	Engine  engine.Engine
	Version string
	Updater *update.Updater

	mutex       sync.Mutex
	lastResult  *engine.Result
	regenStatus func() any
	regenRun    func(ctx context.Context) error
}

// New builds a server for a project.
func New(project *config.Project, eng engine.Engine, version string, updater *update.Updater) *Server {
	return &Server{Project: project, Engine: eng, Version: version, Updater: updater}
}

// RegenerationHooks lets the scheduler expose its status and a manual trigger.
func (s *Server) RegenerationHooks(status func() any, run func(ctx context.Context) error) {
	s.regenStatus = status
	s.regenRun = run
}

// Handler returns the HTTP handler: the API under /api and the web app at the root.
func (s *Server) Handler() http.Handler {
	mux := http.NewServeMux()
	mux.HandleFunc("GET /api/info", s.handleInfo)
	mux.HandleFunc("GET /api/files", s.handleFiles)
	mux.HandleFunc("GET /api/file", s.handleFileRead)
	mux.HandleFunc("PUT /api/file", s.handleFileWrite)
	mux.HandleFunc("POST /api/source/replace", s.handleSourceReplace)
	mux.HandleFunc("POST /api/file/create", s.handleFileCreate)
	mux.HandleFunc("POST /api/check", s.handleCheck)
	mux.HandleFunc("POST /api/compile", s.handleCompile)
	mux.HandleFunc("POST /api/simulate", s.handleSimulate)
	mux.HandleFunc("POST /api/item", s.handleItem)
	mux.HandleFunc("POST /api/loot", s.handleLoot)
	mux.HandleFunc("GET /api/ast", s.handleAST)
	mux.HandleFunc("GET /api/themes", s.handleThemes)
	mux.HandleFunc("POST /api/color", s.handleColor)
	mux.HandleFunc("POST /api/tokens", s.handleTokens)
	mux.HandleFunc("GET /api/manifest", s.handleManifest)
	mux.HandleFunc("GET /api/update/check", s.handleUpdateCheck)
	mux.HandleFunc("POST /api/update/apply", s.handleUpdateApply)
	mux.HandleFunc("GET /api/regen/status", s.handleRegenStatus)
	mux.HandleFunc("POST /api/regen/run", s.handleRegenRun)
	web, _ := fs.Sub(webFiles, "web")
	mux.Handle("/", http.FileServer(http.FS(web)))
	return localOnly(mux)
}

// localOnly rejects requests whose Host is not a loopback name, so a web page on another site cannot drive the editor through
// DNS rebinding, and requires same-origin for anything that writes.
func localOnly(next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		host := r.Host
		if index := strings.LastIndex(host, ":"); index >= 0 {
			host = host[:index]
		}
		if host != "localhost" && host != "127.0.0.1" && host != "[::1]" && host != "::1" {
			http.Error(w, "forbidden host", http.StatusForbidden)
			return
		}
		if r.Method != http.MethodGet && r.Method != http.MethodHead {
			if origin := r.Header.Get("Origin"); origin != "" && !strings.HasSuffix(origin, "://"+r.Host) {
				http.Error(w, "forbidden origin", http.StatusForbidden)
				return
			}
		}
		w.Header().Set("Cache-Control", "no-store")
		next.ServeHTTP(w, r)
	})
}

func writeJSON(w http.ResponseWriter, status int, value any) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(status)
	_ = json.NewEncoder(w).Encode(value)
}

func writeError(w http.ResponseWriter, status int, err error) {
	writeJSON(w, status, map[string]string{"error": err.Error()})
}

// resolve maps a project-relative path to a real path inside the project folder.
func (s *Server) resolve(relative string) (string, error) {
	if relative == "" {
		return "", errors.New("no path")
	}
	clean := filepath.Clean(filepath.FromSlash(relative))
	// A rooted path (a leading slash, a drive or a UNC share) is not project-relative, whichever platform decides it is absolute.
	if filepath.IsAbs(clean) || filepath.VolumeName(clean) != "" || strings.HasPrefix(relative, "/") || strings.HasPrefix(relative, `\`) || clean == ".." || strings.HasPrefix(clean, ".."+string(filepath.Separator)) {
		return "", errors.New("the path leaves the project folder")
	}
	full := filepath.Join(s.Project.Dir, clean)
	root, err := filepath.EvalSymlinks(s.Project.Dir)
	if err != nil {
		return "", err
	}
	// The nearest existing ancestor must resolve inside the project, so a symlink cannot lead out of it.
	probe := full
	for {
		if resolved, err := filepath.EvalSymlinks(probe); err == nil {
			if resolved != root && !strings.HasPrefix(resolved, root+string(filepath.Separator)) {
				return "", errors.New("the path leaves the project folder")
			}
			break
		}
		parent := filepath.Dir(probe)
		if parent == probe {
			break
		}
		probe = parent
	}
	return full, nil
}

func (s *Server) request() engine.Request {
	set := map[string]string{}
	for key, value := range s.Project.Context {
		set[key] = value
	}
	if s.Project.Character != nil && s.Project.Character.Level > 0 {
		set["character.level"] = fmt.Sprint(s.Project.Character.Level)
		if s.Project.Character.Name != "" {
			set["character.name"] = s.Project.Character.Name
		}
	}
	datasets := map[string]string{}
	for _, dataset := range s.Project.Datasets {
		if dataset.Path != "" {
			datasets[dataset.Source] = dataset.Path
		}
	}
	for source, path := range provider.GamePaths(s.Project) {
		datasets[source] = path
	}
	if s.Project.EconomyHistory != nil {
		datasets["economy.history"] = provider.HistoryPath(s.Project)
	}
	return engine.Request{Dir: s.Project.Dir, Entry: s.Project.Entry, Include: s.Project.Include, Set: set, Datasets: datasets}
}

func (s *Server) handleInfo(w http.ResponseWriter, r *http.Request) {
	writeJSON(w, http.StatusOK, map[string]any{
		"version": s.Version,
		"engine":  s.Engine.Version(r.Context()),
		"entry":   s.Project.Entry,
		"project": filepath.Base(s.Project.Dir),
		"outputs": s.Project.Outputs,
	})
}

type fileEntry struct {
	Path string `json:"path"`
	Kind string `json:"kind"`
	Size int64  `json:"size"`
}

func (s *Server) handleFiles(w http.ResponseWriter, r *http.Request) {
	files := []fileEntry{}
	_ = filepath.WalkDir(s.Project.Dir, func(path string, entry fs.DirEntry, err error) error {
		if err != nil {
			return nil
		}
		name := entry.Name()
		if entry.IsDir() {
			if path != s.Project.Dir && (strings.HasPrefix(name, ".") || name == "node_modules") {
				return filepath.SkipDir
			}
			return nil
		}
		kind := ""
		switch strings.ToLower(filepath.Ext(name)) {
		case ".rf":
			kind = "script"
		case ".csv", ".json":
			kind = "data"
		case ".toml":
			kind = "settings"
		case ".filter":
			kind = "filter"
		default:
			return nil
		}
		info, infoErr := entry.Info()
		if infoErr != nil {
			return nil
		}
		relative, relErr := filepath.Rel(s.Project.Dir, path)
		if relErr != nil {
			return nil
		}
		files = append(files, fileEntry{Path: filepath.ToSlash(relative), Kind: kind, Size: info.Size()})
		return nil
	})
	sort.Slice(files, func(i, j int) bool { return files[i].Path < files[j].Path })
	writeJSON(w, http.StatusOK, files)
}

func (s *Server) handleFileRead(w http.ResponseWriter, r *http.Request) {
	full, err := s.resolve(r.URL.Query().Get("path"))
	if err != nil {
		writeError(w, http.StatusBadRequest, err)
		return
	}
	info, err := os.Stat(full)
	if err != nil || info.IsDir() || info.Size() > 16<<20 {
		writeError(w, http.StatusNotFound, errors.New("no such file"))
		return
	}
	data, err := os.ReadFile(full)
	if err != nil {
		writeError(w, http.StatusInternalServerError, err)
		return
	}
	w.Header().Set("Content-Type", "text/plain; charset=utf-8")
	_, _ = w.Write(data)
}

func (s *Server) handleFileWrite(w http.ResponseWriter, r *http.Request) {
	full, err := s.resolve(r.URL.Query().Get("path"))
	if err != nil {
		writeError(w, http.StatusBadRequest, err)
		return
	}
	body, err := io.ReadAll(io.LimitReader(r.Body, 16<<20))
	if err != nil {
		writeError(w, http.StatusBadRequest, err)
		return
	}
	s.mutex.Lock()
	defer s.mutex.Unlock()
	if err := atomicWrite(full, body); err != nil {
		writeError(w, http.StatusInternalServerError, err)
		return
	}
	writeJSON(w, http.StatusOK, map[string]bool{"ok": true})
}

func (s *Server) handleFileCreate(w http.ResponseWriter, r *http.Request) {
	var body struct {
		Path    string `json:"path"`
		Content string `json:"content"`
	}
	if err := json.NewDecoder(io.LimitReader(r.Body, 1<<20)).Decode(&body); err != nil {
		writeError(w, http.StatusBadRequest, err)
		return
	}
	full, err := s.resolve(body.Path)
	if err != nil {
		writeError(w, http.StatusBadRequest, err)
		return
	}
	if _, err := os.Stat(full); err == nil {
		writeError(w, http.StatusConflict, errors.New("the file already exists"))
		return
	}
	if err := atomicWrite(full, []byte(body.Content)); err != nil {
		writeError(w, http.StatusInternalServerError, err)
		return
	}
	writeJSON(w, http.StatusOK, map[string]bool{"ok": true})
}

func atomicWrite(path string, data []byte) error {
	if err := os.MkdirAll(filepath.Dir(path), 0o755); err != nil {
		return err
	}
	temp, err := os.CreateTemp(filepath.Dir(path), ".write-*")
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

func (s *Server) handleCheck(w http.ResponseWriter, r *http.Request) {
	ctx, cancel := context.WithTimeout(r.Context(), 60*time.Second)
	defer cancel()
	diagnostics, err := s.Engine.Check(ctx, s.request())
	if err != nil {
		writeError(w, http.StatusInternalServerError, err)
		return
	}
	writeJSON(w, http.StatusOK, map[string]any{"ok": !hasError(diagnostics), "diagnostics": diagnostics})
}

func hasError(diagnostics []engine.Diagnostic) bool {
	for _, d := range diagnostics {
		if d.Severity == "error" {
			return true
		}
	}
	return false
}

func (s *Server) handleCompile(w http.ResponseWriter, r *http.Request) {
	ctx, cancel := context.WithTimeout(r.Context(), 120*time.Second)
	defer cancel()
	var body struct {
		Set  map[string]string `json:"set"`
		Name string            `json:"name"`
	}
	_ = json.NewDecoder(io.LimitReader(r.Body, 1<<20)).Decode(&body)
	req := s.request()
	for key, value := range body.Set {
		req.Set[key] = value
	}
	req.Name = body.Name
	result, err := s.Engine.Compile(ctx, req)
	if err != nil {
		writeError(w, http.StatusInternalServerError, err)
		return
	}
	s.mutex.Lock()
	s.lastResult = &result
	s.mutex.Unlock()
	writeJSON(w, http.StatusOK, result)
}

func (s *Server) handleSimulate(w http.ResponseWriter, r *http.Request) {
	ctx, cancel := context.WithTimeout(r.Context(), 120*time.Second)
	defer cancel()
	var body struct {
		Items json.RawMessage   `json:"items"`
		Set   map[string]string `json:"set"`
	}
	if err := json.NewDecoder(io.LimitReader(r.Body, 8<<20)).Decode(&body); err != nil || len(body.Items) == 0 {
		writeError(w, http.StatusBadRequest, errors.New("send {\"items\": [...]}"))
		return
	}
	req := s.request()
	for key, value := range body.Set {
		req.Set[key] = value
	}
	results, diagnostics, err := s.Engine.Simulate(ctx, req, body.Items)
	if err != nil {
		writeError(w, http.StatusInternalServerError, err)
		return
	}
	writeJSON(w, http.StatusOK, map[string]any{"ok": results != nil, "results": results, "diagnostics": diagnostics})
}

func (s *Server) handleAST(w http.ResponseWriter, r *http.Request) {
	ctx, cancel := context.WithTimeout(r.Context(), 60*time.Second)
	defer cancel()
	ast, diagnostics, err := s.Engine.AST(ctx, s.request())
	if err != nil {
		writeError(w, http.StatusInternalServerError, err)
		return
	}
	writeJSON(w, http.StatusOK, map[string]any{"ok": ast != nil, "ast": ast, "diagnostics": diagnostics})
}

func (s *Server) handleThemes(w http.ResponseWriter, r *http.Request) {
	ctx, cancel := context.WithTimeout(r.Context(), 60*time.Second)
	defer cancel()
	before, err := s.sourceHashes()
	if err != nil {
		writeError(w, 500, err)
		return
	}
	themes, diagnostics, err := s.Engine.Themes(ctx, s.request())
	if err != nil {
		writeError(w, http.StatusInternalServerError, err)
		return
	}
	after, err := s.sourceHashes()
	if err != nil {
		writeError(w, 500, err)
		return
	}
	for path, hash := range before {
		if after[path] != hash {
			writeError(w, 409, fmt.Errorf("sources changed while the form was loading; reload"))
			return
		}
	}
	if len(before) != len(after) {
		writeError(w, 409, fmt.Errorf("sources changed while the form was loading; reload"))
		return
	}
	writeJSON(w, http.StatusOK, map[string]any{"ok": themes != nil, "themes": themes, "diagnostics": diagnostics, "sourceHashes": after})
}

func (s *Server) handleUpdateCheck(w http.ResponseWriter, r *http.Request) {
	if s.Updater == nil {
		writeJSON(w, http.StatusOK, map[string]any{"available": false})
		return
	}
	release, available, err := s.Updater.Check(r.Context())
	if err != nil {
		writeJSON(w, http.StatusOK, map[string]any{"available": false, "error": err.Error()})
		return
	}
	writeJSON(w, http.StatusOK, map[string]any{"available": available, "release": release, "current": s.Version})
}

func (s *Server) handleUpdateApply(w http.ResponseWriter, r *http.Request) {
	if s.Updater == nil {
		writeError(w, http.StatusBadRequest, errors.New("updates are not configured"))
		return
	}
	release, available, err := s.Updater.Check(r.Context())
	if err != nil || !available {
		writeError(w, http.StatusBadRequest, errors.New("no update is available"))
		return
	}
	if err := s.Updater.Apply(r.Context(), release); err != nil {
		writeError(w, http.StatusInternalServerError, err)
		return
	}
	writeJSON(w, http.StatusOK, map[string]any{"ok": true, "installed": release.Version, "restart": true})
}

func (s *Server) handleRegenStatus(w http.ResponseWriter, r *http.Request) {
	if s.regenStatus == nil {
		writeJSON(w, http.StatusOK, map[string]any{"enabled": false})
		return
	}
	writeJSON(w, http.StatusOK, s.regenStatus())
}

func (s *Server) handleRegenRun(w http.ResponseWriter, r *http.Request) {
	if s.regenRun == nil {
		writeError(w, http.StatusBadRequest, errors.New("regeneration is not configured"))
		return
	}
	if err := s.regenRun(r.Context()); err != nil {
		writeError(w, http.StatusInternalServerError, err)
		return
	}
	writeJSON(w, http.StatusOK, map[string]bool{"ok": true})
}

func (s *Server) handleTokens(w http.ResponseWriter, r *http.Request) {
	ctx, cancel := context.WithTimeout(r.Context(), 20*time.Second)
	defer cancel()
	body, err := io.ReadAll(io.LimitReader(r.Body, 4<<20))
	if err != nil {
		writeError(w, http.StatusBadRequest, err)
		return
	}
	tokens, err := s.Engine.Tokens(ctx, body)
	if err != nil {
		writeError(w, http.StatusInternalServerError, err)
		return
	}
	w.Header().Set("Content-Type", "application/json")
	_, _ = w.Write(tokens)
}

func (s *Server) handleManifest(w http.ResponseWriter, r *http.Request) {
	manifest, err := s.Engine.Manifest(r.Context())
	if err != nil {
		writeError(w, http.StatusInternalServerError, err)
		return
	}
	w.Header().Set("Content-Type", "application/json")
	_, _ = w.Write(manifest)
}
