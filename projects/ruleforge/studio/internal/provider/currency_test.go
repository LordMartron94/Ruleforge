package provider

import (
	"fmt"
	"strings"
	"testing"
)

func TestCurrencyPreservesRegularAndStackTiers(t *testing.T) {
	fixture := "# VERSION:  8.21.1\r\n\r\n"
	for tier := 1; tier <= 9; tier++ {
		name := fmt.Sprintf("Currency %d", tier)
		if tier == 5 {
			name = "Blessed Orb"
		}
		fixture += fmt.Sprintf("Show # $type->currency $tier->t%dexample !currency\r\n\tClass == \"Stackable Currency\"\r\n\tBaseType == %q\r\n\r\n", tier, name)
	}
	for _, group := range []struct {
		name  string
		stack int
	}{{"stackedthree", 3}, {"stackedsix", 6}} {
		fixture += fmt.Sprintf("Show # $type->currency->%s $tier->t1 !currency\r\n\tClass == \"Stackable Currency\"\r\n\tBaseType == \"Blessed Orb\"\r\n\tStackSize >= %d\r\n\r\n", group.name, group.stack)
	}
	fixture += "Show # $type->currency $tier->restex !utility_restex\r\n\tClass == \"Stackable Currency\"\r\n\r\n"
	rows, err := parseCurrency([]byte(fixture))
	if err != nil {
		t.Fatal(err)
	}
	if len(rows) != 11 || rows[4].BaseType != "Blessed Orb" || rows[4].Tier != 5 || rows[4].StackSize != 1 || rows[9].StackSize != 3 || rows[10].StackSize != 6 || rows[0].Version != "8.21.1" || len(rows[0].SHA256) != 64 {
		t.Fatalf("bad import: %+v", rows)
	}
	for _, bad := range []string{
		strings.Replace(fixture, "$tier->t5example", "$tier->unexpected", 1),
		strings.Replace(fixture, "Currency 2", "Currency 1", 1),
		strings.Replace(fixture, "$tier->t9example", "$tier->restex", 1),
		strings.Replace(fixture, "StackSize >= 3", "StackSize >= 7", 1),
		strings.Replace(fixture, "StackSize >= 3", "AreaLevel >= 68", 1),
	} {
		if _, err := parseCurrency([]byte(bad)); err == nil {
			t.Fatal("invalid or incomplete currency dataset accepted")
		}
	}
}
