/* The Windows headers use memcpy/memset, which nexus.h poisons, so they come first; every file that needs them does this. */
#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#  include <bcrypt.h>
#endif

#include "../nexus.h"

#if defined(NEXUS_PLATFORM_WINDOWS)
#elif defined(NEXUS_PLATFORM_LINUX) || defined(NEXUS_PLATFORM_ANDROID)
#  include <errno.h>
#  include <sys/types.h>
#  include <sys/random.h>
#elif defined(NEXUS_PLATFORM_MACOS) || defined(NEXUS_PLATFORM_IOS) || defined(NEXUS_PLATFORM_BSD)
#  include <errno.h>
#  include <unistd.h>
#endif

NexusError *nexus_entropy_system_fill(byte *out_bytes, uint_large byte_count) {
  uint_large offset;

  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_entropy_system_fill");
  NEXUS_PROGRAM_PRECONDITION(out_bytes != NULL || byte_count == 0, "entropy_system_fill: out_bytes is NULL for nonzero byte_count");

  if (byte_count == 0) {
    NEXUS_PROGRAM_CONTEXT_POP();
    return NEXUS_PROGRAM_ERROR_SUCCESS();
  }

#ifdef NEXUS_PLATFORM_WINDOWS
  while (byte_count > 0) {
    ULONG    chunk_size;
    NTSTATUS status;

    chunk_size = byte_count > (uint_large)0xffffffffUL ? 0xffffffffUL : (ULONG)byte_count;
    status     = BCryptGenRandom(NULL, (PUCHAR)out_bytes, chunk_size, BCRYPT_USE_SYSTEM_PREFERRED_RNG);

    if (status < 0) {
      NexusError *error;

      error = NEXUS_PROGRAM_ERROR_FAILURE(GLOBAL_NEXUS_ENTROPY_ERROR_SOURCE_FAILED, "BCryptGenRandom failed with status %ld", (long)status);
      NEXUS_PROGRAM_CONTEXT_POP();
      return error;
    }

    out_bytes += chunk_size;
    byte_count -= chunk_size;
  }
#elif defined(NEXUS_PLATFORM_LINUX) || defined(NEXUS_PLATFORM_ANDROID)
  offset = 0;

  while (offset < byte_count) {
    ssize_t    result;
    uint_large chunk_size;

    chunk_size = byte_count - offset;
    if (chunk_size > 1048576U) {
      chunk_size = 1048576U;
    }
    result = getrandom(out_bytes + offset, (size_t)chunk_size, 0);

    if (result < 0) {
      if (errno == EINTR) {
        continue;
      }

      {
        NexusError *error;

        error = NEXUS_PROGRAM_ERROR_FAILURE(GLOBAL_NEXUS_ENTROPY_ERROR_SOURCE_FAILED, "getrandom failed with errno %d", errno);
        NEXUS_PROGRAM_CONTEXT_POP();
        return error;
      }
    }

    if (result == 0) {
      NexusError *error;

      error = NEXUS_PROGRAM_ERROR_FAILURE(GLOBAL_NEXUS_ENTROPY_ERROR_SOURCE_FAILED, "getrandom returned zero bytes");
      NEXUS_PROGRAM_CONTEXT_POP();
      return error;
    }

    offset += (uint_large)result;
  }
#elif defined(NEXUS_PLATFORM_MACOS) || defined(NEXUS_PLATFORM_IOS) || defined(NEXUS_PLATFORM_BSD)
  offset = 0;

  while (offset < byte_count) {
    uint_large chunk_size;

    chunk_size = byte_count - offset;
    if (chunk_size > 256U) {
      chunk_size = 256U;
    }

    if (getentropy(out_bytes + offset, (size_t)chunk_size) != 0) {
      NexusError *error;

      error = NEXUS_PROGRAM_ERROR_FAILURE(GLOBAL_NEXUS_ENTROPY_ERROR_SOURCE_FAILED, "getentropy failed with errno %d", errno);
      NEXUS_PROGRAM_CONTEXT_POP();
      return error;
    }

    offset += chunk_size;
  }
#else
  {
    NexusError *error;

    error = NEXUS_PROGRAM_ERROR_FAILURE(GLOBAL_NEXUS_ENTROPY_ERROR_SOURCE_FAILED, "no system entropy implementation for this platform");
    NEXUS_PROGRAM_CONTEXT_POP();
    return error;
  }
#endif

  NEXUS_PROGRAM_CONTEXT_POP();
  return NEXUS_PROGRAM_ERROR_SUCCESS();
}
