#include <nexus.h>
#include <echo.h>

#include "../logos.h"
#include "./l_internal.h"

LogosRuntime *logos_runtime_create(EchoRuntime *echo, EchoSystem *parent_system) {
  struct LogosRuntime *runtime;
  NexusError          *error;
  NexusError          *cleanup_error;

  NEXUS_PROGRAM_PRECONDITION(echo != NULL, "logos_runtime_create: NULL echo");

  /* A parse builds a syntax tree many times the size of its source; keeping that memory mapped between parses saves a page fault per page. */
  nexus_memory_retention_set((uint_large)512 * 1024 * 1024);

  runtime              = (LogosRuntime *)nexus_memory_allocate(NEXUS_SIZEOF(*runtime));
  runtime->echo        = echo;
  runtime->log_system  = NULL;
  runtime->log_context = NULL;
  error                = echo_runtime_system_register(echo, LOGOS_ECHO_LABEL, parent_system, &runtime->log_system);

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

void logos_runtime_destroy(LogosRuntime *runtime) {
  NexusError *error;

  NEXUS_PROGRAM_PRECONDITION(runtime != NULL, "logos_runtime_destroy: NULL runtime");

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

EchoSystem *logos_runtime_echo_system_get(LogosRuntime *runtime) {
  NEXUS_PROGRAM_PRECONDITION(runtime != NULL, "logos_runtime_echo_system_get: NULL runtime");

  return runtime->log_system;
}

EchoContext *logos_runtime_echo_context_get(LogosRuntime *runtime) {
  NEXUS_PROGRAM_PRECONDITION(runtime != NULL, "logos_runtime_echo_context_get: NULL runtime");

  return runtime->log_context;
}
