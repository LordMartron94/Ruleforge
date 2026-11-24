package server

import (
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"io"
	"io/fs"
	"net/http"
	"os"
	"path/filepath"
	"strings"
	"unicode/utf8"
)

// Source replacements use engine byte spans and a full-file hash, so stale form controls cannot overwrite concurrent edits.
func (s *Server) handleSourceReplace(w http.ResponseWriter, r *http.Request) {
	var body struct {
		Path  string `json:"path"`
		Start int    `json:"start"`
		End   int    `json:"end"`
		Hash  string `json:"hash"`
		Text  string `json:"text"`
	}
	if err := json.NewDecoder(io.LimitReader(r.Body, 1<<20)).Decode(&body); err != nil {
		writeError(w, 400, err)
		return
	}
	path, err := s.resolve(body.Path)
	if err != nil {
		writeError(w, 400, err)
		return
	}
	s.mutex.Lock()
	defer s.mutex.Unlock()
	data, err := os.ReadFile(path)
	if err != nil {
		writeError(w, 404, err)
		return
	}
	sum := sha256.Sum256(data)
	if hex.EncodeToString(sum[:]) != body.Hash {
		writeError(w, 409, fmt.Errorf("file changed; reload the form"))
		return
	}
	if body.Start < 0 || body.End < body.Start || body.End > len(data) || !utf8.Valid(data[:body.Start]) || !utf8.Valid(data[:body.End]) || !utf8.ValidString(body.Text) {
		writeError(w, 400, fmt.Errorf("invalid source span"))
		return
	}
	result := append([]byte(nil), data[:body.Start]...)
	result = append(result, body.Text...)
	result = append(result, data[body.End:]...)
	if len(result) > 16<<20 {
		writeError(w, 400, fmt.Errorf("file too large"))
		return
	}
	if err := atomicWrite(path, result); err != nil {
		writeError(w, 500, err)
		return
	}
	writeJSON(w, 200, map[string]any{"ok": true, "text": string(result)})
}

// Snapshot hashes bracket the engine query so its byte spans refer to the exact files forms edit.
func (s *Server) sourceHashes() (map[string]string, error) {
	hashes := map[string]string{}
	err := filepath.WalkDir(s.Project.Dir, func(path string, entry fs.DirEntry, err error) error {
		if err != nil {
			return err
		}
		if entry.IsDir() {
			if path != s.Project.Dir && (strings.HasPrefix(entry.Name(), ".") || entry.Name() == "node_modules") {
				return filepath.SkipDir
			}
			return nil
		}
		if !strings.HasSuffix(path, ".rf") {
			return nil
		}
		data, err := os.ReadFile(path)
		if err != nil {
			return err
		}
		sum := sha256.Sum256(data)
		relative, err := filepath.Rel(s.Project.Dir, path)
		if err != nil {
			return err
		}
		hashes[filepath.ToSlash(relative)] = hex.EncodeToString(sum[:])
		return nil
	})
	return hashes, err
}
