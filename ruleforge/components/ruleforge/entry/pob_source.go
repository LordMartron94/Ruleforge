package main

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"log"
	"net/http"
	"net/url"
	"os"
	"path/filepath"
	"regexp"
	"sort"
	"strings"
	"time"
)

// Only the Lua data files consumed by Ruleforge are downloaded; a PoB installation
// and Git are not required. A complete snapshot is installed before it is used.
const (
	pobRepository      = "PathOfBuildingCommunity/PathOfBuilding"
	pobBranch          = "master" // PoB's published/release branch, rather than development HEAD.
	pobRefreshInterval = 7 * 24 * time.Hour
	pobMaxFileSize     = 16 << 20
)

type pobSnapshot struct {
	Commit    string    `json:"commit"`
	CheckedAt time.Time `json:"checked_at"`
	Files     []string  `json:"files"`
}

type pobSource struct {
	client  *http.Client
	apiBase string
	rawBase string
	logger  *log.Logger
}

func defaultPoBSource(logger *log.Logger) pobSource {
	return pobSource{
		client:  &http.Client{Timeout: 30 * time.Second},
		apiBase: "https://api.github.com/repos/" + pobRepository,
		rawBase: "https://raw.githubusercontent.com/" + pobRepository,
		logger:  logger,
	}
}

func (s pobSource) getJSON(ctx context.Context, address string, output any) error {
	request, err := http.NewRequestWithContext(ctx, http.MethodGet, address, nil)
	if err != nil {
		return err
	}
	request.Header.Set("Accept", "application/vnd.github+json")
	request.Header.Set("User-Agent", "Ruleforge-PoB-data-cache")
	response, err := s.client.Do(request)
	if err != nil {
		return err
	}
	defer response.Body.Close()
	if response.StatusCode != http.StatusOK {
		return fmt.Errorf("GitHub returned HTTP %d for %s", response.StatusCode, address)
	}
	return json.NewDecoder(io.LimitReader(response.Body, 8<<20)).Decode(output)
}

func (s pobSource) download(ctx context.Context, address, destination string) error {
	request, err := http.NewRequestWithContext(ctx, http.MethodGet, address, nil)
	if err != nil {
		return err
	}
	request.Header.Set("User-Agent", "Ruleforge-PoB-data-cache")
	response, err := s.client.Do(request)
	if err != nil {
		return err
	}
	defer response.Body.Close()
	if response.StatusCode != http.StatusOK {
		return fmt.Errorf("source download returned HTTP %d for %s", response.StatusCode, address)
	}
	if err := os.MkdirAll(filepath.Dir(destination), 0755); err != nil {
		return err
	}
	out, err := os.OpenFile(destination, os.O_WRONLY|os.O_CREATE|os.O_EXCL, 0644)
	if err != nil {
		return err
	}
	count, copyErr := io.Copy(out, io.LimitReader(response.Body, pobMaxFileSize+1))
	closeErr := out.Close()
	if copyErr != nil {
		return copyErr
	}
	if closeErr != nil {
		return closeErr
	}
	if count == 0 || count > pobMaxFileSize {
		return fmt.Errorf("source file has invalid size: %s", address)
	}
	return nil
}

func readPoBSnapshot(dir string) (pobSnapshot, error) {
	var snapshot pobSnapshot
	data, err := os.ReadFile(filepath.Join(dir, ".ruleforge-source.json"))
	if err != nil {
		return snapshot, err
	}
	err = json.Unmarshal(data, &snapshot)
	return snapshot, err
}

var pobCommitPattern = regexp.MustCompile(`^[a-fA-F0-9]{40}$`)

func completePoBSnapshot(dir string, snapshot pobSnapshot) bool {
	if !pobCommitPattern.MatchString(snapshot.Commit) || len(snapshot.Files) < 4 {
		return false
	}
	required := map[string]bool{"Essence.lua": false, "Gems.lua": false, "Uniques/Special/Generated.lua": false}
	bases, uniques := 0, 0
	for _, name := range snapshot.Files {
		if name == "Essence.lua" || name == "Gems.lua" || name == "Uniques/Special/Generated.lua" {
			required[name] = true
		}
		if strings.HasPrefix(name, "Bases/") {
			bases++
		}
		if strings.HasPrefix(name, "Uniques/") && !strings.Contains(strings.TrimPrefix(name, "Uniques/"), "/") {
			uniques++
		}
		if name == "" || filepath.IsAbs(name) || strings.Contains(name, "\\") || strings.Contains(name, "..") || filepath.Ext(name) != ".lua" {
			return false
		}
		info, err := os.Stat(filepath.Join(dir, filepath.FromSlash(name)))
		if err != nil || info.IsDir() || info.Size() == 0 {
			return false
		}
	}
	for _, ok := range required {
		if !ok {
			return false
		}
	}
	return bases > 0 && uniques > 0
}

func (s pobSource) listLuaFiles(ctx context.Context, commit, directory string) ([]string, error) {
	var entries []struct {
		Name string `json:"name"`
		Type string `json:"type"`
	}
	address := s.apiBase + "/contents/src/Data/" + directory + "?ref=" + url.QueryEscape(commit)
	if err := s.getJSON(ctx, address, &entries); err != nil {
		return nil, err
	}
	var files []string
	for _, entry := range entries {
		if entry.Type != "file" || filepath.Ext(entry.Name) != ".lua" {
			continue
		}
		if entry.Name == "." || entry.Name == ".." || strings.ContainsAny(entry.Name, "/\\") {
			return nil, fmt.Errorf("unsafe PoB file name %q", entry.Name)
		}
		files = append(files, directory+"/"+entry.Name)
	}
	if len(files) == 0 {
		return nil, fmt.Errorf("no Lua files in PoB %s", directory)
	}
	sort.Strings(files)
	return files, nil
}

func (s pobSource) currentCommit(ctx context.Context) (string, error) {
	var commit struct {
		SHA string `json:"sha"`
	}
	if err := s.getJSON(ctx, s.apiBase+"/commits/"+pobBranch, &commit); err != nil {
		return "", err
	}
	if !pobCommitPattern.MatchString(commit.SHA) {
		return "", fmt.Errorf("invalid PoB commit SHA %q", commit.SHA)
	}
	return commit.SHA, nil
}

func (s pobSource) fetchSnapshot(ctx context.Context, dir, commit string) (pobSnapshot, error) {
	snapshot := pobSnapshot{Commit: commit, CheckedAt: time.Now()}
	snapshot.Files = append(snapshot.Files, "Essence.lua", "Gems.lua", "Uniques/Special/Generated.lua")
	for _, directory := range []string{"Bases", "Uniques"} {
		files, err := s.listLuaFiles(ctx, commit, directory)
		if err != nil {
			return pobSnapshot{}, err
		}
		snapshot.Files = append(snapshot.Files, files...)
	}
	// New.lua is optional in PoB; download it only if it is present in the repository.
	var special []struct {
		Name string `json:"name"`
		Type string `json:"type"`
	}
	if err := s.getJSON(ctx, s.apiBase+"/contents/src/Data/Uniques/Special?ref="+url.QueryEscape(commit), &special); err != nil {
		return pobSnapshot{}, err
	}
	for _, entry := range special {
		if entry.Type == "file" && entry.Name == "New.lua" {
			snapshot.Files = append(snapshot.Files, "Uniques/Special/New.lua")
		}
	}
	sort.Strings(snapshot.Files)
	for _, name := range snapshot.Files {
		address := s.rawBase + "/" + commit + "/src/Data/" + name
		if err := s.download(ctx, address, filepath.Join(dir, filepath.FromSlash(name))); err != nil {
			return pobSnapshot{}, fmt.Errorf("fetch %s: %w", name, err)
		}
	}
	if !completePoBSnapshot(dir, snapshot) {
		return pobSnapshot{}, errors.New("downloaded PoB snapshot is incomplete")
	}
	data, err := json.MarshalIndent(snapshot, "", "  ")
	if err != nil {
		return pobSnapshot{}, err
	}
	if err := os.WriteFile(filepath.Join(dir, ".ruleforge-source.json"), append(data, '\n'), 0644); err != nil {
		return pobSnapshot{}, err
	}
	return snapshot, nil
}

// ensure uses a verified snapshot for offline operation. It never overwrites a
// user-managed directory or exposes a partially downloaded snapshot.
func (s pobSource) ensure(ctx context.Context, dir string, force bool) (bool, error) {
	existing, err := readPoBSnapshot(dir)
	valid := err == nil && completePoBSnapshot(dir, existing)
	if valid && !force && time.Since(existing.CheckedAt) >= 0 && time.Since(existing.CheckedAt) < pobRefreshInterval {
		return false, nil
	}
	if !valid {
		if entries, err := os.ReadDir(dir); err == nil && len(entries) > 0 {
			return false, fmt.Errorf("%s contains files but is not a complete Ruleforge-managed PoB snapshot; choose a different PathOfBuildingDataPath", dir)
		} else if err != nil && !os.IsNotExist(err) {
			return false, err
		}
	}
	// Checking the commit is cheap. Do not redownload the full snapshot every
	// time the refresh interval expires if upstream data has not changed.
	commit, err := s.currentCommit(ctx)
	if err != nil {
		if valid && !force {
			if s.logger != nil {
				s.logger.Printf("Warning: PoB update check failed (%v); using cached commit %s", err, existing.Commit)
			}
			return false, nil
		}
		return false, fmt.Errorf("could not check official PoB revision: %w", err)
	}
	if valid && commit == existing.Commit {
		existing.CheckedAt = time.Now()
		data, err := json.MarshalIndent(existing, "", "  ")
		if err != nil {
			return false, err
		}
		if err := os.WriteFile(filepath.Join(dir, ".ruleforge-source.json"), append(data, '\n'), 0644); err != nil {
			return false, err
		}
		return false, nil
	}
	parent := filepath.Dir(dir)
	if err := os.MkdirAll(parent, 0755); err != nil {
		return false, err
	}
	staging, err := os.MkdirTemp(parent, ".ruleforge-pob-download-")
	if err != nil {
		return false, err
	}
	defer os.RemoveAll(staging)
	latest, err := s.fetchSnapshot(ctx, staging, commit)
	if err != nil {
		if valid && !force {
			if s.logger != nil {
				s.logger.Printf("Warning: PoB refresh failed (%v); continuing with cached commit %s", err, existing.Commit)
			}
			return false, nil
		}
		return false, fmt.Errorf("could not refresh PoB data: %w", err)
	}
	backup := dir + ".ruleforge-previous"
	if _, err := os.Stat(backup); err == nil {
		return false, fmt.Errorf("refusing to overwrite existing backup directory %s", backup)
	} else if !os.IsNotExist(err) {
		return false, err
	}
	hadDir := false
	if _, err := os.Stat(dir); err == nil {
		if err := os.Rename(dir, backup); err != nil {
			return false, err
		}
		hadDir = true
	} else if !os.IsNotExist(err) {
		return false, err
	}
	if err := os.Rename(staging, dir); err != nil {
		if hadDir {
			_ = os.Rename(backup, dir)
		}
		return false, err
	}
	if hadDir {
		if err := os.RemoveAll(backup); err != nil && s.logger != nil {
			s.logger.Printf("Warning: remove previous PoB snapshot: %v", err)
		}
	}
	if s.logger != nil {
		s.logger.Printf("PoB data updated to commit %s (%d Lua files)", latest.Commit, len(latest.Files))
	}
	return true, nil
}
