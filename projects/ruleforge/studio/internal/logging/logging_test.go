package logging

import (
	"os"
	"path/filepath"
	"testing"
)

func TestRotationAndSmallerRetention(t *testing.T) {
	dir := t.TempDir()
	w, err := Open(dir, 3, 1024)
	if err != nil {
		t.Fatal(err)
	}
	for i := 0; i < 8; i++ {
		if _, err := w.Write(make([]byte, 700)); err != nil {
			t.Fatal(err)
		}
	}
	if err := w.Close(); err != nil {
		t.Fatal(err)
	}
	files, _ := filepath.Glob(filepath.Join(dir, "studio.log*"))
	if len(files) != 3 {
		t.Fatalf("want 3 retained files: %v", files)
	}
	w, err = Open(dir, 1, 1024)
	if err != nil {
		t.Fatal(err)
	}
	defer w.Close()
	files, _ = filepath.Glob(filepath.Join(dir, "studio.log*"))
	if len(files) != 1 {
		t.Fatalf("retention reduction: %v", files)
	}
	if _, err := os.Stat(files[0]); err != nil {
		t.Fatal(err)
	}
}
