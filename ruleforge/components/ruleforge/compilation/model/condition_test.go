package model

import "testing"

func TestCorruptedModsCompilesToTheGameCondition(t *testing.T) {
	condition := Condition{Identifier: "@corrupted_mods", Operator: ">=", Value: []string{"1"}}
	if got := condition.ConstructCompiledCondition(&map[string][]string{}, nil); got != `CorruptedMods >= "1" ` {
		t.Fatalf("got %q", got)
	}
}
