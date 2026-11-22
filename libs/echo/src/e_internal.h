#pragma once

#include <nexus.h>

#include "../echo.h"

static NEXUS_MAYBE_UNUSED boolean e_string_equals(const char *left, const char *right) {
  return nexus_strings_string_equals(left, right);
}

static NEXUS_MAYBE_UNUSED boolean e_string_contains_char(const char *string, char value) {
  while (*string != '\0') {
    if (*string == value)
      return TRUE;
    string++;
  }
  return FALSE;
}

static NEXUS_MAYBE_UNUSED boolean e_string_contains(const char *string, const char *needle) {
  uint64 needle_length;
  uint64 string_length;
  uint64 i;
  uint64 j;
  needle_length = nexus_strings_string_length_get(needle);
  string_length = nexus_strings_string_length_get(string);
  if (needle_length == 0)
    return TRUE;
  if (needle_length > string_length)
    return FALSE;
  for (i = 0; i + needle_length <= string_length; i++) {
    for (j = 0; j < needle_length && string[i + j] == needle[j]; j++) {
    }
    if (j == needle_length)
      return TRUE;
  }
  return FALSE;
}

static NEXUS_MAYBE_UNUSED boolean e_string_starts_with(const char *string, const char *prefix) {
  uint64 i;
  for (i = 0; prefix[i] != '\0'; i++) {
    if (string[i] != prefix[i])
      return FALSE;
  }
  return TRUE;
}

static NEXUS_MAYBE_UNUSED int e_string_compare(const char *left, const char *right) {
  while (*left != '\0' && *left == *right) {
    left++;
    right++;
  }
  return (unsigned char)*left - (unsigned char)*right;
}

typedef enum EchoRecordKind {
  ERK_LOG,
  ERK_FLUSH,
  ERK_SHUTDOWN
} EchoRecordKind;

typedef struct EchoStoredField {
  char          key[ECHO_FIELD_KEY_LENGTH_MAX];
  EchoFieldType type;
  union {
    char    string_value[ECHO_FIELD_STRING_LENGTH_MAX];
    int64   int64_value;
    uint64  uint64_value;
    f_real  float_value;
    boolean boolean_value;
  } value;
} EchoStoredField;

struct EchoRecord {
  EchoRecordKind  kind;
  EchoLogLevel    level;
  NexusTime       timestamp;
  NexusTime       monotonic_timestamp;
  char            message[ECHO_MESSAGE_LENGTH_MAX];
  char            origin_file[ECHO_ORIGIN_FILE_LENGTH_MAX];
  uint32          origin_line;
  char            system_label[ECHO_SYSTEM_LABEL_LENGTH_MAX];
  char            spans[ECHO_SPAN_COUNT_MAX][ECHO_SPAN_LENGTH_MAX];
  uint8           span_count;
  EchoStoredField fields[ECHO_FIELD_COUNT_MAX];
  uint8           field_count;
  boolean         truncated;
  uint64          barrier_id;
  EchoSink       *control_sink;
  boolean         control_enabled;
};

typedef struct EchoSinkCallbacks {
  EchoFilterFunction      *filter;
  void                    *filter_user_data;
  EchoSinkSubmitFunction  *submit;
  EchoSinkFlushFunction   *flush;
  EchoSinkDestroyFunction *destroy;
  void                    *user_data;
} EchoSinkCallbacks;

struct EchoSink {
  EchoRuntime                  *runtime;
  EchoSinkCallbacks             callbacks;
  NexusAtomicUint32             enabled;
  uint32                        kind;
  NexusMutex                   *configuration_mutex;
  NexusTimePrecision            precision;
  EchoRichTerminalConfiguration rich_terminal_configuration;
  NexusPath                     rich_file_path;
  boolean                       registered;
};

struct EchoSystem {
  EchoRuntime *runtime;
  EchoSystem  *parent; /* NULL for a root; the identity of the tree, since labels may repeat among siblings */
  EchoLogLevel min_level;
  EchoLogLevel last_min_level;
  uint64       context_reference_count;
  char         label[ECHO_SYSTEM_LABEL_LENGTH_MAX];
};

struct EchoContext {
  EchoRuntime    *runtime;
  EchoSystem     *system;
  char            spans[ECHO_SPAN_COUNT_MAX][ECHO_SPAN_LENGTH_MAX];
  uint8           span_count;
  EchoStoredField fields[ECHO_FIELD_COUNT_MAX];
  uint8           field_count;
  uint64          active_operations;
  boolean         destroying;
};

struct EchoRuntime {
  EchoRuntimeConfiguration configuration;
  NexusMutex              *state_mutex;
  NexusCond               *state_cond;
  NexusChannel            *channel;
  NexusThread             *worker_thread;
  boolean                  starting;
  boolean                  worker_ready;
  boolean                  started;
  boolean                  closing;
  boolean                  closed;
  boolean                  worker_finished;
  uint64                   active_operations;
  uint64                   next_barrier_id;
  uint64                   completed_barrier_id;
  EchoFilterFunction      *filter;
  void                    *filter_user_data;
  EchoSink               **sinks;
  uint64                   sink_count;
  uint64                   sink_capacity;
  EchoSystem             **systems;
  uint64                   system_count;
  uint64                   system_capacity;
  EchoRuntimeStatistics    statistics;
  NexusError              *worker_error;
};

extern NEXUS_THREAD_LOCAL EchoRuntime *e_worker_runtime;

/*
Summary: the runtime this thread is currently inside nexus_threads_channel_send/try_send for, or
  NULL. Catches the case echo_worker_runtime can't: a nexus_memory_* trace callback fired by the
  channel's own internal copy of the record being sent, recursing back into logging on the same
  thread that still holds the channel's mutex.
*/
extern NEXUS_THREAD_LOCAL EchoRuntime *e_sending_runtime;

/*
Summary: builds a NexusError tagged with the calling site's own __FILE__/__LINE__, not e_echo_error's.
*/
NexusError *e_echo_error(NexusErrorCode code, const char *message);

/*
Summary: doubles *capacity (from 8) and reallocates *array to match; a no-op capacity check belongs to the caller.
*/
NexusError *e_runtime_capacity_grow(void **array, uint64 *capacity, uint_large element_size);

/*
Summary: TRUE if runtime is non-null and neither closing nor closed. Caller must already hold runtime->state_mutex.
*/
boolean e_runtime_is_valid_locked(EchoRuntime *runtime);

/*
Summary: registers one in-flight public call against runtime, so echo_runtime_close can drain before tearing down state.
*/
NexusError *e_runtime_begin_operation(EchoRuntime *runtime, boolean require_started);
void        e_runtime_end_operation(EchoRuntime *runtime);

void e_runtime_barrier_complete(EchoRuntime *runtime, uint64 barrier_id);
void e_runtime_worker_set_error(EchoRuntime *runtime, NexusError *error);
void e_runtime_flush_sinks(EchoRuntime *runtime);

/*
Summary: runs the runtime-wide filter then every enabled sink's per-sink filter and submit for one record; escalates ELL_CRITICAL into an immediate e_runtime_flush_sinks.
*/
void e_record_dispatch(EchoRuntime *runtime, const EchoRecord *record);

void e_sink_destroy_internal(EchoSink *sink);

#define ECHO_INTERNAL_LOCK(mutex)   (void)nexus_threads_mutex_lock((mutex))
#define ECHO_INTERNAL_UNLOCK(mutex) (void)nexus_threads_mutex_unlock((mutex))
