package server

import (
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"strings"
	"testing"

	"github.com/LordMartron94/Ruleforge/studio/internal/config"
	"github.com/LordMartron94/Ruleforge/studio/internal/engine"
)

func newTestServer(t *testing.T) (*Server, string) {
	dir := t.TempDir()
	if err := os.WriteFile(filepath.Join(dir, "entry.rf"), []byte("version 1.0.0;\nmodule entry;\n"), 0o644); err != nil {
		t.Fatal(err)
	}
	outside := t.TempDir()
	if err := os.WriteFile(filepath.Join(outside, "secret.txt"), []byte("secret"), 0o644); err != nil {
		t.Fatal(err)
	}
	if err := os.Symlink(outside, filepath.Join(dir, "link")); err != nil {
		t.Log("symlinks unavailable:", err)
	}
	return New(&config.Project{Dir: dir, Entry: "entry.rf"}, engine.Engine{Path: "/nonexistent"}, "test", nil), dir
}

func request(handler http.Handler, method, target, host, body string) *httptest.ResponseRecorder {
	r := httptest.NewRequest(method, target, strings.NewReader(body))
	r.Host = host
	w := httptest.NewRecorder()
	handler.ServeHTTP(w, r)
	return w
}

func TestFileAPIStaysInsideProject(t *testing.T) {
	server, _ := newTestServer(t)
	handler := server.Handler()
	for _, path := range []string{"../x", "..", "/etc/passwd", "link/secret.txt", "a/../../x"} {
		if w := request(handler, "GET", "/api/file?path="+path, "localhost:8750", ""); w.Code == http.StatusOK {
			t.Errorf("path %q escaped the project", path)
		}
		if w := request(handler, "PUT", "/api/file?path="+path, "localhost:8750", "x"); w.Code == http.StatusOK {
			t.Errorf("write to %q escaped the project", path)
		}
	}
	if w := request(handler, "PUT", "/api/file?path=filters/new.rf", "localhost:8750", "version 1.0.0;"); w.Code != http.StatusOK {
		t.Fatalf("a project path must be writable: %d %s", w.Code, w.Body.String())
	}
	if w := request(handler, "GET", "/api/file?path=filters/new.rf", "localhost:8750", ""); w.Body.String() != "version 1.0.0;" {
		t.Errorf("read back %q", w.Body.String())
	}
}

func TestRejectsForeignHostAndOrigin(t *testing.T) {
	server, _ := newTestServer(t)
	handler := server.Handler()
	if w := request(handler, "GET", "/api/info", "evil.example.com", ""); w.Code != http.StatusForbidden {
		t.Errorf("a foreign Host (DNS rebinding) was served: %d", w.Code)
	}
	r := httptest.NewRequest("PUT", "/api/file?path=a.rf", strings.NewReader("x"))
	r.Host = "localhost:8750"
	r.Header.Set("Origin", "https://evil.example.com")
	w := httptest.NewRecorder()
	handler.ServeHTTP(w, r)
	if w.Code != http.StatusForbidden {
		t.Errorf("a cross-origin write was accepted: %d", w.Code)
	}
}
