#include <nexus.h>

#include "./r_internal.h"

void ruleforge_diagnostic_report(RuleforgeCompilation *compilation, RuleforgeDiagnosticSeverity severity, const char *code, RuleforgeSpan span,
                                 const char *note, const char *format, ...) {
  RuleforgeDiagnostic *diagnostic;
  char                 message[1024];
  va_list              arguments;

  NEXUS_PROGRAM_PRECONDITION(compilation != NULL && code != NULL && format != NULL, "ruleforge_diagnostic_report: NULL argument");

  va_start(arguments, format);
  (void)stbsp_vsnprintf(message, (int)NEXUS_SIZEOF(message), format, arguments);
  va_end(arguments);

  diagnostic           = (RuleforgeDiagnostic *)nexus_data_arena_allocate(&compilation->arena, NEXUS_SIZEOF(*diagnostic));
  diagnostic->severity = severity;
  diagnostic->code     = code;
  diagnostic->message  = nexus_data_arena_text_copy(&compilation->arena, message, nexus_strings_string_length_get(message));
  diagnostic->span     = span;
  diagnostic->note     = note == NULL ? NULL : nexus_data_arena_text_copy(&compilation->arena, note, nexus_strings_string_length_get(note));

  (void)nexus_data_list_append_pointer(compilation->diagnostics, diagnostic);

  if (severity == RDS_ERROR) {
    compilation->error_count++;
  }
}
