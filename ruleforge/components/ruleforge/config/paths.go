package config

import "path/filepath"

// configurationBaseDir makes paths independent of the caller's working directory.
func configurationBaseDir(configFile string) (string, error) {
	absolute, err := filepath.Abs(configFile)
	if err != nil {
		return "", err
	}
	return filepath.Dir(absolute), nil
}

func resolveConfigurationPath(base, path string) string {
	if path == "" {
		return ""
	}
	if filepath.IsAbs(path) {
		return filepath.Clean(path)
	}
	return filepath.Join(base, path)
}
