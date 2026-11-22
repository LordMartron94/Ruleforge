#include <nexus.h>

#include "../echo.h"
#include "e_internal.h"

#include <stdio.h>

typedef struct EchoTerminalState {
  boolean                       rich;
  NexusMutex                   *mutex;
  NexusTimePrecision            precision;
  EchoRichTerminalConfiguration configuration;
} EchoTerminalState;

typedef struct EchoFileState {
  NexusFileHandle   *file;
  NexusMutex        *mutex;
  NexusTimePrecision precision;
  uint16             unflushed_count;
  NexusPath          path;
} EchoFileState;

/*
Summary: appends one formatted piece to a bounded presentation buffer. Returns FALSE (never an
  error) once the buffer is exhausted -- every call site already treats overflow as best-effort
  truncation, not a failure, matching the field/span truncation policy elsewhere in Echo.
*/
static boolean e_sink_append(char **cursor, uint_large *remaining, const char *format, ...) {
  NexusStringFormatResult result;
  va_list                 args;
  boolean                 fit;

  NEXUS_PROGRAM_CONTEXT_PUSH("e_sink_append");

  if (*remaining == 0) {
    NEXUS_PROGRAM_CONTEXT_POP();
    return FALSE;
  }

  va_start(args, format);
  result = nexus_strings_vstring_format_with_truncation(*cursor, *remaining, format, args);
  va_end(args);
  *cursor += result.written_length;
  *remaining = result.written_length < *remaining ? *remaining - result.written_length : 0;
  fit        = result.truncated == FALSE;

  NEXUS_PROGRAM_CONTEXT_POP();
  return fit;
}

static void e_sink_color(char **cursor, uint_large *remaining, EchoColor color) {
  if (color.mode == ETCM_ANSI) {
    (void)e_sink_append(cursor, remaining, "\x1b[%um", color.ansi_code);
  } else if (color.mode == ETCM_RGB) {
    (void)e_sink_append(cursor, remaining, "\x1b[38;2;%u;%u;%um", color.red, color.green, color.blue);
  }
}

static void e_sink_append_timestamp(char **cursor, uint_large *remaining, NexusDateTime date_time, NexusTimePrecision precision) {
  (void)e_sink_append(cursor, remaining, "[%04d-%02u-%02u %02u:%02u:%02u", date_time.year, date_time.month, date_time.day, date_time.hour,
                      date_time.minute, date_time.second);
  if (precision == NTP_MILLI_SECOND)
    (void)e_sink_append(cursor, remaining, ".%03u", date_time.nanosecond / 1000000);
  else if (precision == NTP_MICRO_SECOND)
    (void)e_sink_append(cursor, remaining, ".%06u", date_time.nanosecond / 1000);
  else if (precision == NTP_NANO_SECOND)
    (void)e_sink_append(cursor, remaining, ".%09u", date_time.nanosecond);
  (void)e_sink_append(cursor, remaining, "] ");
}

static void e_sink_append_record(char *buffer, uint_large capacity, const EchoRecord *record, EchoTerminalState *terminal) {
  char         *cursor;
  uint_large    remaining;
  NexusDateTime date_time;
  uint8         i;
  EchoColor     color;

  cursor    = buffer;
  remaining = capacity;
  date_time = nexus_time_to_local_datetime(record->timestamp);

  if (terminal->rich) {
    color = terminal->configuration.color_time;
    e_sink_color(&cursor, &remaining, color);
  }

  e_sink_append_timestamp(&cursor, &remaining, date_time, terminal->precision);

  if (terminal->precision == NTP_SECOND)
    (void)e_sink_append(&cursor, &remaining, "          ");
  else if (terminal->precision == NTP_MILLI_SECOND)
    (void)e_sink_append(&cursor, &remaining, "      ");
  else if (terminal->precision == NTP_MICRO_SECOND)
    (void)e_sink_append(&cursor, &remaining, "   ");

  if (terminal->rich) {
    (void)e_sink_append(&cursor, &remaining, "\x1b[0m");
    color = terminal->configuration.color_levels[record->level];
    e_sink_color(&cursor, &remaining, color);
  }

  (void)e_sink_append(&cursor, &remaining, "[%-8s] ", echo_log_level_label_get(record->level));

  if (terminal->rich) {
    (void)e_sink_append(&cursor, &remaining, "\x1b[0m");
    e_sink_color(&cursor, &remaining, terminal->configuration.color_system);
  }

  (void)e_sink_append(&cursor, &remaining, "[%-*.*s] ", (int)ECHO_SYSTEM_LABEL_LENGTH_MAX, (int)ECHO_SYSTEM_LABEL_LENGTH_MAX, record->system_label);

  if (terminal->rich) {
    (void)e_sink_append(&cursor, &remaining, "\x1b[0m");
    e_sink_color(&cursor, &remaining, terminal->configuration.color_file);
  }

  (void)e_sink_append(&cursor, &remaining, "[%s:%u] ", record->origin_file, record->origin_line);

  if (terminal->rich)
    (void)e_sink_append(&cursor, &remaining, "\x1b[0m");

  if (record->span_count != 0) {
    (void)e_sink_append(&cursor, &remaining, "[");

    for (i = 0; i < record->span_count; i++) {
      if (i != 0)
        (void)e_sink_append(&cursor, &remaining, " > ");

      (void)e_sink_append(&cursor, &remaining, "%s", record->spans[i]);
    }

    (void)e_sink_append(&cursor, &remaining, "] ");
  }

  if (record->field_count != 0) {
    (void)e_sink_append(&cursor, &remaining, "{");

    for (i = 0; i < record->field_count; i++) {
      if (i != 0)
        (void)e_sink_append(&cursor, &remaining, ", ");

      if (record->fields[i].type == EFT_STRING)
        (void)e_sink_append(&cursor, &remaining, "%s=\"%s\"", record->fields[i].key, record->fields[i].value.string_value);
      else if (record->fields[i].type == EFT_INT64)
        (void)e_sink_append(&cursor, &remaining, "%s=%lld", record->fields[i].key, (long long)record->fields[i].value.int64_value);
      else if (record->fields[i].type == EFT_UINT64)
        (void)e_sink_append(&cursor, &remaining, "%s=%llu", record->fields[i].key, (unsigned long long)record->fields[i].value.uint64_value);
      else if (record->fields[i].type == EFT_FLOAT)
        (void)e_sink_append(&cursor, &remaining, "%s=%f", record->fields[i].key, (double)record->fields[i].value.float_value);
      else
        (void)e_sink_append(&cursor, &remaining, "%s=%s", record->fields[i].key, record->fields[i].value.boolean_value != FALSE ? "true" : "false");
    }

    (void)e_sink_append(&cursor, &remaining, "} ");
  }

  (void)e_sink_append(&cursor, &remaining, "- %s\n", record->message);
}

static NexusError *e_sink_create(const EchoSinkCallbacks *callbacks, uint32 kind, EchoSink **out_sink) {
  EchoSink   *sink;
  NexusError *error;

  NEXUS_PROGRAM_CONTEXT_PUSH("e_sink_create");
  NEXUS_PROGRAM_PRECONDITION(out_sink != NULL && callbacks != NULL && callbacks->submit != NULL,
                             "e_sink_create: out_sink and callbacks->submit must be non-null");

  *out_sink = NULL;
  sink      = (EchoSink *)nexus_memory_allocate(NEXUS_SIZEOF(*sink));
  nexus_memory_bytes_clear(sink, NEXUS_SIZEOF(*sink));
  sink->callbacks = *callbacks;
  sink->kind      = kind;
  sink->precision = NTP_MILLI_SECOND;
  nexus_threads_atomic_uint32_store(&sink->enabled, 1);
  error = nexus_threads_mutex_create(&sink->configuration_mutex);
  if (error != NEXUS_PROGRAM_ERROR_SUCCESS()) {
    nexus_memory_deallocate(sink);
    NEXUS_PROGRAM_CONTEXT_POP();
    return error;
  }

  *out_sink = sink;

  NEXUS_PROGRAM_CONTEXT_POP();
  return NEXUS_PROGRAM_ERROR_SUCCESS();
}

NexusError *echo_runtime_sink_register(EchoRuntime *runtime, EchoSink *sink) {
  NexusError *error;

  NEXUS_PROGRAM_CONTEXT_PUSH("echo_runtime_sink_register");
  NEXUS_PROGRAM_PRECONDITION(runtime != NULL && sink != NULL, "echo_runtime_sink_register: runtime and sink must be non-null");

  error = e_runtime_begin_operation(runtime, FALSE);
  if (error != NEXUS_PROGRAM_ERROR_SUCCESS()) {
    NEXUS_PROGRAM_CONTEXT_POP();
    return error;
  }

  ECHO_INTERNAL_LOCK(runtime->state_mutex);
  if (runtime->started != FALSE || runtime->closing != FALSE || sink->registered != FALSE || sink->runtime != NULL) {
    ECHO_INTERNAL_UNLOCK(runtime->state_mutex);
    e_runtime_end_operation(runtime);
    NEXUS_PROGRAM_CONTEXT_POP();
    return e_echo_error(GLOBAL_ECHO_ERROR_RUNTIME_STATE, "Sink registration is closed or sink is already owned");
  }
  if (runtime->sink_count == runtime->sink_capacity) {
    error = e_runtime_capacity_grow((void **)&runtime->sinks, &runtime->sink_capacity, NEXUS_SIZEOF(*runtime->sinks));
    if (error != NEXUS_PROGRAM_ERROR_SUCCESS()) {
      ECHO_INTERNAL_UNLOCK(runtime->state_mutex);
      e_runtime_end_operation(runtime);
      NEXUS_PROGRAM_CONTEXT_POP();
      return error;
    }
  }
  sink->runtime                         = runtime;
  sink->registered                      = TRUE;
  runtime->sinks[runtime->sink_count++] = sink;
  ECHO_INTERNAL_UNLOCK(runtime->state_mutex);
  e_runtime_end_operation(runtime);

  NEXUS_PROGRAM_CONTEXT_POP();
  return NEXUS_PROGRAM_ERROR_SUCCESS();
}

NexusError *echo_sink_enabled_set(EchoSink *sink, boolean enabled) {
  NexusError *error;

  NEXUS_PROGRAM_CONTEXT_PUSH("echo_sink_enabled_set");
  NEXUS_PROGRAM_PRECONDITION(sink != NULL && sink->runtime != NULL, "echo_sink_enabled_set: sink must be non-null and registered");

  error = e_runtime_begin_operation(sink->runtime, TRUE);
  if (error != NEXUS_PROGRAM_ERROR_SUCCESS()) {
    NEXUS_PROGRAM_CONTEXT_POP();
    return error;
  }

  nexus_threads_atomic_uint32_store(&sink->enabled, enabled != FALSE ? 1 : 0);
  e_runtime_end_operation(sink->runtime);

  NEXUS_PROGRAM_CONTEXT_POP();
  return NEXUS_PROGRAM_ERROR_SUCCESS();
}

boolean echo_sink_enabled_get(EchoSink *sink) {
  boolean enabled;
  NEXUS_PROGRAM_CONTEXT_PUSH("echo_sink_enabled_get");
  enabled = sink != NULL && nexus_threads_atomic_uint32_load(&sink->enabled) != 0;
  NEXUS_PROGRAM_CONTEXT_POP();
  return enabled;
}

void e_sink_destroy_internal(EchoSink *sink) {
  if (sink == NULL)
    return;

  if (sink->callbacks.destroy != NULL)
    sink->callbacks.destroy(sink->callbacks.user_data);
  if (sink->configuration_mutex != NULL)
    nexus_threads_mutex_destroy(sink->configuration_mutex);
  nexus_memory_deallocate(sink);
}

void echo_sink_destroy(EchoSink *sink) {
  NEXUS_PROGRAM_CONTEXT_PUSH("echo_sink_destroy");

  if (sink == NULL) {
    NEXUS_PROGRAM_CONTEXT_POP();
    return;
  }

  NEXUS_PROGRAM_ASSERT(sink->registered == FALSE, "Cannot destroy a registered Echo sink directly");
  e_sink_destroy_internal(sink);

  NEXUS_PROGRAM_CONTEXT_POP();
}

boolean echo_sink_is_valid(EchoSink *sink) {
  boolean valid;
  NEXUS_PROGRAM_CONTEXT_PUSH("echo_sink_is_valid");
  valid = sink != NULL && sink->callbacks.submit != NULL;
  NEXUS_PROGRAM_CONTEXT_POP();
  return valid;
}

static NexusError *e_terminal_submit(const EchoRecord *record, void *user_data) {
  EchoTerminalState *state = (EchoTerminalState *)user_data;
  char               buffer[ECHO_MESSAGE_LENGTH_MAX + ECHO_SYSTEM_LABEL_LENGTH_MAX + ECHO_ORIGIN_FILE_LENGTH_MAX + 2048];

  NEXUS_PROGRAM_CONTEXT_PUSH("e_terminal_submit");

  if (state == NULL || record == NULL) {
    NEXUS_PROGRAM_CONTEXT_POP();
    return e_echo_error(GLOBAL_ECHO_ERROR_SINK_INVALID, "Terminal sink state is invalid");
  }

  ECHO_INTERNAL_LOCK(state->mutex);
  e_sink_append_record(buffer, sizeof(buffer), record, state);
  (void)fputs(buffer, stdout);
  (void)fflush(stdout);
  ECHO_INTERNAL_UNLOCK(state->mutex);

  NEXUS_PROGRAM_CONTEXT_POP();
  return NEXUS_PROGRAM_ERROR_SUCCESS();
}

static void e_terminal_state_destroy(void *user_data) {
  EchoTerminalState *state = (EchoTerminalState *)user_data;

  if (state == NULL)
    return;

  nexus_threads_mutex_destroy(state->mutex);
  nexus_memory_deallocate(state);
}

static NexusError *e_file_submit(const EchoRecord *record, void *user_data) {
  EchoFileState    *state = (EchoFileState *)user_data;
  EchoTerminalState terminal;
  char              buffer[ECHO_MESSAGE_LENGTH_MAX + ECHO_SYSTEM_LABEL_LENGTH_MAX + ECHO_ORIGIN_FILE_LENGTH_MAX + 2048];
  uint_large        length;
  uint_large        written;
  NexusError       *error;

  NEXUS_PROGRAM_CONTEXT_PUSH("e_file_submit");

  if (state == NULL || record == NULL) {
    NEXUS_PROGRAM_CONTEXT_POP();
    return e_echo_error(GLOBAL_ECHO_ERROR_SINK_INVALID, "File sink state is invalid");
  }

  terminal.rich          = FALSE;
  terminal.mutex         = NULL;
  terminal.configuration = echo_rich_terminal_configuration_plain_create(state->precision);
  terminal.precision     = state->precision;

  ECHO_INTERNAL_LOCK(state->mutex);
  e_sink_append_record(buffer, sizeof(buffer), record, &terminal);
  length = nexus_strings_string_length_get(buffer);
  error  = nexus_filesystem_file_write(state->file, (const byte *)buffer, length, &written);
  if (error == NEXUS_PROGRAM_ERROR_SUCCESS() && written == length) {
    state->unflushed_count++;
    if (state->unflushed_count >= ECHO_FILE_FLUSH_THRESHOLD) {
      error = nexus_filesystem_file_flush(state->file);
      if (error == NEXUS_PROGRAM_ERROR_SUCCESS())
        state->unflushed_count = 0;
    }
  }
  ECHO_INTERNAL_UNLOCK(state->mutex);

  NEXUS_PROGRAM_CONTEXT_POP();
  return error;
}

static NexusError *e_file_flush_internal(void *user_data) {
  EchoFileState *state = (EchoFileState *)user_data;
  NexusError    *error;

  NEXUS_PROGRAM_CONTEXT_PUSH("e_file_flush_internal");

  if (state == NULL) {
    NEXUS_PROGRAM_CONTEXT_POP();
    return e_echo_error(GLOBAL_ECHO_ERROR_SINK_INVALID, "File sink state is invalid");
  }

  ECHO_INTERNAL_LOCK(state->mutex);
  error = nexus_filesystem_file_flush(state->file);
  if (error == NEXUS_PROGRAM_ERROR_SUCCESS())
    state->unflushed_count = 0;
  ECHO_INTERNAL_UNLOCK(state->mutex);

  NEXUS_PROGRAM_CONTEXT_POP();
  return error;
}

static void e_file_state_destroy(void *user_data) {
  EchoFileState *state = (EchoFileState *)user_data;

  if (state == NULL)
    return;

  (void)e_file_flush_internal(state);
  (void)nexus_filesystem_file_close(state->file);
  nexus_threads_mutex_destroy(state->mutex);
  nexus_memory_deallocate(state);
}

NexusError *echo_sink_custom_create(const EchoCustomSinkConfiguration *configuration, EchoSink **out_sink) {
  EchoSinkCallbacks callbacks;
  NexusError       *error;

  NEXUS_PROGRAM_CONTEXT_PUSH("echo_sink_custom_create");
  NEXUS_PROGRAM_PRECONDITION(configuration != NULL, "echo_sink_custom_create: configuration must be non-null");

  callbacks.filter           = configuration->filter;
  callbacks.filter_user_data = configuration->filter_user_data;
  callbacks.submit           = configuration->submit;
  callbacks.flush            = configuration->flush;
  callbacks.destroy          = configuration->destroy;
  callbacks.user_data        = configuration->user_data;

  error = e_sink_create(&callbacks, 1, out_sink);
  NEXUS_PROGRAM_CONTEXT_POP();
  return error;
}

static NexusError *e_terminal_create(boolean rich, EchoFilterFunction *filter, void *filter_data, NexusTimePrecision precision,
                                     EchoRichTerminalConfiguration configuration, EchoSink **out_sink) {
  EchoTerminalState *state;
  EchoSinkCallbacks  callbacks;
  NexusError        *error;

  NEXUS_PROGRAM_CONTEXT_PUSH("e_terminal_create");

  state = (EchoTerminalState *)nexus_memory_allocate(NEXUS_SIZEOF(*state));
  nexus_memory_bytes_clear(state, NEXUS_SIZEOF(*state));
  state->rich          = rich;
  state->precision     = precision;
  state->configuration = configuration;
  error                = nexus_threads_mutex_create(&state->mutex);
  if (error != NEXUS_PROGRAM_ERROR_SUCCESS()) {
    nexus_memory_deallocate(state);
    NEXUS_PROGRAM_CONTEXT_POP();
    return error;
  }

  callbacks.filter           = filter;
  callbacks.filter_user_data = filter_data;
  callbacks.submit           = e_terminal_submit;
  callbacks.flush            = NULL;
  callbacks.destroy          = e_terminal_state_destroy;
  callbacks.user_data        = state;
  error                      = e_sink_create(&callbacks, 2, out_sink);
  if (error != NEXUS_PROGRAM_ERROR_SUCCESS()) {
    e_terminal_state_destroy(state);
    NEXUS_PROGRAM_CONTEXT_POP();
    return error;
  }

  (*out_sink)->precision                   = precision;
  (*out_sink)->rich_terminal_configuration = configuration;

  NEXUS_PROGRAM_CONTEXT_POP();
  return NEXUS_PROGRAM_ERROR_SUCCESS();
}

NexusError *echo_sink_simple_terminal_create(EchoFilterFunction *filter, void *filter_user_data, NexusTimePrecision precision, EchoSink **out_sink) {
  EchoRichTerminalConfiguration configuration;
  NexusError                   *error;
  NEXUS_PROGRAM_CONTEXT_PUSH("echo_sink_simple_terminal_create");
  configuration = echo_rich_terminal_configuration_plain_create(precision);
  error         = e_terminal_create(FALSE, filter, filter_user_data, precision, configuration, out_sink);
  NEXUS_PROGRAM_CONTEXT_POP();
  return error;
}

NexusError *echo_sink_rich_terminal_create(EchoFilterFunction *filter, void *filter_user_data, EchoRichTerminalConfiguration configuration,
                                           EchoSink **out_sink) {
  NexusError *error;
  NEXUS_PROGRAM_CONTEXT_PUSH("echo_sink_rich_terminal_create");
  error = e_terminal_create(TRUE, filter, filter_user_data, configuration.display_precision, configuration, out_sink);
  NEXUS_PROGRAM_CONTEXT_POP();
  return error;
}

NexusError *echo_sink_simple_file_create(EchoFilterFunction *filter, void *filter_user_data, NexusTimePrecision precision, const NexusPath *path,
                                         EchoSink **out_sink) {
  EchoFileState    *state;
  EchoSinkCallbacks callbacks;
  NexusError       *error;

  NEXUS_PROGRAM_CONTEXT_PUSH("echo_sink_simple_file_create");
  NEXUS_PROGRAM_PRECONDITION(path != NULL, "echo_sink_simple_file_create: path must be non-null");

  state = (EchoFileState *)nexus_memory_allocate(NEXUS_SIZEOF(*state));
  nexus_memory_bytes_clear(state, NEXUS_SIZEOF(*state));
  error = nexus_filesystem_file_open(path, NFM_APPEND, &state->file);
  if (error != NEXUS_PROGRAM_ERROR_SUCCESS()) {
    nexus_memory_deallocate(state);
    NEXUS_PROGRAM_CONTEXT_POP();
    return error;
  }
  state->path      = *path;
  state->precision = precision;
  error            = nexus_threads_mutex_create(&state->mutex);
  if (error != NEXUS_PROGRAM_ERROR_SUCCESS()) {
    (void)nexus_filesystem_file_close(state->file);
    nexus_memory_deallocate(state);
    NEXUS_PROGRAM_CONTEXT_POP();
    return error;
  }

  callbacks.filter           = filter;
  callbacks.filter_user_data = filter_user_data;
  callbacks.submit           = e_file_submit;
  callbacks.flush            = e_file_flush_internal;
  callbacks.destroy          = e_file_state_destroy;
  callbacks.user_data        = state;
  error                      = e_sink_create(&callbacks, 3, out_sink);
  if (error != NEXUS_PROGRAM_ERROR_SUCCESS()) {
    e_file_state_destroy(state);
    NEXUS_PROGRAM_CONTEXT_POP();
    return error;
  }

  NEXUS_PROGRAM_CONTEXT_POP();
  return NEXUS_PROGRAM_ERROR_SUCCESS();
}

static boolean e_rich_file_matches(const NexusPath *path, NexusPathType type, const void *user_data) {
  const char *prefix = (const char *)user_data;

  (void)type;

  return e_string_contains(path->buffer, prefix);
}

static void e_rich_file_paths_sort(NexusPath *paths, uint64 count) {
  uint64    i;
  uint64    j;
  NexusPath temporary;

  for (i = 1; i < count; i++) {
    temporary = paths[i];
    j         = i;
    while (j > 0 && e_string_compare(paths[j - 1].buffer, temporary.buffer) > 0) {
      paths[j] = paths[j - 1];
      j--;
    }
    paths[j] = temporary;
  }
}

NexusError *echo_sink_rich_file_create(EchoRichFileConfiguration configuration, EchoSink **out_sink) {
  NexusPathList     paths;
  NexusDateTime     now;
  NexusPath         path;
  char              filename[256];
  NexusError       *error;
  uint64            i;
  EchoFileState    *state;
  EchoSinkCallbacks callbacks;

  NEXUS_PROGRAM_CONTEXT_PUSH("echo_sink_rich_file_create");
  NEXUS_PROGRAM_PRECONDITION(configuration.log_prefix != NULL, "echo_sink_rich_file_create: configuration.log_prefix must be non-null");

  error = nexus_filesystem_directory_create_parents(&configuration.base_directory);
  if (error != NEXUS_PROGRAM_ERROR_SUCCESS()) {
    NEXUS_PROGRAM_CONTEXT_POP();
    return error;
  }

  paths.paths = NULL;
  paths.count = 0;
  error = nexus_filesystem_directory_collect_allocated(&configuration.base_directory, FALSE, e_rich_file_matches, configuration.log_prefix, &paths);
  if (error == NEXUS_PROGRAM_ERROR_SUCCESS() && configuration.max_logs != 0 && paths.count > configuration.max_logs) {
    e_rich_file_paths_sort(paths.paths, paths.count);
    for (i = 0; i < paths.count; i++) {
      if (i + configuration.max_logs < paths.count)
        (void)nexus_filesystem_file_delete(&paths.paths[i]);
    }
  }
  nexus_filesystem_path_list_destroy(&paths);

  now = nexus_time_to_local_datetime(nexus_time_get_real());
  nexus_strings_string_format_with_truncation(filename, sizeof(filename), "%s_%04d%02u%02u_%02u%02u%02u.log", configuration.log_prefix, now.year,
                                              now.month, now.day, now.hour, now.minute, now.second);
  path = configuration.base_directory;
  nexus_filesystem_path_join(&path, filename);

  state = (EchoFileState *)nexus_memory_allocate(NEXUS_SIZEOF(*state));
  nexus_memory_bytes_clear(state, NEXUS_SIZEOF(*state));
  error = nexus_filesystem_file_open(&path, NFM_APPEND, &state->file);
  if (error != NEXUS_PROGRAM_ERROR_SUCCESS()) {
    nexus_memory_deallocate(state);
    NEXUS_PROGRAM_CONTEXT_POP();
    return error;
  }
  state->path      = path;
  state->precision = configuration.display_precision;
  error            = nexus_threads_mutex_create(&state->mutex);
  if (error != NEXUS_PROGRAM_ERROR_SUCCESS()) {
    (void)nexus_filesystem_file_close(state->file);
    nexus_memory_deallocate(state);
    NEXUS_PROGRAM_CONTEXT_POP();
    return error;
  }

  callbacks.filter           = configuration.filter;
  callbacks.filter_user_data = configuration.filter_user_data;
  callbacks.submit           = e_file_submit;
  callbacks.flush            = e_file_flush_internal;
  callbacks.destroy          = e_file_state_destroy;
  callbacks.user_data        = state;
  error                      = e_sink_create(&callbacks, 4, out_sink);
  if (error != NEXUS_PROGRAM_ERROR_SUCCESS()) {
    e_file_state_destroy(state);
    NEXUS_PROGRAM_CONTEXT_POP();
    return error;
  }

  NEXUS_PROGRAM_CONTEXT_POP();
  return NEXUS_PROGRAM_ERROR_SUCCESS();
}

NexusError *echo_sink_terminal_precision_set(EchoSink *sink, NexusTimePrecision precision) {
  EchoTerminalState *state;

  NEXUS_PROGRAM_CONTEXT_PUSH("echo_sink_terminal_precision_set");
  NEXUS_PROGRAM_PRECONDITION(sink != NULL && sink->kind == 2 && sink->callbacks.user_data != NULL,
                             "echo_sink_terminal_precision_set: sink must be a valid terminal sink");

  state = (EchoTerminalState *)sink->callbacks.user_data;
  ECHO_INTERNAL_LOCK(state->mutex);
  state->precision = precision;
  ECHO_INTERNAL_UNLOCK(state->mutex);

  NEXUS_PROGRAM_CONTEXT_POP();
  return NEXUS_PROGRAM_ERROR_SUCCESS();
}

NexusError *echo_sink_rich_terminal_configuration_set(EchoSink *sink, EchoRichTerminalConfiguration configuration) {
  EchoTerminalState *state;

  NEXUS_PROGRAM_CONTEXT_PUSH("echo_sink_rich_terminal_configuration_set");
  NEXUS_PROGRAM_PRECONDITION(sink != NULL && sink->kind == 2, "echo_sink_rich_terminal_configuration_set: sink must be a terminal sink");

  state = (EchoTerminalState *)sink->callbacks.user_data;
  ECHO_INTERNAL_LOCK(state->mutex);
  state->configuration = configuration;
  state->precision     = configuration.display_precision;
  ECHO_INTERNAL_UNLOCK(state->mutex);

  NEXUS_PROGRAM_CONTEXT_POP();
  return NEXUS_PROGRAM_ERROR_SUCCESS();
}

NexusError *echo_sink_rich_file_path_get(EchoSink *sink, NexusPath *out_path) {
  EchoFileState *state;

  NEXUS_PROGRAM_CONTEXT_PUSH("echo_sink_rich_file_path_get");
  NEXUS_PROGRAM_PRECONDITION(sink != NULL && out_path != NULL && (sink->kind == 3 || sink->kind == 4),
                             "echo_sink_rich_file_path_get: sink must be a non-null file sink, out_path must be non-null");

  state = (EchoFileState *)sink->callbacks.user_data;
  ECHO_INTERNAL_LOCK(state->mutex);
  *out_path = state->path;
  ECHO_INTERNAL_UNLOCK(state->mutex);

  NEXUS_PROGRAM_CONTEXT_POP();
  return NEXUS_PROGRAM_ERROR_SUCCESS();
}

EchoColor echo_color_none_create(void) {
  EchoColor color;
  NEXUS_PROGRAM_CONTEXT_PUSH("echo_color_none_create");
  color.mode      = ETCM_NONE;
  color.ansi_code = 0;
  color.red       = 0;
  color.green     = 0;
  color.blue      = 0;
  NEXUS_PROGRAM_CONTEXT_POP();
  return color;
}
EchoColor echo_color_ansi_create(uint8 code) {
  EchoColor color;
  NEXUS_PROGRAM_CONTEXT_PUSH("echo_color_ansi_create");
  color.mode      = ETCM_ANSI;
  color.ansi_code = code;
  color.red       = 0;
  color.green     = 0;
  color.blue      = 0;
  NEXUS_PROGRAM_CONTEXT_POP();
  return color;
}
EchoColor echo_color_rgb_create(uint8 red, uint8 green, uint8 blue) {
  EchoColor color;
  NEXUS_PROGRAM_CONTEXT_PUSH("echo_color_rgb_create");
  color.mode      = ETCM_RGB;
  color.ansi_code = 0;
  color.red       = red;
  color.green     = green;
  color.blue      = blue;
  NEXUS_PROGRAM_CONTEXT_POP();
  return color;
}

EchoRichTerminalConfiguration echo_rich_terminal_configuration_plain_create(NexusTimePrecision precision) {
  EchoRichTerminalConfiguration configuration;
  uint32                        i;
  NEXUS_PROGRAM_CONTEXT_PUSH("echo_rich_terminal_configuration_plain_create");
  configuration.display_precision = precision;
  configuration.color_time        = echo_color_none_create();
  configuration.color_file        = echo_color_none_create();
  configuration.color_system      = echo_color_none_create();
  for (i = 0; i < ELL_COUNT; i++)
    configuration.color_levels[i] = echo_color_none_create();
  NEXUS_PROGRAM_CONTEXT_POP();
  return configuration;
}

EchoRichTerminalConfiguration echo_rich_terminal_configuration_ansi_create(NexusTimePrecision precision) {
  EchoRichTerminalConfiguration configuration;
  NEXUS_PROGRAM_CONTEXT_PUSH("echo_rich_terminal_configuration_ansi_create");
  configuration                            = echo_rich_terminal_configuration_plain_create(precision);
  configuration.color_time                 = echo_color_ansi_create(ECHO_TERMINAL_ANSI_BRIGHT_BLACK);
  configuration.color_file                 = echo_color_ansi_create(ECHO_TERMINAL_ANSI_CYAN);
  configuration.color_system               = echo_color_ansi_create(ECHO_TERMINAL_ANSI_YELLOW);
  configuration.color_levels[ELL_TRACE]    = echo_color_ansi_create(ECHO_TERMINAL_ANSI_BRIGHT_BLACK);
  configuration.color_levels[ELL_DEBUG]    = echo_color_ansi_create(ECHO_TERMINAL_ANSI_BLUE);
  configuration.color_levels[ELL_INFO]     = echo_color_ansi_create(ECHO_TERMINAL_ANSI_GREEN);
  configuration.color_levels[ELL_NOTICE]   = echo_color_ansi_create(ECHO_TERMINAL_ANSI_MAGENTA);
  configuration.color_levels[ELL_WARNING]  = echo_color_ansi_create(ECHO_TERMINAL_ANSI_BRIGHT_YELLOW);
  configuration.color_levels[ELL_ERROR]    = echo_color_ansi_create(ECHO_TERMINAL_ANSI_RED);
  configuration.color_levels[ELL_CRITICAL] = echo_color_ansi_create(ECHO_TERMINAL_ANSI_BRIGHT_RED);
  NEXUS_PROGRAM_CONTEXT_POP();
  return configuration;
}

EchoRichTerminalConfiguration echo_rich_terminal_configuration_rgb_create(NexusTimePrecision precision) {
  EchoRichTerminalConfiguration configuration;
  NEXUS_PROGRAM_CONTEXT_PUSH("echo_rich_terminal_configuration_rgb_create");
  configuration                            = echo_rich_terminal_configuration_ansi_create(precision);
  configuration.color_time                 = echo_color_rgb_create(100, 100, 100);
  configuration.color_system               = echo_color_rgb_create(255, 165, 0);
  configuration.color_levels[ELL_TRACE]    = echo_color_rgb_create(128, 128, 128);
  configuration.color_levels[ELL_DEBUG]    = echo_color_rgb_create(70, 130, 180);
  configuration.color_levels[ELL_INFO]     = echo_color_rgb_create(46, 204, 113);
  configuration.color_levels[ELL_NOTICE]   = echo_color_rgb_create(155, 89, 182);
  configuration.color_levels[ELL_WARNING]  = echo_color_rgb_create(241, 196, 15);
  configuration.color_levels[ELL_ERROR]    = echo_color_rgb_create(231, 76, 60);
  configuration.color_levels[ELL_CRITICAL] = echo_color_rgb_create(255, 0, 0);
  NEXUS_PROGRAM_CONTEXT_POP();
  return configuration;
}
