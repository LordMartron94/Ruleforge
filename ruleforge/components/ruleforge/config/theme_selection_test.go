package config

import (
	"strings"
	"testing"
)

func TestSelectStyles(t *testing.T) {
	styles := []StyleDefinition{
		{ID: "aryzel-1-stranded-prince"}, {ID: "aryzel-2-first-whisper"}, {ID: "new-theme"}, {ID: "original"},
	}
	all := []string{"aryzel-1-stranded-prince", "aryzel-2-first-whisper", "aryzel-3-twilight", "new-theme", "original"}
	ids := func(selected []StyleDefinition) string {
		var out []string
		for _, s := range selected {
			out = append(out, s.ID)
		}
		return strings.Join(out, ",")
	}

	cases := []struct {
		value, want string
	}{
		{"", "aryzel-1-stranded-prince,aryzel-2-first-whisper,new-theme,original"},
		{"aryzel", "aryzel-1-stranded-prince,aryzel-2-first-whisper"},
		{"ARYZEL-2", "aryzel-2-first-whisper"},
		{"new-theme original", "new-theme,original"},
		{"original aryzel-1-stranded-prince", "aryzel-1-stranded-prince,original"},
		{"aryzel-3-twilight", ""}, // known but disabled: skipped, not an error
	}
	for _, c := range cases {
		got, err := SelectStyles(styles, ThemeNames(c.value), all)
		if err != nil {
			t.Fatalf("%q: %v", c.value, err)
		}
		if ids(got) != c.want {
			t.Errorf("%q selected %q, want %q", c.value, ids(got), c.want)
		}
	}

	if _, err := SelectStyles(styles, ThemeNames("orignal"), all); err == nil || !strings.Contains(err.Error(), "orignal") {
		t.Errorf("a misspelt theme should be an error, got %v", err)
	}
	// "aryz" is not a hyphen-bounded prefix of any ID.
	if _, err := SelectStyles(styles, ThemeNames("aryz"), all); err == nil {
		t.Error("a prefix that cuts a word should not match")
	}
}
