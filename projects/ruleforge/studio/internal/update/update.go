// Package update checks the project's public GitHub repository for a newer release and installs it.
//
// A release carries one archive per platform, ruleforge-<version>-<os>-<arch>.zip, and a SHA256SUMS file. The archive is
// downloaded, verified against that file, and only the program files it is allowed to contain are installed, each by writing
// a ".new" file beside the old one and renaming, so a failure part-way leaves the installed version working.
package update

import (
	"archive/zip"
	"bufio"
	"context"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net/http"
	"os"
	"path/filepath"
	"runtime"
	"strconv"
	"strings"
	"time"
)

// Asset is one downloadable file of a release.
type Asset struct {
	Name string `json:"name"`
	URL  string `json:"browser_download_url"`
	Size int64  `json:"size"`
}

// Release is the part of a GitHub release the updater uses.
type Release struct {
	Version string  `json:"version"`
	Tag     string  `json:"tag_name"`
	Notes   string  `json:"body"`
	Page    string  `json:"html_url"`
	Assets  []Asset `json:"assets"`
}

// Updater talks to one repository.
type Updater struct {
	Repo       string // owner/name
	Current    string // the running version, e.g. 1.2.3 (or "dev")
	InstallDir string // the folder holding the program files
	API        string // the API base, default https://api.github.com
	Client     *http.Client
	GOOS       string
	GOARCH     string
}

// installable lists the files an archive may install; anything else in it is ignored.
var installable = map[string]bool{
	"ruleforge": true, "ruleforge.exe": true, "ruleforge-studio": true, "ruleforge-studio.exe": true,
	"libruleforge.so": true, "ruleforge.dll": true, "libnexus.so": true, "libecho.so": true, "libpharos.so": true, "liblogos.so": true,
	"nexus.dll": true, "echo.dll": true, "pharos.dll": true, "logos.dll": true,
}

func (u *Updater) client() *http.Client {
	if u.Client != nil {
		return u.Client
	}
	return &http.Client{Timeout: 5 * time.Minute}
}

func (u *Updater) api() string {
	if u.API != "" {
		return strings.TrimRight(u.API, "/")
	}
	return "https://api.github.com"
}

func (u *Updater) platform() (string, string) {
	goos, goarch := u.GOOS, u.GOARCH
	if goos == "" {
		goos = runtime.GOOS
	}
	if goarch == "" {
		goarch = runtime.GOARCH
	}
	return goos, goarch
}

// Compare orders two versions "1.2.3" (a leading v and a -suffix are ignored): -1, 0 or 1.
func Compare(a, b string) int {
	parse := func(v string) [3]int {
		v = strings.TrimPrefix(strings.TrimSpace(v), "v")
		if cut := strings.IndexAny(v, "-+"); cut >= 0 {
			v = v[:cut]
		}
		var out [3]int
		for i, part := range strings.SplitN(v, ".", 3) {
			out[i], _ = strconv.Atoi(part)
		}
		return out
	}
	x, y := parse(a), parse(b)
	for i := 0; i < 3; i++ {
		if x[i] < y[i] {
			return -1
		}
		if x[i] > y[i] {
			return 1
		}
	}
	return 0
}

func (u *Updater) get(ctx context.Context, address string, limit int64) ([]byte, error) {
	request, err := http.NewRequestWithContext(ctx, http.MethodGet, address, nil)
	if err != nil {
		return nil, err
	}
	request.Header.Set("Accept", "application/vnd.github+json")
	request.Header.Set("User-Agent", "ruleforge-updater")
	response, err := u.client().Do(request)
	if err != nil {
		return nil, err
	}
	defer response.Body.Close()
	if response.StatusCode != http.StatusOK {
		return nil, fmt.Errorf("GET %s: HTTP %d", address, response.StatusCode)
	}
	return io.ReadAll(io.LimitReader(response.Body, limit))
}

// Check asks for the latest release and reports whether it is newer than the running version.
func (u *Updater) Check(ctx context.Context) (Release, bool, error) {
	if u.Repo == "" {
		return Release{}, false, errors.New("no update repository is configured")
	}
	data, err := u.get(ctx, u.api()+"/repos/"+u.Repo+"/releases/latest", 4<<20)
	if err != nil {
		return Release{}, false, err
	}
	var release Release
	if err := json.Unmarshal(data, &release); err != nil {
		return Release{}, false, err
	}
	release.Version = strings.TrimPrefix(release.Tag, "v")
	if release.Version == "" {
		return Release{}, false, errors.New("the latest release has no version tag")
	}
	// A development build never updates itself over a release.
	if u.Current == "" || u.Current == "dev" {
		return release, false, nil
	}
	return release, Compare(release.Version, u.Current) > 0, nil
}

func (u *Updater) asset(release Release, name string) (Asset, bool) {
	for _, asset := range release.Assets {
		if asset.Name == name {
			return asset, true
		}
	}
	return Asset{}, false
}

// Apply downloads, verifies and installs a release.
func (u *Updater) Apply(ctx context.Context, release Release) error {
	goos, goarch := u.platform()
	archiveName := fmt.Sprintf("ruleforge-%s-%s-%s.zip", release.Version, goos, goarch)
	archive, ok := u.asset(release, archiveName)
	if !ok {
		return fmt.Errorf("release %s has no archive for %s/%s (%s)", release.Version, goos, goarch, archiveName)
	}
	sums, ok := u.asset(release, "SHA256SUMS")
	if !ok {
		return fmt.Errorf("release %s has no SHA256SUMS; refusing to install an unverified archive", release.Version)
	}
	sumsData, err := u.get(ctx, sums.URL, 1<<20)
	if err != nil {
		return err
	}
	expected := ""
	scanner := bufio.NewScanner(strings.NewReader(string(sumsData)))
	for scanner.Scan() {
		fields := strings.Fields(scanner.Text())
		if len(fields) == 2 && strings.TrimPrefix(fields[1], "*") == archiveName {
			expected = strings.ToLower(fields[0])
		}
	}
	if expected == "" {
		return fmt.Errorf("SHA256SUMS does not list %s", archiveName)
	}
	data, err := u.get(ctx, archive.URL, 512<<20)
	if err != nil {
		return err
	}
	sum := sha256.Sum256(data)
	if hex.EncodeToString(sum[:]) != expected {
		return errors.New("the downloaded archive does not match its published checksum; nothing was installed")
	}
	return u.install(data)
}

// install writes every installable file of the zip beside the old one, then renames them into place.
func (u *Updater) install(data []byte) error {
	reader, err := zip.NewReader(bytesReaderAt(data), int64(len(data)))
	if err != nil {
		return err
	}
	type staged struct{ temp, final string }
	var files []staged
	cleanup := func() {
		for _, f := range files {
			os.Remove(f.temp)
		}
	}
	for _, file := range reader.File {
		name := filepath.Base(file.Name)
		if file.FileInfo().IsDir() || !installable[name] || file.UncompressedSize64 > 512<<20 {
			continue
		}
		source, err := file.Open()
		if err != nil {
			cleanup()
			return err
		}
		final := filepath.Join(u.InstallDir, name)
		temp := final + ".new"
		out, err := os.OpenFile(temp, os.O_CREATE|os.O_WRONLY|os.O_TRUNC, 0o755)
		if err != nil {
			source.Close()
			cleanup()
			return err
		}
		_, copyErr := io.Copy(out, io.LimitReader(source, 512<<20))
		source.Close()
		closeErr := out.Close()
		files = append(files, staged{temp, final})
		if copyErr != nil || closeErr != nil {
			cleanup()
			return fmt.Errorf("writing %s: %v %v", name, copyErr, closeErr)
		}
	}
	if len(files) == 0 {
		return errors.New("the archive contains no program files")
	}
	for _, f := range files {
		old := f.final + ".old"
		os.Remove(old)
		// Renaming a running executable is allowed on every platform (Windows included); deleting it is not.
		if _, err := os.Stat(f.final); err == nil {
			if err := os.Rename(f.final, old); err != nil {
				cleanup()
				return err
			}
		}
		if err := os.Rename(f.temp, f.final); err != nil {
			_ = os.Rename(old, f.final)
			cleanup()
			return err
		}
	}
	return nil
}
