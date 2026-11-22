#include <nexus.h>
#include <echo.h>

#include <setjmp.h>

static NexusError *echo_test_submit(const EchoRecord *record, void *user_data) {
  uint32 *count = (uint32 *)user_data;
  (void)record;
  *count += 1;
  return NEXUS_PROGRAM_ERROR_SUCCESS();
}

static NexusError *echo_test_flush(void *user_data) {
  (void)user_data;
  return NEXUS_PROGRAM_ERROR_SUCCESS();
}

static void echo_test_destroy(void *user_data) {
  (void)user_data;
}

typedef struct EchoTestProducerArguments {
  EchoContext *context;
  uint32       count;
} EchoTestProducerArguments;

static void echo_test_producer(void *user_data) {
  EchoTestProducerArguments *arguments = (EchoTestProducerArguments *)user_data;
  uint32                     i;
  for (i = 0; i < arguments->count; i++) {
    (void)echo_debug(arguments->context, "worker=%u", i);
  }
}

#if NEXUS_MEMORY_TRACING
static int echo_test_memory_callback_startup(void) {
  EchoRuntimeConfiguration    configuration;
  EchoCustomSinkConfiguration sink_configuration;
  EchoRuntime                *runtime;
  EchoSystem                 *system;
  EchoContext                *context;
  EchoSink                   *sink;
  EchoRuntimeStatistics       statistics;
  NexusError                 *error;
  void                       *allocation;
  uint32                      count;
  uint32                      iteration;
  uint32                      i;

  configuration                       = echo_runtime_configuration_default();
  configuration.queue_capacity        = 1;
  sink_configuration.filter           = NULL;
  sink_configuration.filter_user_data = NULL;
  sink_configuration.submit           = echo_test_submit;
  sink_configuration.flush            = echo_test_flush;
  sink_configuration.destroy          = echo_test_destroy;
  sink_configuration.user_data        = &count;

  /* A startup trace must never block the worker on its own undrained queue. */
  for (iteration = 0; iteration < 16U; iteration++) {
    count = 0;
    error = echo_runtime_create(&configuration, &runtime);
    if (error != NEXUS_PROGRAM_ERROR_SUCCESS())
      return 40;
    error = echo_runtime_system_register(runtime, "startup", NULL, &system);
    if (error != NEXUS_PROGRAM_ERROR_SUCCESS())
      return 41;
    error = echo_context_create(runtime, system, &context);
    if (error != NEXUS_PROGRAM_ERROR_SUCCESS())
      return 42;
    error = echo_sink_custom_create(&sink_configuration, &sink);
    if (error != NEXUS_PROGRAM_ERROR_SUCCESS())
      return 43;
    error = echo_runtime_sink_register(runtime, sink);
    if (error != NEXUS_PROGRAM_ERROR_SUCCESS())
      return 44;
    error = echo_nexus_memory_log_callback_install(context);
    if (error != NEXUS_PROGRAM_ERROR_SUCCESS())
      return 45;
    error = echo_runtime_start(runtime);
    if (error != NEXUS_PROGRAM_ERROR_SUCCESS())
      return 46;

    for (i = 0; i < 64U; i++) {
      allocation = nexus_memory_allocate(32U);
      nexus_memory_deallocate(allocation);
    }
    echo_nexus_memory_log_callback_uninstall();
    error = echo_runtime_flush(runtime);
    if (error != NEXUS_PROGRAM_ERROR_SUCCESS())
      return 47;
    error = echo_runtime_statistics_get(runtime, &statistics);
    if (error != NEXUS_PROGRAM_ERROR_SUCCESS() || statistics.admitted_count != 128U || count != 128U)
      return 48;
    error = echo_runtime_close(runtime);
    if (error != NEXUS_PROGRAM_ERROR_SUCCESS())
      return 49;
    echo_context_destroy(context);
    echo_runtime_destroy(runtime);
  }
  return 0;
}
#endif

int main(void) {
  EchoRuntimeConfiguration    configuration;
  EchoRuntime                *runtime;
  EchoSystem                 *root;
  EchoSystem                 *child;
  EchoContext                *context;
  EchoSink                   *sinks[14];
  EchoSink                   *terminal_sink;
  EchoSink                   *file_sink;
  EchoCustomSinkConfiguration sink_configuration;
  EchoRuntimeStatistics       statistics;
  EchoField                   field;
  NexusPath                   file_path;
  void                       *traced_allocation;
  EchoTestProducerArguments   producer_arguments;
  NexusThread                *producer_threads[4];
  uint32                      count;
  uint32                      records_before_panic;
  jmp_buf                     recovery_point;
  uint32                      i;
  NexusError                 *error;
  uint64                      expected_admitted_count;

#if NEXUS_MEMORY_TRACING
  int startup_result;

  startup_result = echo_test_memory_callback_startup();
  if (startup_result != 0)
    return startup_result;
#endif

  count                   = 0;
  expected_admitted_count = 402;
#if NEXUS_MEMORY_TRACING
  expected_admitted_count = 404;
#endif
  configuration                = echo_runtime_configuration_default();
  configuration.queue_capacity = 8;
  error                        = echo_runtime_create(&configuration, &runtime);
  if (error != NEXUS_PROGRAM_ERROR_SUCCESS())
    return 1;
  error = echo_runtime_system_register(runtime, "test", NULL, &root);
  if (error != NEXUS_PROGRAM_ERROR_SUCCESS())
    return 2;
  error = echo_runtime_system_register(runtime, "worker", root, &child);
  if (error != NEXUS_PROGRAM_ERROR_SUCCESS())
    return 3;
  error = echo_context_create(runtime, child, &context);
  if (error != NEXUS_PROGRAM_ERROR_SUCCESS())
    return 4;

  /* A system is its handle, not its label: a second "worker" under the same parent registers, is a different system, and a level change on one
     (or on the parent) reaches only what is registered below it. Unregistering a parent re-parents what is left below it. */
  {
    EchoSystem *twin;
    EchoSystem *grandchild;

    error = echo_runtime_system_register(runtime, "worker", root, &twin);
    if (error != NEXUS_PROGRAM_ERROR_SUCCESS() || twin == child)
      return 30;
    error = echo_runtime_system_register(runtime, "leaf", child, &grandchild);
    if (error != NEXUS_PROGRAM_ERROR_SUCCESS())
      return 31;

    error = echo_system_level_set(child, ELL_ERROR, ESLP_OVERRIDE);
    if (error != NEXUS_PROGRAM_ERROR_SUCCESS() || echo_system_level_get(child) != ELL_ERROR || echo_system_level_get(grandchild) != ELL_ERROR ||
        echo_system_level_get(twin) != ELL_TRACE || echo_system_level_get(root) != ELL_TRACE)
      return 32;

    error = echo_system_level_set(root, ELL_WARNING, ESLP_OVERRIDE);
    if (error != NEXUS_PROGRAM_ERROR_SUCCESS() || echo_system_level_get(twin) != ELL_WARNING || echo_system_level_get(grandchild) != ELL_WARNING)
      return 33;

    /* Unregister the middle system's twin first (it has no contexts), then check the grandchild still follows the parent that remains. */
    error = echo_runtime_system_unregister(runtime, twin);
    if (error != NEXUS_PROGRAM_ERROR_SUCCESS())
      return 34;
    error = echo_system_level_set(root, ELL_TRACE, ESLP_OVERRIDE);
    if (error != NEXUS_PROGRAM_ERROR_SUCCESS() || echo_system_level_get(child) != ELL_TRACE || echo_system_level_get(grandchild) != ELL_TRACE)
      return 35;
    error = echo_runtime_system_unregister(runtime, grandchild);
    if (error != NEXUS_PROGRAM_ERROR_SUCCESS())
      return 36;
  }
  error = echo_context_span_push(context, "startup");
  if (error != NEXUS_PROGRAM_ERROR_SUCCESS())
    return 5;
  field = echo_field_string_create("component", "echo");
  error = echo_context_field_set(context, field);
  if (error != NEXUS_PROGRAM_ERROR_SUCCESS())
    return 6;

  sink_configuration.filter           = NULL;
  sink_configuration.filter_user_data = NULL;
  sink_configuration.submit           = echo_test_submit;
  sink_configuration.flush            = echo_test_flush;
  sink_configuration.destroy          = echo_test_destroy;
  sink_configuration.user_data        = &count;
  for (i = 0; i < 12; i++) {
    error = echo_sink_custom_create(&sink_configuration, &sinks[i]);
    if (error != NEXUS_PROGRAM_ERROR_SUCCESS())
      return 7;
    error = echo_runtime_sink_register(runtime, sinks[i]);
    if (error != NEXUS_PROGRAM_ERROR_SUCCESS())
      return 8;
  }
  file_path = nexus_filesystem_path_create("/tmp/echo-library-test.log");
  error     = echo_sink_simple_terminal_create(NULL, NULL, NTP_MILLI_SECOND, &terminal_sink);
  if (error != NEXUS_PROGRAM_ERROR_SUCCESS())
    return 13;
  error = echo_runtime_sink_register(runtime, terminal_sink);
  if (error != NEXUS_PROGRAM_ERROR_SUCCESS())
    return 14;
  error = echo_sink_simple_file_create(NULL, NULL, NTP_MILLI_SECOND, &file_path, &file_sink);
  if (error != NEXUS_PROGRAM_ERROR_SUCCESS())
    return 15;
  error = echo_runtime_sink_register(runtime, file_sink);
  if (error != NEXUS_PROGRAM_ERROR_SUCCESS())
    return 16;
  error = echo_runtime_start(runtime);
  if (error != NEXUS_PROGRAM_ERROR_SUCCESS())
    return 9;
  (void)echo_info(context, "hello %s", "world");
  error = echo_runtime_flush(runtime);
  if (error != NEXUS_PROGRAM_ERROR_SUCCESS())
    return 10;
  error = echo_runtime_statistics_get(runtime, &statistics);
  if (error != NEXUS_PROGRAM_ERROR_SUCCESS() || statistics.admitted_count != 1 || count != 12)
    return 11;
  error = echo_nexus_memory_log_callback_install(context);
  if (error != NEXUS_PROGRAM_ERROR_SUCCESS())
    return 22;
  traced_allocation = nexus_memory_allocate(32);
  nexus_memory_deallocate(traced_allocation);
  echo_nexus_memory_log_callback_uninstall();
  error = echo_nexus_assertions_callback_install(context);
  if (error != NEXUS_PROGRAM_ERROR_SUCCESS())
    return 24;
  /* A panic must reach the sinks before the process would abort, with no flush from the caller. */
  records_before_panic = count;
  if (setjmp(recovery_point) == 0) {
    nexus_program_recovery_arm(&recovery_point);
    NEXUS_PROGRAM_PANIC("echo flush-on-panic test");
    nexus_program_recovery_disarm();
    return 25;
  }
  nexus_program_recovery_disarm();
  if (count < records_before_panic + 12)
    return 26;
  echo_nexus_assertions_callback_uninstall();
  error = echo_runtime_flush(runtime);
  if (error != NEXUS_PROGRAM_ERROR_SUCCESS())
    return 23;
  error = echo_sink_enabled_set(terminal_sink, FALSE);
  if (error != NEXUS_PROGRAM_ERROR_SUCCESS())
    return 21;
  producer_arguments.context = context;
  producer_arguments.count   = 100;
  for (i = 0; i < 4; i++) {
    error = nexus_threads_create(&producer_threads[i], echo_test_producer, &producer_arguments);
    if (error != NEXUS_PROGRAM_ERROR_SUCCESS())
      return 17;
  }
  for (i = 0; i < 4; i++) {
    error = nexus_threads_join(producer_threads[i]);
    if (error != NEXUS_PROGRAM_ERROR_SUCCESS())
      return 18;
  }
  error = echo_runtime_flush(runtime);
  if (error != NEXUS_PROGRAM_ERROR_SUCCESS())
    return 19;
  error = echo_runtime_statistics_get(runtime, &statistics);
  if (error != NEXUS_PROGRAM_ERROR_SUCCESS() || statistics.admitted_count != expected_admitted_count)
    return 20;
  echo_context_destroy(context);
  error = echo_runtime_close(runtime);
  if (error != NEXUS_PROGRAM_ERROR_SUCCESS())
    return 12;
  echo_runtime_destroy(runtime);
  (void)nexus_filesystem_file_delete(&file_path);
  (void)root;
  return 0;
}
