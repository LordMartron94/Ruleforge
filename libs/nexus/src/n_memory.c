#define NEXUS_INTERNAL_MEMORY_IMPLEMENTATION
#include "../nexus.h"

#include <stb_sprintf.h>

#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if NEXUS_THREAD_SAFE
#  include <pthread.h>
#endif

#if NEXUS_PLATFORM_POSIX
#  include <errno.h>
#  include <sys/mman.h>
#  include <unistd.h>
#endif

typedef struct MemoryBlockHeader {
  struct MemoryBlockHeader *next;
  struct MemoryBlockHeader *prev;

  uint_large size;

  const char *file;
  uint32      line;

  /* Never a copy -- like file, this must point at a string literal or otherwise
     outlive the allocation. NULL until nexus_memory_comment_set is called. */
  const char *comment;

  /* Excludes this block from nexus_memory_leak_report without unlinking it from the
     live list, for state that is deliberately kept for the program's entire lifetime
     (e.g. the error pool, program_context) and must never be reported as a leak. */
  boolean persistent;

  uint64 checksum;
} MemoryBlockHeader;

static NEXUS_MAYBE_UNUSED MemoryBlockHeader *memory_block_head = NULL;

static const NEXUS_MAYBE_UNUSED uint_large header_size = NEXUS_SIZEOF(MemoryBlockHeader);

static NEXUS_MAYBE_UNUSED uint_large memory_current_bytes = 0;
static NEXUS_MAYBE_UNUSED uint_large memory_current_count = 0;
static NEXUS_MAYBE_UNUSED uint_large memory_peak_bytes    = 0;
static NEXUS_MAYBE_UNUSED uint_large memory_peak_count    = 0;
static NEXUS_MAYBE_UNUSED uint_large memory_total_bytes   = 0;
static NEXUS_MAYBE_UNUSED uint_large memory_total_allocs  = 0;
static NEXUS_MAYBE_UNUSED uint_large memory_total_frees   = 0;

typedef struct NexusMemoryCallsiteStats {
  const char *file;
  uint32      line;

  uint_large allocation_count;
  uint_large allocation_bytes;
  uint_large freed_count;
} NexusMemoryCallsiteStats;

static NEXUS_MAYBE_UNUSED NexusMemoryCallsiteStats *memory_callsites         = NULL;
static NEXUS_MAYBE_UNUSED uint_large                memory_callsite_count    = 0;
static NEXUS_MAYBE_UNUSED uint_large                memory_callsite_capacity = 0;

static NexusMemoryTraceFn *memory_trace_fn = NULL;

void nexus_memory_trace_function_set(NexusMemoryTraceFn *function) {
  memory_trace_fn = function;
}

#if defined(__GLIBC__) && !NEXUS_MEMORY_DEBUG_MODE
#  include <malloc.h>
#endif

void nexus_memory_retention_set(uint_large bytes) {
#if defined(__GLIBC__) && !NEXUS_MEMORY_DEBUG_MODE
  uint_large trim;
  uint_large mapped;

  trim   = bytes > (uint_large)0x7FFFFFFF ? (uint_large)0x7FFFFFFF : bytes;
  mapped = bytes > (uint_large)(32u * 1024u * 1024u) ? (uint_large)(32u * 1024u * 1024u) : bytes;

  if (bytes == 0) {
    /* glibc has no call that restores its dynamic thresholds; the defaults are what it starts with. */
    trim   = (uint_large)(128u * 1024u);
    mapped = (uint_large)(128u * 1024u);
  }

  (void)mallopt(M_TRIM_THRESHOLD, (int)trim);
  (void)mallopt(M_MMAP_THRESHOLD, (int)mapped);
#else
  (void)bytes;
#endif
}

/*
Guards every NEXUS_MEMORY_DEBUG_MODE structure above (the live-allocation list, the callsite
table, the running stats counters) -- the plain, non-debug-mode allocate/reallocate/deallocate
path below calls straight through to malloc/realloc/free, which are already thread-safe on
their own and touch none of this file's own state, so it stays lock-free. PTHREAD_MUTEX_INITIALIZER
for the same reason as the error pool's mutex in n_program.c: this guards plain static storage
that already needs no explicit initialization.
*/
#if NEXUS_THREAD_SAFE
static pthread_mutex_t n_memory_debug_mutex = PTHREAD_MUTEX_INITIALIZER;
#  define N_MEMORY_DEBUG_LOCK()   (void)pthread_mutex_lock(&n_memory_debug_mutex)
#  define N_MEMORY_DEBUG_UNLOCK() (void)pthread_mutex_unlock(&n_memory_debug_mutex)
#else
#  define N_MEMORY_DEBUG_LOCK()   ((void)0)
#  define N_MEMORY_DEBUG_UNLOCK() ((void)0)
#endif

/*
Finds the tracking entry for (file, line), creating one if this is the first allocation
ever seen from that exact call site. The table is grown via raw realloc, never through
nexus_memory_* -- like the quarantine ring, this is the allocator's own bookkeeping and
cannot depend on itself. file is compared by pointer, not content: __FILE__ at a given
source location always expands to the same string literal, the same identity test the
header checksum already relies on for this same field.
*/
static NEXUS_MAYBE_UNUSED NexusMemoryCallsiteStats *_nexus_memory_callsite_find_or_create(const char *file, uint32 line) {
  uint_large                i;
  NexusMemoryCallsiteStats *grown;

  for (i = 0; i < memory_callsite_count; i++) {
    if (memory_callsites[i].file == file && memory_callsites[i].line == line) {
      return &memory_callsites[i];
    }
  }

  if (memory_callsite_count == memory_callsite_capacity) {
    memory_callsite_capacity += 64;

    /* realloc into a temporary first: assigning straight into memory_callsites would lose
       the only pointer to the original buffer if realloc itself failed. */
    grown = (NexusMemoryCallsiteStats *)realloc(memory_callsites, memory_callsite_capacity * NEXUS_SIZEOF(NexusMemoryCallsiteStats));
    NEXUS_PROGRAM_POSTCONDITION_ALWAYS(grown != NULL, "Realloc returned NULL ptr while growing the memory callsite table (%s:%u)", file, line);

    memory_callsites = grown;
  }

  memory_callsites[memory_callsite_count].file             = file;
  memory_callsites[memory_callsite_count].line             = line;
  memory_callsites[memory_callsite_count].allocation_count = 0;
  memory_callsites[memory_callsite_count].allocation_bytes = 0;
  memory_callsites[memory_callsite_count].freed_count      = 0;

  return &memory_callsites[memory_callsite_count++];
}

static NEXUS_MAYBE_UNUSED void _nexus_memory_stats_record_allocate(uint_large size, const char *file, uint32 line) {
  NexusMemoryCallsiteStats *callsite;

  memory_current_bytes += size;
  memory_current_count += 1;
  memory_total_bytes += size;
  memory_total_allocs += 1;

  if (memory_current_bytes > memory_peak_bytes) {
    memory_peak_bytes = memory_current_bytes;
  }
  if (memory_current_count > memory_peak_count) {
    memory_peak_count = memory_current_count;
  }

  callsite = _nexus_memory_callsite_find_or_create(file, line);
  callsite->allocation_count += 1;
  callsite->allocation_bytes += size;
}

/* Takes the header, not a bare size, because it also needs the allocation's own file/line
   to credit the free against the same callsite entry the allocation was recorded under. */
static NEXUS_MAYBE_UNUSED void _nexus_memory_stats_record_free(MemoryBlockHeader *header) {
  NexusMemoryCallsiteStats *callsite;

  memory_current_bytes -= header->size;
  memory_current_count -= 1;
  memory_total_frees += 1;

  callsite = _nexus_memory_callsite_find_or_create(header->file, header->line);
  callsite->freed_count += 1;
}

static NEXUS_MAYBE_UNUSED uint64 _nexus_memory_header_checksum_compute(const MemoryBlockHeader *header) {
  uint64 accumulator;

  accumulator = (uint64)(uintptr_t)header->next;
  accumulator ^= (uint64)(uintptr_t)header->prev + 0x9e3779b97f4a7c15ULL + (accumulator << 6) + (accumulator >> 2);
  accumulator ^= header->size + 0x9e3779b97f4a7c15ULL + (accumulator << 6) + (accumulator >> 2);
  accumulator ^= (uint64)(uintptr_t)header->file + 0x9e3779b97f4a7c15ULL + (accumulator << 6) + (accumulator >> 2);
  accumulator ^= (uint64)header->line + 0x9e3779b97f4a7c15ULL + (accumulator << 6) + (accumulator >> 2);
  accumulator ^= (uint64)(uintptr_t)header->comment + 0x9e3779b97f4a7c15ULL + (accumulator << 6) + (accumulator >> 2);
  accumulator ^= (uint64)header->persistent + 0x9e3779b97f4a7c15ULL + (accumulator << 6) + (accumulator >> 2);

  accumulator = (accumulator ^ (accumulator >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
  accumulator = (accumulator ^ (accumulator >> 27)) * UINT64_C(0x94d049bb133111eb);
  return accumulator ^ (accumulator >> 31);
}

static NEXUS_MAYBE_UNUSED void _nexus_memory_header_validate(MemoryBlockHeader *header, const char *file, uint32 line) {
  NEXUS_MAYBE_UNUSED uint64 current_checksum;

  current_checksum = _nexus_memory_header_checksum_compute(header);

  NEXUS_PROGRAM_ASSERT_ALWAYS(
      current_checksum == header->checksum, "Allocation header got corrupted (%llu now vs %llu original) - site at: %s:%u", current_checksum,
      header->checksum, file,
      line); /* Use always assertions to prevent being compiled away in non-debug mode. This allows memory debugging in release-mode too. */
}

void *_nexus_memory_allocate(uint_large size, const NEXUS_MAYBE_UNUSED char *file, NEXUS_MAYBE_UNUSED uint32 line) {
  NEXUS_MAYBE_UNUSED MemoryBlockHeader *header;
  NEXUS_MAYBE_UNUSED void              *base_ptr;
  NEXUS_MAYBE_UNUSED uint64             i;

  void *user_ptr;

  NEXUS_PROGRAM_PRECONDITION(size > 0, "Allocating 0 is not allowed (%s:%u)", file, line);

#if NEXUS_MEMORY_DEBUG_MODE
  N_MEMORY_DEBUG_LOCK();

  base_ptr = malloc(header_size + NEXUS_MEMORY_PRE_PADDING_SIZE + size + NEXUS_MEMORY_POST_PADDING_SIZE);
  NEXUS_PROGRAM_POSTCONDITION_ALWAYS(base_ptr != NULL, "Malloc returned NULL ptr while trying to allocate %llu bytes at %s:%u", size, file, line);

  header = (MemoryBlockHeader *)base_ptr;

  header->prev = NULL;
  header->next = memory_block_head;

  /* We become the new head. */
  if (memory_block_head != NULL) {
    _nexus_memory_header_validate(memory_block_head, file, line);

    memory_block_head->prev = header;

    /* Update checksum. */
    memory_block_head->checksum = _nexus_memory_header_checksum_compute(memory_block_head);
  }

  memory_block_head = header;

  header->size       = size;
  header->file       = file;
  header->line       = line;
  header->comment    = NULL;
  header->persistent = FALSE;
  header->checksum   = _nexus_memory_header_checksum_compute(header);

  _nexus_memory_stats_record_allocate(size, file, line);

  /* Poison the the pre- and post-padding. */
  for (i = 0; i < NEXUS_MEMORY_PRE_PADDING_SIZE; i++) {
    ((byte *)NEXUS_MEMORY_OFFSET(base_ptr, header_size))[i] = NEXUS_MEMORY_MAGIC_NUMBER;
  }

  for (i = 0; i < NEXUS_MEMORY_POST_PADDING_SIZE; i++) {
    ((byte *)NEXUS_MEMORY_OFFSET(base_ptr, header_size + NEXUS_MEMORY_PRE_PADDING_SIZE + size))[i] = NEXUS_MEMORY_MAGIC_NUMBER;
  }

  /* Set memory to initialization ptr */
  memset(NEXUS_MEMORY_OFFSET(base_ptr, header_size + NEXUS_MEMORY_PRE_PADDING_SIZE), NEXUS_MEMORY_INITIALIZATION, size);

  user_ptr = NEXUS_MEMORY_OFFSET(base_ptr, header_size + NEXUS_MEMORY_PRE_PADDING_SIZE);

  N_MEMORY_DEBUG_UNLOCK();
#else

  user_ptr = malloc(size);

  NEXUS_PROGRAM_POSTCONDITION_ALWAYS(user_ptr != NULL, "Malloc returned NULL ptr while trying to allocate %llu bytes at %s:%u", size, file, line);
#endif

  /* Logged after the fact, not at entry: the postcondition above already aborted on OOM by
     this point, and only now do we actually know the pointer the caller receives -- a trace
     entry naming the requested size but not the resulting address would be far less useful
     for correlating this allocation against its later free. */
#if NEXUS_MEMORY_TRACING
  if (memory_trace_fn != NULL) {
    memory_trace_fn("Allocated %llu bytes at %p (%s:%u)", size, user_ptr, file, line);
  }
#endif

  return user_ptr;
}

void *_nexus_memory_hijack_calloc(uint_large count, uint_large size, const char *file, uint32 line) {
  void      *pointer;
  uint_large total_size;

  NEXUS_PROGRAM_PRECONDITION(count > 0, "_nexus_memory_hijack_calloc: count must be > 0 (%s:%u)", file, line);
  NEXUS_PROGRAM_PRECONDITION(size > 0, "_nexus_memory_hijack_calloc: size must be > 0 (%s:%u)", file, line);
  NEXUS_PROGRAM_PRECONDITION(count <= UINT_LARGE_MAX_VAL / size, "_nexus_memory_hijack_calloc: count * size overflows (%s:%u)", file, line);

  total_size = count * size;

  pointer = _nexus_memory_allocate(total_size, file, line);
  _nexus_memory_bytes_set(pointer, total_size, 0, file, line);

  return pointer;
}

static NEXUS_MAYBE_UNUSED void _nexus_memory_guards_validate(MemoryBlockHeader *header, void *ptr, const char *file, uint32 line) {
  byte  *buf;
  uint64 i;

  /* Check underruns and overruns*/
  buf = (byte *)ptr;
  for (i = 0; i < NEXUS_MEMORY_POST_PADDING_SIZE; i++) {
    if (buf[header->size + i] != NEXUS_MEMORY_MAGIC_NUMBER) {
      break;
    }
  }

  NEXUS_PROGRAM_ASSERT_ALWAYS(i == NEXUS_MEMORY_POST_PADDING_SIZE, "Memory overrun of allocation made on line %u in file %s - detected at %s:%u",
                              header->line, header->file, file, line);

  /* Move within the allocation once; mixing a negative offset with unsigned i wraps. */
  buf -= NEXUS_MEMORY_PRE_PADDING_SIZE;
  for (i = 0; i < NEXUS_MEMORY_PRE_PADDING_SIZE; i++) {
    if (buf[i] != NEXUS_MEMORY_MAGIC_NUMBER) {
      break;
    }
  }

  NEXUS_PROGRAM_ASSERT_ALWAYS(i == NEXUS_MEMORY_PRE_PADDING_SIZE, "Memory underrun of allocation made on line %u in file %s - detected at %s:%u",
                              header->line, header->file, file, line);
}

typedef struct NexusMemoryQuarantineEntry {
  void      *header_ptr;
  uint_large size;

  const char *alloc_file;
  uint32      alloc_line;

  const char *free_file;
  uint32      free_line;
} NexusMemoryQuarantineEntry;

static NEXUS_MAYBE_UNUSED NexusMemoryQuarantineEntry memory_quarantine[NEXUS_MEMORY_FREE_QUARANTINE_CAPACITY_ALLOCS];
static NEXUS_MAYBE_UNUSED uint_large                 memory_quarantine_count = 0;

/*
Walks every tracked allocation (live and quarantined) to classify ptr before anything is
ever computed or dereferenced from it: an exact match means ptr is genuinely ours and it's
safe to proceed; a pointer that lies strictly inside a tracked allocation's user-data range
without matching its start is an interior pointer (e.g. free(p + 20)) -- a real, distinct
programmer error, not corruption, and worth its own message; anything matching neither is a
pointer Nexus never handed out at all, which is UB to free/realloc under any C standard.
*/
static NEXUS_MAYBE_UNUSED void _nexus_memory_pointer_ownership_validate(void *ptr, const char *file, uint32 line) {
  byte              *buf;
  MemoryBlockHeader *node;
  byte              *alloc_buf;
  uint64             i;

  buf = (byte *)ptr;

  for (node = memory_block_head; node != NULL; node = node->next) {
    alloc_buf = (byte *)NEXUS_MEMORY_OFFSET(node, header_size + NEXUS_MEMORY_PRE_PADDING_SIZE);

    if ((uintptr_t)buf == (uintptr_t)alloc_buf) {
      return; /* Exact match against a live allocation -- ptr is ours. */
    }

    if ((uintptr_t)buf > (uintptr_t)alloc_buf && (uintptr_t)buf < (uintptr_t)(alloc_buf + node->size)) {
      NEXUS_PROGRAM_PANIC("Freeing/reallocating an interior pointer %p, %llu bytes into the allocation made at %s:%u - detected at %s:%u", ptr,
                          (uint64)((uintptr_t)buf - (uintptr_t)alloc_buf), node->file, node->line, file, line);
    }
  }

  for (i = 0; i < NEXUS_MEMORY_FREE_QUARANTINE_CAPACITY_ALLOCS; i++) {
    if (memory_quarantine[i].header_ptr == NULL) {
      break; /* Ring buffer invariant: no non-NULLs after the first NULL. */
    }

    alloc_buf = (byte *)NEXUS_MEMORY_OFFSET(memory_quarantine[i].header_ptr, header_size + NEXUS_MEMORY_PRE_PADDING_SIZE);

    if ((uintptr_t)buf == (uintptr_t)alloc_buf) {
      return; /* Exact match against a quarantined allocation -- the quarantine-identity
                 check reports the double-free/use-after-free specifically, not us. */
    }

    if ((uintptr_t)buf > (uintptr_t)alloc_buf && (uintptr_t)buf < (uintptr_t)(alloc_buf + memory_quarantine[i].size)) {
      NEXUS_PROGRAM_PANIC(
          "Freeing/reallocating an interior pointer %p, %llu bytes into an already-freed allocation made at %s:%u (freed at %s:%u) - detected at "
          "%s:%u",
          ptr, (uint64)((uintptr_t)buf - (uintptr_t)alloc_buf), memory_quarantine[i].alloc_file, memory_quarantine[i].alloc_line,
          memory_quarantine[i].free_file, memory_quarantine[i].free_line, file, line);
    }
  }

  NEXUS_PROGRAM_PANIC("Freeing/reallocating pointer %p that was never allocated through Nexus (%s:%u)", ptr, file, line);
}

static NEXUS_MAYBE_UNUSED void _nexus_memory_assert_not_in_quarantine(MemoryBlockHeader *header, void *ptr, const char *error_type, const char *file,
                                                                      uint32 line) {
  NexusMemoryQuarantineEntry scan_entry;
  uint64                     i;

  for (i = 0; i < NEXUS_MEMORY_FREE_QUARANTINE_CAPACITY_ALLOCS; i++) {
    scan_entry = memory_quarantine[i];

    if (scan_entry.header_ptr == NULL) {
      break; /* Continue is more intuitive here, but break is more efficient since it's a ringbuffer, when we encounter a NULL pointer the structure guarantees there are no non-NULLs afterward.*/
    }

    if (scan_entry.header_ptr == header) {
      NEXUS_PROGRAM_PANIC("[%s] Pointer %p was freed on %s:%u (allocation: %s:%u) - detected at %s:%u", error_type, ptr, scan_entry.free_file,
                          scan_entry.free_line, scan_entry.alloc_file, scan_entry.alloc_line, file, line);
    }
  }
}

void _nexus_memory_mark_persistent(NEXUS_MAYBE_UNUSED void *ptr, const NEXUS_MAYBE_UNUSED char *file, NEXUS_MAYBE_UNUSED uint32 line) {
  NEXUS_MAYBE_UNUSED MemoryBlockHeader *header;

  NEXUS_PROGRAM_PRECONDITION(ptr != NULL, "Don't mark NULL persistent (%s:%u)", file, line);

#if NEXUS_MEMORY_DEBUG_MODE
  /* The checksum covers next/prev, which other threads relink under this mutex while they allocate and free, so validating and rewriting the header has to happen under it too. */
  N_MEMORY_DEBUG_LOCK();

  _nexus_memory_pointer_ownership_validate(ptr, file, line);

  header = (MemoryBlockHeader *)((byte *)ptr - (NEXUS_MEMORY_PRE_PADDING_SIZE + header_size));

  _nexus_memory_assert_not_in_quarantine(header, ptr, "USE-AFTER-FREE", file, line);
  _nexus_memory_header_validate(header, file, line);
  _nexus_memory_guards_validate(header, ptr, file, line);

  header->persistent = TRUE;
  header->checksum   = _nexus_memory_header_checksum_compute(header);

  N_MEMORY_DEBUG_UNLOCK();
#endif

#if NEXUS_MEMORY_TRACING
  if (memory_trace_fn != NULL) {
    memory_trace_fn("Marked pointer %p persistent (%s:%u)", ptr, file, line);
  }
#endif
}

void _nexus_memory_comment_set(NEXUS_MAYBE_UNUSED void *ptr, const char *comment, const NEXUS_MAYBE_UNUSED char *file,
                               NEXUS_MAYBE_UNUSED uint32 line) {
  NEXUS_MAYBE_UNUSED MemoryBlockHeader *header;

  NEXUS_PROGRAM_PRECONDITION(ptr != NULL, "Don't comment NULL (%s:%u)", file, line);

#if NEXUS_MEMORY_DEBUG_MODE
  /* Same reason as in mark_persistent: the header's checksum depends on list links that other threads change under the mutex. */
  N_MEMORY_DEBUG_LOCK();

  _nexus_memory_pointer_ownership_validate(ptr, file, line);

  header = (MemoryBlockHeader *)((byte *)ptr - (NEXUS_MEMORY_PRE_PADDING_SIZE + header_size));

  _nexus_memory_assert_not_in_quarantine(header, ptr, "USE-AFTER-FREE", file, line);
  _nexus_memory_header_validate(header, file, line);
  _nexus_memory_guards_validate(header, ptr, file, line);

  header->comment  = comment;
  header->checksum = _nexus_memory_header_checksum_compute(header);

  N_MEMORY_DEBUG_UNLOCK();
#else
  (void)comment;
#endif

#if NEXUS_MEMORY_TRACING
  if (memory_trace_fn != NULL) {
    memory_trace_fn("Set comment for pointer %p to \"%s\" (%s:%u)", ptr, (comment != NULL) ? comment : "(null)", file, line);
  }
#endif
}

/*
Moves a header out of the live list and into quarantine instead of ever calling libc's
free() on it directly: evicts (and truly frees) whichever entry currently occupies the next
ring slot, records this block's alloc/free sites, then poisons the entire physical block now
that every field we still need is safely copied into the quarantine entry.
*/
static NEXUS_MAYBE_UNUSED void _nexus_memory_quarantine_insert(MemoryBlockHeader *header, const char *free_file, uint32 free_line) {
  if (memory_quarantine_count == NEXUS_MEMORY_FREE_QUARANTINE_CAPACITY_ALLOCS) {
    /* reset to 0 */
    memory_quarantine_count = 0;
  }

  if (memory_quarantine[memory_quarantine_count].header_ptr != NULL) {
    free(memory_quarantine[memory_quarantine_count].header_ptr); /* Free the old slot. */
  }

  memory_quarantine[memory_quarantine_count].header_ptr = header;
  memory_quarantine[memory_quarantine_count].size       = header->size;
  memory_quarantine[memory_quarantine_count].alloc_file = header->file;
  memory_quarantine[memory_quarantine_count].alloc_line = header->line;
  memory_quarantine[memory_quarantine_count].free_file  = free_file;
  memory_quarantine[memory_quarantine_count].free_line  = free_line;

  _nexus_memory_stats_record_free(header);

  /* Poison the entire physical block (header, guards, and user data) now that every
     field we still need has been copied out into the quarantine entry above -- a stray
     read or write through a dangling pointer lands on a recognizable pattern instead of
     memory that still looks like a valid, live allocation. */
  memset(header, NEXUS_MEMORY_FREED, header_size + NEXUS_MEMORY_PRE_PADDING_SIZE + header->size + NEXUS_MEMORY_POST_PADDING_SIZE);

  memory_quarantine_count++;
}

void *_nexus_memory_reallocate(void *ptr, uint_large new_size, const NEXUS_MAYBE_UNUSED char *file, NEXUS_MAYBE_UNUSED uint32 line) {
  NEXUS_MAYBE_UNUSED MemoryBlockHeader *header;
  NEXUS_MAYBE_UNUSED uint64             i;

  void *user_ptr;

  NEXUS_PROGRAM_PRECONDITION(ptr != NULL, "Use nexus_memory_allocate for initial allocations, don't reallocate NULL (%s:%u)", file, line);
  NEXUS_PROGRAM_PRECONDITION(new_size > 0, "Reallocating 0 is UB since C23 (%s:%u)", file, line);

#if NEXUS_MEMORY_DEBUG_MODE
  N_MEMORY_DEBUG_LOCK();

  _nexus_memory_pointer_ownership_validate(ptr, file, line);

  header = (MemoryBlockHeader *)((byte *)ptr - (NEXUS_MEMORY_PRE_PADDING_SIZE + header_size));

  /* Check quarantine membership by pointer identity first -- it never dereferences
     header, so it's safe to run before we know whether header is still ours to read. */
  _nexus_memory_assert_not_in_quarantine(header, ptr, "USE-AFTER-FREE", file, line);

  _nexus_memory_header_validate(header, file, line);
  _nexus_memory_guards_validate(header, ptr, file, line);

  /* Because we store our headers before the memory and also include guards, we cannot just use realloc... We need to free + malloc ourselves. */
  {
    void              *new_base_ptr;
    MemoryBlockHeader *new_header;
    uint_large         copy_size;

    new_base_ptr = malloc(header_size + NEXUS_MEMORY_PRE_PADDING_SIZE + new_size + NEXUS_MEMORY_POST_PADDING_SIZE);

    NEXUS_PROGRAM_POSTCONDITION_ALWAYS(new_base_ptr != NULL, "Realloc returned NULL ptr while trying to reallocate %llu bytes at %s:%u", new_size,
                                       file, line);

    new_header = (MemoryBlockHeader *)new_base_ptr;

    new_header->prev       = header->prev;
    new_header->next       = header->next;
    new_header->file       = header->file;
    new_header->line       = header->line;
    new_header->size       = new_size;
    new_header->comment    = header->comment; /* Carried forward -- a reallocated block is still the same logical allocation. */
    new_header->persistent = header->persistent;
    new_header->checksum   = _nexus_memory_header_checksum_compute(new_header);

    /* Credited to this realloc call's own site, not header->file/line (the original
       allocation site, which new_header keeps) -- a loop that keeps reallocating the same
       pointer should show up as a hotspot at the realloc call site itself. */
    _nexus_memory_stats_record_allocate(new_size, file, line);

    /* Poison the the pre- and post-padding. */
    for (i = 0; i < NEXUS_MEMORY_PRE_PADDING_SIZE; i++) {
      ((byte *)NEXUS_MEMORY_OFFSET(new_base_ptr, header_size))[i] = NEXUS_MEMORY_MAGIC_NUMBER;
    }

    for (i = 0; i < NEXUS_MEMORY_POST_PADDING_SIZE; i++) {
      ((byte *)NEXUS_MEMORY_OFFSET(new_base_ptr, header_size + NEXUS_MEMORY_PRE_PADDING_SIZE + new_size))[i] = NEXUS_MEMORY_MAGIC_NUMBER;
    }

    user_ptr = NEXUS_MEMORY_OFFSET(new_base_ptr, header_size + NEXUS_MEMORY_PRE_PADDING_SIZE);

    /* Copy old memory over -- clamp to the smaller of the two sizes so a shrinking
       realloc never reads/writes past either block's user-data region. */
    copy_size = (header->size < new_size) ? header->size : new_size;
    memcpy(user_ptr, ptr, copy_size);

    /* Set newly grown memory to the initialization pattern; shrinking leaves nothing new to poison. */
    if (new_size > header->size) {
      memset(NEXUS_MEMORY_OFFSET(user_ptr, header->size), NEXUS_MEMORY_INITIALIZATION, new_size - header->size);
    }

    /* Update linked list. */
    if (header->next != NULL) {
      /* First verify the header is uncorrupted... */
      _nexus_memory_header_validate(header->next, file, line);

      header->next->prev     = new_header;
      header->next->checksum = _nexus_memory_header_checksum_compute(header->next);
    }

    if (header->prev != NULL) {
      /* First verify the header is uncorrupted... */
      _nexus_memory_header_validate(header->prev, file, line);

      header->prev->next     = new_header;
      header->prev->checksum = _nexus_memory_header_checksum_compute(header->prev);
    } else {
      /* header was the list head; the head pointer must follow it to new_header. */
      memory_block_head = new_header;
    }

    _nexus_memory_quarantine_insert(header, file, line); /* Quarantine the old ptr instead of freeing it outright. */
  }

  N_MEMORY_DEBUG_UNLOCK();
#else
  user_ptr = realloc(ptr, new_size);

  NEXUS_PROGRAM_POSTCONDITION_ALWAYS(user_ptr != NULL, "Realloc returned NULL ptr while trying to reallocate %llu bytes at %s:%u", new_size, file,
                                     line);
#endif

  /* Logged after the fact: debug mode never resizes in place (see the block comment above),
     so user_ptr is essentially always a different address than ptr, and a trace entry that
     only named the old pointer would give no way to tell what the caller actually now holds. */
#if NEXUS_MEMORY_TRACING
  if (memory_trace_fn != NULL) {
    memory_trace_fn("Reallocated pointer %p to %p, %llu bytes (%s:%u)", ptr, user_ptr, new_size, file, line);
  }
#endif

  return user_ptr;
}

void _nexus_memory_deallocate(void *ptr, const NEXUS_MAYBE_UNUSED char *file, NEXUS_MAYBE_UNUSED uint32 line) {
  NEXUS_MAYBE_UNUSED MemoryBlockHeader *header;
  NEXUS_MAYBE_UNUSED uint_large         freed_size;

  NEXUS_PROGRAM_PRECONDITION(ptr != NULL, "Don't free NULL (%s:%u)", file, line);

#if NEXUS_MEMORY_DEBUG_MODE
  N_MEMORY_DEBUG_LOCK();

  _nexus_memory_pointer_ownership_validate(ptr, file, line);

  header = (MemoryBlockHeader *)((byte *)ptr - (NEXUS_MEMORY_PRE_PADDING_SIZE + header_size));

  /* Check quarantine membership by pointer identity first -- it never dereferences
     header, so it's safe to run before we know whether header is still ours to read. */
  _nexus_memory_assert_not_in_quarantine(header, ptr, "DOUBLE-FREE", file, line);

  _nexus_memory_header_validate(header, file, line);
  _nexus_memory_guards_validate(header, ptr, file, line);

  if (header->next != NULL) {
    /* First verify the header is uncorrupted... */
    _nexus_memory_header_validate(header->next, file, line);

    header->next->prev     = header->prev; /* This can safely be NULL */
    header->next->checksum = _nexus_memory_header_checksum_compute(header->next);
  }

  if (header->prev != NULL) {
    /* First verify the header is uncorrupted... */
    _nexus_memory_header_validate(header->prev, file, line);

    header->prev->next     = header->next; /* This can safely be NULL */
    header->prev->checksum = _nexus_memory_header_checksum_compute(header->prev);
  } else {
    memory_block_head = header->next;
  }

  /* Captured here, not read after quarantine_insert: that call poisons header's own memory
     as its last step, so header->size has to be read before it, not strictly at the end of
     the function. */
  freed_size = header->size;

  _nexus_memory_quarantine_insert(header, file, line); /* Don't free the header directly, quarantine it instead. */

  N_MEMORY_DEBUG_UNLOCK();
#else
  free(ptr);
#endif

  /* Logged after N_MEMORY_DEBUG_UNLOCK, not before: memory_trace_fn may call back into
     nexus_memory_* on this same thread (e.g. Echo lazily allocating this thread's own
     diagnostic context the first time it traces), and n_memory_debug_mutex is a plain,
     non-recursive mutex -- firing the callback while still holding it self-deadlocks the
     first time that happens. allocate/reallocate already unlock before tracing; this matches. */
#if NEXUS_MEMORY_TRACING
  if (memory_trace_fn != NULL) {
#  if NEXUS_MEMORY_DEBUG_MODE
    memory_trace_fn("Deallocated pointer %p, %llu bytes (%s:%u)", ptr, freed_size, file, line);
#  else
    /* No size here -- unlike allocate/reallocate, deallocate has no size parameter of its own,
       and outside NEXUS_MEMORY_DEBUG_MODE there is no header to read one from either. */
    memory_trace_fn("Deallocated pointer %p (%s:%u)", ptr, file, line);
#  endif
  }
#endif
}

void nexus_memory_foreign_free(void *ptr) {
  if (ptr == NULL) {
    return;
  }

  free(ptr);
}

#if NEXUS_PLATFORM_POSIX

static int _n_memory_protection_to_native(NexusMemoryProtection protection) {
  int native_protection = PROT_NONE;

  if (protection & NMP_READ) {
    native_protection |= PROT_READ;
  }
  if (protection & NMP_WRITE) {
    native_protection |= PROT_WRITE;
  }
  if (protection & NMP_EXEC) {
    native_protection |= PROT_EXEC;
  }

  return native_protection;
}

static int _n_memory_sharing_to_native(NexusMemorySharing sharing) {
  switch (sharing) {
  case NMS_SHARED:
    return MAP_SHARED;

  case NMS_PRIVATE:
  default:
    return MAP_PRIVATE;
  }
}

uint_large nexus_memory_page_size_get(void) {
  long page_size;

  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_memory_page_size_get");

  /* glibc already caches this (populated from the auxiliary vector at process start, not a
     real syscall per call) -- caching it again here would just be solving an already-solved
     problem. */
  page_size = sysconf(_SC_PAGESIZE);
  NEXUS_PROGRAM_POSTCONDITION_ALWAYS(page_size > 0, "sysconf(_SC_PAGESIZE) failed");

  NEXUS_PROGRAM_CONTEXT_POP();
  return (uint_large)page_size;
}

NexusError *nexus_memory_map(NexusFileHandle *file_handle, uint_large size, NexusMemoryProtection protection, NexusMemorySharing sharing,
                             void **out_pointer, uint_large *out_mapped_size) {
  void       *mapped;
  NexusError *error;
  int         native_flags;
  int         native_descriptor;
  uint_large  file_size;

  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_memory_map");
  NEXUS_PROGRAM_PRECONDITION(size > 0, "memory_map: size must be > 0");
  NEXUS_PROGRAM_PRECONDITION(out_pointer != NULL, "memory_map: out_pointer must not be NULL");
  NEXUS_PROGRAM_PRECONDITION(out_mapped_size != NULL, "memory_map: out_mapped_size must not be NULL");
  NEXUS_PROGRAM_PRECONDITION((uint_large)(size_t)size == size, "memory_map: size exceeds size_t");

  *out_pointer     = NULL;
  *out_mapped_size = 0;

  native_flags      = _n_memory_sharing_to_native(sharing);
  native_descriptor = -1;

  if (file_handle != NULL) {
    error = nexus_filesystem_file_size_get(file_handle, &file_size);
    if (error->code != GLOBAL_NEXUS_ERROR_SUCCESS) {
      NEXUS_PROGRAM_CONTEXT_POP();
      return error;
    }

    if (size > file_size) {
      error = NEXUS_PROGRAM_ERROR_FAILURE(NEXUS_FILESYSTEM_ERROR_FILE_TOO_SMALL,
                                          "cannot map %llu bytes from a %llu-byte file; resize it explicitly first", (unsigned long long)size,
                                          (unsigned long long)file_size);
      NEXUS_PROGRAM_CONTEXT_POP();
      return error;
    }

    native_descriptor = (int)nexus_filesystem_file_descriptor_get(file_handle);
  } else {
    native_flags |= MAP_ANONYMOUS;
  }

  mapped = mmap(NULL, (size_t)size, _n_memory_protection_to_native(protection), native_flags, native_descriptor, 0);
  if (mapped == MAP_FAILED) {
    error = NEXUS_PROGRAM_ERROR_FROM_ERRNO("failed to map %llu bytes", (unsigned long long)size);
    NEXUS_PROGRAM_CONTEXT_POP();
    return error;
  }

  *out_pointer     = mapped;
  *out_mapped_size = size;

  NEXUS_PROGRAM_CONTEXT_POP();
  return &nexus_program_errors_success;
}

NexusError *nexus_memory_unmap(void *pointer, uint_large size) {
  NexusError *error;

  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_memory_unmap");
  NEXUS_PROGRAM_PRECONDITION(pointer != NULL, "memory_unmap: pointer must not be NULL");
  NEXUS_PROGRAM_PRECONDITION(size > 0, "memory_unmap: size must be > 0");
  NEXUS_PROGRAM_PRECONDITION((uint_large)(size_t)size == size, "memory_unmap: size exceeds size_t");

  if (munmap(pointer, (size_t)size) != 0) {
    error = NEXUS_PROGRAM_ERROR_FROM_ERRNO("failed to unmap %llu bytes at %p", (unsigned long long)size, pointer);
    NEXUS_PROGRAM_CONTEXT_POP();
    return error;
  }

  NEXUS_PROGRAM_CONTEXT_POP();
  return &nexus_program_errors_success;
}

NexusError *nexus_memory_protect(void *pointer, uint_large size, NexusMemoryProtection protection) {
  NexusError *error;

  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_memory_protect");
  NEXUS_PROGRAM_PRECONDITION(pointer != NULL, "memory_protect: pointer must not be NULL");
  NEXUS_PROGRAM_PRECONDITION(size > 0, "memory_protect: size must be > 0");
  NEXUS_PROGRAM_PRECONDITION((uint_large)(size_t)size == size, "memory_protect: size exceeds size_t");

  if (mprotect(pointer, (size_t)size, _n_memory_protection_to_native(protection)) != 0) {
    error = NEXUS_PROGRAM_ERROR_FROM_ERRNO("failed to change protection of %llu bytes at %p", (unsigned long long)size, pointer);
    NEXUS_PROGRAM_CONTEXT_POP();
    return error;
  }

  NEXUS_PROGRAM_CONTEXT_POP();
  return &nexus_program_errors_success;
}

#else

uint_large nexus_memory_page_size_get(void) {
  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_memory_page_size_get");
  NEXUS_PLATFORM_UNSUPPORTED_RAISE("memory_page_size_get");
}
NexusError *nexus_memory_map(NexusFileHandle *file_handle, uint_large size, NexusMemoryProtection protection, NexusMemorySharing sharing,
                             void **out_pointer, uint_large *out_mapped_size) {
  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_memory_map");
  NEXUS_PLATFORM_UNSUPPORTED_RAISE("memory_map");
}
NexusError *nexus_memory_unmap(void *pointer, uint_large size) {
  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_memory_unmap");
  NEXUS_PLATFORM_UNSUPPORTED_RAISE("memory_unmap");
}
NexusError *nexus_memory_protect(void *pointer, uint_large size, NexusMemoryProtection protection) {
  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_memory_protect");
  NEXUS_PLATFORM_UNSUPPORTED_RAISE("memory_protect");
}

#endif

void _nexus_memory_bytes_set(void *ptr, uint_large size, byte value, const NEXUS_MAYBE_UNUSED char *file, NEXUS_MAYBE_UNUSED uint32 line) {
  NEXUS_PROGRAM_PRECONDITION(ptr != NULL, "Don't set NULL (%s:%u)", file, line);

#if NEXUS_MEMORY_TRACING
  if (memory_trace_fn != NULL) {
    memory_trace_fn("Setting %llu bytes for pointer %p to %u at %s:%u", size, ptr, value, file, line);
  }
#endif

  memset(ptr, value, size);
}

void _nexus_memory_bytes_copy(const void *src_ptr, void *dest_ptr, uint_large size, const NEXUS_MAYBE_UNUSED char *file,
                              NEXUS_MAYBE_UNUSED uint32 line) {
  NEXUS_PROGRAM_PRECONDITION(src_ptr != NULL, "Don't copy from NULL (%s:%u)", file, line);
  NEXUS_PROGRAM_PRECONDITION(dest_ptr != NULL, "Don't copy to NULL (%s:%u)", file, line);

#if NEXUS_MEMORY_TRACING
  if (memory_trace_fn != NULL) {
    memory_trace_fn("Copying %llu bytes from pointer %p to %p at %s:%u", size, src_ptr, dest_ptr, file, line);
  }
#endif

  memcpy(dest_ptr, src_ptr, size);
}

void _nexus_memory_bytes_move(const void *src_ptr, void *dest_ptr, uint_large size, const NEXUS_MAYBE_UNUSED char *file,
                              NEXUS_MAYBE_UNUSED uint32 line) {
  NEXUS_PROGRAM_PRECONDITION(src_ptr != NULL, "Don't move from NULL (%s:%u)", file, line);
  NEXUS_PROGRAM_PRECONDITION(dest_ptr != NULL, "Don't move to NULL (%s:%u)", file, line);

#if NEXUS_MEMORY_TRACING
  if (memory_trace_fn != NULL) {
    memory_trace_fn("Moving %llu bytes from pointer %p to %p at %s:%u", (uint64)size, src_ptr, dest_ptr, file, line);
  }
#endif

  memmove(dest_ptr, src_ptr, size);
}

void _nexus_memory_bytes_copy_untraced(const void *src_ptr, void *dest_ptr, uint_large size, const NEXUS_MAYBE_UNUSED char *file,
                                       NEXUS_MAYBE_UNUSED uint32 line) {
  NEXUS_PROGRAM_PRECONDITION(src_ptr != NULL, "Don't copy from NULL (%s:%u)", file, line);
  NEXUS_PROGRAM_PRECONDITION(dest_ptr != NULL, "Don't copy to NULL (%s:%u)", file, line);

  memcpy(dest_ptr, src_ptr, size);
}

boolean _nexus_memory_bytes_equal(const void *left_ptr, const void *right_ptr, uint_large size, const NEXUS_MAYBE_UNUSED char *file,
                                  NEXUS_MAYBE_UNUSED uint32 line) {
  NEXUS_PROGRAM_PRECONDITION(left_ptr != NULL, "Don't compare NULL (%s:%u)", file, line);
  NEXUS_PROGRAM_PRECONDITION(right_ptr != NULL, "Don't compare NULL (%s:%u)", file, line);

  return memcmp(left_ptr, right_ptr, size) == 0;
}

/* Formats bytes using the largest unit (B, KiB, MiB, GiB, TiB) for which the value is at
   least 1, matching the conventional file-browser presentation. */
static NEXUS_MAYBE_UNUSED void _nexus_memory_format_bytes(char *buffer, int buffer_size, uint64 bytes) {
  static const char *const units[] = {"B", "KiB", "MiB", "GiB", "TiB"};
  real64                   scaled;
  uint64                   unit_index;

  scaled     = (real64)bytes;
  unit_index = 0;

  while (scaled >= 1024.0 && unit_index < 4) {
    scaled /= 1024.0;
    unit_index++;
  }

  if (unit_index == 0) {
    stbsp_snprintf(buffer, buffer_size, "%llu B", bytes);
  } else {
    stbsp_snprintf(buffer, buffer_size, "%.2f %s", scaled, units[unit_index]);
  }
}

/*
Appends one formatted message to buffer, advancing *written and *would_write exactly like every
report function used to do inline at each of its own call sites -- extracted here because
adding per-value unit formatting on top of that already-repeated pattern would have pushed
every call site to six-plus duplicated lines apiece.
*/
static NEXUS_MAYBE_UNUSED void _nexus_memory_report_append(char *buffer, uint_large buffer_size, uint_large *written, uint_large *would_write,
                                                           const char *format, ...) {
  uint_large buffer_size_remaining;
  char      *destination;
  int        message_length;
  va_list    args;

  buffer_size_remaining = (*written < buffer_size) ? (buffer_size - *written) : 0;
  /* stbsp_vsnprintf underflows one byte before a non-NULL buffer when given
   * count == 0 -- once the real buffer is exhausted, switch to the
   * documented count-only mode (NULL buffer) instead of a zero-capacity
   * pointer into the middle of it. */
  destination = (buffer_size_remaining > 0) ? (buffer + *written) : NULL;

  va_start(args, format);
  message_length = stbsp_vsnprintf(destination, (int)buffer_size_remaining, format, args);
  va_end(args);

  *would_write += (uint_large)message_length;
  if (buffer_size_remaining > 0) {
    *written += ((uint_large)message_length < buffer_size_remaining) ? (uint_large)message_length : buffer_size_remaining - 1;
  }
}

uint_large nexus_memory_leak_report(char *buffer, uint_large buffer_size) {
  uint_large         written;
  uint_large         would_write;
  uint_large         leak_count;
  uint_large         leaked_bytes;
  MemoryBlockHeader *node;
  char               bytes_formatted[32];

  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_memory_leak_report");
  NEXUS_PROGRAM_PRECONDITION(buffer != NULL, "Buffer must not be null.");

  written      = 0;
  would_write  = 0;
  leak_count   = 0;
  leaked_bytes = 0;

  /* Locked for the whole walk (and the second one below): memory_block_head is the same list
     allocate/reallocate/deallocate mutate under this lock, and walking it while another thread
     concurrently unlinks a node would be a use-after-free, not just a stale read. */
  N_MEMORY_DEBUG_LOCK();

  for (node = memory_block_head; node != NULL; node = node->next) {
    if (node->persistent) {
      continue;
    }

    leak_count++;
    leaked_bytes += node->size;
  }

  _nexus_memory_format_bytes(bytes_formatted, (int)NEXUS_SIZEOF(bytes_formatted), leaked_bytes);

  _nexus_memory_report_append(buffer, buffer_size, &written, &would_write, "Memory leak report: %llu leak(s), %s still live\n", leak_count,
                              bytes_formatted);
  _nexus_memory_report_append(buffer, buffer_size, &written, &would_write, "----------------------------------------------\n");

  for (node = memory_block_head; node != NULL; node = node->next) {
    if (node->persistent) {
      continue;
    }

    _nexus_memory_format_bytes(bytes_formatted, (int)NEXUS_SIZEOF(bytes_formatted), node->size);

    _nexus_memory_report_append(buffer, buffer_size, &written, &would_write, "%s:%u\n", node->file, node->line);
    _nexus_memory_report_append(buffer, buffer_size, &written, &would_write, " - Size: %s\n", bytes_formatted);
    _nexus_memory_report_append(buffer, buffer_size, &written, &would_write, " - Address: %p\n",
                                NEXUS_MEMORY_OFFSET(node, header_size + NEXUS_MEMORY_PRE_PADDING_SIZE));

    if (node->comment != NULL) {
      _nexus_memory_report_append(buffer, buffer_size, &written, &would_write, " - Comment: %s\n", node->comment);
    }

    _nexus_memory_report_append(buffer, buffer_size, &written, &would_write, "\n");
  }

  _nexus_memory_report_append(buffer, buffer_size, &written, &would_write, "----------------------------------------------\n");

  N_MEMORY_DEBUG_UNLOCK();

  NEXUS_PROGRAM_CONTEXT_POP();
  return would_write;
}

uint_large nexus_memory_summary_report(char *buffer, uint_large buffer_size, uint_large hotspot_min_allocations) {
  uint_large written;
  uint_large would_write;
  uint_large i;
  boolean    any_hotspot;
  char       current_formatted[32];
  char       peak_formatted[32];
  char       total_formatted[32];
  char       hotspot_bytes_formatted[32];

  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_memory_summary_report");
  NEXUS_PROGRAM_PRECONDITION(buffer != NULL, "Buffer must not be null.");

  written     = 0;
  would_write = 0;
  any_hotspot = FALSE;

  /* Locked for the whole function: memory_callsites/memory_callsite_count can grow (realloc)
     under this same lock elsewhere, and the running counters are updated under it too. */
  N_MEMORY_DEBUG_LOCK();

  _nexus_memory_format_bytes(current_formatted, (int)NEXUS_SIZEOF(current_formatted), memory_current_bytes);
  _nexus_memory_format_bytes(peak_formatted, (int)NEXUS_SIZEOF(peak_formatted), memory_peak_bytes);
  _nexus_memory_format_bytes(total_formatted, (int)NEXUS_SIZEOF(total_formatted), memory_total_bytes);

  _nexus_memory_report_append(buffer, buffer_size, &written, &would_write,
                              "Memory summary\n"
                              "----------------------------------------------\n"
                              " - Current: %llu allocation(s), %s live\n"
                              " - Peak: %llu allocation(s), %s\n"
                              " - Total: %llu allocation(s), %llu free(s), %s allocated\n"
                              "\n"
                              "Hotspots (>= %llu allocations from a single call site):\n"
                              "----------------------------------------------\n",
                              memory_current_count, current_formatted, memory_peak_count, peak_formatted, memory_total_allocs, memory_total_frees,
                              total_formatted, hotspot_min_allocations);

  /* Not sorted by count -- reported in first-seen order, the same as Forge's own
     per-callsite report. A line that allocates repeatedly (a loop that should be hoisting
     or reusing a buffer instead) crosses hotspot_min_allocations and is worth checking;
     one that only ever runs once never will, no matter how large its allocation is. */
  for (i = 0; i < memory_callsite_count; i++) {
    if (memory_callsites[i].allocation_count < hotspot_min_allocations) {
      continue;
    }

    any_hotspot = TRUE;

    _nexus_memory_format_bytes(hotspot_bytes_formatted, (int)NEXUS_SIZEOF(hotspot_bytes_formatted), memory_callsites[i].allocation_bytes);

    _nexus_memory_report_append(buffer, buffer_size, &written, &would_write, "%s:%u\n", memory_callsites[i].file, memory_callsites[i].line);
    _nexus_memory_report_append(buffer, buffer_size, &written, &would_write, " - Allocations: %llu\n", memory_callsites[i].allocation_count);
    _nexus_memory_report_append(buffer, buffer_size, &written, &would_write, " - Freed: %llu\n", memory_callsites[i].freed_count);
    _nexus_memory_report_append(buffer, buffer_size, &written, &would_write, " - Total: %s\n", hotspot_bytes_formatted);
    _nexus_memory_report_append(buffer, buffer_size, &written, &would_write, " - Note: check whether this runs inside a loop\n\n");
  }

  if (!any_hotspot) {
    _nexus_memory_report_append(buffer, buffer_size, &written, &would_write, "  (none)\n");
  }

  _nexus_memory_report_append(buffer, buffer_size, &written, &would_write, "----------------------------------------------\n");

  N_MEMORY_DEBUG_UNLOCK();

  NEXUS_PROGRAM_CONTEXT_POP();
  return would_write;
}
