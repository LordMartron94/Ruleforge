// Package logging persists host logs with bounded size and retention.
package logging

import (
	"fmt"
	"os"
	"path/filepath"
	"sync"
)

type Writer struct {
	mutex sync.Mutex
	path  string
	keep  int
	limit int64
	file  *os.File
	size  int64
}

func Open(dir string, keep int, limit int64) (*Writer, error) {
	if keep < 1 || keep > 65535 || limit < 1024 {
		return nil, fmt.Errorf("invalid log retention: keep 1..65535 files, size at least 1024 bytes")
	}
	if err := os.MkdirAll(dir, 0o755); err != nil {
		return nil, err
	}
	w := &Writer{path: filepath.Join(dir, "studio.log"), keep: keep, limit: limit}
	f, err := os.OpenFile(w.path, os.O_CREATE|os.O_APPEND|os.O_WRONLY, 0o600)
	if err != nil {
		return nil, err
	}
	w.file = f
	info, err := f.Stat()
	if err != nil {
		f.Close()
		return nil, err
	}
	w.size = info.Size()
	// Apply smaller retention settings on startup too.
	matches, _ := filepath.Glob(w.path + ".*")
	for _, path := range matches {
		var n int
		if _, err := fmt.Sscanf(path, w.path+".%d", &n); err == nil && n >= keep {
			if err := os.Remove(path); err != nil {
				f.Close()
				return nil, err
			}
		}
	}
	return w, nil
}

func (w *Writer) Write(p []byte) (int, error) {
	w.mutex.Lock()
	defer w.mutex.Unlock()
	if w.file == nil {
		return 0, os.ErrClosed
	}
	if w.size > 0 && w.size+int64(len(p)) > w.limit {
		if err := w.file.Close(); err != nil {
			return 0, err
		}
		w.file = nil
		if w.keep > 1 {
			if err := os.Remove(fmt.Sprintf("%s.%d", w.path, w.keep-1)); err != nil && !os.IsNotExist(err) {
				return 0, err
			}
			for n := w.keep - 2; n >= 1; n-- {
				if err := os.Rename(fmt.Sprintf("%s.%d", w.path, n), fmt.Sprintf("%s.%d", w.path, n+1)); err != nil && !os.IsNotExist(err) {
					return 0, err
				}
			}
			if err := os.Rename(w.path, w.path+".1"); err != nil {
				return 0, err
			}
		}
		f, err := os.OpenFile(w.path, os.O_CREATE|os.O_TRUNC|os.O_WRONLY, 0o600)
		if err != nil {
			return 0, err
		}
		w.file = f
		w.size = 0
	}
	n, err := w.file.Write(p)
	w.size += int64(n)
	return n, err
}
func (w *Writer) Close() error {
	w.mutex.Lock()
	defer w.mutex.Unlock()
	if w.file == nil {
		return nil
	}
	err := w.file.Close()
	w.file = nil
	return err
}
