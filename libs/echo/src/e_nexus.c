#include <nexus.h>

#include "../echo.h"
#include "e_internal.h"

#include <stdarg.h>
#include <stdio.h>

static EchoContext               *e_memory_context            = NULL;
static EchoContext               *e_assertion_context         = NULL;
static NEXUS_THREAD_LOCAL boolean e_memory_callback_active    = FALSE;
static NEXUS_THREAD_LOCAL boolean e_assertion_callback_active = FALSE;

static void e_memory_trace_callback(const char *format, ...) {
  va_list args;

  if (e_memory_context == NULL || format == NULL || e_memory_callback_active != FALSE)
    return;

  e_memory_callback_active = TRUE;
  va_start(args, format);
  (void)echo_message_logv_at(e_memory_context, ELL_TRACE, "<nexus-memory>", 0, NULL, 0, format, args);
  va_end(args);
  e_memory_callback_active = FALSE;
}

static void e_assertion_callback(void *user_data, const char *occurrence, const char *message, const char *file, uint32 line) {
  EchoContext *context = (EchoContext *)user_data;

  if (context == NULL || message == NULL || e_assertion_callback_active != FALSE)
    return;

  e_assertion_callback_active = TRUE;
  (void)echo_message_log_at(context, ELL_CRITICAL, file, line, "%s: %s", occurrence == NULL ? "Nexus diagnostic" : occurrence, message);

  /* Logging only queues the record for the worker thread, and the process aborts right after Nexus
     calls this observer, so the worker would never get to dispatch it (or anything queued before it)
     and the sinks would be left empty. Drain synchronously instead. On the worker thread itself the
     barrier would wait on its own drain loop, so skip it there -- the worker flushes after a critical
     record anyway. A recovered (non-fatal) violation just pays for one extra flush. */
  if (e_worker_runtime != context->runtime) {
    (void)echo_runtime_flush(context->runtime);
  }
  e_assertion_callback_active = FALSE;
}

NexusError *echo_nexus_memory_log_callback_install(EchoContext *context) {
  NEXUS_PROGRAM_CONTEXT_PUSH("echo_nexus_memory_log_callback_install");
  NEXUS_PROGRAM_PRECONDITION(context != NULL, "echo_nexus_memory_log_callback_install: context must be non-null");
  if (e_memory_context != NULL) {
    NEXUS_PROGRAM_CONTEXT_POP();
    return e_echo_error(GLOBAL_ECHO_ERROR_RUNTIME_STATE, "Echo memory callback is already installed");
  }

  e_memory_context = context;
  nexus_memory_trace_function_set(e_memory_trace_callback);

  NEXUS_PROGRAM_CONTEXT_POP();
  return NEXUS_PROGRAM_ERROR_SUCCESS();
}

void echo_nexus_memory_log_callback_uninstall(void) {
  NEXUS_PROGRAM_CONTEXT_PUSH("echo_nexus_memory_log_callback_uninstall");
  nexus_memory_trace_function_set(NULL);
  e_memory_context = NULL;
  NEXUS_PROGRAM_CONTEXT_POP();
}

NexusError *echo_nexus_assertions_callback_install(EchoContext *context) {
  NEXUS_PROGRAM_CONTEXT_PUSH("echo_nexus_assertions_callback_install");
  NEXUS_PROGRAM_PRECONDITION(context != NULL, "echo_nexus_assertions_callback_install: context must be non-null");
  if (e_assertion_context != NULL) {
    NEXUS_PROGRAM_CONTEXT_POP();
    return e_echo_error(GLOBAL_ECHO_ERROR_RUNTIME_STATE, "Echo assertion callback is already installed");
  }

  e_assertion_context = context;
  nexus_program_diagnostic_function_set(e_assertion_callback, context);

  NEXUS_PROGRAM_CONTEXT_POP();
  return NEXUS_PROGRAM_ERROR_SUCCESS();
}

void echo_nexus_assertions_callback_uninstall(void) {
  NEXUS_PROGRAM_CONTEXT_PUSH("echo_nexus_assertions_callback_uninstall");
  nexus_program_diagnostic_function_set(NULL, NULL);
  e_assertion_context = NULL;
  NEXUS_PROGRAM_CONTEXT_POP();
}
