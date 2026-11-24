#include <nexus.h>

#include "../echo.h"
#include "e_internal.h"

static boolean e_context_registered_locked(EchoContext *context) {
  return context != NULL && context->runtime != NULL && context->system != NULL && context->destroying == FALSE;
}

static NexusError *e_context_copy_field(EchoStoredField *destination, EchoField source) {
  NexusStringFormatResult result;

  NEXUS_PROGRAM_CONTEXT_PUSH("e_context_copy_field");

  if (source.key == NULL || source.key[0] == '\0') {
    NEXUS_PROGRAM_CONTEXT_POP();
    return e_echo_error(GLOBAL_ECHO_ERROR_RUNTIME_STATE, "Echo field key must be non-empty");
  }

  result = nexus_strings_string_format_with_truncation(destination->key, sizeof(destination->key), "%s", source.key);
  if (result.truncated != FALSE) {
    NEXUS_PROGRAM_CONTEXT_POP();
    return e_echo_error(GLOBAL_ECHO_ERROR_RUNTIME_CAPACITY, "Echo field key is too long");
  }

  destination->type = source.type;
  if (source.type == EFT_STRING) {
    if (source.value.string_value == NULL) {
      destination->value.string_value[0] = '\0';
    } else {
      nexus_strings_string_format_with_truncation(destination->value.string_value, sizeof(destination->value.string_value), "%s",
                                                  source.value.string_value);
    }
  } else if (source.type == EFT_INT64) {
    destination->value.int64_value = source.value.int64_value;
  } else if (source.type == EFT_UINT64) {
    destination->value.uint64_value = source.value.uint64_value;
  } else if (source.type == EFT_FLOAT) {
    destination->value.float_value = source.value.float_value;
  } else if (source.type == EFT_BOOLEAN) {
    destination->value.boolean_value = source.value.boolean_value;
  } else {
    NEXUS_PROGRAM_CONTEXT_POP();
    return e_echo_error(GLOBAL_ECHO_ERROR_RUNTIME_STATE, "Unknown Echo field type");
  }

  NEXUS_PROGRAM_CONTEXT_POP();
  return NEXUS_PROGRAM_ERROR_SUCCESS();
}

NexusError *echo_context_create(EchoRuntime *runtime, EchoSystem *system, EchoContext **out_context) {
  EchoContext *context;
  NexusError  *error;
  uint64       i;

  NEXUS_PROGRAM_CONTEXT_PUSH("echo_context_create");
  NEXUS_PROGRAM_PRECONDITION(runtime != NULL && system != NULL && out_context != NULL,
                             "echo_context_create: runtime, system, and out_context must be non-null");

  *out_context = NULL;
  error        = e_runtime_begin_operation(runtime, FALSE);
  if (error != NEXUS_PROGRAM_ERROR_SUCCESS()) {
    NEXUS_PROGRAM_CONTEXT_POP();
    return error;
  }

  context = (EchoContext *)nexus_memory_allocate(NEXUS_SIZEOF(*context));
  nexus_memory_bytes_clear(context, NEXUS_SIZEOF(*context));

  ECHO_INTERNAL_LOCK(runtime->state_mutex);
  for (i = 0; i < runtime->system_count; i++) {
    if (runtime->systems[i] == system)
      break;
  }
  NEXUS_PROGRAM_PRECONDITION(i != runtime->system_count, "echo_context_create: system is not registered with runtime");

  context->runtime = runtime;
  context->system  = system;
  system->context_reference_count++;
  *out_context = context;
  ECHO_INTERNAL_UNLOCK(runtime->state_mutex);
  e_runtime_end_operation(runtime);

  NEXUS_PROGRAM_CONTEXT_POP();
  return NEXUS_PROGRAM_ERROR_SUCCESS();
}

NexusError *echo_context_clone(EchoContext *source, EchoContext **out_context) {
  EchoRuntime *runtime;
  EchoContext *clone;
  NexusError  *error;

  NEXUS_PROGRAM_CONTEXT_PUSH("echo_context_clone");
  NEXUS_PROGRAM_PRECONDITION(source != NULL && out_context != NULL, "echo_context_clone: source and out_context must be non-null");

  runtime      = source->runtime;
  *out_context = NULL;
  error        = e_runtime_begin_operation(runtime, FALSE);
  if (error != NEXUS_PROGRAM_ERROR_SUCCESS()) {
    NEXUS_PROGRAM_CONTEXT_POP();
    return error;
  }

  clone = (EchoContext *)nexus_memory_allocate(NEXUS_SIZEOF(*clone));
  nexus_memory_bytes_clear(clone, NEXUS_SIZEOF(*clone));

  ECHO_INTERNAL_LOCK(runtime->state_mutex);
  if (e_context_registered_locked(source) == FALSE) {
    ECHO_INTERNAL_UNLOCK(runtime->state_mutex);
    nexus_memory_deallocate(clone);
    e_runtime_end_operation(runtime);
    NEXUS_PROGRAM_CONTEXT_POP();
    return e_echo_error(GLOBAL_ECHO_ERROR_RUNTIME_STATE, "Source context is not active");
  }

  *clone                   = *source;
  clone->active_operations = 0;
  clone->destroying        = FALSE;
  source->system->context_reference_count++;
  *out_context = clone;
  ECHO_INTERNAL_UNLOCK(runtime->state_mutex);
  e_runtime_end_operation(runtime);

  NEXUS_PROGRAM_CONTEXT_POP();
  return NEXUS_PROGRAM_ERROR_SUCCESS();
}

void echo_context_destroy(EchoContext *context) {
  EchoRuntime *runtime;

  NEXUS_PROGRAM_CONTEXT_PUSH("echo_context_destroy");

  if (context == NULL) {
    NEXUS_PROGRAM_CONTEXT_POP();
    return;
  }

  runtime = context->runtime;
  ECHO_INTERNAL_LOCK(runtime->state_mutex);
  context->destroying = TRUE;
  while (context->active_operations != 0) {
    (void)nexus_threads_cond_wait(runtime->state_cond, runtime->state_mutex);
  }
  if (context->system->context_reference_count > 0)
    context->system->context_reference_count--;
  ECHO_INTERNAL_UNLOCK(runtime->state_mutex);
  nexus_memory_deallocate(context);

  NEXUS_PROGRAM_CONTEXT_POP();
}

NexusError *echo_context_span_push(EchoContext *context, const char *span) {
  NexusStringFormatResult result;
  EchoRuntime            *runtime;

  NEXUS_PROGRAM_CONTEXT_PUSH("echo_context_span_push");
  NEXUS_PROGRAM_PRECONDITION(context != NULL && span != NULL, "echo_context_span_push: context and span must be non-null");

  runtime = context->runtime;
  ECHO_INTERNAL_LOCK(runtime->state_mutex);
  if (e_context_registered_locked(context) == FALSE) {
    ECHO_INTERNAL_UNLOCK(runtime->state_mutex);
    NEXUS_PROGRAM_CONTEXT_POP();
    return e_echo_error(GLOBAL_ECHO_ERROR_RUNTIME_STATE, "Context is inactive");
  }

  /* A full span stack or an over-length span is tolerated, not fatal -- matches
     e_record_copy_field's per-message truncation policy: best effort, never an error. */
  if (context->span_count < ECHO_SPAN_COUNT_MAX) {
    result = nexus_strings_string_format_with_truncation(context->spans[context->span_count], ECHO_SPAN_LENGTH_MAX, "%s", span);
    (void)result;
    context->span_count++;
  }
  ECHO_INTERNAL_UNLOCK(runtime->state_mutex);

  NEXUS_PROGRAM_CONTEXT_POP();
  return NEXUS_PROGRAM_ERROR_SUCCESS();
}

NexusError *echo_context_span_pop(EchoContext *context) {
  EchoRuntime *runtime;

  NEXUS_PROGRAM_CONTEXT_PUSH("echo_context_span_pop");
  NEXUS_PROGRAM_PRECONDITION(context != NULL, "echo_context_span_pop: context must be non-null");

  runtime = context->runtime;
  ECHO_INTERNAL_LOCK(runtime->state_mutex);
  if (e_context_registered_locked(context) == FALSE || context->span_count == 0) {
    ECHO_INTERNAL_UNLOCK(runtime->state_mutex);
    NEXUS_PROGRAM_CONTEXT_POP();
    return e_echo_error(GLOBAL_ECHO_ERROR_RUNTIME_STATE, "Echo context span stack is empty or inactive");
  }

  context->span_count--;
  ECHO_INTERNAL_UNLOCK(runtime->state_mutex);

  NEXUS_PROGRAM_CONTEXT_POP();
  return NEXUS_PROGRAM_ERROR_SUCCESS();
}

NexusError *echo_context_field_set(EchoContext *context, EchoField field) {
  EchoRuntime    *runtime;
  EchoStoredField copy;
  NexusError     *error;
  uint8           i;

  NEXUS_PROGRAM_CONTEXT_PUSH("echo_context_field_set");
  NEXUS_PROGRAM_PRECONDITION(context != NULL, "echo_context_field_set: context must be non-null");

  error = e_context_copy_field(&copy, field);
  if (error != NEXUS_PROGRAM_ERROR_SUCCESS()) {
    NEXUS_PROGRAM_CONTEXT_POP();
    return error;
  }

  runtime = context->runtime;
  ECHO_INTERNAL_LOCK(runtime->state_mutex);
  if (e_context_registered_locked(context) == FALSE) {
    ECHO_INTERNAL_UNLOCK(runtime->state_mutex);
    NEXUS_PROGRAM_CONTEXT_POP();
    return e_echo_error(GLOBAL_ECHO_ERROR_RUNTIME_STATE, "Context is inactive");
  }

  for (i = 0; i < context->field_count; i++) {
    if (e_string_equals(context->fields[i].key, copy.key))
      break;
  }
  /* A full field table is tolerated, not fatal -- matches e_record_copy_field's per-message
     truncation policy: the new/updated field is best-effort dropped rather than erroring. */
  if (i != context->field_count || context->field_count < ECHO_FIELD_COUNT_MAX) {
    if (i == context->field_count)
      context->field_count++;
    context->fields[i] = copy;
  }
  ECHO_INTERNAL_UNLOCK(runtime->state_mutex);

  NEXUS_PROGRAM_CONTEXT_POP();
  return NEXUS_PROGRAM_ERROR_SUCCESS();
}

boolean echo_context_field_remove(EchoContext *context, const char *key) {
  uint8 i;

  NEXUS_PROGRAM_CONTEXT_PUSH("echo_context_field_remove");

  if (context == NULL || key == NULL) {
    NEXUS_PROGRAM_CONTEXT_POP();
    return FALSE;
  }

  ECHO_INTERNAL_LOCK(context->runtime->state_mutex);
  for (i = 0; i < context->field_count; i++) {
    if (e_string_equals(context->fields[i].key, key)) {
      context->field_count--;
      context->fields[i] = context->fields[context->field_count];
      ECHO_INTERNAL_UNLOCK(context->runtime->state_mutex);
      NEXUS_PROGRAM_CONTEXT_POP();
      return TRUE;
    }
  }
  ECHO_INTERNAL_UNLOCK(context->runtime->state_mutex);

  NEXUS_PROGRAM_CONTEXT_POP();
  return FALSE;
}

EchoField echo_field_string_create(const char *key, const char *value) {
  EchoField field;
  NEXUS_PROGRAM_CONTEXT_PUSH("echo_field_string_create");
  field.key                = key;
  field.type               = EFT_STRING;
  field.value.string_value = value;
  NEXUS_PROGRAM_CONTEXT_POP();
  return field;
}
EchoField echo_field_int64_create(const char *key, int64 value) {
  EchoField field;
  NEXUS_PROGRAM_CONTEXT_PUSH("echo_field_int64_create");
  field.key               = key;
  field.type              = EFT_INT64;
  field.value.int64_value = value;
  NEXUS_PROGRAM_CONTEXT_POP();
  return field;
}
EchoField echo_field_uint64_create(const char *key, uint64 value) {
  EchoField field;
  NEXUS_PROGRAM_CONTEXT_PUSH("echo_field_uint64_create");
  field.key                = key;
  field.type               = EFT_UINT64;
  field.value.uint64_value = value;
  NEXUS_PROGRAM_CONTEXT_POP();
  return field;
}
EchoField echo_field_float_create(const char *key, f_real value) {
  EchoField field;
  NEXUS_PROGRAM_CONTEXT_PUSH("echo_field_float_create");
  field.key               = key;
  field.type              = EFT_FLOAT;
  field.value.float_value = value;
  NEXUS_PROGRAM_CONTEXT_POP();
  return field;
}
EchoField echo_field_boolean_create(const char *key, boolean value) {
  EchoField field;
  NEXUS_PROGRAM_CONTEXT_PUSH("echo_field_boolean_create");
  field.key                 = key;
  field.type                = EFT_BOOLEAN;
  field.value.boolean_value = value;
  NEXUS_PROGRAM_CONTEXT_POP();
  return field;
}
