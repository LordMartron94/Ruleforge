#include <nexus.h>

#include "../echo.h"
#include "e_internal.h"

#include <stdarg.h>

static boolean e_record_copy_field(EchoRecord *record, EchoField source) {
  EchoStoredField        *destination;
  uint8                   i;
  NexusStringFormatResult result;

  NEXUS_PROGRAM_CONTEXT_PUSH("e_record_copy_field");

  if (source.key == NULL || source.key[0] == '\0') {
    NEXUS_PROGRAM_CONTEXT_POP();
    return FALSE;
  }

  for (i = 0; i < record->field_count; i++) {
    if (e_string_equals(record->fields[i].key, source.key))
      break;
  }
  if (i == record->field_count) {
    if (record->field_count == ECHO_FIELD_COUNT_MAX) {
      NEXUS_PROGRAM_CONTEXT_POP();
      return FALSE;
    }
    record->field_count++;
  }

  destination = &record->fields[i];
  result      = nexus_strings_string_format_with_truncation(destination->key, NEXUS_SIZEOF(destination->key), "%s", source.key);
  if (result.truncated != FALSE) {
    record->truncated = TRUE;
  }
  destination->type = source.type;
  switch (source.type) {
  case EFT_STRING:
    result = nexus_strings_string_format_with_truncation(destination->value.string_value, NEXUS_SIZEOF(destination->value.string_value), "%s",
                                                         source.value.string_value == NULL ? "" : source.value.string_value);
    if (result.truncated != FALSE)
      record->truncated = TRUE;
    break;
  case EFT_INT64:
    destination->value.int64_value = source.value.int64_value;
    break;
  case EFT_UINT64:
    destination->value.uint64_value = source.value.uint64_value;
    break;
  case EFT_FLOAT:
    destination->value.float_value = source.value.float_value;
    break;
  case EFT_BOOLEAN:
    destination->value.boolean_value = source.value.boolean_value;
    break;
  }

  NEXUS_PROGRAM_CONTEXT_POP();
  return TRUE;
}

static void e_message_operation_end(EchoContext *context) {
  EchoRuntime *runtime = context->runtime;

  ECHO_INTERNAL_LOCK(runtime->state_mutex);
  if (context->active_operations > 0)
    context->active_operations--;
  if (runtime->active_operations > 0)
    runtime->active_operations--;
  (void)nexus_threads_cond_broadcast(runtime->state_cond);
  ECHO_INTERNAL_UNLOCK(runtime->state_mutex);
}

EchoLogOutcome echo_message_logv_at(EchoContext *context, EchoLogLevel level, const char *file, uint32 line, const EchoField *fields,
                                    uint8 field_count, const char *format, va_list args) {
  EchoRuntime            *runtime;
  EchoRuntime            *previous_sending_runtime;
  EchoRecord              record;
  NexusStringFormatResult formatted;
  NexusError             *error;
  uint8                   i;
  boolean                 sent;

  NEXUS_PROGRAM_CONTEXT_PUSH("echo_message_logv_at");

  if (context == NULL || format == NULL || level >= ELL_COUNT) {
    NEXUS_PROGRAM_CONTEXT_POP();
    return ELO_SILENCED_CLOSED;
  }

  runtime = context->runtime;

  /* The worker is the sole consumer of its own channel -- a blocking send from here while it's
     already draining that channel is a self-deadlock (waiting on space only its own drain loop
     could free), so it can never safely re-enter the normal record/sink pipeline. Dropped
     silently; statistics.recursive_count still counts it. A producer thread re-entering mid-send
     (e_sending_runtime) no longer needs this branch -- the channel's mutex is recursive (see
     nexus_threads_channel_create_recursive), so that case now just falls through and goes
     through the exact same pipeline as any other message. */
  if (e_worker_runtime == runtime) {
    ECHO_INTERNAL_LOCK(runtime->state_mutex);
    runtime->statistics.attempted_count++;
    runtime->statistics.recursive_count++;
    ECHO_INTERNAL_UNLOCK(runtime->state_mutex);
    NEXUS_PROGRAM_CONTEXT_POP();
    return ELO_SILENCED_RECURSIVE;
  }

  ECHO_INTERNAL_LOCK(runtime->state_mutex);
  if (context->destroying != FALSE || runtime->started == FALSE || runtime->closing != FALSE || runtime->closed != FALSE) {
    ECHO_INTERNAL_UNLOCK(runtime->state_mutex);
    NEXUS_PROGRAM_CONTEXT_POP();
    return ELO_SILENCED_CLOSED;
  }
  runtime->statistics.attempted_count++;
  if (level < context->system->min_level) {
    runtime->statistics.level_silenced_count++;
    ECHO_INTERNAL_UNLOCK(runtime->state_mutex);
    NEXUS_PROGRAM_CONTEXT_POP();
    return ELO_SILENCED_LEVEL;
  }

  context->active_operations++;
  runtime->active_operations++;
  record.message[0]          = '\0';
  record.origin_file[0]      = '\0';
  record.system_label[0]     = '\0';
  record.span_count          = 0;
  record.field_count         = 0;
  record.truncated           = FALSE;
  record.kind                = ERK_LOG;
  record.level               = level;
  record.timestamp           = nexus_time_get_real();
  record.monotonic_timestamp = nexus_time_get_monotonic();
  formatted = nexus_strings_string_format_with_truncation(record.origin_file, NEXUS_SIZEOF(record.origin_file), "%s", file == NULL ? "?" : file);
  if (formatted.truncated != FALSE)
    record.truncated = TRUE;
  record.origin_line = line;
  nexus_strings_string_format_with_truncation(record.system_label, NEXUS_SIZEOF(record.system_label), "%s", context->system->label);
  record.span_count = context->span_count;
  for (i = 0; i < context->span_count; i++) {
    nexus_strings_string_format_with_truncation(record.spans[i], NEXUS_SIZEOF(record.spans[i]), "%s", context->spans[i]);
  }
  for (i = 0; i < context->field_count; i++)
    record.fields[i] = context->fields[i];
  record.field_count = context->field_count;
  ECHO_INTERNAL_UNLOCK(runtime->state_mutex);

  formatted = nexus_strings_vstring_format_with_truncation(record.message, NEXUS_SIZEOF(record.message), format, args);
  if (formatted.truncated != FALSE)
    record.truncated = TRUE;
  if (fields != NULL) {
    for (i = 0; i < field_count; i++) {
      if (e_record_copy_field(&record, fields[i]) == FALSE)
        record.truncated = TRUE;
    }
  }

  previous_sending_runtime = e_sending_runtime;
  e_sending_runtime        = runtime;
  if (runtime->configuration.saturation_policy == EQSP_DROP_NEWEST && level < ELL_ERROR) {
    sent = nexus_threads_channel_try_send(runtime->channel, &record);
  } else {
    error = nexus_threads_channel_send(runtime->channel, &record);
    sent  = error == NEXUS_PROGRAM_ERROR_SUCCESS();
  }
  e_sending_runtime = previous_sending_runtime;
  if (sent == FALSE) {
    ECHO_INTERNAL_LOCK(runtime->state_mutex);
    runtime->statistics.dropped_count++;
    e_message_operation_end(context);
    ECHO_INTERNAL_UNLOCK(runtime->state_mutex);
    NEXUS_PROGRAM_CONTEXT_POP();
    return runtime->closed != FALSE || runtime->closing != FALSE ? ELO_SILENCED_CLOSED : ELO_SILENCED_DROPPED;
  }
  ECHO_INTERNAL_LOCK(runtime->state_mutex);
  runtime->statistics.admitted_count++;
  if (record.truncated != FALSE)
    runtime->statistics.truncated_count++;
  e_message_operation_end(context);
  ECHO_INTERNAL_UNLOCK(runtime->state_mutex);
  NEXUS_PROGRAM_CONTEXT_POP();
  return record.truncated != FALSE ? ELO_ECHOED_TRUNCATED : ELO_ECHOED;
}

EchoLogOutcome echo_message_log_at(EchoContext *context, EchoLogLevel level, const char *file, uint32 line, const char *format, ...) {
  EchoLogOutcome outcome;
  va_list        args;
  NEXUS_PROGRAM_CONTEXT_PUSH("echo_message_log_at");
  va_start(args, format);
  outcome = echo_message_logv_at(context, level, file, line, NULL, 0, format, args);
  va_end(args);
  NEXUS_PROGRAM_CONTEXT_POP();
  return outcome;
}

EchoLogOutcome echo_message_log_fields_at(EchoContext *context, EchoLogLevel level, const char *file, uint32 line, const EchoField *fields,
                                          uint8 field_count, const char *format, ...) {
  EchoLogOutcome outcome;
  va_list        args;
  NEXUS_PROGRAM_CONTEXT_PUSH("echo_message_log_fields_at");
  va_start(args, format);
  outcome = echo_message_logv_at(context, level, file, line, fields, field_count, format, args);
  va_end(args);
  NEXUS_PROGRAM_CONTEXT_POP();
  return outcome;
}

const char *echo_log_level_label_get(EchoLogLevel level) {
  static const char *labels[ELL_COUNT] = {"TRACE", "DEBUG", "INFO", "NOTICE", "WARNING", "ERROR", "CRITICAL"};
  const char        *label;
  NEXUS_PROGRAM_CONTEXT_PUSH("echo_log_level_label_get");
  label = level < ELL_COUNT ? labels[level] : "UNKNOWN";
  NEXUS_PROGRAM_CONTEXT_POP();
  return label;
}

EchoLogLevel echo_record_level_get(const EchoRecord *record) {
  EchoLogLevel level;
  NEXUS_PROGRAM_CONTEXT_PUSH("echo_record_level_get");
  level = record == NULL ? ELL_TRACE : record->level;
  NEXUS_PROGRAM_CONTEXT_POP();
  return level;
}
NexusTime echo_record_timestamp_get(const EchoRecord *record) {
  NexusTime zero;
  NexusTime result;
  NEXUS_PROGRAM_CONTEXT_PUSH("echo_record_timestamp_get");
  zero.time         = 0;
  zero.precision    = NTP_SECOND;
  zero.clock_origin = NCO_REAL;
  result            = record == NULL ? zero : record->timestamp;
  NEXUS_PROGRAM_CONTEXT_POP();
  return result;
}
NexusTime echo_record_monotonic_timestamp_get(const EchoRecord *record) {
  NexusTime zero;
  NexusTime result;
  NEXUS_PROGRAM_CONTEXT_PUSH("echo_record_monotonic_timestamp_get");
  zero.time         = 0;
  zero.precision    = NTP_SECOND;
  zero.clock_origin = NCO_MONOTONIC;
  result            = record == NULL ? zero : record->monotonic_timestamp;
  NEXUS_PROGRAM_CONTEXT_POP();
  return result;
}
const char *echo_record_message_get(const EchoRecord *record) {
  const char *result;
  NEXUS_PROGRAM_CONTEXT_PUSH("echo_record_message_get");
  result = record == NULL ? NULL : record->message;
  NEXUS_PROGRAM_CONTEXT_POP();
  return result;
}
const char *echo_record_origin_file_get(const EchoRecord *record) {
  const char *result;
  NEXUS_PROGRAM_CONTEXT_PUSH("echo_record_origin_file_get");
  result = record == NULL ? NULL : record->origin_file;
  NEXUS_PROGRAM_CONTEXT_POP();
  return result;
}
uint32 echo_record_origin_line_get(const EchoRecord *record) {
  uint32 result;
  NEXUS_PROGRAM_CONTEXT_PUSH("echo_record_origin_line_get");
  result = record == NULL ? 0 : record->origin_line;
  NEXUS_PROGRAM_CONTEXT_POP();
  return result;
}
const char *echo_record_system_label_get(const EchoRecord *record) {
  const char *result;
  NEXUS_PROGRAM_CONTEXT_PUSH("echo_record_system_label_get");
  result = record == NULL ? NULL : record->system_label;
  NEXUS_PROGRAM_CONTEXT_POP();
  return result;
}
uint8 echo_record_span_count_get(const EchoRecord *record) {
  uint8 result;
  NEXUS_PROGRAM_CONTEXT_PUSH("echo_record_span_count_get");
  result = record == NULL ? 0 : record->span_count;
  NEXUS_PROGRAM_CONTEXT_POP();
  return result;
}
const char *echo_record_span_get(const EchoRecord *record, uint8 index) {
  const char *result;
  NEXUS_PROGRAM_CONTEXT_PUSH("echo_record_span_get");
  result = record == NULL || index >= record->span_count ? NULL : record->spans[index];
  NEXUS_PROGRAM_CONTEXT_POP();
  return result;
}
uint8 echo_record_field_count_get(const EchoRecord *record) {
  uint8 result;
  NEXUS_PROGRAM_CONTEXT_PUSH("echo_record_field_count_get");
  result = record == NULL ? 0 : record->field_count;
  NEXUS_PROGRAM_CONTEXT_POP();
  return result;
}
boolean echo_record_field_get(const EchoRecord *record, uint8 index, EchoField *out_field) {
  const EchoStoredField *field;

  NEXUS_PROGRAM_CONTEXT_PUSH("echo_record_field_get");

  if (record == NULL || out_field == NULL || index >= record->field_count) {
    NEXUS_PROGRAM_CONTEXT_POP();
    return FALSE;
  }

  field           = &record->fields[index];
  out_field->key  = field->key;
  out_field->type = field->type;
  if (field->type == EFT_STRING)
    out_field->value.string_value = field->value.string_value;
  else if (field->type == EFT_INT64)
    out_field->value.int64_value = field->value.int64_value;
  else if (field->type == EFT_UINT64)
    out_field->value.uint64_value = field->value.uint64_value;
  else if (field->type == EFT_FLOAT)
    out_field->value.float_value = field->value.float_value;
  else
    out_field->value.boolean_value = field->value.boolean_value;

  NEXUS_PROGRAM_CONTEXT_POP();
  return TRUE;
}

void e_record_dispatch(EchoRuntime *runtime, const EchoRecord *record) {
  EchoFilterFunction *runtime_filter;
  void               *runtime_filter_data;
  EchoSink           *sink;
  uint64              i;
  boolean             accepted;
  NexusError         *error;

  NEXUS_PROGRAM_CONTEXT_PUSH("e_record_dispatch");

  ECHO_INTERNAL_LOCK(runtime->state_mutex);
  runtime_filter      = runtime->filter;
  runtime_filter_data = runtime->filter_user_data;
  ECHO_INTERNAL_UNLOCK(runtime->state_mutex);
  if (runtime_filter != NULL && runtime_filter(record, runtime_filter_data) == FALSE) {
    ECHO_INTERNAL_LOCK(runtime->state_mutex);
    runtime->statistics.filtered_count++;
    ECHO_INTERNAL_UNLOCK(runtime->state_mutex);
    NEXUS_PROGRAM_CONTEXT_POP();
    return;
  }

  for (i = 0; i < runtime->sink_count; i++) {
    sink = runtime->sinks[i];
    if (nexus_threads_atomic_uint32_load(&sink->enabled) == 0)
      continue;
    accepted = sink->callbacks.filter == NULL || sink->callbacks.filter(record, sink->callbacks.filter_user_data);
    if (accepted == FALSE) {
      ECHO_INTERNAL_LOCK(runtime->state_mutex);
      runtime->statistics.sink_filtered_count++;
      ECHO_INTERNAL_UNLOCK(runtime->state_mutex);
      continue;
    }
    if (sink->callbacks.submit != NULL) {
      error = sink->callbacks.submit(record, sink->callbacks.user_data);
      if (error != NEXUS_PROGRAM_ERROR_SUCCESS())
        e_runtime_worker_set_error(runtime, error);
    }
  }
  if (record->level == ELL_CRITICAL)
    e_runtime_flush_sinks(runtime);

  NEXUS_PROGRAM_CONTEXT_POP();
}
