#include <nexus.h>

#include "../echo.h"
#include "e_internal.h"

static NEXUS_MAYBE_UNUSED boolean e_system_registered_locked(EchoRuntime *runtime, EchoSystem *system) {
  uint64 i;
  for (i = 0; i < runtime->system_count; i++) {
    if (runtime->systems[i] == system) {
      return TRUE;
    }
  }
  return FALSE;
}

/* TRUE when candidate is ancestor or registered anywhere below it. Walks the parent links, not the labels, because labels may repeat. */
static boolean e_system_is_descendant(const EchoSystem *candidate, const EchoSystem *ancestor) {
  const EchoSystem *current;

  for (current = candidate; current != NULL; current = current->parent) {
    if (current == ancestor) {
      return TRUE;
    }
  }
  return FALSE;
}

NexusError *echo_runtime_system_register(EchoRuntime *runtime, const char *label, EchoSystem *parent, EchoSystem **out_system) {
  EchoSystem             *system;
  char                    full_label[ECHO_SYSTEM_LABEL_LENGTH_MAX];
  NexusStringFormatResult formatted;
  NexusError             *error;

  NEXUS_PROGRAM_CONTEXT_PUSH("echo_runtime_system_register");
  NEXUS_PROGRAM_PRECONDITION(runtime != NULL && label != NULL && out_system != NULL,
                             "echo_runtime_system_register: runtime, label, and out_system must be non-null");

  *out_system = NULL;
  error       = e_runtime_begin_operation(runtime, FALSE);
  if (error != NEXUS_PROGRAM_ERROR_SUCCESS()) {
    NEXUS_PROGRAM_CONTEXT_POP();
    return error;
  }

  ECHO_INTERNAL_LOCK(runtime->state_mutex);
  NEXUS_PROGRAM_PRECONDITION(parent == NULL || e_system_registered_locked(runtime, parent) != FALSE,
                             "echo_runtime_system_register: parent is not registered with runtime");
  if (e_string_contains_char(label, '.')) {
    ECHO_INTERNAL_UNLOCK(runtime->state_mutex);
    e_runtime_end_operation(runtime);
    NEXUS_PROGRAM_CONTEXT_POP();
    return e_echo_error(GLOBAL_ECHO_ERROR_SYSTEM_INVALID, "System labels cannot contain the hierarchy separator");
  }
  if (parent == NULL) {
    formatted = nexus_strings_string_format_with_truncation(full_label, sizeof(full_label), "%s", label);
  } else {
    formatted = nexus_strings_string_format_with_truncation(full_label, sizeof(full_label), "%s.%s", parent->label, label);
  }
  if (formatted.truncated != FALSE) {
    ECHO_INTERNAL_UNLOCK(runtime->state_mutex);
    e_runtime_end_operation(runtime);
    NEXUS_PROGRAM_CONTEXT_POP();
    return e_echo_error(GLOBAL_ECHO_ERROR_SYSTEM_INVALID, "System label exceeds Echo's configured maximum");
  }

  system = (EchoSystem *)nexus_memory_allocate(NEXUS_SIZEOF(*system));
  nexus_memory_bytes_clear(system, NEXUS_SIZEOF(*system));
  system->runtime        = runtime;
  system->parent         = parent;
  system->min_level      = ELL_TRACE;
  system->last_min_level = ELL_TRACE;
  nexus_strings_string_format_with_truncation(system->label, sizeof(system->label), "%s", full_label);
  if (runtime->system_count == runtime->system_capacity) {
    error = e_runtime_capacity_grow((void **)&runtime->systems, &runtime->system_capacity, NEXUS_SIZEOF(*runtime->systems));
    if (error != NEXUS_PROGRAM_ERROR_SUCCESS()) {
      nexus_memory_deallocate(system);
      ECHO_INTERNAL_UNLOCK(runtime->state_mutex);
      e_runtime_end_operation(runtime);
      NEXUS_PROGRAM_CONTEXT_POP();
      return error;
    }
  }
  runtime->systems[runtime->system_count++] = system;
  *out_system                               = system;
  ECHO_INTERNAL_UNLOCK(runtime->state_mutex);
  e_runtime_end_operation(runtime);

  NEXUS_PROGRAM_CONTEXT_POP();
  return NEXUS_PROGRAM_ERROR_SUCCESS();
}

NexusError *echo_runtime_system_unregister(EchoRuntime *runtime, EchoSystem *system) {
  uint64      i;
  NexusError *error;

  NEXUS_PROGRAM_CONTEXT_PUSH("echo_runtime_system_unregister");

  error = e_runtime_begin_operation(runtime, FALSE);
  if (error != NEXUS_PROGRAM_ERROR_SUCCESS()) {
    NEXUS_PROGRAM_CONTEXT_POP();
    return error;
  }

  ECHO_INTERNAL_LOCK(runtime->state_mutex);
  NEXUS_PROGRAM_PRECONDITION(e_system_registered_locked(runtime, system) != FALSE,
                             "echo_runtime_system_unregister: system is not registered with runtime");
  if (system->context_reference_count != 0) {
    ECHO_INTERNAL_UNLOCK(runtime->state_mutex);
    e_runtime_end_operation(runtime);
    NEXUS_PROGRAM_CONTEXT_POP();
    return e_echo_error(GLOBAL_ECHO_ERROR_SYSTEM_IN_USE, "System still has live contexts");
  }
  /* Whatever is still registered below this system moves up to its parent, so no system is left pointing at one that is gone. */
  for (i = 0; i < runtime->system_count; i++) {
    if (runtime->systems[i]->parent == system) {
      runtime->systems[i]->parent = system->parent;
    }
  }
  for (i = 0; i < runtime->system_count; i++) {
    if (runtime->systems[i] == system) {
      runtime->system_count--;
      runtime->systems[i] = runtime->systems[runtime->system_count];
      break;
    }
  }
  ECHO_INTERNAL_UNLOCK(runtime->state_mutex);
  nexus_memory_deallocate(system);
  e_runtime_end_operation(runtime);

  NEXUS_PROGRAM_CONTEXT_POP();
  return NEXUS_PROGRAM_ERROR_SUCCESS();
}

NexusError *echo_system_level_set(EchoSystem *system, EchoLogLevel level, EchoSystemLevelPolicy policy) {
  EchoRuntime *runtime;
  EchoSystem  *current;
  uint64       i;
  NexusError  *error;

  NEXUS_PROGRAM_CONTEXT_PUSH("echo_system_level_set");

  if (system == NULL || level >= ELL_COUNT || policy > ESLP_LOCAL_ONLY) {
    NEXUS_PROGRAM_CONTEXT_POP();
    return e_echo_error(GLOBAL_ECHO_ERROR_SYSTEM_INVALID, "Invalid system level request");
  }

  runtime = system->runtime;
  error   = e_runtime_begin_operation(runtime, FALSE);
  if (error != NEXUS_PROGRAM_ERROR_SUCCESS()) {
    NEXUS_PROGRAM_CONTEXT_POP();
    return error;
  }

  ECHO_INTERNAL_LOCK(runtime->state_mutex);
  NEXUS_PROGRAM_PRECONDITION(e_system_registered_locked(runtime, system) != FALSE, "echo_system_level_set: system is not registered with runtime");

  for (i = 0; i < runtime->system_count; i++) {
    current = runtime->systems[i];
    if (e_system_is_descendant(current, system) == FALSE || (policy == ESLP_LOCAL_ONLY && current != system)) {
      continue;
    }
    if (policy != ESLP_REVERT) {
      current->last_min_level = current->min_level;
    }
    switch (policy) {
    case ESLP_MOST_RESTRICTIVE:
      if (level > current->min_level)
        current->min_level = level;
      break;
    case ESLP_LEAST_RESTRICTIVE:
      if (level < current->min_level)
        current->min_level = level;
      break;
    case ESLP_OVERRIDE:
    case ESLP_LOCAL_ONLY:
      current->min_level = level;
      break;
    case ESLP_REVERT:
      current->min_level = current->last_min_level;
      break;
    }
  }
  ECHO_INTERNAL_UNLOCK(runtime->state_mutex);
  e_runtime_end_operation(runtime);

  NEXUS_PROGRAM_CONTEXT_POP();
  return NEXUS_PROGRAM_ERROR_SUCCESS();
}

EchoLogLevel echo_system_level_get(EchoSystem *system) {
  EchoLogLevel level = ELL_TRACE;

  NEXUS_PROGRAM_CONTEXT_PUSH("echo_system_level_get");

  if (system == NULL) {
    NEXUS_PROGRAM_CONTEXT_POP();
    return level;
  }

  ECHO_INTERNAL_LOCK(system->runtime->state_mutex);
  level = system->min_level;
  ECHO_INTERNAL_UNLOCK(system->runtime->state_mutex);

  NEXUS_PROGRAM_CONTEXT_POP();
  return level;
}

const char *echo_system_label_get(EchoSystem *system) {
  const char *label;

  NEXUS_PROGRAM_CONTEXT_PUSH("echo_system_label_get");
  label = system == NULL ? NULL : system->label;
  NEXUS_PROGRAM_CONTEXT_POP();
  return label;
}
