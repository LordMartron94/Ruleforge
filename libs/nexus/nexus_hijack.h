#pragma once

/*
Summary: opt-in alternative to nexus.h's default poisoning of malloc/calloc/realloc/free/memset/
  memcpy -- include this BEFORE <nexus.h> in a translation unit to redirect those identifiers to
  Nexus's own tracked implementations instead of refusing to compile on any use of them. Modeled
  on Eskil Steenberg's Forge (forge.h's FORGE_MEMORY_DEBUG malloc/realloc/free redirect -- see
  nexus.h's own credit to Forge at its top).
Description: does not replace poisoning as this codebase's default. Poisoning exists to catch an
  ACCIDENTAL raw allocation call in code that should always go through nexus_memory_*, and that
  guarantee stays intact everywhere this header isn't explicitly included first -- nothing about
  including this file anywhere changes what poisoning does for every other translation unit.
  Where this is genuinely useful: a translation unit that compiles third-party SOURCE as part of
  this build (a vendored .c file, e.g. something under external/, or a header-only library) and
  wants THOSE allocations tracked by Nexus's debugger too, without patching the third party's
  source. Include this, then <nexus.h>, then the third party's own header/source; every malloc/
  calloc/realloc/free/memset/memcpy token from that point on in this translation unit, including
  inside subsequently-included headers, resolves to Nexus's tracked versions instead.
  NOTE this cannot reach a third party's calls that are NOT compiled as source in this
  translation unit -- a pre-built shared or static library (e.g. libxcb, already linked rather
  than vendored) made its allocation calls when IT was compiled, with no visibility into this
  header's macros at all; redirecting malloc here has no effect on code that already exists as
  machine code in a .so/.a. For that case (freeing a buffer a pre-built third-party library
  itself allocated and owns the allocator contract for) there is no substitute for calling that
  library's own matching free function directly, isolated in its own translation unit per
  Insulation -- see e.g. libs/janus/src/j_platform_libc.c's _j_platform_libc_free.
  mmap/munmap/mprotect are deliberately NOT redirected here and stay poisoned regardless of
  whether this header is included -- their signatures and semantics (flags, protection bits,
  fd-or-anonymous, the anonymous-vs-shared distinction nexus_memory_map added) don't map onto a
  safe drop-in macro the way the allocation functions do. A caller that needs a mapping calls
  nexus_memory_map/nexus_memory_unmap/nexus_memory_protect directly, hijack or not.
*/

#ifndef _DEFAULT_SOURCE
#  define _DEFAULT_SOURCE /* NOLINT -- nexus.h normally does this first; as the header meant to be included before nexus.h, this one has to instead */
#endif

#include <stdlib.h> /* must be fully processed before the redirects below are defined, or its own
                        malloc/calloc/realloc/free declarations get mangled by them too */
#include <string.h> /* same reasoning, for memset/memcpy */

#ifndef NEXUS_MEMORY_HIJACK
#  define NEXUS_MEMORY_HIJACK /* tells nexus.h's poison block to skip malloc/calloc/realloc/free/memset/memcpy for this translation unit */
#endif

/* Each of these forwards to a nexus_memory_* macro that isn't defined yet at this point in the
   file -- that's fine, a macro body is just tokens until it's actually expanded, and every real
   call site that expands malloc/calloc/realloc/free/memset/memcpy comes after <nexus.h> (require
   it immediately after this header), by which point nexus_memory_allocate and friends are real,
   already-defined macros. */

#define malloc(size)           nexus_memory_allocate(size)
#define free(pointer)          nexus_memory_deallocate(pointer)
#define realloc(pointer, size) nexus_memory_reallocate((pointer), (size))

/* calloc has no Nexus-native equivalent by design (see nexus.h's own no-calloc-equivalent
   comment) -- _nexus_memory_hijack_calloc exists specifically to back this redirect, not as a
   general convenience; see its own doc comment in nexus.h. */
#define calloc(count, size) _nexus_memory_hijack_calloc((count), (size), __FILE__, __LINE__)

#define memset(pointer, value, size)      nexus_memory_bytes_set((pointer), (size), (value))
#define memcpy(destination, source, size) nexus_memory_bytes_copy((source), (destination), (size))
