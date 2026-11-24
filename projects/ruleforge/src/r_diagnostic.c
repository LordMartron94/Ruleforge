#include <nexus.h>
#include <echo.h>

#include <stdarg.h>

#include "../ruleforge.h"
#include "./r_internal.h"

void ruleforge_diagnostic_report(RuleforgeCompilation *compilation, RuleforgeDiagnosticSeverity severity, const char *code, RuleforgeSpan span,
                                 const char *note, const char *format, ...) {
  RuleforgeDiagnostic *diagnostic;
  char                 message[1024];
  va_list              arguments;
  char                 key_buffer[2304];
  const char          *key;

  NEXUS_PROGRAM_PRECONDITION(compilation != NULL && code != NULL && format != NULL, "ruleforge_diagnostic_report: NULL argument");

  va_start(arguments, format);
  (void)stbsp_vsnprintf(message, (int)NEXUS_SIZEOF(message), format, arguments);
  va_end(arguments);

  (void)stbsp_snprintf(key_buffer, (int)NEXUS_SIZEOF(key_buffer), "%u:%u:%u:%u:%s:%s:%s", (unsigned)severity, (unsigned)span.file,
                       (unsigned)span.start, (unsigned)span.end, code, message, note == NULL ? "" : note);
  key = key_buffer;

  if (nexus_data_hashmap_get_pointer(compilation->diagnostic_keys, (const void *)&key) != NULL) {
    return;
  }

  key = nexus_data_arena_text_copy(&compilation->arena, key_buffer, nexus_strings_string_length_get(key_buffer));

  diagnostic           = (RuleforgeDiagnostic *)nexus_data_arena_allocate(&compilation->arena, NEXUS_SIZEOF(*diagnostic));
  diagnostic->severity = severity;
  diagnostic->code     = code;
  diagnostic->message  = nexus_data_arena_text_copy(&compilation->arena, message, nexus_strings_string_length_get(message));
  diagnostic->span     = span;
  diagnostic->note     = note == NULL ? NULL : nexus_data_arena_text_copy(&compilation->arena, note, nexus_strings_string_length_get(note));

  nexus_data_hashmap_put_pointer(compilation->diagnostic_keys, (const void *)&key, diagnostic);
  (void)nexus_data_list_append_pointer(compilation->diagnostics, diagnostic);
  (void)echo_message_log_at(compilation->runtime->log_context, severity == RDS_ERROR ? ELL_ERROR : ELL_WARNING, __FILE__, (uint32)__LINE__, "%s: %s",
                            code, message);

  if (severity == RDS_ERROR) {
    compilation->error_count++;
  }
}

const char *ruleforge_diagnostic_severity_name_get(RuleforgeDiagnosticSeverity severity) {
  if (severity == RDS_ERROR) {
    return "error";
  }

  if (severity == RDS_WARNING) {
    return "warning";
  }

  return "note";
}
