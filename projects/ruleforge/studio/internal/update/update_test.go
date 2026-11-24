package update

import (
	"archive/zip"
	"bytes"
	"context"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"testing"
)

func TestCompare(t *testing.T) {
	cases := []struct {
		a, b string
		want int
	}{{"1.2.3", "1.2.3", 0}, {"v1.10.0", "1.9.9", 1}, {"0.9.0", "1.0.0", -1}, {"1.0.0-rc1", "1.0.0", 0}}
	for _, c := range cases {
		if got := Compare(c.a, c.b); got != c.want {
			t.Errorf("Compare(%q,%q)=%d want %d", c.a, c.b, got, c.want)
		}
	}
}

func buildArchive(t *testing.T, files map[string]string) []byte {
	var buffer bytes.Buffer
	writer := zip.NewWriter(&buffer)
	for name, content := range files {
		entry, err := writer.Create(name)
		if err != nil {
			t.Fatal(err)
		}
		_, _ = entry.Write([]byte(content))
	}
	if err := writer.Close(); err != nil {
		t.Fatal(err)
	}
	return buffer.Bytes()
}

func releaseServer(t *testing.T, version string, archive []byte, goos, goarch string, checksum string) *httptest.Server {
	var server *httptest.Server
	mux := http.NewServeMux()
	name := fmt.Sprintf("ruleforge-%s-%s-%s.zip", version, goos, goarch)
	mux.HandleFunc("/repos/owner/repo/releases/latest", func(w http.ResponseWriter, r *http.Request) {
		_ = json.NewEncoder(w).Encode(map[string]any{
			"tag_name": "v" + version,
			"assets": []map[string]any{
				{"name": name, "browser_download_url": server.URL + "/download/" + name},
				{"name": "SHA256SUMS", "browser_download_url": server.URL + "/download/SHA256SUMS"},
			},
		})
	})
	mux.HandleFunc("/download/"+name, func(w http.ResponseWriter, r *http.Request) { _, _ = w.Write(archive) })
	mux.HandleFunc("/download/SHA256SUMS", func(w http.ResponseWriter, r *http.Request) {
		fmt.Fprintf(w, "%s  %s\n", checksum, name)
	})
	server = httptest.NewServer(mux)
	return server
}

func TestApplyInstallsVerifiedArchive(t *testing.T) {
	dir := t.TempDir()
	if err := os.WriteFile(filepath.Join(dir, "ruleforge"), []byte("old"), 0o755); err != nil {
		t.Fatal(err)
	}
	archive := buildArchive(t, map[string]string{"ruleforge": "new engine", "ruleforge-studio": "new studio", "../evil": "x", "README.md": "ignored"})
	sum := sha256.Sum256(archive)
	server := releaseServer(t, "1.1.0", archive, "linux", "amd64", hex.EncodeToString(sum[:]))
	defer server.Close()
	updater := &Updater{Repo: "owner/repo", Current: "1.0.0", InstallDir: dir, API: server.URL, GOOS: "linux", GOARCH: "amd64"}
	release, available, err := updater.Check(context.Background())
	if err != nil || !available {
		t.Fatalf("Check: available=%v err=%v", available, err)
	}
	if err := updater.Apply(context.Background(), release); err != nil {
		t.Fatal(err)
	}
	for name, want := range map[string]string{"ruleforge": "new engine", "ruleforge-studio": "new studio"} {
		got, err := os.ReadFile(filepath.Join(dir, name))
		if err != nil || string(got) != want {
			t.Errorf("%s = %q (%v), want %q", name, got, err, want)
		}
	}
	if _, err := os.Stat(filepath.Join(dir, "README.md")); err == nil {
		t.Error("a file outside the allowlist was installed")
	}
	if _, err := os.Stat(filepath.Join(filepath.Dir(dir), "evil")); err == nil {
		t.Error("a path-traversal entry escaped the install folder")
	}
	old, _ := os.ReadFile(filepath.Join(dir, "ruleforge.old"))
	if string(old) != "old" {
		t.Errorf("the previous engine was not kept as .old: %q", old)
	}
}

func TestApplyRejectsBadChecksum(t *testing.T) {
	dir := t.TempDir()
	if err := os.WriteFile(filepath.Join(dir, "ruleforge"), []byte("old"), 0o755); err != nil {
		t.Fatal(err)
	}
	archive := buildArchive(t, map[string]string{"ruleforge": "tampered"})
	server := releaseServer(t, "1.1.0", archive, "linux", "amd64", "00"+hex.EncodeToString(make([]byte, 31)))
	defer server.Close()
	updater := &Updater{Repo: "owner/repo", Current: "1.0.0", InstallDir: dir, API: server.URL, GOOS: "linux", GOARCH: "amd64"}
	release, _, err := updater.Check(context.Background())
	if err != nil {
		t.Fatal(err)
	}
	if err := updater.Apply(context.Background(), release); err == nil {
		t.Fatal("a checksum mismatch was accepted")
	}
	got, _ := os.ReadFile(filepath.Join(dir, "ruleforge"))
	if string(got) != "old" {
		t.Errorf("the installed engine changed despite the failed verification: %q", got)
	}
}

func TestDevBuildNeverUpdates(t *testing.T) {
	archive := buildArchive(t, map[string]string{"ruleforge": "x"})
	sum := sha256.Sum256(archive)
	server := releaseServer(t, "9.0.0", archive, "linux", "amd64", hex.EncodeToString(sum[:]))
	defer server.Close()
	updater := &Updater{Repo: "owner/repo", Current: "dev", InstallDir: t.TempDir(), API: server.URL}
	if _, available, err := updater.Check(context.Background()); err != nil || available {
		t.Fatalf("available=%v err=%v", available, err)
	}
}
