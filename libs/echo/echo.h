/*
Some of the implementation in this library is LLM-generated.
Echo is reused as foundational infrastructure rather than rewritten from scratch.
*/

/*
Summary: Echo turns logging attempts into bounded asynchronous sink records.
Layer: logging above Nexus.
See: DESIGN.md, adr/0003-accept-variadic-macros-as-a-c89-exception
*/
#pragma once

#include <stdarg.h>

#include <nexus.h>

/*
Summary: the Echo library identifier used by every Echo error code.
*/
#define ECHO_ERROR_LIBRARY_IDENTIFIER NEXUS_PROGRAM_ERROR_LIBRARY_IDENTIFIER_CREATE('E', 'C', 'H', 'O')

/*
Summary: the facility identifier for EchoRuntimeError.
*/
#define ECHO_ERROR_RUNTIME_FACILITY NEXUS_PROGRAM_ERROR_FACILITY_IDENTIFIER_CREATE('R', 'T', 'M', 'E')

/*
Summary: categories within EchoRuntimeError; currently just EREC_RUNTIME.
*/
typedef enum EchoRuntimeErrorCategory {
  EREC_RUNTIME,

  EREC_COUNT
} EchoRuntimeErrorCategory;

/*
Summary: EchoRuntimeError's own error list, defining EchoRuntimeError via NEXUS_ERROR_FACILITY_DEFINE below.
*/
#define ECHO_RUNTIME_ERROR_LIST(X, library, facility)                                                                                                \
  X(ECHO_ERROR_RUNTIME_STATE, EREC_RUNTIME, 1, library, facility)                                                                                    \
  X(ECHO_ERROR_RUNTIME_QUEUE_CLOSED, EREC_RUNTIME, 2, library, facility)                                                                             \
  X(ECHO_ERROR_RUNTIME_CAPACITY, EREC_RUNTIME, 3, library, facility)

NEXUS_ERROR_FACILITY_DEFINE(EchoRuntimeError, ECHO_ERROR_LIBRARY_IDENTIFIER, ECHO_ERROR_RUNTIME_FACILITY, ECHO_RUNTIME_ERROR_LIST)

/*
Summary: the facility identifier for EchoSinkError.
*/
#define ECHO_ERROR_SINK_FACILITY NEXUS_PROGRAM_ERROR_FACILITY_IDENTIFIER_CREATE('S', 'I', 'N', 'K')

/*
Summary: categories within EchoSinkError; currently just ESEC_SINK.
*/
typedef enum EchoSinkErrorCategory {
  ESEC_SINK,

  ESINK_COUNT
} EchoSinkErrorCategory;

/*
Summary: EchoSinkError's own error list, defining EchoSinkError via NEXUS_ERROR_FACILITY_DEFINE below.
*/
#define ECHO_SINK_ERROR_LIST(X, library, facility) X(ECHO_ERROR_SINK_INVALID, ESEC_SINK, 1, library, facility)

NEXUS_ERROR_FACILITY_DEFINE(EchoSinkError, ECHO_ERROR_LIBRARY_IDENTIFIER, ECHO_ERROR_SINK_FACILITY, ECHO_SINK_ERROR_LIST)

/*
Summary: the facility identifier for EchoSystemError.
*/
#define ECHO_ERROR_SYSTEM_FACILITY NEXUS_PROGRAM_ERROR_FACILITY_IDENTIFIER_CREATE('S', 'Y', 'S', 'T')

/*
Summary: categories within EchoSystemError; currently just ESEC_SYSTEM.
*/
typedef enum EchoSystemErrorCategory {
  ESEC_SYSTEM,

  ESYSTEM_COUNT
} EchoSystemErrorCategory;

/*
Summary: EchoSystemError's own error list, defining EchoSystemError via NEXUS_ERROR_FACILITY_DEFINE below.
*/
#define ECHO_SYSTEM_ERROR_LIST(X, library, facility)                                                                                                 \
  X(ECHO_ERROR_SYSTEM_INVALID, ESEC_SYSTEM, 1, library, facility)                                                                                    \
  X(ECHO_ERROR_SYSTEM_IN_USE, ESEC_SYSTEM, 3, library, facility)

NEXUS_ERROR_FACILITY_DEFINE(EchoSystemError, ECHO_ERROR_LIBRARY_IDENTIFIER, ECHO_ERROR_SYSTEM_FACILITY, ECHO_SYSTEM_ERROR_LIST)

/*
Summary: compile-time capacity and length ceilings; define any of these before including echo.h to override the default.
*/
#ifndef ECHO_MESSAGE_LENGTH_MAX
#  define ECHO_MESSAGE_LENGTH_MAX 1024
#endif

#ifndef ECHO_ORIGIN_FILE_LENGTH_MAX
#  define ECHO_ORIGIN_FILE_LENGTH_MAX 512
#endif

#ifndef ECHO_SYSTEM_LABEL_LENGTH_MAX
#  define ECHO_SYSTEM_LABEL_LENGTH_MAX 30
#endif

#ifndef ECHO_SPAN_LENGTH_MAX
#  define ECHO_SPAN_LENGTH_MAX 64
#endif

#ifndef ECHO_SPAN_COUNT_MAX
#  define ECHO_SPAN_COUNT_MAX 16
#endif

#ifndef ECHO_FIELD_KEY_LENGTH_MAX
#  define ECHO_FIELD_KEY_LENGTH_MAX 64
#endif

#ifndef ECHO_FIELD_STRING_LENGTH_MAX
#  define ECHO_FIELD_STRING_LENGTH_MAX 256
#endif

#ifndef ECHO_FIELD_COUNT_MAX
#  define ECHO_FIELD_COUNT_MAX 16
#endif

#ifndef ECHO_QUEUE_CAPACITY_DEFAULT
#  define ECHO_QUEUE_CAPACITY_DEFAULT 256
#endif

#ifndef ECHO_FILE_FLUSH_THRESHOLD
#  define ECHO_FILE_FLUSH_THRESHOLD 50
#endif

/*
Summary: an owned, started, stoppable Echo logging runtime -- the transport, worker thread, and registries.
*/
typedef struct EchoRuntime EchoRuntime;
/*
Summary: a node in Echo's hierarchical system tree, holding its own effective log level.
*/
typedef struct EchoSystem EchoSystem;
/*
Summary: a producer's logging handle -- bound to one system, carrying the span stack and structured fields it attaches to every record.
*/
typedef struct EchoContext EchoContext;
/*
Summary: a registered destination that receives dispatched records -- terminal, file, or custom.
*/
typedef struct EchoSink EchoSink;
/*
Summary: one bounded, fully self-contained log record as observed by a filter or custom sink; opaque, read through the echo_record_* accessors.
*/
typedef struct EchoRecord EchoRecord;

/*
Summary: a producer-side log severity, from ELL_TRACE (least severe) to ELL_CRITICAL (most severe).
*/
typedef enum EchoLogLevel {
  ELL_TRACE,
  ELL_DEBUG,
  ELL_INFO,
  ELL_NOTICE,
  ELL_WARNING,
  ELL_ERROR,
  ELL_CRITICAL,

  ELL_COUNT
} EchoLogLevel;

/*
Summary: how echo_system_level_set resolves a new level against a system subtree's current levels.
Values:
  ESLP_MOST_RESTRICTIVE   keep the stricter (higher-severity-only) of the current and new level
  ESLP_LEAST_RESTRICTIVE  keep the looser (lower-severity-inclusive) of the current and new level
  ESLP_OVERRIDE           replace the current level unconditionally
  ESLP_REVERT             restore the level each affected system had before its last non-revert change
  ESLP_LOCAL_ONLY         like ESLP_OVERRIDE, but affects only the named system, never its descendants
*/
typedef enum EchoSystemLevelPolicy {
  ESLP_MOST_RESTRICTIVE,
  ESLP_LEAST_RESTRICTIVE,
  ESLP_OVERRIDE,
  ESLP_REVERT,
  ESLP_LOCAL_ONLY
} EchoSystemLevelPolicy;

/*
Summary: how the bounded transport behaves when its queue is full.
Description: EQSP_DROP_NEWEST only ever silences ELL_WARNING and below -- ELL_ERROR and
  ELL_CRITICAL records always use blocking admission under either policy, so a saturated
  queue can never silently drop a diagnostic severe enough to matter.
*/
typedef enum EchoQueueSaturationPolicy {
  EQSP_BLOCK,
  EQSP_DROP_NEWEST
} EchoQueueSaturationPolicy;

/*
Summary: what became of one echo_message_log_at-family call -- the echoed/silenced binary distinction from DESIGN.md, split into its concrete reasons.
*/
typedef enum EchoLogOutcome {
  ELO_ECHOED,
  ELO_ECHOED_TRUNCATED,
  ELO_SILENCED_LEVEL,
  ELO_SILENCED_DROPPED,
  ELO_SILENCED_CLOSED,
  ELO_SILENCED_RECURSIVE
} EchoLogOutcome;

/*
Summary: the value kind carried by an EchoField.
*/
typedef enum EchoFieldType {
  EFT_STRING,
  EFT_INT64,
  EFT_UINT64,
  EFT_FLOAT,
  EFT_BOOLEAN
} EchoFieldType;

/*
Summary: one structured key/value attached to a context or a single log call.
Fields:
  key    the field's name; borrowed at echo_context_field_set/echo_message_log_fields_at time, copied into bounded storage
  type   which member of value is live
  value  the field's payload; string_value is borrowed the same way key is
*/
typedef struct EchoField {
  const char   *key;
  EchoFieldType type;
  union {
    const char *string_value;
    int64       int64_value;
    uint64      uint64_value;
    f_real      float_value;
    boolean     boolean_value;
  } value;
} EchoField;

/*
Summary: tunable parameters for echo_runtime_create; echo_runtime_configuration_default gives the baseline.
Fields:
  queue_capacity     bounded transport capacity in records; must be non-zero
  saturation_policy  see EchoQueueSaturationPolicy
*/
typedef struct EchoRuntimeConfiguration {
  uint_large                queue_capacity;
  EchoQueueSaturationPolicy saturation_policy;
} EchoRuntimeConfiguration;

/*
Summary: cumulative counters for one runtime's logging traffic, read via echo_runtime_statistics_get.
Fields:
  attempted_count       every echo_message_log_at-family call, regardless of outcome
  admitted_count        attempts that reached the transport (ELO_ECHOED or ELO_ECHOED_TRUNCATED)
  level_silenced_count  attempts below the effective system level (ELO_SILENCED_LEVEL)
  filtered_count        attempts rejected by the runtime-wide filter
  sink_filtered_count   admitted records rejected by a per-sink filter (counted once per rejecting sink)
  dropped_count         attempts silenced by EQSP_DROP_NEWEST saturation (ELO_SILENCED_DROPPED)
  recursive_count       attempts silenced by recursion protection (ELO_SILENCED_RECURSIVE)
  truncated_count       admitted records whose message or a field value was truncated to fit
  sink_failure_count    sink callback invocations (submit or flush) that returned a real NexusError
*/
typedef struct EchoRuntimeStatistics {
  uint64 attempted_count;
  uint64 admitted_count;
  uint64 level_silenced_count;
  uint64 filtered_count;
  uint64 sink_filtered_count;
  uint64 dropped_count;
  uint64 recursive_count;
  uint64 truncated_count;
  uint64 sink_failure_count;
} EchoRuntimeStatistics;

/*
Summary: decides whether a record is admitted (runtime-wide) or dispatched to one sink; TRUE accepts.
Description: runs on Echo's worker thread, not the producer thread. Must not log through the
  runtime it is filtering for -- see DESIGN.md § Autopoietic Execution on recursion.
*/
typedef boolean EchoFilterFunction(const EchoRecord *record, void *user_data);

/*
Summary: delivers one accepted record to a custom sink.
Returns: a real NexusError to report a delivery failure (counted in sink_failure_count and surfaced through echo_runtime_close); NEXUS_PROGRAM_ERROR_SUCCESS() otherwise.
Thread-safety: called only from Echo's worker thread, never concurrently with itself or with the same sink's flush/destroy.
*/
typedef NexusError *EchoSinkSubmitFunction(const EchoRecord *record, void *user_data);

/*
Summary: flushes a custom sink's buffered output; called on echo_runtime_flush and on every ELL_CRITICAL record. May be NULL if the sink has nothing to buffer.
Thread-safety: see EchoSinkSubmitFunction.
*/
typedef NexusError *EchoSinkFlushFunction(void *user_data);

/*
Summary: releases a custom sink's user_data; called once, after the sink is unreachable from any runtime. May be NULL if user_data needs no cleanup.
Thread-safety: see EchoSinkSubmitFunction.
*/
typedef void EchoSinkDestroyFunction(void *user_data);

/*
Summary: the callback set and user_data passed to echo_sink_custom_create.
Ownership: user_data is borrowed by Echo and handed back unchanged to filter/submit/flush/destroy; destroy is the caller's one chance to release it.
Fields:
  filter             optional; see EchoFilterFunction
  filter_user_data   passed to filter
  submit             required; see EchoSinkSubmitFunction
  flush              optional; see EchoSinkFlushFunction
  destroy            optional; see EchoSinkDestroyFunction
  user_data          passed to submit/flush/destroy
*/
typedef struct EchoCustomSinkConfiguration {
  EchoFilterFunction      *filter;
  void                    *filter_user_data;
  EchoSinkSubmitFunction  *submit;
  EchoSinkFlushFunction   *flush;
  EchoSinkDestroyFunction *destroy;
  void                    *user_data;
} EchoCustomSinkConfiguration;

/*
Summary: how a built-in terminal sink derives an EchoColor's escape sequence -- none, 8/16-color ANSI, or 24-bit RGB.
*/
typedef enum EchoTerminalColorMode {
  ETCM_NONE,
  ETCM_ANSI,
  ETCM_RGB
} EchoTerminalColorMode;

/*
Summary: one presentation color for the rich terminal sink; construct with echo_color_none_create/ansi_create/rgb_create rather than setting fields directly.
Fields:
  mode       which of ansi_code / red,green,blue is meaningful; see EchoTerminalColorMode
  ansi_code  used when mode == ETCM_ANSI; one of the ECHO_TERMINAL_ANSI_* constants
  red, green, blue  used when mode == ETCM_RGB
*/
typedef struct EchoColor {
  EchoTerminalColorMode mode;
  uint8                 ansi_code;
  uint8                 red;
  uint8                 green;
  uint8                 blue;
} EchoColor;

/*
Summary: presentation settings for a rich terminal sink; build one with echo_rich_terminal_configuration_plain_create/ansi_create/rgb_create.
Fields:
  display_precision  sub-second precision shown in the timestamp; see NexusTimePrecision
  color_time, color_file, color_system  colors for the fixed prefix fields
  color_levels       one color per EchoLogLevel, indexed by level
*/
typedef struct EchoRichTerminalConfiguration {
  NexusTimePrecision display_precision;
  EchoColor          color_time;
  EchoColor          color_file;
  EchoColor          color_system;
  EchoColor          color_levels[ELL_COUNT];
} EchoRichTerminalConfiguration;

/*
Summary: configuration for echo_sink_rich_file_create -- a rotating, timestamped log file.
Description: on creation, Echo lists base_directory for files matching log_prefix and, if
  max_logs is non-zero and already exceeded, deletes the oldest of them before opening the
  new file (see e_rich_file_paths_sort's lexical ordering, which relies on the fixed
  zero-padded timestamp format e_rich_file_create writes into the filename).
Fields:
  filter             optional; see EchoFilterFunction
  filter_user_data   passed to filter
  base_directory     directory the log file (and its prior rotations) live in; created if missing
  max_logs           0 disables rotation; otherwise the number of log files kept for log_prefix
  display_precision  see EchoRichTerminalConfiguration
  log_prefix         filename prefix and the substring rotation matches against; must not be NULL
*/
typedef struct EchoRichFileConfiguration {
  EchoFilterFunction *filter;
  void               *filter_user_data;
  NexusPath           base_directory;
  uint16              max_logs;
  NexusTimePrecision  display_precision;
  const char         *log_prefix;
} EchoRichFileConfiguration;

/*
Summary: the baseline EchoRuntimeConfiguration -- queue_capacity ECHO_QUEUE_CAPACITY_DEFAULT, saturation_policy EQSP_BLOCK.
*/
extern EchoRuntimeConfiguration echo_runtime_configuration_default(void);

/*
Summary: allocates an EchoRuntime and its transport; does not start the worker thread.
Params:
  configuration  borrowed for the duration of the call and copied; NULL selects echo_runtime_configuration_default()
  out_runtime    out; on success owns one runtime, released with echo_runtime_destroy after echo_runtime_close
Errors: returns GLOBAL_ECHO_ERROR_RUNTIME_STATE for a null out_runtime, GLOBAL_ECHO_ERROR_RUNTIME_CAPACITY for a zero or overflowing queue_capacity, or any NexusError from the underlying mutex/condvar/channel creation.
*/
extern NEXUS_NODISCARD NexusError *echo_runtime_create(const EchoRuntimeConfiguration *configuration, EchoRuntime **out_runtime);

/*
Summary: spawns the worker thread; the runtime accepts log traffic only after this succeeds.
Description: waits until the consumer has completed startup before admitting records.
Errors: returns GLOBAL_ECHO_ERROR_RUNTIME_STATE if runtime is null or already starting/started/closing/closed, or the NexusError from thread creation.
*/
extern NEXUS_NODISCARD NexusError *echo_runtime_start(EchoRuntime *runtime);

/*
Summary: blocks until every record admitted before this call has been dispatched to every enabled sink and any buffered sink output flushed.
Description: does not promise durable storage -- see DESIGN.md's Flush invariant. Safe to call from
  any thread while the runtime is running.
Errors: returns GLOBAL_ECHO_ERROR_RUNTIME_STATE if runtime is null, not started, or already closing/closed; otherwise returns the first sink failure observed since the previous flush or close, if any.
*/
extern NEXUS_NODISCARD NexusError *echo_runtime_flush(EchoRuntime *runtime);

/*
Summary: drains admitted records, flushes sinks, joins the worker thread, and marks the runtime closed. Idempotent -- a second call returns the same result without re-running shutdown.
Errors: returns GLOBAL_ECHO_ERROR_RUNTIME_STATE if runtime is null; otherwise returns the first sink or worker failure observed during shutdown, if any.
Thread-safety: concurrent callers all block until the one shutdown completes and then all observe its result.
*/
extern NEXUS_NODISCARD NexusError *echo_runtime_close(EchoRuntime *runtime);

/*
Summary: releases a closed runtime's registries, sinks, and synchronization objects.
Errors: NEXUS_PROGRAM_ASSERT if runtime is not yet closed, still has active operations, or any of its systems still has a live context -- all programmer errors; a NULL runtime is a silent no-op.
Ownership: runtime and every EchoSystem/EchoContext/EchoSink still reachable from it must not be used afterward.
*/
extern void echo_runtime_destroy(EchoRuntime *runtime);

/*
Summary: copies the runtime's current EchoRuntimeStatistics into *out_statistics.
Errors: returns GLOBAL_ECHO_ERROR_RUNTIME_STATE if runtime or out_statistics is null.
Thread-safety: safe to call concurrently with logging; the snapshot is taken under the runtime's own lock.
*/
extern NEXUS_NODISCARD NexusError *echo_runtime_statistics_get(EchoRuntime *runtime, EchoRuntimeStatistics *out_statistics);

/*
Summary: installs a runtime-wide filter, evaluated before any per-sink filter; echo_runtime_filter_disable clears it.
Params:
  filter     borrowed; must remain valid until replaced or the runtime is destroyed
  user_data  passed to filter on every call
Errors: returns GLOBAL_ECHO_ERROR_RUNTIME_STATE if runtime is null or not accepting configuration changes.
Thread-safety: safe to call while the runtime is running; takes effect for records not yet dispatched.
*/
extern NEXUS_NODISCARD NexusError *echo_runtime_filter_set(EchoRuntime *runtime, EchoFilterFunction *filter, void *user_data);

/*
Summary: removes the runtime-wide filter installed by echo_runtime_filter_set -- see it for Errors/Thread-safety.
*/
extern NEXUS_NODISCARD NexusError *echo_runtime_filter_disable(EchoRuntime *runtime);

/*
Summary: creates a hierarchical system named label (dot-joined under parent's label if parent is non-NULL) and registers it with runtime.
Description: a system's effective level starts at ELL_TRACE. label must not contain '.', the hierarchy separator. A system is identified by its
  handle, not its label: siblings may share a label (two instances of one library under one parent, say) and then show up under the same name in
  records. Level changes follow the parent links, so they reach a system's own descendants and never a same-named sibling.
Params:
  parent      NULL for a root system, otherwise a system already registered with runtime
  out_system  out; on success owns one system, released with echo_runtime_system_unregister
Errors: NEXUS_PROGRAM_PRECONDITION: runtime, label, and out_system must be non-null, and parent (if non-NULL) must already be registered with runtime.
  Returns GLOBAL_ECHO_ERROR_SYSTEM_INVALID for a label containing '.' or a joined label exceeding ECHO_SYSTEM_LABEL_LENGTH_MAX.
*/
extern NEXUS_NODISCARD NexusError *echo_runtime_system_register(EchoRuntime *runtime, const char *label, EchoSystem *parent, EchoSystem **out_system);

/*
Summary: unregisters and releases a system that has no live EchoContext.
Description: systems still registered below it are re-parented to its parent, so they keep their place in the tree for level changes.
Errors: NEXUS_PROGRAM_PRECONDITION: system must be registered with runtime. Returns GLOBAL_ECHO_ERROR_SYSTEM_IN_USE if it still has a live context.
*/
extern NEXUS_NODISCARD NexusError *echo_runtime_system_unregister(EchoRuntime *runtime, EchoSystem *system);

/*
Summary: changes system's effective level, and that of every system registered below it, per policy.
See: EchoSystemLevelPolicy
Errors: returns GLOBAL_ECHO_ERROR_SYSTEM_INVALID for a null system, an out-of-range level, or an out-of-range policy. NEXUS_PROGRAM_PRECONDITION:
  system must be registered with runtime.
*/
extern NEXUS_NODISCARD NexusError *echo_system_level_set(EchoSystem *system, EchoLogLevel level, EchoSystemLevelPolicy policy);

/*
Summary: the system's current effective level; ELL_TRACE for a null system.
*/
extern EchoLogLevel echo_system_level_get(EchoSystem *system);

/*
Summary: the system's fully-joined label; NULL for a null system.
Ownership: borrowed -- valid until the system is unregistered.
*/
extern const char *echo_system_label_get(EchoSystem *system);

/*
Summary: creates a producer handle bound to system, with an empty span stack and no fields.
Params:
  out_context  out; on success owns one context, released with echo_context_destroy
Errors: NEXUS_PROGRAM_PRECONDITION: runtime, system, and out_context must be non-null, and system must be registered with runtime. Returns
  GLOBAL_ECHO_ERROR_RUNTIME_STATE if runtime is closing or closed.
*/
extern NEXUS_NODISCARD NexusError *echo_context_create(EchoRuntime *runtime, EchoSystem *system, EchoContext **out_context);

/*
Summary: creates an independent copy of source's spans and fields, bound to the same system.
Description: the preferred way to fan a context out across concurrent tasks -- see DESIGN.md's
  "copying a context per logical task is the preferred model" invariant. The clone counts as
  another live context against its system.
Params:
  out_context  out; on success owns one context, released with echo_context_destroy
Errors: NEXUS_PROGRAM_PRECONDITION: source and out_context must be non-null. Returns GLOBAL_ECHO_ERROR_RUNTIME_STATE for an inactive source, or
  if runtime is closing or closed.
*/
extern NEXUS_NODISCARD NexusError *echo_context_clone(EchoContext *source, EchoContext **out_context);

/*
Summary: releases a context, waiting for any in-flight logging call on it to finish first. A NULL context is a silent no-op.
*/
extern void echo_context_destroy(EchoContext *context);

/*
Summary: pushes span onto context's span stack; every subsequent log call on context carries the full stack until popped.
Description: a full span stack or an over-length span is truncated/dropped rather than failing --
  matches the truncation policy already used for per-message fields/spans (see e_record_copy_field).
Errors: NEXUS_PROGRAM_PRECONDITION: context and span must be non-null. Returns GLOBAL_ECHO_ERROR_RUNTIME_STATE for an inactive context.
*/
extern NEXUS_NODISCARD NexusError *echo_context_span_push(EchoContext *context, const char *span);

/*
Summary: pops the most recently pushed span from context's span stack.
Errors: NEXUS_PROGRAM_PRECONDITION: context must be non-null. Returns GLOBAL_ECHO_ERROR_RUNTIME_STATE for an inactive context or an empty span
  stack.
*/
extern NEXUS_NODISCARD NexusError *echo_context_span_pop(EchoContext *context);

/*
Summary: attaches field to context, replacing any existing field with the same key; every subsequent log call on context carries it.
Description: a full field table is dropped silently rather than failing -- matches the truncation
  policy already used for per-message fields/spans (see e_record_copy_field). An empty field.key or
  unrecognized field.type still returns an error: those describe a malformed EchoField, not a
  capacity concern.
Errors: NEXUS_PROGRAM_PRECONDITION: context must be non-null. Returns GLOBAL_ECHO_ERROR_RUNTIME_STATE for an inactive context, an empty
  field.key, or an unrecognized field.type.
*/
extern NEXUS_NODISCARD NexusError *echo_context_field_set(EchoContext *context, EchoField field);

/*
Summary: removes the field named key from context, if present.
Returns: TRUE if a field was removed; FALSE for a null context/key or no matching field.
*/
extern boolean echo_context_field_remove(EchoContext *context, const char *key);

/*
Summary: builds an EchoField for echo_context_field_set/echo_message_log_fields_at from key and a value of the matching type; one constructor per EchoFieldType.
Params:
  key    borrowed by the returned EchoField -- copied into bounded storage by whichever call consumes it
  value  for echo_field_string_create, borrowed the same way; a NULL value is stored as an empty string
*/
extern EchoField echo_field_string_create(const char *key, const char *value);
extern EchoField echo_field_int64_create(const char *key, int64 value);
extern EchoField echo_field_uint64_create(const char *key, uint64 value);
extern EchoField echo_field_float_create(const char *key, f_real value);
extern EchoField echo_field_boolean_create(const char *key, boolean value);

/*
Summary: captures and admits one log record from context at level, if not silenced -- the shared implementation behind echo_log and echo_message_log_at/echo_message_log_fields_at.
Description: silences (without formatting the message) when context is destroying, the runtime
  isn't started or is closing/closed, level is below the system's effective level, or this
  thread is already inside this runtime's own worker (recursion protection; see DESIGN.md §
  Autopoietic Execution). Otherwise snapshots context's spans/fields under lock, formats
  message/fields outside the lock, and admits the record per the runtime's saturation policy.
Params:
  file, line  provenance, normally __FILE__/__LINE__ via the echo_log family of macros
  fields, field_count  optional additional fields merged with context's own, context's taking priority on key collision
  format, args  a printf-style format and its already-started va_list
Errors: none returned -- failures are reported as an EchoLogOutcome, never a NexusError.
Thread-safety: safe to call concurrently on distinct contexts; concurrent calls on the same context are serialized internally.
*/
extern EchoLogOutcome echo_message_logv_at(EchoContext *context, EchoLogLevel level, const char *file, uint32 line, const EchoField *fields,
                                           uint8 field_count, const char *format, va_list args);

/*
Summary: variadic entry point behind the echo_log family of macros -- see echo_message_logv_at for behavior.
*/
extern EchoLogOutcome echo_message_log_at(EchoContext *context, EchoLogLevel level, const char *file, uint32 line, const char *format, ...);

/*
Summary: like echo_message_log_at, plus fields merged into the record -- see echo_message_logv_at for behavior.
*/
extern EchoLogOutcome echo_message_log_fields_at(EchoContext *context, EchoLogLevel level, const char *file, uint32 line, const EchoField *fields,
                                                 uint8 field_count, const char *format, ...);

/*
Summary: the fixed uppercase label for level (e.g. "WARNING"); "UNKNOWN" for an out-of-range level.
Ownership: borrowed -- a pointer to static storage, valid for the process lifetime.
*/
extern const char *echo_log_level_label_get(EchoLogLevel level);

/*
Summary: read-only accessors over an EchoRecord, as seen by an EchoFilterFunction or a custom sink's submit/flush.
Description: every accessor is null-safe: a NULL record returns the type's zero-ish default
  (ELL_TRACE, a zeroed NexusTime of the matching clock_origin, NULL, or 0) rather than
  asserting, since filters and sinks may be handed a defensively-checked pointer.
Params:
  index  for echo_record_span_get/field_get, must be below the record's own span_count/field_count or the call returns NULL/FALSE
Ownership: every returned pointer is borrowed and valid only for the duration of the filter/sink call that received the record.
*/
extern EchoLogLevel echo_record_level_get(const EchoRecord *record);
extern NexusTime    echo_record_timestamp_get(const EchoRecord *record);
extern NexusTime    echo_record_monotonic_timestamp_get(const EchoRecord *record);
extern const char  *echo_record_message_get(const EchoRecord *record);
extern const char  *echo_record_origin_file_get(const EchoRecord *record);
extern uint32       echo_record_origin_line_get(const EchoRecord *record);
extern const char  *echo_record_system_label_get(const EchoRecord *record);
extern uint8        echo_record_span_count_get(const EchoRecord *record);
extern const char  *echo_record_span_get(const EchoRecord *record, uint8 index);
extern uint8        echo_record_field_count_get(const EchoRecord *record);
extern boolean      echo_record_field_get(const EchoRecord *record, uint8 index, EchoField *out_field);

/*
Summary: logs a message at an explicit level, capturing __FILE__/__LINE__ automatically -- the primitive behind echo_trace..echo_critical.
See: adr/0003-accept-variadic-macros-as-a-c89-exception
*/
#define echo_log(context, level, format, ...) echo_message_log_at((context), (level), __FILE__, (uint32)__LINE__, (format), ##__VA_ARGS__)

/*
Summary: echo_log fixed to one EchoLogLevel each, from least (echo_trace) to most (echo_critical) severe.
*/
#define echo_trace(context, format, ...)    echo_log((context), ELL_TRACE, (format), ##__VA_ARGS__)
#define echo_debug(context, format, ...)    echo_log((context), ELL_DEBUG, (format), ##__VA_ARGS__)
#define echo_info(context, format, ...)     echo_log((context), ELL_INFO, (format), ##__VA_ARGS__)
#define echo_notice(context, format, ...)   echo_log((context), ELL_NOTICE, (format), ##__VA_ARGS__)
#define echo_warning(context, format, ...)  echo_log((context), ELL_WARNING, (format), ##__VA_ARGS__)
#define echo_error(context, format, ...)    echo_log((context), ELL_ERROR, (format), ##__VA_ARGS__)
#define echo_critical(context, format, ...) echo_log((context), ELL_CRITICAL, (format), ##__VA_ARGS__)

/*
Summary: registers sink with runtime; only possible before echo_runtime_start. Ownership of sink passes to runtime.
Errors: NEXUS_PROGRAM_PRECONDITION: runtime and sink must be non-null. Returns GLOBAL_ECHO_ERROR_RUNTIME_STATE if the runtime is already
  started/closing or the sink is already registered/owned.
*/
extern NEXUS_NODISCARD NexusError *echo_runtime_sink_register(EchoRuntime *runtime, EchoSink *sink);

/*
Summary: enables or disables sink; a disabled sink is skipped during dispatch and flush without being unregistered.
Errors: NEXUS_PROGRAM_PRECONDITION: sink must be non-null and registered.
Thread-safety: safe to call concurrently with dispatch; takes effect for records not yet dispatched.
*/
extern NEXUS_NODISCARD NexusError *echo_sink_enabled_set(EchoSink *sink, boolean enabled);

/*
Summary: sink's current enabled state; FALSE for a null sink.
*/
extern boolean echo_sink_enabled_get(EchoSink *sink);

/*
Summary: creates a sink from a caller-supplied EchoCustomSinkConfiguration.
Params:
  out_sink  out; on success owns one unregistered sink -- either register it with echo_runtime_sink_register or release it with echo_sink_destroy
Errors: NEXUS_PROGRAM_PRECONDITION: configuration and configuration->submit must be non-null. Returns the NexusError from mutex creation, if any.
*/
extern NEXUS_NODISCARD NexusError *echo_sink_custom_create(const EchoCustomSinkConfiguration *configuration, EchoSink **out_sink);

/*
Summary: creates a plain (uncolored) terminal sink writing to stdout at precision, passing every record through filter first.
Params:
  filter  optional; NULL admits every record to this sink
Errors: returns the NexusError from mutex creation, if any.
*/
extern NEXUS_NODISCARD NexusError *echo_sink_simple_terminal_create(EchoFilterFunction *filter, void *filter_user_data, NexusTimePrecision precision,
                                                                    EchoSink **out_sink);

/*
Summary: creates a color-capable terminal sink writing to stdout per configuration -- see echo_sink_simple_terminal_create for filter/Errors.
*/
extern NEXUS_NODISCARD NexusError *echo_sink_rich_terminal_create(EchoFilterFunction *filter, void *filter_user_data,
                                                                  EchoRichTerminalConfiguration configuration, EchoSink **out_sink);

/*
Summary: creates a plain-text sink appending to the file at path, with no rotation.
Errors: NEXUS_PROGRAM_PRECONDITION: path must be non-null. Returns the NexusError from opening the file or creating the mutex.
*/
extern NEXUS_NODISCARD NexusError *echo_sink_simple_file_create(EchoFilterFunction *filter, void *filter_user_data, NexusTimePrecision precision,
                                                                const NexusPath *path, EchoSink **out_sink);

/*
Summary: creates a rotating, timestamped file sink -- see EchoRichFileConfiguration for the rotation rule.
Errors: NEXUS_PROGRAM_PRECONDITION: configuration.log_prefix must be non-null. Returns the NexusError from directory creation, file opening, or
  mutex creation.
*/
extern NEXUS_NODISCARD NexusError *echo_sink_rich_file_create(EchoRichFileConfiguration configuration, EchoSink **out_sink);

/*
Summary: changes a terminal sink's timestamp precision after creation.
Errors: NEXUS_PROGRAM_PRECONDITION: sink must be a non-null terminal sink.
*/
extern NEXUS_NODISCARD NexusError *echo_sink_terminal_precision_set(EchoSink *sink, NexusTimePrecision precision);

/*
Summary: replaces a rich terminal sink's whole EchoRichTerminalConfiguration, including its display_precision.
Errors: NEXUS_PROGRAM_PRECONDITION: sink must be a non-null terminal sink.
*/
extern NEXUS_NODISCARD NexusError *echo_sink_rich_terminal_configuration_set(EchoSink *sink, EchoRichTerminalConfiguration configuration);

/*
Summary: the current log file path of a simple or rich file sink.
Errors: NEXUS_PROGRAM_PRECONDITION: sink and out_path must be non-null, and sink must be a file sink.
*/
extern NEXUS_NODISCARD NexusError *echo_sink_rich_file_path_get(EchoSink *sink, NexusPath *out_path);

/*
Summary: releases an unregistered sink; use echo_runtime_close/destroy to release a registered one instead.
Errors: NEXUS_PROGRAM_ASSERT if sink is still registered -- a programmer error; a NULL sink is a silent no-op.
*/
extern void echo_sink_destroy(EchoSink *sink);

/*
Summary: TRUE if sink is non-null and has a submit callback -- a basic structural sanity check, not a registration check.
*/
extern boolean echo_sink_is_valid(EchoSink *sink);

/*
Summary: constructs an EchoColor; echo_color_none_create disables coloring for that slot.
*/
extern EchoColor echo_color_none_create(void);
extern EchoColor echo_color_ansi_create(uint8 code);
extern EchoColor echo_color_rgb_create(uint8 red, uint8 green, uint8 blue);

/*
Summary: built-in EchoRichTerminalConfiguration presets at the given timestamp precision -- plain (no color), a fixed ANSI palette, or a fixed 24-bit RGB palette.
*/
extern EchoRichTerminalConfiguration echo_rich_terminal_configuration_plain_create(NexusTimePrecision precision);
extern EchoRichTerminalConfiguration echo_rich_terminal_configuration_ansi_create(NexusTimePrecision precision);
extern EchoRichTerminalConfiguration echo_rich_terminal_configuration_rgb_create(NexusTimePrecision precision);

/*
Summary: standard ANSI SGR foreground color codes for EchoColor's ansi_code field (30-37 normal, 90-97 bright).
*/
#define ECHO_TERMINAL_ANSI_BLACK          30
#define ECHO_TERMINAL_ANSI_RED            31
#define ECHO_TERMINAL_ANSI_GREEN          32
#define ECHO_TERMINAL_ANSI_YELLOW         33
#define ECHO_TERMINAL_ANSI_BLUE           34
#define ECHO_TERMINAL_ANSI_MAGENTA        35
#define ECHO_TERMINAL_ANSI_CYAN           36
#define ECHO_TERMINAL_ANSI_WHITE          37
#define ECHO_TERMINAL_ANSI_BRIGHT_BLACK   90
#define ECHO_TERMINAL_ANSI_BRIGHT_RED     91
#define ECHO_TERMINAL_ANSI_BRIGHT_GREEN   92
#define ECHO_TERMINAL_ANSI_BRIGHT_YELLOW  93
#define ECHO_TERMINAL_ANSI_BRIGHT_BLUE    94
#define ECHO_TERMINAL_ANSI_BRIGHT_MAGENTA 95
#define ECHO_TERMINAL_ANSI_BRIGHT_CYAN    96
#define ECHO_TERMINAL_ANSI_BRIGHT_WHITE   97

/*
Summary: routes Nexus's own memory-tracer output into context at ELL_TRACE; echo_nexus_memory_log_callback_uninstall removes it.
Description: see DESIGN.md's note on channel copies themselves being trace events, and the two
  separate thread-local guards that keep that from deadlocking or infinitely regressing. Only one
  install may be active process-wide at a time.
Errors: NEXUS_PROGRAM_PRECONDITION: context must be non-null. Returns GLOBAL_ECHO_ERROR_RUNTIME_STATE if a callback is already installed.
*/
extern NEXUS_NODISCARD NexusError *echo_nexus_memory_log_callback_install(EchoContext *context);
extern void                        echo_nexus_memory_log_callback_uninstall(void);

/*
Summary: routes Nexus's own assertion/diagnostic callback into context at ELL_CRITICAL; echo_nexus_assertions_callback_uninstall removes it.
Errors: NEXUS_PROGRAM_PRECONDITION: context must be non-null. Returns GLOBAL_ECHO_ERROR_RUNTIME_STATE if a callback is already installed.
*/
extern NEXUS_NODISCARD NexusError *echo_nexus_assertions_callback_install(EchoContext *context);
extern void                        echo_nexus_assertions_callback_uninstall(void);
