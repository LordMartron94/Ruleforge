// Package config reads the studio's project file, studio.json, which describes one Ruleforge project: its entry module, its
// output filters, where its datasets come from and when they refresh.
package config

import (
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"time"
)

// Dataset declares where one dataset source's snapshot comes from.
type Dataset struct {
	Source   string `json:"source"`            // the logical name a script's `dataset X: S from "source"` uses
	Provider string `json:"provider"`          // "poe-ninja-stash", "poe-ninja-exchange", "csv" or "json"
	Path     string `json:"path"`              // the snapshot file the engine reads; providers write it
	League   string `json:"league,omitempty"`  // poe.ninja: the league name or id; "current" is the challenge league
	Type     string `json:"type,omitempty"`    // poe.ninja: the item type, e.g. Currency
	Refresh  string `json:"refresh,omitempty"` // how often a provider refreshes, e.g. "1h"
	Root     string `json:"root,omitempty"`    // json: the member holding the array
	FieldMap string `json:"-"`
}

// Output is one filter the project produces.
type Output struct {
	Name string            `json:"name"`          // the filter's name, readable as @filter
	Path string            `json:"path"`          // where the .filter file is published
	Set  map[string]string `json:"set,omitempty"` // compile-time values for this filter, e.g. {"strictness": "3"}
}

// Character is the progression input: manual, or synchronized by a provider the host supports.
type Character struct {
	Source string `json:"source"` // "manual" (the only built-in source)
	Name   string `json:"name,omitempty"`
	Level  int    `json:"level,omitempty"`
}

// Regeneration controls the scheduler.
type Regeneration struct {
	Enabled          bool   `json:"enabled"`
	MinimumInterval  string `json:"minimumInterval,omitempty"`
	PreserveLastGood bool   `json:"preserveLastValid"`
}

// Logging bounds persisted logs. Keep is the number of retained files per stream.
type Logging struct {
	Directory string `json:"directory,omitempty"`
	Keep      int    `json:"keep,omitempty"`
	MaxBytes  int64  `json:"maxBytes,omitempty"`
}

func (p *Project) LogOptions() (string, int, int64) {
	dir := p.Logging.Directory
	if dir == "" {
		dir = ".ruleforge/logs"
	}
	if !filepath.IsAbs(dir) {
		dir = filepath.Join(p.Dir, dir)
	}
	keep := p.Logging.Keep
	if keep == 0 {
		keep = 10
	}
	size := p.Logging.MaxBytes
	if size == 0 {
		size = 4 << 20
	}
	return dir, keep, size
}

// Project is the whole file.
// GameData uses a PoB src/Data path or a managed, revision-pinned download, with optional wiki or local drop levels.
type GameData struct {
	PoBPath         string `json:"pobPath,omitempty"`
	Commit          string `json:"commit,omitempty"`
	DropLevelsPath  string `json:"dropLevelsPath,omitempty"`
	FetchDropLevels bool   `json:"fetchDropLevels,omitempty"`
}

type History struct {
	Database string         `json:"database,omitempty"`
	URL      string         `json:"url,omitempty"`
	TokenEnv string         `json:"tokenEnv,omitempty"`
	Leagues  []string       `json:"leagues"`
	Memory   *EconomyMemory `json:"memory,omitempty"`
}

type Project struct {
	EconomyHistory *History          `json:"economyHistory,omitempty"`
	GameData       *GameData         `json:"gameData,omitempty"`
	Logging        Logging           `json:"logging,omitempty"`
	Entry          string            `json:"entry"`
	Include        []string          `json:"include,omitempty"`
	Context        map[string]string `json:"context,omitempty"` // compile-time values shared by every output
	Datasets       []Dataset         `json:"datasets,omitempty"`
	Outputs        []Output          `json:"outputs,omitempty"`
	Character      *Character        `json:"character,omitempty"`
	Regeneration   Regeneration      `json:"regeneration"`
	Dir            string            `json:"-"` // the project folder
}

// Load reads studio.json from a project folder.
func Load(dir string) (*Project, error) {
	path := filepath.Join(dir, "studio.json")
	data, err := os.ReadFile(path)
	if err != nil {
		if os.IsNotExist(err) {
			return &Project{Entry: "entry.rf", Dir: dir, Regeneration: Regeneration{PreserveLastGood: true}}, nil
		}
		return nil, err
	}
	var project Project
	if err := json.Unmarshal(data, &project); err != nil {
		return nil, fmt.Errorf("%s: %w", path, err)
	}
	if project.Entry == "" {
		project.Entry = "entry.rf"
	}
	project.Dir = dir
	_, keep, size := project.LogOptions()
	if keep < 1 || keep > 65535 || size < 1024 {
		return nil, fmt.Errorf("invalid logging retention")
	}

	for _, output := range project.Outputs {
		if output.Path == "" {
			return nil, fmt.Errorf("%s: an output has no path", path)
		}
	}
	return &project, nil
}

// Interval parses a duration such as "5m" or "1h"; empty means the default.
func Interval(text string, fallback time.Duration) (time.Duration, error) {
	if text == "" {
		return fallback, nil
	}
	return time.ParseDuration(text)
}

// Save writes studio.json atomically.
func (p *Project) Save() error {
	data, err := json.MarshalIndent(p, "", "  ")
	if err != nil {
		return err
	}
	path := filepath.Join(p.Dir, "studio.json")
	temp := path + ".tmp"
	if err := os.WriteFile(temp, append(data, '\n'), 0o644); err != nil {
		return err
	}
	return os.Rename(temp, path)
}
