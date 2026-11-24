#include <nexus.h>

#include "./p_internal.h"

#define PHAROS_MESSAGE_MAX 1024

static uint_large p_diagnostics_diagnostic_line_of(const PharosSource *source, uint_large offset);
static uint_large p_diagnostics_diagnostic_column_of(const PharosSource *source, uint_large line, uint_large offset);

/* ---------------------------------------------------------------------------- */
/* CONSTRUCTION                                                                  */
/* ---------------------------------------------------------------------------- */

static char *p_diagnostics_diagnostic_format(const char *format, va_list arguments) {
  char       buffer[PHAROS_MESSAGE_MAX];
  char      *copy;
  uint_large length;

  (void)stbsp_vsnprintf(buffer, (int)PHAROS_MESSAGE_MAX, format, arguments);
  length = nexus_strings_string_length_get(buffer);
  copy   = (char *)nexus_memory_allocate(length + 1);
  nexus_memory_bytes_copy(buffer, copy, length + 1);

  return copy;
}

static char *p_diagnostics_diagnostic_copy(const char *text) {
  uint_large length;
  char      *copy;

  length = nexus_strings_string_length_get(text);
  copy   = (char *)nexus_memory_allocate(length + 1);
  nexus_memory_bytes_copy(text, copy, length + 1);

  return copy;
}

PharosDiagnostic *pharos_diagnostics_report(PharosDiagnostics *diagnostics, PharosSeverity severity, const char *code, PharosSpan span,
                                            const char *format, ...) {
  PharosDiagnostic *diagnostic;
  va_list           arguments;

  NEXUS_PROGRAM_PRECONDITION(diagnostics != NULL && code != NULL && format != NULL, "diagnostic arguments must not be NULL");

  diagnostic           = (PharosDiagnostic *)nexus_memory_allocate(NEXUS_SIZEOF(*diagnostic));
  diagnostic->severity = severity;
  diagnostic->code     = p_diagnostics_diagnostic_copy(code);
  diagnostic->span     = span;
  diagnostic->labels   = nexus_data_list_create(NEXUS_SIZEOF(PharosLabel), 2);
  diagnostic->notes    = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 2);

  {
    const PharosSource *source;
    uint_large          line;

    source                  = pharos_diagnostics_source_get(diagnostics, span.source);
    line                    = p_diagnostics_diagnostic_line_of(source, span.offset);
    diagnostic->source_name = source->name;
    diagnostic->line        = line + 1;
    diagnostic->column      = p_diagnostics_diagnostic_column_of(source, line, span.offset);
  }

  diagnostic->helps = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 2);

  va_start(arguments, format);
  diagnostic->message = p_diagnostics_diagnostic_format(format, arguments);
  va_end(arguments);

  (void)nexus_data_list_append_pointer(diagnostics->diagnostics, diagnostic);

  return diagnostic;
}

void pharos_diagnostics_label_add(PharosDiagnostic *diagnostic, PharosSpan span, boolean primary, const char *format, ...) {
  PharosLabel label;
  va_list     arguments;

  NEXUS_PROGRAM_PRECONDITION(diagnostic != NULL, "diagnostic must not be NULL");

  label.span    = span;
  label.primary = primary;
  label.text    = NULL;

  if (format != NULL) {
    va_start(arguments, format);
    label.text = p_diagnostics_diagnostic_format(format, arguments);
    va_end(arguments);
  }

  (void)nexus_data_list_append(diagnostic->labels, &label);
}

void pharos_diagnostics_note_add(PharosDiagnostic *diagnostic, const char *format, ...) {
  va_list arguments;

  NEXUS_PROGRAM_PRECONDITION(diagnostic != NULL && format != NULL, "note arguments must not be NULL");

  va_start(arguments, format);
  (void)nexus_data_list_append_pointer(diagnostic->notes, p_diagnostics_diagnostic_format(format, arguments));
  va_end(arguments);
}

void pharos_diagnostics_help_add(PharosDiagnostic *diagnostic, const char *format, ...) {
  va_list arguments;

  NEXUS_PROGRAM_PRECONDITION(diagnostic != NULL && format != NULL, "help arguments must not be NULL");

  va_start(arguments, format);
  (void)nexus_data_list_append_pointer(diagnostic->helps, p_diagnostics_diagnostic_format(format, arguments));
  va_end(arguments);
}

static void p_diagnostics_diagnostic_destroy(PharosDiagnostic *diagnostic) {
  uint_large i;

  for (i = 0; i < nexus_data_list_length_get(diagnostic->labels); i++) {
    PharosLabel *label;

    label = (PharosLabel *)nexus_data_list_at(diagnostic->labels, i);

    if (label->text != NULL) {
      nexus_memory_deallocate(label->text);
    }
  }

  for (i = 0; i < nexus_data_list_length_get(diagnostic->notes); i++) {
    nexus_memory_deallocate(nexus_data_list_pointer_at(diagnostic->notes, i));
  }

  for (i = 0; i < nexus_data_list_length_get(diagnostic->helps); i++) {
    nexus_memory_deallocate(nexus_data_list_pointer_at(diagnostic->helps, i));
  }

  nexus_data_list_destroy(diagnostic->labels);
  nexus_data_list_destroy(diagnostic->notes);
  nexus_data_list_destroy(diagnostic->helps);
  nexus_memory_deallocate(diagnostic->code);
  nexus_memory_deallocate(diagnostic->message);
  nexus_memory_deallocate(diagnostic);
}

void pharos_diagnostics_clear(PharosDiagnostics *diagnostics) {
  uint_large i;

  NEXUS_PROGRAM_PRECONDITION(diagnostics != NULL, "diagnostics must not be NULL");

  for (i = 0; i < nexus_data_list_length_get(diagnostics->diagnostics); i++) {
    p_diagnostics_diagnostic_destroy((PharosDiagnostic *)nexus_data_list_pointer_at(diagnostics->diagnostics, i));
  }

  nexus_data_list_clear(diagnostics->diagnostics);
}

/* ---------------------------------------------------------------------------- */
/* GETTERS                                                                       */
/* ---------------------------------------------------------------------------- */

uint_large pharos_diagnostics_count_get(const PharosDiagnostics *diagnostics) {
  NEXUS_PROGRAM_PRECONDITION(diagnostics != NULL, "diagnostics must not be NULL");

  return nexus_data_list_length_get(diagnostics->diagnostics);
}

const PharosDiagnostic *pharos_diagnostics_at(const PharosDiagnostics *diagnostics, uint_large index) {
  NEXUS_PROGRAM_PRECONDITION(diagnostics != NULL && index < nexus_data_list_length_get(diagnostics->diagnostics),
                             "diagnostic index must be in bounds");

  return (const PharosDiagnostic *)nexus_data_list_pointer_at(diagnostics->diagnostics, index);
}

boolean pharos_diagnostics_has_errors(const PharosDiagnostics *diagnostics) {
  uint_large i;

  NEXUS_PROGRAM_PRECONDITION(diagnostics != NULL, "diagnostics must not be NULL");

  for (i = 0; i < nexus_data_list_length_get(diagnostics->diagnostics); i++) {
    if (((const PharosDiagnostic *)nexus_data_list_pointer_at(diagnostics->diagnostics, i))->severity == PDS_ERROR) {
      return TRUE;
    }
  }

  return FALSE;
}

PharosSeverity pharos_diagnostics_severity_get(const PharosDiagnostic *diagnostic) {
  NEXUS_PROGRAM_PRECONDITION(diagnostic != NULL, "diagnostic must not be NULL");

  return diagnostic->severity;
}

const char *pharos_diagnostics_code_get(const PharosDiagnostic *diagnostic) {
  NEXUS_PROGRAM_PRECONDITION(diagnostic != NULL, "diagnostic must not be NULL");

  return diagnostic->code;
}

const char *pharos_diagnostics_message_get(const PharosDiagnostic *diagnostic) {
  NEXUS_PROGRAM_PRECONDITION(diagnostic != NULL, "diagnostic must not be NULL");

  return diagnostic->message;
}

uint_large pharos_diagnostics_note_count_get(const PharosDiagnostic *diagnostic) {
  NEXUS_PROGRAM_PRECONDITION(diagnostic != NULL, "diagnostic must not be NULL");

  return nexus_data_list_length_get(diagnostic->notes);
}

const char *pharos_diagnostics_note_at(const PharosDiagnostic *diagnostic, uint_large index) {
  NEXUS_PROGRAM_PRECONDITION(diagnostic != NULL && index < nexus_data_list_length_get(diagnostic->notes), "note index must be in bounds");

  return (const char *)nexus_data_list_pointer_at(diagnostic->notes, index);
}

uint_large pharos_diagnostics_help_count_get(const PharosDiagnostic *diagnostic) {
  NEXUS_PROGRAM_PRECONDITION(diagnostic != NULL, "diagnostic must not be NULL");

  return nexus_data_list_length_get(diagnostic->helps);
}

const char *pharos_diagnostics_help_at(const PharosDiagnostic *diagnostic, uint_large index) {
  NEXUS_PROGRAM_PRECONDITION(diagnostic != NULL && index < nexus_data_list_length_get(diagnostic->helps), "help index must be in bounds");

  return (const char *)nexus_data_list_pointer_at(diagnostic->helps, index);
}

/* Summary: the 0-based line that holds offset, by binary search over the source's line starts. */
static uint_large p_diagnostics_diagnostic_line_of(const PharosSource *source, uint_large offset) {
  uint_large low;
  uint_large high;
  uint_large middle;

  low  = 0;
  high = nexus_data_list_length_get(source->line_starts);

  while (high - low > 1) {
    middle = low + ((high - low) / 2);

    if (*(const uint_large *)nexus_data_list_at_const(source->line_starts, middle) <= offset) {
      low = middle;
    } else {
      high = middle;
    }
  }

  return low;
}

/* Summary: the 1-based character column of an offset within its line, counting UTF-8 characters. */
static uint_large p_diagnostics_diagnostic_column_of(const PharosSource *source, uint_large line, uint_large offset) {
  uint_large start;
  uint_large column;
  uint_large i;

  start  = *(const uint_large *)nexus_data_list_at_const(source->line_starts, line);
  column = 1;

  for (i = start; i < offset && i < source->size; i++) {
    if ((source->text[i] & 0xC0) != 0x80) {
      column++;
    }
  }

  return column;
}

const char *pharos_diagnostics_source_name_get(const PharosDiagnostic *diagnostic) {
  NEXUS_PROGRAM_PRECONDITION(diagnostic != NULL, "diagnostic must not be NULL");

  return diagnostic->source_name;
}

void pharos_diagnostics_span_get(const PharosDiagnostic *diagnostic, uint_large *out_offset, uint_large *out_width, uint_large *out_line,
                                 uint_large *out_column) {
  NEXUS_PROGRAM_PRECONDITION(diagnostic != NULL && out_offset != NULL && out_width != NULL && out_line != NULL && out_column != NULL,
                             "span arguments must not be NULL");

  *out_offset = diagnostic->span.offset;
  *out_width  = diagnostic->span.width;
  *out_line   = diagnostic->line;
  *out_column = diagnostic->column;
}

/* ---------------------------------------------------------------------------- */
/* RENDERING                                                                     */
/* ---------------------------------------------------------------------------- */

typedef struct PharosBuffer {
  byte      *data;
  uint_large size;
  uint_large capacity;
} PharosBuffer;

static void p_diagnostics_buffer_append(PharosBuffer *buffer, const char *text, uint_large size) {
  if (buffer->size + size + 1 > buffer->capacity) {
    buffer->capacity = (buffer->capacity * 2) + size + 256;
    buffer->data     = (byte *)nexus_memory_reallocate(buffer->data, buffer->capacity);
  }

  nexus_memory_bytes_copy(text, buffer->data + buffer->size, size);
  buffer->size += size;
  buffer->data[buffer->size] = '\0';
}

static void p_diagnostics_buffer_text(PharosBuffer *buffer, const char *text) {
  p_diagnostics_buffer_append(buffer, text, nexus_strings_string_length_get(text));
}

static void p_diagnostics_buffer_repeat(PharosBuffer *buffer, char character, uint_large count) {
  uint_large i;

  for (i = 0; i < count; i++) {
    p_diagnostics_buffer_append(buffer, &character, 1);
  }
}

static void p_diagnostics_buffer_number(PharosBuffer *buffer, uint_large value, uint_large width) {
  char       digits[24];
  uint_large count;
  uint_large i;

  count = 0;

  do {
    digits[count++] = (char)('0' + (value % 10));
    value /= 10;
  } while (value > 0);

  for (i = count; i < width; i++) {
    p_diagnostics_buffer_append(buffer, " ", 1);
  }

  for (i = count; i > 0; i--) {
    p_diagnostics_buffer_append(buffer, &digits[i - 1], 1);
  }
}

static uint_large p_diagnostics_digits(uint_large value) {
  uint_large count;

  count = 1;

  while (value >= 10) {
    value /= 10;
    count++;
  }

  return count;
}

static void p_diagnostics_render_diagnostic(const PharosDiagnostics *diagnostics, const PharosDiagnostic *diagnostic, boolean color,
                                            PharosBuffer *out) {
  const PharosSource *source;
  const PharosLabel  *label;
  const PharosLabel  *other;
  uint_large          line;
  uint_large          column;
  uint_large          gutter;
  uint_large          i;
  uint_large          j;
  uint_large          line_start;
  uint_large          line_end;
  uint_large          mark_column;
  uint_large          mark_width;
  uint_large         *order;
  uint_large          count;
  const char         *severity_color;
  const char         *reset;

  reset          = color ? "\033[0m" : "";
  severity_color = "";

  if (color) {
    severity_color = diagnostic->severity == PDS_ERROR ? "\033[1;31m" : "\033[1;33m";
  }

  p_diagnostics_buffer_text(out, severity_color);
  p_diagnostics_buffer_text(out, diagnostic->severity == PDS_ERROR ? "error" : "warning");
  p_diagnostics_buffer_text(out, "[");
  p_diagnostics_buffer_text(out, diagnostic->code);
  p_diagnostics_buffer_text(out, "]");
  p_diagnostics_buffer_text(out, reset);
  p_diagnostics_buffer_text(out, color ? "\033[1m: " : ": ");
  p_diagnostics_buffer_text(out, diagnostic->message);
  p_diagnostics_buffer_text(out, reset);
  p_diagnostics_buffer_text(out, "\n");

  source = pharos_diagnostics_source_get(diagnostics, diagnostic->span.source);
  gutter = 1;

  for (i = 0; i < nexus_data_list_length_get(diagnostic->labels); i++) {
    label = (const PharosLabel *)nexus_data_list_at_const(diagnostic->labels, i);

    if (label->span.source == diagnostic->span.source) {
      line = p_diagnostics_diagnostic_line_of(source, label->span.offset) + 1;

      if (p_diagnostics_digits(line) > gutter) {
        gutter = p_diagnostics_digits(line);
      }
    }
  }

  line   = p_diagnostics_diagnostic_line_of(source, diagnostic->span.offset);
  column = p_diagnostics_diagnostic_column_of(source, line, diagnostic->span.offset);

  p_diagnostics_buffer_repeat(out, ' ', gutter);
  p_diagnostics_buffer_text(out, color ? "\033[1;34m--> \033[0m" : "--> ");
  p_diagnostics_buffer_text(out, source->name);
  p_diagnostics_buffer_text(out, ":");
  p_diagnostics_buffer_number(out, line + 1, 0);
  p_diagnostics_buffer_text(out, ":");
  p_diagnostics_buffer_number(out, column, 0);
  p_diagnostics_buffer_text(out, "\n");

  p_diagnostics_buffer_repeat(out, ' ', gutter + 1);
  p_diagnostics_buffer_text(out, "|\n");

  /* Labels are shown in source order, each with its own underline row. */
  order = (uint_large *)nexus_memory_allocate((nexus_data_list_length_get(diagnostic->labels) + 1) * NEXUS_SIZEOF(uint_large));
  count = 0;

  for (i = 0; i < nexus_data_list_length_get(diagnostic->labels); i++) {
    other = (const PharosLabel *)nexus_data_list_at_const(diagnostic->labels, i);

    if (other->span.source != diagnostic->span.source) {
      continue;
    }

    for (j = count; j > 0 && ((const PharosLabel *)nexus_data_list_at_const(diagnostic->labels, order[j - 1]))->span.offset > other->span.offset;
         j--) {
      order[j] = order[j - 1];
    }

    order[j] = i;
    count++;
  }

  for (i = 0; i < count; i++) {
    uint_large best;

    best = order[i];

    label      = (const PharosLabel *)nexus_data_list_at_const(diagnostic->labels, best);
    line       = p_diagnostics_diagnostic_line_of(source, label->span.offset);
    line_start = *(const uint_large *)nexus_data_list_at_const(source->line_starts, line);
    line_end   = line_start;

    while (line_end < source->size && source->text[line_end] != '\n' && source->text[line_end] != '\r') {
      line_end++;
    }

    p_diagnostics_buffer_number(out, line + 1, gutter);
    p_diagnostics_buffer_text(out, " | ");

    for (j = line_start; j < line_end; j++) {
      char character;

      character = ' ';

      if (source->text[j] != '\t') {
        character = (char)source->text[j];
      }

      p_diagnostics_buffer_append(out, &character, 1);
    }

    p_diagnostics_buffer_text(out, "\n");
    p_diagnostics_buffer_repeat(out, ' ', gutter + 1);
    p_diagnostics_buffer_text(out, "| ");

    mark_column = p_diagnostics_diagnostic_column_of(source, line, label->span.offset) - 1;
    mark_width  = label->span.width;

    if (label->span.offset + mark_width > line_end) {
      mark_width = line_end > label->span.offset ? line_end - label->span.offset : 1;
    }

    mark_width = p_diagnostics_diagnostic_column_of(source, line, label->span.offset + mark_width) - 1 - mark_column;

    if (mark_width == 0) {
      mark_width = 1;
    }

    p_diagnostics_buffer_repeat(out, ' ', mark_column);
    if (label->primary) {
      p_diagnostics_buffer_text(out, severity_color);
    } else if (color) {
      p_diagnostics_buffer_text(out, "\033[1;34m");
    }

    p_diagnostics_buffer_repeat(out, label->primary ? '^' : '-', mark_width);

    if (label->text != NULL) {
      p_diagnostics_buffer_text(out, " ");
      p_diagnostics_buffer_text(out, label->text);
    }

    p_diagnostics_buffer_text(out, reset);
    p_diagnostics_buffer_text(out, "\n");
  }

  nexus_memory_deallocate(order);

  if (nexus_data_list_length_get(diagnostic->notes) > 0 || nexus_data_list_length_get(diagnostic->helps) > 0) {
    p_diagnostics_buffer_repeat(out, ' ', gutter + 1);
    p_diagnostics_buffer_text(out, "|\n");
  }

  for (i = 0; i < nexus_data_list_length_get(diagnostic->notes); i++) {
    p_diagnostics_buffer_repeat(out, ' ', gutter + 1);
    p_diagnostics_buffer_text(out, color ? "= \033[1mnote\033[0m: " : "= note: ");
    p_diagnostics_buffer_text(out, (const char *)nexus_data_list_pointer_at(diagnostic->notes, i));
    p_diagnostics_buffer_text(out, "\n");
  }

  for (i = 0; i < nexus_data_list_length_get(diagnostic->helps); i++) {
    p_diagnostics_buffer_repeat(out, ' ', gutter + 1);
    p_diagnostics_buffer_text(out, color ? "= \033[1;36mhelp\033[0m: " : "= help: ");
    p_diagnostics_buffer_text(out, (const char *)nexus_data_list_pointer_at(diagnostic->helps, i));
    p_diagnostics_buffer_text(out, "\n");
  }

  p_diagnostics_buffer_text(out, "\n");
}

char *pharos_diagnostics_render(const PharosDiagnostics *diagnostics, boolean color) {
  PharosBuffer buffer;
  uint_large   errors;
  uint_large   warnings;
  uint_large   i;

  NEXUS_PROGRAM_PRECONDITION(diagnostics != NULL, "diagnostics must not be NULL");

  buffer.capacity = 1024;
  buffer.size     = 0;
  buffer.data     = (byte *)nexus_memory_allocate(buffer.capacity);
  buffer.data[0]  = '\0';
  errors          = 0;
  warnings        = 0;

  for (i = 0; i < nexus_data_list_length_get(diagnostics->diagnostics); i++) {
    const PharosDiagnostic *diagnostic;

    diagnostic = (const PharosDiagnostic *)nexus_data_list_pointer_at(diagnostics->diagnostics, i);
    p_diagnostics_render_diagnostic(diagnostics, diagnostic, color, &buffer);

    if (diagnostic->severity == PDS_ERROR) {
      errors++;
    } else {
      warnings++;
    }
  }

  if (errors > 0) {
    p_diagnostics_buffer_text(&buffer, color ? "\033[1;31merror\033[0m: could not compile: " : "error: could not compile: ");
    p_diagnostics_buffer_number(&buffer, errors, 0);
    p_diagnostics_buffer_text(&buffer, errors == 1 ? " error" : " errors");

    if (warnings > 0) {
      p_diagnostics_buffer_text(&buffer, " and ");
      p_diagnostics_buffer_number(&buffer, warnings, 0);
      p_diagnostics_buffer_text(&buffer, warnings == 1 ? " warning" : " warnings");
    }

    p_diagnostics_buffer_text(&buffer, "\n");
  } else if (warnings > 0) {
    p_diagnostics_buffer_text(&buffer, color ? "\033[1;33mwarning\033[0m: " : "warning: ");
    p_diagnostics_buffer_number(&buffer, warnings, 0);
    p_diagnostics_buffer_text(&buffer, warnings == 1 ? " warning emitted\n" : " warnings emitted\n");
  }

  return (char *)buffer.data;
}

/* ---------------------------------------------------------------------------- */
/* SOURCES                                                                       */
/* ---------------------------------------------------------------------------- */

PharosDiagnostics *pharos_diagnostics_create(void) {
  PharosDiagnostics *diagnostics;

  diagnostics              = (PharosDiagnostics *)nexus_memory_allocate(NEXUS_SIZEOF(*diagnostics));
  diagnostics->sources     = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 8);
  diagnostics->diagnostics = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 8);

  return diagnostics;
}

void pharos_diagnostics_destroy(PharosDiagnostics *diagnostics) {
  uint_large i;

  pharos_diagnostics_clear(diagnostics);

  for (i = 0; i < nexus_data_list_length_get(diagnostics->sources); i++) {
    PharosSource *source;

    source = (PharosSource *)nexus_data_list_pointer_at(diagnostics->sources, i);
    nexus_memory_deallocate(source->name);
    nexus_memory_deallocate(source->text);
    nexus_data_list_destroy(source->line_starts);
    nexus_memory_deallocate(source);
  }

  nexus_data_list_destroy(diagnostics->sources);
  nexus_data_list_destroy(diagnostics->diagnostics);
  nexus_memory_deallocate(diagnostics);
}

uint32 pharos_diagnostics_source_add(PharosDiagnostics *diagnostics, const char *name, const byte *text, uint_large size) {
  PharosSource *source;
  uint_large    i;
  uint_large    start;

  source              = (PharosSource *)nexus_memory_allocate(NEXUS_SIZEOF(*source));
  source->name        = pharos_paths_text_copy(name);
  source->text        = (byte *)nexus_memory_allocate(size + 1);
  source->size        = size;
  source->line_starts = nexus_data_list_create(NEXUS_SIZEOF(uint_large), 64);

  if (size > 0) {
    nexus_memory_bytes_copy(text, source->text, size);
  }

  source->text[size] = '\0';
  start              = 0;
  (void)nexus_data_list_append(source->line_starts, &start);

  for (i = 0; i < size; i++) {
    if (source->text[i] == '\n') {
      start = i + 1;
      (void)nexus_data_list_append(source->line_starts, &start);
    }
  }

  (void)nexus_data_list_append_pointer(diagnostics->sources, source);

  return (uint32)(nexus_data_list_length_get(diagnostics->sources) - 1);
}

const PharosSource *pharos_diagnostics_source_get(const PharosDiagnostics *diagnostics, uint32 source) {
  NEXUS_PROGRAM_PRECONDITION(diagnostics != NULL && source < nexus_data_list_length_get(diagnostics->sources), "source index must be in bounds");

  return (const PharosSource *)nexus_data_list_pointer_at(diagnostics->sources, source);
}

PharosDiagnostic *pharos_diagnostics_last_get(PharosDiagnostics *diagnostics) {
  NEXUS_PROGRAM_PRECONDITION(diagnostics != NULL, "diagnostics must not be NULL");

  if (nexus_data_list_length_get(diagnostics->diagnostics) == 0) {
    return NULL;
  }

  return (PharosDiagnostic *)nexus_data_list_pointer_at(diagnostics->diagnostics, nexus_data_list_length_get(diagnostics->diagnostics) - 1);
}
