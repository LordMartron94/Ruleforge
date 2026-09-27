package config

import (
	"os"
	"path/filepath"
	"testing"
)

func TestResolveConfigurationPath(t *testing.T) {
	base := t.TempDir()
	absolute := filepath.Join(base, "existing")
	cases := map[string]string{
		"":                   "",
		"input":              filepath.Join(base, "input"),
		"../other":           filepath.Clean(filepath.Join(base, "../other")),
		"./output/../output": filepath.Join(base, "output"),
		absolute:             absolute,
	}
	for input, want := range cases {
		if got := resolveConfigurationPath(base, input); got != want {
			t.Errorf("%q: got %q want %q", input, got, want)
		}
	}
}

func TestConfigurationBaseDir(t *testing.T) {
	path := filepath.Join("folder", "config.toml")
	base, err := configurationBaseDir(path)
	if err != nil {
		t.Fatal(err)
	}
	cwd, err := os.Getwd()
	if err != nil {
		t.Fatal(err)
	}
	if want := filepath.Join(cwd, "folder"); base != want {
		t.Fatalf("got %q want %q", base, want)
	}
}
