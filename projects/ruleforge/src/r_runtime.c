#include <nexus.h>
#include <echo.h>

#include <logos.h>

#include "../ruleforge.h"
#include "./r_internal.h"

RuleforgeRuntime *ruleforge_runtime_create(EchoRuntime *echo, EchoSystem *parent_system) {
  struct RuleforgeRuntime *runtime;
  NexusError              *error;
  NexusError              *cleanup_error;

  NEXUS_PROGRAM_PRECONDITION(echo != NULL, "ruleforge_runtime_create: NULL echo");

  runtime = (RuleforgeRuntime *)nexus_memory_allocate(NEXUS_SIZEOF(*runtime));
  runtime->echo = echo;
  runtime->log_system = NULL;
  runtime->log_context = NULL;
  runtime->logos = NULL;
  runtime->grammar_compiler = NULL;
  runtime->grammar_engines[0] = NULL;
  runtime->grammar_engines[1] = NULL;
  runtime->grammar_engines[2] = NULL;
  runtime->grammar_engines[3] = NULL;
  runtime->grammar_engines[4] = NULL;
  error = echo_runtime_system_register(echo, RULEFORGE_ECHO_LABEL, parent_system, &runtime->log_system);

  if (error->code != GLOBAL_NEXUS_ERROR_SUCCESS) {
    nexus_memory_deallocate(runtime);

    NEXUS_PROGRAM_PANIC_ERROR(error);
  }

  error = echo_context_create(echo, runtime->log_system, &runtime->log_context);

  if (error->code != GLOBAL_NEXUS_ERROR_SUCCESS) {
    cleanup_error = echo_runtime_system_unregister(echo, runtime->log_system);
    if (cleanup_error->code != GLOBAL_NEXUS_ERROR_SUCCESS) {
      NEXUS_PROGRAM_PANIC_ERROR(cleanup_error);
    }
    nexus_memory_deallocate(runtime);

    NEXUS_PROGRAM_PANIC_ERROR(error);
  }

  runtime->logos = logos_runtime_create(echo, runtime->log_system);

  return runtime;
}

void ruleforge_runtime_destroy(RuleforgeRuntime *runtime) {
  NexusError *error;
  uint32      index;

  NEXUS_PROGRAM_PRECONDITION(runtime != NULL, "ruleforge_runtime_destroy: NULL runtime");

  for (index = 0; index < 5; index++) {
    if (runtime->grammar_engines[index] != NULL) {
      logos_languages_genesis_engine_release(runtime->grammar_engines[index]);
    }
  }

  if (runtime->grammar_compiler != NULL) {
    logos_languages_genesis_compiler_destroy(runtime->grammar_compiler);
  }

  logos_runtime_destroy(runtime->logos);
  echo_context_destroy(runtime->log_context);
  runtime->log_context = NULL;

  error = echo_runtime_system_unregister(runtime->echo, runtime->log_system);

  if (error->code != GLOBAL_NEXUS_ERROR_SUCCESS) {
    NEXUS_PROGRAM_PANIC_ERROR(error);
  }

  nexus_memory_deallocate(runtime);
}

EchoSystem *ruleforge_runtime_echo_system_get(RuleforgeRuntime *runtime) {
  NEXUS_PROGRAM_PRECONDITION(runtime != NULL, "ruleforge_runtime_echo_system_get: NULL runtime");

  return runtime->log_system;
}

EchoContext *ruleforge_runtime_echo_context_get(RuleforgeRuntime *runtime) {
  NEXUS_PROGRAM_PRECONDITION(runtime != NULL, "ruleforge_runtime_echo_context_get: NULL runtime");

  return runtime->log_context;
}

void ruleforge_progress_log(RuleforgeRuntime *runtime, const char *phase, const char *source, const char *status, NexusTime started,
                            uint_large completed, uint_large total) {
  EchoField fields[6];
  f_real    seconds;
  seconds = (f_real)nexus_time_duration_between(started, nexus_time_get_monotonic()).nanoseconds / 1000000000.0;
  fields[0] = echo_field_string_create("phase", phase);
  fields[1] = echo_field_string_create("source", source);
  fields[2] = echo_field_string_create("status", status);
  fields[3] = echo_field_uint64_create("completed", completed);
  fields[4] = echo_field_uint64_create("total", total);
  fields[5] = echo_field_float_create("elapsed_seconds", seconds);
  (void)echo_message_log_fields_at(runtime->log_context, ELL_INFO, __FILE__, (uint32)__LINE__, fields, 6, "%s %s: %s (%lu/%lu, %.3fs)", phase,
                                   status, source, (unsigned long)completed, (unsigned long)total, seconds);
}
