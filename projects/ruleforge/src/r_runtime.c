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

  runtime                     = (RuleforgeRuntime *)nexus_memory_allocate(NEXUS_SIZEOF(*runtime));
  runtime->echo               = echo;
  runtime->log_system         = NULL;
  runtime->log_context        = NULL;
  runtime->logos              = NULL;
  runtime->grammar_compiler   = NULL;
  runtime->grammar_engines[0] = NULL;
  runtime->grammar_engines[1] = NULL;
  runtime->grammar_engines[2] = NULL;
  runtime->grammar_engines[3] = NULL;
  runtime->grammar_engines[4] = NULL;
  error                       = echo_runtime_system_register(echo, RULEFORGE_ECHO_LABEL, parent_system, &runtime->log_system);

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
