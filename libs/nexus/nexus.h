/*
Some of the implementation in this library is LLM-generated.
Nexus is reused as foundational infrastructure rather than a domain project.
*/

#pragma once

#include <stdarg.h>

/* ============================================================================ */
/* ---------------------------------------------------------------------------- */
/* GLOBAL DEFINES                                                               */
/* ---------------------------------------------------------------------------- */
/* ============================================================================ */

/*
These typedefs have (in part) been sourced from Eskil Steenberg's Forge.
*/

/* PLATFORM DETECTION */

#ifndef NEXUS_PLATFORMS
#  define NEXUS_PLATFORMS

#  if defined(_WIN32) || defined(_WIN64)
#    define NEXUS_PLATFORM_WINDOWS 1
#  elif defined(__ANDROID__)
#    define NEXUS_PLATFORM_ANDROID 1
#  elif defined(__APPLE__) && defined(__MACH__)
#    if defined(__ENVIRONMENT_IPHONE_OS_VERSION_MIN_REQUIRED__) || defined(__ENVIRONMENT_TV_OS_VERSION_MIN_REQUIRED__)
#      define NEXUS_PLATFORM_IOS 1
#    else
#      define NEXUS_PLATFORM_MACOS 1
#    endif
#  elif defined(__FreeBSD__)
#    define NEXUS_PLATFORM_FREEBSD 1
#    define NEXUS_PLATFORM_BSD     1
#  elif defined(__NetBSD__)
#    define NEXUS_PLATFORM_NETBSD 1
#    define NEXUS_PLATFORM_BSD    1
#  elif defined(__OpenBSD__)
#    define NEXUS_PLATFORM_OPENBSD 1
#    define NEXUS_PLATFORM_BSD     1
#  elif defined(__DragonFly__)
#    define NEXUS_PLATFORM_DRAGONFLY 1
#    define NEXUS_PLATFORM_BSD       1
#  elif defined(__bsdi__) || defined(__bsdi)
#    define NEXUS_PLATFORM_BSDI 1
#    define NEXUS_PLATFORM_BSD  1
#  elif defined(__linux__)
#    define NEXUS_PLATFORM_LINUX 1
#  elif defined(__unix__) || defined(__unix)
#    /* generic UNIX fallback; could be some other BSD or System V UNIX */
#    define NEXUS_PLATFORM_UNIX 1
#  else
#    define NEXUS_PLATFORM_UNKNOWN 1
#  endif

#  if defined(NEXUS_PLATFORM_LINUX) || defined(NEXUS_PLATFORM_MACOS) || defined(NEXUS_PLATFORM_ANDROID)
#    define NEXUS_PLATFORM_POSIX 1
#  endif
#endif /* PLATFORM DETECTION */

#ifdef NEXUS_PLATFORM_POSIX
#  ifndef _DEFAULT_SOURCE
#    define _DEFAULT_SOURCE /* NOLINT */
#  endif
#endif

#ifndef NEXUS_ARCHITECTURES
#  define NEXUS_ARCHITECTURES

/* Adapted from: https://stackoverflow.com/a/66249936 */
/* RISC-V macros: __riscv, __riscv32, __riscv64, __riscv_xlen (GCC/Clang); see
   https://groups.google.com/a/groups.riscv.org/g/sw-dev/c/r4cUgIhWLbY */

#  define NEXUS_ARCH_UNKNOWN (-1)

/* --- X86 --- */
#  define NEXUS_ARCH_X86_64 1
#  define NEXUS_ARCH_X86_32 2

/* --- ARM --- */
#  define NEXUS_ARCH_ARM2   3
#  define NEXUS_ARCH_ARM3   4
#  define NEXUS_ARCH_ARM4T  5
#  define NEXUS_ARCH_ARM5   6
#  define NEXUS_ARCH_ARM6T2 7
#  define NEXUS_ARCH_ARM7   8
#  define NEXUS_ARCH_ARM7A  9
#  define NEXUS_ARCH_ARM7R  10
#  define NEXUS_ARCH_ARM7M  11
#  define NEXUS_ARCH_ARM7S  12
#  define NEXUS_ARCH_ARM64  13

/* --- MISC --- */
#  define NEXUS_ARCH_MIPS      14
#  define NEXUS_ARCH_SUPERH    15
#  define NEXUS_ARCH_POWERPC   16
#  define NEXUS_ARCH_POWERPC64 17
#  define NEXUS_ARCH_SPARC     18
#  define NEXUS_ARCH_M68K      19

/* --- RISC-V --- */
#  define NEXUS_ARCH_RISCV32 20
#  define NEXUS_ARCH_RISCV64 21

#  if defined(__x86_64__) || defined(_M_X64)
#    define NEXUS_ARCH NEXUS_ARCH_X86_64
#  elif defined(i386) || defined(__i386__) || defined(__i386) || defined(_M_IX86)
#    define NEXUS_ARCH NEXUS_ARCH_X86_32
#  elif defined(__ARM_ARCH_2__)
#    define NEXUS_ARCH NEXUS_ARCH_ARM2
#  elif defined(__ARM_ARCH_3__) || defined(__ARM_ARCH_3M__)
#    define NEXUS_ARCH NEXUS_ARCH_ARM3
#  elif defined(__ARM_ARCH_4T__) || defined(__TARGET_ARM_4T)
#    define NEXUS_ARCH NEXUS_ARCH_ARM4T
#  elif defined(__ARM_ARCH_5_) || defined(__ARM_ARCH_5E_)
#    define NEXUS_ARCH NEXUS_ARCH_ARM5
#  elif defined(__ARM_ARCH_6T2_) || defined(__ARM_ARCH_6T2_)
#    define NEXUS_ARCH NEXUS_ARCH_ARM6T2
#  elif defined(__ARM_ARCH_6__) || defined(__ARM_ARCH_6J__) || defined(__ARM_ARCH_6K__) || defined(__ARM_ARCH_6Z__) || defined(__ARM_ARCH_6ZK__)
#    define NEXUS_ARCH NEXUS_ARCH_ARM6
#  elif defined(__ARM_ARCH_7__) || defined(__ARM_ARCH_7A__) || defined(__ARM_ARCH_7R__) || defined(__ARM_ARCH_7M__) || defined(__ARM_ARCH_7S__)
#    define NEXUS_ARCH NEXUS_ARCH_ARM7
#  elif defined(__ARM_ARCH_7A__) || defined(__ARM_ARCH_7R__) || defined(__ARM_ARCH_7M__) || defined(__ARM_ARCH_7S__)
#    define NEXUS_ARCH NEXUS_ARCH_ARM7A
#  elif defined(__ARM_ARCH_7R__) || defined(__ARM_ARCH_7M__) || defined(__ARM_ARCH_7S__)
#    define NEXUS_ARCH NEXUS_ARCH_ARM7R
#  elif defined(__ARM_ARCH_7M__)
#    define NEXUS_ARCH NEXUS_ARCH_ARM7M
#  elif defined(__ARM_ARCH_7S__)
#    define NEXUS_ARCH NEXUS_ARCH_ARM7S
#  elif defined(__aarch64__) || defined(_M_ARM64)
#    define NEXUS_ARCH NEXUS_ARCH_ARM64
#  elif defined(mips) || defined(__mips__) || defined(__mips)
#    define NEXUS_ARCH NEXUS_ARCH_MIPS
#  elif defined(__sh__)
#    define NEXUS_ARCH NEXUS_ARCH_SUPERH
#  elif defined(__powerpc) || defined(__powerpc__) || defined(__powerpc64__) || defined(__POWERPC__) || defined(__ppc__) || defined(__PPC__) ||      \
      defined(_ARCH_PPC)
#    define NEXUS_ARCH NEXUS_ARCH_POWERPC
#  elif defined(__PPC64__) || defined(__ppc64__) || defined(_ARCH_PPC64)
#    define NEXUS_ARCH NEXUS_ARCH_POWERPC64
#  elif defined(__sparc__) || defined(__sparc)
#    define NEXUS_ARCH NEXUS_ARCH_SPARC
#  elif defined(__m68k__)
#    define NEXUS_ARCH NEXUS_ARCH_M68K
#  elif defined(__riscv) || defined(__riscv__) || defined(_riscv)
#    if defined(__riscv64) || (defined(__riscv_xlen) && __riscv_xlen == 64)
#      define NEXUS_ARCH NEXUS_ARCH_RISCV64
#    elif defined(__riscv32) || (defined(__riscv_xlen) && __riscv_xlen == 32)
#      define NEXUS_ARCH NEXUS_ARCH_RISCV32
#    elif defined(__LP64__) || defined(_LP64)
#      define NEXUS_ARCH NEXUS_ARCH_RISCV64
#    else
#      define NEXUS_ARCH NEXUS_ARCH_RISCV32
#    endif
#  else
#    define NEXUS_ARCH NEXUS_ARCH_UNKNOWN
#  endif

#endif /* ARCHITECTURE DETECTION*/

#ifndef NULL
#  ifdef __cplusplus
#    define NULL 0 /* Defines NULL in C++*/
#  else
#    define NULL ((void *)0) /* Defines NULL in C*/
#  endif
#endif

#ifndef TRUE
#  define TRUE 1
#endif

#ifndef FALSE
#  define FALSE 0
#endif

#ifndef _WIN32
#  include <sys/types.h>
#endif

/*
Summary: the fixed-width primitive types -- 8/16/32-bit ints, 32/64-bit floats, byte, boolean.
Description: boolean holds TRUE (1) or FALSE (0); byte is an alias for uint8.
*/
typedef unsigned char  uint8;
typedef unsigned short uint16;
typedef unsigned int   uint32;

typedef signed char  int8;
typedef signed short int16;
typedef signed int   int32;

typedef float  real32;
typedef double real64;

typedef uint8 byte;

typedef unsigned char boolean;

#include <limits.h>

/* ============================================================================ */
/* ---------------------------------------------------------------------------- */
/* PLATFORM SUPPORT                                                             */
/* ---------------------------------------------------------------------------- */
/* ============================================================================ */

/*
Summary: terminate when an unavailable platform-dependent operation is reached.
Description:
  Keeping the failure at the call site lets the rest of Nexus compile while
  a new port is developed. The operation string should identify the missing
  implementation; the macro supplies the source location.
*/
extern void _nexus_platform_unsupported_raise(const char *operation, const char *file, uint32 line);

#define NEXUS_PLATFORM_UNSUPPORTED_RAISE(operation) _nexus_platform_unsupported_raise(operation, __FILE__, (uint32)__LINE__)

/*
Summary: one object per OS thread, without a managed NexusThread.
Description: storage isolation only -- it does not make shared objects atomic and runs no per-thread destructors.
*/
#ifdef _MSC_VER
#  define NEXUS_THREAD_LOCAL __declspec(thread)
#elif defined(__GNUC__) || defined(__clang__)
#  define NEXUS_THREAD_LOCAL __thread
#else
#  define NEXUS_THREAD_LOCAL
#endif

/*
Summary: mark a header-defined definition a translation unit need not call.
Description:
  Chiefly the static header-inlined helpers the BITS and BYTES sections are
  full of (Inlining convention). Without it, a translation unit that calls
  only some of them warns under -Wall's -Wunused-function for each it does
  not call.
*/
#if defined(__GNUC__) || defined(__clang__)
#  define NEXUS_MAYBE_UNUSED __attribute__((unused))
#else
#  define NEXUS_MAYBE_UNUSED
#endif

/*
Summary: mark a function's return value as one the caller must not silently discard.
Description:
  Predates (and stands in for) C23's [[nodiscard]], which isn't available under the C89 target.
*/
#if defined(__GNUC__) || defined(__clang__)
#  define NEXUS_NODISCARD __attribute__((warn_unused_result))
#else
#  define NEXUS_NODISCARD
#endif

/*
Summary: fixed-width 64-bit integers (compiler extensions; Dependable C).
Description:
  Always exactly 64 bits wide, independent of CPU word size. Used for
  nanosecond durations, epoch arithmetic, and the uint64 / int64 primitives.
*/
#ifdef _MSC_VER
typedef signed __int64   int64;
typedef unsigned __int64 uint64;
#elif defined(__GNUC__) || defined(__clang__)
__extension__ typedef signed long long   int64;
__extension__ typedef unsigned long long uint64;
#elif defined(__LP64__) || defined(_LP64)
typedef signed long   int64;
typedef unsigned long uint64;
#else
__extension__ typedef signed long long   int64;
__extension__ typedef unsigned long long uint64;
#endif

/*
Summary: architecture word width (pointer size) -- distinguishes 32-bit from 64-bit address spaces.
Description:
  Used when behavior must follow allocatable object size rather than fixed
  64-bit width. NEXUS_ARCHITECTURE_BITS_NATIVE is the detected host width;
  NEXUS_ARCHITECTURE_BITS is the effective width (may be forced to 32);
  NEXUS_ARCHITECTURE_FORCED_32_BIT is TRUE when NEXUS_FORCE_32_BIT is active.
  Define NEXUS_FORCE_32_BIT before including this header to simulate a 32-bit
  address space on a 64-bit host (uint_large / int_large become 32-bit;
  timestamp / int64 / uint64 are unchanged).
*/
#if defined(_WIN64) || defined(__LP64__) || defined(_LP64) || defined(__x86_64__) || defined(__aarch64__) || defined(__amd64__) ||                   \
    defined(__riscv64) || (defined(__riscv_xlen) && __riscv_xlen == 64)
#  define NEXUS_ARCHITECTURE_BITS_NATIVE 64
#elif defined(_WIN32) || defined(__i386__) || defined(_M_IX86) || defined(__arm__) || defined(__ARMEL__) || defined(__ARMEB__) ||                    \
    defined(__riscv32) || (defined(__riscv_xlen) && __riscv_xlen == 32)
#  define NEXUS_ARCHITECTURE_BITS_NATIVE 32
#else
#  if defined(ULONG_MAX) && (ULONG_MAX > 4294967295UL)
#    define NEXUS_ARCHITECTURE_BITS_NATIVE 64
#  else
#    define NEXUS_ARCHITECTURE_BITS_NATIVE 32
#  endif
#endif

#if NEXUS_FORCE_32_BIT
#  define NEXUS_ARCHITECTURE_BITS          32
#  define NEXUS_ARCHITECTURE_FORCED_32_BIT TRUE
#else
#  define NEXUS_ARCHITECTURE_BITS          NEXUS_ARCHITECTURE_BITS_NATIVE
#  define NEXUS_ARCHITECTURE_FORCED_32_BIT FALSE
#endif

#define NEXUS_ARCHITECTURE_IS_32_BIT (NEXUS_ARCHITECTURE_BITS == 32)
#define NEXUS_ARCHITECTURE_IS_64_BIT (NEXUS_ARCHITECTURE_BITS == 64)

/*
Summary: native word-sized integers -- counts, lengths, indices, buffer sizes.
Description:
  Must fit any single allocatable object on this architecture. On 32-bit
  builds this is 32 bits even when the compiler also provides fixed-width
  64-bit types.
*/
#if NEXUS_ARCHITECTURE_BITS == 64
typedef int64  int_large;
typedef uint64 uint_large;
#else
typedef int32  int_large;
typedef uint32 uint_large;
#endif

#define NEXUS_SIZEOF_POINTER               ((uint_large)sizeof(void *)) /* Actual pointer size independent of simulated configuration. */
#define NEXUS_EFFECTIVE_POINTER_SIZE_BYTES ((uint_large)(NEXUS_ARCHITECTURE_BITS / 8)) /* Simulated pointer size. */

/*
Summary: absolute time as unsigned nanoseconds since an epoch.
Description:
  uint64 on all targets so epoch math is full-range on 32-bit and 64-bit
  CPUs -- uint_large is not used because address width does not limit time
  span.
*/
typedef uint64 timestamp;

/*
Summary: a 128-bit unsigned integer stored as two 64-bit words.
Description: lo is the least significant 64 bits, hi the most significant; arithmetic wraps modulo 2^128.
*/
typedef struct uint128 { /* NOLINT */
  uint64 lo;
  uint64 hi;
} uint128; /* NOLINT */

/*
Summary: construct a 128-bit value from low and high words; a zero value.
*/
extern uint128 nexus_uint128_make(uint64 lo, uint64 hi);
extern uint128 nexus_uint128_zero(void);

/*
Summary: 128-bit arithmetic, all modulo 2^128.
Description: add is left + right, mul is left * right, xor is the bitwise exclusive OR.
*/
extern uint128 nexus_uint128_add(uint128 left, uint128 right);
extern uint128 nexus_uint128_xor(uint128 left, uint128 right);
extern uint128 nexus_uint128_mul(uint128 left, uint128 right);

/*
Summary: logical right shift by shift bits; shifts of 128 or more return zero.
*/
extern uint128 nexus_uint128_shift_right(uint128 value, uint32 shift);

/*
Summary: fold a 128-bit value to 64 bits via hi ^ lo.
*/
extern uint64 nexus_uint128_fold_to_uint64(uint128 value);

/*
Summary: serialize / deserialize a uint128 to and from 16 bytes in the named order.
*/
extern void    nexus_uint128_bytes_little_endian_write(uint128 value, byte out_bytes[16]);
extern uint128 nexus_uint128_bytes_little_endian_read(const byte in_bytes[16]);
extern void    nexus_uint128_bytes_big_endian_write(uint128 value, byte out_bytes[16]);
extern uint128 nexus_uint128_bytes_big_endian_read(const byte in_bytes[16]);

static NEXUS_MAYBE_UNUSED boolean nexus_uint128_compare(uint128 value_a, uint128 value_b) {
  return value_a.hi == value_b.hi && value_a.lo == value_b.lo;
}

/* PRECISIONS */

#ifndef NEXUS_FLOAT_DOUBLE_PRECISION
#  define NEXUS_FLOAT_DOUBLE_PRECISION                                                                                                               \
    TRUE /* if NEXUS_FLOAT_DOUBLE_PRECISION is turned on, the type "f_real" is defined as double otherwise it will be defined as                     \
    float. This is very useful if you want to write an application that can be compiled to use either 32 or 64 bit                                   \
    floating point math.                                                                                                                             \
    */
#endif

#if (NEXUS_FLOAT_DOUBLE_PRECISION)
typedef double f_real;
#else
typedef float f_real;
#endif

#include <float.h>

#define INT8_MAX_VAL  SCHAR_MAX
#define INT8_MIN_VAL  SCHAR_MIN
#define UINT8_MAX_VAL UCHAR_MAX

#define INT16_MAX_VAL  SHRT_MAX
#define INT16_MIN_VAL  SHRT_MIN
#define UINT16_MAX_VAL USHRT_MAX

#define INT32_MAX_VAL  INT_MAX
#define INT32_MIN_VAL  INT_MIN
#define UINT32_MAX_VAL UINT_MAX

/* 64-bit bounds (available on all supported targets). */
#ifdef _MSC_VER
#  define INT64_MAX_VAL  _I64_MAX
#  define INT64_MIN_VAL  _I64_MIN
#  define UINT64_MAX_VAL _UI64_MAX
#else
#  define INT64_MAX_VAL  ((int64)9223372036854775807LL)
#  define INT64_MIN_VAL  ((int64)(-9223372036854775807LL - 1))
#  define UINT64_MAX_VAL ((uint64)18446744073709551615ULL)
#endif

#define TIMESTAMP_MAX_VAL UINT64_MAX_VAL

#if NEXUS_ARCHITECTURE_BITS == 64
#  define UINT_LARGE_MAX_VAL UINT64_MAX_VAL
#  define INT_LARGE_MAX_VAL  INT64_MAX_VAL
#  define INT_LARGE_MIN_VAL  INT64_MIN_VAL
#else
#  define UINT_LARGE_MAX_VAL UINT32_MAX_VAL
#  define INT_LARGE_MAX_VAL  INT32_MAX_VAL
#  define INT_LARGE_MIN_VAL  INT32_MIN_VAL
#endif

#define REAL32_MAX_VAL FLT_MAX
#define REAL32_MIN_VAL FLT_MIN
#define REAL32_EPSILON FLT_EPSILON

#define REAL64_MAX_VAL DBL_MAX
#define REAL64_MIN_VAL DBL_MIN
#define REAL64_EPSILON DBL_EPSILON

#if NEXUS_FLOAT_DOUBLE_PRECISION
#  define F_REAL_MAX     REAL64_MAX_VAL
#  define F_REAL_MIN     REAL64_MIN_VAL
#  define F_REAL_EPSILON REAL64_EPSILON
#else
#  define F_REAL_MAX     REAL32_MAX_VAL
#  define F_REAL_MIN     REAL32_MIN_VAL
#  define F_REAL_EPSILON REAL32_EPSILON
#endif

/* ---------------------------------------------------------------------------- */
/* CORE MEMORY/TYPE HELPERS                                                     */
/* ---------------------------------------------------------------------------- */

#define NEXUS_MEMORY_BYTE_SIZE     1ULL
#define NEXUS_MEMORY_KILOBYTE_SIZE (NEXUS_MEMORY_BYTE_SIZE * 1024)
#define NEXUS_MEMORY_MEGABYTE_SIZE (NEXUS_MEMORY_KILOBYTE_SIZE * 1024)
#define NEXUS_MEMORY_GIGABYTE_SIZE (NEXUS_MEMORY_MEGABYTE_SIZE * 1024)

#define NEXUS_SIZEOF(type)                             ((size_t)sizeof(type))
#define NEXUS_OFFSETOF(type, field)                    ((size_t)offsetof(type, field))
#define NEXUS_ARRAY_SIZE_BYTES(array)                  ((size_t)sizeof(array))
#define NEXUS_ARRAY_SIZE_ELEMENTS(array)               ((uint64)sizeof(array) / (uint64)sizeof((array)[0]))
#define NEXUS_MEMORY_OFFSET(base_pointer, byte_offset) ((void *)((byte *)(base_pointer) + (size_t)(byte_offset)))

#define NEXUS_ALIGNOF(type)                                                                                                                          \
  ((size_t)(sizeof(struct {                                                                                                                          \
              char c;                                                                                                                                \
              type t;                                                                                                                                \
            }) -                                                                                                                                     \
            sizeof(type)))

/* ============================================================================ */
/* ---------------------------------------------------------------------------- */
/* FAILURE PATH SYSTEM                                                          */
/* ---------------------------------------------------------------------------- */
/* ============================================================================ */

/* ---------------------------------------------------------------------------- */
/* CONTEXT / STACK FRAME                                                        */
/* ---------------------------------------------------------------------------- */

#include <stdlib.h>

#ifndef NEXUS_PROGRAM_CONTEXT_MAX_STACK_SIZE
#  define NEXUS_PROGRAM_CONTEXT_MAX_STACK_SIZE 64
#endif

extern boolean _nexus_program_context_push(const char *label, const char *file, uint32 line);
extern boolean _nexus_program_context_pop(const char *file, uint32 line);

#define NEXUS_PROGRAM_CONTEXT_PUSH(label)                                                                                                            \
  do {                                                                                                                                               \
    if (_nexus_program_context_push((label), __FILE__, __LINE__)) {                                                                                  \
      abort();                                                                                                                                       \
    }                                                                                                                                                \
  } while (0)
#define NEXUS_PROGRAM_CONTEXT_POP()                                                                                                                  \
  do {                                                                                                                                               \
    if (_nexus_program_context_pop(__FILE__, __LINE__)) {                                                                                            \
      abort();                                                                                                                                       \
    }                                                                                                                                                \
  } while (0)

/*
Summary: one NEXUS_PROGRAM_CONTEXT_PUSH label captured for the diagnostic call stack.
Fields:
  label  the pushed label, or NULL/empty if the caller passed none
  file   the pushing site's __FILE__
  line   the pushing site's __LINE__
*/
typedef struct NexusProgramContextStackFrame {
  const char *label;
  const char *file;
  uint32      line;
} NexusProgramContextStackFrame;

/*
Summary: a copyable snapshot of the calling OS thread's whole diagnostic call
  stack, for a caller that itself multiplexes several logical call chains
  onto one OS thread (see _nexus_program_context_snapshot_save/_restore).
Fields:
  frames       pushed frames, innermost at frames[frame_count - 1]
  frame_count  how many of frames is live
*/
typedef struct NexusProgramContextSnapshot {
  NexusProgramContextStackFrame frames[NEXUS_PROGRAM_CONTEXT_MAX_STACK_SIZE];
  uint32                        frame_count;
} NexusProgramContextSnapshot;

/*
Summary: copies the calling OS thread's entire current diagnostic call stack into *out_snapshot.
Description: for a cooperative scheduler that runs several logical call
  chains (e.g. coroutines) on one OS thread: NEXUS_PROGRAM_CONTEXT_PUSH/POP
  are scoped per OS thread, so without this, one chain's suspended-but-still-
  open frames would be silently overwritten by another chain's pushes on the
  same thread, corrupting both traces. Save the currently-active chain's
  stack before switching away from it, and restore it when switching back.
Params: out_snapshot  out; overwritten with the current frames and frame_count
Thread-safety: reads only the calling thread's own diagnostic stack.
*/
extern void _nexus_program_context_snapshot_save(NexusProgramContextSnapshot *out_snapshot);

/*
Summary: replaces the calling OS thread's entire current diagnostic call stack with *snapshot.
Params: snapshot  borrowed; see _nexus_program_context_snapshot_save
Thread-safety: writes only the calling thread's own diagnostic stack.
*/
extern void _nexus_program_context_snapshot_restore(const NexusProgramContextSnapshot *snapshot);

/* ---------------------------------------------------------------------------- */
/* ERRORS                                                                       */
/* ---------------------------------------------------------------------------- */

/*
Summary: number of NexusError slots per pool slab, overridable before including this header.
Description: the pool grows by whole slabs (see nexus_program_errors_pool_slabs_add) --
  this constant is the slab's fixed capacity, not the pool's total capacity.
*/
#ifndef NEXUS_PROGRAM_ERROR_POOL_SLAB_SIZE
#  define NEXUS_PROGRAM_ERROR_POOL_SLAB_SIZE 64
#endif

/*
Summary: max bytes (including the terminator) a formatted NexusError message may occupy.
Description: enforced by _nexus_program_errors_failure_create as a programmer-error
  precondition -- a format+args combination that would need more than this is a
  caller bug, not a runtime condition, and is never silently truncated in
  NEXUS_DEVELOPER_MODE.
*/
#ifndef NEXUS_PROGRAM_ERROR_MESSAGE_MAX_SIZE
#  define NEXUS_PROGRAM_ERROR_MESSAGE_MAX_SIZE 300
#endif

/*
Summary: size of the stack buffer NEXUS_PROGRAM_PANIC_ERROR renders an error trace into.
*/
#ifndef NEXUS_PROGRAM_PANIC_ERROR_MESSAGE_MAX_SIZE
#  define NEXUS_PROGRAM_PANIC_ERROR_MESSAGE_MAX_SIZE 10000
#endif

/*
Summary: NexusErrorCode's bit layout -- version, library, facility, category, local code.
Description:
  VVVV LLLLLLLLLLLLLLLLLLLL FFFFFFFFFFFFFFFFFFFF CCCCCCCC NNNNNNNNNNNN
   4           20                   20               8           12
  Each field is validated at compile time where it is assembled (see
  NEXUS_INTERNAL_ERROR_IDENTIFIER_VALIDATE_PACKED) rather than at read time.
See: nexus/adr/0001-nexus-error-representation.md
*/

/*
Summary: a library or facility's packed 4-character tag, before it is placed into a NexusErrorCode.
*/
typedef uint32 NexusErrorIdentifier;

/*
Summary: a fully assembled Nexus error code (version + library + facility + category + local code).
Description: opaque scalar -- decode with _nexus_program_error_code_unpack, never by
  hand-shifting, since the bit layout is an implementation detail this typedef exists
  to hide.
*/
typedef uint64 NexusErrorCode;

/*
Summary: the return-value type of the whole Nexus failure-path system.
Description: a stable pointer into pool-resident storage (_nexus_program_errors_failure_create
  and friends), never a value the caller copies around -- see nexus/adr/0001 for why. Comparing
  two NexusError values for "the same failure" means comparing ->code, not comparing pointers.
Fields:
  message          non-owning; NULL means "no message", not an error (see _nexus_program_errors_failure_create)
  contained_error  the next-older cause in the chain, or NULL at the root; never owned/freed
                     by the caller, walked read-only via nexus_program_errors_error_print
See: nexus/adr/0001-nexus-error-representation.md
*/
typedef struct NexusError {
  const char *file;
  uint32      line;

  NexusErrorCode code;
  const char    *message;

  struct NexusError *contained_error;
} NexusError;

/*
Summary: the schema version embedded in every NexusErrorCode's top 4 bits.
Description: lets a future incompatible repacking of the layout be told apart
  from today's codes at read time; not currently interpreted anywhere.
*/
#define NEXUS_INTERNAL_ERROR_SCHEMA_VERSION 1

/*
Summary: the all-zero NexusErrorCode -- version 0, no library/facility/local code.
Description: distinct from nexus_program_errors_success, which is a real, fully-formed
  success value; this is only ever a sentinel for "no code at all".
*/
#define NEXUS_PROGRAM_ERROR_NONE ((NexusErrorCode)0)

/*
Summary: compile-time assert that four characters are each 'A'-'Z'; not for direct use.
Description: folds to 0 when the check passes and fails the build (illegal bitfield
  width) otherwise -- see the walkthrough in nexus/adr/0001's history for why this
  idiom, not a runtime check, is used here. Used by NEXUS_INTERNAL_ERROR_IDENTIFIER_CREATE.
*/
#define NEXUS_INTERNAL_ERROR_IDENTIFIER_VALIDATE_UNPACKED(c1, c2, c3, c4)                                                                            \
  (0 * sizeof(struct {                                                                                                                               \
     int tag : (((c1) >= 'A' && (c1) <= 'Z') && ((c2) >= 'A' && (c2) <= 'Z') && ((c3) >= 'A' && (c3) <= 'Z') && ((c4) >= 'A' && (c4) <= 'Z')) ? 1    \
                                                                                                                                              : -1;  \
   }))

/*
Summary: compile-time assert that identifier fits in 0..max; not for direct use.
Description: same fold-to-0-or-fail-the-build idiom as
  NEXUS_INTERNAL_ERROR_IDENTIFIER_VALIDATE_UNPACKED, generalized to an arbitrary bound
  so one macro serves every packed field width (library/facility/local code all
  validate against 2^20-1 via this, category and local code inside NEXUS_ERROR_LOCAL
  against their own narrower bounds).
*/
#define NEXUS_INTERNAL_ERROR_IDENTIFIER_VALIDATE_PACKED(identifier, max)                                                                             \
  (0 * sizeof(struct { int tag : ((identifier) >= 0 && (identifier) <= (max)) ? 1 : -1; }))

/*
Summary: packs four A-Z characters into a 20-bit NexusErrorIdentifier; shared by the library/facility macros below.
Description: each character is reduced to a 0-25 code (c - 'A') and placed at a fixed
  5-bit slot (c1 highest, c4 lowest) so the four letters read back in the same order
  they were written in. Out-of-range characters fail to compile rather than silently
  corrupting a neighboring slot. See NEXUS_PROGRAM_ERROR_LIBRARY_IDENTIFIER_CREATE /
  NEXUS_PROGRAM_ERROR_FACILITY_IDENTIFIER_CREATE for the public entry points.
*/
#define NEXUS_INTERNAL_ERROR_IDENTIFIER_CREATE(c1, c2, c3, c4)                                                                                       \
  (NexusErrorIdentifier)((NEXUS_INTERNAL_ERROR_IDENTIFIER_VALIDATE_UNPACKED((c1), (c2), (c3), (c4)) + (((c1) - 'A') << 15)) | (((c2) - 'A') << 10) | \
                         (((c3) - 'A') << 5) | (((c4) - 'A')))

/*
Summary: mints a library's 4-character identifier, e.g. NEXUS_PROGRAM_ERROR_LIBRARY_IDENTIFIER_CREATE('N','E','X','S').
Description: one call per library, usually bound to a NEXUS_ERROR_LIBRARY_IDENTIFIER-style
  constant near the top of that library's header. Each argument must be a compile-time
  'A'-'Z' character literal -- see NEXUS_INTERNAL_ERROR_IDENTIFIER_CREATE for the packing.
*/
#define NEXUS_PROGRAM_ERROR_LIBRARY_IDENTIFIER_CREATE(c1, c2, c3, c4) NEXUS_INTERNAL_ERROR_IDENTIFIER_CREATE((c1), (c2), (c3), (c4))
/*
Summary: mints a facility's 4-character identifier; same packing and constraints as
  NEXUS_PROGRAM_ERROR_LIBRARY_IDENTIFIER_CREATE, one call per facility a library defines.
*/
#define NEXUS_PROGRAM_ERROR_FACILITY_IDENTIFIER_CREATE(c1, c2, c3, c4) NEXUS_INTERNAL_ERROR_IDENTIFIER_CREATE((c1), (c2), (c3), (c4))

/*
Summary: packs a category (0-255) and a local code (0-4095) into one 20-bit local identity.
Description: used as an enumerator's value inside a NEXUS_ERROR_FACILITY_DEFINE list so
  category and code are stated exactly once, at the point an error is named, rather than
  again wherever its global NexusErrorCode gets assembled. Out-of-range category or code
  fails to compile rather than silently corrupting the other field.
Params:
  category  0-255; a coarser grouping within one facility (see e.g. NexusProgramAssertionErrorCategory)
  code      0-4095; the specific error within that category
*/
#define NEXUS_ERROR_LOCAL(category, code)                                                                                                            \
  ((NEXUS_INTERNAL_ERROR_IDENTIFIER_VALIDATE_PACKED((category), 255) + /* (2 ^ 8) - 1 */                                                             \
    NEXUS_INTERNAL_ERROR_IDENTIFIER_VALIDATE_PACKED((code), 4095)) |   /* (2 ^ 12) - 1 */                                                            \
   (((category) << 12) | (code)))

/*
Summary: assembles a full 64-bit NexusErrorCode from a library, a facility, and a local identity.
Description: local is expected to already be a NEXUS_ERROR_LOCAL(category, code) value (or an
  enumerator whose value is one) -- category and code are never passed separately here, so
  they are never restated a second time beyond where NEXUS_ERROR_LOCAL first combined them.
  See the layout comment above NexusErrorCode's typedef for where each argument lands.
Params:
  library   a NEXUS_PROGRAM_ERROR_LIBRARY_IDENTIFIER_CREATE value
  facility  a NEXUS_PROGRAM_ERROR_FACILITY_IDENTIFIER_CREATE value
  local     a NEXUS_ERROR_LOCAL(category, code) value
*/
#define NEXUS_PROGRAM_ERROR_CODE_MAKE(library, facility, local)                                                                                      \
  (NexusErrorCode)((NEXUS_INTERNAL_ERROR_IDENTIFIER_VALIDATE_PACKED((library), 1048575) +  /* (2 ^ 20) - 1 */                                        \
                    NEXUS_INTERNAL_ERROR_IDENTIFIER_VALIDATE_PACKED((facility), 1048575) + /* (2 ^ 20) - 1 */                                        \
                    NEXUS_INTERNAL_ERROR_IDENTIFIER_VALIDATE_PACKED((local), 1048575) +    /* (2 ^ 20) - 1 */                                        \
                    ((NexusErrorCode)(NEXUS_INTERNAL_ERROR_SCHEMA_VERSION) << 60)) |                                                                 \
                   ((NexusErrorCode)(library) << 40) | ((NexusErrorCode)(facility) << 20) | (local))

/*
Summary: a NexusErrorCode's fields, decoded back to human-readable form by _nexus_program_error_code_unpack.
Fields:
  library, facility  exactly 4 characters, NOT nul-terminated -- print with "%.4s", never "%s"
*/
typedef struct NexusInternalUnpackedErrorCode {
  char   library[4];
  char   facility[4];
  uint8  category;
  uint16 local_code;
} NexusInternalUnpackedErrorCode;

/*
Summary: decodes a NexusErrorCode into its library/facility/category/local code fields.
Description: the inverse of NEXUS_PROGRAM_ERROR_CODE_MAKE + NEXUS_INTERNAL_ERROR_IDENTIFIER_CREATE --
  every shift here is "bits below the field, from bit 0" (identical to the shift used to pack
  it, just applied right instead of left), never "bits above"; see nexus/adr/0001's history for
  the worked-through reasoning if this needs re-deriving later.
Params:
  unpacked  out-parameter; fully overwritten, no fields read on entry
*/
static NEXUS_MAYBE_UNUSED void _nexus_program_error_code_unpack(NexusErrorCode code, NexusInternalUnpackedErrorCode *unpacked) {
  uint8 i;

  uint64 library;
  uint64 facility;
  uint64 category;
  uint64 local_code;

  uint64 scanned_character;

  library = (code >> 40);
  library &= (1u << 20) - 1u;

  facility = (code >> 20);
  facility &= (1u << 20) - 1u;

  /*
  Shift: how many bits from 0
  Mask: width of the reading
  */

  for (i = 0; i < 4; i++) {
    scanned_character = (library >> (15 - (i * 5)));
    scanned_character &= ((1u << 5) - 1u);
    scanned_character    = scanned_character + 'A';
    unpacked->library[i] = (char)scanned_character;

    scanned_character = (facility >> (15 - (i * 5)));
    scanned_character &= ((1u << 5) - 1u);
    scanned_character     = scanned_character + 'A';
    unpacked->facility[i] = (char)scanned_character;
  }

  category = (code >> 12);
  category &= (1u << 8) - 1u;
  unpacked->category = category;

  local_code = code;
  local_code &= (1u << 12) - 1u;
  unpacked->local_code = local_code;
}

/*
Summary: the canonical "no failure" NexusError -- a real, fully-formed success value, not NULL.
Description: check for success/failure by comparing ->code (or via
  nexus_program_errors_is_error), never by comparing pointers against this macro's expansion.
*/
#define NEXUS_PROGRAM_ERROR_SUCCESS() (&nexus_program_errors_success)
/*
Summary: constructs a NexusError, supplying __FILE__/__LINE__ automatically -- the only public entry point for _nexus_program_errors_failure_create.
Description: format and every trailing argument forward exactly as given -- format may be
  NULL (no message) or a printf-style string with matching variadic arguments. Uses a C99
  variadic macro plus the GNU ", ##__VA_ARGS__" comma-elision extension so a call with zero
  extra arguments (NEXUS_PROGRAM_ERROR_FAILURE(code, "plain message")) still compiles; both
  are deliberate, disclosed exceptions to the C89 target (see infra/standards' compiler flags
  and .clang-tidy for where their warnings are silenced) -- accepted because both are supported
  by every mainstream compiler in practice, unlike less common GNU/C99 extensions. The same
  two exceptions back every variadic macro in the PANICS and ASSERTIONS sections below; not
  re-disclosed there. Those sections also forward a received va_list through a second function
  call without va_copy in one place (_nexus_program_assert_or_error's pool-capacity retry) --
  not a third exception, since C99 7.15p3 only forbids reusing a va_list once some function has
  actually walked it via va_arg, and that call site guarantees it hasn't been (see its own
  Description); va_copy itself isn't used anywhere, since it doesn't declare cleanly under this
  project's "-std=c89 -pedantic".
See: _nexus_program_errors_failure_create for the full contract (Params/Returns/Ownership/Errors/Thread-safety)
*/
#define NEXUS_PROGRAM_ERROR_FAILURE(code, format, ...) _nexus_program_errors_failure_create((code), (format), __FILE__, __LINE__, ##__VA_ARGS__)

/*
Summary: Nexus's own library identifier ('NEXS'), used by every facility this header defines.
*/
#define NEXUS_ERROR_LIBRARY_IDENTIFIER NEXUS_PROGRAM_ERROR_LIBRARY_IDENTIFIER_CREATE('N', 'E', 'X', 'S')

/*
Summary: internal -- the X-macro callback NEXUS_ERROR_FACILITY_DEFINE passes to expand a facility's error list into its local enum. Not invoked directly.
*/
#define NEXUS_ERROR_X_AS_LOCAL(name, category, code, library, facility) name = NEXUS_ERROR_LOCAL((category), (code)),

/*
Summary: internal -- the X-macro callback NEXUS_ERROR_FACILITY_DEFINE passes to expand a facility's error list into its GLOBAL_* constants. Not invoked directly.
*/
#define NEXUS_ERROR_X_AS_GLOBAL(name, category, code, library, facility)                                                                             \
  static const NexusErrorCode GLOBAL_##name = NEXUS_PROGRAM_ERROR_CODE_MAKE((library), (facility), name);

/*
Summary: defines one facility's whole error surface -- a local enum plus matching global codes -- from a single list.
Description: list_macro must have the shape "#define MY_LIST(X, library, facility) X(name,
  category, code, library, facility) ...", one X(...) row per error; this macro expands
  that same list twice, once into EnumName's enumerators (via NEXUS_ERROR_X_AS_LOCAL) and
  once into a "static const NexusErrorCode GLOBAL_name = ..." per error (via
  NEXUS_ERROR_X_AS_GLOBAL) -- category and code are stated once, in the list, never restated.
  A client library follows the same pattern with its own list_macro, library, and facility.
Params:
  EnumName    the generated local-identity enum's tag and typedef name
  list_macro  see Description -- see NEXUS_ERROR_ERROR_LIST below for a worked example
See: NEXUS_ERROR_ERROR_LIST (below), NexusProgramAssertionError's NEXUS_ASSERTION_ERROR_LIST
Example:
  #define MY_ERROR_LIST(X, library, facility) \
    X(MY_ERROR_TIMEOUT, MY_CATEGORY_IO, 1, library, facility)
  NEXUS_ERROR_FACILITY_DEFINE(MyError, MY_LIBRARY_IDENTIFIER, MY_FACILITY_IDENTIFIER, MY_ERROR_LIST)
  -- generates enum MyError { MY_ERROR_TIMEOUT = ... }; and
     static const NexusErrorCode GLOBAL_MY_ERROR_TIMEOUT = ...;
*/
#define NEXUS_ERROR_FACILITY_DEFINE(EnumName, library, facility, list_macro)                                                                         \
  typedef enum EnumName { /* NOLINT(bugprone-macro-parentheses) -- EnumName is a type tag/declarator here; parens would be a syntax error */         \
    list_macro(NEXUS_ERROR_X_AS_LOCAL, library, facility) NEXUS_ERROR_ENUM_END_##EnumName                                                            \
  } EnumName; /* NOLINT(bugprone-macro-parentheses) */                                                                                               \
                                                                                                                                                     \
  list_macro(NEXUS_ERROR_X_AS_GLOBAL, library, facility)

/*
Summary: current total capacity (in NexusError slots, across every slab) of the error pool.
*/
extern uint64 nexus_program_errors_pool_slabs_get(void);
/*
Summary: grows the error pool by `slabs` more slabs, appended after whatever's already linked.
Description: existing slabs are never moved, resized, or invalidated by this call -- pointers
  returned by _nexus_program_errors_failure_create before a grow stay valid after it. Growth
  is entirely caller-triggered; nothing in this header grows the pool automatically beyond
  its compile-time initial slab.
Errors: NEXUS_PROGRAM_PANIC (not a NexusError) if the underlying allocation fails -- out of
  memory while growing the pool is treated as unrecoverable, deliberately never routed
  through the error system whose own storage it would be growing (see nexus/adr/0001).
Thread-safety: not thread-safe -- the pool is plain static storage with no locking.
*/
extern void nexus_program_errors_pool_slabs_add(uint64 slabs);
/*
Summary: marks every slab's slots as free again and rewinds the fill cursor to the first slab.
Description: does not shrink the pool (slabs stay allocated) and does not zero slot contents --
  only safe to call once nothing still holds a NexusError pointer from before the reset, since
  the next _nexus_program_errors_failure_create call may hand out and overwrite that same slot.
Thread-safety: not thread-safe -- the pool is plain static storage with no locking.
*/
extern void nexus_program_errors_pool_reset(void);

/*
Summary: the facility identifier for errors the error subsystem itself raises (see NexusProgramErrorError below).
*/
#define NEXUS_ERROR_ERROR_FACILITY NEXUS_PROGRAM_ERROR_FACILITY_IDENTIFIER_CREATE('E', 'R', 'R', 'O')

/*
Summary: categories within NexusProgramErrorError -- NPEEC_GENERIC for the forward marker, NPEEC_ERROR_POOL for pool-exhaustion errors.
*/
typedef enum NexusProgramErrorErrorCategory {
  NPEEC_GENERIC,
  NPEEC_ERROR_POOL,

  NPEEC_COUNT
} NexusProgramErrorErrorCategory;

/*
Summary: the error subsystem's own error list, defining NexusProgramErrorError via NEXUS_ERROR_FACILITY_DEFINE below.
*/
#define NEXUS_ERROR_ERROR_LIST(X, library, facility)                                                                                                 \
  X(NEXUS_ERROR_SUCCESS, NPEEC_GENERIC, 1, library, facility)                                                                                        \
  X(NEXUS_ERROR_FORWARD, NPEEC_GENERIC, 2, library, facility)                                                                                        \
  X(NEXUS_ERROR_POOL_CAPACITY_REACHED, NPEEC_ERROR_POOL, 1, library, facility)

NEXUS_ERROR_FACILITY_DEFINE(NexusProgramErrorError, NEXUS_ERROR_LIBRARY_IDENTIFIER, NEXUS_ERROR_ERROR_FACILITY, NEXUS_ERROR_ERROR_LIST)

/*
Summary: the statically-allocated "no failure" sentinel -- see NEXUS_PROGRAM_ERROR_SUCCESS().
Description: not part of the dynamic pool; always valid, never invalidated by
  nexus_program_errors_pool_reset.
*/
extern NexusError nexus_program_errors_success;
/*
Summary: the statically-allocated sentinel _nexus_program_errors_failure_create returns when the pool is exhausted with nowhere left to grow.
Description: not part of the dynamic pool, for the same reason nexus_program_errors_success
  isn't -- an error reporting "the pool is full" cannot itself depend on there being a free
  pool slot.
*/
extern NexusError nexus_program_errors_pool_capacity_reached;

/*
Summary: constructs a NexusError from a code and an optional printf-style format+args, allocating from the shared pool.
Description: the message is rendered immediately (eagerly), not deferred to report time --
  a deliberate simplicity-over-raw-speed call while the pool is new; see nexus/adr/0001's
  history for the measured cost of the deferred alternative if this ever needs revisiting.
  format is genuinely optional: NULL means "no message" and skips formatting entirely,
  rather than being treated as a caller error.
Params:
  format  nullable -- NULL means no message; otherwise a printf-style format string, variadic
            args following it exactly as with stbsp_vsnprintf
Returns: (see Errors) a pointer into pool-resident storage, or nexus_program_errors_pool_capacity_reached
Ownership: the returned pointer refers to pool storage the caller never frees; it stays valid
  until nexus_program_errors_pool_reset is called, not tied to this call's own stack frame.
Errors:
  NEXUS_PROGRAM_PRECONDITION: format's rendered length must be under
    NEXUS_PROGRAM_ERROR_MESSAGE_MAX_SIZE -- a message that would need truncating is a
    programmer error (wrong format string / max size too small), not a runtime condition.
  Returns nexus_program_errors_pool_capacity_reached (not an assert) when every slab is full
    and none is linked to grow into -- see nexus_program_errors_pool_slabs_add.
Thread-safety: not thread-safe -- the pool is plain static storage with no locking.
*/
extern NEXUS_NODISCARD NexusError *_nexus_program_errors_failure_create(NexusErrorCode code, const char *format, const char *file, uint32 line, ...);
/*
Summary: the va_list-accepting core behind _nexus_program_errors_failure_create -- exists so a caller that itself received "..." (e.g. _nexus_program_assert_or_error) can forward it here without redeclaring the whole pool-allocation body.
Description: same contract as _nexus_program_errors_failure_create in every other respect.
  args must come from a va_start (or va_copy) the caller still owns -- this function does not
  va_end it, matching every other va_list-accepting function in the standard library.
See: _nexus_program_errors_failure_create for the full contract.
*/
extern NEXUS_NODISCARD NexusError *_nexus_program_errors_failure_create_v(NexusErrorCode code, const char *format, const char *file, uint32 line,
                                                                          va_list args);

/*
Summary: links internal as current's cause -- current.contained_error = internal.
Description: mutates current in place through the pointer, so the link is visible to
  anyone else holding the same pointer (e.g. nexus_program_errors_error_print walking it
  later) -- there is no separate "commit" step.
Ownership: both arguments must already be valid NexusError pointers (pool-resident or one
  of the static sentinels); this call does not allocate, free, or take ownership of either.
*/
extern void nexus_program_errors_wrap(NexusError *current, NexusError *internal);

/*
Summary: constructs an outer error from code and format, linking internal as its cause.
Description: captures this call site's file and line. Evaluates each argument once,
  formats immediately, and consumes one error-pool slot without growing the pool.
  Uses the same variadic-macro extensions as NEXUS_PROGRAM_ERROR_FAILURE.
Params:
  internal  borrowed, non-NULL cause; its existing chain is preserved
  code  the new outer error's code
  format  nullable printf-style format followed by matching arguments
Returns: a new pooled outer error, or the unchanged pool-capacity sentinel on exhaustion.
Ownership: neither frees nor modifies internal. The result and any pooled cause
  remain valid until nexus_program_errors_pool_reset; callers never free them.
Errors: NULL internal asserts. Formatting and capacity follow
  _nexus_program_errors_failure_create's contract. On exhaustion no cause is linked.
Thread-safety: follows the error pool's synchronization policy; do not reset the
  pool or mutate the borrowed cause while constructing or using its chain.
Example: return NEXUS_PROGRAM_ERRORS_WRAP(error, code, "opening %s", path);
*/
#define NEXUS_PROGRAM_ERRORS_WRAP(internal, code, format, ...)                                                                                       \
  _nexus_program_errors_wrap((internal), (code), (format), __FILE__, __LINE__, ##__VA_ARGS__)

/*
Summary: internal implementation of NEXUS_PROGRAM_ERRORS_WRAP with explicit source location.
See: NEXUS_PROGRAM_ERRORS_WRAP for the full contract.
*/
extern NEXUS_NODISCARD NexusError *_nexus_program_errors_wrap(NexusError *internal, NexusErrorCode code, const char *format, const char *file,
                                                              uint32 line, ...);

/*
Summary: internal -- creates a "forwarding" marker error wrapping forwarded; call through NEXUS_PROGRAM_ERRORS_FORWARD, not directly, so file/line are captured at the real call site.
Description: the marker's own code is GLOBAL_NEXUS_ERROR_FORWARD, printed by
  nexus_program_errors_error_print as "(forwarded)" rather than unpacked/shown as a real
  library:facility error -- it exists purely to put an intermediate function's file:line
  into the causal chain when that function opts in, per the caller-policy design in
  nexus/adr/0001 (a bare "return error;" stays invisible in the trace; calling this doesn't).
Errors: may itself return nexus_program_errors_pool_capacity_reached, exactly like
  _nexus_program_errors_failure_create (which it calls internally) -- checked before forwarded
  is linked in, so a capacity-reached result never has a stale contained_error.
*/
extern NEXUS_NODISCARD NexusError *_nexus_program_errors_forward(NexusError *forwarded, const char *file, uint32 line);

/*
Summary: true if error->code equals code.
*/
static NEXUS_MAYBE_UNUSED boolean nexus_program_errors_is_error(NexusError *error, NexusErrorCode code) {
  return error->code == code;
}

/*
Summary: wraps forwarded in a forwarding marker, capturing __FILE__/__LINE__ at this call site -- see _nexus_program_errors_forward.
*/
#define NEXUS_PROGRAM_ERRORS_FORWARD(forwarded) _nexus_program_errors_forward((forwarded), __FILE__, __LINE__)

/*
Summary: renders error's whole causal chain (see NexusError.contained_error) into buffer as a Java-style "Caused by:" trace.
Description: each line is one error, most-recent/outermost first, indented one level deeper
  per cause, root cause last -- global code shown in both decimal and hex, plus the
  library/facility/category/local code from _nexus_program_error_code_unpack, except for
  forwarding markers which print only their file:line. Never writes past buffer_size (a
  destination of NULL is used internally once the real buffer is exhausted, matching
  stbsp_vsnprintf's own documented count-only mode -- see the inline comment at this
  function's definition for why a zero-capacity non-NULL pointer is avoided instead).
Params:
  buffer       may receive fewer bytes than Returns reports, if buffer_size is too small
  buffer_size  capacity of buffer, including room for the nul terminator
Returns: how many bytes the full, untruncated trace would need -- compare against
  buffer_size to detect truncation, the same contract as stbsp_vsnprintf's return value.
Errors:
  NEXUS_PROGRAM_PRECONDITION: error must not be NULL.
  NEXUS_PROGRAM_PRECONDITION: buffer must not be NULL.
*/
extern NEXUS_NODISCARD uint_large nexus_program_errors_error_print(NexusError *error, char *buffer, uint_large buffer_size);

/* ---------------------------------------------------------------------------- */
/* PANICS                                                                       */
/* ---------------------------------------------------------------------------- */

/*
Summary: logs message with a stack trace to stderr; does not abort by itself -- call through NEXUS_PROGRAM_PANIC, which aborts.
*/
extern void _nexus_program_panic(const char *message, const char *file, uint32 line, ...);
/*
Summary: logs error's full causal chain plus a stack trace to stderr; does not abort by itself -- call through NEXUS_PROGRAM_PANIC_ERROR, which aborts.
Description: renders via nexus_program_errors_error_print into a
  NEXUS_PROGRAM_PANIC_ERROR_MESSAGE_MAX_SIZE stack buffer first -- a chain long enough to
  need more than that truncates (a NEXUS_PROGRAM_PRECONDITION on the untruncated length
  catches this in NEXUS_DEVELOPER_MODE).
*/
extern void _nexus_program_panic_error(NexusError *error, const char *file, uint32 line);

/*
Summary: logs message and a stack trace, then aborts. Never returns.
Description: message is a printf-style format string; trailing variadic args format into it
  exactly as with NEXUS_PROGRAM_ERROR_FAILURE (see its Description for the disclosed C99/GNU
  exceptions this relies on).
*/
#define NEXUS_PROGRAM_PANIC(message, ...)                                                                                                            \
  do {                                                                                                                                               \
    _nexus_program_panic((message), __FILE__, __LINE__, ##__VA_ARGS__);                                                                              \
    abort();                                                                                                                                         \
  } while (0)

/*
Summary: logs error's causal chain and a stack trace, then aborts. Never returns.
Description: the trace reads outermost/most-recent cause first, root cause last -- see
  nexus_program_errors_error_print for the format.
*/
#define NEXUS_PROGRAM_PANIC_ERROR(error)                                                                                                             \
  do {                                                                                                                                               \
    _nexus_program_panic_error((error), __FILE__, __LINE__);                                                                                         \
    abort();                                                                                                                                         \
  } while (0)

/* ---------------------------------------------------------------------------- */
/* ASSERTIONS                                                                   */
/* ---------------------------------------------------------------------------- */

/*
Summary: the facility identifier for assertion/precondition/postcondition/invariant violations (see NexusProgramAssertionError below).
*/
#define NEXUS_ERROR_ASSERTION_FACILITY NEXUS_PROGRAM_ERROR_FACILITY_IDENTIFIER_CREATE('A', 'S', 'R', 'T')

/*
Summary: categories within NexusProgramAssertionError; currently just NPAEC_GENERIC, room for finer-grained categories later.
*/
typedef enum NexusProgramAssertionErrorCategory {
  NPAEC_GENERIC = 0,

  NPAEC_COUNT
} NexusProgramAssertionErrorCategory;

/*
Summary: the assertion subsystem's error list, defining NexusProgramAssertionError via NEXUS_ERROR_FACILITY_DEFINE below.
*/
#define NEXUS_ASSERTION_ERROR_LIST(X, library, facility)                                                                                             \
  X(NEXUS_ASSERTION_VIOLATION, NPAEC_GENERIC, 1, library, facility)                                                                                  \
  X(NEXUS_PRECONDITION_VIOLATION, NPAEC_GENERIC, 2, library, facility)                                                                               \
  X(NEXUS_POSTCONDITION_VIOLATION, NPAEC_GENERIC, 3, library, facility)                                                                              \
  X(NEXUS_INVARIANT_VIOLATION, NPAEC_GENERIC, 4, library, facility)                                                                                  \
  X(NEXUS_PANIC_VIOLATION, NPAEC_GENERIC, 5, library, facility)

NEXUS_ERROR_FACILITY_DEFINE(NexusProgramAssertionError, NEXUS_ERROR_LIBRARY_IDENTIFIER, NEXUS_ERROR_ASSERTION_FACILITY, NEXUS_ASSERTION_ERROR_LIST)

/*
Summary: logs a violation_type + message to stderr with a stack trace; does not abort itself -- called by the NEXUS_PROGRAM_ASSERT-family macros, which do.
Description: message is a printf-style format string; trailing variadic args format into it,
  same disclosed exceptions as NEXUS_PROGRAM_ERROR_FAILURE.
*/
typedef void NexusProgramDiagnosticFunction(void *user_data, const char *occurrence, const char *message, const char *file, uint32 line);

/*
Summary: installs or clears the process-wide observer notified before Nexus writes a program diagnostic to stderr.
Description: notified for every assertion-family violation and every panic (NEXUS_PROGRAM_PANIC and
  NEXUS_PROGRAM_PANIC_ERROR), whether or not a recovery point is armed. The process normally aborts
  as soon as the observer returns, so an observer that hands the diagnostic to an asynchronous
  consumer must also drain that consumer before returning.
Thread-safety: install before starting worker threads and clear only after diagnostic-producing threads have quiesced.
*/
extern void nexus_program_diagnostic_function_set(NexusProgramDiagnosticFunction *function, void *user_data);

extern void _nexus_program_assert(const char *violation_type, NexusErrorCode code, const char *message, const char *file, uint32 line, ...);
/*
Summary: the va_list-accepting core behind _nexus_program_assert -- see _nexus_program_errors_failure_create_v for why this sibling exists.
Description: does not call va_end on args -- same ownership contract as
  _nexus_program_errors_failure_create_v. code identifies which violation this is (see
  NEXUS_ASSERTION_ERROR_LIST) -- used only if a recovery point is armed (see
  nexus_program_recovery_arm) to construct the NexusError a recovered caller reads back via
  nexus_program_recovery_last_error_get; otherwise unused beyond that.
*/
extern void _nexus_program_assert_v(const char *violation_type, NexusErrorCode code, const char *message, const char *file, uint32 line,
                                    va_list args);

#ifndef NEXUS_DEVELOPER_MODE
#  define NEXUS_DEVELOPER_MODE FALSE
#endif

/*
Summary: the shared implementation behind every NEXUS_PROGRAM_*_OR_ERROR macro -- report+abort in NEXUS_DEVELOPER_MODE, or construct+return a NexusError otherwise.
Description: in NEXUS_DEVELOPER_MODE, a false condition reports via _nexus_program_assert and
  aborts, same as the plain (non-_OR_ERROR) assertion macros -- the NexusError-constructing
  path below it is unreachable there, present only to give the compiler a well-typed value
  for that branch. Outside NEXUS_DEVELOPER_MODE, a false condition constructs a NexusError via
  _nexus_program_errors_failure_create_v and returns it instead of aborting, retrying exactly
  once (growing the pool first) only if the first attempt reported pool-capacity-reached. Both
  attempts reuse the same va_list, not a va_copy of it: _nexus_program_errors_failure_create_v
  runs its capacity check before it ever touches the va_list (see its definition), so a
  pool-capacity-reached result guarantees the first attempt never called va_arg on args --
  retrying with the same still-unconsumed args is standards-legal (C99 7.15p3 only forbids
  reuse once some function has actually walked it via va_arg). va_copy would also not compile
  cleanly under this project's "-std=c89 -pedantic" (infra/standards): stdarg.h suppresses its
  declaration outside C99/GNU mode, so this sidesteps that too, not just the extra copy.
Returns: NEXUS_PROGRAM_ERROR_SUCCESS() if condition held; otherwise a pointer from
  _nexus_program_errors_failure_create_v (see its own Errors/Ownership).
*/
static NEXUS_MAYBE_UNUSED NexusError *_nexus_program_assert_or_error(boolean condition, const char *violation_type, NexusErrorCode code,
                                                                     const char *message, const char *file, uint32 line, ...) {
  if (!condition) {
#if NEXUS_DEVELOPER_MODE
    va_list args;

    va_start(args, line);
    _nexus_program_assert_v(violation_type, code, message, file, line, args);
    va_end(args);
    abort();
    return _nexus_program_errors_failure_create(code, (message), file,
                                                line); /* this will never be reached due to the abort, but I put it here to satisfy the compiler*/
#else
    NexusError *gotten_error;
    uint64      current_pool_capacity;
    va_list     args;

    (void)violation_type;
    (void)file;
    (void)line;

    va_start(args, line);

    /* args reused rather than va_copy'd below -- see this function's own Description. */
    gotten_error = _nexus_program_errors_failure_create_v(code, (message), file, line, args);

    if (gotten_error->code == nexus_program_errors_pool_capacity_reached.code) {
      current_pool_capacity = nexus_program_errors_pool_slabs_get();
      nexus_program_errors_pool_slabs_add(current_pool_capacity + 1); /* cheap */

      /* only re-attempt when the first attempt actually failed on capacity --
       * retrying unconditionally wasted a second pool slot + format pass on
       * every successful call. */
      gotten_error = _nexus_program_errors_failure_create_v(code, (message), file, line,
                                                            args); /* guaranteed to be the actual error, because if grow fails, it is an abort. */
    }

    va_end(args);

    return gotten_error;
#endif
  }

  return NEXUS_PROGRAM_ERROR_SUCCESS();
}

#if NEXUS_DEVELOPER_MODE
#  define NEXUS_PROGRAM_ASSERT(condition, message, ...)                                                                                              \
    do {                                                                                                                                             \
      if (!(condition)) {                                                                                                                            \
        _nexus_program_assert("ASSERTION", GLOBAL_NEXUS_ASSERTION_VIOLATION, (message), __FILE__, __LINE__, ##__VA_ARGS__);                          \
        abort();                                                                                                                                     \
      }                                                                                                                                              \
    } while (0)

#  define NEXUS_PROGRAM_PRECONDITION(condition, message, ...)                                                                                        \
    do {                                                                                                                                             \
      if (!(condition)) {                                                                                                                            \
        _nexus_program_assert("PRECONDITION", GLOBAL_NEXUS_PRECONDITION_VIOLATION, (message), __FILE__, __LINE__, ##__VA_ARGS__);                    \
        abort();                                                                                                                                     \
      }                                                                                                                                              \
    } while (0)

#  define NEXUS_PROGRAM_POSTCONDITION(condition, message, ...)                                                                                       \
    do {                                                                                                                                             \
      if (!(condition)) {                                                                                                                            \
        _nexus_program_assert("POSTCONDITION", GLOBAL_NEXUS_POSTCONDITION_VIOLATION, (message), __FILE__, __LINE__, ##__VA_ARGS__);                  \
        abort();                                                                                                                                     \
      }                                                                                                                                              \
    } while (0)

#  define NEXUS_PROGRAM_INVARIANT(condition, message, ...)                                                                                           \
    do {                                                                                                                                             \
      if (!(condition)) {                                                                                                                            \
        _nexus_program_assert("INVARIANT", GLOBAL_NEXUS_INVARIANT_VIOLATION, (message), __FILE__, __LINE__, ##__VA_ARGS__);                          \
        abort();                                                                                                                                     \
      }                                                                                                                                              \
    } while (0)

#else
#  define NEXUS_PROGRAM_ASSERT(condition, message, ...)
#  define NEXUS_PROGRAM_PRECONDITION(condition, message, ...)
#  define NEXUS_PROGRAM_POSTCONDITION(condition, message, ...)
#  define NEXUS_PROGRAM_INVARIANT(condition, message, ...)
#endif

/*
Summary: like NEXUS_PROGRAM_ASSERT, but returns a NexusError instead of aborting outside NEXUS_DEVELOPER_MODE -- see _nexus_program_assert_or_error.
*/
#define NEXUS_PROGRAM_ASSERT_OR_ERROR(condition, message, ...)                                                                                       \
  _nexus_program_assert_or_error((condition), "ASSERTION", GLOBAL_NEXUS_ASSERTION_VIOLATION, (message), __FILE__, __LINE__, ##__VA_ARGS__)
#define NEXUS_PROGRAM_ASSERT_ALWAYS(condition, message, ...)                                                                                         \
  do {                                                                                                                                               \
    if (!(condition)) {                                                                                                                              \
      _nexus_program_assert("ASSERTION", GLOBAL_NEXUS_ASSERTION_VIOLATION, (message), __FILE__, __LINE__, ##__VA_ARGS__);                            \
      abort();                                                                                                                                       \
    }                                                                                                                                                \
  } while (0)

/*
Summary: like NEXUS_PROGRAM_PRECONDITION, but returns a NexusError instead of aborting outside NEXUS_DEVELOPER_MODE -- see _nexus_program_assert_or_error.
*/
#define NEXUS_PROGRAM_PRECONDITION_OR_ERROR(condition, message, ...)                                                                                 \
  _nexus_program_assert_or_error((condition), "PRECONDITION", GLOBAL_NEXUS_PRECONDITION_VIOLATION, (message), __FILE__, __LINE__, ##__VA_ARGS__)
#define NEXUS_PROGRAM_PRECONDITION_ALWAYS(condition, message, ...)                                                                                   \
  do {                                                                                                                                               \
    if (!(condition)) {                                                                                                                              \
      _nexus_program_assert("PRECONDITION", GLOBAL_NEXUS_PRECONDITION_VIOLATION, (message), __FILE__, __LINE__, ##__VA_ARGS__);                      \
      abort();                                                                                                                                       \
    }                                                                                                                                                \
  } while (0)

/*
Summary: like NEXUS_PROGRAM_POSTCONDITION, but returns a NexusError instead of aborting outside NEXUS_DEVELOPER_MODE -- see _nexus_program_assert_or_error.
*/
#define NEXUS_PROGRAM_POSTCONDITION_OR_ERROR(condition, message, ...)                                                                                \
  _nexus_program_assert_or_error((condition), "POSTCONDITION", GLOBAL_NEXUS_POSTCONDITION_VIOLATION, (message), __FILE__, __LINE__, ##__VA_ARGS__)
#define NEXUS_PROGRAM_POSTCONDITION_ALWAYS(condition, message, ...)                                                                                  \
  do {                                                                                                                                               \
    if (!(condition)) {                                                                                                                              \
      _nexus_program_assert("POSTCONDITION", GLOBAL_NEXUS_POSTCONDITION_VIOLATION, (message), __FILE__, __LINE__, ##__VA_ARGS__);                    \
      abort();                                                                                                                                       \
    }                                                                                                                                                \
  } while (0)

/*
Summary: like NEXUS_PROGRAM_INVARIANT, but returns a NexusError instead of aborting outside NEXUS_DEVELOPER_MODE -- see _nexus_program_assert_or_error.
*/
#define NEXUS_PROGRAM_INVARIANT_OR_ERROR(condition, message, ...)                                                                                    \
  _nexus_program_assert_or_error((condition), "INVARIANT", GLOBAL_NEXUS_INVARIANT_VIOLATION, (message), __FILE__, __LINE__, ##__VA_ARGS__)
#define NEXUS_PROGRAM_INVARIANT_ALWAYS(condition, message, ...)                                                                                      \
  do {                                                                                                                                               \
    if (!(condition)) {                                                                                                                              \
      _nexus_program_assert("INVARIANT", GLOBAL_NEXUS_INVARIANT_VIOLATION, (message), __FILE__, __LINE__, ##__VA_ARGS__);                            \
      abort();                                                                                                                                       \
    }                                                                                                                                                \
  } while (0)

/* ---------------------------------------------------------------------------- */
/* RECOVERY                                                                     */
/* ---------------------------------------------------------------------------- */

#include <setjmp.h>

/*
Summary: arms this thread's recovery point -- a subsequent NEXUS_PROGRAM_ASSERT/PRECONDITION/
  POSTCONDITION/INVARIANT (plain or _ALWAYS) or NEXUS_PROGRAM_PANIC/PANIC_ERROR failure, on this
  thread only, longjmps to *recovery_point instead of aborting.
Description: setjmp's saved state is only valid for the exact stack frame that calls it directly,
  so this cannot be a plain "arm and setjmp for you" function -- the caller calls setjmp(*recovery_point)
  itself, at its own call site. Arm only from setjmp's direct-return branch, after the recovery
  point contains a valid saved environment. The intended shape:
    jmp_buf recovery_point;
    if (setjmp(recovery_point) == 0) {
      nexus_program_recovery_arm(&recovery_point);
      risky_call();               // may fail via a Nexus assertion/panic deep inside
      nexus_program_recovery_disarm();
    } else {
      nexus_program_recovery_disarm();  // idempotent; recovery also disarms before longjmp
      NexusError *error = nexus_program_recovery_last_error_get();  // what failed
    }
  Thread-local, not global: a background thread this process spawns (Nexus has its own threading
  module) that hits a failure while this thread has recovery armed still aborts the normal way --
  a longjmp can only ever unwind within the thread that called the matching setjmp, so a
  cross-thread "recovery" would be nonsensical even if attempted.
  Scope: this only ever intercepts Nexus's own explicit assertion/panic checks -- an
  "if (!condition)" that calls into this failure path. It cannot and does not catch a real
  SIGSEGV/hardware trap from a wild pointer write, which is a signal delivered by the OS, bypassing
  this mechanism (and every other piece of ordinary C control flow) entirely. A caller that needs
  to survive a real crash, not just a controlled failure, still needs process-level isolation --
  see nexus/adr/0004's NEXUS_PROCESS_EXIT_SIGNALED, unchanged by this mechanism.
Params: recovery_point  borrowed; must remain a live stack frame (still inside its own setjmp
  call's enclosing function) for as long as recovery stays armed -- disarm before that frame
  returns.
Errors: a NULL recovery_point is a programmer error (asserts).
Thread-safety: affects only the calling thread's own recovery state.
See: nexus/adr/0005
*/
extern void nexus_program_recovery_arm(jmp_buf *recovery_point);
/*
Summary: disarms this thread's recovery point -- a subsequent assertion/panic failure aborts
  again, same as if nexus_program_recovery_arm had never been called.
Description: idempotent -- disarming when nothing is armed is a no-op, not an error, so a caller
  can always safely disarm unconditionally after a guarded call resolves (see
  nexus_program_recovery_arm's Description), regardless of which branch it took.
Thread-safety: affects only the calling thread's own recovery state.
*/
extern void nexus_program_recovery_disarm(void);
/*
Summary: the NexusError a recovered (longjmp'd-to) failure left behind -- call only from the
  setjmp-nonzero branch immediately after a recovery fires.
Description: reuses the same pool-resident NexusError machinery every other Nexus failure path
  uses (see nexus_program_errors_error_print) -- not a second, parallel failure-detail channel.
  Recovery disarms itself before longjmp, so the handler may retrieve and format this error
  without a second Nexus failure recursively jumping into the same handler.
  Reading this when no recovery has fired since the last arm is meaningless (returns whatever the
  last recovery on this thread left, or NULL if none ever has) -- only call it right after a
  setjmp call you armed returns nonzero.
Ownership: the returned pointer is pool storage, never freed by the caller -- same ownership as
  any other NexusError this codebase hands back.
Thread-safety: reads only the calling thread's own recovery state.
*/
extern NexusError *nexus_program_recovery_last_error_get(void);

/* ---------------------------------------------------------------------------- */
/* OS ERRORS                                                                    */
/* ---------------------------------------------------------------------------- */

#include <errno.h>

/*
Summary: the facility identifier for errno-derived errors raised via NEXUS_PROGRAM_ERROR_FROM_ERRNO.
Description: unlike NexusProgramErrorError/NexusProgramAssertionError, this facility has no
  NEXUS_ERROR_FACILITY_DEFINE list -- its local code is the raw platform errno value itself
  (passthrough), not a curated set of named errors, since the point is representing whatever
  errno a syscall happens to set without maintaining a mapping table for it. See
  _nexus_program_errors_from_errno_create's Description for why that's gated to
  NEXUS_PLATFORM_LINUX specifically rather than the broader NEXUS_PLATFORM_POSIX.
*/
#define NEXUS_ERROR_OS_FACILITY NEXUS_PROGRAM_ERROR_FACILITY_IDENTIFIER_CREATE('O', 'S', 'E', 'R')

/*
Summary: NexusProgramOsError's one category -- passthrough has nothing to subdivide by, but NEXUS_ERROR_LOCAL's layout always carries a category alongside the local code.
*/
typedef enum NexusProgramOsErrorCategory {
  NPOEC_ERRNO,

  NPOEC_COUNT
} NexusProgramOsErrorCategory;

/*
Summary: constructs a NexusError from an already-captured errno value and an optional printf-style format+args -- the only public entry point for _nexus_program_errors_from_errno_create.
Description: reads errno at NEXUS_PROGRAM_ERROR_FROM_ERRNO's own expansion site, not inside this
  function -- any intervening call, even one that succeeds, may overwrite errno, so nothing may
  run between the failing call and that macro. The resulting message is strerror(errno_value)'s
  text alone if format is NULL, or "<rendered format>: <strerror text>" otherwise -- format is
  optional exactly as with NEXUS_PROGRAM_ERROR_FAILURE.
  NEXUS_ERROR_LOCAL and NEXUS_PROGRAM_ERROR_CODE_MAKE both validate their arguments with a
  bitfield-width trick that requires a compile-time constant expression -- correct for a
  facility's fixed, compile-time error list, but it will not compile against a runtime int like
  errno. This packs the code by hand instead, with a runtime bounds check
  (NEXUS_PROGRAM_PRECONDITION_ALWAYS, not compiled away in any build) standing in for the
  compile-time one -- matching nexus/adr/0001's stance that failing to represent an error
  correctly is fatal, not silently truncated into a neighboring bit field.
  Linux-only for now: POSIX standardizes errno's *names*, not their numeric values, and
  Linux/macOS/BSD diverge above roughly errno 34 -- passthrough would silently bake that
  divergence into a NexusErrorCode the moment Nexus targets a second POSIX platform. Gated to
  NEXUS_PLATFORM_LINUX rather than NEXUS_PLATFORM_POSIX so that never happens quietly; revisit
  (a real per-platform mapping, or a decoded semantic layer alongside the raw passthrough, the
  way Rust's std::io::ErrorKind sits alongside its raw OS error code) if/when it's needed.
Params:
  errno_value  the errno to convert -- must already be captured by the caller (see
                 NEXUS_PROGRAM_ERROR_FROM_ERRNO), 0-4095
  format       nullable -- NULL means no additional message beyond strerror's text; otherwise a
                 printf-style format string, variadic args following it exactly as with
                 stbsp_vsnprintf
Returns: (see Errors) a pointer into pool-resident storage, or nexus_program_errors_pool_capacity_reached
Ownership: same as _nexus_program_errors_failure_create -- pool-resident, caller never frees.
Errors:
  NEXUS_PROGRAM_PRECONDITION_ALWAYS: errno_value must fit in 0-4095 (every build, not compiled away).
  NEXUS_PROGRAM_PRECONDITION: same message-length contract as _nexus_program_errors_failure_create.
  Returns nexus_program_errors_pool_capacity_reached (not an assert) when the pool is exhausted.
Thread-safety: not thread-safe -- neither the error pool nor strerror's static buffer are locked.
Platform: NEXUS_PLATFORM_UNSUPPORTED_RAISE outside NEXUS_PLATFORM_LINUX.
*/
extern NEXUS_NODISCARD NexusError *_nexus_program_errors_from_errno_create(int32 errno_value, const char *format, const char *file, uint32 line, ...);
/*
Summary: constructs a NexusError from the current errno, supplying __FILE__/__LINE__ automatically -- see _nexus_program_errors_from_errno_create for the full contract.
Description: errno is read here, at this macro's own expansion site -- see
  _nexus_program_errors_from_errno_create's Description for why that matters. Same disclosed
  C99/GNU exceptions as NEXUS_PROGRAM_ERROR_FAILURE (comma-eliding ##__VA_ARGS__).
*/
#define NEXUS_PROGRAM_ERROR_FROM_ERRNO(format, ...) _nexus_program_errors_from_errno_create(errno, (format), __FILE__, __LINE__, ##__VA_ARGS__)

/* ============================================================================ */
/* ---------------------------------------------------------------------------- */
/* MEMORY                                                                       */
/* ---------------------------------------------------------------------------- */
/* ============================================================================ */

/*
Summary: an open file connection -- opaque; always a FILE* underneath, cast at the boundary in
  n_filesystem.c. Not exposed by default; nexus_filesystem_file_descriptor_get is the one
  deliberate escape hatch, for a caller (e.g. a library wrapping a third-party API that needs a
  raw descriptor, such as sharing an anonymous file's fd with another process) that genuinely
  needs it.
Description: forward-declared here, ahead of the FILESYSTEM section below where the rest of its
  API lives, because nexus_memory_map (immediately following) needs it -- MEMORY comes before
  FILESYSTEM in this file's ordering, and nothing about that ordering was worth disturbing for
  one typedef.
*/
typedef void NexusFileHandle;

#if defined(__GNUC__) || defined(__clang__)
#  define C_PRAGMA_(x)         _Pragma(#x)
#  define C_POISON(identifier) C_PRAGMA_(GCC poison identifier)
#else
#  error "C_POISON is unsupported by this compiler"
#endif

#ifndef NEXUS_MEMORY_PRE_PADDING_SIZE
#  define NEXUS_MEMORY_PRE_PADDING_SIZE 128
#endif

#ifndef NEXUS_MEMORY_POST_PADDING_SIZE
#  define NEXUS_MEMORY_POST_PADDING_SIZE 128
#endif

#ifndef NEXUS_MEMORY_FREE_QUARANTINE_CAPACITY_ALLOCS
#  define NEXUS_MEMORY_FREE_QUARANTINE_CAPACITY_ALLOCS 1024
#endif

#ifndef NEXUS_MEMORY_DEBUG_MODE
#  define NEXUS_MEMORY_DEBUG_MODE FALSE
#endif

#ifndef NEXUS_MEMORY_TRACING
#  define NEXUS_MEMORY_TRACING FALSE
#endif

#ifndef NEXUS_MEMORY_MAGIC_NUMBER
#  define NEXUS_MEMORY_MAGIC_NUMBER 0xCF
#endif

#ifndef NEXUS_MEMORY_INITIALIZATION
#  define NEXUS_MEMORY_INITIALIZATION 0xCD
#endif

#ifndef NEXUS_MEMORY_FREED
#  define NEXUS_MEMORY_FREED 0xCE
#endif

/* Clients of Nexus must always allocate through the nexus provided functions, never through the stdlib ones.
   NEXUS_MEMORY_HIJACK (define before including this header -- see nexus_hijack.h) is the one sanctioned
   opt-out: it redirects malloc/calloc/realloc/free/memset/memcpy to Nexus's own tracked implementations
   instead of refusing to compile them, for a translation unit that specifically wants that. It does not
   affect mmap/munmap/mprotect, which stay poisoned unconditionally -- see nexus_hijack.h's own comment for
   why those three aren't safe to redirect with a macro the way the allocation functions are. */

#ifndef NEXUS_INTERNAL_MEMORY_IMPLEMENTATION

#  ifndef NEXUS_MEMORY_HIJACK
C_POISON(malloc)
C_POISON(calloc)
C_POISON(realloc)
C_POISON(free)

C_POISON(memset)
C_POISON(memcpy)
#  endif

/* Same reasoning as malloc/free above -- mmap/munmap/mprotect must go through
   nexus_memory_map/unmap/protect (n_memory.c), never called directly, so every raw mapping
   in the process is made through one place. */
C_POISON(mmap)
C_POISON(munmap)
C_POISON(mprotect)

#endif

/*
Summary: the signature a caller-supplied memory-operation tracer must implement.
Description: message is a printf-style format string; trailing variadic args format into it
  exactly as with NEXUS_PROGRAM_ERROR_FAILURE (see its Description for the disclosed C99/GNU
  exceptions this relies on). Called once per traced operation, after that operation has
  completed -- see nexus_memory_trace_function_set.
*/
typedef void NexusMemoryTraceFn(const char *message, ...);

/*
Summary: installs function as the receiver for every traced memory operation, or disables tracing.
Description: has no effect unless NEXUS_MEMORY_TRACING is set -- outside it, none of the call
  sites that would invoke the tracer are compiled in at all, independent of
  NEXUS_MEMORY_DEBUG_MODE in both directions (trace without paying for guards, checksums, and
  the quarantine ring, or run debug mode silently). NULL disables tracing.
Params:
  function  NULL to disable tracing, or a function matching NexusMemoryTraceFn to receive it
Thread-safety: not thread-safe -- the stored function pointer is plain static storage with no locking.
*/
extern void nexus_memory_trace_function_set(NexusMemoryTraceFn *function);

/*
Summary: tells the allocator to keep up to `bytes` of freed memory in the process and to serve blocks of up to `bytes` (at most 32 MiB) from its heap
  rather than mapping every large block anew and unmapping it on free.
Description: a program that builds and drops large structures over and over (a syntax tree per edit, say) otherwise pays a page fault for every page of
  every structure, which on a virtual machine costs more than the work that fills them. Memory the program has freed stays mapped up to `bytes`
  and is reused. A value of 0 restores the allocator's defaults. Applies to the glibc allocator in a build without NEXUS_MEMORY_DEBUG_MODE; elsewhere
  it does nothing.
Params:
  bytes  the amount of freed memory the process may hold; values above 2 GiB are treated as 2 GiB
Errors: none.
Thread-safety: call it once at start-up, before threads allocate; the allocator itself is thread-safe.
*/
extern void nexus_memory_retention_set(uint_large bytes);

/*
In NEXUS_MEMORY_DEBUG_MODE every allocation carries an inline header (size, allocation
site, an optional comment, a persistent flag, and a checksum over all of it) sandwiched
between two guard regions, and is tracked on a global intrusive list until it is freed into
a bounded quarantine ring rather than ever being handed back to libc immediately -- see
adr/0002-memory-debugger-tracking for the full design and why each piece exists. Outside
debug mode every function below is a thin, unconditional pass-through to the matching libc
call, with none of this machinery compiled in.

NEXUS_MEMORY_TRACING is a second, independent gate: when set, every operation below calls
through to whatever function nexus_memory_trace_function_set installed, once the operation
has actually completed -- so a trace entry for allocate/reallocate reports the pointer
actually returned, not just what was requested, and deallocate/mark_persistent/comment_set
are only ever traced for an operation that didn't panic partway through.
*/

/*
Summary: allocates size bytes, tracked and guarded in NEXUS_MEMORY_DEBUG_MODE.
Description: in debug mode, seeds the returned region with NEXUS_MEMORY_INITIALIZATION so a
  read of never-written memory is loud and recognizable rather than silently plausible.
Params:
  size  must be greater than zero
Errors:
  NEXUS_PROGRAM_PRECONDITION: size must be greater than 0.
  NEXUS_PROGRAM_POSTCONDITION_ALWAYS: aborts (every build, not compiled away) if the
    underlying allocator returns NULL -- this function itself never returns NULL.
Thread-safety: not thread-safe -- the live-allocation list and its bookkeeping are plain
  static storage with no locking.
*/
extern void *_nexus_memory_allocate(uint_large size, const char *file, uint32 line);

/*
Summary: resizes the allocation at ptr to new_size, tracked and guarded in NEXUS_MEMORY_DEBUG_MODE.
Description: never actually calls libc's realloc in debug mode -- the inline header and
  guards move with the block, so it is always a fresh allocate, copy, and quarantine of the
  old pointer instead. Copies min(old size, new size) bytes and seeds any newly grown tail
  with NEXUS_MEMORY_INITIALIZATION; a shrinking call poisons nothing new. The comment and
  persistent flag carry forward, since a reallocated block is still the same logical
  allocation.
Params:
  ptr       must be an address nexus_memory_allocate/reallocate previously returned, still live
  new_size  must be greater than zero
Errors:
  NEXUS_PROGRAM_PRECONDITION: ptr must not be NULL -- call nexus_memory_allocate for a first
    allocation instead of relying on realloc's NULL-behaves-like-malloc fallback.
  NEXUS_PROGRAM_PRECONDITION: new_size must be greater than 0 -- realloc with a size of zero
    is undefined behavior as of C23 (WG14 N2464), regardless of ptr.
  NEXUS_PROGRAM_PANIC: ptr is not exactly a live or quarantined Nexus allocation -- reports
    whether it lies strictly inside one (an interior pointer) or matches nothing at all.
  NEXUS_PROGRAM_PANIC: ptr was already freed (use-after-free).
  NEXUS_PROGRAM_ASSERT_ALWAYS: the allocation's header, or a neighbor's, is corrupted, or its
    guard bytes were overrun or underrun.
  NEXUS_PROGRAM_POSTCONDITION_ALWAYS: aborts (every build, not compiled away) if the
    underlying allocator returns NULL -- this function itself never returns NULL.
Thread-safety: not thread-safe -- the live-allocation list, quarantine ring, and stats are
  plain static storage with no locking.
*/
extern void *_nexus_memory_reallocate(void *ptr, uint_large new_size, const char *file, uint32 line);

/*
Summary: frees the allocation at ptr, tracked and guarded in NEXUS_MEMORY_DEBUG_MODE.
Description: never actually calls libc's free in debug mode -- the block is poisoned with
  NEXUS_MEMORY_FREED and moved into a bounded quarantine ring instead, so a dangling pointer
  used shortly after this call lands on a recognizable pattern rather than memory that still
  looks live, and a second free of the same pointer is caught rather than corrupting the heap.
Params:
  ptr  must be an address nexus_memory_allocate/reallocate previously returned, still live
Errors:
  NEXUS_PROGRAM_PRECONDITION: ptr must not be NULL.
  NEXUS_PROGRAM_PANIC: ptr is not exactly a live or quarantined Nexus allocation -- reports
    whether it lies strictly inside one (an interior pointer) or matches nothing at all.
  NEXUS_PROGRAM_PANIC: ptr was already freed (double-free).
  NEXUS_PROGRAM_ASSERT_ALWAYS: the allocation's header, or a neighbor's, is corrupted, or its
    guard bytes were overrun or underrun.
Thread-safety: not thread-safe -- the live-allocation list, quarantine ring, and stats are
  plain static storage with no locking.
*/
extern void _nexus_memory_deallocate(void *ptr, const char *file, uint32 line);

/*
Summary: frees ptr via the real, unpoisoned, unhijacked libc free -- the one sanctioned way to
  release memory a pre-built third-party library allocated and owns the allocator contract for
  (e.g. an XCB reply struct), which nexus_memory_deallocate cannot safely accept (it validates
  every pointer against Nexus's own tracked-allocation list and panics on anything else) and
  which nexus_hijack.h cannot reach (that only redirects allocation calls made as source compiled
  into this build, not calls already compiled into a linked .so/.a -- see its own doc comment).
Description: NULL is not an error (mirrors libc free(NULL) being a no-op, and
  nexus_filesystem_file_close's same contract). Works identically whether the calling translation
  unit has the default poisoning active or has opted into nexus_hijack.h -- this is a normal
  function, not the token `free`, so neither mechanism touches a call to it.
Params: ptr  must be an address a foreign (non-Nexus) allocator returned, still live by that
  allocator's own contract -- never a pointer nexus_memory_allocate/reallocate returned. Passing
  a Nexus-tracked pointer here instead of to nexus_memory_deallocate skips Nexus's own tracking
  entirely and frees at the wrong address (Nexus-tracked pointers are offset from the real
  malloc'd base by a header Nexus itself manages) -- undefined behavior, not a caught error, the
  same way calling libc free twice on one pointer is.
*/
extern void nexus_memory_foreign_free(void *ptr);

/*
Summary: fills size bytes at ptr with value.
Description: a thin, checked wrapper over memset -- does not itself validate ptr against the
  tracked allocation (that happens on the next operation that recovers ptr's header, such as
  a free or realloc). nexus_memory_bytes_clear is this with value fixed to 0.
Params:
  ptr  must not be NULL
Errors:
  NEXUS_PROGRAM_PRECONDITION: ptr must not be NULL.
*/
extern void _nexus_memory_bytes_set(void *ptr, uint_large size, byte value, const char *file, uint32 line);

/*
Summary: copies size bytes from src_ptr to dest_ptr.
Description: a thin, checked wrapper over memcpy -- does not itself validate either pointer
  against its tracked allocation, and does not handle overlapping regions (memcpy's own
  contract applies unchanged).
Params:
  src_ptr   must not be NULL
  dest_ptr  must not be NULL
Errors:
  NEXUS_PROGRAM_PRECONDITION: src_ptr must not be NULL.
  NEXUS_PROGRAM_PRECONDITION: dest_ptr must not be NULL.
*/
extern void _nexus_memory_bytes_copy(const void *src_ptr, void *dest_ptr, uint_large size, const char *file, uint32 line);

/*
Summary: moves size bytes from src_ptr to dest_ptr, permitting overlapping regions.
Description: a checked, traced wrapper over memmove. Does not validate pointers
  against tracked allocation bounds. Source and destination must both be non-NULL,
  including for zero size. Identical regions are valid; no allocation is performed.
Errors: NULL source/destination asserts in developer mode.
*/
extern void _nexus_memory_bytes_move(const void *src_ptr, void *dest_ptr, uint_large size, const char *file, uint32 line);

/*
Summary: like nexus_memory_bytes_copy, but never reports to the installed memory trace hook.
Description: for internal bookkeeping copies that move an already-accounted-for, fixed-size
  element and aren't a new application-visible allocation event -- e.g. NexusChannel's ring-buffer
  slot writes/reads. Tracing those is just noise, and for a trace hook that itself logs through a
  channel built on this same copy, it turns every message into another traced copy to report.
  Still asserts src_ptr/dest_ptr non-NULL; only the trace call is skipped.
Params:
  src_ptr   must not be NULL
  dest_ptr  must not be NULL
Errors:
  NEXUS_PROGRAM_PRECONDITION: src_ptr must not be NULL.
  NEXUS_PROGRAM_PRECONDITION: dest_ptr must not be NULL.
*/
extern void _nexus_memory_bytes_copy_untraced(const void *src_ptr, void *dest_ptr, uint_large size, const char *file, uint32 line);

/*
Summary: compares size bytes at left_ptr and right_ptr for equality.
Description: compares raw bytes, including embedded zeros. Zero size returns
  true. Identical and overlapping regions are allowed. Does not allocate or
  invoke the memory trace hook; comparison time may depend on the contents.
Params:
  left_ptr   borrowed readable region of at least size bytes; never NULL
  right_ptr  borrowed readable region of at least size bytes; never NULL
Returns: true when every compared byte is equal, false otherwise.
Ownership: both regions remain caller-owned; neither is modified or retained.
Errors: NULL left_ptr or right_ptr is a programmer error (asserts), even
  when size is zero.
Thread-safety: safe for concurrent reads; callers synchronize writes.
Complexity: O(size) worst-case time, O(1) space.
*/
extern boolean _nexus_memory_bytes_equal(const void *left_ptr, const void *right_ptr, uint_large size, const char *file, uint32 line);

/*
Summary: excludes ptr from nexus_memory_leak_report without unlinking it from the live list.
Description: for state deliberately kept for the program's entire lifetime (the error pool,
  program_context) that must never be reported as a leak. The block still counts toward
  nexus_memory_summary_report's current/peak/total stats, and its guards and checksum are
  still validated on every operation that touches it -- only the leak report skips it. A
  no-op outside NEXUS_MEMORY_DEBUG_MODE, since there is no header to mark.
Params:
  ptr  must be an address nexus_memory_allocate/reallocate previously returned, still live
Errors:
  NEXUS_PROGRAM_PRECONDITION: ptr must not be NULL.
  NEXUS_PROGRAM_PANIC: ptr is not exactly a live or quarantined Nexus allocation, or was
    already freed.
  NEXUS_PROGRAM_ASSERT_ALWAYS: the allocation's header is corrupted, or its guard bytes were
    overrun or underrun.
Thread-safety: not thread-safe -- the live-allocation list is plain static storage with no locking.
*/
extern void _nexus_memory_mark_persistent(void *ptr, const char *file, uint32 line);

/*
Summary: attaches a human-readable comment to the allocation at ptr, shown by nexus_memory_leak_report.
Description: comment is stored by pointer, never copied -- like file, it must be a string
  literal or otherwise outlive the allocation. Passing NULL clears a previously set comment.
  Carried forward across nexus_memory_reallocate, since a reallocated block is still the
  same logical allocation. A no-op outside NEXUS_MEMORY_DEBUG_MODE, since there is no header
  to attach it to.
Params:
  ptr      must be an address nexus_memory_allocate/reallocate previously returned, still live
  comment  a string literal or otherwise-persistent string, or NULL to clear
Errors:
  NEXUS_PROGRAM_PRECONDITION: ptr must not be NULL.
  NEXUS_PROGRAM_PANIC: ptr is not exactly a live or quarantined Nexus allocation, or was
    already freed.
  NEXUS_PROGRAM_ASSERT_ALWAYS: the allocation's header is corrupted, or its guard bytes were
    overrun or underrun.
Thread-safety: not thread-safe -- the live-allocation list is plain static storage with no locking.
*/
extern void _nexus_memory_comment_set(void *ptr, const char *comment, const char *file, uint32 line);

/*
We have no Calloc equivalent on purpose because it's easy to at a glance miss whether something is mallocing or callocing, which can lead to bugs.

Allocate + Clear is far more explicit and readable.
*/

/*
Summary: the tracked, zero-filling allocation nexus_hijack.h's calloc(...) redirect resolves
  to -- not a general-purpose calloc equivalent (see the no-calloc-equivalent rationale directly
  above; that reasoning is unchanged and still governs Nexus-native code). This exists only so
  nexus_hijack.h's redirect has something faithful to send a third party's calloc() call to when
  that third party's own source is compiled under the hijack, without patching it -- Nexus-native
  code should keep writing nexus_memory_allocate + nexus_memory_bytes_clear explicitly, same as
  always; this function is not part of that pattern and not meant to be called by name directly.
Errors: NEXUS_PROGRAM_PRECONDITION: count and size must each be > 0; count * size must not
  overflow uint_large.
*/
extern void *_nexus_memory_hijack_calloc(uint_large count, uint_large size, const char *file, uint32 line);

#define nexus_memory_allocate(size)            _nexus_memory_allocate((size), __FILE__, __LINE__)
#define nexus_memory_reallocate(ptr, new_size) _nexus_memory_reallocate((ptr), (new_size), __FILE__, __LINE__)
#define nexus_memory_deallocate(ptr)           _nexus_memory_deallocate((ptr), __FILE__, __LINE__)

#define nexus_memory_bytes_set(ptr, size, value)                  _nexus_memory_bytes_set((ptr), (size), (value), __FILE__, __LINE__)
#define nexus_memory_bytes_clear(ptr, size)                       _nexus_memory_bytes_set((ptr), (size), 0, __FILE__, __LINE__)
#define nexus_memory_bytes_copy(src_ptr, dest_ptr, size)          _nexus_memory_bytes_copy((src_ptr), (dest_ptr), (size), __FILE__, __LINE__)
#define nexus_memory_bytes_move(src_ptr, dest_ptr, size)          _nexus_memory_bytes_move((src_ptr), (dest_ptr), (size), __FILE__, __LINE__)
#define nexus_memory_bytes_copy_untraced(src_ptr, dest_ptr, size) _nexus_memory_bytes_copy_untraced((src_ptr), (dest_ptr), (size), __FILE__, __LINE__)
#define nexus_memory_bytes_equal(left_ptr, right_ptr, size)       _nexus_memory_bytes_equal((left_ptr), (right_ptr), (size), __FILE__, __LINE__)

/*
Summary: zeroes a whole object in place -- nexus_memory_bytes_clear over the object's own address and size.
Description: object must be an lvalue naming the object itself (a struct or array variable), not a
  pointer to it: given a pointer variable this clears the pointer. Meant for stack structs that
  must start fully zeroed (e.g. API info structs whose unset fields are read by the callee), where
  no brace initializer is warning-clean under -Wc++-compat and -Wmissing-field-initializers.
Errors: none beyond nexus_memory_bytes_clear's own.
*/
#define nexus_memory_object_clear(object) nexus_memory_bytes_clear((void *)&(object), NEXUS_SIZEOF(object))

#define nexus_memory_mark_persistent(ptr)      _nexus_memory_mark_persistent((ptr), __FILE__, __LINE__)
#define nexus_memory_comment_set(ptr, comment) _nexus_memory_comment_set((ptr), (comment), __FILE__, __LINE__)

/*
Summary: page protection for nexus_memory_map/protect -- a bitwise OR of what the mapped/protected pages may be used for.
*/
typedef enum NexusMemoryProtection {
  NMP_NONE  = 0,
  NMP_READ  = 1 << 0,
  NMP_WRITE = 1 << 1,
  NMP_EXEC  = 1 << 2,

  NMP_READ_WRITE = NMP_READ | NMP_WRITE
} NexusMemoryProtection;

/*
Summary: whether a mapping's pages are private to this mapping (NMS_PRIVATE, copy-on-write --
  the default that makes sense for an anonymous mapping only this process will ever see) or
  shared with every other mapping of the same file_handle, in this process or another
  (NMS_SHARED -- required for a mapping meant to be handed to, or exchanged with, something else
  that maps the same underlying file, such as a Wayland compositor mapping a client's shared
  buffer).
*/
typedef enum NexusMemorySharing {
  NMS_PRIVATE,
  NMS_SHARED
} NexusMemorySharing;

/*
Summary: the OS page size used to align mapping and protection addresses.
*/
extern NEXUS_NODISCARD uint_large nexus_memory_page_size_get(void);
/*
Summary: maps size usable bytes, anonymously or from file offset zero.
Description:
  A NULL file_handle creates zero-filled anonymous memory. Otherwise maps
  the file from offset zero, independently of its stream position. The file
  must already contain at least size bytes; this call never resizes it.
  Flush buffered writes first. Use nexus_filesystem_file_size_get to query
  its OS-visible size and nexus_filesystem_file_resize to grow it explicitly.
  size need not be page-aligned. The OS covers whole pages internally, but
  bytes beyond size are outside this API's usable range, including the tail
  of a file's final page. Use the returned size unchanged for unmap/protect.
  Shared writes update the file; private writes do not. Unmapping is not a
  durability barrier for shared writes.
Params:
  file_handle  nullable borrowed handle; may be closed after mapping succeeds
  out_pointer  out; page-aligned base on success, NULL on returned failure
  out_mapped_size  out; exactly size on success, zero on returned failure;
    usable byte length, not the OS's page-rounded address-space extent
Ownership: caller owns the mapping; release with nexus_memory_unmap.
Errors:
  Zero size, size exceeding size_t, or NULL output pointers assert.
  Returns NEXUS_FILESYSTEM_ERROR_FILE_TOO_SMALL when size exceeds EOF,
  file-size query errors, or an OS error when mmap fails.
Thread-safety:
  Caller must synchronize buffered I/O and file resizing with mapping and
  use. The EOF check is a snapshot: later truncation can still cause SIGBUS.
See: adr/0011-explicit-file-sizing-for-memory-maps
*/
extern NEXUS_NODISCARD NexusError *nexus_memory_map(NexusFileHandle *file_handle, uint_large size, NexusMemoryProtection protection,
                                                    NexusMemorySharing sharing, void **out_pointer, uint_large *out_mapped_size);
/*
Summary: unmaps a region previously returned by nexus_memory_map.
Description: pass the returned base and byte length unchanged. The starting
  address must be page-aligned; size need not be. Every intersecting page
  is unmapped, so a partial unmap also affects its final page in full.
Ownership: on success releases the specified pages; their pointers are invalid.
Errors:
  NULL pointer, zero size, or size exceeding size_t assert.
  Returns an OS error on munmap failure, including an unaligned address.
Thread-safety: caller must quiesce access to the pages being unmapped.
*/
extern NEXUS_NODISCARD NexusError *nexus_memory_unmap(void *pointer, uint_large size);
/*
Summary: changes access protection for pages intersecting a mapped byte range.
Description: pointer must be page-aligned; size need not be. Every page
  intersecting the range changes protection, including its final page.
Ownership: borrows the mapping; does not release or enlarge its usable range.
Errors:
  NULL pointer, zero size, or size exceeding size_t assert.
  Returns an OS error on mprotect failure, including an unaligned address.
Thread-safety: caller must synchronize access with protection changes.
*/
extern NEXUS_NODISCARD NexusError *nexus_memory_protect(void *pointer, uint_large size, NexusMemoryProtection protection);

/*
Summary: an mmap-backed stack with a guard page at its overflow end -- see nexus_stack_create.
Fields:
  usable       start of the writable region -- this is what a ucontext-style stack_t's ss_sp
                 (or equivalent) expects; the guard page sits immediately below it
  usable_size  size of the writable region, page-aligned (rounded up from what was requested)
  base, mapped_size  the whole mapping (guard page + usable region) -- only nexus_stack_destroy needs these
*/
typedef struct NexusStack {
  void      *base;
  void      *usable;
  uint_large usable_size;
  uint_large mapped_size;
} NexusStack;

/*
Summary: allocates a stack of at least usable_size bytes (rounded up to a whole page), with one PROT_NONE guard page immediately below the usable region.
Description: stacks on every architecture this runs on grow downward, so the guard page goes
  at the low-address end -- the direction an overflowing stack actually writes into. Overflow
  and use-after-destroy both become an immediate SEGV (an unmapped/PROT_NONE page faults on any
  access) rather than silent corruption -- stronger than the heap allocator's own software
  guard bytes above, for free, because a real stack's overflow direction is known in advance
  and its lifetime is explicit (destroy unmaps outright, it doesn't quarantine-and-poison).
  No pooling/reuse here -- create/destroy are the raw primitive; a scheduler that wants to
  recycle stacks across coroutines owns that policy itself (same posture as
  nexus/adr/0003 on environment-variable caching: Nexus gives you the correct primitive,
  not a cache it would have to guess the right invalidation rule for).
Errors: a NULL out_stack, zero usable_size, or a size whose page rounding plus
  guard page cannot be represented by uint_large is a programmer error (always
  asserts).
*/
extern NEXUS_NODISCARD NexusError *nexus_stack_create(uint_large usable_size, NexusStack *out_stack);
/*
Summary: unmaps stack's entire region (guard page + usable). Safe to call on an already-destroyed (zeroed) NexusStack.
*/
extern void nexus_stack_destroy(NexusStack *stack);

/*
Summary: renders a report of every non-persistent allocation still live into buffer, one line per leak plus a totals line.
Description: skips any allocation marked via nexus_memory_mark_persistent -- it is still live,
  still validated on every operation, just never counted or listed here. Each entry includes
  the comment set via nexus_memory_comment_set, if any. Byte counts are rendered in the
  largest unit (B/KiB/MiB/GiB/TiB) for which the value is still at least 1, not raw byte
  counts. Only ever reports anything when NEXUS_MEMORY_DEBUG_MODE tracking is active -- in a
  release build no allocation is ever tracked, so the report is always empty. Never writes
  past buffer_size (a destination of NULL is used internally once the real buffer is
  exhausted, the same stbsp_vsnprintf count-only convention as
  nexus_program_errors_error_print).
Params:
  buffer       may receive fewer bytes than Returns reports, if buffer_size is too small
  buffer_size  capacity of buffer, including room for the nul terminator
Returns: how many bytes the full, untruncated report would need -- compare against
  buffer_size to detect truncation, the same contract as stbsp_vsnprintf's return value.
Errors:
  NEXUS_PROGRAM_PRECONDITION: buffer must not be NULL.
Thread-safety: not thread-safe -- the live-allocation list is plain static storage with no locking.
*/
extern NEXUS_NODISCARD uint_large nexus_memory_leak_report(char *buffer, uint_large buffer_size);

/*
Summary: renders current/peak/total allocation stats plus a per-call-site hotspot list into buffer.
Description: a hotspot is any call site (a single nexus_memory_allocate/reallocate call, or a
  realloc call site inside a loop) whose lifetime allocation count reaches
  hotspot_min_allocations -- exactly the pattern worth checking for hoisting or buffer reuse.
  Reported in first-seen order, not sorted by count. Byte counts are rendered in the largest
  unit (B/KiB/MiB/GiB/TiB) for which the value is still at least 1, not raw byte counts. Only
  ever reports anything when NEXUS_MEMORY_DEBUG_MODE tracking is active -- in a release build
  nothing is ever tracked, so the report always shows all zeros and no hotspots. Never writes
  past buffer_size (a destination of NULL is used internally once the real buffer is
  exhausted, the same stbsp_vsnprintf count-only convention as
  nexus_program_errors_error_print).
Params:
  buffer                   may receive fewer bytes than Returns reports, if buffer_size is too small
  buffer_size              capacity of buffer, including room for the nul terminator
  hotspot_min_allocations  a call site is listed once its lifetime allocation count reaches this
Returns: how many bytes the full, untruncated report would need -- compare against
  buffer_size to detect truncation, the same contract as stbsp_vsnprintf's return value.
Errors:
  NEXUS_PROGRAM_PRECONDITION: buffer must not be NULL.
Thread-safety: not thread-safe -- the stats counters and hotspot table are plain static
  storage with no locking.
*/
extern NEXUS_NODISCARD uint_large nexus_memory_summary_report(char *buffer, uint_large buffer_size, uint_large hotspot_min_allocations);

/* ============================================================================ */
/* ---------------------------------------------------------------------------- */
/* ENTROPY                                                                      */
/* ---------------------------------------------------------------------------- */
/* ============================================================================ */

/*
Summary: entropy acquisition errors.
*/
#define NEXUS_ERROR_ENTROPY_FACILITY NEXUS_PROGRAM_ERROR_FACILITY_IDENTIFIER_CREATE('E', 'N', 'T', 'R')
typedef enum NexusEntropyErrorCategory {
  NEEC_SOURCE,
  NEEC_COUNT
} NexusEntropyErrorCategory;
#define NEXUS_ENTROPY_ERROR_LIST(X, library, facility) X(NEXUS_ENTROPY_ERROR_SOURCE_FAILED, NEEC_SOURCE, 1, library, facility)
NEXUS_ERROR_FACILITY_DEFINE(NexusEntropyError, NEXUS_ERROR_LIBRARY_IDENTIFIER, NEXUS_ERROR_ENTROPY_FACILITY, NEXUS_ENTROPY_ERROR_LIST)

/*
Summary: acquires unpredictable bytes from the operating system.
Description: uses getrandom, getentropy, or BCryptGenRandom; no deterministic
  fallback. May wait for OS random initialization. Zero bytes succeeds.
Params: out_bytes  out; exactly byte_count bytes written on success
Ownership: output storage is borrowed for the duration of the call.
Errors: NULL output with nonzero count asserts. Returns
  NEXUS_ENTROPY_ERROR_SOURCE_FAILED on OS failure or unsupported platforms;
  discard the entire output buffer after failure.
Thread-safety: safe to call concurrently.
Complexity: O(byte_count) time, O(1) space, no allocation.
*/
extern NEXUS_NODISCARD NexusError *nexus_entropy_system_fill(byte *out_bytes, uint_large byte_count);

/* ============================================================================ */
/* ---------------------------------------------------------------------------- */
/* RANDOM                                                                       */
/* ---------------------------------------------------------------------------- */
/* ============================================================================ */

/*
Summary: random generator errors.
*/
#define NEXUS_ERROR_RANDOM_FACILITY NEXUS_PROGRAM_ERROR_FACILITY_IDENTIFIER_CREATE('R', 'A', 'N', 'D')
typedef enum NexusRandomErrorCategory {
  NREC_GENERATOR,
  NREC_COUNT
} NexusRandomErrorCategory;
#define NEXUS_RANDOM_ERROR_LIST(X, library, facility)                                                                                                \
  X(NEXUS_RANDOM_ERROR_TAPE_EXHAUSTED, NREC_GENERATOR, 1, library, facility)                                                                         \
  X(NEXUS_RANDOM_ERROR_STREAM_EXHAUSTED, NREC_GENERATOR, 2, library, facility)
NEXUS_ERROR_FACILITY_DEFINE(NexusRandomError, NEXUS_ERROR_LIBRARY_IDENTIFIER, NEXUS_ERROR_RANDOM_FACILITY, NEXUS_RANDOM_ERROR_LIST)

/*
Summary: version of the stable random byte-stream and scalar-draw contract.
See: adr/0009-separate-entropy-and-random-generation
*/
#define NEXUS_RANDOM_SEQUENCE_VERSION 1

/*
Summary: fills a caller buffer with bytes from a random source.
Params:
  state      source-specific borrowed state; nullable when the source needs none
  out_bytes  out; exactly byte_count bytes are written on success
Errors:
  A NULL out_bytes with nonzero byte_count is a programmer error.
  Returns a source-specific NexusError when bytes cannot be produced.
*/
typedef NexusError *NexusRandomFillFunction(void *state, byte *out_bytes, uint_large byte_count);

/*
Summary: a borrowed random source consisting of state plus its fill operation.
Description:
  Sources are capability objects. They do not imply cryptographic security:
  nexus_random_system_source_get is cryptographically secure, while generator
  sources are deterministic and intended for simulation, testing, or explicit
  pseudorandom workloads. Sampling retains the source's quality; a counter or
  tape is a test fixture, not a statistically uniform generator.
  Fallible random operations return Nexus error chains; source/exhaustion
  reasons may be nested in contained_error behind forwarding markers. Error
  pool exhaustion can replace the original reason with a pool-capacity error.
Ownership: state remains owned by the caller and must outlive every source use.
*/
typedef struct NexusRandomSource {
  void                    *state;
  NexusRandomFillFunction *fill;
} NexusRandomSource;

/*
Summary: deterministic algorithms supported by NexusRandomGenerator.
Values:
  NRA_LCG64         small compatibility generator; not statistically strong
  NRA_SPLITMIX64    fast seed expansion and independent lightweight streams
  NRA_XOROSHIRO128  fast general simulation generator
  NRA_PCG32         PCG-XSH-RR 64/32 output with a 63-bit stream selector
  NRA_CHACHA20      deterministic cryptographic stream generator
  NRA_TAPE          replay caller-provided bytes exactly once
  NRA_COUNTER       predictable diagnostic sequence
*/
typedef enum NexusRandomAlgorithm {
  NRA_LCG64,
  NRA_SPLITMIX64,
  NRA_XOROSHIRO128,
  NRA_PCG32,
  NRA_CHACHA20,
  NRA_TAPE,
  NRA_COUNTER
} NexusRandomAlgorithm;

/*
Summary: fixed working-storage size; not configurable or a serialized format.
*/
#define NEXUS_RANDOM_GENERATOR_STATE_WORDS 40

/*
Summary: caller-owned state for one deterministic random generator.
Description:
  Initialize with exactly one nexus_random_generator_initialize_* function.
  The storage is public only so generators can live inline or on the stack;
  clients must not interpret or modify it directly. Initialization never
  acquires entropy. No implicit global generator exists. The named algorithms
  retain identical output across library versions and architectures under
  NEXUS_RANDOM_SEQUENCE_VERSION. Integer draws decode successive little-endian
  bytes; mixing draw widths or splitting fills consumes the same byte stream.
  State memory is not a portable serialization format.
  xoroshiro128+ uses the original (55, 14, 36) transition; a zero seed maps to
  lo=0x9e3779b97f4a7c15, hi=0x6a09e667f3bcc909. PCG32 emits low word first
  when combining two outputs. Only ChaCha20 has a cryptographic construction;
  its key must be secret and unpredictable and its stream positions unique.
  Do not use LCG64, SplitMix64, xoroshiro128+, or PCG32 for secrets.
Ownership:
  The generator owns no allocation. A tape generator borrows its tape bytes.
  Generating and sampling from built-in generators never allocates.
Thread-safety: one generator may be used by only one thread at a time.
See: adr/0009-separate-entropy-and-random-generation
*/
typedef struct NexusRandomGenerator {
  NexusRandomAlgorithm algorithm;
  uint32               word_index;
  uint64               state[NEXUS_RANDOM_GENERATOR_STATE_WORDS];
  const byte          *tape;
  uint_large           tape_length;
  uint_large           tape_offset;
} NexusRandomGenerator;

/*
Summary: returns the operating system's cryptographically secure random source.
Description:
  Use for secrets, unpredictable HashMap seeds, UUIDv4, and UUIDv7 random
  fields. The source has no caller-owned state and may be copied freely.
Returns: a stateless source backed by nexus_random_system_fill; it is safe to copy.
*/
extern NexusRandomSource nexus_random_system_source_get(void);

/*
Summary: fills out_bytes from the operating system cryptographic random source.
Params:
  state      ignored; present for NexusRandomFillFunction compatibility
  out_bytes  out; exactly byte_count bytes are written on success
Errors:
  A NULL out_bytes with nonzero byte_count is a programmer error.
  Returns NEXUS_ENTROPY_ERROR_SOURCE_FAILED when the operating system source
  fails; no partially filled buffer should be consumed after failure.
Thread-safety: safe to call concurrently.
*/
extern NEXUS_NODISCARD NexusError *nexus_random_system_fill(void *state, byte *out_bytes, uint_large byte_count);

/*
Summary: draws seed_size bytes of seed material from the secure OS source.
Description: supports arbitrary seed widths, including 32-byte ChaCha20 keys.
  Does not initialize or mutate a generator. Zero bytes succeeds. All byte
  patterns are permitted; this does not enforce algorithm-specific seed rules.
Params: out_seed  out; exactly seed_size bytes written on success
Ownership: caller owns the borrowed output storage; it is never retained.
Errors: NULL output with nonzero seed_size asserts. Returns an OS entropy
  error on failure; discard the entire seed buffer after failure.
Thread-safety: safe to call concurrently for distinct output storage.
Complexity: O(seed_size) time, O(1) space, no allocation.
*/
extern NEXUS_NODISCARD NexusError *nexus_random_seed_fill(byte *out_seed, uint_large seed_size);

/*
Summary: draws a 32-bit seed from the secure OS source.
Description: accepts every bit pattern, including zero. Does not seed a
  generator automatically; record the returned value for later replay.
Params: out_seed  out; assigned only on success
Ownership: caller owns the borrowed output storage; it is never retained.
Errors: NULL output asserts. Returns an OS entropy error chain on failure.
Thread-safety: safe to call concurrently for distinct output storage.
Complexity: O(1) time and space, no allocation.
*/
extern NEXUS_NODISCARD NexusError *nexus_random_seed_uint32_get(uint32 *out_seed);

/*
Summary: draws a 64-bit seed from the secure OS source.
Description: accepts every bit pattern, including zero. Does not seed a
  generator automatically; record the returned value for later replay.
Params: out_seed  out; assigned only on success
Ownership: caller owns the borrowed output storage; it is never retained.
Errors: NULL output asserts. Returns an OS entropy error chain on failure.
Thread-safety: safe to call concurrently for distinct output storage.
Complexity: O(1) time and space, no allocation.
*/
extern NEXUS_NODISCARD NexusError *nexus_random_seed_uint64_get(uint64 *out_seed);

/*
Summary: draws a 128-bit seed from the secure OS source.
Description: decodes 16 random bytes little-endian, low limb first. Accepts
  every bit pattern, including zero. Does not initialize a generator; record
  both limbs for later replay rather than serializing native object memory.
Params: out_seed  out; both limbs assigned only on success
Ownership: caller owns the borrowed output storage; it is never retained.
Errors: NULL output asserts. Returns an OS entropy error chain on failure.
Thread-safety: safe to call concurrently for distinct output storage.
Complexity: O(1) time and space, no allocation.
*/
extern NEXUS_NODISCARD NexusError *nexus_random_seed_uint128_get(uint128 *out_seed);

/*
Summary: fills out_bytes through source.
Params:
  source     borrowed and not retained
  out_bytes  out; exactly byte_count bytes are written on success
Errors:
  NULL source.fill or NULL out_bytes with nonzero byte_count is a programmer
  error. Returns the source's error chain, wrapped with this call site. Discard output after failure.
Thread-safety: inherits the selected source's contract.
*/
extern NEXUS_NODISCARD NexusError *nexus_random_source_fill(NexusRandomSource source, byte *out_bytes, uint_large byte_count);

/*
Summary: initializes generator as a 64-bit linear congruential generator.
Description: retained for compatibility and simple diagnostics; do not use for
  security or statistical simulation where generator quality matters.
Params: generator  caller-owned state; overwritten completely
Errors: a NULL generator is a programmer error (asserts).
*/
extern void nexus_random_generator_initialize_lcg64(NexusRandomGenerator *generator, uint64 seed);

/*
Summary: initializes generator as SplitMix64.
Description: best used to expand a compact seed into other generator state or
  for lightweight independent streams, not for security.
Params: generator  caller-owned state; overwritten completely
Errors: a NULL generator is a programmer error (asserts).
*/
extern void nexus_random_generator_initialize_splitmix64(NexusRandomGenerator *generator, uint64 seed);

/*
Summary: initializes generator as xoroshiro128+ from a 128-bit seed.
Description: use for fast simulations that do not require cryptographic output.
  An all-zero seed is deterministically remapped to a nonzero state.
Params: generator  caller-owned state; overwritten completely
Errors: a NULL generator is a programmer error (asserts).
*/
extern void nexus_random_generator_initialize_xoroshiro128(NexusRandomGenerator *generator, uint128 seed);

/*
Summary: initializes generator as PCG-XSH-RR with a selectable stream.
Description: PCG-XSH-RR 64/32 with reference two-step seeding. Only the low
  63 bits of sequence select the stream; selectors differing only in bit 63
  select the same stream. Never uses operating-system entropy.
Params: generator  caller-owned state; overwritten completely
Errors: a NULL generator is a programmer error (asserts).
*/
extern void nexus_random_generator_initialize_pcg32(NexusRandomGenerator *generator, uint64 seed, uint64 sequence);

/*
Summary: initializes generator as a ChaCha stream with a 256-bit key.
Description: use when deterministic output must remain unpredictable without
  the key. Uses RFC 8439's 20 rounds, 96-bit nonce, and 32-bit block counter.
  Never reuse a key/nonce/counter position. No automatic seeding, reseeding,
  or forward-secrecy guarantee. Fills beyond the remaining counter space fail
  without modifying output or generator state.
Params:
  key    borrowed 32-byte key, copied during initialization
  nonce  borrowed 12-byte nonce, copied during initialization
Errors: a NULL generator, key, or nonce is a programmer error (asserts).
*/
extern void nexus_random_generator_initialize_chacha20(NexusRandomGenerator *generator, const byte key[32], const byte nonce[12], uint32 counter);

/*
Summary: initializes generator to replay a caller-provided byte tape.
Description: use for exact regression reproduction. Reads never wrap.
Ownership: tape is borrowed and must outlive generator use.
Errors: a NULL generator, or NULL tape with nonzero tape_length, is a programmer error (asserts).
*/
extern void nexus_random_generator_initialize_tape(NexusRandomGenerator *generator, const void *tape, uint_large tape_length);

/*
Summary: initializes generator as a predictable counter sequence.
Description: each 64-bit draw yields the current value, then advances by step.
  Use only for diagnostics and deterministic boundary tests.
Params: generator  caller-owned state; overwritten completely
Errors: a NULL generator is a programmer error (asserts).
*/
extern void nexus_random_generator_initialize_counter(NexusRandomGenerator *generator, uint64 start, uint64 step);

/*
Summary: returns a source borrowing generator.
Returns: a source whose state pointer is generator.
Ownership: generator must outlive use of the returned source.
Thread-safety: one generator may be advanced by only one thread at a time.
Errors: a NULL generator is a programmer error (asserts).
*/
extern NexusRandomSource nexus_random_generator_source_get(NexusRandomGenerator *generator);

/*
Summary: fills out_bytes from generator and advances its state.
Errors:
  NULL generator or NULL out_bytes with nonzero byte_count is a programmer
  error. Returns NEXUS_RANDOM_ERROR_TAPE_EXHAUSTED if a tape has too few
  bytes, or NEXUS_RANDOM_ERROR_STREAM_EXHAUSTED before ChaCha20's counter
  would wrap. Both leave state and output unchanged for that fill request.
  An invalid algorithm asserts. A zero-byte fill succeeds for initialized state.
Thread-safety: one generator may be advanced by only one thread at a time.
*/
extern NEXUS_NODISCARD NexusError *nexus_random_generator_fill(void *generator, byte *out_bytes, uint_large byte_count);

/*
Summary: draws one 32-bit value from generator.
Errors: NULL generator or out_value is a programmer error. Tape exhaustion is
  returned as NEXUS_RANDOM_ERROR_TAPE_EXHAUSTED; ChaCha20 exhaustion returns
  NEXUS_RANDOM_ERROR_STREAM_EXHAUSTED. Invalid algorithms assert.
*/
extern NEXUS_NODISCARD NexusError *nexus_random_generator_uint32_get(NexusRandomGenerator *generator, uint32 *out_value);

/*
Summary: draws one 64-bit value from generator.
Errors: NULL generator or out_value is a programmer error. Tape exhaustion is
  returned as NEXUS_RANDOM_ERROR_TAPE_EXHAUSTED; ChaCha20 exhaustion returns
  NEXUS_RANDOM_ERROR_STREAM_EXHAUSTED. Invalid algorithms assert.
*/
extern NEXUS_NODISCARD NexusError *nexus_random_generator_uint64_get(NexusRandomGenerator *generator, uint64 *out_value);

/*
Summary: draws one little-endian integer from source.
Params: out_value  out; assigned only on success
Ownership: source and output storage are borrowed, never retained.
Errors: NULL source.fill, NULL output asserts.
  Returns the selected source error chain.
Thread-safety: synchronize access to shared mutable source state.
Complexity: O(1) for built-in generators; inherits source costs otherwise.
*/
extern NEXUS_NODISCARD NexusError *nexus_random_source_uint32_get(NexusRandomSource source, uint32 *out_value);

/*
Summary: draws one little-endian integer from source.
Params: out_value  out; assigned only on success
Ownership: source and output storage are borrowed, never retained.
Errors: NULL source.fill, NULL output asserts.
  Returns the selected source error chain.
Thread-safety: synchronize access to shared mutable source state.
Complexity: O(1) for built-in generators; inherits source costs otherwise.
*/
extern NEXUS_NODISCARD NexusError *nexus_random_source_uint64_get(NexusRandomSource source, uint64 *out_value);

/*
Summary: draws one unbiased integer in [0, upper_bound) from source.
Description: rejects draws below 2^width modulo upper_bound, then reduces
  modulo upper_bound. Requires uniform input draws; may consume many draws.
  A finite source may exhaust after consuming rejected draws.
Params: out_value  out; assigned only on success
Ownership: source and output storage are borrowed, never retained.
Errors: NULL source.fill, NULL output, or zero upper_bound asserts.
  Returns the selected source error chain.
Thread-safety: synchronize access to shared mutable source state.
Complexity: expected O(1) draws, unbounded worst case; inherits source costs.
*/
extern NEXUS_NODISCARD NexusError *nexus_random_source_uint32_bounded_get(NexusRandomSource source, uint32 upper_bound, uint32 *out_value);

/*
Summary: draws one unbiased integer in [0, upper_bound) from source.
Description: rejects draws below 2^width modulo upper_bound, then reduces
  modulo upper_bound. Requires uniform input draws; may consume many draws.
  A finite source may exhaust after consuming rejected draws.
Params: out_value  out; assigned only on success
Ownership: source and output storage are borrowed, never retained.
Errors: NULL source.fill, NULL output, or zero upper_bound asserts.
  Returns the selected source error chain.
Thread-safety: synchronize access to shared mutable source state.
Complexity: expected O(1) draws, unbounded worst case; inherits source costs.
*/
extern NEXUS_NODISCARD NexusError *nexus_random_source_uint64_bounded_get(NexusRandomSource source, uint64 upper_bound, uint64 *out_value);

/*
Summary: draws one unbiased integer in [minimum, maximum) from source.
Description: draws an unbiased offset below maximum - minimum, then adds
  minimum. Requires uniform input draws; may consume multiple draws. A finite
  source can exhaust after rejected draws; output remains unchanged on error.
Params:
  minimum    inclusive lower bound
  maximum    exclusive upper bound; must exceed minimum
  out_value  out; assigned only on success
Ownership: source and output storage are borrowed, never retained.
Errors: NULL source.fill, NULL output, or minimum >= maximum asserts.
  Returns the selected source error chain.
Thread-safety: synchronize access to shared mutable source state.
Complexity: expected O(1) draws, unbounded worst case; inherits source costs.
See: nexus_random_source_uint32_bounded_get
*/
extern NEXUS_NODISCARD NexusError *nexus_random_source_uint32_range_get(NexusRandomSource source, uint32 minimum, uint32 maximum, uint32 *out_value);

/*
Summary: draws one unbiased integer in [minimum, maximum) from source.
Description: draws an unbiased offset below maximum - minimum, then adds
  minimum. Requires uniform input draws; may consume multiple draws. A finite
  source can exhaust after rejected draws; output remains unchanged on error.
Params:
  minimum    inclusive lower bound
  maximum    exclusive upper bound; must exceed minimum
  out_value  out; assigned only on success
Ownership: source and output storage are borrowed, never retained.
Errors: NULL source.fill, NULL output, or minimum >= maximum asserts.
  Returns the selected source error chain.
Thread-safety: synchronize access to shared mutable source state.
Complexity: expected O(1) draws, unbounded worst case; inherits source costs.
See: nexus_random_source_uint64_bounded_get
*/
extern NEXUS_NODISCARD NexusError *nexus_random_source_uint64_range_get(NexusRandomSource source, uint64 minimum, uint64 maximum, uint64 *out_value);

/*
Summary: draws one unbiased signed integer in [minimum, maximum) from source.
Description: supports negative bounds and intervals crossing zero, including
  spans wider than INT32_MAX_VAL. Uses unsigned arithmetic for the span
  and sampling; signed conversion never overflows or casts outside its range.
  Requires uniform input draws. Rejected draws may consume source state.
Params:
  minimum    inclusive lower bound
  maximum    exclusive upper bound; must exceed minimum
  out_value  out; assigned only on success
Ownership: source and output storage are borrowed, never retained.
Errors: NULL source.fill, NULL output, or minimum >= maximum asserts.
  Returns the selected source error chain.
Thread-safety: synchronize access to shared mutable source state.
Complexity: expected O(1) draws, unbounded worst case; inherits source costs.
See: nexus_random_source_uint32_range_get
*/
extern NEXUS_NODISCARD NexusError *nexus_random_source_int32_range_get(NexusRandomSource source, int32 minimum, int32 maximum, int32 *out_value);

/*
Summary: draws one unbiased signed integer in [minimum, maximum) from source.
Description: supports negative bounds and intervals crossing zero, including
  spans wider than INT64_MAX_VAL. Uses unsigned arithmetic for the span
  and sampling; signed conversion never overflows or casts outside its range.
  Requires uniform input draws. Rejected draws may consume source state.
Params:
  minimum    inclusive lower bound
  maximum    exclusive upper bound; must exceed minimum
  out_value  out; assigned only on success
Ownership: source and output storage are borrowed, never retained.
Errors: NULL source.fill, NULL output, or minimum >= maximum asserts.
  Returns the selected source error chain.
Thread-safety: synchronize access to shared mutable source state.
Complexity: expected O(1) draws, unbounded worst case; inherits source costs.
See: nexus_random_source_uint64_range_get
*/
extern NEXUS_NODISCARD NexusError *nexus_random_source_int64_range_get(NexusRandomSource source, int64 minimum, int64 maximum, int64 *out_value);

/*
Summary: draws one uniform value in [0, 1) from source.
Description: uses the upper 24 bits of a uint32 draw, scaled by 2^-24.
  The fixed grid includes zero and excludes one; requires uniform input.
Params: out_value  out; assigned only on success
Ownership: source and output storage are borrowed, never retained.
Errors: NULL source.fill, NULL output asserts.
  Returns the selected source error chain.
Thread-safety: synchronize access to shared mutable source state.
Complexity: O(1) for built-in generators; inherits source costs otherwise.
*/
extern NEXUS_NODISCARD NexusError *nexus_random_source_real32_get(NexusRandomSource source, real32 *out_value);

/*
Summary: draws one uniform value in [0, 1) from source.
Description: uses the upper 53 bits of a uint64 draw, scaled by 2^-53.
  The fixed grid includes zero and excludes one; requires uniform input.
Params: out_value  out; assigned only on success
Ownership: source and output storage are borrowed, never retained.
Errors: NULL source.fill, NULL output asserts.
  Returns the selected source error chain.
Thread-safety: synchronize access to shared mutable source state.
Complexity: O(1) for built-in generators; inherits source costs otherwise.
*/
extern NEXUS_NODISCARD NexusError *nexus_random_source_real64_get(NexusRandomSource source, real64 *out_value);

/*
Summary: draws one unbiased integer in [0, upper_bound) from generator.
Description: rejects draws below 2^width modulo upper_bound, then reduces
  modulo upper_bound. Requires uniform input draws; may consume many draws.
  A finite source may exhaust after consuming rejected draws.
Params: out_value  out; assigned only on success
Ownership: generator and output storage are borrowed, never retained.
Errors: NULL generator, NULL output, or zero upper_bound asserts.
  Invalid algorithms assert; returns a random tape/stream exhaustion chain.
Thread-safety: synchronize access to shared mutable source state.
Complexity: expected O(1) draws, unbounded worst case; inherits source costs.
*/
extern NEXUS_NODISCARD NexusError *nexus_random_generator_uint32_bounded_get(NexusRandomGenerator *generator, uint32 upper_bound, uint32 *out_value);

/*
Summary: draws one unbiased integer in [0, upper_bound) from generator.
Description: rejects draws below 2^width modulo upper_bound, then reduces
  modulo upper_bound. Requires uniform input draws; may consume many draws.
  A finite source may exhaust after consuming rejected draws.
Params: out_value  out; assigned only on success
Ownership: generator and output storage are borrowed, never retained.
Errors: NULL generator, NULL output, or zero upper_bound asserts.
  Invalid algorithms assert; returns a random tape/stream exhaustion chain.
Thread-safety: synchronize access to shared mutable source state.
Complexity: expected O(1) draws, unbounded worst case; inherits source costs.
*/
extern NEXUS_NODISCARD NexusError *nexus_random_generator_uint64_bounded_get(NexusRandomGenerator *generator, uint64 upper_bound, uint64 *out_value);

/*
Summary: draws one unbiased integer in [minimum, maximum) from generator.
Description: draws an unbiased offset below maximum - minimum, then adds
  minimum. Requires uniform input draws; may consume multiple draws. A finite
  source can exhaust after rejected draws; output remains unchanged on error.
Params:
  minimum    inclusive lower bound
  maximum    exclusive upper bound; must exceed minimum
  out_value  out; assigned only on success
Ownership: generator and output storage are borrowed, never retained.
Errors: NULL generator, NULL output, or minimum >= maximum asserts.
  Invalid algorithms assert; returns a random tape/stream exhaustion chain.
Thread-safety: synchronize access to shared mutable source state.
Complexity: expected O(1) draws, unbounded worst case; inherits source costs.
See: nexus_random_generator_uint32_bounded_get
*/
extern NEXUS_NODISCARD NexusError *nexus_random_generator_uint32_range_get(NexusRandomGenerator *generator, uint32 minimum, uint32 maximum,
                                                                           uint32 *out_value);

/*
Summary: draws one unbiased integer in [minimum, maximum) from generator.
Description: draws an unbiased offset below maximum - minimum, then adds
  minimum. Requires uniform input draws; may consume multiple draws. A finite
  source can exhaust after rejected draws; output remains unchanged on error.
Params:
  minimum    inclusive lower bound
  maximum    exclusive upper bound; must exceed minimum
  out_value  out; assigned only on success
Ownership: generator and output storage are borrowed, never retained.
Errors: NULL generator, NULL output, or minimum >= maximum asserts.
  Invalid algorithms assert; returns a random tape/stream exhaustion chain.
Thread-safety: synchronize access to shared mutable source state.
Complexity: expected O(1) draws, unbounded worst case; inherits source costs.
See: nexus_random_generator_uint64_bounded_get
*/
extern NEXUS_NODISCARD NexusError *nexus_random_generator_uint64_range_get(NexusRandomGenerator *generator, uint64 minimum, uint64 maximum,
                                                                           uint64 *out_value);

/*
Summary: draws one unbiased signed integer in [minimum, maximum) from generator.
Description: supports negative bounds and intervals crossing zero, including
  spans wider than INT32_MAX_VAL. Uses unsigned arithmetic for the span
  and sampling; signed conversion never overflows or casts outside its range.
  Requires uniform input draws. Rejected draws may consume source state.
Params:
  minimum    inclusive lower bound
  maximum    exclusive upper bound; must exceed minimum
  out_value  out; assigned only on success
Ownership: generator and output storage are borrowed, never retained.
Errors: NULL generator, NULL output, or minimum >= maximum asserts.
  Invalid algorithms assert; returns a random tape/stream exhaustion chain.
Thread-safety: synchronize access to shared mutable source state.
Complexity: expected O(1) draws, unbounded worst case; inherits source costs.
See: nexus_random_generator_uint32_range_get
*/
extern NEXUS_NODISCARD NexusError *nexus_random_generator_int32_range_get(NexusRandomGenerator *generator, int32 minimum, int32 maximum,
                                                                          int32 *out_value);

/*
Summary: draws one unbiased signed integer in [minimum, maximum) from generator.
Description: supports negative bounds and intervals crossing zero, including
  spans wider than INT64_MAX_VAL. Uses unsigned arithmetic for the span
  and sampling; signed conversion never overflows or casts outside its range.
  Requires uniform input draws. Rejected draws may consume source state.
Params:
  minimum    inclusive lower bound
  maximum    exclusive upper bound; must exceed minimum
  out_value  out; assigned only on success
Ownership: generator and output storage are borrowed, never retained.
Errors: NULL generator, NULL output, or minimum >= maximum asserts.
  Invalid algorithms assert; returns a random tape/stream exhaustion chain.
Thread-safety: synchronize access to shared mutable source state.
Complexity: expected O(1) draws, unbounded worst case; inherits source costs.
See: nexus_random_generator_uint64_range_get
*/
extern NEXUS_NODISCARD NexusError *nexus_random_generator_int64_range_get(NexusRandomGenerator *generator, int64 minimum, int64 maximum,
                                                                          int64 *out_value);

/*
Summary: draws one uniform value in [0, 1) from generator.
Description: uses the upper 24 bits of a uint32 draw, scaled by 2^-24.
  The fixed grid includes zero and excludes one; requires uniform input.
Params: out_value  out; assigned only on success
Ownership: generator and output storage are borrowed, never retained.
Errors: NULL generator, NULL output asserts.
  Invalid algorithms assert; returns a random tape/stream exhaustion chain.
Thread-safety: synchronize access to shared mutable source state.
Complexity: O(1) for built-in generators; inherits source costs otherwise.
*/
extern NEXUS_NODISCARD NexusError *nexus_random_generator_real32_get(NexusRandomGenerator *generator, real32 *out_value);

/*
Summary: draws one uniform value in [0, 1) from generator.
Description: uses the upper 53 bits of a uint64 draw, scaled by 2^-53.
  The fixed grid includes zero and excludes one; requires uniform input.
Params: out_value  out; assigned only on success
Ownership: generator and output storage are borrowed, never retained.
Errors: NULL generator, NULL output asserts.
  Invalid algorithms assert; returns a random tape/stream exhaustion chain.
Thread-safety: synchronize access to shared mutable source state.
Complexity: O(1) for built-in generators; inherits source costs otherwise.
*/
extern NEXUS_NODISCARD NexusError *nexus_random_generator_real64_get(NexusRandomGenerator *generator, real64 *out_value);

/* ============================================================================ */
/* ---------------------------------------------------------------------------- */
/* HASHING                                                                      */
/* ---------------------------------------------------------------------------- */
/* ============================================================================ */

/*
Summary: returns the standard FNV-1a 32-bit offset basis.
*/
extern uint32 nexus_hash_fnv1a32_begin(void);

/*
Summary: continues a 32-bit FNV-1a hash over byte_count bytes.
Params: data  borrowed bytes; may be NULL only when byte_count is zero
Returns: the updated FNV-1a state.
Errors: NULL data with nonzero byte_count is a programmer error (asserts).
*/
extern uint32 nexus_hash_fnv1a32_update(uint32 hash, const void *data, uint_large byte_count);

/*
Summary: computes standard 32-bit FNV-1a over byte_count bytes.
Params: data  borrowed bytes; may be NULL only when byte_count is zero
Returns: the FNV-1a digest.
Errors: NULL data with nonzero byte_count is a programmer error (asserts).
*/
extern uint32 nexus_hash_fnv1a32(const void *data, uint_large byte_count);

/*
Summary: returns the standard FNV-1a 64-bit offset basis.
*/
extern uint64 nexus_hash_fnv1a64_begin(void);

/*
Summary: continues a 64-bit FNV-1a hash over byte_count bytes.
Params: data  borrowed bytes; may be NULL only when byte_count is zero
Returns: the updated FNV-1a state.
Errors: NULL data with nonzero byte_count is a programmer error (asserts).
*/
extern uint64 nexus_hash_fnv1a64_update(uint64 hash, const void *data, uint_large byte_count);

/*
Summary: computes standard 64-bit FNV-1a over byte_count bytes.
Params: data  borrowed bytes; may be NULL only when byte_count is zero
Returns: the FNV-1a digest.
Errors: NULL data with nonzero byte_count is a programmer error (asserts).
*/
extern uint64 nexus_hash_fnv1a64(const void *data, uint_large byte_count);

/*
Summary: returns the standard FNV-1a 128-bit offset basis.
*/
extern uint128 nexus_hash_fnv1a128_begin(void);

/*
Summary: continues a 128-bit FNV-1a hash over byte_count bytes.
Params: data  borrowed bytes; may be NULL only when byte_count is zero
Returns: the updated FNV-1a state.
Errors: NULL data with nonzero byte_count is a programmer error (asserts).
*/
extern uint128 nexus_hash_fnv1a128_update(uint128 hash, const void *data, uint_large byte_count);

/*
Summary: computes standard 128-bit FNV-1a over byte_count bytes.
Params: data  borrowed bytes; may be NULL only when byte_count is zero
Returns: the FNV-1a digest.
Errors: NULL data with nonzero byte_count is a programmer error (asserts).
*/
extern uint128 nexus_hash_fnv1a128(const void *data, uint_large byte_count);

/*
Summary: computes SipHash-2-4 with a 128-bit secret key.
Description: use for short, attacker-controlled HashMap keys when hash-flood
  resistance matters. It is a keyed PRF, not a general password hash.
Params: data  borrowed bytes; may be NULL only when byte_count is zero
Returns: the keyed 64-bit digest.
Errors: NULL data with nonzero byte_count is a programmer error (asserts).
*/
extern uint64 nexus_hash_siphash24(const void *data, uint_large byte_count, uint128 key);

/*
Summary: computes XXH3's 64-bit hash with seed zero.
Description: use for fast non-cryptographic content hashing. XXH3 is wrapped
  here so no client depends on xxHash headers or ABI.
Params: data  borrowed bytes; may be NULL only when byte_count is zero
Returns: the 64-bit digest.
Errors: NULL data with nonzero byte_count is a programmer error (asserts).
*/
extern uint64 nexus_hash_xxh3_64(const void *data, uint_large byte_count);

/*
Summary: computes XXH3's 64-bit seeded hash.
Description: seed selects a deterministic hash family member; it does not turn
  XXH3 into a cryptographic PRF.
Params: data  borrowed bytes; may be NULL only when byte_count is zero
Returns: the seeded 64-bit digest.
Errors: NULL data with nonzero byte_count is a programmer error (asserts).
*/
extern uint64 nexus_hash_xxh3_64_seeded(const void *data, uint_large byte_count, uint64 seed);

/*
Summary: computes XXH3's 128-bit hash with seed zero.
Params: data  borrowed bytes; may be NULL only when byte_count is zero
Returns: the 128-bit digest.
Errors: NULL data with nonzero byte_count is a programmer error (asserts).
*/
extern uint128 nexus_hash_xxh3_128(const void *data, uint_large byte_count);

/*
Summary: computes XXH3's 128-bit seeded hash.
Params: data  borrowed bytes; may be NULL only when byte_count is zero
Returns: the seeded 128-bit digest.
Errors: NULL data with nonzero byte_count is a programmer error (asserts).
*/
extern uint128 nexus_hash_xxh3_128_seeded(const void *data, uint_large byte_count, uint64 seed);

/* ============================================================================ */
/* ---------------------------------------------------------------------------- */
/* IDENTITY                                                                     */
/* ---------------------------------------------------------------------------- */
/* ============================================================================ */

#define NEXUS_UUID_STRING_LENGTH      36
#define NEXUS_UUID_STRING_BUFFER_SIZE 37

/*
Summary: an RFC 9562 UUID stored as Nexus's uint128 value.
Description:
  Numeric order matches the RFC network-byte-order representation. Serialize
  through nexus_identity_uuid_bytes_write or nexus_identity_uuid_format when
  crossing a process or storage boundary.
*/
typedef uint128 NexusUUID;

/*
Summary: identity subsystem errors.
*/
#define NEXUS_ERROR_IDENTITY_FACILITY NEXUS_PROGRAM_ERROR_FACILITY_IDENTIFIER_CREATE('I', 'D', 'E', 'N')

typedef enum NexusIdentityErrorCategory {
  NIEC_FORMAT,
  NIEC_TIME,

  NIEC_COUNT
} NexusIdentityErrorCategory;

#define NEXUS_IDENTITY_ERROR_LIST(X, library, facility)                                                                                              \
  X(NEXUS_IDENTITY_ERROR_INVALID_UUID, NIEC_FORMAT, 1, library, facility)                                                                            \
  X(NEXUS_IDENTITY_ERROR_TIMESTAMP_RANGE, NIEC_TIME, 1, library, facility)                                                                           \
  X(NEXUS_IDENTITY_ERROR_SEQUENCE_EXHAUSTED, NIEC_TIME, 2, library, facility)

NEXUS_ERROR_FACILITY_DEFINE(NexusIdentityError, NEXUS_ERROR_LIBRARY_IDENTIFIER, NEXUS_ERROR_IDENTITY_FACILITY, NEXUS_IDENTITY_ERROR_LIST)

/*
Summary: returns the all-zero Nil UUID.
Returns: NexusUUID with every bit clear.
*/
extern NexusUUID nexus_identity_uuid_nil(void);

/*
Summary: returns the all-one Max UUID.
Returns: NexusUUID with every bit set.
*/
extern NexusUUID nexus_identity_uuid_max(void);

/*
Summary: writes uuid as 16 network-byte-order bytes.
Params: out_bytes  out; exactly 16 bytes are written
Errors: NULL out_bytes is a programmer error (asserts).
*/
extern void nexus_identity_uuid_bytes_write(NexusUUID uuid, byte out_bytes[16]);

/*
Summary: reads a UUID from 16 network-byte-order bytes.
Params: bytes  borrowed; exactly 16 bytes are read
Returns: the UUID represented by the bytes.
Errors: NULL bytes is a programmer error (asserts).
*/
extern NexusUUID nexus_identity_uuid_bytes_read(const byte bytes[16]);

/*
Summary: writes the lowercase canonical 8-4-4-4-12 UUID representation.
Params: out_string  out; must have NEXUS_UUID_STRING_BUFFER_SIZE bytes
Errors: NULL out_string is a programmer error (asserts).
*/
extern void nexus_identity_uuid_format(NexusUUID uuid, char out_string[NEXUS_UUID_STRING_BUFFER_SIZE]);

/*
Summary: parses one canonical 8-4-4-4-12 UUID representation.
Errors:
  NULL string/out_uuid is a programmer error. Returns
  NEXUS_IDENTITY_ERROR_INVALID_UUID for malformed text.
*/
extern NEXUS_NODISCARD NexusError *nexus_identity_uuid_parse(const char *string, NexusUUID *out_uuid);

/*
Summary: returns uuid's RFC version nibble, or 0 for Nil/reserved values.
Returns: the four-bit version field.
*/
extern uint8 nexus_identity_uuid_version_get(NexusUUID uuid);

/*
Summary: generates an RFC 9562 UUIDv1 from wall time and process-local state.
Description:
  UUIDv1 exists for legacy time-based interoperability. It exposes creation
  time and a pseudorandom node identifier; prefer UUIDv7 for new sortable
  identifiers and UUIDv4 when time disclosure is undesirable.
Errors: NULL out_uuid is a programmer error (asserts). Returns an entropy error during first process initialization, or
  NEXUS_IDENTITY_ERROR_TIMESTAMP_RANGE if the Gregorian timestamp overflows.
Thread-safety: safe to call concurrently; process-local state is locked.
*/
extern NEXUS_NODISCARD NexusError *nexus_identity_uuid_v1_generate(NexusUUID *out_uuid);

/*
Summary: deterministically builds UUIDv1 from its RFC fields.
Description: use for protocol interop and test vectors, not as a uniqueness
  service. timestamp is 100-nanosecond ticks since 1582-10-15.
Params:
  timestamp       60-bit Gregorian timestamp in 100-nanosecond ticks
  clock_sequence  14-bit clock sequence
  node            48-bit node identifier
  out_uuid        out; receives the encoded UUID
Errors: NULL out_uuid or out-of-range clock_sequence/node is a programmer error (asserts).
*/
extern void nexus_identity_uuid_v1_build(uint64 timestamp, uint16 clock_sequence, uint64 node, NexusUUID *out_uuid);

/*
Summary: generates legacy name-based UUIDv3 using MD5.
Description: deterministic for canonical name bytes and namespace. Use only
  where UUIDv3 interoperability is required; prefer UUIDv5 otherwise.
Params:
  name      borrowed name bytes; may be NULL only when name_size is zero
  out_uuid  out; receives the generated UUID
Errors: NULL name with nonzero name_size or NULL out_uuid is a programmer error (asserts).
*/
extern void nexus_identity_uuid_v3_generate(NexusUUID namespace_uuid, const void *name, uint_large name_size, NexusUUID *out_uuid);

/*
Summary: generates random RFC 9562 UUIDv4 from the secure system random source.
Description: use for general opaque identifiers and security-sensitive contexts
  where embedded time/order is undesirable.
Errors: NULL out_uuid is a programmer error. Returns an operating-system entropy error.
Thread-safety: safe to call concurrently.
*/
extern NEXUS_NODISCARD NexusError *nexus_identity_uuid_v4_generate(NexusUUID *out_uuid);

/*
Summary: generates UUIDv4 from an injected random source.
Description: chiefly for deterministic tests and controlled random-source ownership.
Errors: NULL out_uuid or source.fill is a programmer error. Returns the source error.
*/
extern NEXUS_NODISCARD NexusError *nexus_identity_uuid_v4_generate_with_random(NexusRandomSource source, NexusUUID *out_uuid);

/*
Summary: generates name-based RFC 9562 UUIDv5 using SHA-1.
Description: use when the same canonical name and namespace must always produce
  the same UUID. Canonicalization of the name itself belongs to the caller.
Params:
  name      borrowed name bytes; may be NULL only when name_size is zero
  out_uuid  out; receives the generated UUID
Errors: NULL name with nonzero name_size or NULL out_uuid is a programmer error (asserts).
*/
extern void nexus_identity_uuid_v5_generate(NexusUUID namespace_uuid, const void *name, uint_large name_size, NexusUUID *out_uuid);

/*
Summary: generates an RFC 9562 UUIDv6 from wall time and process-local state.
Description: UUIDv6 reorders UUIDv1 for database locality. Use only when a
  UUIDv1-compatible field model is required; prefer UUIDv7 for new systems.
Errors: same as nexus_identity_uuid_v1_generate.
Thread-safety: safe to call concurrently; process-local state is locked.
*/
extern NEXUS_NODISCARD NexusError *nexus_identity_uuid_v6_generate(NexusUUID *out_uuid);

/*
Summary: deterministically builds UUIDv6 from its RFC fields.
Description: use for protocol interop and test vectors, not as a uniqueness service.
Params: fields have the same timestamp, clock_sequence, node contracts as UUIDv1.
Errors: NULL out_uuid or out-of-range clock_sequence/node is a programmer error (asserts).
*/
extern void nexus_identity_uuid_v6_build(uint64 timestamp, uint16 clock_sequence, uint64 node, NexusUUID *out_uuid);

/*
Summary: generates a process-monotonic RFC 9562 UUIDv7.
Description:
  Use as the default sortable UUID. The 48-bit Unix-millisecond prefix orders
  values by creation time; a 74-bit random field is incremented during equal or
  regressed clock ticks. State is process-local and reset safely after fork.
Errors: NULL out_uuid is a programmer error (asserts). Returns entropy or timestamp errors.
Thread-safety: safe to call concurrently; process-local state is locked.
*/
extern NEXUS_NODISCARD NexusError *nexus_identity_uuid_v7_generate(NexusUUID *out_uuid);

/*
Summary: generates a stateless random-field UUIDv7.
Description: timestamp order is preserved, but UUIDs produced in the same
  millisecond are not guaranteed to follow generation order.
Errors: NULL out_uuid is a programmer error (asserts). Returns entropy or timestamp errors.
Thread-safety: safe to call concurrently.
*/
extern NEXUS_NODISCARD NexusError *nexus_identity_uuid_v7_generate_random(NexusUUID *out_uuid);

/*
Summary: generates stateless UUIDv7 with an injected random source.
Errors: NULL out_uuid or source.fill is a programmer error. Returns the source
  error or NEXUS_IDENTITY_ERROR_TIMESTAMP_RANGE.
*/
extern NEXUS_NODISCARD NexusError *nexus_identity_uuid_v7_generate_with_random(NexusRandomSource source, NexusUUID *out_uuid);

/*
Summary: builds UUIDv8 from 16 application-defined bytes and fixes version/variant bits.
Description:
  UUIDv8 is a container for a documented application-specific layout, not a
  uniqueness algorithm. Callers own collision, privacy, and ordering semantics.
Params: custom_bytes  borrowed 16-byte application payload
Returns: a UUIDv8 containing the payload with RFC version and variant bits set.
Errors: NULL custom_bytes is a programmer error (asserts).
*/
extern NexusUUID nexus_identity_uuid_v8_build(const byte custom_bytes[16]);

/* ============================================================================ */
/* ---------------------------------------------------------------------------- */
/* BITS                                                                         */
/* ---------------------------------------------------------------------------- */
/* ============================================================================ */

/*
Endian-safe encoders and decoders for packed binary data.

LSB functions treat bytes[0] as the least significant byte (little-endian wire order).
MSB functions treat bytes[0] as the most significant byte (big-endian wire order).

Integer signed variants cast two's complement bit patterns directly.
Floating-point variants reinterpret IEEE-754 bit patterns via union conversion.

out and bytes buffers must not be NULL. Each function writes or reads exactly
sizeof(type) bytes.
*/

/* --- Bit Reinterpretation Helpers --- */

static NEXUS_MAYBE_UNUSED uint32 nexus_bits_uint32_from_real32(real32 value) /* NOLINT */ {
  union {
    real32 value;
    uint32 bit_pattern;
  } converter;

  converter.value = value;
  return converter.bit_pattern;
}

static NEXUS_MAYBE_UNUSED real32 nexus_bits_real32_from_uint32(uint32 bits) /* NOLINT */ {
  union {
    uint32 bit_pattern;
    real32 value;
  } converter;

  converter.bit_pattern = bits;
  return converter.value;
}

static NEXUS_MAYBE_UNUSED uint64 nexus_bits_uint64_from_real64(real64 value) /* NOLINT */ {
  union {
    real64 value;
    uint64 bit_pattern;
  } converter;

  converter.value = value;
  return converter.bit_pattern;
}

static NEXUS_MAYBE_UNUSED real64 nexus_bits_real64_from_uint64(uint64 bits) /* NOLINT */ {
  union {
    uint64 bit_pattern;
    real64 value;
  } converter;

  converter.bit_pattern = bits;
  return converter.value;
}

static NEXUS_MAYBE_UNUSED uint32 nexus_bits_uint32_from_f_real(f_real value) /* NOLINT */ {
#if NEXUS_FLOAT_DOUBLE_PRECISION
  return (uint32)nexus_bits_uint64_from_real64((real64)value);
#else
  return nexus_bits_uint32_from_real32((real32)value);
#endif
}

static NEXUS_MAYBE_UNUSED uint64 nexus_bits_uint64_from_f_real(f_real value) /* NOLINT */ {
#if NEXUS_FLOAT_DOUBLE_PRECISION
  return nexus_bits_uint64_from_real64((real64)value);
#else
  return (uint64)nexus_bits_uint32_from_real32((real32)value);
#endif
}

static NEXUS_MAYBE_UNUSED f_real nexus_bits_f_real_from_uint32(uint32 bits) /* NOLINT */ {
#if NEXUS_FLOAT_DOUBLE_PRECISION
  return (f_real)nexus_bits_real64_from_uint64((uint64)bits);
#else
  return (f_real)nexus_bits_real32_from_uint32(bits);
#endif
}

static NEXUS_MAYBE_UNUSED f_real nexus_bits_f_real_from_uint64(uint64 bits) /* NOLINT */ {
#if NEXUS_FLOAT_DOUBLE_PRECISION
  return (f_real)nexus_bits_real64_from_uint64(bits);
#else
  return (f_real)nexus_bits_real32_from_uint32((uint32)bits);
#endif
}

/* --- Little-Endian (LSB) Encoders (Packing) --- */

static NEXUS_MAYBE_UNUSED void nexus_bits_uint16_to_bytes_lsb(byte *out, uint16 value) /* NOLINT */ {
  NEXUS_PROGRAM_PRECONDITION(out != NULL, "bits: out != NULL");
  out[0] = (byte)(value & 0xFFu);
  out[1] = (byte)((value >> 8) & 0xFFu);
}

static NEXUS_MAYBE_UNUSED void nexus_bits_uint32_to_bytes_lsb(byte *out, uint32 value) /* NOLINT */ {
  NEXUS_PROGRAM_PRECONDITION(out != NULL, "bits: out != NULL");
  out[0] = (byte)(value & 0xFFu);
  out[1] = (byte)((value >> 8) & 0xFFu);
  out[2] = (byte)((value >> 16) & 0xFFu);
  out[3] = (byte)((value >> 24) & 0xFFu);
}

static NEXUS_MAYBE_UNUSED void nexus_bits_uint64_to_bytes_lsb(byte *out, uint64 value) /* NOLINT */ {
  NEXUS_PROGRAM_PRECONDITION(out != NULL, "bits: out != NULL");
  out[0] = (byte)(value & 0xFFu);
  out[1] = (byte)((value >> 8) & 0xFFu);
  out[2] = (byte)((value >> 16) & 0xFFu);
  out[3] = (byte)((value >> 24) & 0xFFu);
  out[4] = (byte)((value >> 32) & 0xFFu);
  out[5] = (byte)((value >> 40) & 0xFFu);
  out[6] = (byte)((value >> 48) & 0xFFu);
  out[7] = (byte)((value >> 56) & 0xFFu);
}

static NEXUS_MAYBE_UNUSED void nexus_bits_int16_to_bytes_lsb(byte *out, int16 value) /* NOLINT */ {
  nexus_bits_uint16_to_bytes_lsb(out, (uint16)value);
}

static NEXUS_MAYBE_UNUSED void nexus_bits_int32_to_bytes_lsb(byte *out, int32 value) /* NOLINT */ {
  nexus_bits_uint32_to_bytes_lsb(out, (uint32)value);
}

static NEXUS_MAYBE_UNUSED void nexus_bits_int64_to_bytes_lsb(byte *out, int64 value) /* NOLINT */ {
  nexus_bits_uint64_to_bytes_lsb(out, (uint64)value);
}

static NEXUS_MAYBE_UNUSED void nexus_bits_real32_to_bytes_lsb(byte *out, real32 value) /* NOLINT */ {
  nexus_bits_uint32_to_bytes_lsb(out, nexus_bits_uint32_from_real32(value));
}

static NEXUS_MAYBE_UNUSED void nexus_bits_real64_to_bytes_lsb(byte *out, real64 value) /* NOLINT */ {
  nexus_bits_uint64_to_bytes_lsb(out, nexus_bits_uint64_from_real64(value));
}

/* --- Little-Endian (LSB) Decoders (Unpacking) --- */

static NEXUS_MAYBE_UNUSED uint16 nexus_bits_uint16_from_bytes_lsb(const byte *bytes) /* NOLINT */ {
  NEXUS_PROGRAM_PRECONDITION(bytes != NULL, "bits: bytes != NULL");
  return (uint16)((uint16)bytes[0] | ((uint16)bytes[1] << 8));
}

static NEXUS_MAYBE_UNUSED uint32 nexus_bits_uint32_from_bytes_lsb(const byte *bytes) /* NOLINT */ {
  NEXUS_PROGRAM_PRECONDITION(bytes != NULL, "bits: bytes != NULL");
  return (uint32)bytes[0] | ((uint32)bytes[1] << 8) | ((uint32)bytes[2] << 16) | ((uint32)bytes[3] << 24);
}

static NEXUS_MAYBE_UNUSED uint64 nexus_bits_uint64_from_bytes_lsb(const byte *bytes) /* NOLINT */ {
  NEXUS_PROGRAM_PRECONDITION(bytes != NULL, "bits: bytes != NULL");
  return (uint64)bytes[0] | ((uint64)bytes[1] << 8) | ((uint64)bytes[2] << 16) | ((uint64)bytes[3] << 24) | ((uint64)bytes[4] << 32) |
         ((uint64)bytes[5] << 40) | ((uint64)bytes[6] << 48) | ((uint64)bytes[7] << 56);
}

static NEXUS_MAYBE_UNUSED int16 nexus_bits_int16_from_bytes_lsb(const byte *bytes) /* NOLINT */ {
  return (int16)nexus_bits_uint16_from_bytes_lsb(bytes);
}

static NEXUS_MAYBE_UNUSED int32 nexus_bits_int32_from_bytes_lsb(const byte *bytes) /* NOLINT */ {
  return (int32)nexus_bits_uint32_from_bytes_lsb(bytes);
}

static NEXUS_MAYBE_UNUSED int64 nexus_bits_int64_from_bytes_lsb(const byte *bytes) /* NOLINT */ {
  return (int64)nexus_bits_uint64_from_bytes_lsb(bytes);
}

static NEXUS_MAYBE_UNUSED real32 nexus_bits_real32_from_bytes_lsb(const byte *bytes) /* NOLINT */ {
  return nexus_bits_real32_from_uint32(nexus_bits_uint32_from_bytes_lsb(bytes));
}

static NEXUS_MAYBE_UNUSED real64 nexus_bits_real64_from_bytes_lsb(const byte *bytes) /* NOLINT */ {
  return nexus_bits_real64_from_uint64(nexus_bits_uint64_from_bytes_lsb(bytes));
}

/* --- Big-Endian (MSB) Encoders (Packing) --- */

static NEXUS_MAYBE_UNUSED void nexus_bits_uint16_to_bytes_msb(byte *out, uint16 value) /* NOLINT */ {
  NEXUS_PROGRAM_PRECONDITION(out != NULL, "bits: out != NULL");
  out[0] = (byte)((value >> 8) & 0xFFu);
  out[1] = (byte)(value & 0xFFu);
}

static NEXUS_MAYBE_UNUSED void nexus_bits_uint32_to_bytes_msb(byte *out, uint32 value) /* NOLINT */ {
  NEXUS_PROGRAM_PRECONDITION(out != NULL, "bits: out != NULL");
  out[0] = (byte)((value >> 24) & 0xFFu);
  out[1] = (byte)((value >> 16) & 0xFFu);
  out[2] = (byte)((value >> 8) & 0xFFu);
  out[3] = (byte)(value & 0xFFu);
}

static NEXUS_MAYBE_UNUSED void nexus_bits_uint64_to_bytes_msb(byte *out, uint64 value) /* NOLINT */ {
  NEXUS_PROGRAM_PRECONDITION(out != NULL, "bits: out != NULL");
  out[0] = (byte)((value >> 56) & 0xFFu);
  out[1] = (byte)((value >> 48) & 0xFFu);
  out[2] = (byte)((value >> 40) & 0xFFu);
  out[3] = (byte)((value >> 32) & 0xFFu);
  out[4] = (byte)((value >> 24) & 0xFFu);
  out[5] = (byte)((value >> 16) & 0xFFu);
  out[6] = (byte)((value >> 8) & 0xFFu);
  out[7] = (byte)(value & 0xFFu);
}

static NEXUS_MAYBE_UNUSED void nexus_bits_int16_to_bytes_msb(byte *out, int16 value) /* NOLINT */ {
  nexus_bits_uint16_to_bytes_msb(out, (uint16)value);
}

static NEXUS_MAYBE_UNUSED void nexus_bits_int32_to_bytes_msb(byte *out, int32 value) /* NOLINT */ {
  nexus_bits_uint32_to_bytes_msb(out, (uint32)value);
}

static NEXUS_MAYBE_UNUSED void nexus_bits_int64_to_bytes_msb(byte *out, int64 value) /* NOLINT */ {
  nexus_bits_uint64_to_bytes_msb(out, (uint64)value);
}

static NEXUS_MAYBE_UNUSED void nexus_bits_real32_to_bytes_msb(byte *out, real32 value) /* NOLINT */ {
  nexus_bits_uint32_to_bytes_msb(out, nexus_bits_uint32_from_real32(value));
}

static NEXUS_MAYBE_UNUSED void nexus_bits_real64_to_bytes_msb(byte *out, real64 value) /* NOLINT */ {
  nexus_bits_uint64_to_bytes_msb(out, nexus_bits_uint64_from_real64(value));
}

/* --- Big-Endian (MSB) Decoders (Unpacking) --- */

static NEXUS_MAYBE_UNUSED uint16 nexus_bits_uint16_from_bytes_msb(const byte *bytes) /* NOLINT */ {
  NEXUS_PROGRAM_PRECONDITION(bytes != NULL, "bits: bytes != NULL");
  return (uint16)(((uint16)bytes[0] << 8) | (uint16)bytes[1]);
}

static NEXUS_MAYBE_UNUSED uint32 nexus_bits_uint32_from_bytes_msb(const byte *bytes) /* NOLINT */ {
  NEXUS_PROGRAM_PRECONDITION(bytes != NULL, "bits: bytes != NULL");
  return ((uint32)bytes[0] << 24) | ((uint32)bytes[1] << 16) | ((uint32)bytes[2] << 8) | (uint32)bytes[3];
}

static NEXUS_MAYBE_UNUSED uint64 nexus_bits_uint64_from_bytes_msb(const byte *bytes) /* NOLINT */ {
  NEXUS_PROGRAM_PRECONDITION(bytes != NULL, "bits: bytes != NULL");
  return ((uint64)bytes[0] << 56) | ((uint64)bytes[1] << 48) | ((uint64)bytes[2] << 40) | ((uint64)bytes[3] << 32) | ((uint64)bytes[4] << 24) |
         ((uint64)bytes[5] << 16) | ((uint64)bytes[6] << 8) | (uint64)bytes[7];
}

static NEXUS_MAYBE_UNUSED int16 nexus_bits_int16_from_bytes_msb(const byte *bytes) /* NOLINT */ {
  return (int16)nexus_bits_uint16_from_bytes_msb(bytes);
}

static NEXUS_MAYBE_UNUSED int32 nexus_bits_int32_from_bytes_msb(const byte *bytes) /* NOLINT */ {
  return (int32)nexus_bits_uint32_from_bytes_msb(bytes);
}

static NEXUS_MAYBE_UNUSED int64 nexus_bits_int64_from_bytes_msb(const byte *bytes) /* NOLINT */ {
  return (int64)nexus_bits_uint64_from_bytes_msb(bytes);
}

static NEXUS_MAYBE_UNUSED real32 nexus_bits_real32_from_bytes_msb(const byte *bytes) /* NOLINT */ {
  return nexus_bits_real32_from_uint32(nexus_bits_uint32_from_bytes_msb(bytes));
}

static NEXUS_MAYBE_UNUSED real64 nexus_bits_real64_from_bytes_msb(const byte *bytes) /* NOLINT */ {
  return nexus_bits_real64_from_uint64(nexus_bits_uint64_from_bytes_msb(bytes));
}

/* --- Bitfield Manipulation Operations --- */

/*
Summary: read a uint8 bit; index 0 is the LSB.
*/
static NEXUS_MAYBE_UNUSED boolean nexus_bits_uint8_get_lsb(uint8 value, uint32 bit_index) /* NOLINT */ {
  NEXUS_PROGRAM_PRECONDITION(bit_index < 8u, "bit_index out of range for uint8.");
  return (boolean)((value >> bit_index) & 1u);
}

/*
Summary: set or clear a uint8 bit; index 0 is the LSB.
*/
static NEXUS_MAYBE_UNUSED uint8 nexus_bits_uint8_set_lsb(uint8 value, uint32 bit_index, boolean bit_value) /* NOLINT */ {
  uint8 mask;

  NEXUS_PROGRAM_PRECONDITION(bit_index < 8u, "bit_index out of range for uint8.");
  mask = (uint8)(1u << bit_index);

  if (bit_value) {
    return (uint8)(value | mask);
  }

  return (uint8)(value & (uint8)~mask);
}

/*
Summary: read a uint16 bit; index 0 is the LSB.
*/
static NEXUS_MAYBE_UNUSED boolean nexus_bits_uint16_get_lsb(uint16 value, uint32 bit_index) /* NOLINT */ {
  NEXUS_PROGRAM_PRECONDITION(bit_index < 16u, "bit_index out of range for uint16.");
  return (boolean)((value >> bit_index) & 1u);
}

/*
Summary: set or clear a uint16 bit; index 0 is the LSB.
*/
static NEXUS_MAYBE_UNUSED uint16 nexus_bits_uint16_set_lsb(uint16 value, uint32 bit_index, boolean bit_value) /* NOLINT */ {
  uint16 mask;

  NEXUS_PROGRAM_PRECONDITION(bit_index < 16u, "bit_index out of range for uint16.");
  mask = (uint16)(1u << bit_index);

  if (bit_value) {
    return (uint16)(value | mask);
  }

  return (uint16)(value & (uint16)~mask);
}

/*
Summary: read a uint32 bit; index 0 is the LSB.
*/
static NEXUS_MAYBE_UNUSED boolean nexus_bits_uint32_get_lsb(uint32 value, uint32 bit_index) /* NOLINT */ {
  NEXUS_PROGRAM_PRECONDITION(bit_index < 32u, "bit_index out of range for uint32.");
  return (boolean)((value >> bit_index) & 1u);
}

/*
Summary: set or clear a uint32 bit; index 0 is the LSB.
*/
static NEXUS_MAYBE_UNUSED uint32 nexus_bits_uint32_set_lsb(uint32 value, uint32 bit_index, boolean bit_value) /* NOLINT */ {
  uint32 mask;

  NEXUS_PROGRAM_PRECONDITION(bit_index < 32u, "bit_index out of range for uint32.");
  mask = (uint32)(1u << bit_index);

  if (bit_value) {
    return value | mask;
  }

  return value & ~mask;
}

/*
Summary: read a uint64 bit; index 0 is the LSB.
*/
static NEXUS_MAYBE_UNUSED boolean nexus_bits_uint64_get_lsb(uint64 value, uint32 bit_index) /* NOLINT */ {
  NEXUS_PROGRAM_PRECONDITION(bit_index < 64u, "bit_index out of range for uint64.");
  return (boolean)((value >> bit_index) & 1u);
}

/*
Summary: set or clear a uint64 bit; index 0 is the LSB.
*/
static NEXUS_MAYBE_UNUSED uint64 nexus_bits_uint64_set_lsb(uint64 value, uint32 bit_index, boolean bit_value) /* NOLINT */ {
  uint64 mask;

  NEXUS_PROGRAM_PRECONDITION(bit_index < 64u, "bit_index out of range for uint64.");
  mask = ((uint64)1) << bit_index;

  if (bit_value) {
    return value | mask;
  }

  return value & ~mask;
}

/*
Summary: read a uint_large bit; index 0 is the LSB.
*/
static NEXUS_MAYBE_UNUSED boolean nexus_bits_uint_large_get_lsb(uint_large value, uint32 bit_index) /* NOLINT */ {
  NEXUS_PROGRAM_PRECONDITION(bit_index < (uint32)(NEXUS_SIZEOF(uint_large) * 8u), "bit_index out of range for uint_large.");
  return (boolean)((value >> bit_index) & 1u);
}

/*
Summary: set or clear a uint_large bit; index 0 is the LSB.
*/
static NEXUS_MAYBE_UNUSED uint_large nexus_bits_uint_large_set_lsb(uint_large value, uint32 bit_index, boolean bit_value) /* NOLINT */ {
  uint_large mask;

  NEXUS_PROGRAM_PRECONDITION(bit_index < (uint32)(NEXUS_SIZEOF(uint_large) * 8u), "bit_index out of range for uint_large.");
  mask = ((uint_large)1) << bit_index;

  if (bit_value) {
    return value | mask;
  }

  return value & ~mask;
}

/*
Summary: read a uint8 bit; index 0 is the MSB.
*/
static NEXUS_MAYBE_UNUSED boolean nexus_bits_uint8_get_msb(uint8 value, uint32 bit_index) /* NOLINT */ {
  NEXUS_PROGRAM_PRECONDITION(bit_index < 8u, "bit_index out of range for uint8.");
  return (boolean)((value >> (7u - bit_index)) & 1u);
}

/*
Summary: set or clear a uint8 bit; index 0 is the MSB.
*/
static NEXUS_MAYBE_UNUSED uint8 nexus_bits_uint8_set_msb(uint8 value, uint32 bit_index, boolean bit_value) /* NOLINT */ {
  uint8 mask;

  NEXUS_PROGRAM_PRECONDITION(bit_index < 8u, "bit_index out of range for uint8.");
  mask = (uint8)(1u << (7u - bit_index));

  if (bit_value) {
    return (uint8)(value | mask);
  }

  return (uint8)(value & (uint8)~mask);
}

/*
Summary: read a uint16 bit; index 0 is the MSB.
*/
static NEXUS_MAYBE_UNUSED boolean nexus_bits_uint16_get_msb(uint16 value, uint32 bit_index) /* NOLINT */ {
  NEXUS_PROGRAM_PRECONDITION(bit_index < 16u, "bit_index out of range for uint16.");
  return (boolean)((value >> (15u - bit_index)) & 1u);
}

/*
Summary: set or clear a uint16 bit; index 0 is the MSB.
*/
static NEXUS_MAYBE_UNUSED uint16 nexus_bits_uint16_set_msb(uint16 value, uint32 bit_index, boolean bit_value) /* NOLINT */ {
  uint16 mask;

  NEXUS_PROGRAM_PRECONDITION(bit_index < 16u, "bit_index out of range for uint16.");
  mask = (uint16)(1u << (15u - bit_index));

  if (bit_value) {
    return (uint16)(value | mask);
  }

  return (uint16)(value & (uint16)~mask);
}

/*
Summary: read a uint32 bit; index 0 is the MSB.
*/
static NEXUS_MAYBE_UNUSED boolean nexus_bits_uint32_get_msb(uint32 value, uint32 bit_index) /* NOLINT */ {
  NEXUS_PROGRAM_PRECONDITION(bit_index < 32u, "bit_index out of range for uint32.");
  return (boolean)((value >> (31u - bit_index)) & 1u);
}

/*
Summary: set or clear a uint32 bit; index 0 is the MSB.
*/
static NEXUS_MAYBE_UNUSED uint32 nexus_bits_uint32_set_msb(uint32 value, uint32 bit_index, boolean bit_value) /* NOLINT */ {
  uint32 mask;

  NEXUS_PROGRAM_PRECONDITION(bit_index < 32u, "bit_index out of range for uint32.");
  mask = (uint32)(1u << (31u - bit_index));

  if (bit_value) {
    return value | mask;
  }

  return value & ~mask;
}

/*
Summary: read a uint64 bit; index 0 is the MSB.
*/
static NEXUS_MAYBE_UNUSED boolean nexus_bits_uint64_get_msb(uint64 value, uint32 bit_index) /* NOLINT */ {
  NEXUS_PROGRAM_PRECONDITION(bit_index < 64u, "bit_index out of range for uint64.");
  return (boolean)((value >> (63u - bit_index)) & 1u);
}

/*
Summary: set or clear a uint64 bit; index 0 is the MSB.
*/
static NEXUS_MAYBE_UNUSED uint64 nexus_bits_uint64_set_msb(uint64 value, uint32 bit_index, boolean bit_value) /* NOLINT */ {
  uint64 mask;

  NEXUS_PROGRAM_PRECONDITION(bit_index < 64u, "bit_index out of range for uint64.");
  mask = ((uint64)1) << (63u - bit_index);

  if (bit_value) {
    return value | mask;
  }

  return value & ~mask;
}

/*
Summary: read a uint_large bit; index 0 is the MSB.
*/
static NEXUS_MAYBE_UNUSED boolean nexus_bits_uint_large_get_msb(uint_large value, uint32 bit_index) /* NOLINT */ {
  NEXUS_PROGRAM_PRECONDITION(bit_index < (uint32)(NEXUS_SIZEOF(uint_large) * 8u), "bit_index out of range for uint_large.");
  return (boolean)((value >> (((uint32)(NEXUS_SIZEOF(uint_large) * 8u) - 1u) - bit_index)) & 1u);
}

/*
Summary: set or clear a uint_large bit; index 0 is the MSB.
*/
static NEXUS_MAYBE_UNUSED uint_large nexus_bits_uint_large_set_msb(uint_large value, uint32 bit_index, boolean bit_value) /* NOLINT */ {
  uint_large mask;

  NEXUS_PROGRAM_PRECONDITION(bit_index < (uint32)(NEXUS_SIZEOF(uint_large) * 8u), "bit_index out of range for uint_large.");
  mask = ((uint_large)1) << (((uint32)(NEXUS_SIZEOF(uint_large) * 8u) - 1u) - bit_index);

  if (bit_value) {
    return value | mask;
  }

  return value & ~mask;
}

/* ---------------------------------------------------------------------------- */
/* SUB-WORD PACKING & UNPACKING                                                 */
/* ---------------------------------------------------------------------------- */

/* --- Half Extraction Helpers (Architecture-Agnostic) --- */

static NEXUS_MAYBE_UNUSED uint8 nexus_bits_uint16_lo_u8(uint16 value) /* NOLINT */ {
  return (uint8)(value & 0xFFu);
}

static NEXUS_MAYBE_UNUSED uint8 nexus_bits_uint16_hi_u8(uint16 value) /* NOLINT */ {
  return (uint8)((value >> 8) & 0xFFu);
}

static NEXUS_MAYBE_UNUSED uint16 nexus_bits_uint32_lo_u16(uint32 value) /* NOLINT */ {
  return (uint16)(value & 0xFFFFu);
}

static NEXUS_MAYBE_UNUSED uint16 nexus_bits_uint32_hi_u16(uint32 value) /* NOLINT */ {
  return (uint16)((value >> 16) & 0xFFFFu);
}

static NEXUS_MAYBE_UNUSED uint32 nexus_bits_uint64_lo_u32(uint64 value) /* NOLINT */ {
  return (uint32)(value & 0xFFFFFFFFULL);
}

static NEXUS_MAYBE_UNUSED uint32 nexus_bits_uint64_hi_u32(uint64 value) /* NOLINT */ {
  return (uint32)((value >> 32) & 0xFFFFFFFFULL);
}

/* --- 16-Bit Operations (2x uint8) --- */

static NEXUS_MAYBE_UNUSED uint16 nexus_bits_uint16_pack_2u8_lsb(uint8 b0, uint8 b1) /* NOLINT */ {
  return (uint16)((uint16)b0 | ((uint16)b1 << 8));
}

static NEXUS_MAYBE_UNUSED uint16 nexus_bits_uint16_pack_2u8_msb(uint8 b0, uint8 b1) /* NOLINT */ {
  return (uint16)(((uint16)b0 << 8) | (uint16)b1);
}

static NEXUS_MAYBE_UNUSED void nexus_bits_uint16_unpack_2u8_lsb(uint16 value, uint8 *out_b0, uint8 *out_b1) /* NOLINT */ {
  NEXUS_PROGRAM_PRECONDITION(out_b0 != NULL, "bits: out_b0 != NULL");
  NEXUS_PROGRAM_PRECONDITION(out_b1 != NULL, "bits: out_b1 != NULL");

  *out_b0 = (uint8)(value & 0xFFu);
  *out_b1 = (uint8)((value >> 8) & 0xFFu);
}

static NEXUS_MAYBE_UNUSED void nexus_bits_uint16_unpack_2u8_msb(uint16 value, uint8 *out_b0, uint8 *out_b1) /* NOLINT */ {
  NEXUS_PROGRAM_PRECONDITION(out_b0 != NULL, "bits: out_b0 != NULL");
  NEXUS_PROGRAM_PRECONDITION(out_b1 != NULL, "bits: out_b1 != NULL");

  *out_b0 = (uint8)((value >> 8) & 0xFFu);
  *out_b1 = (uint8)(value & 0xFFu);
}

/* --- 32-Bit Operations (2x uint16, 4x uint8) --- */

static NEXUS_MAYBE_UNUSED uint32 nexus_bits_uint32_pack_2u16_lsb(uint16 w0, uint16 w1) /* NOLINT */ {
  return (uint32)w0 | ((uint32)w1 << 16);
}

static NEXUS_MAYBE_UNUSED uint32 nexus_bits_uint32_pack_2u16_msb(uint16 w0, uint16 w1) /* NOLINT */ {
  return ((uint32)w0 << 16) | (uint32)w1;
}

static NEXUS_MAYBE_UNUSED void nexus_bits_uint32_unpack_2u16_lsb(uint32 value, uint16 *out_w0, uint16 *out_w1) /* NOLINT */ {
  NEXUS_PROGRAM_PRECONDITION(out_w0 != NULL, "bits: out_w0 != NULL");
  NEXUS_PROGRAM_PRECONDITION(out_w1 != NULL, "bits: out_w1 != NULL");

  *out_w0 = (uint16)(value & 0xFFFFu);
  *out_w1 = (uint16)((value >> 16) & 0xFFFFu);
}

static NEXUS_MAYBE_UNUSED void nexus_bits_uint32_unpack_2u16_msb(uint32 value, uint16 *out_w0, uint16 *out_w1) /* NOLINT */ {
  NEXUS_PROGRAM_PRECONDITION(out_w0 != NULL, "bits: out_w0 != NULL");
  NEXUS_PROGRAM_PRECONDITION(out_w1 != NULL, "bits: out_w1 != NULL");

  *out_w0 = (uint16)((value >> 16) & 0xFFFFu);
  *out_w1 = (uint16)(value & 0xFFFFu);
}

static NEXUS_MAYBE_UNUSED uint32 nexus_bits_uint32_pack_4u8_lsb(uint8 b0, uint8 b1, uint8 b2, uint8 b3) /* NOLINT */ {
  return (uint32)b0 | ((uint32)b1 << 8) | ((uint32)b2 << 16) | ((uint32)b3 << 24);
}

static NEXUS_MAYBE_UNUSED uint32 nexus_bits_uint32_pack_4u8_msb(uint8 b0, uint8 b1, uint8 b2, uint8 b3) /* NOLINT */ {
  return ((uint32)b0 << 24) | ((uint32)b1 << 16) | ((uint32)b2 << 8) | (uint32)b3;
}

static NEXUS_MAYBE_UNUSED void nexus_bits_uint32_unpack_4u8_lsb(uint32 value, uint8 *out_b0, uint8 *out_b1, uint8 *out_b2,
                                                                uint8 *out_b3) /* NOLINT */ {
  NEXUS_PROGRAM_PRECONDITION(out_b0 != NULL, "bits: out_b0 != NULL");
  NEXUS_PROGRAM_PRECONDITION(out_b1 != NULL, "bits: out_b1 != NULL");
  NEXUS_PROGRAM_PRECONDITION(out_b2 != NULL, "bits: out_b2 != NULL");
  NEXUS_PROGRAM_PRECONDITION(out_b3 != NULL, "bits: out_b3 != NULL");

  *out_b0 = (uint8)(value & 0xFFu);
  *out_b1 = (uint8)((value >> 8) & 0xFFu);
  *out_b2 = (uint8)((value >> 16) & 0xFFu);
  *out_b3 = (uint8)((value >> 24) & 0xFFu);
}

static NEXUS_MAYBE_UNUSED void nexus_bits_uint32_unpack_4u8_msb(uint32 value, uint8 *out_b0, uint8 *out_b1, uint8 *out_b2,
                                                                uint8 *out_b3) /* NOLINT */ {
  NEXUS_PROGRAM_PRECONDITION(out_b0 != NULL, "bits: out_b0 != NULL");
  NEXUS_PROGRAM_PRECONDITION(out_b1 != NULL, "bits: out_b1 != NULL");
  NEXUS_PROGRAM_PRECONDITION(out_b2 != NULL, "bits: out_b2 != NULL");
  NEXUS_PROGRAM_PRECONDITION(out_b3 != NULL, "bits: out_b3 != NULL");

  *out_b0 = (uint8)((value >> 24) & 0xFFu);
  *out_b1 = (uint8)((value >> 16) & 0xFFu);
  *out_b2 = (uint8)((value >> 8) & 0xFFu);
  *out_b3 = (uint8)(value & 0xFFu);
}

/* --- 64-Bit Operations (2x uint32, 4x uint16, 8x uint8) --- */

static NEXUS_MAYBE_UNUSED uint64 nexus_bits_uint64_pack_2u32_lsb(uint32 d0, uint32 d1) /* NOLINT */ {
  return (uint64)d0 | ((uint64)d1 << 32);
}

static NEXUS_MAYBE_UNUSED uint64 nexus_bits_uint64_pack_2u32_msb(uint32 d0, uint32 d1) /* NOLINT */ {
  return ((uint64)d0 << 32) | (uint64)d1;
}

static NEXUS_MAYBE_UNUSED void nexus_bits_uint64_unpack_2u32_lsb(uint64 value, uint32 *out_d0, uint32 *out_d1) /* NOLINT */ {
  NEXUS_PROGRAM_PRECONDITION(out_d0 != NULL, "bits: out_d0 != NULL");
  NEXUS_PROGRAM_PRECONDITION(out_d1 != NULL, "bits: out_d1 != NULL");

  *out_d0 = (uint32)(value & 0xFFFFFFFFULL);
  *out_d1 = (uint32)((value >> 32) & 0xFFFFFFFFULL);
}

static NEXUS_MAYBE_UNUSED void nexus_bits_uint64_unpack_2u32_msb(uint64 value, uint32 *out_d0, uint32 *out_d1) /* NOLINT */ {
  NEXUS_PROGRAM_PRECONDITION(out_d0 != NULL, "bits: out_d0 != NULL");
  NEXUS_PROGRAM_PRECONDITION(out_d1 != NULL, "bits: out_d1 != NULL");

  *out_d0 = (uint32)((value >> 32) & 0xFFFFFFFFULL);
  *out_d1 = (uint32)(value & 0xFFFFFFFFULL);
}

static NEXUS_MAYBE_UNUSED uint64 nexus_bits_uint64_pack_4u16_lsb(uint16 w0, uint16 w1, uint16 w2, uint16 w3) /* NOLINT */ {
  return (uint64)w0 | ((uint64)w1 << 16) | ((uint64)w2 << 32) | ((uint64)w3 << 48);
}

static NEXUS_MAYBE_UNUSED uint64 nexus_bits_uint64_pack_4u16_msb(uint16 w0, uint16 w1, uint16 w2, uint16 w3) /* NOLINT */ {
  return ((uint64)w0 << 48) | ((uint64)w1 << 32) | ((uint64)w2 << 16) | (uint64)w3;
}

static NEXUS_MAYBE_UNUSED void nexus_bits_uint64_unpack_4u16_lsb(uint64 value, uint16 *out_w0, uint16 *out_w1, uint16 *out_w2,
                                                                 uint16 *out_w3) /* NOLINT */ {
  NEXUS_PROGRAM_PRECONDITION(out_w0 != NULL, "bits: out_w0 != NULL");
  NEXUS_PROGRAM_PRECONDITION(out_w1 != NULL, "bits: out_w1 != NULL");
  NEXUS_PROGRAM_PRECONDITION(out_w2 != NULL, "bits: out_w2 != NULL");
  NEXUS_PROGRAM_PRECONDITION(out_w3 != NULL, "bits: out_w3 != NULL");

  *out_w0 = (uint16)(value & 0xFFFFu);
  *out_w1 = (uint16)((value >> 16) & 0xFFFFu);
  *out_w2 = (uint16)((value >> 32) & 0xFFFFu);
  *out_w3 = (uint16)((value >> 48) & 0xFFFFu);
}

static NEXUS_MAYBE_UNUSED void nexus_bits_uint64_unpack_4u16_msb(uint64 value, uint16 *out_w0, uint16 *out_w1, uint16 *out_w2,
                                                                 uint16 *out_w3) /* NOLINT */ {
  NEXUS_PROGRAM_PRECONDITION(out_w0 != NULL, "bits: out_w0 != NULL");
  NEXUS_PROGRAM_PRECONDITION(out_w1 != NULL, "bits: out_w1 != NULL");
  NEXUS_PROGRAM_PRECONDITION(out_w2 != NULL, "bits: out_w2 != NULL");
  NEXUS_PROGRAM_PRECONDITION(out_w3 != NULL, "bits: out_w3 != NULL");

  *out_w0 = (uint16)((value >> 48) & 0xFFFFu);
  *out_w1 = (uint16)((value >> 32) & 0xFFFFu);
  *out_w2 = (uint16)((value >> 16) & 0xFFFFu);
  *out_w3 = (uint16)(value & 0xFFFFu);
}

static NEXUS_MAYBE_UNUSED uint64 nexus_bits_uint64_pack_8u8_lsb(uint8 b0, uint8 b1, uint8 b2, uint8 b3, /* NOLINT */
                                                                uint8 b4, uint8 b5, uint8 b6, uint8 b7) /* NOLINT */ {
  return (uint64)b0 | ((uint64)b1 << 8) | ((uint64)b2 << 16) | ((uint64)b3 << 24) | ((uint64)b4 << 32) | ((uint64)b5 << 40) | ((uint64)b6 << 48) |
         ((uint64)b7 << 56);
}

static NEXUS_MAYBE_UNUSED uint64 nexus_bits_uint64_pack_8u8_msb(uint8 b0, uint8 b1, uint8 b2, uint8 b3, /* NOLINT */
                                                                uint8 b4, uint8 b5, uint8 b6, uint8 b7) /* NOLINT */ {
  return ((uint64)b0 << 56) | ((uint64)b1 << 48) | ((uint64)b2 << 40) | ((uint64)b3 << 32) | ((uint64)b4 << 24) | ((uint64)b5 << 16) |
         ((uint64)b6 << 8) | (uint64)b7;
}

static NEXUS_MAYBE_UNUSED void nexus_bits_uint64_unpack_8u8_lsb(uint64 value, uint8 *out_b0, uint8 *out_b1, uint8 *out_b2, uint8 *out_b3, /* NOLINT */
                                                                uint8 *out_b4, uint8 *out_b5, uint8 *out_b6, uint8 *out_b7) /* NOLINT */ {
  NEXUS_PROGRAM_PRECONDITION(out_b0 != NULL, "bits: out_b0 != NULL");
  NEXUS_PROGRAM_PRECONDITION(out_b1 != NULL, "bits: out_b1 != NULL");
  NEXUS_PROGRAM_PRECONDITION(out_b2 != NULL, "bits: out_b2 != NULL");
  NEXUS_PROGRAM_PRECONDITION(out_b3 != NULL, "bits: out_b3 != NULL");
  NEXUS_PROGRAM_PRECONDITION(out_b4 != NULL, "bits: out_b4 != NULL");
  NEXUS_PROGRAM_PRECONDITION(out_b5 != NULL, "bits: out_b5 != NULL");
  NEXUS_PROGRAM_PRECONDITION(out_b6 != NULL, "bits: out_b6 != NULL");
  NEXUS_PROGRAM_PRECONDITION(out_b7 != NULL, "bits: out_b7 != NULL");

  *out_b0 = (uint8)(value & 0xFFu);
  *out_b1 = (uint8)((value >> 8) & 0xFFu);
  *out_b2 = (uint8)((value >> 16) & 0xFFu);
  *out_b3 = (uint8)((value >> 24) & 0xFFu);
  *out_b4 = (uint8)((value >> 32) & 0xFFu);
  *out_b5 = (uint8)((value >> 40) & 0xFFu);
  *out_b6 = (uint8)((value >> 48) & 0xFFu);
  *out_b7 = (uint8)((value >> 56) & 0xFFu);
}

static NEXUS_MAYBE_UNUSED void nexus_bits_uint64_unpack_8u8_msb(uint64 value, uint8 *out_b0, uint8 *out_b1, uint8 *out_b2, uint8 *out_b3, /* NOLINT */
                                                                uint8 *out_b4, uint8 *out_b5, uint8 *out_b6, uint8 *out_b7) /* NOLINT */ {
  NEXUS_PROGRAM_PRECONDITION(out_b0 != NULL, "bits: out_b0 != NULL");
  NEXUS_PROGRAM_PRECONDITION(out_b1 != NULL, "bits: out_b1 != NULL");
  NEXUS_PROGRAM_PRECONDITION(out_b2 != NULL, "bits: out_b2 != NULL");
  NEXUS_PROGRAM_PRECONDITION(out_b3 != NULL, "bits: out_b3 != NULL");
  NEXUS_PROGRAM_PRECONDITION(out_b4 != NULL, "bits: out_b4 != NULL");
  NEXUS_PROGRAM_PRECONDITION(out_b5 != NULL, "bits: out_b5 != NULL");
  NEXUS_PROGRAM_PRECONDITION(out_b6 != NULL, "bits: out_b6 != NULL");
  NEXUS_PROGRAM_PRECONDITION(out_b7 != NULL, "bits: out_b7 != NULL");

  *out_b0 = (uint8)((value >> 56) & 0xFFu);
  *out_b1 = (uint8)((value >> 48) & 0xFFu);
  *out_b2 = (uint8)((value >> 40) & 0xFFu);
  *out_b3 = (uint8)((value >> 32) & 0xFFu);
  *out_b4 = (uint8)((value >> 24) & 0xFFu);
  *out_b5 = (uint8)((value >> 16) & 0xFFu);
  *out_b6 = (uint8)((value >> 8) & 0xFFu);
  *out_b7 = (uint8)(value & 0xFFu);
}

/* --- Bit Count & Bit Scan Operations --- */

static NEXUS_MAYBE_UNUSED uint64 nexus_bits_uint64_clear_lowest_set(uint64 value) /* NOLINT */ {
  return value & (value - 1ULL);
}

static NEXUS_MAYBE_UNUSED uint32 nexus_bits_uint64_trailing_zeros(uint64 value) /* NOLINT */ {
  NEXUS_PROGRAM_PRECONDITION(value != 0ULL, "bits: value != 0ULL");

#if defined(__GNUC__) || defined(__clang__)
  return (uint32)__builtin_ctzll(value);
#else
  {
    static const uint8 index64[64] = {0,  47, 1,  56, 48, 27, 2,  60, 57, 49, 41, 37, 28, 16, 3,  61, 54, 58, 35, 52, 50, 42,
                                      21, 44, 38, 32, 29, 23, 17, 11, 4,  62, 46, 55, 26, 59, 40, 36, 15, 53, 34, 51, 20, 43,
                                      31, 22, 10, 45, 25, 39, 14, 33, 19, 30, 9,  24, 13, 18, 8,  12, 7,  6,  5,  63};

    return (uint32)index64[(uint32)(((value ^ (value - 1ULL)) * 0x03f79d71b4cb0a89ULL) >> 58)];
  }
#endif
}

static NEXUS_MAYBE_UNUSED uint32 nexus_bits_uint64_popcount(uint64 value) /* NOLINT */ {
#if defined(__GNUC__) || defined(__clang__)
  return (uint32)__builtin_popcountll(value);
#else
  {
    uint32 count;

    count = 0;
    while (value != 0ULL) {
      value = nexus_bits_uint64_clear_lowest_set(value);
      count++;
    }
    return count;
  }
#endif
}

static NEXUS_MAYBE_UNUSED uint32 nexus_bits_uint32_clear_lowest_set(uint32 value) /* NOLINT */ {
  return value & (value - 1U);
}

static NEXUS_MAYBE_UNUSED uint32 nexus_bits_uint32_popcount(uint32 value) /* NOLINT */ {
#if defined(__GNUC__) || defined(__clang__)
  return (uint32)__builtin_popcount(value);
#else
  {
    uint32 count;

    count = 0;
    while (value != 0U) {
      value = nexus_bits_uint32_clear_lowest_set(value);
      count++;
    }
    return count;
  }
#endif
}

static NEXUS_MAYBE_UNUSED uint32 nexus_bits_uint32_trailing_zeros(uint32 value) /* NOLINT */ {
  NEXUS_PROGRAM_PRECONDITION(value != 0U, "bits: value != 0U");

  return nexus_bits_uint64_trailing_zeros((uint64)value);
}

static NEXUS_MAYBE_UNUSED uint32 nexus_bits_uint32_leading_zeros(uint32 value) /* NOLINT */ {
  NEXUS_PROGRAM_PRECONDITION(value != 0U, "bits: value != 0U");

#if defined(__GNUC__) || defined(__clang__)
  return (uint32)__builtin_clz(value);
#else
  {
    uint32 count;

    count = 0;
    while ((value & 0x80000000U) == 0U) {
      value <<= 1;
      count++;
    }
    return count;
  }
#endif
}

static NEXUS_MAYBE_UNUSED uint32 nexus_bits_uint64_leading_zeros(uint64 value) /* NOLINT */ {
  NEXUS_PROGRAM_PRECONDITION(value != 0ULL, "bits: value != 0ULL");

#if defined(__GNUC__) || defined(__clang__)
  return (uint32)__builtin_clzll(value);
#else
  {
    uint32 count;

    count = 0;
    while ((value & 0x8000000000000000ULL) == 0ULL) {
      value <<= 1;
      count++;
    }
    return count;
  }
#endif
}

/* --- Full-Width Bit Reversal --- */

static NEXUS_MAYBE_UNUSED uint64 nexus_bits_uint64_reverse(uint64 value) /* NOLINT */ {
  value = ((value & 0x5555555555555555ULL) << 1) | ((value >> 1) & 0x5555555555555555ULL);
  value = ((value & 0x3333333333333333ULL) << 2) | ((value >> 2) & 0x3333333333333333ULL);
  value = ((value & 0x0F0F0F0F0F0F0F0FULL) << 4) | ((value >> 4) & 0x0F0F0F0F0F0F0F0FULL);
  value = ((value & 0x00FF00FF00FF00FFULL) << 8) | ((value >> 8) & 0x00FF00FF00FF00FFULL);
  value = ((value & 0x0000FFFF0000FFFFULL) << 16) | ((value >> 16) & 0x0000FFFF0000FFFFULL);
  return (value << 32) | (value >> 32);
}

static NEXUS_MAYBE_UNUSED uint32 nexus_bits_uint32_reverse(uint32 value) /* NOLINT */ {
  return (uint32)(nexus_bits_uint64_reverse((uint64)value) >> 32);
}

static NEXUS_MAYBE_UNUSED uint16 nexus_bits_uint16_reverse(uint16 value) /* NOLINT */ {
  return (uint16)(nexus_bits_uint64_reverse((uint64)value) >> 48);
}

static NEXUS_MAYBE_UNUSED uint8 nexus_bits_uint8_reverse(uint8 value) /* NOLINT */ {
  return (uint8)(nexus_bits_uint64_reverse((uint64)value) >> 56);
}

static NEXUS_MAYBE_UNUSED uint_large nexus_bits_uint_large_reverse(uint_large value) /* NOLINT */ {
  NEXUS_PROGRAM_PRECONDITION(NEXUS_SIZEOF(uint_large) == 4 || NEXUS_SIZEOF(uint_large) == 8, "uint_large must be 32 or 64 bits");

  return (uint_large)(nexus_bits_uint64_reverse((uint64)value) >> (64u - (NEXUS_SIZEOF(uint_large) * 8u)));
}

/* --- Partial Bit Reversal --- */

static NEXUS_MAYBE_UNUSED uint8 nexus_bits_uint8_reverse_low(uint8 value, uint32 bit_count) /* NOLINT */ {
  NEXUS_PROGRAM_PRECONDITION(bit_count <= 8u, "bit_count out of range for uint8");
  return bit_count == 0 ? 0 : (uint8)(nexus_bits_uint8_reverse(value) >> (8u - bit_count));
}

static NEXUS_MAYBE_UNUSED uint16 nexus_bits_uint16_reverse_low(uint16 value, uint32 bit_count) /* NOLINT */ {
  NEXUS_PROGRAM_PRECONDITION(bit_count <= 16u, "bit_count out of range for uint16");
  return bit_count == 0 ? 0 : (uint16)(nexus_bits_uint16_reverse(value) >> (16u - bit_count));
}

static NEXUS_MAYBE_UNUSED uint32 nexus_bits_uint32_reverse_low(uint32 value, uint32 bit_count) /* NOLINT */ {
  NEXUS_PROGRAM_PRECONDITION(bit_count <= 32u, "bit_count out of range for uint32");
  return bit_count == 0 ? 0 : nexus_bits_uint32_reverse(value) >> (32u - bit_count);
}

static NEXUS_MAYBE_UNUSED uint64 nexus_bits_uint64_reverse_low(uint64 value, uint32 bit_count) /* NOLINT */ {
  NEXUS_PROGRAM_PRECONDITION(bit_count <= 64u, "bit_count out of range for uint64");
  return bit_count == 0 ? 0 : nexus_bits_uint64_reverse(value) >> (64u - bit_count);
}

static NEXUS_MAYBE_UNUSED uint_large nexus_bits_uint_large_reverse_low(uint_large value, uint32 bit_count) /* NOLINT */ {
  NEXUS_PROGRAM_PRECONDITION(bit_count <= NEXUS_SIZEOF(uint_large) * 8u, "bit_count out of range for uint_large");
  return bit_count == 0 ? 0 : nexus_bits_uint_large_reverse(value) >> ((NEXUS_SIZEOF(uint_large) * 8u) - bit_count);
}

/* ============================================================================ */
/* ---------------------------------------------------------------------------- */
/* DATA STRUCTURES                                                              */
/* ---------------------------------------------------------------------------- */
/* ============================================================================ */

/*
Summary: compares two elements for sorting and priority ordering.
Params: left/right  borrowed element_size-byte objects; user_data is caller context
Returns: a negative value for left-before-right, zero for equal, positive for after.
*/
typedef int32 NexusDataCompareFunction(const void *left, const void *right, void *user_data);

/*
Summary: a borrowed fixed-capacity view over equally sized elements.
Description:
  NexusDataArray deliberately has no logical length and no append/pop API.
  Every slot in [0, capacity) is addressable. It never allocates, grows, owns,
  initializes, or destroys element storage.
Ownership: data remains caller-owned and must outlive the view and every subview.
Thread-safety: synchronization follows the borrowed storage.
*/
typedef struct NexusDataArray {
  byte      *data;
  uint_large element_size;
  uint_large capacity;
} NexusDataArray;

/*
Summary: constructs a borrowed array view.
Returns: a view with no ownership or logical length.
Errors: NULL data with nonzero capacity or zero element_size is a programmer error.
*/
extern NexusDataArray nexus_data_array_view_create(void *data, uint_large element_size, uint_large capacity);

/*
Summary: returns a borrowed subview of array covering count elements at offset.
Returns: a view over the selected slots; storage remains owned by the original caller.
Errors: a range outside array is a programmer error.
*/
extern NexusDataArray nexus_data_array_subview(NexusDataArray array, uint_large offset, uint_large count);

/*
Summary: returns a mutable pointer to the element at index.
Returns: borrowed pointer to the slot.
Ownership: borrowed; invalid when the underlying storage lifetime ends.
Errors: NULL storage or index outside capacity is a programmer error.
*/
extern void *nexus_data_array_at(NexusDataArray *array, uint_large index);

/*
Summary: returns a const pointer to the element at index.
Returns: borrowed pointer to the slot.
Ownership: borrowed; invalid when the underlying storage lifetime ends.
Errors: NULL storage or index outside capacity is a programmer error.
*/
extern const void *nexus_data_array_at_const(const NexusDataArray *array, uint_large index);

/*
Summary: returns a mutable pointer to the first slot of array's contiguous storage.
Description: the capacity slots are tightly packed, element_size bytes apart, so the result may be
  handed to any API expecting a plain C array of capacity elements (e.g. Vulkan). Prefer this to
  &at(0), which only works by accident of layout.
Returns: borrowed pointer, or NULL when array has no storage (capacity zero).
Ownership: borrowed; invalid when the underlying storage lifetime ends.
Errors: NULL array is a programmer error.
*/
extern void *nexus_data_array_data_get(NexusDataArray *array);

/*
Summary: returns a const pointer to the first slot of array's contiguous storage.
Description: see nexus_data_array_data_get.
Returns: borrowed pointer, or NULL when array has no storage (capacity zero).
Ownership: borrowed; invalid when the underlying storage lifetime ends.
Errors: NULL array is a programmer error.
*/
extern const void *nexus_data_array_data_get_const(const NexusDataArray *array);

/*
Summary: copies element into the slot at index.
Params: element  borrowed element_size bytes; copied into the view
Errors: NULL arguments or index outside capacity is a programmer error.
*/
extern void nexus_data_array_set(NexusDataArray *array, uint_large index, const void *element);

/*
Summary: copies the slot at index into out_element.
Params: out_element  out buffer of element_size bytes
Errors: NULL arguments or index outside capacity is a programmer error.
*/
extern void nexus_data_array_get(const NexusDataArray *array, uint_large index, void *out_element);

/*
Summary: stores pointer itself in a pointer-sized array slot.
Params: pointer  stored verbatim; NULL is accepted
Ownership: pointees remain caller-owned; no pointed-to resource is released.
Errors: NULL array/storage, non-pointer-sized elements, or index outside capacity asserts.
Thread-safety: caller synchronizes access to storage and pointees.
Complexity: O(1) time; no allocation.
*/
extern void nexus_data_array_set_pointer(NexusDataArray *array, uint_large index, const void *pointer);

/*
Summary: returns the pointer value stored at index.
Returns: stored pointer value, including NULL; not the slot address.
Ownership: pointees remain caller-owned; no pointed-to resource is released.
Errors: NULL array/storage, non-pointer-sized elements, or index outside capacity asserts.
Thread-safety: caller synchronizes access to storage and pointees.
Complexity: O(1) time; no allocation.
*/
extern void *nexus_data_array_pointer_at(const NexusDataArray *array, uint_large index);

/*
Summary: reverses every element in array in place.
Errors: NULL array or invalid array storage is a programmer error (asserts).
Complexity: O(capacity) time and O(1) storage.
*/
extern void nexus_data_array_reverse(NexusDataArray *array);

/*
Summary: sorts every element in array in place using an unstable introsort.
Description:
  Introsort combines median-of-three quicksort, insertion sort for small
  partitions, and a heapsort depth fallback, guaranteeing O(n log n) worst-case
  comparisons without allocation or recursion.
Errors: NULL array/comparator or invalid array storage is a programmer error.
Complexity: O(n log n) worst-case time and O(log n) fixed stack storage.
*/
extern void nexus_data_array_sort(NexusDataArray *array, NexusDataCompareFunction *compare, void *user_data);

/*
Summary: binary-searches a sorted array for key.
Params: out_index  nullable; receives the matching index, or insertion index when absent
Returns: TRUE when an equal element exists.
Errors: NULL array/key/comparator or invalid array storage is a programmer error.
Complexity: O(log n).
*/
extern boolean nexus_data_array_binary_search(const NexusDataArray *array, const void *key, NexusDataCompareFunction *compare, void *user_data,
                                              uint_large *out_index);

/*
Summary: stably sorts count 64-bit keys ascending with an LSD radix sort, moving a parallel 32-bit value (typically an index into the caller's item array) with each key.
Description:
  Sorting (key, index) pairs instead of wide items keeps every pass over 12 bytes per element; the caller then reads its items in the sorted index order.
  All eight byte histograms are built in one read of the keys, and any byte position on which every key agrees is skipped, so keys that only use a few
  bytes cost proportionally fewer passes. Equal keys keep their original relative order.
Params: keys            count keys, sorted in place
        values          count values, permuted together with keys
        scratch_keys    count keys of caller-owned storage, overwritten
        scratch_values  count values of caller-owned storage, overwritten
Ownership: nothing is allocated; all four arrays remain caller-owned and must not overlap.
Errors: NULL arrays with a nonzero count, or a count above UINT32_MAX, are programmer errors (asserts).
Complexity: O(n) time (at most 8 passes) and O(n) caller-provided scratch.
*/
extern void nexus_data_keys_radix_sort(uint64 *keys, uint32 *values, uint_large count, uint64 *scratch_keys, uint32 *scratch_values);

/*
Summary: a borrowed fixed-width set of bit_count bits over caller-owned 32-bit words.
Description:
  Like NexusDataArray, a bitset never allocates, grows, owns, or destroys its storage; the
  caller supplies NEXUS_DATA_BITSET_WORD_COUNT(bit_count) words. Because the storage is plain
  words, a set can live by value inside a larger struct (a configuration, a candidate record),
  be copied with a plain struct copy, and be zero-initialized into the empty set; a view is made
  over it on demand. Bits at or past bit_count in the last word are ignored by every query, so
  stale tail bits never change a result.
Ownership: words remains caller-owned and must outlive the view.
Thread-safety: synchronization follows the borrowed storage.
*/
typedef struct NexusDataBitset {
  uint32    *words;
  uint_large bit_count;
} NexusDataBitset;

#define NEXUS_DATA_BITSET_WORD_BITS 32U

/* Number of uint32 words needed to hold bit_count bits; usable in array bounds. */
#define NEXUS_DATA_BITSET_WORD_COUNT(bit_count) (((bit_count) + (NEXUS_DATA_BITSET_WORD_BITS - 1U)) / NEXUS_DATA_BITSET_WORD_BITS)

/*
Summary: constructs a borrowed bitset view over words.
Description: the storage is not modified; call nexus_data_bitset_clear_all for an empty set.
Returns: a view with no ownership.
Errors: NULL words with nonzero bit_count is a programmer error.
*/
extern NexusDataBitset nexus_data_bitset_view_create(uint32 *words, uint_large bit_count);

/*
Summary: sets every bit in bitset to 0.
Errors: NULL bitset is a programmer error.
Complexity: O(bit_count / 32).
*/
extern void nexus_data_bitset_clear_all(NexusDataBitset *bitset);

/*
Summary: returns how many bits in bitset are 1.
Errors: NULL bitset is a programmer error.
Complexity: O(bit_count / 32) words, O(set bits) per word.
*/
extern uint_large nexus_data_bitset_count(const NexusDataBitset *bitset);

/*
Summary: tells whether every bit set in subset is also set in superset.
Description: the empty set is a subset of every set. Both views must have the same bit_count.
Returns: TRUE when subset is contained in superset.
Errors: NULL arguments or differing bit_count is a programmer error.
Complexity: O(bit_count / 32).
*/
extern boolean nexus_data_bitset_is_subset(const NexusDataBitset *subset, const NexusDataBitset *superset);

/*
Summary: tells whether left and right hold exactly the same bits.
Errors: NULL arguments or differing bit_count is a programmer error.
Complexity: O(bit_count / 32).
*/
extern boolean nexus_data_bitset_equals(const NexusDataBitset *left, const NexusDataBitset *right);

/*
Summary: destination |= source.
Errors: NULL arguments or differing bit_count is a programmer error. Aliasing is allowed.
Complexity: O(bit_count / 32).
*/
extern void nexus_data_bitset_union_into(NexusDataBitset *destination, const NexusDataBitset *source);

/*
Summary: destination &= source.
Errors: NULL arguments or differing bit_count is a programmer error. Aliasing is allowed.
Complexity: O(bit_count / 32).
*/
extern void nexus_data_bitset_intersect_into(NexusDataBitset *destination, const NexusDataBitset *source);

/*
Summary: destination &= ~source (removes from destination every bit set in source).
Errors: NULL arguments or differing bit_count is a programmer error. Aliasing is allowed.
Complexity: O(bit_count / 32).
*/
extern void nexus_data_bitset_difference_into(NexusDataBitset *destination, const NexusDataBitset *source);

/*
Summary: sets the bit at index to 1.
Errors: NULL bitset or index outside bit_count is a programmer error.
*/
static NEXUS_MAYBE_UNUSED void nexus_data_bitset_set(NexusDataBitset *bitset, uint_large index) {
  NEXUS_PROGRAM_PRECONDITION(bitset != NULL, "data_bitset_set: bitset is NULL");
  NEXUS_PROGRAM_PRECONDITION(index < bitset->bit_count, "data_bitset_set: index is outside bit_count");

  bitset->words[index / NEXUS_DATA_BITSET_WORD_BITS] |= (uint32)1U << (index % NEXUS_DATA_BITSET_WORD_BITS);
}

/*
Summary: sets the bit at index to 0.
Errors: NULL bitset or index outside bit_count is a programmer error.
*/
static NEXUS_MAYBE_UNUSED void nexus_data_bitset_clear(NexusDataBitset *bitset, uint_large index) {
  NEXUS_PROGRAM_PRECONDITION(bitset != NULL, "data_bitset_clear: bitset is NULL");
  NEXUS_PROGRAM_PRECONDITION(index < bitset->bit_count, "data_bitset_clear: index is outside bit_count");

  bitset->words[index / NEXUS_DATA_BITSET_WORD_BITS] &= ~((uint32)1U << (index % NEXUS_DATA_BITSET_WORD_BITS));
}

/*
Summary: tells whether the bit at index is 1.
Returns: TRUE when the bit is set.
Errors: NULL bitset or index outside bit_count is a programmer error.
*/
static NEXUS_MAYBE_UNUSED boolean nexus_data_bitset_test(const NexusDataBitset *bitset, uint_large index) {
  NEXUS_PROGRAM_PRECONDITION(bitset != NULL, "data_bitset_test: bitset is NULL");
  NEXUS_PROGRAM_PRECONDITION(index < bitset->bit_count, "data_bitset_test: index is outside bit_count");

  return (boolean)((bitset->words[index / NEXUS_DATA_BITSET_WORD_BITS] >> (index % NEXUS_DATA_BITSET_WORD_BITS)) & 1U);
}

typedef struct NexusDataList          NexusDataList;
typedef struct NexusDataStack         NexusDataStack;
typedef struct NexusDataQueue         NexusDataQueue;
typedef struct NexusDataRingBuffer    NexusDataRingBuffer;
typedef struct NexusDataPriorityQueue NexusDataPriorityQueue;
typedef struct NexusDataHashMap       NexusDataHashMap;

/*
Summary: creates an owning contiguous list with logical length zero.
Description: capacity grows geometrically. All element addresses may change on
  append, insert, or reserve; indices remain stable until structural edits.
Params: element_size  fixed byte width of every element
Returns: an owned list released by nexus_data_list_destroy.
Errors: zero element_size or allocation overflow is a programmer error (asserts).
*/
extern NexusDataList *nexus_data_list_create(uint_large element_size, uint_large initial_capacity);
/*
Summary: destroys list and releases its owned storage.
Params: list  owned list; NULL is accepted as a no-op
Ownership: consumes the list handle and all element storage it owns.
*/
extern void nexus_data_list_destroy(NexusDataList *list);
/*
Summary: returns list's current logical element count.
Returns: number of initialized logical elements.
Errors: NULL list is a programmer error (asserts).
*/
extern uint_large nexus_data_list_length_get(const NexusDataList *list);
/*
Summary: returns the size in bytes of one element, as given to nexus_data_list_create.
Returns: the element size; callers that write elements through an uninitialized slot use it to check their type fits.
Errors: NULL list is a programmer error (asserts).
*/
extern uint_large nexus_data_list_element_size_get(const NexusDataList *list);
/*
Summary: returns list's allocated element capacity.
Returns: number of element slots available before growth.
Errors: NULL list is a programmer error (asserts).
*/
extern uint_large nexus_data_list_capacity_get(const NexusDataList *list);
/*
Summary: grows list so at least minimum_capacity elements fit.
Errors: NULL list or an unrepresentable allocation size is a programmer error (asserts).
*/
extern void nexus_data_list_reserve(NexusDataList *list, uint_large minimum_capacity);
/*
Summary: returns a mutable borrowed pointer to list[index].
Returns: pointer invalidated by a structural edit or destroy.
Ownership: pointer remains owned by list.
Errors: NULL list or index outside length is a programmer error (asserts).
*/
extern void *nexus_data_list_at(NexusDataList *list, uint_large index);
/*
Summary: returns a const borrowed pointer to list[index].
Returns: pointer invalidated by a structural edit or destroy.
Ownership: pointer remains owned by list.
Errors: NULL list or index outside length is a programmer error (asserts).
*/
extern const void *nexus_data_list_at_const(const NexusDataList *list, uint_large index);
/*
Summary: returns a mutable pointer to the first element of list's contiguous storage.
Description: the length elements are tightly packed, element_size bytes apart, so the result may
  be handed to any API expecting a plain C array of length elements (e.g. Vulkan). Prefer this to
  &at(0), which only works by accident of layout.
Returns: pointer valid for length elements, invalidated by a structural edit or destroy; NULL
  when the list has no storage yet.
Ownership: pointer remains owned by list.
Errors: NULL list is a programmer error (asserts).
*/
extern void *nexus_data_list_data_get(NexusDataList *list);
/*
Summary: returns a const pointer to the first element of list's contiguous storage.
Description: see nexus_data_list_data_get.
Returns: pointer valid for length elements, invalidated by a structural edit or destroy; NULL
  when the list has no storage yet.
Ownership: pointer remains owned by list.
Errors: NULL list is a programmer error (asserts).
*/
extern const void *nexus_data_list_data_get_const(const NexusDataList *list);
/*
Summary: appends pointer itself (not the bytes it points to) to a list of pointers.
Description: for lists created with an element_size of NEXUS_EFFECTIVE_POINTER_SIZE_BYTES. The
  pointee is not copied or owned, so string literals and other long-lived objects are the usual
  use. Together with nexus_data_list_data_get_const this builds arrays like Vulkan's
  const char *const * name lists without a temporary pointer variable.
Params: pointer  stored verbatim; may be NULL
Returns: mutable borrowed slot holding the pointer, invalidated by a later structural edit.
Errors: NULL list or a list whose element_size is not pointer-sized is a programmer error (asserts).
*/
extern void *nexus_data_list_append_pointer(NexusDataList *list, const void *pointer);
/*
Summary: returns the pointer stored at index in a list of pointers.
Returns: the stored pointer value (not a pointer to the slot).
Errors: NULL list, a list whose element_size is not pointer-sized, or index outside length is a programmer error (asserts).
*/
extern void *nexus_data_list_pointer_at(const NexusDataList *list, uint_large index);
/*
Summary: appends element and returns its mutable borrowed slot.
Params: element  borrowed element_size bytes; copied before return
Returns: pointer to the appended slot, invalidated by a later structural edit.
Errors: NULL list/element or allocation overflow is a programmer error (asserts).
*/
extern void *nexus_data_list_append(NexusDataList *list, const void *element);
/*
Summary: appends one slot without initializing its bytes.
Returns: mutable borrowed slot, invalidated by a later structural edit.
Errors: NULL list or allocation overflow is a programmer error (asserts).
*/
extern void *nexus_data_list_append_uninitialized(NexusDataList *list);
/*
Summary: inserts element before index, shifting later elements right.
Params: element  borrowed element_size bytes; may not alias list storage
Errors: NULL list/element, index greater than length, or allocation overflow is a programmer error (asserts).
*/
extern void nexus_data_list_insert(NexusDataList *list, uint_large index, const void *element);
/*
Summary: removes index and optionally copies it to out_element.
Params: out_element  optional out buffer of element_size bytes
Errors: NULL list or index outside length is a programmer error (asserts).
*/
extern void nexus_data_list_remove(NexusDataList *list, uint_large index, void *out_element);
/*
Summary: removes the last element and reports whether one existed.
Params: out_element  optional out buffer of element_size bytes
Returns: TRUE when an element was removed.
Errors: NULL list is a programmer error (asserts).
*/
extern boolean nexus_data_list_pop(NexusDataList *list, void *out_element);
/*
Summary: inserts pointer itself before index in a pointer-sized list.
Params: pointer  stored verbatim; NULL is accepted
Ownership: pointees remain caller-owned; no pointed-to resource is released.
Errors: NULL list, non-pointer-sized elements, invalid insertion index,
  or allocation/capacity overflow asserts.
Thread-safety: caller synchronizes access to storage and pointees.
Complexity: O(n) metadata shifts; may allocate on growth.
*/
extern void nexus_data_list_insert_pointer(NexusDataList *list, uint_large index, const void *pointer);

/*
Summary: removes index and optionally copies its stored pointer.
Params: out_pointer  optional out; must not overlap list storage
Ownership: pointees remain caller-owned; no pointed-to resource is released.
Errors: NULL list, non-pointer-sized elements, or index outside length asserts.
Thread-safety: caller synchronizes access to storage and pointees.
Complexity: O(n) shifts; no allocation.
*/
extern void nexus_data_list_remove_pointer(NexusDataList *list, uint_large index, void **out_pointer);

/*
Summary: removes the last pointer and reports whether one existed.
Params: out_pointer  optional out; must not overlap list storage;
  left untouched when empty
Returns: TRUE on removal, including a stored NULL; FALSE when empty.
Ownership: pointees remain caller-owned; no pointed-to resource is released.
Errors: NULL list or non-pointer-sized elements asserts.
Thread-safety: caller synchronizes access to storage and pointees.
Complexity: O(1) time; no allocation.
*/
extern boolean nexus_data_list_pop_pointer(NexusDataList *list, void **out_pointer);

/*
Summary: reverses list element order in place.
Description:
  Reverses logical index order. Length, capacity, and the storage
  allocation are unchanged; empty and single-element lists are no-ops.
Ownership:
  Borrows list; element slots stay allocated but may hold different
  elements after reversal. Stored pointees remain caller-owned.
Errors: NULL list is a programmer error (asserts).
Thread-safety: caller synchronizes access to list and its elements.
Complexity: O(n * element_size) time and O(1) extra storage; no allocation.
*/
extern void nexus_data_list_reverse(NexusDataList *list);

/*
Summary: sets list length to zero while retaining allocated capacity.
Errors: NULL list is a programmer error (asserts).
*/
extern void nexus_data_list_clear(NexusDataList *list);
/*
Summary: sorts all list elements with the supplied comparator.
Params: compare  borrowed comparator; called with two element pointers
Errors: NULL list/comparator is a programmer error (asserts).
*/
extern void nexus_data_list_sort(NexusDataList *list, NexusDataCompareFunction *compare, void *user_data);

/*
Summary: creates an owning growable LIFO stack.
Params: element_size  fixed byte width of every element
Returns: an owned stack released by nexus_data_stack_destroy.
Errors: zero element_size or allocation overflow is a programmer error (asserts).
*/
extern NexusDataStack *nexus_data_stack_create(uint_large element_size, uint_large initial_capacity);
/*
Summary: destroys stack and releases its owned storage.
Ownership: consumes stack; NULL is accepted as a no-op.
*/
extern void nexus_data_stack_destroy(NexusDataStack *stack);
/*
Summary: returns the number of elements currently on stack.
Returns: current LIFO depth.
Errors: NULL stack is a programmer error (asserts).
*/
extern uint_large nexus_data_stack_length_get(const NexusDataStack *stack);
/*
Summary: returns a mutable pointer to the bottom element of stack's contiguous storage (oldest first).
Description: the depth elements are tightly packed, element_size bytes apart, so the result is a
  plain C array of depth elements (oldest first).
Returns: pointer valid for depth elements, invalidated by a structural edit or destroy; NULL when
  the stack has no storage yet.
Ownership: pointer remains owned by stack.
Errors: NULL stack is a programmer error (asserts).
*/
extern void *nexus_data_stack_data_get(NexusDataStack *stack);
/*
Summary: returns a const pointer to the bottom element of stack's contiguous storage (oldest first).
Description: see nexus_data_stack_data_get.
Returns: pointer valid for depth elements, invalidated by a structural edit or destroy; NULL when
  the stack has no storage yet.
Ownership: pointer remains owned by stack.
Errors: NULL stack is a programmer error (asserts).
*/
extern const void *nexus_data_stack_data_get_const(const NexusDataStack *stack);
/*
Summary: pushes element and returns its mutable borrowed slot.
Params: element  borrowed element_size bytes; copied before return
Returns: pointer invalidated by a later push, pop, reserve, or destroy.
Errors: NULL stack/element or allocation overflow is a programmer error (asserts).
*/
extern void *nexus_data_stack_push(NexusDataStack *stack, const void *element);
/*
Summary: pops the top element and reports whether one existed.
Params: out_element  optional out buffer of element_size bytes
Returns: TRUE when an element was removed.
Errors: NULL stack is a programmer error (asserts).
*/
extern boolean nexus_data_stack_pop(NexusDataStack *stack, void *out_element);
/*
Summary: returns the top element without removing it, or NULL when empty.
Returns: borrowed pointer invalidated by a later structural edit.
Ownership: pointer remains owned by stack.
Errors: NULL stack is a programmer error (asserts).
*/
extern void *nexus_data_stack_peek(NexusDataStack *stack);
/*
Summary: pushes pointer itself into a pointer-sized container.
Params: pointer  stored verbatim; NULL is accepted
Returns: borrowed pointer slot; invalidated by a later structural edit.
Ownership: pointees remain caller-owned; no pointed-to resource is released.
Errors: NULL stack, non-pointer-sized elements, or allocation/capacity overflow asserts.
Thread-safety: caller synchronizes access to storage and pointees.
Complexity: O(1) amortized; may allocate on growth.
*/
extern void *nexus_data_stack_push_pointer(NexusDataStack *stack, const void *pointer);

/*
Summary: pops the next pointer and reports whether one existed.
Params: out_pointer  optional out; must not overlap stack storage;
  left untouched when empty
Returns: TRUE on removal, including a stored NULL; FALSE when empty.
Ownership: pointees remain caller-owned; no pointed-to resource is released.
Errors: NULL stack or non-pointer-sized elements asserts.
Thread-safety: caller synchronizes access to storage and pointees.
Complexity: O(1) time; no allocation.
*/
extern boolean nexus_data_stack_pop_pointer(NexusDataStack *stack, void **out_pointer);

/*
Summary: returns the next stored pointer without removing it.
Description: use length_get or pop_pointer to distinguish an empty container
  from a stored NULL.
Returns: stored pointer value; NULL means empty or a stored NULL.
Ownership: pointees remain caller-owned; no pointed-to resource is released.
Errors: NULL stack or non-pointer-sized elements asserts.
Thread-safety: caller synchronizes access to storage and pointees.
Complexity: O(1) time; no allocation.
*/
extern void *nexus_data_stack_peek_pointer(NexusDataStack *stack);

/*
Summary: reverses stack element order in place.
Description:
  Reverses the current pop order; the bottom element becomes the top.
  Length, capacity, and the storage allocation are unchanged.
Ownership:
  Borrows stack; element slots stay allocated but may hold different
  elements after reversal. Stored pointees remain caller-owned.
Errors: NULL stack is a programmer error (asserts).
Thread-safety: caller synchronizes access to stack and its elements.
Complexity: O(n * element_size) time and O(1) extra storage; no allocation.
*/
extern void nexus_data_stack_reverse(NexusDataStack *stack);

/*
Summary: removes all elements while retaining stack capacity.
Errors: NULL stack is a programmer error (asserts).
*/
extern void nexus_data_stack_clear(NexusDataStack *stack);

/*
Summary: creates an owning growable FIFO queue backed by a circular array.
Params: element_size  fixed byte width of every element
Returns: an owned queue released by nexus_data_queue_destroy.
Errors: zero element_size or allocation overflow is a programmer error (asserts).
*/
extern NexusDataQueue *nexus_data_queue_create(uint_large element_size, uint_large initial_capacity);
/*
Summary: destroys queue and releases its owned storage.
Ownership: consumes queue; NULL is accepted as a no-op.
*/
extern void nexus_data_queue_destroy(NexusDataQueue *queue);
/*
Summary: returns the number of elements currently queued.
Returns: current FIFO depth.
Errors: NULL queue is a programmer error (asserts).
*/
extern uint_large nexus_data_queue_length_get(const NexusDataQueue *queue);
/*
Summary: rearranges queue's circular storage so the oldest element sits at slot 0, and returns that pointer.
Description: the queue wraps around its allocation, so its elements are not contiguous in FIFO order
  until linearized. This rotates storage in place (no allocation) and leaves the queue fully
  usable; the length elements then form a plain C array, front-to-back. Mutates the queue, so
  there is no const form.
Returns: pointer valid for length elements, invalidated by a structural edit or destroy, and
  invalidating every earlier peek or push pointer; NULL when the queue has no storage yet.
Ownership: pointer remains owned by queue.
Errors: NULL queue is a programmer error (asserts).
Complexity: O(1) when already aligned at slot 0, otherwise O(capacity) time and O(1) storage.
*/
extern void *nexus_data_queue_linearize(NexusDataQueue *queue);
/*
Summary: enqueues element and returns its mutable borrowed slot.
Params: element  borrowed element_size bytes; copied before return
Returns: pointer invalidated by a later push, pop, reserve, or destroy.
Errors: NULL queue/element or allocation overflow is a programmer error (asserts).
*/
extern void *nexus_data_queue_push(NexusDataQueue *queue, const void *element);
/*
Summary: dequeues the oldest element and reports whether one existed.
Params: out_element  optional out buffer of element_size bytes
Returns: TRUE when an element was removed.
Errors: NULL queue is a programmer error (asserts).
*/
extern boolean nexus_data_queue_pop(NexusDataQueue *queue, void *out_element);
/*
Summary: returns the oldest element without removing it, or NULL when empty.
Returns: borrowed pointer invalidated by a later structural edit.
Ownership: pointer remains owned by queue.
Errors: NULL queue is a programmer error (asserts).
*/
extern void *nexus_data_queue_peek(NexusDataQueue *queue);

/*
Summary: enqueues a pointer value in a pointer-sized queue.
Description: stores the pointer itself, without copying its pointee.
Params: pointer  stored verbatim; NULL is accepted
Returns: borrowed pointer to the stored pointer slot, valid until a later structural edit or destroy.
Ownership: queue owns the slot; the pointee stays caller-owned.
Errors: NULL queue, non-pointer-sized elements, or length/allocation overflow asserts.
Thread-safety: caller synchronizes access to the container and pointees.
Complexity: O(1) amortized; no allocation within reserved capacity.
*/
extern void *nexus_data_queue_push_pointer(NexusDataQueue *queue, const void *pointer);

/*
Summary: dequeues the oldest pointer and reports whether one existed.
Description: TRUE also reports success when the dequeued pointer is NULL.
Params: out_pointer  optional out; receives the stored pointer on success;
  must not overlap queue-owned storage; left untouched when empty
Returns: TRUE if an element was removed; FALSE when empty.
Ownership: pointee stays caller-owned; no pointed-to resource is released.
Errors: NULL queue or non-pointer-sized elements asserts.
Thread-safety: caller synchronizes access to the container and pointees.
Complexity: O(1) time; no allocation.
*/
extern boolean nexus_data_queue_pop_pointer(NexusDataQueue *queue, void **out_pointer);

/*
Summary: returns the oldest stored pointer without removing it.
Description: returns the pointer value, not a pointer to its queue slot.
  NULL means either an empty queue or a stored NULL; use length_get or
  pop_pointer when those cases must be distinguished.
Returns: stored pointer value, or NULL when empty.
Ownership: pointee stays caller-owned; its lifetime is caller-controlled.
Errors: NULL queue or non-pointer-sized elements asserts.
Thread-safety: caller synchronizes access to the container and pointees.
Complexity: O(1) time; no allocation.
*/
extern void *nexus_data_queue_peek_pointer(NexusDataQueue *queue);

/*
Summary: reverses queue element order in place.
Description:
  Reverses the current pop order, including wrapped live elements.
  Length, capacity, and the storage allocation are unchanged.
Ownership:
  Borrows queue; element slots stay allocated but may hold different
  elements after reversal. Stored pointees remain caller-owned.
Errors: NULL queue is a programmer error (asserts).
Thread-safety: caller synchronizes access to queue and its elements.
Complexity: O(n * element_size) time and O(1) extra storage; no allocation.
*/
extern void nexus_data_queue_reverse(NexusDataQueue *queue);

/*
Summary: removes all elements while retaining queue capacity.
Errors: NULL queue is a programmer error (asserts).
*/
extern void nexus_data_queue_clear(NexusDataQueue *queue);

/*
Summary: creates an owning fixed-capacity overwrite ringbuffer.
Description: push appends newest data; when full it overwrites the oldest slot.
  Logical index zero is always the oldest retained element.
Params: element_size  fixed byte width; capacity is never grown
Returns: an owned ringbuffer released by nexus_data_ringbuffer_destroy.
Errors: zero element_size/capacity or allocation overflow is a programmer error (asserts).
*/
extern NexusDataRingBuffer *nexus_data_ringbuffer_create(uint_large element_size, uint_large capacity);
/*
Summary: destroys ringbuffer and releases its owned storage.
Ownership: consumes ringbuffer; NULL is accepted as a no-op.
*/
extern void nexus_data_ringbuffer_destroy(NexusDataRingBuffer *ringbuffer);
/*
Summary: returns the number of retained elements.
Returns: current oldest-first length.
Errors: NULL ringbuffer is a programmer error (asserts).
*/
extern uint_large nexus_data_ringbuffer_length_get(const NexusDataRingBuffer *ringbuffer);
/*
Summary: returns the fixed ringbuffer capacity.
Returns: maximum retained element count.
Errors: NULL ringbuffer is a programmer error (asserts).
*/
extern uint_large nexus_data_ringbuffer_capacity_get(const NexusDataRingBuffer *ringbuffer);
/*
Summary: appends element and reports whether the oldest element was overwritten.
Params: element  borrowed element_size bytes; copied before return
Returns: TRUE when a full ring overwrote its oldest element.
Errors: NULL ringbuffer/element is a programmer error (asserts).
*/
extern boolean nexus_data_ringbuffer_push(NexusDataRingBuffer *ringbuffer, const void *element);
/*
Summary: removes the oldest retained element and reports whether one existed.
Params: out_element  optional out buffer of element_size bytes
Returns: TRUE when an element was removed.
Errors: NULL ringbuffer is a programmer error (asserts).
*/
extern boolean nexus_data_ringbuffer_pop(NexusDataRingBuffer *ringbuffer, void *out_element);
/*
Summary: returns retained element at logical oldest-first index.
Returns: mutable borrowed pointer invalidated by later structural edits.
Ownership: pointer remains owned by ringbuffer.
Errors: NULL ringbuffer or index outside length is a programmer error (asserts).
*/
extern void *nexus_data_ringbuffer_at(NexusDataRingBuffer *ringbuffer, uint_large logical_index);
/*
Summary: rearranges ringbuffer's circular storage so the oldest element sits at slot 0, and returns that pointer.
Description: a wrapped ring is not contiguous in logical order, so &at(0) followed by at(1) is not
  guaranteed adjacent. This rotates storage in place (no allocation); the length retained elements
  then form a plain C array, oldest-first. Mutates the ring, so there is no const form.
Returns: pointer valid for length elements, invalidated by a push, pop, clear, or destroy, and
  invalidating every earlier at pointer.
Ownership: pointer remains owned by ringbuffer.
Errors: NULL ringbuffer is a programmer error (asserts).
Complexity: O(1) when already aligned at slot 0, otherwise O(capacity) time and O(1) storage.
*/
extern void *nexus_data_ringbuffer_linearize(NexusDataRingBuffer *ringbuffer);
/*
Summary: stores pointer itself, overwriting the oldest slot when full.
Params: pointer  stored verbatim; NULL is accepted
Returns: TRUE if the oldest pointer was overwritten; FALSE otherwise.
Ownership: pointees remain caller-owned; no pointed-to resource is released.
Errors: NULL ringbuffer or non-pointer-sized elements asserts.
Thread-safety: caller synchronizes access to storage and pointees.
Complexity: O(1) time; no allocation.
*/
extern boolean nexus_data_ringbuffer_push_pointer(NexusDataRingBuffer *ringbuffer, const void *pointer);

/*
Summary: removes the oldest pointer and reports whether one existed.
Params: out_pointer  optional out; must not overlap ringbuffer storage;
  left untouched when empty
Returns: TRUE on removal, including a stored NULL; FALSE when empty.
Ownership: pointees remain caller-owned; no pointed-to resource is released.
Errors: NULL ringbuffer or non-pointer-sized elements asserts.
Thread-safety: caller synchronizes access to storage and pointees.
Complexity: O(1) time; no allocation.
*/
extern boolean nexus_data_ringbuffer_pop_pointer(NexusDataRingBuffer *ringbuffer, void **out_pointer);

/*
Summary: returns the stored pointer at logical_index, oldest first.
Returns: stored pointer value, including NULL; not the slot address.
Ownership: pointees remain caller-owned; no pointed-to resource is released.
Errors: NULL ringbuffer, non-pointer-sized elements, or index outside length asserts.
Thread-safety: caller synchronizes access to storage and pointees.
Complexity: O(1) time; no allocation.
*/
extern void *nexus_data_ringbuffer_pointer_at(NexusDataRingBuffer *ringbuffer, uint_large logical_index);

/*
Summary: reverses ringbuffer element order in place.
Description:
  Reverses the current pop order, including wrapped live elements.
  Length, capacity, and the storage allocation are unchanged.
Ownership:
  Borrows ringbuffer; element slots stay allocated but may hold different
  elements after reversal. Stored pointees remain caller-owned.
Errors: NULL ringbuffer is a programmer error (asserts).
Thread-safety: caller synchronizes access to ringbuffer and its elements.
Complexity: O(n * element_size) time and O(1) extra storage; no allocation.
*/
extern void nexus_data_ringbuffer_reverse(NexusDataRingBuffer *ringbuffer);

/*
Summary: removes all retained elements without releasing ring storage.
Errors: NULL ringbuffer is a programmer error (asserts).
*/
extern void nexus_data_ringbuffer_clear(NexusDataRingBuffer *ringbuffer);

/*
Summary: creates an owning growable binary min-heap priority queue.
Params:
  element_size  fixed byte width of every element
  compare       borrowed ordering callback; lower values have higher priority
Ownership: compare and user_data are borrowed for the queue's lifetime.
Returns: an owned queue released by nexus_data_priority_queue_destroy.
Errors: zero element_size, NULL compare, or allocation overflow is a programmer error (asserts).
*/
extern NexusDataPriorityQueue *nexus_data_priority_queue_create(uint_large element_size, uint_large initial_capacity,
                                                                NexusDataCompareFunction *compare, void *user_data);
/*
Summary: destroys priority queue and releases its owned storage.
Ownership: consumes queue; NULL is accepted as a no-op.
*/
extern void nexus_data_priority_queue_destroy(NexusDataPriorityQueue *queue);
/*
Summary: returns the number of elements in priority queue.
Returns: current heap size.
Errors: NULL queue is a programmer error (asserts).
*/
extern uint_large nexus_data_priority_queue_length_get(const NexusDataPriorityQueue *queue);
/*
Summary: inserts element and returns its mutable borrowed slot.
Params: element  borrowed element_size bytes; copied before heap adjustment
Returns: pointer invalidated by later push, pop, reserve, or destroy.
Errors: NULL queue/element or allocation overflow is a programmer error (asserts).
*/
extern void *nexus_data_priority_queue_push(NexusDataPriorityQueue *queue, const void *element);
/*
Summary: removes the minimum element and reports whether one existed.
Params: out_element  optional out buffer of element_size bytes
Returns: TRUE when an element was removed.
Errors: NULL queue is a programmer error (asserts).
*/
extern boolean nexus_data_priority_queue_pop(NexusDataPriorityQueue *queue, void *out_element);
/*
Summary: returns the minimum element without removing it, or NULL when empty.
Returns: mutable borrowed pointer invalidated by later structural edits.
Ownership: pointer remains owned by queue.
Errors: NULL queue is a programmer error (asserts).
*/
extern void *nexus_data_priority_queue_peek(NexusDataPriorityQueue *queue);
/*
Summary: pushes pointer itself into a pointer-sized container.
Description: comparator receives pointer slots, as with ordinary push;
  it must dereference them to compare pointees.
Params: pointer  stored verbatim; NULL is accepted
Returns: borrowed pointer slot; invalidated by a later structural edit.
Ownership: pointees remain caller-owned; no pointed-to resource is released.
Errors: NULL queue, non-pointer-sized elements, or allocation/capacity overflow asserts.
Thread-safety: caller synchronizes access to storage and pointees.
Complexity: O(log n) time; may allocate on growth.
*/
extern void *nexus_data_priority_queue_push_pointer(NexusDataPriorityQueue *queue, const void *pointer);

/*
Summary: pops the next pointer and reports whether one existed.
Params: out_pointer  optional out; must not overlap queue storage;
  left untouched when empty
Returns: TRUE on removal, including a stored NULL; FALSE when empty.
Ownership: pointees remain caller-owned; no pointed-to resource is released.
Errors: NULL queue or non-pointer-sized elements asserts.
Thread-safety: caller synchronizes access to storage and pointees.
Complexity: O(log n) time; no allocation.
*/
extern boolean nexus_data_priority_queue_pop_pointer(NexusDataPriorityQueue *queue, void **out_pointer);

/*
Summary: returns the next stored pointer without removing it.
Description: use length_get or pop_pointer to distinguish an empty container
  from a stored NULL.
Returns: stored pointer value; NULL means empty or a stored NULL.
Ownership: pointees remain caller-owned; no pointed-to resource is released.
Errors: NULL queue or non-pointer-sized elements asserts.
Thread-safety: caller synchronizes access to storage and pointees.
Complexity: O(1) time; no allocation.
*/
extern void *nexus_data_priority_queue_peek_pointer(NexusDataPriorityQueue *queue);

/*
Summary: removes all elements while retaining queue capacity.
Errors: NULL queue is a programmer error (asserts).
*/
extern void nexus_data_priority_queue_clear(NexusDataPriorityQueue *queue);

/*
Summary: one caller-embedded node in an intrusive doubly linked list.
Description: initialize every node explicitly before insertion. The list stores
  no payload and allocates nothing; use NEXUS_DATA_LINKED_LIST_ENTRY to recover
  the containing object.
*/
typedef struct NexusDataLinkedListNode {
  struct NexusDataLinkedListNode *previous;
  struct NexusDataLinkedListNode *next;
  void                           *owner;
} NexusDataLinkedListNode;

/*
Summary: an allocation-free intrusive doubly linked list header.
*/
typedef struct NexusDataLinkedList {
  NexusDataLinkedListNode *first;
  NexusDataLinkedListNode *last;
  uint_large               length;
} NexusDataLinkedList;

#define NEXUS_DATA_LINKED_LIST_ENTRY(node, type, member) ((type *)((byte *)(node) - NEXUS_OFFSETOF(type, member)))

/*
Summary: initializes an empty intrusive list header.
Errors: NULL list is a programmer error (asserts).
*/
extern void nexus_data_linked_list_initialize(NexusDataLinkedList *list);
/*
Summary: resets one detached intrusive node.
Errors: NULL node is a programmer error (asserts).
*/
extern void nexus_data_linked_list_node_initialize(NexusDataLinkedListNode *node);
/*
Summary: inserts node at the front of list.
Ownership: list borrows node; the containing object remains caller-owned.
Errors: NULL arguments or an already-linked node is a programmer error (asserts).
*/
extern void nexus_data_linked_list_push_front(NexusDataLinkedList *list, NexusDataLinkedListNode *node);
/*
Summary: inserts node at the back of list.
Ownership: list borrows node; the containing object remains caller-owned.
Errors: NULL arguments or an already-linked node is a programmer error (asserts).
*/
extern void nexus_data_linked_list_push_back(NexusDataLinkedList *list, NexusDataLinkedListNode *node);
/*
Summary: inserts node immediately before position.
Errors: NULL arguments, position from another list, or an already-linked node is a programmer error (asserts).
*/
extern void nexus_data_linked_list_insert_before(NexusDataLinkedList *list, NexusDataLinkedListNode *position, NexusDataLinkedListNode *node);
/*
Summary: inserts node immediately after position.
Errors: NULL arguments, position from another list, or an already-linked node is a programmer error (asserts).
*/
extern void nexus_data_linked_list_insert_after(NexusDataLinkedList *list, NexusDataLinkedListNode *position, NexusDataLinkedListNode *node);
/*
Summary: removes node and leaves it detached.
Errors: NULL arguments or a node owned by another list is a programmer error (asserts).
*/
extern void nexus_data_linked_list_remove(NexusDataLinkedList *list, NexusDataLinkedListNode *node);
/*
Summary: removes and returns the front node, or NULL when empty.
Returns: detached borrowed node, still owned by its containing object.
Errors: NULL list is a programmer error (asserts).
*/
extern NexusDataLinkedListNode *nexus_data_linked_list_pop_front(NexusDataLinkedList *list);
/*
Summary: removes and returns the back node, or NULL when empty.
Returns: detached borrowed node, still owned by its containing object.
Errors: NULL list is a programmer error (asserts).
*/
extern NexusDataLinkedListNode *nexus_data_linked_list_pop_back(NexusDataLinkedList *list);
/*
Summary: reverses list element order in place.
Description:
  Exchanges first/last and every node's next/previous links. Length
  and node ownership are unchanged; empty and single-node lists are no-ops.
Ownership:
  Borrows list and its caller-owned nodes; no payload is moved or freed.
Errors: NULL list is a programmer error (asserts).
Thread-safety: caller synchronizes access to list and its elements.
Complexity: O(n) time and O(1) extra storage; no allocation.
*/
extern void nexus_data_linked_list_reverse(NexusDataLinkedList *list);

/*
Summary: detaches every node and resets list length to zero.
Errors: NULL list is a programmer error (asserts).
*/
extern void nexus_data_linked_list_clear(NexusDataLinkedList *list);

/*
Summary: hashes one HashMap key into 64 well-distributed bits.
Description: equal keys must always produce equal hashes for the same seed.
Params: key  borrowed key_size bytes; seed is the map's selected hash seed
Returns: the key's 64-bit hash.
*/
typedef uint64 NexusDataHashFunction(const void *key, uint_large key_size, uint64 seed, void *user_data);

/*
Summary: decides whether two HashMap keys are equal.
Description: equality must be reflexive, symmetric, and transitive, and must
  agree with the selected NexusDataHashFunction.
Params: left/right  borrowed key_size-byte objects
Returns: TRUE when the keys are equal.
*/
typedef boolean NexusDataEqualityFunction(const void *left, const void *right, uint_large key_size, void *user_data);

/*
Summary: configuration for an owning flat or slabbed Swiss-table HashMap.
Description:
  Initialize with nexus_data_hashmap_config_initialize, then optionally replace
  hash/equality/user_data or provide a deterministic seed. Defaults compare raw
  key bytes, hash them with seeded XXH3, and obtain the seed from OS entropy.
*/
typedef struct NexusDataHashMapConfig {
  uint_large                 key_size;
  uint_large                 value_size;
  uint_large                 initial_capacity;
  uint64                     seed;
  boolean                    seed_is_set;
  NexusDataHashFunction     *hash;
  NexusDataEqualityFunction *equals;
  void                      *user_data;
} NexusDataHashMapConfig;

/*
Summary: initializes HashMap config with raw-byte hashing defaults.
Params: key_size/value_size  fixed byte widths copied into the config
Errors: NULL config or a zero key/value size is a programmer error (asserts).
*/
extern void nexus_data_hashmap_config_initialize(NexusDataHashMapConfig *config, uint_large key_size, uint_large value_size);

/*
Summary: creates an owning flat Swiss-table HashMap from config.
Description:
  Entries are inline and cache-local. Any insertion/reserve that rehashes
  invalidates all returned key/value pointers and the current enumerator.
Params: out_hashmap  out; owns the created map on success, NULL on failure
Errors:
  NULL/invalid config or out_hashmap is a programmer error. Returns an entropy
  error only when no deterministic seed was supplied and system seeding fails.
*/
extern NEXUS_NODISCARD NexusError *nexus_data_hashmap_create(const NexusDataHashMapConfig *config, NexusDataHashMap **out_hashmap);

/*
Summary: destroys HashMap and releases all owned metadata and entries.
Ownership: consumes hashmap; NULL is accepted as a no-op.
*/
extern void nexus_data_hashmap_destroy(NexusDataHashMap *hashmap);
/*
Summary: returns the number of key/value entries currently stored.
Returns: logical entry count.
Errors: NULL hashmap is a programmer error (asserts).
*/
extern uint_large nexus_data_hashmap_length_get(const NexusDataHashMap *hashmap);
/*
Summary: returns the number of entries allowed before the next growth.
Returns: current table capacity at the configured load factor.
Errors: NULL hashmap is a programmer error (asserts).
*/
extern uint_large nexus_data_hashmap_capacity_get(const NexusDataHashMap *hashmap);
/*
Summary: grows HashMap so at least minimum_capacity entries fit without rehash.
Errors: NULL hashmap or an unrepresentable allocation size is a programmer error (asserts).
*/
extern void nexus_data_hashmap_reserve(NexusDataHashMap *hashmap, uint_large minimum_capacity);
/*
Summary: finds key or creates an entry, returning its mutable value slot.
Params: key  borrowed key_size bytes; copied into a new entry when absent
Returns: mutable value slot invalidated by rehash or destroy.
Errors: NULL hashmap/key or an unrepresentable allocation size is a programmer error (asserts).
*/
extern void *nexus_data_hashmap_insert(NexusDataHashMap *hashmap, const void *key, boolean *out_inserted);
/*
Summary: inserts key/value or replaces the value for an equal key.
Params: key/value  borrowed fixed-width byte objects copied into the map
Errors: NULL hashmap/key/value or an unrepresentable allocation size is a programmer error (asserts).
*/
extern void nexus_data_hashmap_put(NexusDataHashMap *hashmap, const void *key, const void *value);
/*
Summary: returns a mutable value slot for key, or NULL when absent.
Returns: borrowed value pointer invalidated by rehash or destroy.
Ownership: pointer remains owned by hashmap.
Errors: NULL hashmap/key is a programmer error (asserts).
*/
extern void *nexus_data_hashmap_get(NexusDataHashMap *hashmap, const void *key);
/*
Summary: returns a const value slot for key, or NULL when absent.
Returns: borrowed value pointer invalidated by rehash or destroy.
Ownership: pointer remains owned by hashmap.
Errors: NULL hashmap/key is a programmer error (asserts).
*/
extern const void *nexus_data_hashmap_get_const(const NexusDataHashMap *hashmap, const void *key);
/*
Summary: copies key's value into out_value and reports whether key existed.
Params: out_value  out buffer of value_size bytes
Returns: TRUE when key existed and was copied.
Errors: NULL hashmap/key/out_value is a programmer error (asserts).
*/
extern boolean nexus_data_hashmap_get_copy(const NexusDataHashMap *hashmap, const void *key, void *out_value);
/*
Summary: reports whether HashMap contains an equal key.
Returns: TRUE when key exists.
Errors: NULL hashmap/key is a programmer error (asserts).
*/
extern boolean nexus_data_hashmap_contains(const NexusDataHashMap *hashmap, const void *key);
/*
Summary: removes key and optionally copies its value to out_value.
Params: out_value  optional out buffer of value_size bytes
Returns: TRUE when key existed and was removed.
Errors: NULL hashmap/key is a programmer error (asserts).
*/
extern boolean nexus_data_hashmap_remove(NexusDataHashMap *hashmap, const void *key, void *out_value);
/*
Summary: inserts or replaces a pointer value without copying its pointee.
Description: keys retain the ordinary fixed-width byte-key semantics.
Params: key  borrowed key_size bytes; copied on insertion
  pointer  stored verbatim; NULL is accepted
Ownership: pointees remain caller-owned; no pointed-to resource is released.
Errors: NULL hashmap/key, non-pointer-sized values, or allocation/capacity overflow asserts.
Thread-safety: caller synchronizes access to storage and pointees.
Complexity: O(key_size + 1) expected; O(n * key_size) worst case;
  may allocate on growth.
*/
extern void nexus_data_hashmap_put_pointer(NexusDataHashMap *hashmap, const void *key, const void *pointer);

/*
Summary: returns the pointer value stored for key.
Description: keys retain the ordinary fixed-width byte-key semantics.
  use contains or get_copy_pointer to distinguish absent keys from NULL values.
Params: key  borrowed key_size bytes; not retained
Returns: stored pointer value; NULL means absent key or stored NULL.
Ownership: pointees remain caller-owned; no pointed-to resource is released.
Errors: NULL hashmap/key or non-pointer-sized values asserts.
Thread-safety: caller synchronizes access to storage and pointees.
Complexity: O(key_size + 1) expected; O(n * key_size) worst case; no allocation.
*/
extern void *nexus_data_hashmap_get_pointer(const NexusDataHashMap *hashmap, const void *key);

/*
Summary: copies the stored pointer and reports whether key exists.
Description: keys retain the ordinary fixed-width byte-key semantics.
Params: key  borrowed key_size bytes; not retained
  out_pointer  required out; must not overlap map storage;
  left untouched when key is absent
Returns: TRUE when key exists, including a NULL value; FALSE when absent.
Ownership: pointees remain caller-owned; no pointed-to resource is released.
Errors: NULL hashmap/key/out_pointer or non-pointer-sized values asserts.
Thread-safety: caller synchronizes access to storage and pointees.
Complexity: O(key_size + 1) expected; O(n * key_size) worst case; no allocation.
*/
extern boolean nexus_data_hashmap_get_copy_pointer(const NexusDataHashMap *hashmap, const void *key, void **out_pointer);

/*
Summary: removes key and optionally copies its stored pointer.
Description: keys retain the ordinary fixed-width byte-key semantics.
Params: key  borrowed key_size bytes; not retained
  out_pointer  optional out; must not overlap map storage;
  left untouched when key is absent
Returns: TRUE when key was removed, including a NULL value; FALSE when absent.
Ownership: pointees remain caller-owned; no pointed-to resource is released.
Errors: NULL hashmap/key or non-pointer-sized values asserts.
Thread-safety: caller synchronizes access to storage and pointees.
Complexity: O(key_size + 1) expected; O(n * key_size) worst case; no allocation.
*/
extern boolean nexus_data_hashmap_remove_pointer(NexusDataHashMap *hashmap, const void *key, void **out_pointer);

/*
Summary: removes all entries while retaining allocated table storage.
Errors: NULL hashmap is a programmer error (asserts).
*/
extern void nexus_data_hashmap_clear(NexusDataHashMap *hashmap);

/*
Summary: stable-size cursor for allocation-free HashMap enumeration.
Description: initialize with nexus_data_hashmap_enumerator_initialize. Any
  structural map mutation invalidates the cursor.
*/
typedef struct NexusDataHashMapEnumerator {
  NexusDataHashMap *hashmap;
  uint_large        slot_index;
  uint64            version;
} NexusDataHashMapEnumerator;

/*
Summary: initializes an allocation-free cursor over current HashMap entries.
Ownership: enumerator borrows hashmap and is invalidated by structural mutation.
Errors: NULL hashmap/enumerator is a programmer error (asserts).
*/
extern void nexus_data_hashmap_enumerator_initialize(NexusDataHashMap *hashmap, NexusDataHashMapEnumerator *enumerator);
/*
Summary: advances cursor and returns borrowed key/value slots when available.
Params: out_key/out_value  optional outputs valid until structural map mutation
Returns: TRUE when another entry was returned.
Errors: NULL enumerator, detached map, or mutation during enumeration is a programmer error (asserts).
*/
extern boolean nexus_data_hashmap_enumerator_next(NexusDataHashMapEnumerator *enumerator, void **out_key, void **out_value);

/* ---------------------------------------------------------------------------- */
/* SLABBED DATA STRUCTURES                                                      */
/* ---------------------------------------------------------------------------- */

/*
Summary: default number of payload slots allocated in one slab.
Description: every slabbed create accepts slab_capacity; zero selects this
  default. Slabs never move and are retained until container destruction.
*/
#define NEXUS_DATA_SLAB_DEFAULT_CAPACITY 64U

/*
Summary: an owning indexed list with slabbed, address-stable element storage.
Description:
  All slabbed containers preserve pointers to each surviving element across
  growth, reserve, insertion, removal of other elements, sorting, and heap
  adjustment. Ordering metadata may move; element payloads never do. Indices
  may change when elements are reordered. Removed slots are reused; a pointer
  becomes invalid when its element is removed, on clear, or on destroy, even
  if its address is reused later. There are no contiguous data/linearize APIs.
  Append/push/insert copy bytes; pointed-to resources remain caller-owned.
  Payloads support ordinary C type alignment, not over-aligned objects.
  Input elements/keys/values may refer to live payloads in the same container.
  Copy-out buffers must not overlap any container-owned payload.
Ownership: owns its slabs and ordering metadata, not resources in elements.
Thread-safety: callers synchronize access to each container and its payloads.
See: adr/0008-slabbed-containers-preserve-element-identity
*/
typedef struct NexusDataSlabbedList NexusDataSlabbedList;
/*
Summary: an owning LIFO stack with slabbed, address-stable element storage.
Description: shares the lifetime/alignment rules of NexusDataSlabbedList.
Ownership: owns its slabs and ordering metadata, not resources in elements.
Thread-safety: callers synchronize access to the stack and its payloads.
*/
typedef struct NexusDataSlabbedStack NexusDataSlabbedStack;
/*
Summary: an owning FIFO queue with slabbed, address-stable element storage.
Description: shares the lifetime/alignment rules of NexusDataSlabbedList.
Ownership: owns its slabs and ordering metadata, not resources in elements.
Thread-safety: callers synchronize access to the queue and its payloads.
*/
typedef struct NexusDataSlabbedQueue NexusDataSlabbedQueue;
/*
Summary: an owning min-heap with slabbed, address-stable element storage.
Description:
  Shares the lifetime/alignment rules of NexusDataSlabbedList. Heap adjustment
  moves references rather than payloads. Do not mutate ordering fields of a
  live element through its pointer; this would violate the heap invariant.
Ownership: owns slabs/metadata; borrows compare and user_data until destroy.
Thread-safety: callers synchronize access to the queue and its payloads.
*/
typedef struct NexusDataSlabbedPriorityQueue NexusDataSlabbedPriorityQueue;
/*
Summary: an owning Swiss-table HashMap with slabbed, stable key/value storage.
Description:
  Shares the lifetime/alignment rules of NexusDataSlabbedList. Rehashing moves
  references, preserving both key and value addresses. Replacing an equal
  key's value writes in place and preserves its address. Keys are immutable
  while stored. Enumeration cursors still expire on structural mutation.
Ownership: owns keys/values by byte copy; borrows config callbacks/user_data.
Thread-safety: callers synchronize access to the map and its payloads.
*/
typedef struct NexusDataSlabbedHashMap NexusDataSlabbedHashMap;

/*
Summary: creates an owning slabbed list.
Description:
  Reserves payload and ordering storage for initial_capacity elements.
  slab_capacity is a slot count; zero selects NEXUS_DATA_SLAB_DEFAULT_CAPACITY.
Returns: owned list, initially empty.
Ownership: caller destroys the result with nexus_data_slabbed_list_destroy.
Errors:
  zero element_size, size/capacity overflow assert.
  Allocation failure follows the Nexus allocator failure policy.
Thread-safety: caller synchronizes access to the container and payloads.
Complexity: O(initial_capacity) time/storage, rounded to whole slabs.
*/
extern NexusDataSlabbedList *nexus_data_slabbed_list_create(uint_large element_size, uint_large initial_capacity, uint_large slab_capacity);

/*
Summary: destroys slabbed list and its storage.
Ownership: consumes list; NULL is a no-op. Invalidates all payload pointers.
Thread-safety: caller synchronizes access to the container and payloads.
Complexity: O(number of slabs) time.
*/
extern void nexus_data_slabbed_list_destroy(NexusDataSlabbedList *list);

/*
Summary: returns the number of live elements.
Returns: current element count.
Errors: NULL list asserts.
Thread-safety: caller synchronizes access to the container and payloads.
*/
extern uint_large nexus_data_slabbed_list_length_get(const NexusDataSlabbedList *list);

/*
Summary: returns current capacity without allocating more storage.
Description: The smaller of payload-slot and ordering-directory capacity.
Returns: number of elements that fit using allocated storage.
Errors: NULL list asserts.
Thread-safety: caller synchronizes access to the container and payloads.
*/
extern uint_large nexus_data_slabbed_list_capacity_get(const NexusDataSlabbedList *list);

/*
Summary: reserves space for at least minimum_capacity live elements.
Description: Preserves every live element pointer; never shrinks storage.
Errors: NULL list or unrepresentable allocation/capacity asserts.
Thread-safety: caller synchronizes access to the container and payloads.
Complexity: O(new payload slots + current length) worst-case time.
*/
extern void nexus_data_slabbed_list_reserve(NexusDataSlabbedList *list, uint_large minimum_capacity);

/*
Summary: returns the element at logical index.
Returns: borrowed payload pointer; valid until its removal/clear/destroy.
Ownership: payload remains list-owned.
Errors: NULL list or index outside length asserts.
Thread-safety: caller synchronizes access to the container and payloads.
*/
extern void *nexus_data_slabbed_list_at(NexusDataSlabbedList *list, uint_large index);

/*
Summary: returns the element at logical index.
Returns: const borrowed payload pointer; valid until its removal/clear/destroy.
Ownership: payload remains list-owned.
Errors: NULL list or index outside length asserts.
Thread-safety: caller synchronizes access to the container and payloads.
*/
extern const void *nexus_data_slabbed_list_at_const(const NexusDataSlabbedList *list, uint_large index);

/*
Summary: appends a slot without initializing its payload.
Description: Caller must initialize all element bytes before reading/sorting the slot.
Returns: borrowed pointer valid until its removal/clear/destroy.
Ownership: slot remains list-owned.
Errors: NULL list or length/allocation overflow asserts.
Thread-safety: caller synchronizes access to the container and payloads.
Complexity: O(1) amortized; no allocation within reserved capacity.
*/
extern void *nexus_data_slabbed_list_append_uninitialized(NexusDataSlabbedList *list);

/*
Summary: appends a pointer value to a pointer-sized slabbed list.
Params: pointer  borrowed pointer value; NULL is accepted
Returns: borrowed pointer to the stored pointer slot, with normal slot lifetime.
Ownership: pointee stays caller-owned.
Errors: NULL list, non-pointer-sized elements, or length/allocation overflow asserts.
Thread-safety: caller synchronizes access to the container and payloads.
Complexity: O(1) amortized.
*/
extern void *nexus_data_slabbed_list_append_pointer(NexusDataSlabbedList *list, const void *pointer);

/*
Summary: returns the pointer value stored at index.
Returns: stored pointer value; its pointee lifetime is caller-controlled.
Ownership: pointee stays caller-owned.
Errors: NULL list, non-pointer-sized elements, or index outside length asserts.
Thread-safety: caller synchronizes access to the container and payloads.
*/
extern void *nexus_data_slabbed_list_pointer_at(const NexusDataSlabbedList *list, uint_large index);

/*
Summary: copies and inserts one element.
Params: element  borrowed element_size bytes; may be a live payload in this container
Returns: borrowed pointer to that element, valid until its removal/clear/destroy.
Ownership: payload becomes list-owned; preserves all existing payload pointers.
Errors: NULL list/element or length/allocation overflow asserts.
Thread-safety: caller synchronizes access to the container and payloads.
Complexity: O(1) amortized; no allocation within reserved capacity.
*/
extern void *nexus_data_slabbed_list_append(NexusDataSlabbedList *list, const void *element);

/*
Summary: copies element into list before logical index.
Description: Moves references only; preserves pointers to every existing element.
Params: element  borrowed element_size bytes; may be a live list payload
Ownership: new payload is list-owned.
Errors: NULL list/element, index greater than length, or size/length overflow asserts.
Thread-safety: caller synchronizes access to the container and payloads.
Complexity: O(length - index) time plus amortized growth.
*/
extern void nexus_data_slabbed_list_insert(NexusDataSlabbedList *list, uint_large index, const void *element);

/*
Summary: removes the element at index and reuses its slot later.
Params: out_element  optional element_size-byte output; must not overlap owned payloads
Ownership: only the removed element pointer is invalidated.
Errors: NULL list or index outside length asserts.
Thread-safety: caller synchronizes access to the container and payloads.
Complexity: O(length - index) time; no allocation.
*/
extern void nexus_data_slabbed_list_remove(NexusDataSlabbedList *list, uint_large index, void *out_element);

/*
Summary: removes the last element.
Params: out_element  optional element_size-byte output; must not overlap owned payloads
Returns: TRUE if an element was removed; FALSE leaves output untouched.
Ownership: only the removed element pointer is invalidated.
Errors: NULL list asserts.
Thread-safety: caller synchronizes access to the container and payloads.
Complexity: O(1) time; no allocation.
*/
extern boolean nexus_data_slabbed_list_pop(NexusDataSlabbedList *list, void *out_element);

/*
Summary: inserts pointer itself before index in a pointer-sized list.
Params: pointer  stored verbatim; NULL is accepted
Ownership: pointees remain caller-owned; no pointed-to resource is released.
Errors: NULL list, non-pointer-sized elements, invalid insertion index,
  or allocation/capacity overflow asserts.
Thread-safety: caller synchronizes access to storage and pointees.
Complexity: O(n) metadata shifts; may allocate on growth.
*/
extern void nexus_data_slabbed_list_insert_pointer(NexusDataSlabbedList *list, uint_large index, const void *pointer);

/*
Summary: removes index and optionally copies its stored pointer.
Params: out_pointer  optional out; must not overlap list storage
Ownership: pointees remain caller-owned; no pointed-to resource is released.
Errors: NULL list, non-pointer-sized elements, or index outside length asserts.
Thread-safety: caller synchronizes access to storage and pointees.
Complexity: O(n) shifts; no allocation.
*/
extern void nexus_data_slabbed_list_remove_pointer(NexusDataSlabbedList *list, uint_large index, void **out_pointer);

/*
Summary: removes the last pointer and reports whether one existed.
Params: out_pointer  optional out; must not overlap list storage;
  left untouched when empty
Returns: TRUE on removal, including a stored NULL; FALSE when empty.
Ownership: pointees remain caller-owned; no pointed-to resource is released.
Errors: NULL list or non-pointer-sized elements asserts.
Thread-safety: caller synchronizes access to storage and pointees.
Complexity: O(1) time; no allocation.
*/
extern boolean nexus_data_slabbed_list_pop_pointer(NexusDataSlabbedList *list, void **out_pointer);

/*
Summary: reverses list element order in place.
Description:
  Reverses logical index order. Length, capacity, and the storage
  allocation are unchanged; empty and single-element lists are no-ops.
  Only ordering references move; payload bytes and addresses never move.
Ownership:
  Borrows list; all surviving element pointers retain their identity.
Errors: NULL list is a programmer error (asserts).
Thread-safety: caller synchronizes access to list and its elements.
Complexity: O(n) pointer swaps and O(1) extra storage; no allocation.
*/
extern void nexus_data_slabbed_list_reverse(NexusDataSlabbedList *list);

/*
Summary: removes every element while retaining slabs and metadata.
Ownership: invalidates all element pointers; releases no pointed-to resources.
Errors: NULL list asserts.
Thread-safety: caller synchronizes access to the container and payloads.
Complexity: O(length) time; no allocation.
*/
extern void nexus_data_slabbed_list_clear(NexusDataSlabbedList *list);

/*
Summary: sorts logical order without moving payloads.
Description:
  The sort is not stable for equal keys. Every element pointer survives.
  The callback receives payloads, not references; it must not mutate the list.
Ownership: compare and user_data are borrowed during this call.
Errors: NULL list/compare asserts.
Thread-safety: caller synchronizes access to the container and payloads.
Complexity: O(n log n) time, O(1) auxiliary storage; no allocation.
*/
extern void nexus_data_slabbed_list_sort(NexusDataSlabbedList *list, NexusDataCompareFunction *compare, void *user_data);

/*
Summary: creates an owning slabbed stack.
Description:
  Reserves payload and ordering storage for initial_capacity elements.
  slab_capacity is a slot count; zero selects NEXUS_DATA_SLAB_DEFAULT_CAPACITY.
Returns: owned stack, initially empty.
Ownership: caller destroys the result with nexus_data_slabbed_stack_destroy.
Errors:
  zero element_size, size/capacity overflow assert.
  Allocation failure follows the Nexus allocator failure policy.
Thread-safety: caller synchronizes access to the container and payloads.
Complexity: O(initial_capacity) time/storage, rounded to whole slabs.
*/
extern NexusDataSlabbedStack *nexus_data_slabbed_stack_create(uint_large element_size, uint_large initial_capacity, uint_large slab_capacity);

/*
Summary: destroys slabbed stack and its storage.
Ownership: consumes stack; NULL is a no-op. Invalidates all payload pointers.
Thread-safety: caller synchronizes access to the container and payloads.
Complexity: O(number of slabs) time.
*/
extern void nexus_data_slabbed_stack_destroy(NexusDataSlabbedStack *stack);

/*
Summary: returns the number of live elements.
Returns: current element count.
Errors: NULL stack asserts.
Thread-safety: caller synchronizes access to the container and payloads.
*/
extern uint_large nexus_data_slabbed_stack_length_get(const NexusDataSlabbedStack *stack);

/*
Summary: returns current capacity without allocating more storage.
Description: The smaller of payload-slot and ordering-directory capacity.
Returns: number of elements that fit using allocated storage.
Errors: NULL stack asserts.
Thread-safety: caller synchronizes access to the container and payloads.
*/
extern uint_large nexus_data_slabbed_stack_capacity_get(const NexusDataSlabbedStack *stack);

/*
Summary: reserves space for at least minimum_capacity live elements.
Description: Preserves every live element pointer; never shrinks storage.
Errors: NULL stack or unrepresentable allocation/capacity asserts.
Thread-safety: caller synchronizes access to the container and payloads.
Complexity: O(new payload slots + current length) worst-case time.
*/
extern void nexus_data_slabbed_stack_reserve(NexusDataSlabbedStack *stack, uint_large minimum_capacity);

/*
Summary: copies and inserts one element.
Params: element  borrowed element_size bytes; may be a live payload in this container
Returns: borrowed pointer to that element, valid until its removal/clear/destroy.
Ownership: payload becomes stack-owned; preserves all existing payload pointers.
Errors: NULL stack/element or length/allocation overflow asserts.
Thread-safety: caller synchronizes access to the container and payloads.
Complexity: O(1) amortized; no allocation within reserved capacity.
*/
extern void *nexus_data_slabbed_stack_push(NexusDataSlabbedStack *stack, const void *element);

/*
Summary: removes the last element.
Params: out_element  optional element_size-byte output; must not overlap owned payloads
Returns: TRUE if an element was removed; FALSE leaves output untouched.
Ownership: only the removed element pointer is invalidated.
Errors: NULL stack asserts.
Thread-safety: caller synchronizes access to the container and payloads.
Complexity: O(1) time; no allocation.
*/
extern boolean nexus_data_slabbed_stack_pop(NexusDataSlabbedStack *stack, void *out_element);

/*
Summary: returns the top element.
Returns: borrowed payload pointer or NULL if empty; normal payload lifetime.
Ownership: payload remains stack-owned.
Errors: NULL stack asserts.
Thread-safety: caller synchronizes access to the container and payloads.
*/
extern void *nexus_data_slabbed_stack_peek(NexusDataSlabbedStack *stack);

/*
Summary: pushes pointer itself into a pointer-sized container.
Params: pointer  stored verbatim; NULL is accepted
Returns: borrowed pointer slot; valid until removal, clear, or destroy.
Ownership: pointees remain caller-owned; no pointed-to resource is released.
Errors: NULL stack, non-pointer-sized elements, or allocation/capacity overflow asserts.
Thread-safety: caller synchronizes access to storage and pointees.
Complexity: O(1) amortized; may allocate on growth.
*/
extern void *nexus_data_slabbed_stack_push_pointer(NexusDataSlabbedStack *stack, const void *pointer);

/*
Summary: pops the next pointer and reports whether one existed.
Params: out_pointer  optional out; must not overlap stack storage;
  left untouched when empty
Returns: TRUE on removal, including a stored NULL; FALSE when empty.
Ownership: pointees remain caller-owned; no pointed-to resource is released.
Errors: NULL stack or non-pointer-sized elements asserts.
Thread-safety: caller synchronizes access to storage and pointees.
Complexity: O(1) time; no allocation.
*/
extern boolean nexus_data_slabbed_stack_pop_pointer(NexusDataSlabbedStack *stack, void **out_pointer);

/*
Summary: returns the next stored pointer without removing it.
Description: use length_get or pop_pointer to distinguish an empty container
  from a stored NULL.
Returns: stored pointer value; NULL means empty or a stored NULL.
Ownership: pointees remain caller-owned; no pointed-to resource is released.
Errors: NULL stack or non-pointer-sized elements asserts.
Thread-safety: caller synchronizes access to storage and pointees.
Complexity: O(1) time; no allocation.
*/
extern void *nexus_data_slabbed_stack_peek_pointer(NexusDataSlabbedStack *stack);

/*
Summary: reverses stack element order in place.
Description:
  Reverses the current pop order; the bottom element becomes the top.
  Length, capacity, and the storage allocation are unchanged.
  Only ordering references move; payload bytes and addresses never move.
Ownership:
  Borrows stack; all surviving element pointers retain their identity.
Errors: NULL stack is a programmer error (asserts).
Thread-safety: caller synchronizes access to stack and its elements.
Complexity: O(n) pointer swaps and O(1) extra storage; no allocation.
*/
extern void nexus_data_slabbed_stack_reverse(NexusDataSlabbedStack *stack);

/*
Summary: removes every element while retaining slabs and metadata.
Ownership: invalidates all element pointers; releases no pointed-to resources.
Errors: NULL stack asserts.
Thread-safety: caller synchronizes access to the container and payloads.
Complexity: O(length) time; no allocation.
*/
extern void nexus_data_slabbed_stack_clear(NexusDataSlabbedStack *stack);

/*
Summary: creates an owning slabbed queue.
Description:
  Reserves payload and ordering storage for initial_capacity elements.
  slab_capacity is a slot count; zero selects NEXUS_DATA_SLAB_DEFAULT_CAPACITY.
Returns: owned queue, initially empty.
Ownership: caller destroys the result with nexus_data_slabbed_queue_destroy.
Errors:
  zero element_size, size/capacity overflow assert.
  Allocation failure follows the Nexus allocator failure policy.
Thread-safety: caller synchronizes access to the container and payloads.
Complexity: O(initial_capacity) time/storage, rounded to whole slabs.
*/
extern NexusDataSlabbedQueue *nexus_data_slabbed_queue_create(uint_large element_size, uint_large initial_capacity, uint_large slab_capacity);

/*
Summary: destroys slabbed queue and its storage.
Ownership: consumes queue; NULL is a no-op. Invalidates all payload pointers.
Thread-safety: caller synchronizes access to the container and payloads.
Complexity: O(number of slabs) time.
*/
extern void nexus_data_slabbed_queue_destroy(NexusDataSlabbedQueue *queue);

/*
Summary: returns the number of live elements.
Returns: current element count.
Errors: NULL queue asserts.
Thread-safety: caller synchronizes access to the container and payloads.
*/
extern uint_large nexus_data_slabbed_queue_length_get(const NexusDataSlabbedQueue *queue);

/*
Summary: returns current capacity without allocating more storage.
Description: The smaller of payload-slot and ordering-directory capacity.
Returns: number of elements that fit using allocated storage.
Errors: NULL queue asserts.
Thread-safety: caller synchronizes access to the container and payloads.
*/
extern uint_large nexus_data_slabbed_queue_capacity_get(const NexusDataSlabbedQueue *queue);

/*
Summary: reserves space for at least minimum_capacity live elements.
Description: Preserves every live element pointer; never shrinks storage.
Errors: NULL queue or unrepresentable allocation/capacity asserts.
Thread-safety: caller synchronizes access to the container and payloads.
Complexity: O(new payload slots + current length) worst-case time.
*/
extern void nexus_data_slabbed_queue_reserve(NexusDataSlabbedQueue *queue, uint_large minimum_capacity);

/*
Summary: copies and inserts one element.
Params: element  borrowed element_size bytes; may be a live payload in this container
Returns: borrowed pointer to that element, valid until its removal/clear/destroy.
Ownership: payload becomes queue-owned; preserves all existing payload pointers.
Errors: NULL queue/element or length/allocation overflow asserts.
Thread-safety: caller synchronizes access to the container and payloads.
Complexity: O(1) amortized; no allocation within reserved capacity.
*/
extern void *nexus_data_slabbed_queue_push(NexusDataSlabbedQueue *queue, const void *element);

/*
Summary: removes the oldest element.
Params: out_element  optional element_size-byte output; must not overlap owned payloads
Returns: TRUE if an element was removed; FALSE leaves output untouched.
Ownership: only the removed element pointer is invalidated.
Errors: NULL queue asserts.
Thread-safety: caller synchronizes access to the container and payloads.
Complexity: O(1) time; no allocation.
*/
extern boolean nexus_data_slabbed_queue_pop(NexusDataSlabbedQueue *queue, void *out_element);

/*
Summary: returns the oldest element.
Returns: borrowed payload pointer or NULL if empty; normal payload lifetime.
Ownership: payload remains queue-owned.
Errors: NULL queue asserts.
Thread-safety: caller synchronizes access to the container and payloads.
*/
extern void *nexus_data_slabbed_queue_peek(NexusDataSlabbedQueue *queue);

/*
Summary: enqueues a pointer value in a pointer-sized queue.
Description: stores the pointer itself, without copying its pointee.
Params: pointer  stored verbatim; NULL is accepted
Returns: borrowed pointer to the stored pointer slot, valid until its removal, clear, or destroy.
Ownership: queue owns the slot; the pointee stays caller-owned.
Errors: NULL queue, non-pointer-sized elements, or length/allocation overflow asserts.
Thread-safety: caller synchronizes access to the container and pointees.
Complexity: O(1) amortized; no allocation within reserved capacity.
*/
extern void *nexus_data_slabbed_queue_push_pointer(NexusDataSlabbedQueue *queue, const void *pointer);

/*
Summary: dequeues the oldest pointer and reports whether one existed.
Description: TRUE also reports success when the dequeued pointer is NULL.
Params: out_pointer  optional out; receives the stored pointer on success;
  must not overlap queue-owned storage; left untouched when empty
Returns: TRUE if an element was removed; FALSE when empty.
Ownership: pointee stays caller-owned; no pointed-to resource is released.
Errors: NULL queue or non-pointer-sized elements asserts.
Thread-safety: caller synchronizes access to the container and pointees.
Complexity: O(1) time; no allocation.
*/
extern boolean nexus_data_slabbed_queue_pop_pointer(NexusDataSlabbedQueue *queue, void **out_pointer);

/*
Summary: returns the oldest stored pointer without removing it.
Description: returns the pointer value, not a pointer to its queue slot.
  NULL means either an empty queue or a stored NULL; use length_get or
  pop_pointer when those cases must be distinguished.
Returns: stored pointer value, or NULL when empty.
Ownership: pointee stays caller-owned; its lifetime is caller-controlled.
Errors: NULL queue or non-pointer-sized elements asserts.
Thread-safety: caller synchronizes access to the container and pointees.
Complexity: O(1) time; no allocation.
*/
extern void *nexus_data_slabbed_queue_peek_pointer(NexusDataSlabbedQueue *queue);

/*
Summary: reverses queue element order in place.
Description:
  Reverses the current pop order, including wrapped live elements.
  Length, capacity, and the storage allocation are unchanged.
  Only ordering references move; payload bytes and addresses never move.
Ownership:
  Borrows queue; all surviving element pointers retain their identity.
Errors: NULL queue is a programmer error (asserts).
Thread-safety: caller synchronizes access to queue and its elements.
Complexity: O(n) pointer swaps and O(1) extra storage; no allocation.
*/
extern void nexus_data_slabbed_queue_reverse(NexusDataSlabbedQueue *queue);

/*
Summary: removes every element while retaining slabs and metadata.
Ownership: invalidates all element pointers; releases no pointed-to resources.
Errors: NULL queue asserts.
Thread-safety: caller synchronizes access to the container and payloads.
Complexity: O(length) time; no allocation.
*/
extern void nexus_data_slabbed_queue_clear(NexusDataSlabbedQueue *queue);

/*
Summary: creates an owning slabbed priority queue.
Description:
  Reserves payload and ordering storage for initial_capacity elements.
  slab_capacity is a slot count; zero selects NEXUS_DATA_SLAB_DEFAULT_CAPACITY.
Params: compare/user_data  borrowed until destroy; compare orders lowest first
Returns: owned queue, initially empty.
Ownership: caller destroys the result with nexus_data_slabbed_priority_queue_destroy.
Errors:
  zero element_size, NULL compare, size/capacity overflow assert.
  Allocation failure follows the Nexus allocator failure policy.
Thread-safety: caller synchronizes access to the container and payloads.
Complexity: O(initial_capacity) time/storage, rounded to whole slabs.
*/
extern NexusDataSlabbedPriorityQueue *nexus_data_slabbed_priority_queue_create(uint_large element_size, uint_large initial_capacity,
                                                                               uint_large slab_capacity, NexusDataCompareFunction *compare,
                                                                               void *user_data);

/*
Summary: destroys slabbed priority queue and its storage.
Ownership: consumes queue; NULL is a no-op. Invalidates all payload pointers.
Thread-safety: caller synchronizes access to the container and payloads.
Complexity: O(number of slabs) time.
*/
extern void nexus_data_slabbed_priority_queue_destroy(NexusDataSlabbedPriorityQueue *queue);

/*
Summary: returns the number of live elements.
Returns: current element count.
Errors: NULL queue asserts.
Thread-safety: caller synchronizes access to the container and payloads.
*/
extern uint_large nexus_data_slabbed_priority_queue_length_get(const NexusDataSlabbedPriorityQueue *queue);

/*
Summary: returns current capacity without allocating more storage.
Description: The smaller of payload-slot and ordering-directory capacity.
Returns: number of elements that fit using allocated storage.
Errors: NULL queue asserts.
Thread-safety: caller synchronizes access to the container and payloads.
*/
extern uint_large nexus_data_slabbed_priority_queue_capacity_get(const NexusDataSlabbedPriorityQueue *queue);

/*
Summary: reserves space for at least minimum_capacity live elements.
Description: Preserves every live element pointer; never shrinks storage.
Errors: NULL queue or unrepresentable allocation/capacity asserts.
Thread-safety: caller synchronizes access to the container and payloads.
Complexity: O(new payload slots + current length) worst-case time.
*/
extern void nexus_data_slabbed_priority_queue_reserve(NexusDataSlabbedPriorityQueue *queue, uint_large minimum_capacity);

/*
Summary: copies and inserts one element.
Params: element  borrowed element_size bytes; may be a live payload in this container
Returns: borrowed pointer to that element, valid until its removal/clear/destroy.
Ownership: payload becomes queue-owned; preserves all existing payload pointers.
Errors: NULL queue/element or length/allocation overflow asserts.
Thread-safety: caller synchronizes access to the container and payloads.
Complexity: O(log n) time plus amortized growth.
*/
extern void *nexus_data_slabbed_priority_queue_push(NexusDataSlabbedPriorityQueue *queue, const void *element);

/*
Summary: removes the minimum element.
Params: out_element  optional element_size-byte output; must not overlap owned payloads
Returns: TRUE if an element was removed; FALSE leaves output untouched.
Ownership: only the removed element pointer is invalidated.
Errors: NULL queue asserts.
Thread-safety: caller synchronizes access to the container and payloads.
Complexity: O(log n) time; no allocation.
*/
extern boolean nexus_data_slabbed_priority_queue_pop(NexusDataSlabbedPriorityQueue *queue, void *out_element);

/*
Summary: returns the minimum element.
Returns: borrowed payload pointer or NULL if empty; normal payload lifetime.
Ownership: payload remains queue-owned.
Errors: NULL queue asserts.
Thread-safety: caller synchronizes access to the container and payloads.
*/
extern void *nexus_data_slabbed_priority_queue_peek(NexusDataSlabbedPriorityQueue *queue);

/*
Summary: pushes pointer itself into a pointer-sized container.
Description: comparator receives pointer slots, as with ordinary push;
  it must dereference them to compare pointees.
Params: pointer  stored verbatim; NULL is accepted
Returns: borrowed pointer slot; valid until removal, clear, or destroy.
Ownership: pointees remain caller-owned; no pointed-to resource is released.
Errors: NULL queue, non-pointer-sized elements, or allocation/capacity overflow asserts.
Thread-safety: caller synchronizes access to storage and pointees.
Complexity: O(log n) time; may allocate on growth.
*/
extern void *nexus_data_slabbed_priority_queue_push_pointer(NexusDataSlabbedPriorityQueue *queue, const void *pointer);

/*
Summary: pops the next pointer and reports whether one existed.
Params: out_pointer  optional out; must not overlap queue storage;
  left untouched when empty
Returns: TRUE on removal, including a stored NULL; FALSE when empty.
Ownership: pointees remain caller-owned; no pointed-to resource is released.
Errors: NULL queue or non-pointer-sized elements asserts.
Thread-safety: caller synchronizes access to storage and pointees.
Complexity: O(log n) time; no allocation.
*/
extern boolean nexus_data_slabbed_priority_queue_pop_pointer(NexusDataSlabbedPriorityQueue *queue, void **out_pointer);

/*
Summary: returns the next stored pointer without removing it.
Description: use length_get or pop_pointer to distinguish an empty container
  from a stored NULL.
Returns: stored pointer value; NULL means empty or a stored NULL.
Ownership: pointees remain caller-owned; no pointed-to resource is released.
Errors: NULL queue or non-pointer-sized elements asserts.
Thread-safety: caller synchronizes access to storage and pointees.
Complexity: O(1) time; no allocation.
*/
extern void *nexus_data_slabbed_priority_queue_peek_pointer(NexusDataSlabbedPriorityQueue *queue);

/*
Summary: removes every element while retaining slabs and metadata.
Ownership: invalidates all element pointers; releases no pointed-to resources.
Errors: NULL queue asserts.
Thread-safety: caller synchronizes access to the container and payloads.
Complexity: O(length) time; no allocation.
*/
extern void nexus_data_slabbed_priority_queue_clear(NexusDataSlabbedPriorityQueue *queue);

/*
Summary: cursor over a slabbed HashMap's current entry references.
Description: structural mutation invalidates this cursor, even though pointers
  already returned still follow their individual key/value lifetimes.
Ownership: borrows hashmap until structural mutation or destruction.
*/
typedef struct NexusDataSlabbedHashMapEnumerator {
  NexusDataSlabbedHashMap *hashmap;
  uint_large               slot_index;
  uint64                   version;
} NexusDataSlabbedHashMapEnumerator;

/*
Summary: creates a slabbed HashMap using the existing HashMap config.
Description:
  Copies config. Zero slab_capacity selects NEXUS_DATA_SLAB_DEFAULT_CAPACITY.
  Reserves config.initial_capacity payload slots and table capacity.
Params:
  config  initialized with nexus_data_hashmap_config_initialize
  out_hashmap  out; owned map on success, NULL on entropy failure
Ownership: caller destroys the map; callbacks/user_data are borrowed until destroy.
Errors:
  NULL config/out_hashmap, zero key/value sizes, NULL hash/equals,
  or unrepresentable storage assert. An entropy error is returned only when
  no deterministic seed was supplied and OS seeding fails.
Thread-safety: caller synchronizes access to the container and payloads.
Complexity: O(initial_capacity) time/storage, rounded to whole slabs/table groups.
*/
extern NEXUS_NODISCARD NexusError *nexus_data_slabbed_hashmap_create(const NexusDataHashMapConfig *config, uint_large slab_capacity,
                                                                     NexusDataSlabbedHashMap **out_hashmap);

/*
Summary: destroys the map and all slab/table storage.
Ownership: consumes hashmap; NULL is a no-op; all entry pointers expire.
Thread-safety: caller synchronizes access to the container and payloads.
Complexity: O(number of slabs) time.
*/
extern void nexus_data_slabbed_hashmap_destroy(NexusDataSlabbedHashMap *hashmap);

/*
Summary: returns the number of live key/value entries.
Returns: current entry count.
Errors: NULL hashmap asserts.
Thread-safety: caller synchronizes access to the container and payloads.
*/
extern uint_large nexus_data_slabbed_hashmap_length_get(const NexusDataSlabbedHashMap *hashmap);

/*
Summary: returns entry capacity using already allocated storage.
Returns: minimum of slab-slot capacity and table load-limit capacity.
Errors: NULL hashmap asserts.
Thread-safety: caller synchronizes access to the container and payloads.
*/
extern uint_large nexus_data_slabbed_hashmap_capacity_get(const NexusDataSlabbedHashMap *hashmap);

/*
Summary: reserves slab slots and table capacity for minimum_capacity.
Description:
  Rehashing preserves all key/value pointers; enumeration cursors expire
  if the table is rebuilt. Tombstones can still trigger later table cleanup.
Errors: NULL hashmap or unrepresentable storage asserts.
Thread-safety: caller synchronizes access to the container and payloads.
Complexity: O(new slab slots + table slots) time.
*/
extern void nexus_data_slabbed_hashmap_reserve(NexusDataSlabbedHashMap *hashmap, uint_large minimum_capacity);

/*
Summary: finds or inserts key and returns its stable value slot.
Description:
  A new value is uninitialized and must be initialized before reading it.
  An existing value is left unchanged. Preserves every surviving entry pointer.
Params:
  key  borrowed key_size bytes, copied only on insertion
  out_inserted  optional out; TRUE on insertion, FALSE for an existing key
Returns: borrowed value pointer valid until its key is removed/clear/destroy.
Ownership: map owns copied key/value bytes; stored keys must not be modified.
Errors: NULL hashmap/key or length/allocation overflow asserts.
Thread-safety: caller synchronizes access to the container and payloads.
Complexity: expected O(1) amortized; collisions may require O(n) probes.
*/
extern void *nexus_data_slabbed_hashmap_insert(NexusDataSlabbedHashMap *hashmap, const void *key, boolean *out_inserted);

/*
Summary: inserts key/value or replaces an existing value in place.
Params: key/value  borrowed byte objects; may be live payloads in this map
Ownership: map owns copied bytes; replacement preserves key/value addresses.
Errors: NULL hashmap/key/value or length/allocation overflow asserts.
Thread-safety: caller synchronizes access to the container and payloads.
Complexity: expected O(1) amortized; collisions may require O(n) probes.
*/
extern void nexus_data_slabbed_hashmap_put(NexusDataSlabbedHashMap *hashmap, const void *key, const void *value);

/*
Summary: returns the value for an equal key.
Params: key  borrowed key_size bytes
Returns: borrowed value pointer or NULL; normal entry lifetime.
Ownership: value remains map-owned.
Errors: NULL hashmap/key asserts.
Thread-safety: caller synchronizes access to the container and payloads.
Complexity: expected O(1); worst-case O(n).
*/
extern void *nexus_data_slabbed_hashmap_get(NexusDataSlabbedHashMap *hashmap, const void *key);

/*
Summary: returns the value for an equal key.
Params: key  borrowed key_size bytes
Returns: const borrowed value pointer or NULL; normal entry lifetime.
Ownership: value remains map-owned.
Errors: NULL hashmap/key asserts.
Thread-safety: caller synchronizes access to the container and payloads.
Complexity: expected O(1); worst-case O(n).
*/
extern const void *nexus_data_slabbed_hashmap_get_const(const NexusDataSlabbedHashMap *hashmap, const void *key);

/*
Summary: copies the value for an equal key.
Params:
  key  borrowed key_size bytes
  out_value  value_size-byte output; must not overlap owned payloads
Returns: TRUE when found; FALSE leaves output untouched.
Ownership: stored value remains map-owned.
Errors: NULL hashmap/key/out_value asserts.
Thread-safety: caller synchronizes access to the container and payloads.
Complexity: expected O(1); worst-case O(n).
*/
extern boolean nexus_data_slabbed_hashmap_get_copy(const NexusDataSlabbedHashMap *hashmap, const void *key, void *out_value);

/*
Summary: reports whether an equal key exists.
Params: key  borrowed key_size bytes
Returns: TRUE if found.
Errors: NULL hashmap/key asserts.
Thread-safety: caller synchronizes access to the container and payloads.
Complexity: expected O(1); worst-case O(n).
*/
extern boolean nexus_data_slabbed_hashmap_contains(const NexusDataSlabbedHashMap *hashmap, const void *key);

/*
Summary: removes key and returns its slab slot for reuse.
Params:
  key  borrowed key_size bytes
  out_value  optional value_size-byte output; must not overlap owned payloads
Returns: TRUE when removed; FALSE leaves output untouched.
Ownership: only this entry pointers expire; surviving entry pointers are preserved.
Errors: NULL hashmap/key asserts.
Thread-safety: caller synchronizes access to the container and payloads.
Complexity: expected O(1); worst-case O(n); no allocation.
*/
extern boolean nexus_data_slabbed_hashmap_remove(NexusDataSlabbedHashMap *hashmap, const void *key, void *out_value);

/*
Summary: inserts or replaces a pointer value without copying its pointee.
Description: keys retain the ordinary fixed-width byte-key semantics.
Params: key  borrowed key_size bytes; copied on insertion
  pointer  stored verbatim; NULL is accepted
Ownership: pointees remain caller-owned; no pointed-to resource is released.
Errors: NULL hashmap/key, non-pointer-sized values, or allocation/capacity overflow asserts.
Thread-safety: caller synchronizes access to storage and pointees.
Complexity: O(key_size + 1) expected; O(n * key_size) worst case;
  may allocate on growth.
*/
extern void nexus_data_slabbed_hashmap_put_pointer(NexusDataSlabbedHashMap *hashmap, const void *key, const void *pointer);

/*
Summary: returns the pointer value stored for key.
Description: keys retain the ordinary fixed-width byte-key semantics.
  use contains or get_copy_pointer to distinguish absent keys from NULL values.
Params: key  borrowed key_size bytes; not retained
Returns: stored pointer value; NULL means absent key or stored NULL.
Ownership: pointees remain caller-owned; no pointed-to resource is released.
Errors: NULL hashmap/key or non-pointer-sized values asserts.
Thread-safety: caller synchronizes access to storage and pointees.
Complexity: O(key_size + 1) expected; O(n * key_size) worst case; no allocation.
*/
extern void *nexus_data_slabbed_hashmap_get_pointer(const NexusDataSlabbedHashMap *hashmap, const void *key);

/*
Summary: copies the stored pointer and reports whether key exists.
Description: keys retain the ordinary fixed-width byte-key semantics.
Params: key  borrowed key_size bytes; not retained
  out_pointer  required out; must not overlap map storage;
  left untouched when key is absent
Returns: TRUE when key exists, including a NULL value; FALSE when absent.
Ownership: pointees remain caller-owned; no pointed-to resource is released.
Errors: NULL hashmap/key/out_pointer or non-pointer-sized values asserts.
Thread-safety: caller synchronizes access to storage and pointees.
Complexity: O(key_size + 1) expected; O(n * key_size) worst case; no allocation.
*/
extern boolean nexus_data_slabbed_hashmap_get_copy_pointer(const NexusDataSlabbedHashMap *hashmap, const void *key, void **out_pointer);

/*
Summary: removes key and optionally copies its stored pointer.
Description: keys retain the ordinary fixed-width byte-key semantics.
Params: key  borrowed key_size bytes; not retained
  out_pointer  optional out; must not overlap map storage;
  left untouched when key is absent
Returns: TRUE when key was removed, including a NULL value; FALSE when absent.
Ownership: pointees remain caller-owned; no pointed-to resource is released.
Errors: NULL hashmap/key or non-pointer-sized values asserts.
Thread-safety: caller synchronizes access to storage and pointees.
Complexity: O(key_size + 1) expected; O(n * key_size) worst case; no allocation.
*/
extern boolean nexus_data_slabbed_hashmap_remove_pointer(NexusDataSlabbedHashMap *hashmap, const void *key, void **out_pointer);

/*
Summary: removes every entry and retains slab/table capacity.
Ownership: invalidates all entry pointers and enumerators.
Errors: NULL hashmap asserts.
Thread-safety: caller synchronizes access to the container and payloads.
Complexity: O(table slots) time; no allocation.
*/
extern void nexus_data_slabbed_hashmap_clear(NexusDataSlabbedHashMap *hashmap);

/*
Summary: initializes a cursor over current HashMap entries.
Ownership: enumerator borrows hashmap; structural mutation invalidates it.
Errors: NULL hashmap/enumerator asserts.
Thread-safety: caller synchronizes access to the container and payloads.
*/
extern void nexus_data_slabbed_hashmap_enumerator_initialize(NexusDataSlabbedHashMap *hashmap, NexusDataSlabbedHashMapEnumerator *enumerator);

/*
Summary: returns the next entry in unspecified table order.
Params: out_key/out_value  optional outputs; unchanged when enumeration ends
Returns: TRUE if an entry was returned, FALSE at end.
Ownership:
  returned key/value pointers survive other entries being inserted/removed
  and rehashing, until their entry is removed/clear/destroy; key is read-only.
Errors: NULL enumerator, detached map, or structural mutation asserts.
Thread-safety: caller synchronizes access to the container and payloads.
Complexity: O(table slots) time across complete enumeration; no allocation.
*/
extern boolean nexus_data_slabbed_hashmap_enumerator_next(NexusDataSlabbedHashMapEnumerator *enumerator, const void **out_key, void **out_value);

/* ---------------------------------------------------------------------------- */
/* CHUNKED LIST AND WORD MAP                                                     */
/* ---------------------------------------------------------------------------- */

/*
Summary: an append-only list of fixed-size elements in address-stable chunks, with inline indexing.
Description:
  Elements live in chunks of 2^chunk_shift. An index becomes an address with a shift, a mask and one
  multiplication, inline in the caller, so a hot loop pays no call per access. Appending never moves an
  element; elements are never removed individually, only all at once by clear. Compared with
  NexusDataSlabbedList it gives up insertion, removal and reordering for that speed and a smaller
  footprint (no ordering metadata). The struct is visible only so the accessors can be inline:
  treat every field as private and use the functions.
  Payloads support ordinary C type alignment, not over-aligned objects. New slots are unspecified bytes.
Ownership: owns its chunks, not resources inside elements.
Thread-safety: callers synchronize access to each list and its elements.
*/
typedef struct NexusDataChunkedList {
  byte     **chunks;
  uint_large chunk_count;
  uint_large chunk_capacity;
  uint_large length;
  uint_large element_size;
  uint_large mask;
  uint32     shift;
} NexusDataChunkedList;

/*
Summary: prepares an empty chunked list in caller-provided storage.
Params:
  element_size  bytes per element; at least 1
  chunk_shift  log2 of elements per chunk, 1 to 23; a chunk is allocated on first need
Ownership: list is written in place; release it with nexus_data_chunked_list_release.
Errors: NULL list, a zero element_size, or a chunk_shift outside 1 to 23 asserts.
*/
extern void nexus_data_chunked_list_initialize(NexusDataChunkedList *list, uint_large element_size, uint32 chunk_shift);

/*
Summary: frees every chunk and leaves the list empty and reusable only after a new initialize.
Ownership: every element pointer expires.
Errors: NULL list asserts.
*/
extern void nexus_data_chunked_list_release(NexusDataChunkedList *list);

/*
Summary: adds one chunk of storage; the slow path of the inline append.
Errors: NULL list asserts.
Complexity: O(1) amortized; allocates one chunk, and occasionally doubles the chunk table.
*/
extern void nexus_data_chunked_list_chunk_add(NexusDataChunkedList *list);

/*
Summary: returns the element at index.
Returns: a stable pointer valid until clear or release.
Errors: NULL list or an index at or beyond the length asserts.
*/
NEXUS_MAYBE_UNUSED static void *nexus_data_chunked_list_at(const NexusDataChunkedList *list, uint_large index) {
  NEXUS_PROGRAM_PRECONDITION(list != NULL, "data_chunked_list_at: list is NULL");
  NEXUS_PROGRAM_PRECONDITION(index < list->length, "data_chunked_list_at: index out of range");

  return list->chunks[index >> list->shift] + ((index & list->mask) * list->element_size);
}

/*
Summary: returns the number of elements.
Errors: NULL list asserts.
*/
NEXUS_MAYBE_UNUSED static uint_large nexus_data_chunked_list_length_get(const NexusDataChunkedList *list) {
  NEXUS_PROGRAM_PRECONDITION(list != NULL, "data_chunked_list_length_get: list is NULL");

  return list->length;
}

/*
Summary: removes every element at or after new_length and keeps the chunks.
Description: the list returns to an earlier length, so work appended after a recorded length can be undone in O(1).
Ownership: pointers to the removed elements expire.
Errors: NULL list or a new_length above the current length asserts.
*/
NEXUS_MAYBE_UNUSED static void nexus_data_chunked_list_truncate(NexusDataChunkedList *list, uint_large new_length) {
  NEXUS_PROGRAM_PRECONDITION(list != NULL, "data_chunked_list_truncate: list is NULL");
  NEXUS_PROGRAM_PRECONDITION(new_length <= list->length, "data_chunked_list_truncate: new_length exceeds the length");

  list->length = new_length;
}

/*
Summary: removes every element and keeps the chunks for reuse.
Ownership: every element pointer expires.
Errors: NULL list asserts.
*/
NEXUS_MAYBE_UNUSED static void nexus_data_chunked_list_clear(NexusDataChunkedList *list) {
  NEXUS_PROGRAM_PRECONDITION(list != NULL, "data_chunked_list_clear: list is NULL");

  list->length = 0;
}

/*
Summary: appends one slot whose bytes are unspecified.
Returns: the stable slot for the caller to fill.
Errors: NULL list asserts.
Complexity: O(1); a call into the library only when a new chunk is needed.
*/
NEXUS_MAYBE_UNUSED static void *nexus_data_chunked_list_append_uninitialized(NexusDataChunkedList *list) {
  void *slot;

  NEXUS_PROGRAM_PRECONDITION(list != NULL, "data_chunked_list_append_uninitialized: list is NULL");

  if ((list->length >> list->shift) >= list->chunk_count) {
    nexus_data_chunked_list_chunk_add(list);
  }

  slot = list->chunks[list->length >> list->shift] + ((list->length & list->mask) * list->element_size);
  list->length++;

  return slot;
}

/*
Summary: appends a copy of element_size bytes.
Params: element  borrowed; copied before return; may refer to a live element of this list
Returns: the stable slot holding the copy.
Errors: NULL list or element asserts.
*/
NEXUS_MAYBE_UNUSED static void *nexus_data_chunked_list_append(NexusDataChunkedList *list, const void *element) {
  void *slot;

  NEXUS_PROGRAM_PRECONDITION(element != NULL, "data_chunked_list_append: element is NULL");

  slot = nexus_data_chunked_list_append_uninitialized(list);
  nexus_memory_bytes_move(element, slot, list->element_size);

  return slot;
}

/*
Summary: a bump allocator whose allocations all live until the arena is released together.
Description: allocations are zeroed, eight-byte aligned and never move. Suited to the syntax trees and expansions of one
  compilation, where every object has the same lifetime and freeing one by one would only cost time.
Ownership: the arena owns its blocks; release frees every allocation at once. Initialize before use.
Thread-safety: callers synchronize access to each arena.
*/
typedef struct NexusDataArena {
  NexusDataList *blocks;
  byte          *cursor;
  uint_large     remaining;
} NexusDataArena;

/* Summary: prepares an empty arena. Errors: NULL arena asserts. */
extern void nexus_data_arena_initialize(NexusDataArena *arena);

/* Summary: frees every block; every pointer the arena returned expires. The arena may be initialized again. */
extern void nexus_data_arena_release(NexusDataArena *arena);

/* Summary: returns size zeroed bytes, eight-byte aligned, valid until release. */
extern void *nexus_data_arena_allocate(NexusDataArena *arena, uint_large size);

/* Summary: copies size bytes of text into the arena and terminates the copy with a NUL byte. */
extern char *nexus_data_arena_text_copy(NexusDataArena *arena, const char *text, uint_large size);

/*
Summary: an owning open-addressing hash map from fixed-width integer-word keys to integer-word values, emptied in O(1).
Description:
  A key is key_words uint_large words (1 to 4) and a value value_words words (1 to 4), compared and hashed
  directly with no callbacks, so lookups are a few instructions plus the probe. Linear probing in a
  power-of-two table kept at most half full. clear advances an epoch instead of touching slots, so a map
  that is emptied often (a memo for one unit of work) costs nothing to reset and stays cache-resident.
  Entries are never removed individually. Keys are compared exactly.
Ownership: owns its table and copies keys and values by value.
Thread-safety: callers synchronize access to each map.
*/
typedef struct NexusDataWordMap NexusDataWordMap;

/*
Summary: creates an empty word map.
Params:
  key_words  words per key, 1 to 4
  value_words  words per value, 1 to 4
  initial_capacity  entries to hold before the first growth; rounded up to a power of two, at least 8
Returns: the owned map.
Ownership: caller destroys it with nexus_data_word_map_destroy.
Errors: a key_words or value_words outside 1 to 4 asserts. Allocation follows Nexus policy.
*/
extern NexusDataWordMap *nexus_data_word_map_create(uint32 key_words, uint32 value_words, uint_large initial_capacity);

/*
Summary: destroys the map and its table.
Ownership: consumes map; NULL is a no-op; every value pointer expires.
*/
extern void nexus_data_word_map_destroy(NexusDataWordMap *map);

/*
Summary: removes every entry in O(1) and keeps the table.
Ownership: every value pointer expires.
Errors: NULL map asserts.
Complexity: O(1) time, no allocation.
*/
extern void nexus_data_word_map_clear(NexusDataWordMap *map);

/*
Summary: returns the number of entries.
Errors: NULL map asserts.
*/
extern uint_large nexus_data_word_map_length_get(const NexusDataWordMap *map);

/*
Summary: finds the value stored for key.
Params: key  key_words words; borrowed
Returns: the value's words, writable in place, or NULL when the key is absent.
Ownership: the pointer expires at the next insert, clear or destroy.
Errors: NULL map or key asserts.
Complexity: O(1) expected.
*/
extern uint_large *nexus_data_word_map_get(const NexusDataWordMap *map, const uint_large *key);

/*
Summary: adds an entry for a key that is not yet present.
Params: key  key_words words; copied
Returns: the new value's words, unspecified until the caller writes them.
Ownership: the pointer expires at the next insert, clear or destroy.
Errors: NULL map or key, or a key already present, asserts.
Complexity: O(1) amortized; a growth rehashes the live entries.
*/
extern uint_large *nexus_data_word_map_insert(NexusDataWordMap *map, const uint_large *key);

/* ============================================================================ */
/* ---------------------------------------------------------------------------- */
/* REALS                                                                        */
/* ---------------------------------------------------------------------------- */
/* ============================================================================ */

/*
Summary: TRUE when value is neither NaN nor +/-infinity.
Description: value == value is FALSE only for NaN (the standard portable NaN test, no <math.h>
  needed); the range check against F_REAL_MAX catches both infinities.
*/
static NEXUS_MAYBE_UNUSED boolean nexus_real_is_finite(f_real value) {
  return (value == value) && (value <= (f_real)F_REAL_MAX) && (value >= (f_real)-F_REAL_MAX);
}

/*
Summary: value if finite, otherwise 0 -- scrubs NaN/Inf before they poison an accumulator.
*/
static NEXUS_MAYBE_UNUSED f_real nexus_real_finite_or_zero(f_real value) {
  if (!nexus_real_is_finite(value)) {
    return (f_real)0;
  }

  return value;
}

/*
Summary: IEEE-754 machine epsilon for the configured f_real (FLT_EPSILON or DBL_EPSILON) -- a silicon property, not a tunable.
*/
static NEXUS_MAYBE_UNUSED f_real nexus_real_epsilon(void) {
  return (f_real)F_REAL_EPSILON;
}

/*
Summary: how nexus_real*_round and nexus_real_round_to_decimals round a value that falls exactly between two candidates.
*/
typedef enum NexusRealRoundMode {
  /* Toward negative infinity. */
  NRRM_FLOOR,
  /* Toward positive infinity. */
  NRRM_CEIL,
  /* Toward zero. */
  NRRM_TRUNC,
  /* Nearest; a halfway value moves away from zero. */
  NRRM_ROUND,
  /* Nearest; a halfway value moves to the nearest even candidate (banker's rounding). */
  NRRM_ROUND_EVEN,

  NRRM_COUNT
} NexusRealRoundMode;

/*
Summary: rounds a 32-bit float to the nearest integer value (still returned as a float) per mode.
*/
extern real32 nexus_real32_round(real32 value, NexusRealRoundMode mode);
/*
Summary: rounds a 64-bit float to the nearest integer value (still returned as a float) per mode.
*/
extern real64 nexus_real64_round(real64 value, NexusRealRoundMode mode);
/*
Summary: rounds the configured f_real to the nearest integer value (still returned as an f_real) per mode.
*/
extern f_real nexus_real_round(f_real value, NexusRealRoundMode mode);

/*
Summary: rounds value to decimal_places fractional digits per mode, returned as an f_real (e.g. round_to_decimals(3.14159, 2, NRRM_ROUND) -> 3.14).
Description: scale-round-unscale (value * 10^decimal_places, round to an integer, divide back
  down) -- decimal_places is clamped to 0-17 (f_real's useful decimal precision ceiling; beyond
  that 10^n itself starts losing representable precision, so a larger request couldn't be
  honored exactly regardless).
*/
extern f_real nexus_real_round_to_decimals(f_real value, uint32 decimal_places, NexusRealRoundMode mode);

/* ============================================================================ */
/* ---------------------------------------------------------------------------- */
/* STRINGS                                                                      */
/* ---------------------------------------------------------------------------- */
/* ============================================================================ */

static NEXUS_MAYBE_UNUSED uint64 nexus_strings_string_length_get(const char *string) {
  const char *cursor = string;

  while (*cursor != '\0')
    ++cursor;

  return (uint64)(cursor - string);
}

/*
Summary: borrows string as read-only bytes without copying; NULL string asserts.
*/
static NEXUS_MAYBE_UNUSED const byte *nexus_strings_string_bytes_get(const char *string) {
  NEXUS_PROGRAM_PRECONDITION_ALWAYS(string != NULL, "String must not be null.");

  return (const byte *)string;
}

/*
Summary: TRUE if two nul-terminated strings have identical contents.
*/
static NEXUS_MAYBE_UNUSED boolean nexus_strings_string_equals(const char *left, const char *right) {
  while (*left != '\0' && *left == *right) {
    ++left;
    ++right;
  }

  return *left == *right;
}

/*
Summary: TRUE if string is empty.
*/
static NEXUS_MAYBE_UNUSED boolean nexus_strings_string_is_empty(const char *string) {
  return string[0] == '\0';
}

/*
Summary: TRUE if string begins with prefix. An empty prefix always matches.
*/
extern boolean nexus_strings_string_starts_with(const char *string, const char *prefix);

/*
Summary: TRUE if string ends with suffix. An empty suffix always matches.
*/
extern boolean nexus_strings_string_ends_with(const char *string, const char *suffix);

/*
Summary: TRUE if string contains substring. An empty substring always matches.
*/
extern boolean nexus_strings_string_contains(const char *string, const char *substring);

/*
Summary: returns a pointer to the first occurrence of substring in string, or NULL if absent.
Ownership: returned pointer borrows storage from string; do not free.
*/
extern const char *nexus_strings_string_find(const char *string, const char *substring);

/*
Summary: returns a pointer to the last occurrence of substring in string, or NULL if absent. Overlapping matches count.
Description: an empty substring matches at the end of string.
Ownership: returned pointer borrows storage from string; do not free.
*/
extern const char *nexus_strings_string_find_last(const char *string, const char *substring);

/*
Summary: the edit distance between two strings: insertions, deletions, substitutions and swaps of neighbouring bytes cost one.
Returns: the distance, or 0xFFFFFFFF when either string is longer than 255 bytes.
*/
extern uint32 nexus_strings_string_edit_distance(const char *left, const char *right);

/*
Summary: the candidate nearest to name, as a "did you mean" suggestion, or NULL when none is close enough.
Description: close enough is a distance of at most a third of name's length (at least 1, at most 3); a candidate that name is
  a prefix of (three or more bytes) counts as one edit away.
Ownership: the result is one of the candidates; do not free.
*/
extern const char *nexus_strings_string_closest(const char *name, const char *const *candidates, uint_large count);

/* Summary: writes the UTF-8 encoding of a Unicode scalar value into out (at least four bytes) and returns its length from one to four. */
extern uint32 nexus_strings_utf8_encode(uint32 value, byte *out);

/* stb_sprintf.h is third-party and written with // comments, which -std=c89 -pedantic warns about on every inclusion. */
#ifdef __clang__
#  pragma clang diagnostic push
#  pragma clang diagnostic ignored "-Wcomment"
#endif
#include <stb_sprintf.h>
#ifdef __clang__
#  pragma clang diagnostic pop
#endif

/*
Summary: reports the outcome of a bounded, truncation-tolerant format/write operation.
Fields:
  written_length   characters actually written into the destination, excluding the null terminator
  required_length  characters the full, untruncated output would have needed, excluding the null terminator
  truncated        TRUE when required_length exceeded what the destination could hold
*/
typedef struct NexusStringFormatResult {
  uint_large written_length;
  uint_large required_length;
  boolean    truncated;
} NexusStringFormatResult;

/*
Summary: formats format+args into string (capacity max_string_length), truncating rather than overflowing -- the va_list-accepting core behind nexus_strings_string_format_with_truncation.
Description: one stbsp_vsnprintf call: it already reports the full required length in the same
  call that writes the (possibly truncated) text, so there's no separate measuring pass and no
  va_copy concern here, unlike a strict non-truncating variant that would need two passes.
*/
static NEXUS_MAYBE_UNUSED NexusStringFormatResult nexus_strings_vstring_format_with_truncation(char *string, uint_large max_string_length,
                                                                                               const char *format, va_list args) {
  NexusStringFormatResult result;
  int                     formatted_length;

  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_strings_vstring_format_with_truncation");
  NEXUS_PROGRAM_PRECONDITION(max_string_length <= (uint_large)INT32_MAX_VAL, "max_string_length exceeds stbsp_vsnprintf's int capacity parameter");

  formatted_length = stbsp_vsnprintf(string, (int)max_string_length, format, args);

  result.required_length = (uint_large)formatted_length;
  result.truncated       = (uint_large)formatted_length >= max_string_length;

  if (result.truncated) {
    result.written_length = (max_string_length > 0) ? (max_string_length - 1) : 0;
  } else {
    result.written_length = (uint_large)formatted_length;
  }

  NEXUS_PROGRAM_CONTEXT_POP();
  return result;
}

/*
Summary: formats format+... into string (capacity max_string_length), truncating rather than overflowing. See NexusStringFormatResult.
*/
static NEXUS_MAYBE_UNUSED NexusStringFormatResult nexus_strings_string_format_with_truncation(char *string, uint_large max_string_length,
                                                                                              const char *format, ...) {
  va_list                 args;
  NexusStringFormatResult result;

  va_start(args, format);
  result = nexus_strings_vstring_format_with_truncation(string, max_string_length, format, args);
  va_end(args);

  return result;
}

/*
Summary: which storage mode a NexusStringBuilder uses -- chosen once, at creation.
*/
typedef enum NexusStringBuilderMode {
  /* Caller-owned buffer, fixed capacity -- an append that would overflow it returns a real NexusError instead of writing anything. */
  NSBM_FIXED,
  /* Nexus-owned buffer via nexus_memory_allocate/reallocate, grown (doubled) on demand -- an append can't fail for lack of room. */
  NSBM_GROWABLE,

  NSBM_COUNT
} NexusStringBuilderMode;

/*
Summary: a Go strings.Builder-style incremental string buffer -- see nexus_strings_string_builder_create_fixed / create_growable.
Fields:
  buffer    NULL until first written to in NSBM_GROWABLE mode; always the caller's pointer in NSBM_FIXED mode
  length    bytes written so far, excluding the null terminator
  capacity  current usable size of buffer, excluding the null terminator's byte
*/
typedef struct NexusStringBuilder {
  char                  *buffer;
  uint_large             length;
  uint_large             capacity;
  NexusStringBuilderMode mode;
} NexusStringBuilder;

/*
Summary: a NexusStringBuilder over a caller-owned, fixed-size buffer -- appends past capacity return a real NexusError; nothing is ever allocated or freed.
Params: buffer  at least 1 byte; immediately null-terminated
*/
static NEXUS_MAYBE_UNUSED NexusStringBuilder nexus_strings_string_builder_create_fixed(char *buffer, uint_large capacity) {
  NexusStringBuilder builder;

  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_strings_string_builder_create_fixed");
  NEXUS_PROGRAM_PRECONDITION(capacity > 0, "string_builder_create_fixed: capacity must be > 0");

  builder.buffer   = buffer;
  builder.length   = 0;
  builder.capacity = capacity;
  builder.mode     = NSBM_FIXED;

  buffer[0] = '\0';

  NEXUS_PROGRAM_CONTEXT_POP();
  return builder;
}

/*
Summary: a NexusStringBuilder over a Nexus-owned, growable buffer -- appends never fail for lack of room; nexus_strings_string_builder_destroy must be called when done.
Description: nothing is actually allocated until the first append -- initial_capacity is only a
  hint for that first allocation (0 means "use the built-in default").
*/
static NEXUS_MAYBE_UNUSED NexusStringBuilder nexus_strings_string_builder_create_growable(uint_large initial_capacity) {
  NexusStringBuilder builder;

  builder.buffer   = NULL;
  builder.length   = 0;
  builder.capacity = initial_capacity;
  builder.mode     = NSBM_GROWABLE;

  return builder;
}

/*
Summary: the builder's current content -- a NUL-terminated view into its own buffer, valid until the next append/reset/destroy.
*/
static NEXUS_MAYBE_UNUSED const char *nexus_strings_string_builder_string_get(const NexusStringBuilder *builder) {
  static const char empty_string[1] = {'\0'};

  return builder->buffer != NULL ? builder->buffer : empty_string;
}

/*
Summary: bytes written so far, excluding the null terminator.
*/
static NEXUS_MAYBE_UNUSED uint_large nexus_strings_string_builder_length_get(const NexusStringBuilder *builder) {
  return builder->length;
}

/*
Summary: rewinds the builder to empty without releasing NSBM_GROWABLE's buffer or touching NSBM_FIXED's caller-owned one -- a later append reuses whatever capacity is already there.
*/
static NEXUS_MAYBE_UNUSED void nexus_strings_string_builder_reset(NexusStringBuilder *builder) {
  builder->length = 0;

  if (builder->buffer != NULL) {
    builder->buffer[0] = '\0';
  }
}

/*
Summary: releases a NSBM_GROWABLE builder's buffer. A no-op for NSBM_FIXED (the caller owns that buffer, not the builder).
*/
static NEXUS_MAYBE_UNUSED void nexus_strings_string_builder_destroy(NexusStringBuilder *builder) {
  if (builder->mode == NSBM_GROWABLE && builder->buffer != NULL) {
    nexus_memory_deallocate(builder->buffer);
  }

  builder->buffer   = NULL;
  builder->length   = 0;
  builder->capacity = 0;
}

/*
Summary: the facility identifier for synthetic strings-subsystem errors -- currently just a NSBM_FIXED builder's capacity being exceeded, which isn't an OS errno condition.
*/
#define NEXUS_ERROR_STRINGS_FACILITY NEXUS_PROGRAM_ERROR_FACILITY_IDENTIFIER_CREATE('S', 'T', 'R', 'S')

/*
Summary: categories within NexusStringsError; currently just NSEC_GENERIC.
*/
typedef enum NexusStringsErrorCategory {
  NSEC_GENERIC,

  NSEC_COUNT
} NexusStringsErrorCategory;

#define NEXUS_STRINGS_ERROR_LIST(X, library, facility) X(NEXUS_STRINGS_ERROR_CAPACITY_EXCEEDED, NSEC_GENERIC, 1, library, facility)

NEXUS_ERROR_FACILITY_DEFINE(NexusStringsError, NEXUS_ERROR_LIBRARY_IDENTIFIER, NEXUS_ERROR_STRINGS_FACILITY, NEXUS_STRINGS_ERROR_LIST)

/*
Summary: appends text to builder -- see NexusStringBuilderMode for what happens on overflow.
*/
extern NEXUS_NODISCARD NexusError *nexus_strings_string_builder_append(NexusStringBuilder *builder, const char *text);
/*
Summary: like nexus_strings_string_builder_append, but format+... printf-style.
*/
extern NEXUS_NODISCARD NexusError *nexus_strings_string_builder_append_formatted(NexusStringBuilder *builder, const char *format, ...);

/*
Summary: JSON (RFC 8259) read into an arena, and written as quoted strings.
Description:
  nexus_json_parse builds the whole value tree in the arena the caller owns, so there is nothing to free but the arena.
  Strings are decoded to UTF-8 (\u escapes, surrogate pairs included); a number keeps its integer part and is otherwise not
  interpreted; nesting deeper than 64 levels is refused. An object keeps its members in order and a duplicate key is kept
  (member lookup finds the first).
Fields (NexusJson):
  kind           one of NexusJsonKind
  boolean_value  the value of a boolean
  integer        the integer part of a number
  text           a string's text, decoded
  items          the elements of an array, the values of an object (count of them)
  keys           the keys of an object, parallel to items
Thread-safety: none; a value tree is read-only after parsing.
*/
typedef enum NexusJsonKind {
  NJK_NULL,
  NJK_BOOLEAN,
  NJK_NUMBER,
  NJK_STRING,
  NJK_ARRAY,
  NJK_OBJECT
} NexusJsonKind;

typedef struct NexusJson NexusJson;

struct NexusJson {
  NexusJsonKind      kind;
  boolean            boolean_value;
  int64              integer;
  const char        *text;
  NexusJson *const  *items;
  const char *const *keys;
  uint32             count;
};

/* Summary: parses a JSON text (size bytes) into the arena; NULL when it is not valid JSON or anything but blanks follows the value. */
extern const NexusJson *nexus_json_parse(NexusDataArena *arena, const char *text, uint_large size);

/* Summary: the value of an object's member, or NULL when it is not an object or has no such member. */
extern const NexusJson *nexus_json_member_get(const NexusJson *object, const char *key);

/* Summary: the text of a string member, or NULL when it is absent or not a string. */
extern const char *nexus_json_member_text_get(const NexusJson *object, const char *key);

/* Summary: the integer of a number member, or fallback when it is absent or not a number. */
extern int64 nexus_json_member_integer_get(const NexusJson *object, const char *key, int64 fallback);

/* Summary: appends text to a string builder as a JSON string literal (quoted, with the escapes JSON needs); UTF-8 passes through. */
extern NEXUS_NODISCARD NexusError *nexus_json_quote_append(NexusStringBuilder *builder, const char *text);

/*
Summary: writes byte_count as a human-readable IEC binary size (B, KiB, MiB, GiB, TiB -- 1024-based).
*/
extern NEXUS_NODISCARD NexusStringFormatResult nexus_strings_bytes_format(char *string, uint_large max_string_length, uint64 byte_count);
/*
Summary: like nexus_strings_bytes_format, but for a fractional byte count (e.g. a computed rate) -- NaN/+Inf/-Inf render as "NaN", "Inf", "-Inf".
*/
extern NEXUS_NODISCARD NexusStringFormatResult nexus_strings_bytes_format_f_real(char *string, uint_large max_string_length, f_real byte_count);
/*
Summary: writes value as a decimal SI-scaled unitless quantity (1000-based: "", "K", "M", "G", "T").
*/
extern NEXUS_NODISCARD NexusStringFormatResult nexus_strings_quantity_format(char *string, uint_large max_string_length, uint64 value);
/*
Summary: like nexus_strings_quantity_format, but for a fractional value, with decimal_places fractional digits (clamped 0-9). NaN/+Inf/-Inf render as "NaN", "Inf", "-Inf".
*/
extern NEXUS_NODISCARD NexusStringFormatResult nexus_strings_quantity_format_f_real(char *string, uint_large max_string_length, f_real value,
                                                                                    uint32 decimal_places);
/*
Summary: writes percent_value (already on a 0-100 scale) with decimal_places fractional digits (clamped 0-9) and a trailing '%'. NaN/+Inf/-Inf render as "NaN%", "Inf%", "-Inf%".
*/
extern NEXUS_NODISCARD NexusStringFormatResult nexus_strings_percent_format_f_real(char *string, uint_large max_string_length, f_real percent_value,
                                                                                   uint32 decimal_places);

/* ============================================================================ */
/* ---------------------------------------------------------------------------- */
/* TIME                                                                         */
/* ---------------------------------------------------------------------------- */
/* ============================================================================ */

#include <time.h>

#define NEXUS_NANOSECONDS_PER_MICROSECOND 1000ULL
#define NEXUS_NANOSECONDS_PER_MILLISECOND 1000000ULL
#define NEXUS_NANOSECONDS_PER_SECOND      1000000000ULL

/*
Summary: precision an operation can actually vouch for in a NexusTime/NexusDuration's sub-second field.
*/
typedef enum NexusTimePrecision {
  NTP_NANO_SECOND,
  NTP_MICRO_SECOND,
  NTP_MILLI_SECOND,
  NTP_SECOND,

  /*
  NTP_UNKNOWN: the source can't vouch for any precision at all -- e.g. a filesystem timestamp,
  where nothing in a stat() call reports the filesystem's own timestamp granularity (ext4 tracks
  nanoseconds, FAT32 only ~2-second steps, and stat() never says which). Claiming NTP_NANO_SECOND
  just because the struct field has nanosecond width would assert a precision that isn't real.
  */
  NTP_UNKNOWN,

  NTP_COUNT
} NexusTimePrecision;

/*
Summary: which OS clock produced a NexusTime value -- NCO_REAL (UTC epoch) vs. NCO_MONOTONIC (arbitrary, boot-relative).
Description: this is the field's whole job: telling UTC-epoch values apart from monotonic
  counters, since combining the two in arithmetic is meaningless (a monotonic value has no
  epoch to compare against a UTC one). It's about what the number *means*, not about how or
  when it was obtained -- a stat()-sourced timestamp is NCO_REAL, exactly like
  nexus_time_get_real()'s result, because its content is genuinely a UTC epoch value; that's
  what lets nexus_time_duration_between(file_mtime, nexus_time_get_real()) mean something.
*/
typedef enum NexusClockOrigin {
  NCO_REAL,
  NCO_MONOTONIC,

  NCO_COUNT
} NexusClockOrigin;

/*
Summary: an absolute, timezone-agnostic point in time (Machine Time) -- nanoseconds since either the UTC epoch or an arbitrary monotonic reference, per clock_origin.
Description: must strictly hold a pure UTC epoch or a pure monotonic count -- never shifted to
  represent a local timezone offset. Timezone/DST is exclusively NexusDateTime's concern, the
  presentation layer.
Fields:
  time          the raw timestamp; UTC epoch or monotonic count per clock_origin
  precision     the reported precision of the source (see NexusTimePrecision) -- how far the
                  sub-second field can actually be trusted
  clock_origin  which clock produced this value -- operations combining two NexusTime values
                  expect matching origins (NEXUS_PROGRAM_ASSERT in NEXUS_DEVELOPER_MODE)
*/
typedef struct NexusTime {
  timestamp          time;
  NexusTimePrecision precision;
  NexusClockOrigin   clock_origin;
} NexusTime;

/*
Summary: the current wall-clock time as a pure UTC epoch.
*/
extern NexusTime nexus_time_get_real(void);
/*
Summary: an absolute monotonic count since an arbitrary hardware boot-time reference.
Description: only meaningful for measuring durations between two of its own results -- never
  for calendar dates, and never combined with a NexusTime carrying NCO_REAL.
*/
extern NexusTime nexus_time_get_monotonic(void);
/*
Summary: builds a NexusTime from a POSIX struct timespec (e.g. one read out of struct stat) and an explicit precision.
Description: unlike nexus_time_get_real/get_monotonic, this never queries a clock itself -- it
  just packs an already-obtained timespec's seconds+nanoseconds into a NexusTime, so the
  caller decides both clock_origin (NCO_REAL for anything UTC-epoch-based, which a filesystem
  timestamp is) and precision (NTP_UNKNOWN for a stat() timestamp -- see NexusTimePrecision).
Params:
  timespec_value  seconds+nanoseconds, already read from the source (e.g. st_mtim)
  clock_origin    see NexusClockOrigin
  precision       see NexusTimePrecision -- the caller's honest claim, not derived here
*/
extern NexusTime nexus_time_from_timespec(struct timespec timespec_value, NexusClockOrigin clock_origin, NexusTimePrecision precision);

/*
Summary: a broken-down calendar date and time (Human Time) -- the presentation layer; the only structure that ever reflects a local timezone or DST.
Fields:
  year        full year, e.g. 2026; negative supports dates before 1 AD
  month, day  month [1, 12], day of month [1, 31]
  hour, minute, second  [0, 23], [0, 59], [0, 60] (60 accommodates a leap second)
  nanosecond  sub-second [0, 999999999]
  precision   the originating NexusTime's precision, carried through
  is_dst      -1 unknown, 0 no, 1 yes
*/
typedef struct NexusDateTime {
  int32              year;
  uint8              month;
  uint8              day;
  uint8              hour;
  uint8              minute;
  uint8              second;
  uint32             nanosecond;
  NexusTimePrecision precision;
  int8               is_dst;
} NexusDateTime;

/*
Summary: a signed time interval, independent of any absolute point in time.
Fields:
  nanoseconds  signed duration in nanoseconds
  precision    the minimal granularity used to calculate this duration
*/
typedef struct NexusDuration {
  int64              nanoseconds;
  NexusTimePrecision precision;
} NexusDuration;

/*
Summary: constructs a NexusDuration from a plain integer count -- one of these four per unit (nanoseconds/microseconds/milliseconds/seconds), precision set to match the unit given.
*/
extern NexusDuration nexus_time_duration_from_nanoseconds(int64 nanoseconds);
extern NexusDuration nexus_time_duration_from_microseconds(int64 microseconds);
extern NexusDuration nexus_time_duration_from_milliseconds(int64 milliseconds);
extern NexusDuration nexus_time_duration_from_seconds(int64 seconds);

/*
Summary: the signed interval from start to end (negative if end precedes start).
Errors: NEXUS_PROGRAM_ASSERT (NEXUS_DEVELOPER_MODE only): start.clock_origin must equal end.clock_origin.
*/
extern NexusDuration nexus_time_duration_between(NexusTime start, NexusTime end);
/*
Summary: time offset by duration, saturating to TIMESTAMP_MAX_VAL / 0 instead of wrapping on overflow/underflow.
*/
extern NexusTime nexus_time_add_duration(NexusTime time, NexusDuration duration);
/*
Summary: time offset by the negation of duration -- see nexus_time_add_duration.
*/
extern NexusTime nexus_time_sub_duration(NexusTime time, NexusDuration duration);

extern NexusDuration nexus_time_duration_add(NexusDuration duration1, NexusDuration duration2);
extern NexusDuration nexus_time_duration_sub(NexusDuration duration1, NexusDuration duration2);
extern NexusDuration nexus_time_duration_mul(NexusDuration duration, int64 scalar);
/*
Summary: like nexus_time_duration_mul, but by a floating-point scalar.
Description: truncates precision for durations exceeding roughly 104 days (f_real's mantissa
  can't represent a nanosecond count that large exactly).
*/
extern NexusDuration nexus_time_duration_mul_f(NexusDuration duration, f_real scalar);
/*
Summary: like nexus_time_duration_mul, but division; scalar must not be 0 (NEXUS_PROGRAM_ASSERT, NEXUS_DEVELOPER_MODE only).
*/
extern NexusDuration nexus_time_duration_div(NexusDuration duration, int64 scalar);

/*
Summary: converts a UTC NexusTime into this process's local broken-down calendar time.
Errors: NEXUS_PROGRAM_ASSERT (NEXUS_DEVELOPER_MODE only): utc_time.clock_origin must be NCO_REAL.
  A conversion failure (out-of-range seconds, or the underlying localtime_r call failing) yields
  a zeroed NexusDateTime rather than a NexusError.
*/
extern NexusDateTime nexus_time_to_local_datetime(NexusTime utc_time);
/*
Summary: the inverse of nexus_time_to_local_datetime -- a local broken-down time back to a UTC NexusTime.
Errors: a conversion failure (the underlying mktime call failing) yields a zeroed NexusTime.
*/
extern NexusTime nexus_time_from_local_datetime(NexusDateTime date_time);

/*
Summary: parses "YYYY-MM-DD HH:MM:SS[.nanosecond]" into out_date_time.
Returns: FALSE on a NULL argument or fewer than 6 parsed fields (date+time; the fractional part is optional).
*/
extern boolean nexus_time_datetime_parse(const char *string, NexusDateTime *out_date_time);
/*
Summary: nexus_time_datetime_parse followed by nexus_time_from_local_datetime, in one call.
*/
extern boolean nexus_time_from_local_datetime_string(const char *string, NexusTime *out_time);

/*
Summary: formats duration as a human-readable magnitude with an auto-selected unit ("1.500 ms", "12.000 s", ...).
Description: returns uint_large (the length that would have been written, snprintf-style),
  matching nexus_program_errors_error_print's return convention.
*/
extern NEXUS_NODISCARD uint_large nexus_time_duration_format(char *string, uint_large max_string_length, NexusDuration duration);
/*
Summary: formats date_time as "YYYY-MM-DD HH:MM:SS.nanosecond". See nexus_time_duration_format's Description re: return type.
*/
extern NEXUS_NODISCARD uint_large nexus_time_datetime_format(char *string, uint_large max_string_length, NexusDateTime date_time);

/* ============================================================================ */
/* ---------------------------------------------------------------------------- */
/* FILESYSTEM                                                                   */
/* ---------------------------------------------------------------------------- */
/* ============================================================================ */

#if NEXUS_PLATFORM_POSIX
#  include <sys/stat.h>
#endif

#include <stdio.h> /* rename(), remove() -- standard C89, not POSIX-specific. */

#ifndef NEXUS_FILESYSTEM_MAX_PATH_LENGTH
#  define NEXUS_FILESYSTEM_MAX_PATH_LENGTH 4096
#endif

typedef struct NexusPath {
  char   buffer[NEXUS_FILESYSTEM_MAX_PATH_LENGTH + 1];
  uint64 path_length;
} NexusPath;

static NEXUS_MAYBE_UNUSED NexusPath nexus_filesystem_path_create(const char *path) {
  NexusPath nexus_path;
  uint64    path_length;

  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_filesystem_path_create");
  path_length = nexus_strings_string_length_get(path);

  NEXUS_PROGRAM_PRECONDITION(path_length <= NEXUS_FILESYSTEM_MAX_PATH_LENGTH, "Path length exceeds max path length: got=%llu,max=%llu", path_length,
                             NEXUS_FILESYSTEM_MAX_PATH_LENGTH);

  nexus_path.path_length = path_length;
  nexus_memory_bytes_copy(path, nexus_path.buffer, path_length);
  nexus_path.buffer[path_length] = '\0';

  NEXUS_PROGRAM_CONTEXT_POP();
  return nexus_path;
}

static NEXUS_MAYBE_UNUSED void nexus_filesystem_path_join(NexusPath *base, const char *addition) {
  uint64 addition_length;

  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_filesystem_path_join");
  addition_length = nexus_strings_string_length_get(addition);

  NEXUS_PROGRAM_PRECONDITION(base->path_length + addition_length + 1 <= NEXUS_FILESYSTEM_MAX_PATH_LENGTH,
                             "Combined path length exceeds max path length: got=%llu,max=%llu", base->path_length + addition_length + 1,
                             NEXUS_FILESYSTEM_MAX_PATH_LENGTH);

#if NEXUS_PLATFORM_POSIX
  base->buffer[base->path_length] = '/';
  nexus_memory_bytes_copy(addition, &base->buffer[base->path_length + 1], addition_length);
  base->path_length += (1 + addition_length);
  base->buffer[base->path_length] = '\0';
#else
  NEXUS_PLATFORM_UNSUPPORTED_RAISE("filesystem_path_join");
#endif

  NEXUS_PROGRAM_CONTEXT_POP();
}

static NEXUS_MAYBE_UNUSED NexusPath nexus_filesystem_path_parent(const NexusPath *path) {
  NexusPath parent_path;
  uint64    separator_idx;
  uint64    i;

  separator_idx = UINT64_MAX_VAL;

  for (i = path->path_length; i-- > 0;) {
#if NEXUS_PLATFORM_POSIX
    if (path->buffer[i] == '/') {
      separator_idx = i;
      break;
    }
#else
    NEXUS_PROGRAM_CONTEXT_PUSH("nexus_filesystem_path_parent");
    NEXUS_PLATFORM_UNSUPPORTED_RAISE("filesystem_path_parent");
#endif
  }

  if (separator_idx == UINT64_MAX_VAL) {
    /* No separator at all -- nothing to strip; the path has no parent within itself. */
    parent_path = *path;
  } else if (separator_idx == 0) {
    /* Direct child of root ("/etc") -- the parent is root itself ("/"), not an off-by-one
       underflow into a ~2^64-byte "copy" (the bug this replaces: separator_idx - 1 on an
       unsigned 0 wraps to UINT64_MAX, which nexus_memory_bytes_copy would then try to honor). */
    parent_path.path_length = 1;
    parent_path.buffer[0]   = '/';
    parent_path.buffer[1]   = '\0';
  } else {
    /* Up to but excluding the separator -- e.g. "/etc/foo" -> "/etc" (length 4, the index of
       the separator itself). The bug this replaces used separator_idx - 1, truncating one
       character too many out of every multi-component path ("/etc" -> "/et"). */
    parent_path.path_length = separator_idx;
    nexus_memory_bytes_copy(path->buffer, parent_path.buffer, parent_path.path_length);
    parent_path.buffer[parent_path.path_length] = '\0'; /* Never set by the original -- buffer is
                                                            uninitialized stack storage otherwise. */
  }

  return parent_path;
}

static NEXUS_MAYBE_UNUSED NexusPath nexus_filesystem_path_root(const NexusPath *path) {
  NexusPath root_path;
  uint64    first_separator_idx;
  uint64    i;

  first_separator_idx = UINT64_MAX_VAL;

  for (i = 0; i < path->path_length; i++) {
#if NEXUS_PLATFORM_POSIX
    if (path->buffer[i] == '/') {
      first_separator_idx = i;
      break;
    }
#else
    NEXUS_PROGRAM_CONTEXT_PUSH("nexus_filesystem_path_root");
    NEXUS_PLATFORM_UNSUPPORTED_RAISE("filesystem_path_root");
#endif
  }

  if (first_separator_idx == UINT64_MAX_VAL) {
    /* No separator at all -- the whole path is its own root (a single relative component). */
    root_path = *path;
  } else if (first_separator_idx == 0) {
    /* Absolute path -- the root is "/" itself, same special case path_parent needs. */
    root_path.path_length = 1;
    root_path.buffer[0]   = '/';
    root_path.buffer[1]   = '\0';
  } else {
    /* Up to but excluding the separator -- e.g. "usr/local/bin" -> "usr" (length 3, the index
       of the separator itself). Same bug this replaces as nexus_filesystem_path_parent had:
       first_separator_idx - 1 truncated one character too many, and never null-terminated. */
    root_path.path_length = first_separator_idx;
    nexus_memory_bytes_copy(path->buffer, root_path.buffer, root_path.path_length);
    root_path.buffer[root_path.path_length] = '\0';
  }

  return root_path;
}

static NEXUS_MAYBE_UNUSED NEXUS_NODISCARD NexusError *nexus_filesystem_path_exists(const NexusPath *path, boolean *path_exists,
                                                                                   boolean follow_symlinks) {
#if NEXUS_PLATFORM_POSIX
  struct stat path_info;
  NexusError *error;
  int         stat_result;

  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_filesystem_path_exists");
  stat_result = follow_symlinks ? stat(path->buffer, &path_info) : lstat(path->buffer, &path_info);

  if (stat_result == 0) {
    *path_exists = TRUE;
    NEXUS_PROGRAM_CONTEXT_POP();
    return &nexus_program_errors_success;
  }

  if (errno == ENOENT || errno == ENOTDIR) {
    /* Not a failure of this function -- a valid "no" answer to "does it exist". */
    *path_exists = FALSE;
    NEXUS_PROGRAM_CONTEXT_POP();
    return &nexus_program_errors_success;
  }

  error = NEXUS_PROGRAM_ERROR_FROM_ERRNO("failed to stat '%s'", path->buffer);
  NEXUS_PROGRAM_CONTEXT_POP();
  return error;
#else
  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_filesystem_path_exists");
  NEXUS_PLATFORM_UNSUPPORTED_RAISE("filesystem_path_exists");
#endif
}

/*
Summary: which kind of filesystem entry a path names.
Description: exhaustive over struct stat's S_ISxxx type bits -- NEXUS_PATH_TYPE_UNKNOWN exists
  only as a defensive fallback (POSIX's format bits are already exhaustive, so this should be
  unreachable in practice), not a real category a caller should expect to see.
  Every function below that produces one uses lstat, never stat -- lstat is a strict superset
  for a single path (identical to stat's result for anything that isn't itself a symlink, and
  the only way to ever observe NPT_SYMLINK at all), so there's no follow_symlinks parameter to
  get wrong here the way there would be with stat.
*/
typedef enum NexusPathType {
  NPT_UNKNOWN,
  NPT_FILE,
  NPT_DIRECTORY,
  NPT_SYMLINK,
  NPT_CHARACTER_DEVICE,
  NPT_BLOCK_DEVICE,
  NPT_FIFO,
  NPT_SOCKET,

  NPT_COUNT
} NexusPathType;

/*
Summary: struct stat's standard information about a path, in Nexus's own portable types -- never the raw platform dev_t/ino_t/mode_t/off_t/etc, which vary in width across platforms.
Fields:
  type                                numeric type. See NexusPathType.
  size                                size in bytes -- meaningful for NPT_FILE; filesystem-defined for other types
  owner_user_id, owner_group_id       numeric uid/gid that own the path
  hard_link_count                     number of hard links to this inode
  access_time                         last read
  modification_time                  last content write
  change_time                        last inode-metadata change (permissions, ownership, link
                                        count, or content) -- NOT creation time; POSIX stat has
                                        no creation-time field at all (only statx()'s
                                        STATX_BTIME does, on filesystems that track it)
*/
typedef struct NexusPathInfo {
  NexusPathType type;
  uint64        size;
  uint32        owner_user_id;
  uint32        owner_group_id;
  uint64        hard_link_count;
  NexusTime     access_time;
  NexusTime     modification_time;
  NexusTime     change_time;
} NexusPathInfo;

/*
Summary: internal -- classifies a raw st_mode bit pattern into a NexusPathType. Shared by every function below instead of repeating the S_ISxxx chain per call site.
Params: mode_bits  path_stat.st_mode, widened to Nexus's own portable type -- cast back to mode_t internally, POSIX-only.
*/
static NEXUS_MAYBE_UNUSED NexusPathType nexus_filesystem_path_type_from_mode_bits(uint32 mode_bits) {
#if NEXUS_PLATFORM_POSIX
  mode_t mode = (mode_t)mode_bits;

  if (S_ISREG(mode)) {
    return NPT_FILE;
  }
  if (S_ISDIR(mode)) {
    return NPT_DIRECTORY;
  }
  if (S_ISLNK(mode)) {
    return NPT_SYMLINK;
  }
  if (S_ISCHR(mode)) {
    return NPT_CHARACTER_DEVICE;
  }
  if (S_ISBLK(mode)) {
    return NPT_BLOCK_DEVICE;
  }
  if (S_ISFIFO(mode)) {
    return NPT_FIFO;
  }
  if (S_ISSOCK(mode)) {
    return NPT_SOCKET;
  }

  return NPT_UNKNOWN;
#else
  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_filesystem_path_type_from_mode_bits");
  NEXUS_PLATFORM_UNSUPPORTED_RAISE("filesystem_path_type_from_mode_bits");
#endif
}

/*
Summary: this path's NexusPathType (file/directory/symlink/device/fifo/socket) -- one lstat call.
Description: unlike path_exists, a nonexistent path is a real NEXUS_PROGRAM_ERROR_FROM_ERRNO here
  -- there's no type to report if there's nothing there, unlike path_exists where nonexistence
  is itself the (successful) answer.
See: nexus_filesystem_path_info_get, if more than just the type is needed -- one lstat call
  either way, so calling both back to back costs two.
*/
static NEXUS_MAYBE_UNUSED NEXUS_NODISCARD NexusError *nexus_filesystem_path_type_get(const NexusPath *path, NexusPathType *type) {
#if NEXUS_PLATFORM_POSIX
  struct stat path_stat;
  NexusError *error;

  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_filesystem_path_type_get");
  if (lstat(path->buffer, &path_stat) != 0) {
    error = NEXUS_PROGRAM_ERROR_FROM_ERRNO("failed to stat '%s'", path->buffer);
    NEXUS_PROGRAM_CONTEXT_POP();
    return error;
  }

  *type = nexus_filesystem_path_type_from_mode_bits((uint32)path_stat.st_mode);
  NEXUS_PROGRAM_CONTEXT_POP();
  return &nexus_program_errors_success;
#else
  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_filesystem_path_type_get");
  NEXUS_PLATFORM_UNSUPPORTED_RAISE("filesystem_path_type_get");
#endif
}

/*
Summary: TRUE if path names a regular file. See nexus_filesystem_path_type_get's Description re: nonexistence.
*/
static NEXUS_MAYBE_UNUSED NEXUS_NODISCARD NexusError *nexus_filesystem_path_is_file(const NexusPath *path, boolean *is_file) {
#if NEXUS_PLATFORM_POSIX
  struct stat path_stat;
  NexusError *error;

  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_filesystem_path_is_file");
  if (lstat(path->buffer, &path_stat) != 0) {
    error = NEXUS_PROGRAM_ERROR_FROM_ERRNO("failed to stat '%s'", path->buffer);
    NEXUS_PROGRAM_CONTEXT_POP();
    return error;
  }

  *is_file = nexus_filesystem_path_type_from_mode_bits((uint32)path_stat.st_mode) == NPT_FILE;
  NEXUS_PROGRAM_CONTEXT_POP();
  return &nexus_program_errors_success;
#else
  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_filesystem_path_is_file");
  NEXUS_PLATFORM_UNSUPPORTED_RAISE("filesystem_path_is_file");
#endif
}

/*
Summary: TRUE if path names a directory. See nexus_filesystem_path_type_get's Description re: nonexistence.
*/
static NEXUS_MAYBE_UNUSED NEXUS_NODISCARD NexusError *nexus_filesystem_path_is_directory(const NexusPath *path, boolean *is_directory) {
#if NEXUS_PLATFORM_POSIX
  struct stat path_stat;
  NexusError *error;

  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_filesystem_path_is_directory");
  if (lstat(path->buffer, &path_stat) != 0) {
    error = NEXUS_PROGRAM_ERROR_FROM_ERRNO("failed to stat '%s'", path->buffer);
    NEXUS_PROGRAM_CONTEXT_POP();
    return error;
  }

  *is_directory = nexus_filesystem_path_type_from_mode_bits((uint32)path_stat.st_mode) == NPT_DIRECTORY;
  NEXUS_PROGRAM_CONTEXT_POP();
  return &nexus_program_errors_success;
#else
  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_filesystem_path_is_directory");
  NEXUS_PLATFORM_UNSUPPORTED_RAISE("filesystem_path_is_directory");
#endif
}

/*
Summary: TRUE if path itself is a symlink (never resolved -- lstat, not stat). See nexus_filesystem_path_type_get's Description re: nonexistence.
*/
static NEXUS_MAYBE_UNUSED NEXUS_NODISCARD NexusError *nexus_filesystem_path_is_symlink(const NexusPath *path, boolean *is_symlink) {
#if NEXUS_PLATFORM_POSIX
  struct stat path_stat;
  NexusError *error;

  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_filesystem_path_is_symlink");
  if (lstat(path->buffer, &path_stat) != 0) {
    error = NEXUS_PROGRAM_ERROR_FROM_ERRNO("failed to stat '%s'", path->buffer);
    NEXUS_PROGRAM_CONTEXT_POP();
    return error;
  }

  *is_symlink = nexus_filesystem_path_type_from_mode_bits((uint32)path_stat.st_mode) == NPT_SYMLINK;
  NEXUS_PROGRAM_CONTEXT_POP();
  return &nexus_program_errors_success;
#else
  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_filesystem_path_is_symlink");
  NEXUS_PLATFORM_UNSUPPORTED_RAISE("filesystem_path_is_symlink");
#endif
}

/*
Summary: struct stat's standard information about path, in one lstat call -- see NexusPathInfo.
Description: a nonexistent path is a real NEXUS_PROGRAM_ERROR_FROM_ERRNO, same as
  nexus_filesystem_path_type_get -- see its Description. Timestamps are built via
  nexus_time_from_timespec with clock_origin=NCO_REAL (a filesystem timestamp's content is
  genuinely UTC-epoch) and precision=NTP_UNKNOWN (nothing in a stat() call reports the
  filesystem's actual timestamp granularity, so claiming otherwise would assert a precision
  that isn't real -- see NexusTimePrecision's NTP_UNKNOWN).
*/
static NEXUS_MAYBE_UNUSED NEXUS_NODISCARD NexusError *nexus_filesystem_path_info_get(const NexusPath *path, NexusPathInfo *info) {
#if NEXUS_PLATFORM_POSIX
  struct stat path_stat;
  NexusError *error;

  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_filesystem_path_info_get");
  if (lstat(path->buffer, &path_stat) != 0) {
    error = NEXUS_PROGRAM_ERROR_FROM_ERRNO("failed to stat '%s'", path->buffer);
    NEXUS_PROGRAM_CONTEXT_POP();
    return error;
  }

  info->type              = nexus_filesystem_path_type_from_mode_bits((uint32)path_stat.st_mode);
  info->size              = (uint64)path_stat.st_size;
  info->owner_user_id     = (uint32)path_stat.st_uid;
  info->owner_group_id    = (uint32)path_stat.st_gid;
  info->hard_link_count   = (uint64)path_stat.st_nlink;
  info->access_time       = nexus_time_from_timespec(path_stat.st_atim, NCO_REAL, NTP_UNKNOWN);
  info->modification_time = nexus_time_from_timespec(path_stat.st_mtim, NCO_REAL, NTP_UNKNOWN);
  info->change_time       = nexus_time_from_timespec(path_stat.st_ctim, NCO_REAL, NTP_UNKNOWN);

  NEXUS_PROGRAM_CONTEXT_POP();
  return &nexus_program_errors_success;
#else
  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_filesystem_path_info_get");
  NEXUS_PLATFORM_UNSUPPORTED_RAISE("filesystem_path_info_get");
#endif
}

/*
Summary: TRUE if path is absolute (starts with '/' on POSIX).
*/
static NEXUS_MAYBE_UNUSED boolean nexus_filesystem_path_is_absolute(const NexusPath *path) {
#if NEXUS_PLATFORM_POSIX
  return path->path_length > 0 && path->buffer[0] == '/';
#else
  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_filesystem_path_is_absolute");
  NEXUS_PLATFORM_UNSUPPORTED_RAISE("filesystem_path_is_absolute");
#endif
}

/*
Summary: TRUE if the path's filename has a nonempty extension.
Description: leading dots alone do not constitute an extension (e.g. ".gitignore"), and dots in parent directories are ignored. Does not access the filesystem.
*/
extern boolean nexus_filesystem_path_has_extension(const NexusPath *path);

/*
Summary: returns the path's extension without the leading dot, or NULL if absent.
Ownership: returned pointer borrows storage from path; do not free.
*/
extern const char *nexus_filesystem_path_extension_get(const NexusPath *path);

/*
Summary: TRUE if the path has the specified extension (without the leading dot).
Description: comparison is case-sensitive.
*/
extern boolean nexus_filesystem_path_extension_equals(const NexusPath *path, const char *extension);

/*
Summary: TRUE if the path's extension matches any string in extensions.
Description: extensions is a NexusDataArray of const char * values, without leading dots. Comparison is case-sensitive.
*/
extern boolean nexus_filesystem_path_extension_is_any_of(const NexusPath *path, const NexusDataArray *extensions);

/*
Summary: renames or moves old_path to new_path -- one rename() call, works identically for files and directories (POSIX rename() doesn't distinguish them, so Nexus doesn't invent a separate "move" name for the same operation).
Description: fails with a real NEXUS_PROGRAM_ERROR_FROM_ERRNO (errno EXDEV) if old_path and
  new_path are on different filesystems -- deliberately no copy+delete fallback. Nexus is a
  foundational library, not a framework: it surfaces the OS's actual semantics rather than
  silently paying for a slow path the caller didn't ask for. A caller that needs to move across
  filesystems composes it themselves from nexus_filesystem_file_copy /
  nexus_filesystem_directory_collect_allocated + nexus_filesystem_file_delete /
  nexus_filesystem_directory_delete.
*/
static NEXUS_MAYBE_UNUSED NEXUS_NODISCARD NexusError *nexus_filesystem_path_rename(const NexusPath *old_path, const NexusPath *new_path) {
#if NEXUS_PLATFORM_POSIX
  NexusError *error;

  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_filesystem_path_rename");
  if (rename(old_path->buffer, new_path->buffer) == 0) {
    NEXUS_PROGRAM_CONTEXT_POP();
    return &nexus_program_errors_success;
  }

  error = NEXUS_PROGRAM_ERROR_FROM_ERRNO("failed to rename '%s' to '%s'", old_path->buffer, new_path->buffer);
  NEXUS_PROGRAM_CONTEXT_POP();
  return error;
#else
  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_filesystem_path_rename");
  NEXUS_PLATFORM_UNSUPPORTED_RAISE("filesystem_path_rename");
#endif
}

/*
Summary: creates directory_path -- EEXIST is not an error (the directory existing is the caller's actual goal).
Description: does not create missing parent directories -- see nexus_filesystem_directory_create_parents.
*/
static NEXUS_MAYBE_UNUSED NEXUS_NODISCARD NexusError *nexus_filesystem_directory_create(const NexusPath *directory_path) {
#if NEXUS_PLATFORM_POSIX
  NexusError *error;

  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_filesystem_directory_create");
  if (mkdir(directory_path->buffer, 0777) == 0 || errno == EEXIST) {
    NEXUS_PROGRAM_CONTEXT_POP();
    return &nexus_program_errors_success;
  }

  error = NEXUS_PROGRAM_ERROR_FROM_ERRNO("failed to create directory '%s'", directory_path->buffer);
  NEXUS_PROGRAM_CONTEXT_POP();
  return error;
#else
  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_filesystem_directory_create");
  NEXUS_PLATFORM_UNSUPPORTED_RAISE("filesystem_directory_create");
#endif
}

/*
Summary: deletes file_path if it exists -- nonexistence is not an error (the file being gone is the caller's actual goal).
*/
static NEXUS_MAYBE_UNUSED NEXUS_NODISCARD NexusError *nexus_filesystem_file_delete(const NexusPath *file_path) {
#if NEXUS_PLATFORM_POSIX
  NexusError *error;

  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_filesystem_file_delete");
  if (remove(file_path->buffer) == 0 || errno == ENOENT) {
    NEXUS_PROGRAM_CONTEXT_POP();
    return &nexus_program_errors_success;
  }

  error = NEXUS_PROGRAM_ERROR_FROM_ERRNO("failed to delete '%s'", file_path->buffer);
  NEXUS_PROGRAM_CONTEXT_POP();
  return error;
#else
  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_filesystem_file_delete");
  NEXUS_PLATFORM_UNSUPPORTED_RAISE("filesystem_file_delete");
#endif
}

/* NexusFileHandle is declared earlier, in the MEMORY section -- nexus_memory_map needs it and
   MEMORY comes first in this file's ordering. See its Summary there. */

/*
Summary: how nexus_filesystem_file_open opens a file -- maps 1:1 onto fopen's mode strings.
*/
typedef enum NexusFileMode {
  NFM_READ,
  NFM_READ_BINARY,
  NFM_WRITE,
  NFM_WRITE_BINARY,
  NFM_APPEND,
  NFM_APPEND_BINARY,
  NFM_READ_PLUS,
  NFM_READ_BINARY_PLUS,
  NFM_WRITE_PLUS,
  NFM_WRITE_BINARY_PLUS,
  NFM_APPEND_PLUS,
  NFM_APPEND_BINARY_PLUS,

  NFM_COUNT
} NexusFileMode;

/*
Summary: a caller-owned or Nexus-allocated array of paths -- see nexus_filesystem_directory_collect / collect_allocated.
Ownership: paths==NULL, count==0 is the empty/uninitialized state. A list produced by
  collect_allocated owns its paths array and must go through nexus_filesystem_path_list_destroy;
  a list produced by collect into a caller-supplied array never does (the caller owns that
  storage however they created it).
*/
typedef struct NexusPathList {
  NexusPath *paths;
  uint64     count;
} NexusPathList;

/*
Summary: releases a NexusPathList's paths array, if collect_allocated produced it. Safe to call on an already-empty/destroyed list.
*/
static NEXUS_MAYBE_UNUSED void nexus_filesystem_path_list_destroy(NexusPathList *path_list) {
  if (path_list->paths != NULL) {
    nexus_memory_deallocate(path_list->paths);
  }

  path_list->paths = NULL;
  path_list->count = 0;
}

/*
Summary: called once per direct child during nexus_filesystem_directory_visit.
Returns: a real NexusError to stop the visit and propagate it; NEXUS_PROGRAM_ERROR_SUCCESS() to continue.
*/
typedef NexusError *NexusFilesystemVisitCallback(const NexusPath *entry_path, void *user_data);

/*
Summary: called once per entry during nexus_filesystem_directory_walk.
Description: entry_type comes for free -- directory_walk already has to determine it to decide
  whether to recurse into the entry, so passing it to the callback costs nothing extra (unlike
  directory_visit, which never stats an entry at all and so has no type to offer).
Returns: a real NexusError to stop the walk and propagate it; NEXUS_PROGRAM_ERROR_SUCCESS() to continue.
*/
typedef NexusError *NexusFilesystemWalkCallback(const NexusPath *entry_path, NexusPathType entry_type, void *user_data);

/*
Summary: called once per entry during a directory_collect* function to decide whether it belongs in the result. TRUE includes the entry. entry_type comes for free -- see NexusFilesystemWalkCallback's Description.
*/
typedef boolean NexusFilesystemFilterCallback(const NexusPath *entry_path, NexusPathType entry_type, const void *user_data);

/*
Summary: the facility identifier for synthetic filesystem-subsystem errors -- conditions that aren't an OS errno at all, so they can't go through NEXUS_ERROR_OS_FACILITY's passthrough.
Description: e.g. a regex pattern failing to compile: regcomp reports failure via its own
  return code, not errno, so NEXUS_PROGRAM_ERROR_FROM_ERRNO has nothing to convert.
*/
#define NEXUS_ERROR_FILESYSTEM_FACILITY NEXUS_PROGRAM_ERROR_FACILITY_IDENTIFIER_CREATE('F', 'S', 'Y', 'S')

/*
Summary: categories within NexusFilesystemError; currently just NFSEC_GENERIC.
*/
typedef enum NexusFilesystemErrorCategory {
  NFSEC_GENERIC,

  NFSEC_COUNT
} NexusFilesystemErrorCategory;

/*
Summary: the filesystem subsystem's own synthetic error list, defining NexusFilesystemError via NEXUS_ERROR_FACILITY_DEFINE below.
*/
#define NEXUS_FILESYSTEM_ERROR_LIST(X, library, facility)                                                                                            \
  X(NEXUS_FILESYSTEM_ERROR_REGEX_PATTERN_INVALID, NFSEC_GENERIC, 1, library, facility)                                                               \
  X(NEXUS_FILESYSTEM_ERROR_FILE_TOO_SMALL, NFSEC_GENERIC, 2, library, facility)                                                                      \
  X(NEXUS_FILESYSTEM_ERROR_FILE_SIZE_OUT_OF_RANGE, NFSEC_GENERIC, 3, library, facility)

NEXUS_ERROR_FACILITY_DEFINE(NexusFilesystemError, NEXUS_ERROR_LIBRARY_IDENTIFIER, NEXUS_ERROR_FILESYSTEM_FACILITY, NEXUS_FILESYSTEM_ERROR_LIST)

/*
Summary: opens file_path with the requested stream mode.
Description: NFM_READ_BINARY_PLUS preserves existing contents and size;
  NFM_WRITE_BINARY_PLUS creates or truncates the file to zero bytes.
  Neither mode extends a file for mapping. Call nexus_filesystem_file_resize
  explicitly before mapping more bytes than the file currently contains.
Ownership: caller owns the returned handle; close with nexus_filesystem_file_close.
Errors: invalid mode asserts; returns an OS error if fopen fails.
*/
extern NEXUS_NODISCARD NexusError *nexus_filesystem_file_open(const NexusPath *file_path, NexusFileMode mode, NexusFileHandle **out_file_handle);
/*
Summary: queries the OS-visible byte length of an open file.
Description: does not move the stream position or flush buffered writes;
  flush first when pending output must be included. Intended for regular
  files and anonymous memory-backed files; device sizes need not describe
  mappable ranges.
Params: out_size  out; byte length on success, zero on returned failure
Ownership: borrows file_handle; does not retain or close it.
Errors:
  NULL file_handle or out_size assert. Returns an OS error on fstat failure,
  or NEXUS_FILESYSTEM_ERROR_FILE_SIZE_OUT_OF_RANGE for an unrepresentable size.
Thread-safety: a size snapshot; synchronize with writes and resizing as needed.
*/
extern NEXUS_NODISCARD NexusError *nexus_filesystem_file_size_get(NexusFileHandle *file_handle, uint_large *out_size);
/*
Summary: grows or shrinks an open writable file to exactly size bytes.
Description:
  Synchronizes buffered I/O before resizing. Growth preserves the existing
  prefix and makes new bytes read as zero; shrink discards the removed data.
  Zero size is valid. The stream position is unchanged, even beyond the new
  EOF. Does not clear stream error/EOF indicators, remap existing views, or
  reserve physical storage. A resize failure may follow a successful flush.
  Shrinking a mapped file requires all users to stop accessing the removed
  range first; later accesses may cause SIGBUS.
Ownership: borrows file_handle; does not retain or close it.
Errors:
  NULL file_handle asserts. Returns NEXUS_FILESYSTEM_ERROR_FILE_SIZE_OUT_OF_RANGE
  when size exceeds off_t, or an OS error on flush/ftruncate failure.
Thread-safety: caller must synchronize stream I/O, resizing, and mapped access.
See: nexus_memory_map
*/
extern NEXUS_NODISCARD NexusError *nexus_filesystem_file_resize(NexusFileHandle *file_handle, uint_large size);
/*
Summary: creates an anonymous, unnamed file backed by memory rather than a path -- readable,
  writable, and resizable through the same nexus_filesystem_file_* functions as any file opened
  by nexus_filesystem_file_open, and mappable via nexus_memory_map by passing the resulting
  handle. Its content exists only as long as something holds the handle or a mapping of it open;
  there is no NexusPath for it and nothing to unlink.
Description: Linux-only for now (memfd_create; see nexus/adr/0004 for the precedent of shipping
  a Linux-only capability rather than blocking on macOS's/BSD's shm_open, which is named and
  needs an explicit unlink, or Windows's CreateFileMapping, a different API shape entirely) --
  outside NEXUS_PLATFORM_LINUX this is NEXUS_PLATFORM_UNSUPPORTED_RAISE, same as everything else
  in this file outside NEXUS_PLATFORM_POSIX.
Params:
  initial_size  the file's starting size in bytes; grow or shrink it later the same way you
    would any other open file.
Ownership: *out_file_handle is owned by the caller; release it with nexus_filesystem_file_close,
  same as a path-backed handle.
Errors: NEXUS_PROGRAM_PRECONDITION: initial_size must be greater than 0; out_file_handle must be
  non-NULL.
  Returns NEXUS_FILESYSTEM_ERROR_FILE_SIZE_OUT_OF_RANGE when initial_size
  exceeds off_t, or an OS error on creation, sizing, or fdopen failure.
*/
extern NEXUS_NODISCARD NexusError *nexus_filesystem_file_anonymous_create(uint_large initial_size, NexusFileHandle **out_file_handle);
/*
Summary: returns file_handle's raw platform file descriptor.
Description: the one deliberate crack in NexusFileHandle's opacity (see its own Summary) -- for
  a caller that has to hand the descriptor to something outside Nexus entirely, such as another
  process (a Wayland compositor receiving a shared memory-backed buffer's fd, say). Nexus's own
  functions never need this; reach for nexus_filesystem_file_read/write/etc. instead unless the
  raw descriptor itself is genuinely the thing required.
Errors: NEXUS_PROGRAM_PRECONDITION: file_handle must be non-NULL.
*/
extern NEXUS_NODISCARD int32 nexus_filesystem_file_descriptor_get(NexusFileHandle *file_handle);
/*
Summary: closes file_handle. NULL is not an error (mirrors fclose(NULL) being a no-op contract elsewhere in this header, e.g. nexus_filesystem_file_flush).
Description: fclose can fail even after every prior read/write succeeded (a buffered write's
  error surfacing late) -- returning NexusError* here, like every other function in this
  section, rather than discarding that the way a void return would.
*/
extern NEXUS_NODISCARD NexusError *nexus_filesystem_file_close(NexusFileHandle *file_handle);
/*
Summary: reads up to byte_length bytes sequentially from file_handle's current position.
Params: out_reached_eof  set TRUE only when the stream is actually exhausted, never on a mere short read that isn't EOF
*/
extern NEXUS_NODISCARD NexusError *nexus_filesystem_file_read(NexusFileHandle *file_handle, byte *buffer, uint_large byte_length,
                                                              uint_large *out_bytes_read, boolean *out_reached_eof);
/*
Summary: reads up to byte_length bytes starting at start_byte, seeking there first -- random access, independent of the handle's current position.
*/
extern NEXUS_NODISCARD NexusError *nexus_filesystem_file_read_at(NexusFileHandle *file_handle, byte *buffer, uint_large byte_length,
                                                                 uint32 start_byte, uint_large *out_bytes_read);
/*
Summary: reads one line (delimiter stripped, not included) into a caller-owned fixed buffer -- truncates silently if the line doesn't fit, same as fgets.
*/
extern NEXUS_NODISCARD NexusError *nexus_filesystem_file_read_line(NexusFileHandle *file_handle, char *buffer, uint_large buffer_max_length,
                                                                   uint_large *out_bytes_read);
/*
Summary: reads one line of any length into a Nexus-owned, growable buffer -- never truncates.
Description: handles \n, \r\n, and a bare trailing \r as line endings, none of which end up in
  the returned text. *io_buffer is allocated/grown via nexus_memory_allocate/reallocate as
  needed; it is the caller's to nexus_memory_deallocate whenever they're done reusing it across
  calls (this function only ever grows it, never frees it).
Params:
  io_buffer           in/out -- pass *io_buffer=NULL, *io_buffer_capacity=0 the first time; reuse both across repeated calls to amortize growth
  io_buffer_capacity  in/out -- current allocated size of *io_buffer
*/
extern NEXUS_NODISCARD NexusError *nexus_filesystem_file_read_line_allocated(NexusFileHandle *file_handle, char **io_buffer,
                                                                             uint_large *io_buffer_capacity, uint_large *out_bytes_read,
                                                                             boolean *out_reached_eof);
extern NEXUS_NODISCARD NexusError *nexus_filesystem_file_write(NexusFileHandle *file_handle, const byte *bytes, uint_large length,
                                                               uint_large *out_bytes_written);
/*
Summary: flushes file_handle's buffered writes to the OS. NULL is a no-op, not an error.
*/
extern NEXUS_NODISCARD NexusError *nexus_filesystem_file_flush(NexusFileHandle *file_handle);
/*
Summary: copies source_path's contents onto destination_path, creating destination_path's parent directories if needed. Overwrites destination_path if it already exists.
*/
extern NEXUS_NODISCARD NexusError *nexus_filesystem_file_copy(const NexusPath *source_path, const NexusPath *destination_path);
/*
Summary: creates every missing ancestor directory of file_path (not file_path itself) -- see nexus_filesystem_directory_create_parents.
*/
extern NEXUS_NODISCARD NexusError *nexus_filesystem_file_ensure_parent_directory(const NexusPath *file_path);

/*
Summary: like nexus_filesystem_directory_create, but creates every missing ancestor directory too (mkdir -p).
*/
extern NEXUS_NODISCARD NexusError *nexus_filesystem_directory_create_parents(const NexusPath *directory_path);
/*
Summary: deletes directory_path. Nonexistence is not an error. recursive=FALSE only removes an empty directory (same failure as POSIX rmdir on a non-empty one); recursive=TRUE deletes its contents first.
Description: the recursive case classifies each entry via nexus_filesystem_path_is_directory
  (lstat-based) specifically so a symlink to a directory is never followed and recursed into --
  it is deleted as the leaf (the link itself) it is, exactly like a real "rm -rf" never
  descending through a symlink.
*/
extern NEXUS_NODISCARD NexusError *nexus_filesystem_directory_delete(const NexusPath *directory_path, boolean recursive);
/*
Summary: calls callback once for each direct child of directory_path (one level, never recurses). "." and ".." are never visited.
*/
extern NEXUS_NODISCARD NexusError *nexus_filesystem_directory_visit(const NexusPath *directory_path, NexusFilesystemVisitCallback *callback,
                                                                    void *user_data);
/*
Summary: like nexus_filesystem_directory_visit, but optionally descends into subdirectories -- callback receives each entry's type for free (see NexusFilesystemWalkCallback). Never follows a symlink to recurse into it (see nexus_filesystem_directory_delete's Description -- same lstat-based reasoning).
*/
extern NEXUS_NODISCARD NexusError *nexus_filesystem_directory_walk(const NexusPath *directory_path, boolean recursive,
                                                                   NexusFilesystemWalkCallback *callback, void *user_data);
/*
Summary: like nexus_filesystem_directory_walk, but collects matching entries into a list instead of calling a callback per entry.
Description: filter is nullable (NULL means "everything matches"). Two calling shapes: pass an
  already-sized out_path_list->paths (out_path_list->count as its capacity) to fill a
  caller-owned buffer -- NEXUS_PROGRAM_PRECONDITION_ALWAYS if it turns out too small; or pass
  out_path_list->paths==NULL to only count matches into out_path_list->count, so the caller can
  size a buffer (or just call nexus_filesystem_directory_collect_allocated instead). Costs one
  full walk per call either way -- calling it once to count then again to fill costs two.
*/
extern NEXUS_NODISCARD NexusError *nexus_filesystem_directory_collect(const NexusPath *directory_path, boolean recursive,
                                                                      NexusFilesystemFilterCallback *filter, const void *filter_user_data,
                                                                      NexusPathList *out_path_list);
/*
Summary: like nexus_filesystem_directory_collect, but allocates out_path_list->paths itself (via nexus_memory_allocate) sized exactly to the match count -- walks twice internally (count, then fill).
Ownership: caller must nexus_filesystem_path_list_destroy(out_path_list) when done.
*/
extern NEXUS_NODISCARD NexusError *nexus_filesystem_directory_collect_allocated(const NexusPath *directory_path, boolean recursive,
                                                                                NexusFilesystemFilterCallback *filter, const void *filter_user_data,
                                                                                NexusPathList *out_path_list);
/*
Summary: like nexus_filesystem_directory_collect_allocated, but the filter is a POSIX extended regular expression matched against each entry's full path, instead of a callback.
Errors: a NEXUS_ERROR_OS_FACILITY error carrying regcomp's own message if regex_pattern fails to compile.
*/
extern NEXUS_NODISCARD NexusError *nexus_filesystem_directory_collect_matching_allocated(const NexusPath *directory_path, boolean recursive,
                                                                                         const char *regex_pattern, NexusPathList *out_path_list);

/*
Summary: which standard, well-known directory nexus_filesystem_special_directory_get should resolve.
Description: POSIX/Linux only -- e.g. NSD_OPT (the FHS's /opt) has no Windows equivalent at all,
  and Nexus doesn't implement Windows regardless (see the getter's own NEXUS_PLATFORM_POSIX
  guard). The set below is the common, standard subset worth having a foundational library
  resolve for you: user-specific locations POSIX/XDG already standardizes, so getting them
  wrong (or reinventing the fallback chain per-caller) is pure risk with no offsetting benefit.
Fields:
  NSD_HOME    $HOME, or the current user's passwd entry if HOME isn't set (e.g. under cron)
  NSD_TEMP    $TMPDIR, $TMP, $TEMP, or /tmp -- first one set wins
  NSD_CONFIG  $XDG_CONFIG_HOME, or $HOME/.config -- where a well-behaved program keeps its config
  NSD_CACHE   $XDG_CACHE_HOME, or $HOME/.cache -- disposable, regeneratable data
  NSD_DATA    $XDG_DATA_HOME, or $HOME/.local/share -- persistent, non-disposable application data
  NSD_OPT     /opt -- the FHS's fixed location for self-contained third-party packages; not
                sourced from an environment variable, there isn't one
*/
typedef enum NexusSpecialDirectory {
  NSD_HOME,
  NSD_TEMP,
  NSD_CONFIG,
  NSD_CACHE,
  NSD_DATA,
  NSD_OPT,

  NSD_COUNT
} NexusSpecialDirectory;

extern NEXUS_NODISCARD NexusError *nexus_filesystem_special_directory_get(NexusSpecialDirectory directory, NexusPath *out_path);

/* ============================================================================ */
/* ---------------------------------------------------------------------------- */
/* STDIO                                                                        */
/* ---------------------------------------------------------------------------- */
/* ============================================================================ */

/*
Summary: writes byte_length bytes from bytes to stdout.
Params: out_bytes_written  out; bytes actually written before any error
Errors: NEXUS_PROGRAM_ERROR_FROM_ERRNO if the underlying stream reports a write error.
*/
extern NEXUS_NODISCARD NexusError *nexus_stdio_stdout_write(const byte *bytes, uint_large byte_length, uint_large *out_bytes_written);
/*
Summary: writes a NUL-terminated string to stdout.
*/
extern NEXUS_NODISCARD NexusError *nexus_stdio_stdout_write_cstring(const char *text);
/*
Summary: formats format+... printf-style and writes the result to stdout.
Description: formats into a fixed 4096-byte internal buffer -- a formatted message longer than
  that is silently truncated, same as nexus_strings_string_format_with_truncation.
*/
extern NEXUS_NODISCARD NexusError *nexus_stdio_stdout_write_formatted(const char *format, ...);
/*
Summary: flushes stdout's buffered writes to the OS.
*/
extern NEXUS_NODISCARD NexusError *nexus_stdio_stdout_flush(void);
/*
Summary: writes byte_length bytes from bytes to stderr.
Params: out_bytes_written  out; bytes actually written before any error
Errors: NEXUS_PROGRAM_ERROR_FROM_ERRNO if the underlying stream reports a write error.
*/
extern NEXUS_NODISCARD NexusError *nexus_stdio_stderr_write(const byte *bytes, uint_large byte_length, uint_large *out_bytes_written);
/*
Summary: writes a NUL-terminated string to stderr.
*/
extern NEXUS_NODISCARD NexusError *nexus_stdio_stderr_write_cstring(const char *text);
/*
Summary: formats format+... printf-style and writes the result to stderr.
Description: formats into a fixed 4096-byte internal buffer -- a formatted message longer than
  that is silently truncated, same as nexus_strings_string_format_with_truncation.
*/
extern NEXUS_NODISCARD NexusError *nexus_stdio_stderr_write_formatted(const char *format, ...);
/*
Summary: flushes stderr's buffered writes to the OS.
*/
extern NEXUS_NODISCARD NexusError *nexus_stdio_stderr_flush(void);
/*
Summary: reads one line from stdin into buffer (delimiter stripped, not included) -- truncates silently if the line doesn't fit, same as fgets.
Params: out_reached_eof  set TRUE only when stdin is actually exhausted, never on a mere short read that isn't EOF
Description: flushes stdout first, so a prompt printed just before this call appears before the read blocks.
Errors: NEXUS_PROGRAM_ERROR_FROM_ERRNO if the underlying stream reports a read error.
*/
extern NEXUS_NODISCARD NexusError *nexus_stdio_stdin_read_line(char *buffer, uint_large buffer_max_length, boolean *out_reached_eof);
/*
Summary: TRUE when stdin is connected to an interactive terminal device rather than a pipe/file/redirect.
*/
extern boolean nexus_stdio_stdin_is_terminal(void);
/*
Summary: reads exactly byte_length bytes from stdin (binary), blocking until they arrived or stdin ended.
Description: for framed protocols whose messages are not line-terminated. A short result means stdin reached its end.
Params: out_bytes_read  the number of bytes stored; less than byte_length only at the end of stdin
Errors: NEXUS_PROGRAM_ERROR_FROM_ERRNO if the underlying stream reports a read error.
*/
extern NEXUS_NODISCARD NexusError *nexus_stdio_stdin_read(byte *buffer, uint_large byte_length, uint_large *out_bytes_read);

/* ============================================================================ */
/* ---------------------------------------------------------------------------- */
/* ENVIRONMENT                                                                  */
/* ---------------------------------------------------------------------------- */
/* ============================================================================ */

/*
Summary: reads environment variable name into buffer.
Description: does not cache -- another part of the process (or, via nexus_environment_variable_set,
  this one) can change the environment between calls, so a cached value could go stale; see
  adr/00XX. A client that wants to avoid repeated lookups owns that decision and that cache.
Params: out_found  set FALSE (buffer left empty) when name isn't set at all -- not an error
Errors: NEXUS_PROGRAM_PRECONDITION if buffer is too small for a variable that IS set
*/
static NEXUS_MAYBE_UNUSED NEXUS_NODISCARD NexusError *nexus_environment_variable_get(const char *name, char *buffer, uint_large buffer_max_length,
                                                                                     boolean *out_found) {
#if NEXUS_PLATFORM_POSIX
  const char             *value;
  NexusStringFormatResult copy_result;

  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_environment_variable_get");
  value = getenv(name);
  if (value == NULL) {
    *out_found = FALSE;
    buffer[0]  = '\0';
    NEXUS_PROGRAM_CONTEXT_POP();
    return &nexus_program_errors_success;
  }

  copy_result = nexus_strings_string_format_with_truncation(buffer, buffer_max_length, "%s", value);
  NEXUS_PROGRAM_PRECONDITION(!copy_result.truncated, "environment_variable_get: buffer too small for '%s' (%llu bytes needed, %llu given)", name,
                             (unsigned long long)copy_result.required_length, (unsigned long long)buffer_max_length);
  (void)copy_result; /* only read by the PRECONDITION check, which compiles away entirely outside NEXUS_DEVELOPER_MODE */

  *out_found = TRUE;
  NEXUS_PROGRAM_CONTEXT_POP();
  return &nexus_program_errors_success;
#else
  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_environment_variable_get");
  NEXUS_PLATFORM_UNSUPPORTED_RAISE("environment_variable_get");
#endif
}

/*
Summary: sets (or replaces) environment variable name to value in the current process.
*/
static NEXUS_MAYBE_UNUSED NEXUS_NODISCARD NexusError *nexus_environment_variable_set(const char *name, const char *value) {
#if NEXUS_PLATFORM_POSIX
  NexusError *error;

  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_environment_variable_set");
  if (setenv(name, value, 1) == 0) {
    NEXUS_PROGRAM_CONTEXT_POP();
    return &nexus_program_errors_success;
  }

  error = NEXUS_PROGRAM_ERROR_FROM_ERRNO("failed to set environment variable '%s'", name);
  NEXUS_PROGRAM_CONTEXT_POP();
  return error;
#else
  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_environment_variable_set");
  NEXUS_PLATFORM_UNSUPPORTED_RAISE("environment_variable_set");
#endif
}

/*
Summary: removes environment variable name from the current process, if it exists.
*/
static NEXUS_MAYBE_UNUSED NEXUS_NODISCARD NexusError *nexus_environment_variable_unset(const char *name) {
#if NEXUS_PLATFORM_POSIX
  NexusError *error;

  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_environment_variable_unset");
  if (unsetenv(name) == 0) {
    NEXUS_PROGRAM_CONTEXT_POP();
    return &nexus_program_errors_success;
  }

  error = NEXUS_PROGRAM_ERROR_FROM_ERRNO("failed to unset environment variable '%s'", name);
  NEXUS_PROGRAM_CONTEXT_POP();
  return error;
#else
  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_environment_variable_unset");
  NEXUS_PLATFORM_UNSUPPORTED_RAISE("environment_variable_unset");
#endif
}

/* ============================================================================ */
/* ---------------------------------------------------------------------------- */
/* SIGNALS                                                                      */
/* ---------------------------------------------------------------------------- */
/* ============================================================================ */

/*
Summary: a process-level signal Nexus can capture portably.
*/
typedef enum NexusSignal {
  NEXUS_SIGNAL_INTERRUPT, /* SIGINT */
  NEXUS_SIGNAL_TERMINATE, /* SIGTERM */

  NEXUS_SIGNAL_COUNT
} NexusSignal;

/*
Summary: installs Nexus's handling for signal -- received afterward, it's recorded (see nexus_signals_received_get/exchange), never acted on inside the handler itself.
*/
extern NEXUS_NODISCARD NexusError *nexus_signals_capture(NexusSignal signal);
/*
Summary: restores the platform's default handling for signal and clears its received state.
*/
extern NEXUS_NODISCARD NexusError *nexus_signals_release(NexusSignal signal);
/*
Summary: TRUE if signal has been received since capture or the most recent clear/exchange. Does not clear it.
*/
extern boolean nexus_signals_received_get(NexusSignal signal);
/*
Summary: like nexus_signals_received_get, but also clears the received state -- an atomic check-and-clear.
*/
extern boolean nexus_signals_received_exchange(NexusSignal signal);
/*
Summary: clears signal's received state without reporting it first.
*/
extern void nexus_signals_received_clear(NexusSignal signal);

/* ============================================================================ */
/* ---------------------------------------------------------------------------- */
/* PROCESS                                                                      */
/* ---------------------------------------------------------------------------- */
/* ============================================================================ */

/*
Summary: a child OS process spawned by nexus_process_spawn.
Description: Linux-only (see nexus/adr/0004) -- outside NEXUS_PLATFORM_LINUX every function
  below is NEXUS_PLATFORM_UNSUPPORTED_RAISE. Owns a pidfd and, for each stream opened with
  NEXUS_PROCESS_STREAM_PIPE, the parent-side end of that pipe. Reaping the child (via
  nexus_process_poll, nexus_process_wait, or nexus_process_communicate) is the caller's
  responsibility, same as any spawned process -- an exited-but-never-reaped child is a zombie
  until either something reaps it or this process itself exits.
*/
typedef struct NexusProcess NexusProcess;

/*
Summary: how one of a spawned process's three standard streams is connected.
*/
typedef enum NexusProcessStreamMode {
  NEXUS_PROCESS_STREAM_INHERIT, /* shares this process's own fd, e.g. visible in the same terminal */
  NEXUS_PROCESS_STREAM_PIPE,    /* a pipe nexus_process_communicate writes to / reads from */
  NEXUS_PROCESS_STREAM_DISCARD, /* redirected to /dev/null */

  NEXUS_PROCESS_STREAM_MODE_COUNT
} NexusProcessStreamMode;

/*
Summary: nexus_process_spawn's specification for the child to start.
Params:
  argv               borrowed, not retained past the call; NULL-terminated; argv[0] is the
                       program (searched via PATH only if it contains no '/', per execve/execvp
                       convention)
  envp               nullable, borrowed, not retained; NULL-terminated when given; NULL means
                       the child inherits this process's current environment
  working_directory  nullable, borrowed, not retained; NULL means the child inherits this
                       process's current working directory
*/
typedef struct NexusProcessSpawnOptions {
  const char *const     *argv;
  const char *const     *envp;
  const char            *working_directory;
  NexusProcessStreamMode stdin_mode;
  NexusProcessStreamMode stdout_mode;
  NexusProcessStreamMode stderr_mode;
} NexusProcessSpawnOptions;

/*
Summary: how a reaped process finished.
*/
typedef enum NexusProcessExitKind {
  NEXUS_PROCESS_EXIT_NORMAL,   /* ran to completion or called exit()/_exit(); detail is the exit code */
  NEXUS_PROCESS_EXIT_SIGNALED, /* terminated by a signal, e.g. a crash or an external kill; detail is the signal number */

  NEXUS_PROCESS_EXIT_KIND_COUNT
} NexusProcessExitKind;

/*
Summary: a reaped process's outcome -- the mechanism nexus_process_wait/poll/communicate use to
  report a crash (NEXUS_PROCESS_EXIT_SIGNALED) exactly like any other outcome, with no dependency
  on whether the child itself made any C-level distinction between an assert, a precondition, or
  a raw segfault -- from here, all of those are just "terminated by a signal".
*/
typedef struct NexusProcessExitStatus {
  NexusProcessExitKind kind;
  int32                detail;
} NexusProcessExitStatus;

/*
Summary: a signal nexus_process_signal can send to a running child.
Description: deliberately a separate type from NexusSignal (SIGNALS section above): NexusSignal
  models a signal delivered TO this process; this type models a signal THIS process sends TO a
  child -- same underlying platform signal numbers, opposite direction, different contract (that
  section captures and defers; this one fires immediately via pidfd_send_signal).
*/
typedef enum NexusProcessSignal {
  NEXUS_PROCESS_SIGNAL_INTERRUPT, /* SIGINT */
  NEXUS_PROCESS_SIGNAL_TERMINATE, /* SIGTERM */
  NEXUS_PROCESS_SIGNAL_KILL,      /* SIGKILL -- cannot be caught, blocked, or ignored by the child */

  NEXUS_PROCESS_SIGNAL_COUNT
} NexusProcessSignal;

/*
Summary: spawns options->argv[0] as a child process.
Description: forks, then in the child only calls async-signal-safe operations (dup2/close/chdir/
  execve/_exit) before either exec succeeds or _exit(127) runs -- never anything from Nexus's own
  error/context/allocation machinery, which is not safe to touch between fork and exec. A setup
  failure in the child (a bad working_directory, a stream that can't be redirected, or execve
  itself failing, e.g. ENOENT) is relayed back to the parent over a close-on-exec pipe and
  reported here as an ordinary errno-based NexusError -- the same failure a direct, unforked
  execve would have reported, just observed from the parent instead of losing it in an unreachable
  child. On any failure, no child is left running and every descriptor this call opened is closed.
Params: out_process  out; on success, an owned handle this process later releases with
  nexus_process_destroy
Ownership: *out_process is owned by the caller on success, untouched on failure.
Errors: a NULL options/out_process, a NULL or empty options->argv, or a NULL options->argv[0] is
  a programmer error (asserts). Every other failure (pipe/fork/dup2/chdir/execve, all real errno
  conditions) is a NexusError via NEXUS_PROGRAM_ERROR_FROM_ERRNO.
Thread-safety: safe to call concurrently for distinct calls; like fork() generally, avoid calling
  this while another thread might be mutating state this process's child-side setup depends on.
See: adr/0004
*/
extern NEXUS_NODISCARD NexusError *nexus_process_spawn(const NexusProcessSpawnOptions *options, NexusProcess **out_process);
/*
Summary: process's OS process ID.
Errors: NULL process is a programmer error (asserts).
*/
extern int32 nexus_process_id_get(const NexusProcess *process);
/*
Summary: process's parent-side stdin pipe write-fd, for direct incremental writes while process
  is still running.
Description: an alternative to nexus_process_communicate's one-shot write, for a caller that needs
  multiple write/read round-trips against a still-running child (e.g. dispatching more than one
  request to a long-lived worker process) rather than draining it once to exit. The fd's lifecycle
  stays owned by process -- nexus_process_destroy closes it; the caller must never close it
  independently. Once nexus_process_communicate is called on this process, that call takes over
  the fd (may close it itself on EPIPE) -- do not mix communicate with direct use of this fd on
  the same process.
Errors: a NULL process, or a process whose stdin_mode was not NEXUS_PROCESS_STREAM_PIPE at spawn
  time, is a programmer error (asserts).
See: adr/0004
*/
extern int32 nexus_process_stdin_fd_get(const NexusProcess *process);
/*
Summary: process's parent-side stdout pipe read-fd -- see nexus_process_stdin_fd_get.
Errors: a NULL process, or a process whose stdout_mode was not NEXUS_PROCESS_STREAM_PIPE at spawn
  time, is a programmer error (asserts).
See: adr/0004
*/
extern int32 nexus_process_stdout_fd_get(const NexusProcess *process);
/*
Summary: checks whether process has exited yet, without blocking.
Description: once a process is observed to have exited (by this, nexus_process_wait, or
  nexus_process_communicate), it is reaped immediately and the same NexusProcessExitStatus is
  cached and returned again on every later call -- never re-waited, never re-reaped.
Params: out_status  untouched when *out_has_exited comes back FALSE
Errors: NULL process/out_status/out_has_exited is a programmer error (asserts). A real poll/wait
  failure (not "still running") is a NexusError via NEXUS_PROGRAM_ERROR_FROM_ERRNO.
*/
extern NEXUS_NODISCARD NexusError *nexus_process_poll(NexusProcess *process, NexusProcessExitStatus *out_status, boolean *out_has_exited);
/*
Summary: like nexus_process_poll, but blocks until process exits or timeout_milliseconds elapses.
Params: timeout_milliseconds  negative waits with no time limit; zero polls once without blocking
Errors: same as nexus_process_poll.
*/
extern NEXUS_NODISCARD NexusError *nexus_process_wait(NexusProcess *process, int32 timeout_milliseconds, NexusProcessExitStatus *out_status,
                                                      boolean *out_has_exited);
/*
Summary: writes stdin_data to process, drains its stdout/stderr as they arrive, and waits for it
  to exit -- all in one bounded loop, avoiding the classic pipe deadlock of writing a large stdin
  in full before ever reading a child's output (a child that fills its own stdout pipe waiting
  for a reader, while the parent is still blocked writing stdin, deadlocks both sides).
Description: a stream only accumulates into out_stdout/out_stderr if its spawn-time mode was
  NEXUS_PROCESS_STREAM_PIPE; otherwise the corresponding out parameter must be NULL and is left
  untouched. If process's stdin was not piped, stdin_data/stdin_size must be 0/NULL. Once process
  exits, any stdin_data left unwritten is simply discarded, including when the child closes its
  stdin read end early (EPIPE) without consuming everything offered -- a partial consumer is not
  a failure of this call, matching every mainstream language's "communicate" primitive.
Params:
  stdin_data   borrowed, not retained; ignored if stdin_size is 0
  out_stdout   nullable; must be non-NULL exactly when process's stdout_mode was
                NEXUS_PROCESS_STREAM_PIPE; bytes are appended via nexus_data_list_append_uninitialized
                (element_size 1), not cleared first
  out_stderr   like out_stdout, for stderr
  timeout_milliseconds  negative waits with no time limit; see nexus_process_wait
Errors: NULL process/out_status/out_has_exited, an out_stdout/out_stderr that doesn't match its
  stream's spawn-time mode, or stdin_data/stdin_size given for a non-piped stdin is a programmer
  error (asserts). A real poll/read/write failure (not EPIPE while writing stdin, see Description)
  is a NexusError via NEXUS_PROGRAM_ERROR_FROM_ERRNO.
*/
extern NEXUS_NODISCARD NexusError *nexus_process_communicate(NexusProcess *process, const void *stdin_data, uint_large stdin_size,
                                                             NexusDataList *out_stdout, NexusDataList *out_stderr, int32 timeout_milliseconds,
                                                             NexusProcessExitStatus *out_status, boolean *out_has_exited);
/*
Summary: sends signal to process via pidfd_send_signal -- race-free even if process's pid has
  since been reused by an unrelated process, unlike kill(pid, ...) against a stored pid number.
Errors: NULL process or signal >= NEXUS_PROCESS_SIGNAL_COUNT is a programmer error (asserts). A
  real send failure, including ESRCH because process already exited, is a NexusError via
  NEXUS_PROGRAM_ERROR_FROM_ERRNO.
*/
extern NEXUS_NODISCARD NexusError *nexus_process_signal(NexusProcess *process, NexusProcessSignal signal);
/*
Summary: releases process's Nexus-side resources (pidfd, any open pipe fds, the handle itself).
Description: never sends any signal and never blocks waiting for exit, regardless of whether
  process has exited yet -- destroy is resource release, not a lifecycle action. If process is
  still running when destroyed, it keeps running untouched; if it later exits with nothing left
  to reap it (nexus_process_poll/wait/communicate were never called first), it becomes a zombie
  until this program exits, same as any orphaned unreaped child -- a caller that needs a
  running process actually stopped calls nexus_process_signal first and reaps the result before
  destroying.
Params: process  NULL is accepted as a no-op.
*/
extern void nexus_process_destroy(NexusProcess *process);

/* ============================================================================ */
/* ---------------------------------------------------------------------------- */
/* THREADS                                                                      */
/* ---------------------------------------------------------------------------- */
/* ============================================================================ */

/*
This section also backs NEXUS_THREAD_SAFE (below), the single compile-time switch that gates
locking in the error pool (n_program.c), the memory debugger (n_memory.c), and program_context
(n_program.c) -- see each file's own guard comment for what it protects and why. Default on;
a caller who knows a given build is genuinely single-threaded can define it to FALSE before
including nexus.h to drop every one of those locks and pay zero overhead for them.
*/

#ifndef NEXUS_THREAD_SAFE
#  define NEXUS_THREAD_SAFE TRUE
#endif

/*
Summary: the facility identifier for synthetic threads-subsystem errors -- currently just sending on a closed NexusChannel, which isn't an OS errno condition.
*/
#define NEXUS_ERROR_THREADS_FACILITY NEXUS_PROGRAM_ERROR_FACILITY_IDENTIFIER_CREATE('T', 'H', 'R', 'D')

/*
Summary: categories within NexusThreadsError; currently just NTEC_GENERIC.
*/
typedef enum NexusThreadsErrorCategory {
  NTEC_GENERIC,

  NTEC_COUNT
} NexusThreadsErrorCategory;

#define NEXUS_THREADS_ERROR_LIST(X, library, facility) X(NEXUS_THREADS_ERROR_CHANNEL_CLOSED, NTEC_GENERIC, 1, library, facility)

NEXUS_ERROR_FACILITY_DEFINE(NexusThreadsError, NEXUS_ERROR_LIBRARY_IDENTIFIER, NEXUS_ERROR_THREADS_FACILITY, NEXUS_THREADS_ERROR_LIST)

/*
Summary: an OS thread created by nexus_threads_create.
*/
typedef struct NexusThread NexusThread;
/*
Summary: a recursive-capable OS mutex.
*/
typedef struct NexusMutex NexusMutex;
/*
Summary: an OS condition variable, used together with a NexusMutex.
*/
typedef struct NexusCond NexusCond;
/*
Summary: a counting OS semaphore.
*/
typedef struct NexusSemaphore NexusSemaphore;
/*
Summary: a Go-style WaitGroup -- a counter that nexus_threads_waitgroup_wait blocks on until it reaches zero.
*/
typedef struct NexusThreadsWaitGroup NexusThreadsWaitGroup;

/*
Summary: a function nexus_threads_create runs on the new thread, receiving the user_data it was given.
*/
typedef void (*NexusThreadFunc)(void *user_data);

/*
Summary: suspends the calling thread for at least duration (zero or negative returns immediately).
*/
extern void nexus_threads_sleep(NexusDuration duration);
/*
Summary: like nexus_threads_sleep, but busy-waits instead of yielding to the scheduler -- only for durations short enough that a context switch would cost more than the wait itself.
*/
extern void nexus_threads_spin_wait(NexusDuration duration);

/*
Summary: spawns a new thread running entry_func(user_data).
*/
extern NEXUS_NODISCARD NexusError *nexus_threads_create(NexusThread **out_thread, NexusThreadFunc entry_func, void *user_data);
/*
Summary: blocks until thread finishes, then frees its handle. Every created thread must eventually be joined (or the OS thread and its handle both leak).
*/
extern NEXUS_NODISCARD NexusError *nexus_threads_join(NexusThread *thread);

/*
Summary: allocates and initializes a recursive-capable mutex.
*/
extern NEXUS_NODISCARD NexusError *nexus_threads_mutex_create(NexusMutex **out_mutex);
/*
Summary: blocks until exclusive ownership of mutex is acquired.
*/
extern NEXUS_NODISCARD NexusError *nexus_threads_mutex_lock(NexusMutex *mutex);
/*
Summary: acquires exclusive ownership of mutex without blocking. Returns TRUE if acquired.
*/
extern boolean nexus_threads_mutex_try_lock(NexusMutex *mutex);
/*
Summary: releases exclusive ownership of mutex.
*/
extern NEXUS_NODISCARD NexusError *nexus_threads_mutex_unlock(NexusMutex *mutex);
/*
Summary: frees mutex. mutex must be unlocked first.
*/
extern void nexus_threads_mutex_destroy(NexusMutex *mutex);

/*
Summary: allocates and initializes a condition variable.
*/
extern NEXUS_NODISCARD NexusError *nexus_threads_cond_create(NexusCond **out_cond);
/*
Summary: atomically unlocks mutex and blocks until signaled, then re-acquires mutex before returning.
*/
extern NEXUS_NODISCARD NexusError *nexus_threads_cond_wait(NexusCond *cond, NexusMutex *mutex);
/*
Summary: like nexus_threads_cond_wait, but gives up after duration. Returns TRUE if signaled, FALSE if it timed out.
*/
extern boolean nexus_threads_cond_wait_timeout(NexusCond *cond, NexusMutex *mutex, NexusDuration duration);
/*
Summary: wakes at least one thread waiting on cond.
*/
extern NEXUS_NODISCARD NexusError *nexus_threads_cond_signal(NexusCond *cond);
/*
Summary: wakes every thread waiting on cond.
*/
extern NEXUS_NODISCARD NexusError *nexus_threads_cond_broadcast(NexusCond *cond);
/*
Summary: frees cond.
*/
extern void nexus_threads_cond_destroy(NexusCond *cond);

/*
Summary: allocates and initializes a counting semaphore starting at initial_count.
*/
extern NEXUS_NODISCARD NexusError *nexus_threads_semaphore_create(NexusSemaphore **out_semaphore, uint32 initial_count);
/*
Summary: decrements sem's counter, blocking while it's zero.
*/
extern NEXUS_NODISCARD NexusError *nexus_threads_semaphore_wait(NexusSemaphore *sem);
/*
Summary: increments sem's counter, unblocking one waiter if any.
*/
extern NEXUS_NODISCARD NexusError *nexus_threads_semaphore_post(NexusSemaphore *sem);
/*
Summary: frees sem.
*/
extern void nexus_threads_semaphore_destroy(NexusSemaphore *sem);

/*
Summary: allocates and initializes a waitgroup at counter 0.
*/
extern NEXUS_NODISCARD NexusError *nexus_threads_waitgroup_create(NexusThreadsWaitGroup **out_waitgroup);
/*
Summary: adds delta (may be negative) to waitgroup's counter -- unblocks every waiter once it reaches zero.
Errors: NEXUS_PROGRAM_ASSERT (NEXUS_DEVELOPER_MODE only) if the counter would drop below zero.
*/
extern NEXUS_NODISCARD NexusError *nexus_threads_waitgroup_add(NexusThreadsWaitGroup *waitgroup, int32 delta);
/*
Summary: nexus_threads_waitgroup_add(waitgroup, -1).
*/
extern NEXUS_NODISCARD NexusError *nexus_threads_waitgroup_done(NexusThreadsWaitGroup *waitgroup);
/*
Summary: blocks until waitgroup's counter reaches zero (returns immediately if it's already there).
*/
extern NEXUS_NODISCARD NexusError *nexus_threads_waitgroup_wait(NexusThreadsWaitGroup *waitgroup);
/*
Summary: TRUE if waitgroup's counter is currently zero. Never blocks; the result is only a snapshot.
*/
extern boolean nexus_threads_waitgroup_try_wait(NexusThreadsWaitGroup *waitgroup);
/*
Summary: frees waitgroup. Nothing may still be waiting on it.
*/
extern void nexus_threads_waitgroup_destroy(NexusThreadsWaitGroup *waitgroup);

/*
Summary: a bounded, fixed-capacity FIFO for passing fixed-size elements between threads -- a Go-style buffered channel.
Description: element_size is fixed at creation and every element is copied by value in and out
  of an internal ring buffer -- there is no per-message allocation, so there's no per-message
  ownership question either (unlike a channel of raw pointers, where every message needs someone
  to eventually free whatever it points to, exactly once). Passing a pointer through is still
  possible and exactly as cheap: create with element_size = sizeof(void*), send &your_pointer --
  this is a superset of a pointer-only channel, not a different, more restrictive thing.
  Capacity must be at least 1 -- an unbuffered/rendezvous channel (capacity 0, a direct
  sender-meets-receiver handoff) needs genuinely different internal logic and isn't implemented
  yet.
*/
typedef struct NexusChannel NexusChannel;

/*
Summary: allocates a channel holding up to capacity elements of element_size bytes each.
*/
extern NEXUS_NODISCARD NexusError *nexus_threads_channel_create(NexusChannel **out_channel, uint_large element_size, uint_large capacity);
/*
Summary: like nexus_threads_channel_create, but the channel's internal mutex is recursive.
Description: lets the same thread safely call nexus_threads_channel_send/try_send/receive/try_receive
  on this channel again from inside a callback triggered by its own first call (e.g. a diagnostic
  hook that itself logs through the same channel) instead of self-deadlocking on a second,
  same-thread lock attempt. Plain nexus_threads_channel_create is the right default -- prefer it
  unless a caller specifically needs same-thread reentrancy; the recursive path costs a little
  more per lock/unlock and turns an accidental double-lock elsewhere into silent success instead
  of a loud deadlock.
*/
extern NEXUS_NODISCARD NexusError *nexus_threads_channel_create_recursive(NexusChannel **out_channel, uint_large element_size, uint_large capacity);
/*
Summary: copies element_size bytes from element into the channel, blocking while it's full.
Errors: a real NexusError if the channel has been closed (whether or not it was already full) -- sending on a closed channel never succeeds.
*/
extern NEXUS_NODISCARD NexusError *nexus_threads_channel_send(NexusChannel *channel, const void *element);
/*
Summary: like nexus_threads_channel_send, but returns FALSE immediately instead of blocking (full, or closed) rather than sending.
*/
extern boolean nexus_threads_channel_try_send(NexusChannel *channel, const void *element);
/*
Summary: copies the oldest buffered element_size bytes into out_element, blocking while the channel is empty and not yet closed.
Description: closed does not mean "nothing more to receive" by itself -- a closed channel still
  drains whatever was already buffered before out_closed is finally reported TRUE. Every
  already-sent element is always delivered; closing only ever stops further sends.
Params: out_closed  set FALSE whenever an element was actually delivered; TRUE only once the
                       channel is both closed and empty (out_element is untouched in that case)
*/
extern NEXUS_NODISCARD NexusError *nexus_threads_channel_receive(NexusChannel *channel, void *out_element, boolean *out_closed);
/*
Summary: like nexus_threads_channel_receive, but returns FALSE immediately instead of blocking (empty and not yet closed) rather than receiving.
Description: unlike the blocking form, does not distinguish "empty, try again later" from
  "closed and drained" -- both return FALSE. Use nexus_threads_channel_receive when that
  distinction matters.
*/
extern boolean nexus_threads_channel_try_receive(NexusChannel *channel, void *out_element);
/*
Summary: marks channel closed -- every blocked/future send fails, every blocked/future receive still drains whatever's buffered first. Safe to call from any thread, exactly once semantically (a second close is a no-op).
*/
extern void nexus_threads_channel_close(NexusChannel *channel);
/*
Summary: frees channel. Nothing may still be sending to or receiving from it.
*/
extern void nexus_threads_channel_destroy(NexusChannel *channel);

/*
Summary: concrete, allocation-free atomic values -- may be embedded directly in a struct and zero-initialized with ordinary C aggregate initialization ({0}).
Description: every operation is sequentially consistent. Once a value is reachable from more
  than one thread, it must only ever be touched through this API, never its .storage field
  directly.
*/
#if defined(__GNUC__) || defined(__clang__)
#  define NEXUS_THREADS_ATOMIC_ALIGN(bytes) __attribute__((aligned(bytes)))
#else
#  error "NexusAtomic* needs a compiler that supports __atomic builtins (GCC/Clang)"
#endif

typedef struct NEXUS_THREADS_ATOMIC_ALIGN(4) NexusAtomicInt32 {
  volatile int32 storage;
} NexusAtomicInt32;
typedef struct NEXUS_THREADS_ATOMIC_ALIGN(4) NexusAtomicUint32 {
  volatile uint32 storage;
} NexusAtomicUint32;
typedef struct NEXUS_THREADS_ATOMIC_ALIGN(8) NexusAtomicInt64 {
  volatile int64 storage;
} NexusAtomicInt64;
typedef struct NEXUS_THREADS_ATOMIC_ALIGN(8) NexusAtomicUint64 {
  volatile uint64 storage;
} NexusAtomicUint64;
/*
Summary: uses a 32-bit word rather than boolean's byte-sized representation -- 32-bit atomics are universally well-supported; byte-sized ones are not.
*/
typedef struct NEXUS_THREADS_ATOMIC_ALIGN(4) NexusAtomicBoolean {
  volatile uint32 storage;
} NexusAtomicBoolean;
typedef struct NexusAtomicPointer {
  void *volatile storage;
} NexusAtomicPointer;

#undef NEXUS_THREADS_ATOMIC_ALIGN

#if NEXUS_ARCHITECTURE_BITS == 64
typedef NexusAtomicInt64  NexusAtomicIntLarge;
typedef NexusAtomicUint64 NexusAtomicUintLarge;
#else
typedef NexusAtomicInt32  NexusAtomicIntLarge;
typedef NexusAtomicUint32 NexusAtomicUintLarge;
#endif

static NEXUS_MAYBE_UNUSED int32 nexus_threads_atomic_int32_load(NexusAtomicInt32 *atomic) {
  return __atomic_load_n(&atomic->storage, __ATOMIC_SEQ_CST);
}
static NEXUS_MAYBE_UNUSED void nexus_threads_atomic_int32_store(NexusAtomicInt32 *atomic, int32 value) {
  __atomic_store_n(&atomic->storage, value, __ATOMIC_SEQ_CST);
}
static NEXUS_MAYBE_UNUSED int32 nexus_threads_atomic_int32_swap(NexusAtomicInt32 *atomic, int32 value) {
  return __atomic_exchange_n(&atomic->storage, value, __ATOMIC_SEQ_CST);
}
static NEXUS_MAYBE_UNUSED boolean nexus_threads_atomic_int32_compare_exchange(NexusAtomicInt32 *atomic, int32 old_value, int32 new_value) {
  return __atomic_compare_exchange_n(&atomic->storage, &old_value, new_value, FALSE, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST) ? TRUE : FALSE;
}
static NEXUS_MAYBE_UNUSED int32 nexus_threads_atomic_int32_add(NexusAtomicInt32 *atomic, int32 delta) {
  return __atomic_add_fetch(&atomic->storage, delta, __ATOMIC_SEQ_CST);
}

static NEXUS_MAYBE_UNUSED uint32 nexus_threads_atomic_uint32_load(NexusAtomicUint32 *atomic) {
  return __atomic_load_n(&atomic->storage, __ATOMIC_SEQ_CST);
}
static NEXUS_MAYBE_UNUSED void nexus_threads_atomic_uint32_store(NexusAtomicUint32 *atomic, uint32 value) {
  __atomic_store_n(&atomic->storage, value, __ATOMIC_SEQ_CST);
}
static NEXUS_MAYBE_UNUSED uint32 nexus_threads_atomic_uint32_swap(NexusAtomicUint32 *atomic, uint32 value) {
  return __atomic_exchange_n(&atomic->storage, value, __ATOMIC_SEQ_CST);
}
static NEXUS_MAYBE_UNUSED boolean nexus_threads_atomic_uint32_compare_exchange(NexusAtomicUint32 *atomic, uint32 old_value, uint32 new_value) {
  return __atomic_compare_exchange_n(&atomic->storage, &old_value, new_value, FALSE, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST) ? TRUE : FALSE;
}
static NEXUS_MAYBE_UNUSED uint32 nexus_threads_atomic_uint32_add(NexusAtomicUint32 *atomic, uint32 delta) {
  return __atomic_add_fetch(&atomic->storage, delta, __ATOMIC_SEQ_CST);
}

static NEXUS_MAYBE_UNUSED int64 nexus_threads_atomic_int64_load(NexusAtomicInt64 *atomic) {
  return __atomic_load_n(&atomic->storage, __ATOMIC_SEQ_CST);
}
static NEXUS_MAYBE_UNUSED void nexus_threads_atomic_int64_store(NexusAtomicInt64 *atomic, int64 value) {
  __atomic_store_n(&atomic->storage, value, __ATOMIC_SEQ_CST);
}
static NEXUS_MAYBE_UNUSED int64 nexus_threads_atomic_int64_swap(NexusAtomicInt64 *atomic, int64 value) {
  return __atomic_exchange_n(&atomic->storage, value, __ATOMIC_SEQ_CST);
}
static NEXUS_MAYBE_UNUSED boolean nexus_threads_atomic_int64_compare_exchange(NexusAtomicInt64 *atomic, int64 old_value, int64 new_value) {
  return __atomic_compare_exchange_n(&atomic->storage, &old_value, new_value, FALSE, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST) ? TRUE : FALSE;
}
static NEXUS_MAYBE_UNUSED int64 nexus_threads_atomic_int64_add(NexusAtomicInt64 *atomic, int64 delta) {
  return __atomic_add_fetch(&atomic->storage, delta, __ATOMIC_SEQ_CST);
}

static NEXUS_MAYBE_UNUSED uint64 nexus_threads_atomic_uint64_load(NexusAtomicUint64 *atomic) {
  return __atomic_load_n(&atomic->storage, __ATOMIC_SEQ_CST);
}
static NEXUS_MAYBE_UNUSED void nexus_threads_atomic_uint64_store(NexusAtomicUint64 *atomic, uint64 value) {
  __atomic_store_n(&atomic->storage, value, __ATOMIC_SEQ_CST);
}
static NEXUS_MAYBE_UNUSED uint64 nexus_threads_atomic_uint64_swap(NexusAtomicUint64 *atomic, uint64 value) {
  return __atomic_exchange_n(&atomic->storage, value, __ATOMIC_SEQ_CST);
}
static NEXUS_MAYBE_UNUSED boolean nexus_threads_atomic_uint64_compare_exchange(NexusAtomicUint64 *atomic, uint64 old_value, uint64 new_value) {
  return __atomic_compare_exchange_n(&atomic->storage, &old_value, new_value, FALSE, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST) ? TRUE : FALSE;
}
static NEXUS_MAYBE_UNUSED uint64 nexus_threads_atomic_uint64_add(NexusAtomicUint64 *atomic, uint64 delta) {
  return __atomic_add_fetch(&atomic->storage, delta, __ATOMIC_SEQ_CST);
}

static NEXUS_MAYBE_UNUSED boolean nexus_threads_atomic_boolean_load(NexusAtomicBoolean *atomic) {
  return __atomic_load_n(&atomic->storage, __ATOMIC_SEQ_CST) ? TRUE : FALSE;
}
static NEXUS_MAYBE_UNUSED void nexus_threads_atomic_boolean_store(NexusAtomicBoolean *atomic, boolean value) {
  __atomic_store_n(&atomic->storage, (uint32)(value ? 1 : 0), __ATOMIC_SEQ_CST);
}
static NEXUS_MAYBE_UNUSED boolean nexus_threads_atomic_boolean_swap(NexusAtomicBoolean *atomic, boolean value) {
  return __atomic_exchange_n(&atomic->storage, (uint32)(value ? 1 : 0), __ATOMIC_SEQ_CST) ? TRUE : FALSE;
}
static NEXUS_MAYBE_UNUSED boolean nexus_threads_atomic_boolean_compare_exchange(NexusAtomicBoolean *atomic, boolean old_value, boolean new_value) {
  uint32 old_word = old_value ? 1u : 0u;
  return __atomic_compare_exchange_n(&atomic->storage, &old_word, (uint32)(new_value ? 1 : 0), FALSE, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST) ? TRUE
                                                                                                                                          : FALSE;
}

static NEXUS_MAYBE_UNUSED void *nexus_threads_atomic_pointer_load(NexusAtomicPointer *atomic) {
  return __atomic_load_n(&atomic->storage, __ATOMIC_SEQ_CST);
}
static NEXUS_MAYBE_UNUSED void nexus_threads_atomic_pointer_store(NexusAtomicPointer *atomic, void *value) {
  __atomic_store_n(&atomic->storage, value, __ATOMIC_SEQ_CST);
}
static NEXUS_MAYBE_UNUSED void *nexus_threads_atomic_pointer_swap(NexusAtomicPointer *atomic, void *value) {
  return __atomic_exchange_n(&atomic->storage, value, __ATOMIC_SEQ_CST);
}
static NEXUS_MAYBE_UNUSED boolean nexus_threads_atomic_pointer_compare_exchange(NexusAtomicPointer *atomic, void *old_value, void *new_value) {
  return __atomic_compare_exchange_n(&atomic->storage, &old_value, new_value, FALSE, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST) ? TRUE : FALSE;
}

/* ---------------------------------------------------------------------------- */
/* NETWORK                                                                      */
/* ---------------------------------------------------------------------------- */

/*
Summary: Internet address families; NNF_ANY is only a resolver filter.
*/
typedef enum NexusNetworkFamily {
  NNF_ANY,
  NNF_IPV4,
  NNF_IPV6
} NexusNetworkFamily;

/*
Summary: transport selected at socket creation and address resolution.
*/
typedef enum NexusNetworkProtocol {
  NNP_TCP,
  NNP_UDP
} NexusNetworkProtocol;

/*
Summary: caller-owned Internet endpoint independent of native socket layouts.
Description:
  bytes holds 4 IPv4 or 16 IPv6 bytes in network order; unused IPv4 bytes
  are zero. port and scope_id are host-order integers. scope_id is zero
  for IPv4; IPv6 interface indices are explicit, never parsed from a zone
  suffix. No native headers or byte-order conversions are needed by clients.
Ownership: value type; no resources or borrowed storage.
*/
typedef struct NexusNetworkAddress {
  NexusNetworkFamily family;
  byte               bytes[16];
  uint16             port;
  uint32             scope_id;
} NexusNetworkAddress;

/*
Summary: room for any numeric IPv4/IPv6 host text, including its terminator.
*/
#define NEXUS_NETWORK_ADDRESS_TEXT_CAPACITY 46U

/*
Summary: synthetic network failures; real syscall errors retain OS errno codes.
*/
#define NEXUS_ERROR_NETWORK_FACILITY NEXUS_PROGRAM_ERROR_FACILITY_IDENTIFIER_CREATE('N', 'E', 'T', 'W')
typedef enum NexusNetworkErrorCategory {
  NNEC_ADDRESS,
  NNEC_RESOLUTION,
  NNEC_CAPACITY,
  NNEC_COUNT
} NexusNetworkErrorCategory;
#define NEXUS_NETWORK_ERROR_LIST(X, library, facility)                                                                                               \
  X(NEXUS_NETWORK_ERROR_INVALID_ADDRESS, NNEC_ADDRESS, 1, library, facility)                                                                         \
  X(NEXUS_NETWORK_ERROR_RESOLUTION_FAILED, NNEC_RESOLUTION, 1, library, facility)                                                                    \
  X(NEXUS_NETWORK_ERROR_RESOLUTION_TEMPORARY, NNEC_RESOLUTION, 2, library, facility)                                                                 \
  X(NEXUS_NETWORK_ERROR_NAME_NOT_FOUND, NNEC_RESOLUTION, 3, library, facility)                                                                       \
  X(NEXUS_NETWORK_ERROR_BUFFER_TOO_SMALL, NNEC_CAPACITY, 1, library, facility)
NEXUS_ERROR_FACILITY_DEFINE(NexusNetworkError, NEXUS_ERROR_LIBRARY_IDENTIFIER, NEXUS_ERROR_NETWORK_FACILITY, NEXUS_NETWORK_ERROR_LIST)

/*
Summary: parses a numeric host into an endpoint without DNS or allocation.
Params:
  host  borrowed, terminated IPv4/IPv6 literal; no brackets, port, or zone
  out_address  out; changes only on success
Ownership: borrows pointers for the call.
Errors: NULL pointers assert. INVALID_ADDRESS for malformed text or an IPv4
  address with nonzero scope_id. Linux backend; other platforms raise via
  NEXUS_PLATFORM_UNSUPPORTED_RAISE, as do all non-inline network operations.
Thread-safety: subject to Nexus error-pool synchronization on failure.
*/
extern NEXUS_NODISCARD NexusError *nexus_network_address_parse(const char *host, uint16 port, uint32 scope_id, NexusNetworkAddress *out_address);
/*
Summary: formats only the numeric host, without port, brackets, or IPv6 zone.
Params:
  address  borrowed endpoint with concrete family
  out_host  borrowed output; empty on failure
  capacity  bytes available, including terminator; must be positive
Ownership: borrows pointers for the call.
Errors: NULL pointers, invalid family, or zero capacity assert. Returns
  BUFFER_TOO_SMALL for insufficient capacity; otherwise OS errno on failure.
Thread-safety: subject to Nexus error-pool synchronization on failure.
*/
extern NEXUS_NODISCARD NexusError *nexus_network_address_format(const NexusNetworkAddress *address, char *out_host, uint_large capacity);
/*
Summary: resolves a host into caller-owned endpoints using the OS resolver.
Description:
  Blocking operation; libc may allocate. No cache, service-name lookup, or
  implicit connection attempt. NNF_ANY permits both families. A NULL host
  selects wildcard addresses when passive is TRUE, loopback otherwise.
  All results use port. Retry with larger storage after BUFFER_TOO_SMALL;
  separate calls may observe different DNS results. No addresses are written
  unless the entire result fits. Result order follows the OS resolver.
Params:
  host  borrowed terminated hostname/literal, or NULL
  out_addresses  borrowed output; may be NULL only for zero capacity
  out_count  out; required count on success/BUFFER_TOO_SMALL, zero otherwise
Ownership: returned addresses are values; retains no pointers.
Errors: invalid family/protocol, NULL out_count, or inconsistent output
  pointer/capacity assert. Returns BUFFER_TOO_SMALL, NAME_NOT_FOUND,
  RESOLUTION_TEMPORARY, RESOLUTION_FAILED, or OS errno for EAI_SYSTEM.
Thread-safety: caller synchronizes Nexus's shared error/memory machinery.
Complexity: O(number of results) space in libc; no Nexus allocation.
*/
extern NEXUS_NODISCARD NexusError *nexus_network_addresses_resolve(const char *host, uint16 port, NexusNetworkFamily family,
                                                                   NexusNetworkProtocol protocol, boolean passive, NexusNetworkAddress *out_addresses,
                                                                   uint32 capacity, uint32 *out_count);

/*
Summary: owned socket, closed with nexus_network_socket_close.
Description:
  Linux backend. No hidden buffering, framing, event loop, thread, or retries
  beyond interrupted I/O. All socket calls borrow their handle; no operation
  retains caller buffers. I/O and waits allocate no Nexus memory on success.
  Errors use Nexus's shared pool. Socket creation/accept and wait-set lifecycle
  also use the shared memory tracker. These facilities require caller
  synchronization as documented by the error and memory subsystems.
Ownership: unique owner; successful create/accept returns a new handle.
Thread-safety: synchronize lifecycle, connect, mode/options changes, and
  waits against close. Independent send and receive need no socket-local lock,
  but shared Nexus diagnostics still require external synchronization.
See: adr/0010-low-level-internet-networking
*/
typedef struct NexusNetworkSocket NexusNetworkSocket;

/*
Summary: ordinary operation outcomes, independent of genuine failures.
Values:
  NNO_COMPLETE  success, including zero-byte UDP messages and short TCP I/O
  NNO_WOULD_BLOCK  retry after readiness; no data/connection transferred
  NNO_IN_PROGRESS  connect started or remains pending
  NNO_END_OF_STREAM  TCP peer has shut down its send side
  NNO_TRUNCATED  one UDP datagram consumed; reported size exceeds capacity
*/
typedef enum NexusNetworkOutcome {
  NNO_COMPLETE,
  NNO_WOULD_BLOCK,
  NNO_IN_PROGRESS,
  NNO_END_OF_STREAM,
  NNO_TRUNCATED
} NexusNetworkOutcome;

/*
Summary: creates a close-on-exec TCP/UDP socket in the requested mode.
Description: IPv6 sockets initially use IPV6_V6ONLY; change explicitly before
  bind if dual-stack operation is wanted. No automatic bind or connect.
Params: out_socket  out; NULL on failure
Ownership: caller closes the returned handle.
Errors: invalid family/protocol or NULL output asserts; OS errno on failure.
  Allocation follows Nexus memory's abort-on-exhaustion contract.
Thread-safety: caller synchronizes shared Nexus memory/error machinery.
*/
extern NEXUS_NODISCARD NexusError *nexus_network_socket_create(NexusNetworkFamily family, NexusNetworkProtocol protocol, boolean nonblocking,
                                                               NexusNetworkSocket **out_socket);
/*
Summary: closes once and consumes the handle even when close reports failure.
Description: never retries close after EINTR; Linux has already released the
  descriptor. Does not change SIGPIPE policy or wait for peer acknowledgement.
Params: socket  borrowed handle slot; set to NULL on every return
Ownership: consumes *socket; concurrent users must already be quiescent.
Errors: NULL slot/handle assert. OS errno on close failure.
Thread-safety: exclusive access to handle and shared Nexus memory/errors.
*/
extern NEXUS_NODISCARD NexusError *nexus_network_socket_close(NexusNetworkSocket **socket);
/*
Summary: changes blocking mode, preserving unrelated native descriptor flags.
Ownership: borrows socket for the call.
Errors: NULL socket asserts; OS errno on failure.
Thread-safety: exclusive mode/lifecycle access; shared error synchronization.
*/
extern NEXUS_NODISCARD NexusError *nexus_network_socket_nonblocking_set(NexusNetworkSocket *socket, boolean nonblocking);
/*
Summary: binds a socket to an explicit local endpoint; port zero is ephemeral.
Ownership: borrows socket and address for the call.
Errors: NULL pointers or family mismatch assert; OS errno on failure.
Thread-safety: exclusive configuration/lifecycle access; shared errors.
*/
extern NEXUS_NODISCARD NexusError *nexus_network_socket_bind(NexusNetworkSocket *socket, const NexusNetworkAddress *address);
/*
Summary: enables TCP listening with the caller's positive backlog.
Ownership: borrows socket for the call.
Errors: NULL socket, non-TCP protocol, or nonpositive backlog assert;
  OS errno on failure.
Thread-safety: exclusive configuration/lifecycle access; shared errors.
*/
extern NEXUS_NODISCARD NexusError *nexus_network_socket_listen(NexusNetworkSocket *socket, int32 backlog);
/*
Summary: accepts one TCP peer into a close-on-exec socket of explicit mode.
Params:
  out_socket  out; NULL unless an acceptance completes
  out_peer  optional borrowed output; changes only on completed acceptance
  out_outcome  out; COMPLETE or WOULD_BLOCK on success, COMPLETE on error
Ownership: caller closes the new socket; all pointer inputs are borrowed.
Errors: NULL listener/out_socket/out_outcome or non-TCP listener assert;
  OS errno on failure. Allocation follows Nexus memory's exhaustion contract.
Thread-safety: synchronize lifecycle and shared memory/errors.
*/
extern NEXUS_NODISCARD NexusError *nexus_network_socket_accept(NexusNetworkSocket *listener, boolean nonblocking, NexusNetworkSocket **out_socket,
                                                               NexusNetworkAddress *out_peer, NexusNetworkOutcome *out_outcome);
/*
Summary: initiates TCP connection or sets a UDP socket's default peer.
Description:
  COMPLETE means connected. IN_PROGRESS requires connect_finish after a
  readable/writable/error/hangup notification. Do not call connect again
  while pending. A failed TCP connection must be closed, not reused.
  An interrupted TCP connect remains IN_PROGRESS, even on a blocking socket.
Params: out_outcome  out; COMPLETE or IN_PROGRESS; COMPLETE on error
Ownership: borrows socket, address, and output for the call.
Errors: NULL pointers, family mismatch, or already pending connect assert;
  OS errno on failure.
Thread-safety: exclusive connect/lifecycle access; shared errors.
*/
extern NEXUS_NODISCARD NexusError *nexus_network_socket_connect(NexusNetworkSocket *socket, const NexusNetworkAddress *address,
                                                                NexusNetworkOutcome *out_outcome);
/*
Summary: checks pending connection readiness and its SO_ERROR result.
Description: never blocks. Early calls return IN_PROGRESS. A ready socket's
  SO_ERROR is consumed once; COMPLETE means the connection succeeded.
Params: out_outcome  out; COMPLETE or IN_PROGRESS; COMPLETE on error
Ownership: borrows socket and output for the call.
Errors: NULL pointers or no pending connect assert; OS errno on failure.
Thread-safety: exclusive connect/lifecycle access; shared errors.
*/
extern NEXUS_NODISCARD NexusError *nexus_network_socket_connect_finish(NexusNetworkSocket *socket, NexusNetworkOutcome *out_outcome);
/*
Summary: queries the current local endpoint, including assigned ephemeral port.
Params: out_address  borrowed output; changes only on success
Ownership: borrows socket and output for the call.
Errors: NULL pointers assert; OS errno on failure.
Thread-safety: synchronize lifecycle and shared errors.
*/
extern NEXUS_NODISCARD NexusError *nexus_network_socket_local_address_get(NexusNetworkSocket *socket, NexusNetworkAddress *out_address);
/*
Summary: queries a connected socket's remote endpoint.
Params: out_address  borrowed output; changes only on success
Ownership: borrows socket and output for the call.
Errors: NULL pointers assert; OS errno on failure, including no connected peer.
Thread-safety: synchronize connect/lifecycle and shared errors.
*/
extern NEXUS_NODISCARD NexusError *nexus_network_socket_peer_address_get(NexusNetworkSocket *socket, NexusNetworkAddress *out_address);
/*
Summary: sends once to a connected TCP/UDP socket, suppressing SIGPIPE locally.
Description: TCP may send fewer bytes than requested. No send-all loop or
  allocation. A zero-size request completes; UDP transmits an empty datagram.
Params:
  bytes  borrowed input; NULL allowed only for zero size
  out_size  borrowed output; transferred bytes, zero on error/WOULD_BLOCK
  out_outcome  borrowed output; COMPLETE or WOULD_BLOCK, COMPLETE on error
Ownership: borrows socket and all pointers for the call.
Errors: NULL socket/outputs, invalid input pointer, or size beyond the native
  signed I/O limit assert. OS errno on failure.
Thread-safety: synchronize lifecycle and shared errors.
*/
extern NEXUS_NODISCARD NexusError *nexus_network_socket_send(NexusNetworkSocket *socket, const void *bytes, uint_large size, uint_large *out_size,
                                                             NexusNetworkOutcome *out_outcome);
/*
Summary: receives once from TCP or UDP, discarding UDP sender information.
Description:
  TCP reports short reads or END_OF_STREAM. UDP consumes exactly one message,
  reports empty datagrams as COMPLETE and oversized ones as TRUNCATED.
  out_size is the full UDP message size, even if only capacity bytes fit.
Params:
  bytes  borrowed output; non-NULL with positive capacity
  out_size  borrowed output; received/full message size, zero on error/would-block
  out_outcome  borrowed output; COMPLETE/WOULD_BLOCK/END_OF_STREAM/TRUNCATED;
    COMPLETE on error
Ownership: borrows socket and all pointers for the call.
Errors: NULL pointers, zero capacity, or capacity beyond the native signed I/O
  limit assert. OS errno on failure; discard output bytes after failure.
Thread-safety: synchronize lifecycle and shared errors.
*/
extern NEXUS_NODISCARD NexusError *nexus_network_socket_receive(NexusNetworkSocket *socket, void *bytes, uint_large capacity, uint_large *out_size,
                                                                NexusNetworkOutcome *out_outcome);
/*
Summary: sends one UDP datagram to an explicit destination, including empty ones.
Params:
  bytes  borrowed input; NULL allowed only for zero size
  out_size  borrowed output; sent bytes, zero on error/WOULD_BLOCK
  out_outcome  borrowed output; COMPLETE/WOULD_BLOCK, COMPLETE on error
Ownership: borrows socket, address, and all buffers for the call.
Errors: NULL socket/address/outputs, non-UDP socket, family mismatch, invalid
  input pointer, or size beyond the native signed I/O limit assert.
  OS errno on failure; datagrams are never split into separate sends.
Thread-safety: synchronize lifecycle and shared errors.
*/
extern NEXUS_NODISCARD NexusError *nexus_network_socket_send_to(NexusNetworkSocket *socket, const NexusNetworkAddress *address, const void *bytes,
                                                                uint_large size, uint_large *out_size, NexusNetworkOutcome *out_outcome);
/*
Summary: consumes one UDP datagram, returning its full size and optional sender.
Description: same truncation/empty-message semantics as socket_receive.
Params:
  bytes  borrowed output; non-NULL with positive capacity
  out_sender  optional borrowed output; changes only after a received message
  out_size  borrowed output; full datagram size, zero on error/WOULD_BLOCK
  out_outcome  borrowed output; COMPLETE/WOULD_BLOCK/TRUNCATED; COMPLETE on error
Ownership: borrows socket and all pointers for the call.
Errors: NULL socket/bytes/size/outcome, non-UDP socket, zero capacity, or
  capacity beyond the native signed I/O limit assert. OS errno on failure;
  discard buffer bytes after failure.
Thread-safety: synchronize lifecycle and shared errors.
*/
extern NEXUS_NODISCARD NexusError *nexus_network_socket_receive_from(NexusNetworkSocket *socket, void *bytes, uint_large capacity,
                                                                     NexusNetworkAddress *out_sender, uint_large *out_size,
                                                                     NexusNetworkOutcome *out_outcome);
/*
Summary: socket directions that an explicit shutdown operation disables.
*/
typedef enum NexusNetworkShutdown {
  NNS_RECEIVE,
  NNS_SEND,
  NNS_BOTH
} NexusNetworkShutdown;
/*
Summary: shuts down selected directions without closing the owned socket.
Ownership: borrows socket for the call.
Errors: NULL socket or invalid direction assert; OS errno on failure.
Thread-safety: synchronize shutdown/lifecycle and shared errors.
*/
extern NEXUS_NODISCARD NexusError *nexus_network_socket_shutdown(NexusNetworkSocket *socket, NexusNetworkShutdown direction);

/*
Summary: integer socket options, independent of native constants.
Values:
  NNSO_*_BUFFER_SIZE  positive byte counts; Linux getters include kernel overhead
  NNSO_TCP_NO_DELAY  TCP only
  NNSO_IPV6_ONLY  IPv6 only; set before binding
  Remaining options are booleans, set with exactly 0 or 1.
*/
typedef enum NexusNetworkSocketOption {
  NNSO_REUSE_ADDRESS,
  NNSO_REUSE_PORT,
  NNSO_KEEP_ALIVE,
  NNSO_TCP_NO_DELAY,
  NNSO_IPV6_ONLY,
  NNSO_BROADCAST,
  NNSO_RECEIVE_BUFFER_SIZE,
  NNSO_SEND_BUFFER_SIZE
} NexusNetworkSocketOption;
/*
Summary: sets a boolean or byte-count socket option before its next use.
Ownership: borrows socket for the call.
Errors: NULL socket, invalid option/value, TCP-only option on UDP, or IPv6-only
  option on IPv4 assert. OS errno on failure.
Thread-safety: exclusive configuration/lifecycle access; shared errors.
*/
extern NEXUS_NODISCARD NexusError *nexus_network_socket_option_set(NexusNetworkSocket *socket, NexusNetworkSocketOption option, int32 value);
/*
Summary: reads a current socket option as a boolean or byte count.
Params: out_value  borrowed output; changes only on success
Ownership: borrows socket and output for the call.
Errors: NULL pointers, invalid option, TCP-only option on UDP, or IPv6-only
  option on IPv4 assert. OS errno on failure.
Thread-safety: synchronize configuration/lifecycle and shared errors.
*/
extern NEXUS_NODISCARD NexusError *nexus_network_socket_option_get(NexusNetworkSocket *socket, NexusNetworkSocketOption option, int32 *out_value);

/*
Summary: reusable native readiness storage allocated once for a fixed capacity.
Description: readiness is a hint, not a reservation; use nonblocking sockets
  after waiting. Each wait scans its supplied entries; no event loop or retained
  socket registrations. timeout_ms uses a monotonic deadline across EINTR.
Ownership: caller destroys the set; it never owns sockets or entry storage.
Thread-safety: one waiter per set; callers synchronize shared memory/errors.
*/
typedef struct NexusNetworkWaitSet NexusNetworkWaitSet;
/*
Summary: socket readiness flags; ERROR/HANGUP/INVALID are output-only.
Description: HANGUP can coexist with READABLE while TCP data remains buffered.
*/
typedef enum NexusNetworkEvent {
  NNE_READABLE = 1,
  NNE_WRITABLE = 2,
  NNE_ERROR    = 4,
  NNE_HANGUP   = 8,
  NNE_INVALID  = 16
} NexusNetworkEvent;
/*
Summary: one borrowed socket and its requested/returned readiness bitmasks.
Ownership: caller owns the entry; socket must stay live throughout wait.
*/
typedef struct NexusNetworkWaitEntry {
  NexusNetworkSocket *socket;
  uint32              events;
  uint32              ready_events;
} NexusNetworkWaitEntry;
/*
Summary: allocates readiness scratch space for capacity entries.
Returns: newly owned wait set.
Ownership: release with nexus_network_wait_set_destroy.
Errors: zero capacity or allocation-size overflow assert. Allocation follows
  Nexus memory's abort-on-exhaustion contract.
Thread-safety: caller synchronizes shared memory machinery.
Complexity: O(capacity) storage, allocated only here.
*/
extern NexusNetworkWaitSet *nexus_network_wait_set_create(uint32 capacity);
/*
Summary: releases readiness storage and nulls the owner's handle slot.
Ownership: consumes *set; borrows slot.
Errors: NULL slot/handle assert.
Thread-safety: exclusive access, including no active wait; shared memory.
*/
extern void nexus_network_wait_set_destroy(NexusNetworkWaitSet **set);
/*
Summary: waits for readiness on at most the set's configured capacity.
Description:
  timeout_ms is -1 for unlimited wait, 0 for polling, positive for a relative
  timeout. Signals do not restart that timeout. Zero entries are allowed.
  All ready_events and out_ready_count are cleared before waiting, including
  on failure. Error/hangup/invalid are returned even if not requested.
Params:
  entries  borrowed mutable array; NULL only when count is zero
  out_ready_count  borrowed output; number of entries with any ready event
Ownership: borrows set, entries, their sockets, and output for the call.
Errors: NULL set/output, invalid entries, excessive count, invalid input
  event bits, or timeout below -1 assert. OS errno on poll/clock failure.
Thread-safety: exclusive set access; sockets must not be closed during wait;
  shared error synchronization on failure.
Complexity: O(count) time per scan, O(1) extra space; no Nexus allocation.
*/
extern NEXUS_NODISCARD NexusError *nexus_network_wait(NexusNetworkWaitSet *set, NexusNetworkWaitEntry *entries, uint32 count, int32 timeout_ms,
                                                      uint32 *out_ready_count);

/* ---------------------------------------------------------------------------- */
/* VERSION                                                                      */
/* ---------------------------------------------------------------------------- */

#ifndef NEXUS_VERSION_PACK
#  define NEXUS_VERSION_PACK(variant, major, minor, patch)                                                                                           \
    ((NexusSemanticVersion)(((NexusSemanticVersion)(variant) & 0xFFFFu) << 48) | (((NexusSemanticVersion)(major) & 0xFFFFu) << 32) |                 \
     (((NexusSemanticVersion)(minor) & 0xFFFFu) << 16) | ((NexusSemanticVersion)(patch) & 0xFFFFu))
#endif

/*
NexusSemanticVersion stores packed variant/major/minor/patch fields in a uint64.
*/
typedef uint64 NexusSemanticVersion;

/*
nexus_version_pack packs variant/major/minor/patch into a semantic version.
*/
static NEXUS_MAYBE_UNUSED NexusSemanticVersion nexus_version_pack(uint16 variant, uint16 major, uint16 minor, uint16 patch) {
  return NEXUS_VERSION_PACK(variant, major, minor, patch);
}

/*
nexus_version_unpack unpacks a semantic version into its component fields.
*/
static NEXUS_MAYBE_UNUSED void nexus_version_unpack(NexusSemanticVersion version, uint16 *out_variant, uint16 *out_major, uint16 *out_minor,
                                                    uint16 *out_patch) {
  NEXUS_PROGRAM_PRECONDITION(out_variant != NULL, "");
  NEXUS_PROGRAM_PRECONDITION(out_major != NULL, "");
  NEXUS_PROGRAM_PRECONDITION(out_minor != NULL, "");
  NEXUS_PROGRAM_PRECONDITION(out_patch != NULL, "");

  *out_variant = (uint16)((version >> 48) & 0xFFFFu);
  *out_major   = (uint16)((version >> 32) & 0xFFFFu);
  *out_minor   = (uint16)((version >> 16) & 0xFFFFu);
  *out_patch   = (uint16)(version & 0xFFFFu);
}

/*
nexus_version_format writes a semantic version as "major.minor.patch (variant=variant)".
*/
static NEXUS_MAYBE_UNUSED void nexus_version_format(NexusSemanticVersion version, char *out_buffer, uint_large out_buffer_size) {
  uint16 variant;
  uint16 major;
  uint16 minor;
  uint16 patch;

  nexus_version_unpack(version, &variant, &major, &minor, &patch);
  nexus_strings_string_format_with_truncation(out_buffer, out_buffer_size, "%u.%u.%u (variant=%u)", major, minor, patch, variant);
}
