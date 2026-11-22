#include <nexus.h>
#include <pharos.h>

static int pharos_test_fail(int code) {
  return code;
}

static int pharos_test_paths(void) {
  char *path;
  char *uri;

  path = pharos_paths_path_of_uri("file:///a/b/../c/./d%20e.gen");

  if (!nexus_strings_string_equals(path, "/a/c/d e.gen")) {
    return pharos_test_fail(1);
  }

  uri = pharos_paths_uri_of_path(path);

  if (!nexus_strings_string_equals(uri, "file:///a/c/d%20e.gen")) {
    return pharos_test_fail(2);
  }

  nexus_memory_deallocate(path);
  nexus_memory_deallocate(uri);

  return 0;
}

static int pharos_test_diagnostics(void) {
  PharosDiagnostics *diagnostics;
  PharosDiagnostic  *diagnostic;
  PharosSpan         span;
  uint32             source;
  char              *text;
  uint_large         offset;
  uint_large         width;
  uint_large         line;
  uint_large         column;

  diagnostics = pharos_diagnostics_create();
  source      = pharos_diagnostics_source_add(diagnostics, "a.txt", (const byte *)"one\ntwo three\n", 14);
  span.source = source;
  span.offset = 8;
  span.width  = 5;
  diagnostic  = pharos_diagnostics_report(diagnostics, PDS_ERROR, "E0001", span, "unexpected `%s`", "three");
  pharos_diagnostics_label_add(diagnostic, span, TRUE, "here");
  pharos_diagnostics_help_add(diagnostic, "remove it");
  pharos_diagnostics_span_get(diagnostic, &offset, &width, &line, &column);

  if (line != 2 || column != 5 || pharos_diagnostics_count_get(diagnostics) != 1 || !pharos_diagnostics_has_errors(diagnostics)) {
    return pharos_test_fail(3);
  }

  text = pharos_diagnostics_render(diagnostics, FALSE);

  if (!nexus_strings_string_contains(text, "error[E0001]: unexpected `three`") || !nexus_strings_string_contains(text, "a.txt:2:5") ||
      !nexus_strings_string_contains(text, "two three") || !nexus_strings_string_contains(text, "^^^^^ here") ||
      !nexus_strings_string_contains(text, "help: remove it")) {
    nexus_memory_deallocate(text);

    return pharos_test_fail(4);
  }

  nexus_memory_deallocate(text);
  pharos_diagnostics_destroy(diagnostics);

  return 0;
}

int main(void) {
  int result;

  result = pharos_test_paths();

  if (result != 0) {
    return result;
  }

  return pharos_test_diagnostics();
}
