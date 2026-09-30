package tomledit

import "testing"

func TestEditsKeepEverythingElse(t *testing.T) {
	src := "# Head\nName = \"A\" # the name\n\n[T.one]\n# about x\nx = 1\n\n# the second\n[T.two]\ny = { A = 1 }\n"
	doc, err := Parse(src)
	if err != nil {
		t.Fatal(err)
	}
	if doc.String() != src {
		t.Fatalf("round trip changed the file:\n%s", doc.String())
	}
	if err := doc.Set("", "Name", String("B")); err != nil {
		t.Fatal(err)
	}
	if err := doc.Set("T.one", "z", Int(2)); err != nil {
		t.Fatal(err)
	}
	doc.Delete("T.two", "y")
	if err := doc.AddTable("T.three", "T.one", []KeyValue{{Key: "w", Value: StringArray([]string{"a", "b"})}}); err != nil {
		t.Fatal(err)
	}
	want := "# Head\nName = \"B\" # the name\n\n[T.one]\n# about x\nx = 1\nz = 2\n\n[T.three]\nw = [\"a\", \"b\"]\n\n# the second\n[T.two]\n"
	if got := doc.String(); got != want {
		t.Fatalf("got:\n%s\nwant:\n%s", got, want)
	}
	if comments := doc.Comments("T.two"); len(comments) != 1 || comments[0] != "# the second" {
		t.Fatalf("comments = %q", comments)
	}
}

func TestStringEscapes(t *testing.T) {
	if got := String("a \"b\" \\ c"); got != `"a \"b\" \\ c"` {
		t.Fatalf("got %s", got)
	}
	if FormatKey("rad-t0-bg") != "rad-t0-bg" || FormatKey("a b") != `"a b"` {
		t.Fatal("key formatting")
	}
}
