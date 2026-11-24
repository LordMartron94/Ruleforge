package server

import (
	"crypto/sha256"
	"fmt"
	"os"
	"path/filepath"
	"testing"
)

func TestFormsRefuseStaleSourcePositions(t *testing.T) {
	server, dir := newTestServer(t)
	path := filepath.Join(dir, "entry.rf")
	text, _ := os.ReadFile(path)
	hash := sha256.Sum256(text)
	body := fmt.Sprintf(`{"path":"entry.rf","start":0,"end":7,"hash":"%x","text":"version"}`, hash)
	handler := server.Handler()
	if w := request(handler, "POST", "/api/source/replace", "localhost:8750", body); w.Code != 200 {
		t.Fatal(w.Body.String())
	}
	os.WriteFile(path, []byte("changed outside the form"), 0o644)
	if w := request(handler, "POST", "/api/source/replace", "localhost:8750", body); w.Code != 409 {
		t.Fatalf("stale edit accepted: %d %s", w.Code, w.Body.String())
	}
	after, _ := os.ReadFile(path)
	if string(after) != "changed outside the form" {
		t.Fatal("concurrent source was overwritten")
	}
}
