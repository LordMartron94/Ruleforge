package data_generation

import (
	"os"
	"path/filepath"
	"testing"
	"time"

	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/data_generation/model"
)

func TestOldItemCacheIsNotReused(t *testing.T) {
	root := t.TempDir()
	items := filepath.Join(root, "basetypes.json")
	old := []byte(`{"expiry_date":"2099-01-01T00:00:00Z","uniques":[{"Name":"old duplicate"}]}`)
	if err := os.WriteFile(items, old, 0644); err != nil {
		t.Fatal(err)
	}
	repo := NewCacheRepository(items)
	loaded, err := repo.LoadCache()
	if err != nil {
		t.Fatal(err)
	}
	if loaded != nil {
		t.Fatal("outdated cache reused after generated-unique fix")
	}
	want := []model.Unique{{Name: "Fresh unique"}}
	if err := repo.SaveItemCache(nil, nil, nil, want); err != nil {
		t.Fatal(err)
	}
	loaded, err = repo.LoadCache()
	if err != nil {
		t.Fatal(err)
	}
	if loaded == nil || loaded.Version != itemCacheSchemaVersion || len(loaded.Uniques) != 1 || loaded.Uniques[0].Name != want[0].Name || time.Now().After(loaded.ExpiryDate) {
		t.Fatalf("fresh cache not reusable: %#v", loaded)
	}
}
