package provider

import (
	"fmt"
	"strings"
	"testing"
)

func TestDivinationPreservesUpstreamSubtiersAndExceptions(t *testing.T) {
	fixture := "# VERSION:  8.21.1\r\n\r\n"
	for _, tier := range []string{"t1", "t2", "t3", "t4", "t4c", "t5", "t5c", "excustomstack"} {
		stack := ""
		if tier == "excustomstack" {
			stack = "\tStackSize >= 3\r\n"
		}
		fixture += fmt.Sprintf("Show # $type->divination $tier->%s !test\r\n\tClass == \"Divination Cards\"\r\n\tBaseType == \"Card %s\"\r\n%s\r\n", tier, tier, stack)
	}
	rows, err := parseDivination([]byte(fixture))
	if err != nil {
		t.Fatal(err)
	}
	if len(rows) != 8 || rows[7].Tier != "excustomstack" || rows[7].StackSize != 3 || rows[0].Version != "8.21.1" || len(rows[0].SHA256) != 64 {
		t.Fatalf("bad import: %+v", rows)
	}
	for _, bad := range []string{strings.Replace(fixture, "$tier->t4c", "$tier->unexpected", 1), strings.Replace(fixture, "Card t2", "Card t1", 1), strings.Replace(fixture, "$tier->t1", "$tier->restex", 1)} {
		if _, err := parseDivination([]byte(bad)); err == nil {
			t.Fatal("invalid or incomplete tier dataset accepted")
		}
	}
}
