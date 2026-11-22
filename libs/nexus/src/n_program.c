/* On Windows the C runtime headers pull in memcpy/memset, which nexus.h poisons, so they come first there. */
#ifdef _WIN32
#  include <setjmp.h>
#  include <stdio.h>
#  include <stdlib.h>
#endif

#include "../nexus.h"

#include <stb_sprintf.h>

#ifndef _WIN32
#  include <setjmp.h>
#  include <stdlib.h>
#  include <stdio.h>
#endif
#include <strings.h>

/* <string.h> declares memcpy/memset, which nexus.h poisons outside n_memory.c (its one
   permitted implementation file) -- forward-declared alone here instead of pulling in the
   whole header just for this. Clang already knows strerror as a library builtin, hence the
   NOLINT: this isn't a duplicate of another declaration in this file. */
extern char *strerror(int errnum); /* NOLINT(readability-redundant-declaration) */

#if NEXUS_THREAD_SAFE
#  include <pthread.h>
#endif

/*
TODO -> ANSI / Truecolor coloring

- Perhaps source code showing like Python exceptions if that is plausible to do (parsing the file at report time and such).
  - Performance is irrelevant here because it's a crash anyway. The only performance that matters is at collect time.
*/

/* ---------------------------------------------------------------------------- */
/* CONTEXT / STACK FRAME                                                        */
/* ---------------------------------------------------------------------------- */

typedef NexusProgramContextSnapshot ProgramContext;

/*
program_context is inherently per-thread, not a shared resource: it's a call-stack trace, and
two threads pushing/popping into one shared instance would interleave into a single nonsensical
stack rather than each getting a correct trace of its own -- a mutex alone would prevent data
races but not that logical corruption. Under NEXUS_THREAD_SAFE, each thread gets its own
lazily-allocated ProgramContext via pthread_getspecific/setspecific, freed automatically when
the thread exits (the pthread_key_create destructor below); with it off, this reverts to the
original single static instance, correct for a genuinely single-threaded build at zero cost.
*/
#if NEXUS_THREAD_SAFE

static pthread_key_t  n_program_context_key;
static pthread_once_t n_program_context_key_once = PTHREAD_ONCE_INIT;

static void _n_program_context_destroy(void *raw_context) {
  nexus_memory_deallocate(raw_context);
}

static void _n_program_context_key_create(void) {
  (void)pthread_key_create(&n_program_context_key, _n_program_context_destroy);
}

static ProgramContext *_n_program_context_get(void) {
  ProgramContext *context;

  (void)pthread_once(&n_program_context_key_once, _n_program_context_key_create);

  context = (ProgramContext *)pthread_getspecific(n_program_context_key);
  if (context == NULL) {
    context = (ProgramContext *)nexus_memory_allocate(NEXUS_SIZEOF(ProgramContext));
    nexus_memory_bytes_clear(context, NEXUS_SIZEOF(ProgramContext));
    (void)pthread_setspecific(n_program_context_key, context);
    nexus_memory_mark_persistent(context);
    nexus_memory_comment_set(context, "Nexus program diagnostic context");
  }

  return context;
}

#else

/* Static variables are initialized as zero, so this is safe to do... they cannot contain garbage. */
static ProgramContext program_context;

static ProgramContext *_n_program_context_get(void) {
  return &program_context;
}

#endif

static void _nexus_program_log_stack_trace(void) {
  ProgramContext               *context;
  NexusProgramContextStackFrame scan_frame;
  uint32                        frame_idx;

  context = _n_program_context_get();

  if (context->frame_count > 0) {
    (void)fprintf(stderr, "Call stack at panic:\n");
  }

  for (frame_idx = context->frame_count; frame_idx-- > 0;) {
    scan_frame = context->frames[frame_idx];

    if (scan_frame.label != NULL && scan_frame.label[0] != '\0') { /* Labels are optional. */
      (void)fprintf(stderr, " - %s:%u [%s]\n", scan_frame.file, scan_frame.line, scan_frame.label);
    } else {
      (void)fprintf(stderr, " - %s:%u\n", scan_frame.file, scan_frame.line);
    }
  }
}

static NexusProgramDiagnosticFunction *program_diagnostic_function  = NULL;
static void                           *program_diagnostic_user_data = NULL;

void nexus_program_diagnostic_function_set(NexusProgramDiagnosticFunction *function, void *user_data) {
  program_diagnostic_function  = function;
  program_diagnostic_user_data = user_data;
}

static void _nexus_program_log_violation(const char *occurrence, const char *message, const char *file, uint32 line) {
  if (program_diagnostic_function != NULL) {
    program_diagnostic_function(program_diagnostic_user_data, occurrence, message, file, line);
  }
  (void)fprintf(stderr, "%s at %s:%u: %s\n", occurrence, file, line, message);

  _nexus_program_log_stack_trace();
}

boolean _nexus_program_context_push(const char *label, const char *file, uint32 line) {
  ProgramContext               *context;
  NexusProgramContextStackFrame new_stack_frame;

  context = _n_program_context_get();

  if (context->frame_count == NEXUS_PROGRAM_CONTEXT_MAX_STACK_SIZE) {
    _nexus_program_panic("Tried to allocate beyond maximum stack size", file, line);
    return TRUE;
  }

  new_stack_frame.label                 = label;
  new_stack_frame.file                  = file;
  new_stack_frame.line                  = line;
  context->frames[context->frame_count] = new_stack_frame;
  context->frame_count++;
  return FALSE;
}

boolean _nexus_program_context_pop(const char *file, uint32 line) {
  ProgramContext *context;

  context = _n_program_context_get();

  if (context->frame_count == 0) {
    _nexus_program_panic("Tried to pop empty program context", file, line);
    return TRUE;
  }

  context->frame_count--;
  return FALSE;
}

void _nexus_program_context_snapshot_save(NexusProgramContextSnapshot *out_snapshot) {
  *out_snapshot = *_n_program_context_get();
}

void _nexus_program_context_snapshot_restore(const NexusProgramContextSnapshot *snapshot) {
  *_n_program_context_get() = *snapshot;
}

/* ---------------------------------------------------------------------------- */
/* RECOVERY                                                                     */
/* ---------------------------------------------------------------------------- */

/*
Thread-local for the same reason program_context is above: two threads sharing one armed
recovery point would race on which one a failure actually belongs to, and a longjmp can only ever
unwind within its own thread regardless, so a shared instance couldn't be meaningfully correct
even with a lock around it.
*/
typedef struct NProgramRecoveryState {
  boolean     armed;
  jmp_buf    *recovery_point; /* borrowed; owned by whoever called nexus_program_recovery_arm */
  NexusError *last_error;     /* set only once a recovery has actually fired */
} NProgramRecoveryState;

#if NEXUS_THREAD_SAFE

static pthread_key_t  n_program_recovery_key;
static pthread_once_t n_program_recovery_key_once = PTHREAD_ONCE_INIT;

static void _n_program_recovery_destroy(void *raw_state) {
  nexus_memory_deallocate(raw_state);
}

static void _n_program_recovery_key_create(void) {
  (void)pthread_key_create(&n_program_recovery_key, _n_program_recovery_destroy);
}

static NProgramRecoveryState *_n_program_recovery_get(void) {
  NProgramRecoveryState *state;

  (void)pthread_once(&n_program_recovery_key_once, _n_program_recovery_key_create);

  state = (NProgramRecoveryState *)pthread_getspecific(n_program_recovery_key);
  if (state == NULL) {
    state = (NProgramRecoveryState *)nexus_memory_allocate(NEXUS_SIZEOF(NProgramRecoveryState));
    nexus_memory_bytes_clear(state, NEXUS_SIZEOF(NProgramRecoveryState));
    (void)pthread_setspecific(n_program_recovery_key, state);
    nexus_memory_mark_persistent(state);
    nexus_memory_comment_set(state, "Nexus program recovery state");
  }

  return state;
}

#else

static NProgramRecoveryState program_recovery_state;

static NProgramRecoveryState *_n_program_recovery_get(void) {
  return &program_recovery_state;
}

#endif

void nexus_program_recovery_arm(jmp_buf *recovery_point) {
  NProgramRecoveryState *state;

  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_program_recovery_arm");
  NEXUS_PROGRAM_PRECONDITION(recovery_point != NULL, "nexus_program_recovery_arm: recovery_point must not be NULL");

  state                 = _n_program_recovery_get();
  state->armed          = TRUE;
  state->recovery_point = recovery_point;
  state->last_error     = NULL;

  NEXUS_PROGRAM_CONTEXT_POP();
}

void nexus_program_recovery_disarm(void) {
  NProgramRecoveryState *state = _n_program_recovery_get();

  state->armed          = FALSE;
  state->recovery_point = NULL;
}

NexusError *nexus_program_recovery_last_error_get(void) {
  return _n_program_recovery_get()->last_error;
}

/*
Summary: internal -- TRUE if this thread currently has a recovery point armed. Checked by
  _nexus_program_assert_v/_nexus_program_panic/_nexus_program_panic_error before they would
  otherwise abort.
*/
static boolean _n_program_recovery_is_armed(void) {
  return _n_program_recovery_get()->armed;
}

/*
Summary: internal -- records error as this thread's recovered failure and longjmps to the armed
  recovery point. Never returns. Only call when _n_program_recovery_is_armed() just returned TRUE.
*/
static void _n_program_recovery_fire(NexusError *error) {
  NProgramRecoveryState *state = _n_program_recovery_get();
  jmp_buf               *recovery_point;

  recovery_point        = state->recovery_point;
  state->last_error     = error;
  state->armed          = FALSE;
  state->recovery_point = NULL;

  /* Disarm before transferring control. The recovered branch is allowed to call Nexus while it
     formats or reports the failure; a second assertion there must abort normally rather than
     jumping back into the same handler recursively. */
  longjmp(*recovery_point, 1);
}

/* ---------------------------------------------------------------------------- */
/* ERRORS                                                                       */
/* ---------------------------------------------------------------------------- */

typedef struct NErrorSlab {
  NexusError         errors[NEXUS_PROGRAM_ERROR_POOL_SLAB_SIZE];
  char               messages[NEXUS_PROGRAM_ERROR_POOL_SLAB_SIZE][NEXUS_PROGRAM_ERROR_MESSAGE_MAX_SIZE];
  struct NErrorSlab *next_slab;

  uint64 errors_in_use;
} NErrorSlab;

static NErrorSlab  n_error_pool;
static uint64      n_slab_count      = 1;
static NErrorSlab *n_error_pool_head = &n_error_pool;
static NErrorSlab *n_error_pool_tail = &n_error_pool;
static NErrorSlab *n_error_pool_current =
    &n_error_pool; /* the slab actively being filled -- distinct from tail, which is only where new slabs get appended */

/*
The pool's slab list, fill cursor, and slab count are shared, genuinely cross-thread state
(unlike program_context above) -- any thread constructing an error draws from the same pool.
PTHREAD_MUTEX_INITIALIZER rather than nexus_threads_mutex_create: this guards plain static
storage that already needs no explicit initialization, and a lazily-created NexusMutex* would
just reintroduce that problem one level down.
*/
#if NEXUS_THREAD_SAFE
static pthread_mutex_t n_error_pool_mutex = PTHREAD_MUTEX_INITIALIZER;
#  define N_ERROR_POOL_LOCK()   (void)pthread_mutex_lock(&n_error_pool_mutex)
#  define N_ERROR_POOL_UNLOCK() (void)pthread_mutex_unlock(&n_error_pool_mutex)
#else
#  define N_ERROR_POOL_LOCK()   ((void)0)
#  define N_ERROR_POOL_UNLOCK() ((void)0)
#endif

uint64 nexus_program_errors_pool_slabs_get(void) {
  uint64 slab_count;

  N_ERROR_POOL_LOCK();
  slab_count = n_slab_count;
  N_ERROR_POOL_UNLOCK();

  return slab_count;
}

void nexus_program_errors_pool_slabs_add(uint64 slabs) {
  NErrorSlab *new_slabs_address;
  uint64      i;

  /* nexus_memory_allocate never actually returns NULL -- it aborts internally on OOM via
     its own NEXUS_PROGRAM_POSTCONDITION_ALWAYS -- so a local NULL check here would be dead
     code that can never run, not a second line of OOM handling. Deliberately outside the lock:
     it doesn't touch any pool state yet, and holding a lock across an allocation call that can
     abort the process is no safer than not holding it. */
  new_slabs_address = (NErrorSlab *)nexus_memory_allocate(slabs * NEXUS_SIZEOF(NErrorSlab));
  nexus_memory_bytes_clear(new_slabs_address, slabs * NEXUS_SIZEOF(NErrorSlab));

  /* The error pool only ever grows, never shrinks or frees -- flagging this here means
     nexus_memory_leak_report never has to special-case it. */
  nexus_memory_mark_persistent(new_slabs_address);
  nexus_memory_comment_set(new_slabs_address, "Nexus error pool slab(s)");

  N_ERROR_POOL_LOCK();

  n_slab_count += slabs;

  for (i = 0; i < slabs; i++) {
    n_error_pool_tail->next_slab = (NErrorSlab *)NEXUS_MEMORY_OFFSET(new_slabs_address, i * NEXUS_SIZEOF(NErrorSlab));
    n_error_pool_tail            = n_error_pool_tail->next_slab;
  }

  N_ERROR_POOL_UNLOCK();
}

void nexus_program_errors_pool_reset(void) {
  NErrorSlab *current_slab;

  N_ERROR_POOL_LOCK();

  current_slab                = n_error_pool_head;
  current_slab->errors_in_use = 0;

  while (current_slab->next_slab != NULL) {
    current_slab->next_slab->errors_in_use = 0;
    current_slab                           = current_slab->next_slab;
  }

  n_error_pool_current = n_error_pool_head;

  N_ERROR_POOL_UNLOCK();
}

NexusError nexus_program_errors_success               = {__FILE__, __LINE__, GLOBAL_NEXUS_ERROR_SUCCESS, "success.", NULL};
NexusError nexus_program_errors_pool_capacity_reached = {__FILE__, __LINE__, GLOBAL_NEXUS_ERROR_POOL_CAPACITY_REACHED, "pool capacity exhausted.",
                                                         NULL};

NexusError *_nexus_program_errors_failure_create_v(NexusErrorCode code, const char *format, const char *file, uint32 line, va_list args) {
  NexusError *error;
  int         required_length;

  NEXUS_PROGRAM_CONTEXT_PUSH("_nexus_program_errors_failure_create_v");
  N_ERROR_POOL_LOCK();

  while (n_error_pool_current->errors_in_use == NEXUS_PROGRAM_ERROR_POOL_SLAB_SIZE) {
    if (n_error_pool_current->next_slab == NULL) {
      N_ERROR_POOL_UNLOCK();
      NEXUS_PROGRAM_CONTEXT_POP();
      return &nexus_program_errors_pool_capacity_reached;
    }

    n_error_pool_current = n_error_pool_current->next_slab;
  }

  error = &n_error_pool_current->errors[n_error_pool_current->errors_in_use];

  if (format != NULL) {
    required_length =
        stbsp_vsnprintf(n_error_pool_current->messages[n_error_pool_current->errors_in_use], NEXUS_PROGRAM_ERROR_MESSAGE_MAX_SIZE, format, args);

    NEXUS_PROGRAM_PRECONDITION(required_length < NEXUS_PROGRAM_ERROR_MESSAGE_MAX_SIZE,
                               "Formatted error message exceeds NEXUS_PROGRAM_ERROR_MESSAGE_MAX_SIZE");
    (void)required_length; /* only read by the PRECONDITION check, which compiles away entirely outside NEXUS_DEVELOPER_MODE */

    error->message = n_error_pool_current->messages[n_error_pool_current->errors_in_use];
  } else {
    /* format is optional -- NULL means "no message", not an error. */
    error->message = NULL;
  }

  error->code            = code;
  error->contained_error = NULL;
  error->file            = file;
  error->line            = line;

  n_error_pool_current->errors_in_use++;

  N_ERROR_POOL_UNLOCK();

  NEXUS_PROGRAM_CONTEXT_POP();
  return error;
}

NexusError *_nexus_program_errors_failure_create(NexusErrorCode code, const char *format, const char *file, uint32 line, ...) {
  NexusError *error;
  va_list     args;

  va_start(args, line);
  error = _nexus_program_errors_failure_create_v(code, format, file, line, args);
  va_end(args);

  return error;
}

void nexus_program_errors_wrap(NexusError *current, NexusError *internal) {
  current->contained_error = internal;
}

NexusError *_nexus_program_errors_wrap(NexusError *internal, NexusErrorCode code, const char *format, const char *file, uint32 line, ...) {
  NexusError *outer_error;
  va_list     args;

  NEXUS_PROGRAM_PRECONDITION(internal != NULL, "internal error must not be NULL");

  va_start(args, line);
  outer_error = _nexus_program_errors_failure_create_v(code, format, file, line, args);
  va_end(args);

  if (outer_error != &nexus_program_errors_pool_capacity_reached) {
    outer_error->contained_error = internal;
  }

  return outer_error;
}

NexusError *_nexus_program_errors_forward(NexusError *forwarded, const char *file, uint32 line) {
  NexusError *outer_error;

  /* Create a dummy outer error with the forward code, this means the print operations can properly format it. */
  outer_error = _nexus_program_errors_failure_create(GLOBAL_NEXUS_ERROR_FORWARD, "", file, line);
  if (outer_error->code == GLOBAL_NEXUS_ERROR_POOL_CAPACITY_REACHED) {
    return outer_error;
  }

  outer_error->contained_error = forwarded;
  return outer_error;
}

#if NEXUS_PLATFORM_LINUX
NexusError *_nexus_program_errors_from_errno_create(int32 errno_value, const char *format, const char *file, uint32 line, ...) {
  NexusErrorCode code;
  char           user_message[NEXUS_PROGRAM_ERROR_MESSAGE_MAX_SIZE];
  va_list        args;
  NexusError    *error;

  NEXUS_PROGRAM_CONTEXT_PUSH("_nexus_program_errors_from_errno_create");
  NEXUS_PROGRAM_PRECONDITION_ALWAYS(errno_value >= 0 && errno_value <= 4095, "errno value %d exceeds NexusErrorCode's 12-bit local-code budget",
                                    errno_value);

  /* Hand-packed instead of NEXUS_ERROR_LOCAL/NEXUS_PROGRAM_ERROR_CODE_MAKE -- both validate
     their arguments via a bitfield-width trick that requires a compile-time constant, and
     errno_value is only known at runtime. See NEXUS_PROGRAM_ERROR_FROM_ERRNO's Description. */
  code = ((NexusErrorCode)NEXUS_INTERNAL_ERROR_SCHEMA_VERSION << 60) | ((NexusErrorCode)NEXUS_ERROR_LIBRARY_IDENTIFIER << 40) |
         ((NexusErrorCode)NEXUS_ERROR_OS_FACILITY << 20) | (((NexusErrorCode)NPOEC_ERRNO << 12) | (NexusErrorCode)errno_value);

  if (format != NULL) {
    va_start(args, line);
    (void)stbsp_vsnprintf(user_message, NEXUS_PROGRAM_ERROR_MESSAGE_MAX_SIZE, format, args);
    va_end(args);

    error = _nexus_program_errors_failure_create(code, "%s: %s", file, line, user_message, strerror(errno_value));
  } else {
    error = _nexus_program_errors_failure_create(code, "%s", file, line, strerror(errno_value));
  }

  NEXUS_PROGRAM_CONTEXT_POP();
  return error;
}
#else
NexusError *_nexus_program_errors_from_errno_create(int32 errno_value, const char *format, const char *file, uint32 line, ...) {
  NEXUS_PROGRAM_CONTEXT_PUSH("_nexus_program_errors_from_errno_create");
  NEXUS_PLATFORM_UNSUPPORTED_RAISE("errors_from_errno_create");
}
#endif

uint_large nexus_program_errors_error_print(NexusError *error, char *buffer, uint_large buffer_size) {
  uint_large written;
  uint_large would_write;
  uint_large buffer_size_remaining;
  int        message_length;
  uint32     depth;
  char      *destination;

  NexusError                    *current_error;
  NexusInternalUnpackedErrorCode current_error_unpacked;

  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_program_errors_error_print");
  NEXUS_PROGRAM_PRECONDITION(error != NULL, "Error must not be null.");
  NEXUS_PROGRAM_PRECONDITION(buffer != NULL, "Buffer must not be null.");

  written       = 0;
  would_write   = 0;
  current_error = error;
  depth         = 0;

  /* Format: like Java's "Caused by:" chains -- outermost/most-recent error
   * first, each nested cause beneath it indented one level deeper, root
   * cause at the bottom. Matches the chain's own construction order
   * (contained_error walks from most-recent toward the originator). */
  while (current_error != NULL) {
    buffer_size_remaining = (written < buffer_size) ? (buffer_size - written) : 0;
    /* stbsp_vsnprintf underflows one byte before a non-NULL buffer when
     * given count == 0 -- once the real buffer is exhausted, switch to the
     * documented count-only mode (NULL buffer) instead of a zero-capacity
     * pointer into the middle of it. */
    destination = (buffer_size_remaining > 0) ? (buffer + written) : NULL;

    if (current_error->code == GLOBAL_NEXUS_ERROR_FORWARD) {
      message_length = stbsp_snprintf(destination, (int)buffer_size_remaining, "%*s%sat %s:%u [global=%llu (0x%016llX)]\n", (int)(depth * 2), "",
                                      (depth == 0) ? "" : "Caused by: (forwarded) ", current_error->file, current_error->line,
                                      (unsigned long long)current_error->code, (unsigned long long)current_error->code);
    } else {
      _nexus_program_error_code_unpack(current_error->code, &current_error_unpacked);
      message_length = stbsp_snprintf(
          destination, (int)buffer_size_remaining, "%*s%s%s at %s:%u [%.4s:%.4s cat=%u code=%u] global=%llu (0x%016llX)\n", (int)(depth * 2), "",
          (depth == 0) ? "" : "Caused by: ", (current_error->message != NULL) ? current_error->message : "(no message)", current_error->file,
          current_error->line, current_error_unpacked.library, current_error_unpacked.facility, (unsigned)current_error_unpacked.category,
          (unsigned)current_error_unpacked.local_code, (unsigned long long)current_error->code, (unsigned long long)current_error->code);
    }

    would_write += (uint_large)message_length;

    if (buffer_size_remaining > 0) {
      written += ((uint_large)message_length < buffer_size_remaining) ? (uint_large)message_length : buffer_size_remaining - 1;
    }

    current_error = current_error->contained_error;
    depth++;
  }

  NEXUS_PROGRAM_CONTEXT_POP();
  return would_write;
}

/* ---------------------------------------------------------------------------- */
/* PANICS                                                                       */
/* ---------------------------------------------------------------------------- */

void _nexus_program_panic(const char *message, const char *file, uint32 line, ...) {
  char    formatted_message[NEXUS_PROGRAM_ERROR_MESSAGE_MAX_SIZE];
  va_list args;
  int     required_length;

  va_start(args, line);
  required_length = stbsp_vsnprintf(formatted_message, NEXUS_PROGRAM_ERROR_MESSAGE_MAX_SIZE, message, args);
  va_end(args);

  NEXUS_PROGRAM_PRECONDITION(required_length < NEXUS_PROGRAM_ERROR_MESSAGE_MAX_SIZE,
                             "Formatted panic message exceeds NEXUS_PROGRAM_ERROR_MESSAGE_MAX_SIZE");
  (void)required_length; /* only read by the PRECONDITION check, which compiles away entirely outside NEXUS_DEVELOPER_MODE */

  _nexus_program_log_violation("Panic", formatted_message, file, line);

  /* Recovery check last, after the diagnostic is fully logged either way -- a recovered panic
     still leaves the same stderr trail a fatal one would, so nothing about the failure is lost
     just because this instance happened to be caught. */
  if (_n_program_recovery_is_armed()) {
    NexusError *recovered = _nexus_program_errors_failure_create(GLOBAL_NEXUS_PANIC_VIOLATION, "%s", file, line, formatted_message);
    _n_program_recovery_fire(recovered); /* never returns */
  }
}

void _nexus_program_panic_error(NexusError *error, const char *file, uint32 line) {
  char       error_buffer[NEXUS_PROGRAM_PANIC_ERROR_MESSAGE_MAX_SIZE];
  uint_large would_write;

  would_write = nexus_program_errors_error_print(error, error_buffer, NEXUS_PROGRAM_PANIC_ERROR_MESSAGE_MAX_SIZE);
  NEXUS_PROGRAM_PRECONDITION(would_write < NEXUS_PROGRAM_PANIC_ERROR_MESSAGE_MAX_SIZE,
                             "Formatted panic error message exceeds buffer for panic errors");
  (void)would_write;

  /* Notify the diagnostic observer like every other violation path does: without this, a panic raised
     from an error (NEXUS_PROGRAM_PANIC_ERROR) never reached an installed observer, so e.g. Echo could
     neither log it nor flush its sinks before the process aborts. */
  if (program_diagnostic_function != NULL) {
    program_diagnostic_function(program_diagnostic_user_data, "Panic", error_buffer, file, line);
  }

  (void)fprintf(stderr, "Panic at %s:%u\n", file, line);
  (void)fprintf(stderr, "Error trace (most recent first):\n");
  (void)fprintf(stderr, "%s", error_buffer);

  _nexus_program_log_stack_trace();

  if (_n_program_recovery_is_armed()) {
    /* error is already a real pool-resident NexusError -- reuse it directly as the recovered
       failure rather than constructing a second one that would just say the same thing. */
    _n_program_recovery_fire(error); /* never returns */
  }
}

/* ---------------------------------------------------------------------------- */
/* ASSERTIONS                                                                   */
/* ---------------------------------------------------------------------------- */

void _nexus_program_assert_v(const char *violation_type, NexusErrorCode code, const char *message, const char *file, uint32 line, va_list args) {
  char type_buffer[100];
  char formatted_message[NEXUS_PROGRAM_ERROR_MESSAGE_MAX_SIZE];
  int  required_length;

  stbsp_sprintf(type_buffer, "%s VIOLATION", violation_type);

  required_length = stbsp_vsnprintf(formatted_message, NEXUS_PROGRAM_ERROR_MESSAGE_MAX_SIZE, message, args);
  NEXUS_PROGRAM_PRECONDITION(required_length < NEXUS_PROGRAM_ERROR_MESSAGE_MAX_SIZE,
                             "Formatted assertion message exceeds NEXUS_PROGRAM_ERROR_MESSAGE_MAX_SIZE");
  (void)required_length; /* only read by the PRECONDITION check, which compiles away entirely outside NEXUS_DEVELOPER_MODE */

  _nexus_program_log_violation(type_buffer, formatted_message, file, line);

  if (_n_program_recovery_is_armed()) {
    /* formatted_message, not message+args a second time -- args was already consumed by
       stbsp_vsnprintf above (a va_list may not be walked twice without an intervening va_copy,
       see NEXUS_PROGRAM_ERROR_FAILURE's Description), so the already-rendered string is passed
       through "%s" instead of reformatting from scratch. */
    NexusError *recovered = _nexus_program_errors_failure_create(code, "%s", file, line, formatted_message);
    _n_program_recovery_fire(recovered); /* never returns */
  }
}

void _nexus_program_assert(const char *violation_type, NexusErrorCode code, const char *message, const char *file, uint32 line, ...) {
  va_list args;

  va_start(args, line);
  _nexus_program_assert_v(violation_type, code, message, file, line, args);
  va_end(args);
}
