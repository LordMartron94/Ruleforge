#include <nexus.h>
#include <echo.h>

#include "../pharos.h"
#include "./p_internal.h"

PharosRuntime *pharos_runtime_create(EchoRuntime *echo, EchoSystem *parent_system) {
  struct PharosRuntime *runtime;
  NexusError           *error;
  NexusError           *cleanup_error;

  NEXUS_PROGRAM_PRECONDITION(echo != NULL, "pharos_runtime_create: NULL echo");

  runtime              = (PharosRuntime *)nexus_memory_allocate(NEXUS_SIZEOF(*runtime));
  runtime->echo        = echo;
  runtime->log_system  = NULL;
  runtime->log_context = NULL;
  error                = echo_runtime_system_register(echo, PHAROS_ECHO_LABEL, parent_system, &runtime->log_system);

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

  return runtime;
}

void pharos_runtime_destroy(PharosRuntime *runtime) {
  NexusError *error;

  NEXUS_PROGRAM_PRECONDITION(runtime != NULL, "pharos_runtime_destroy: NULL runtime");

  if (runtime->log_context != NULL) {
    echo_context_destroy(runtime->log_context);
    runtime->log_context = NULL;
  }

  error = echo_runtime_system_unregister(runtime->echo, runtime->log_system);

  if (error->code != GLOBAL_NEXUS_ERROR_SUCCESS) {
    NEXUS_PROGRAM_PANIC_ERROR(error);
  }

  nexus_memory_deallocate(runtime);
}

EchoSystem *pharos_runtime_echo_system_get(PharosRuntime *runtime) {
  NEXUS_PROGRAM_PRECONDITION(runtime != NULL, "pharos_runtime_echo_system_get: NULL runtime");

  return runtime->log_system;
}

EchoContext *pharos_runtime_echo_context_get(PharosRuntime *runtime) {
  NEXUS_PROGRAM_PRECONDITION(runtime != NULL, "pharos_runtime_echo_context_get: NULL runtime");

  return runtime->log_context;
}
