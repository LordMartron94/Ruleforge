#include <nexus.h>

#include "../echo.h"
#include "e_internal.h"

NEXUS_THREAD_LOCAL EchoRuntime *e_worker_runtime  = NULL;
NEXUS_THREAD_LOCAL EchoRuntime *e_sending_runtime = NULL;

NexusError *e_echo_error(NexusErrorCode code, const char *message) {
  return _nexus_program_errors_failure_create(code, "%s", __FILE__, __LINE__, message);
}

NexusError *e_runtime_capacity_grow(void **array, uint64 *capacity, uint_large element_size) {
  uint64 new_capacity;
  void  *grown;

  NEXUS_PROGRAM_CONTEXT_PUSH("e_runtime_capacity_grow");

  if (*capacity > ((uint64)-1) / 2) {
    NEXUS_PROGRAM_CONTEXT_POP();
    return e_echo_error(GLOBAL_ECHO_ERROR_RUNTIME_CAPACITY, "Echo registry capacity cannot grow further");
  }

  new_capacity = (*capacity == 0) ? 8 : (*capacity * 2);
  grown        = (*array == NULL) ? nexus_memory_allocate(element_size * new_capacity) : nexus_memory_reallocate(*array, element_size * new_capacity);
  *array       = grown;
  *capacity    = new_capacity;

  NEXUS_PROGRAM_CONTEXT_POP();
  return NEXUS_PROGRAM_ERROR_SUCCESS();
}

boolean e_runtime_is_valid_locked(EchoRuntime *runtime) {
  return runtime != NULL && runtime->closed == FALSE && runtime->closing == FALSE;
}

NexusError *e_runtime_begin_operation(EchoRuntime *runtime, boolean require_started) {
  NEXUS_PROGRAM_CONTEXT_PUSH("e_runtime_begin_operation");

  if (runtime == NULL) {
    NEXUS_PROGRAM_CONTEXT_POP();
    return e_echo_error(GLOBAL_ECHO_ERROR_RUNTIME_STATE, "Echo runtime is null");
  }

  ECHO_INTERNAL_LOCK(runtime->state_mutex);
  if (e_runtime_is_valid_locked(runtime) == FALSE || (require_started != FALSE && runtime->started == FALSE)) {
    ECHO_INTERNAL_UNLOCK(runtime->state_mutex);
    NEXUS_PROGRAM_CONTEXT_POP();
    return e_echo_error(GLOBAL_ECHO_ERROR_RUNTIME_STATE, "Echo runtime is not accepting this operation");
  }
  runtime->active_operations++;
  ECHO_INTERNAL_UNLOCK(runtime->state_mutex);

  NEXUS_PROGRAM_CONTEXT_POP();
  return NEXUS_PROGRAM_ERROR_SUCCESS();
}

void e_runtime_end_operation(EchoRuntime *runtime) {
  ECHO_INTERNAL_LOCK(runtime->state_mutex);
  if (runtime->active_operations > 0) {
    runtime->active_operations--;
  }
  (void)nexus_threads_cond_broadcast(runtime->state_cond);
  ECHO_INTERNAL_UNLOCK(runtime->state_mutex);
}

void e_runtime_worker_set_error(EchoRuntime *runtime, NexusError *error) {
  if (error == NULL || error == NEXUS_PROGRAM_ERROR_SUCCESS()) {
    return;
  }
  ECHO_INTERNAL_LOCK(runtime->state_mutex);
  if (runtime->worker_error == NULL) {
    runtime->worker_error = error;
  }
  runtime->statistics.sink_failure_count++;
  ECHO_INTERNAL_UNLOCK(runtime->state_mutex);
}

void e_runtime_barrier_complete(EchoRuntime *runtime, uint64 barrier_id) {
  ECHO_INTERNAL_LOCK(runtime->state_mutex);
  if (barrier_id > runtime->completed_barrier_id) {
    runtime->completed_barrier_id = barrier_id;
  }
  (void)nexus_threads_cond_broadcast(runtime->state_cond);
  ECHO_INTERNAL_UNLOCK(runtime->state_mutex);
}

void e_runtime_flush_sinks(EchoRuntime *runtime) {
  uint64    i;
  EchoSink *sink;

  for (i = 0; i < runtime->sink_count; i++) {
    sink = runtime->sinks[i];
    if (nexus_threads_atomic_uint32_load(&sink->enabled) == 0 || sink->callbacks.flush == NULL) {
      continue;
    }
    e_runtime_worker_set_error(runtime, sink->callbacks.flush(sink->callbacks.user_data));
  }
}

static void e_runtime_worker(void *user_data) {
  EchoRuntime *runtime;
  EchoRecord   record;
  boolean      closed;
  NexusError  *error;

  NEXUS_PROGRAM_CONTEXT_PUSH("e_runtime_worker");

  runtime          = (EchoRuntime *)user_data;
  e_worker_runtime = runtime;

  ECHO_INTERNAL_LOCK(runtime->state_mutex);
  runtime->worker_ready = TRUE;
  (void)nexus_threads_cond_broadcast(runtime->state_cond);
  ECHO_INTERNAL_UNLOCK(runtime->state_mutex);

  for (;;) {
    error = nexus_threads_channel_receive(runtime->channel, &record, &closed);
    if (error != NEXUS_PROGRAM_ERROR_SUCCESS() || closed != FALSE) {
      break;
    }

    if (record.kind == ERK_LOG) {
      e_record_dispatch(runtime, &record);
      continue;
    }

    e_runtime_flush_sinks(runtime);
    e_runtime_barrier_complete(runtime, record.barrier_id);

    if (record.kind == ERK_SHUTDOWN) {
      break;
    }
  }

  e_worker_runtime = NULL;
  ECHO_INTERNAL_LOCK(runtime->state_mutex);
  runtime->worker_finished = TRUE;
  (void)nexus_threads_cond_broadcast(runtime->state_cond);
  ECHO_INTERNAL_UNLOCK(runtime->state_mutex);

  NEXUS_PROGRAM_CONTEXT_POP();
}

EchoRuntimeConfiguration echo_runtime_configuration_default(void) {
  EchoRuntimeConfiguration configuration;
  NEXUS_PROGRAM_CONTEXT_PUSH("echo_runtime_configuration_default");
  configuration.queue_capacity    = ECHO_QUEUE_CAPACITY_DEFAULT;
  configuration.saturation_policy = EQSP_BLOCK;
  NEXUS_PROGRAM_CONTEXT_POP();
  return configuration;
}

NexusError *echo_runtime_create(const EchoRuntimeConfiguration *configuration, EchoRuntime **out_runtime) {
  EchoRuntime             *runtime;
  EchoRuntimeConfiguration defaults;
  NexusError              *error;

  NEXUS_PROGRAM_CONTEXT_PUSH("echo_runtime_create");

  if (out_runtime == NULL) {
    NEXUS_PROGRAM_CONTEXT_POP();
    return e_echo_error(GLOBAL_ECHO_ERROR_RUNTIME_STATE, "out_runtime is null");
  }

  *out_runtime = NULL;
  defaults     = echo_runtime_configuration_default();
  if (configuration == NULL) {
    configuration = &defaults;
  }
  if (configuration->queue_capacity == 0 || configuration->queue_capacity > ((uint_large)-1) / NEXUS_SIZEOF(EchoRecord)) {
    NEXUS_PROGRAM_CONTEXT_POP();
    return e_echo_error(GLOBAL_ECHO_ERROR_RUNTIME_CAPACITY, "Echo queue capacity must be greater than zero");
  }

  runtime = (EchoRuntime *)nexus_memory_allocate(NEXUS_SIZEOF(*runtime));
  nexus_memory_bytes_clear(runtime, NEXUS_SIZEOF(*runtime));
  runtime->configuration = *configuration;

  error = nexus_threads_mutex_create(&runtime->state_mutex);
  if (error != NEXUS_PROGRAM_ERROR_SUCCESS()) {
    nexus_memory_deallocate(runtime);
    NEXUS_PROGRAM_CONTEXT_POP();
    return error;
  }
  error = nexus_threads_cond_create(&runtime->state_cond);
  if (error != NEXUS_PROGRAM_ERROR_SUCCESS()) {
    nexus_threads_mutex_destroy(runtime->state_mutex);
    nexus_memory_deallocate(runtime);
    NEXUS_PROGRAM_CONTEXT_POP();
    return error;
  }
  /* Recursive: a producer thread that's mid-send can end up re-entering echo_message_logv_at on
     itself (e.g. a diagnostic hook that logs through this same channel) -- see
     nexus_threads_channel_create_recursive. */
  error = nexus_threads_channel_create_recursive(&runtime->channel, NEXUS_SIZEOF(EchoRecord), configuration->queue_capacity);
  if (error != NEXUS_PROGRAM_ERROR_SUCCESS()) {
    nexus_threads_cond_destroy(runtime->state_cond);
    nexus_threads_mutex_destroy(runtime->state_mutex);
    nexus_memory_deallocate(runtime);
    NEXUS_PROGRAM_CONTEXT_POP();
    return error;
  }

  *out_runtime = runtime;

  NEXUS_PROGRAM_CONTEXT_POP();
  return NEXUS_PROGRAM_ERROR_SUCCESS();
}

NexusError *echo_runtime_start(EchoRuntime *runtime) {
  NexusError *error;

  NEXUS_PROGRAM_CONTEXT_PUSH("echo_runtime_start");

  if (runtime == NULL) {
    NEXUS_PROGRAM_CONTEXT_POP();
    return e_echo_error(GLOBAL_ECHO_ERROR_RUNTIME_STATE, "Echo runtime is null");
  }

  ECHO_INTERNAL_LOCK(runtime->state_mutex);
  if (runtime->starting != FALSE || runtime->started != FALSE || runtime->closing != FALSE || runtime->closed != FALSE) {
    ECHO_INTERNAL_UNLOCK(runtime->state_mutex);
    NEXUS_PROGRAM_CONTEXT_POP();
    return e_echo_error(GLOBAL_ECHO_ERROR_RUNTIME_STATE, "Echo runtime cannot be started in its current state");
  }
  runtime->starting = TRUE;
  error             = nexus_threads_create(&runtime->worker_thread, e_runtime_worker, runtime);
  if (error == NEXUS_PROGRAM_ERROR_SUCCESS()) {
    /* Startup cleanup can invoke the memory logger before the consumer enters
       e_runtime_worker. Keep producers closed until that consumer is ready. */
    while (runtime->worker_ready == FALSE) {
      (void)nexus_threads_cond_wait(runtime->state_cond, runtime->state_mutex);
    }
    runtime->started = TRUE;
  }
  runtime->starting = FALSE;
  (void)nexus_threads_cond_broadcast(runtime->state_cond);
  ECHO_INTERNAL_UNLOCK(runtime->state_mutex);

  NEXUS_PROGRAM_CONTEXT_POP();
  return error;
}

static NexusError *e_runtime_submit_barrier(EchoRuntime *runtime, EchoRecordKind kind) {
  EchoRecord  control;
  NexusError *error;
  uint64      barrier_id;

  NEXUS_PROGRAM_CONTEXT_PUSH("e_runtime_submit_barrier");

  error = e_runtime_begin_operation(runtime, TRUE);
  if (error != NEXUS_PROGRAM_ERROR_SUCCESS()) {
    NEXUS_PROGRAM_CONTEXT_POP();
    return error;
  }

  ECHO_INTERNAL_LOCK(runtime->state_mutex);
  barrier_id = ++runtime->next_barrier_id;
  ECHO_INTERNAL_UNLOCK(runtime->state_mutex);

  nexus_memory_bytes_clear(&control, NEXUS_SIZEOF(control));
  control.kind       = kind;
  control.barrier_id = barrier_id;
  error              = nexus_threads_channel_send(runtime->channel, &control);
  if (error == NEXUS_PROGRAM_ERROR_SUCCESS()) {
    ECHO_INTERNAL_LOCK(runtime->state_mutex);
    while (runtime->completed_barrier_id < barrier_id && runtime->worker_finished == FALSE) {
      (void)nexus_threads_cond_wait(runtime->state_cond, runtime->state_mutex);
    }
    if (runtime->worker_error != NULL) {
      error = runtime->worker_error;
    }
    ECHO_INTERNAL_UNLOCK(runtime->state_mutex);
  }
  e_runtime_end_operation(runtime);

  NEXUS_PROGRAM_CONTEXT_POP();
  return error;
}

NexusError *echo_runtime_flush(EchoRuntime *runtime) {
  NexusError *error;
  NEXUS_PROGRAM_CONTEXT_PUSH("echo_runtime_flush");
  error = e_runtime_submit_barrier(runtime, ERK_FLUSH);
  NEXUS_PROGRAM_CONTEXT_POP();
  return error;
}

NexusError *echo_runtime_close(EchoRuntime *runtime) {
  NexusError *error;
  NexusError *worker_error;
  EchoRecord  shutdown;
  uint64      barrier_id;

  NEXUS_PROGRAM_CONTEXT_PUSH("echo_runtime_close");

  if (runtime == NULL) {
    NEXUS_PROGRAM_CONTEXT_POP();
    return e_echo_error(GLOBAL_ECHO_ERROR_RUNTIME_STATE, "Echo runtime is null");
  }

  ECHO_INTERNAL_LOCK(runtime->state_mutex);
  while (runtime->starting != FALSE || (runtime->closing != FALSE && runtime->closed == FALSE)) {
    (void)nexus_threads_cond_wait(runtime->state_cond, runtime->state_mutex);
  }
  if (runtime->closed != FALSE) {
    error = runtime->worker_error != NULL ? runtime->worker_error : NEXUS_PROGRAM_ERROR_SUCCESS();
    ECHO_INTERNAL_UNLOCK(runtime->state_mutex);
    NEXUS_PROGRAM_CONTEXT_POP();
    return error;
  }

  runtime->closing = TRUE;
  while (runtime->active_operations != 0) {
    (void)nexus_threads_cond_wait(runtime->state_cond, runtime->state_mutex);
  }
  if (runtime->started == FALSE) {
    runtime->closed = TRUE;
    (void)nexus_threads_cond_broadcast(runtime->state_cond);
    ECHO_INTERNAL_UNLOCK(runtime->state_mutex);
    NEXUS_PROGRAM_CONTEXT_POP();
    return NEXUS_PROGRAM_ERROR_SUCCESS();
  }
  ECHO_INTERNAL_UNLOCK(runtime->state_mutex);

  ECHO_INTERNAL_LOCK(runtime->state_mutex);
  barrier_id = ++runtime->next_barrier_id;
  ECHO_INTERNAL_UNLOCK(runtime->state_mutex);

  nexus_memory_bytes_clear(&shutdown, NEXUS_SIZEOF(shutdown));
  shutdown.kind       = ERK_SHUTDOWN;
  shutdown.barrier_id = barrier_id;
  error               = nexus_threads_channel_send(runtime->channel, &shutdown);
  if (error == NEXUS_PROGRAM_ERROR_SUCCESS()) {
    ECHO_INTERNAL_LOCK(runtime->state_mutex);
    while (runtime->completed_barrier_id < barrier_id && runtime->worker_finished == FALSE) {
      (void)nexus_threads_cond_wait(runtime->state_cond, runtime->state_mutex);
    }
    ECHO_INTERNAL_UNLOCK(runtime->state_mutex);
  }
  if (error != NEXUS_PROGRAM_ERROR_SUCCESS()) {
    nexus_threads_channel_close(runtime->channel);
  }
  worker_error = nexus_threads_join(runtime->worker_thread);

  ECHO_INTERNAL_LOCK(runtime->state_mutex);
  runtime->closed          = TRUE;
  runtime->worker_finished = TRUE;
  if (error == NEXUS_PROGRAM_ERROR_SUCCESS() && worker_error != NEXUS_PROGRAM_ERROR_SUCCESS()) {
    error = worker_error;
  }
  if (error == NEXUS_PROGRAM_ERROR_SUCCESS() && runtime->worker_error != NULL) {
    error = runtime->worker_error;
  }
  (void)nexus_threads_cond_broadcast(runtime->state_cond);
  ECHO_INTERNAL_UNLOCK(runtime->state_mutex);

  NEXUS_PROGRAM_CONTEXT_POP();
  return error;
}

void echo_runtime_destroy(EchoRuntime *runtime) {
  uint64 i;

  NEXUS_PROGRAM_CONTEXT_PUSH("echo_runtime_destroy");

  if (runtime == NULL) {
    NEXUS_PROGRAM_CONTEXT_POP();
    return;
  }

  NEXUS_PROGRAM_ASSERT(runtime->closed != FALSE, "Echo runtime must be closed before destroy");
  ECHO_INTERNAL_LOCK(runtime->state_mutex);
  if (runtime->closed == FALSE) {
    ECHO_INTERNAL_UNLOCK(runtime->state_mutex);
    NEXUS_PROGRAM_CONTEXT_POP();
    return;
  }
  NEXUS_PROGRAM_ASSERT(runtime->active_operations == 0, "Echo runtime still has active operations");
  if (runtime->active_operations != 0) {
    ECHO_INTERNAL_UNLOCK(runtime->state_mutex);
    NEXUS_PROGRAM_CONTEXT_POP();
    return;
  }
  for (i = 0; i < runtime->system_count; i++) {
    NEXUS_PROGRAM_ASSERT(runtime->systems[i]->context_reference_count == 0, "Echo runtime still has active contexts");
    if (runtime->systems[i]->context_reference_count != 0) {
      ECHO_INTERNAL_UNLOCK(runtime->state_mutex);
      NEXUS_PROGRAM_CONTEXT_POP();
      return;
    }
  }
  ECHO_INTERNAL_UNLOCK(runtime->state_mutex);

  for (i = 0; i < runtime->sink_count; i++) {
    e_sink_destroy_internal(runtime->sinks[i]);
  }
  for (i = 0; i < runtime->system_count; i++) {
    nexus_memory_deallocate(runtime->systems[i]);
  }
  if (runtime->sinks != NULL) {
    nexus_memory_deallocate((void *)runtime->sinks);
  }
  if (runtime->systems != NULL) {
    nexus_memory_deallocate((void *)runtime->systems);
  }
  nexus_threads_channel_destroy(runtime->channel);
  nexus_threads_cond_destroy(runtime->state_cond);
  nexus_threads_mutex_destroy(runtime->state_mutex);
  nexus_memory_deallocate(runtime);

  NEXUS_PROGRAM_CONTEXT_POP();
}

NexusError *echo_runtime_statistics_get(EchoRuntime *runtime, EchoRuntimeStatistics *out_statistics) {
  NEXUS_PROGRAM_CONTEXT_PUSH("echo_runtime_statistics_get");

  if (runtime == NULL || out_statistics == NULL) {
    NEXUS_PROGRAM_CONTEXT_POP();
    return e_echo_error(GLOBAL_ECHO_ERROR_RUNTIME_STATE, "Runtime and output statistics must be non-null");
  }

  ECHO_INTERNAL_LOCK(runtime->state_mutex);
  *out_statistics = runtime->statistics;
  ECHO_INTERNAL_UNLOCK(runtime->state_mutex);

  NEXUS_PROGRAM_CONTEXT_POP();
  return NEXUS_PROGRAM_ERROR_SUCCESS();
}

NexusError *echo_runtime_filter_set(EchoRuntime *runtime, EchoFilterFunction *filter, void *user_data) {
  NexusError *error;

  NEXUS_PROGRAM_CONTEXT_PUSH("echo_runtime_filter_set");
  error = e_runtime_begin_operation(runtime, FALSE);

  if (error != NEXUS_PROGRAM_ERROR_SUCCESS()) {
    NEXUS_PROGRAM_CONTEXT_POP();
    return error;
  }

  ECHO_INTERNAL_LOCK(runtime->state_mutex);
  runtime->filter           = filter;
  runtime->filter_user_data = user_data;
  ECHO_INTERNAL_UNLOCK(runtime->state_mutex);
  e_runtime_end_operation(runtime);

  NEXUS_PROGRAM_CONTEXT_POP();
  return NEXUS_PROGRAM_ERROR_SUCCESS();
}

NexusError *echo_runtime_filter_disable(EchoRuntime *runtime) {
  NexusError *error;
  NEXUS_PROGRAM_CONTEXT_PUSH("echo_runtime_filter_disable");
  error = echo_runtime_filter_set(runtime, NULL, NULL);
  NEXUS_PROGRAM_CONTEXT_POP();
  return error;
}
