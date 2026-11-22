/* The Windows headers use memcpy/memset, which nexus.h poisons, so they come first; every file that needs them does this. */
#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#endif

#include "../nexus.h"

/*
Directory collection by pattern uses the C library's POSIX regex where there is one. MinGW-w64 has none, so Nexus carries a small
matcher for the same extended syntax; defining NEXUS_FILESYSTEM_BUILTIN_REGEX selects it everywhere (the tests run it on Linux).
*/
#if !NEXUS_PLATFORM_POSIX || defined(NEXUS_FILESYSTEM_BUILTIN_REGEX)
#  define NEXUS_FILESYSTEM_USE_BUILTIN_REGEX 1
#endif

#if NEXUS_PLATFORM_POSIX_API

#  include <dirent.h>
#  include <errno.h>
#  include <stdio.h>
#  include <stdlib.h>
#  include <sys/stat.h>
#  include <sys/types.h>
#  include <unistd.h>
#  if NEXUS_PLATFORM_POSIX
#    include <pwd.h>
#  endif
#  if NEXUS_PLATFORM_POSIX && !defined(NEXUS_FILESYSTEM_USE_BUILTIN_REGEX)
#    include <regex.h>
#  endif

/* memfd_create lives in <sys/mman.h> alongside mmap/munmap/mprotect, which nexus.h poisons
   (Insulation -- every mapping must go through nexus_memory_map/unmap, never libc directly) --
   so that header can't be included here even though this file only needs the one, unpoisoned
   symbol. A minimal manual prototype, matching the stable glibc/Linux signature exactly, avoids
   the conflict without reaching for NEXUS_INTERNAL_MEMORY_IMPLEMENTATION (that escape hatch is
   n_memory.c's own, for mmap/munmap/mprotect themselves -- this file never touches those). */
extern int memfd_create(const char *name, unsigned int flags);
#  ifndef N_FILESYSTEM_MFD_CLOEXEC
#    define N_FILESYSTEM_MFD_CLOEXEC 1U /* matches MFD_CLOEXEC in <sys/mman.h>, redeclared for the same reason as memfd_create above */
#  endif

#  ifndef NEXUS_FILESYSTEM_COPY_CHUNK_SIZE
#    define NEXUS_FILESYSTEM_COPY_CHUNK_SIZE (65536u)
#  endif

#  ifdef __MINGW32__

/* ---------------------------------------------------------------------------- */
/* WINDOWS SEMANTICS THE C RUNTIME LACKS                                        */
/* ---------------------------------------------------------------------------- */

/* Summary: the errno a Win32 error stands for, for the operations below that call Win32 directly. */
static int _nexus_filesystem_errno_from_windows(unsigned long code) {
  switch (code) {
  case ERROR_FILE_NOT_FOUND:
  case ERROR_PATH_NOT_FOUND:
  case ERROR_INVALID_NAME:
    return ENOENT;
  case ERROR_ACCESS_DENIED:
  case ERROR_SHARING_VIOLATION:
  case ERROR_LOCK_VIOLATION:
    return EACCES;
  case ERROR_ALREADY_EXISTS:
  case ERROR_FILE_EXISTS:
    return EEXIST;
  case ERROR_NOT_SAME_DEVICE:
    return EXDEV;
  case ERROR_DIR_NOT_EMPTY:
    return ENOTEMPTY;
  default:
    return EIO;
  }
}

/*
Summary: lstat for Windows: a symbolic link or junction is reported as a link (mode S_IFLNK) instead of being followed.
Description: the C runtime's stat follows links, so a recursive walk or delete that classified entries with it would descend into the target of
  a directory link. Any reparse point that is a symbolic link or a mount point (junction) counts as a link, and a dangling one is reported too.
  Other reparse points (cloud placeholders, dedup) are ordinary files and directories to every caller.
*/
int _nexus_filesystem_lstat(const char *path, struct stat *info) {
  unsigned long    attributes;
  WIN32_FIND_DATAA found;
  HANDLE           handle;

  attributes = GetFileAttributesA(path);
  if (attributes == INVALID_FILE_ATTRIBUTES) {
    errno = _nexus_filesystem_errno_from_windows(GetLastError());
    return -1;
  }

  if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
    handle = FindFirstFileA(path, &found);
    if (handle != INVALID_HANDLE_VALUE) {
      (void)FindClose(handle);
      if (found.dwReserved0 == IO_REPARSE_TAG_SYMLINK || found.dwReserved0 == IO_REPARSE_TAG_MOUNT_POINT) {
        nexus_memory_bytes_set(info, NEXUS_SIZEOF(*info), 0);
        info->st_mode  = (unsigned short)(0120000 | 0777);
        info->st_nlink = 1;
        info->st_size  = (_off_t)found.nFileSizeLow;

        return 0;
      }
    }
  }

  return stat(path, info);
}

/* Summary: rename that replaces an existing destination, as POSIX rename does; the C runtime's rename fails with EEXIST instead. */
int _nexus_filesystem_rename_replace(const char *old_path, const char *new_path) {
  if (MoveFileExA(old_path, new_path, MOVEFILE_REPLACE_EXISTING) != 0) {
    return 0;
  }

  errno = _nexus_filesystem_errno_from_windows(GetLastError());

  return -1;
}

#  endif /* __MINGW32__ */

/* ---------------------------------------------------------------------------- */
/* PATHS                                                                        */
/* ---------------------------------------------------------------------------- */

boolean nexus_filesystem_path_has_extension(const NexusPath *path) {
  return nexus_filesystem_path_extension_get(path) != NULL;
}

const char *nexus_filesystem_path_extension_get(const NexusPath *path) {
  const char *filename;
  const char *last_separator;
  const char *last_dot;

  last_separator = nexus_strings_string_find_last(path->buffer, "/");
  filename       = last_separator != NULL ? last_separator + 1 : path->buffer;

  last_dot = nexus_strings_string_find_last(filename, ".");

  if (last_dot == NULL || last_dot[1] == '\0')
    return NULL;

  while (filename < last_dot && *filename == '.')
    ++filename;

  return filename < last_dot ? last_dot + 1 : NULL;
}

boolean nexus_filesystem_path_extension_equals(const NexusPath *path, const char *extension) {
  const char *path_extension = nexus_filesystem_path_extension_get(path);

  return path_extension != NULL && nexus_strings_string_equals(path_extension, extension);
}

boolean nexus_filesystem_path_extension_is_any_of(const NexusPath *path, const NexusDataArray *extensions) {
  const char *path_extension;
  uint_large  i;

  NEXUS_PROGRAM_PRECONDITION(extensions != NULL, "extensions must not be NULL");
  NEXUS_PROGRAM_PRECONDITION(extensions->element_size == sizeof(const char *), "extensions must contain string pointers");

  path_extension = nexus_filesystem_path_extension_get(path);

  if (path_extension == NULL)
    return FALSE;

  for (i = 0; i < extensions->capacity; i++) {
    const char *const *extension = (const char *const *)nexus_data_array_at_const(extensions, i);

    if (nexus_strings_string_equals(path_extension, *extension))
      return TRUE;
  }

  return FALSE;
}

/* ---------------------------------------------------------------------------- */
/* FILE I/O                                                                     */
/* ---------------------------------------------------------------------------- */

NexusError *nexus_filesystem_file_open(const NexusPath *file_path, NexusFileMode mode, NexusFileHandle **out_file_handle) {
  const char *c_mode;
  FILE       *file;
  NexusError *error;

  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_filesystem_file_open");

  switch (mode) {
  case NFM_READ:
    c_mode = "r";
    break;
  case NFM_READ_BINARY:
    c_mode = "rb";
    break;
  case NFM_WRITE:
    c_mode = "w";
    break;
  case NFM_WRITE_BINARY:
    c_mode = "wb";
    break;
  case NFM_APPEND:
    c_mode = "a";
    break;
  case NFM_APPEND_BINARY:
    c_mode = "ab";
    break;
  case NFM_READ_PLUS:
    c_mode = "r+";
    break;
  case NFM_READ_BINARY_PLUS:
    c_mode = "rb+";
    break;
  case NFM_WRITE_PLUS:
    c_mode = "w+";
    break;
  case NFM_WRITE_BINARY_PLUS:
    c_mode = "wb+";
    break;
  case NFM_APPEND_PLUS:
    c_mode = "a+";
    break;
  case NFM_APPEND_BINARY_PLUS:
    c_mode = "ab+";
    break;
  default:
    /* An out-of-range enum must never be silently reinterpreted as a valid mode. */
    NEXUS_PROGRAM_PRECONDITION_ALWAYS(FALSE, "invalid NexusFileMode: %d", (int)mode);
    c_mode = NULL; /* unreachable -- PRECONDITION_ALWAYS aborts above */
    break;
  }

  file = fopen(file_path->buffer, c_mode);
  if (file == NULL) {
    error = NEXUS_PROGRAM_ERROR_FROM_ERRNO("failed to open '%s'", file_path->buffer);
    NEXUS_PROGRAM_CONTEXT_POP();
    return error;
  }

  *out_file_handle = (NexusFileHandle *)file;
  NEXUS_PROGRAM_CONTEXT_POP();
  return &nexus_program_errors_success;
}

#  if NEXUS_PLATFORM_LINUX

NexusError *nexus_filesystem_file_anonymous_create(uint_large initial_size, NexusFileHandle **out_file_handle) {
  int         raw_descriptor;
  FILE       *file;
  NexusError *error;

  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_filesystem_file_anonymous_create");
  NEXUS_PROGRAM_PRECONDITION(initial_size > 0, "nexus_filesystem_file_anonymous_create: initial_size must be > 0");
  NEXUS_PROGRAM_PRECONDITION(out_file_handle != NULL, "nexus_filesystem_file_anonymous_create: out_file_handle must not be NULL");

  if ((off_t)initial_size < 0 || (uint_large)(off_t)initial_size != initial_size) {
    error = NEXUS_PROGRAM_ERROR_FAILURE(NEXUS_FILESYSTEM_ERROR_FILE_SIZE_OUT_OF_RANGE, "anonymous file size %llu exceeds off_t",
                                        (unsigned long long)initial_size);
    NEXUS_PROGRAM_CONTEXT_POP();
    return error;
  }

  raw_descriptor = memfd_create("nexus-anonymous-file", N_FILESYSTEM_MFD_CLOEXEC);
  if (raw_descriptor < 0) {
    error = NEXUS_PROGRAM_ERROR_FROM_ERRNO("memfd_create failed");
    NEXUS_PROGRAM_CONTEXT_POP();
    return error;
  }

  if (ftruncate(raw_descriptor, (off_t)initial_size) != 0) {
    error = NEXUS_PROGRAM_ERROR_FROM_ERRNO("ftruncate failed sizing an anonymous file to %llu bytes", (unsigned long long)initial_size);
    (void)close(raw_descriptor);
    NEXUS_PROGRAM_CONTEXT_POP();
    return error;
  }

  file = fdopen(raw_descriptor, "r+b");
  if (file == NULL) {
    error = NEXUS_PROGRAM_ERROR_FROM_ERRNO("fdopen failed for an anonymous file's descriptor");
    (void)close(raw_descriptor);
    NEXUS_PROGRAM_CONTEXT_POP();
    return error;
  }

  *out_file_handle = (NexusFileHandle *)file;
  NEXUS_PROGRAM_CONTEXT_POP();
  return &nexus_program_errors_success;
}

#  else /* !NEXUS_PLATFORM_LINUX (still NEXUS_PLATFORM_POSIX) */

NexusError *nexus_filesystem_file_anonymous_create(uint_large initial_size, NexusFileHandle **out_file_handle) {
  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_filesystem_file_anonymous_create");
  NEXUS_PLATFORM_UNSUPPORTED_RAISE("filesystem_file_anonymous_create");
}

#  endif /* NEXUS_PLATFORM_LINUX */

int32 nexus_filesystem_file_descriptor_get(NexusFileHandle *file_handle) {
  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_filesystem_file_descriptor_get");
  NEXUS_PROGRAM_PRECONDITION(file_handle != NULL, "nexus_filesystem_file_descriptor_get: file_handle must not be NULL");

  NEXUS_PROGRAM_CONTEXT_POP();
  return (int32)fileno((FILE *)file_handle);
}

NexusError *nexus_filesystem_file_size_get(NexusFileHandle *file_handle, uint_large *out_size) {
  struct stat status;
  NexusError *error;

  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_filesystem_file_size_get");
  NEXUS_PROGRAM_PRECONDITION(file_handle != NULL, "file_size_get: file_handle must not be NULL");
  NEXUS_PROGRAM_PRECONDITION(out_size != NULL, "file_size_get: out_size must not be NULL");

  *out_size = 0;
  if (fstat(fileno((FILE *)file_handle), &status) != 0) {
    error = NEXUS_PROGRAM_ERROR_FROM_ERRNO("failed to query file size");
    NEXUS_PROGRAM_CONTEXT_POP();
    return error;
  }

  if (status.st_size < 0 || (off_t)(uint_large)status.st_size != status.st_size) {
    error = NEXUS_PROGRAM_ERROR_FAILURE(NEXUS_FILESYSTEM_ERROR_FILE_SIZE_OUT_OF_RANGE, "file size cannot be represented as uint_large");
    NEXUS_PROGRAM_CONTEXT_POP();
    return error;
  }

  *out_size = (uint_large)status.st_size;
  NEXUS_PROGRAM_CONTEXT_POP();
  return &nexus_program_errors_success;
}

NexusError *nexus_filesystem_file_resize(NexusFileHandle *file_handle, uint_large size) {
  NexusError *error;
  FILE       *file;

  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_filesystem_file_resize");
  NEXUS_PROGRAM_PRECONDITION(file_handle != NULL, "file_resize: file_handle must not be NULL");

  if ((off_t)size < 0 || (uint_large)(off_t)size != size) {
    error = NEXUS_PROGRAM_ERROR_FAILURE(NEXUS_FILESYSTEM_ERROR_FILE_SIZE_OUT_OF_RANGE, "file size %llu exceeds off_t", (unsigned long long)size);
    NEXUS_PROGRAM_CONTEXT_POP();
    return error;
  }

  file = (FILE *)file_handle;
  /* POSIX fflush also discards unread buffered input on a seekable stream,
     keeping stale read-ahead from surviving a shrink. */
  if (fflush(file) != 0) {
    error = NEXUS_PROGRAM_ERROR_FROM_ERRNO("failed to synchronize file before resizing");
    NEXUS_PROGRAM_CONTEXT_POP();
    return error;
  }

  if (ftruncate(fileno(file), (off_t)size) != 0) {
    error = NEXUS_PROGRAM_ERROR_FROM_ERRNO("failed to resize file to %llu bytes", (unsigned long long)size);
    NEXUS_PROGRAM_CONTEXT_POP();
    return error;
  }

  NEXUS_PROGRAM_CONTEXT_POP();
  return &nexus_program_errors_success;
}

NexusError *nexus_filesystem_file_close(NexusFileHandle *file_handle) {
  NexusError *error;

  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_filesystem_file_close");

  if (file_handle == NULL) {
    NEXUS_PROGRAM_CONTEXT_POP();
    return &nexus_program_errors_success;
  }

  if (fclose((FILE *)file_handle) != 0) {
    error = NEXUS_PROGRAM_ERROR_FROM_ERRNO("failed to close file");
    NEXUS_PROGRAM_CONTEXT_POP();
    return error;
  }

  NEXUS_PROGRAM_CONTEXT_POP();
  return &nexus_program_errors_success;
}

NexusError *nexus_filesystem_file_read(NexusFileHandle *file_handle, byte *buffer, uint_large byte_length, uint_large *out_bytes_read,
                                       boolean *out_reached_eof) {
  size_t      read_count;
  FILE       *file;
  NexusError *error;

  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_filesystem_file_read");

  *out_bytes_read  = 0;
  *out_reached_eof = FALSE;

  if (byte_length == 0) {
    NEXUS_PROGRAM_CONTEXT_POP();
    return &nexus_program_errors_success;
  }

  NEXUS_PROGRAM_PRECONDITION(byte_length <= (uint_large)((size_t)-1), "file_read: byte_length exceeds this platform's size_t range");

  file            = (FILE *)file_handle;
  read_count      = fread(buffer, 1, (size_t)byte_length, file);
  *out_bytes_read = (uint_large)read_count;

  if (ferror(file) != 0) {
    error = NEXUS_PROGRAM_ERROR_FROM_ERRNO("failed to read from file");
    NEXUS_PROGRAM_CONTEXT_POP();
    return error;
  }

  if (read_count < (size_t)byte_length && feof(file) != 0) {
    *out_reached_eof = TRUE;
  }

  NEXUS_PROGRAM_CONTEXT_POP();
  return &nexus_program_errors_success;
}

NexusError *nexus_filesystem_file_read_at(NexusFileHandle *file_handle, byte *buffer, uint_large byte_length, uint32 start_byte,
                                          uint_large *out_bytes_read) {
  size_t      read_count;
  FILE       *file;
  NexusError *error;

  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_filesystem_file_read_at");

  file = (FILE *)file_handle;

  if (fseek(file, (long)start_byte, SEEK_SET) != 0) {
    *out_bytes_read = 0;
    error           = NEXUS_PROGRAM_ERROR_FROM_ERRNO("failed to seek to byte %u", start_byte);
    NEXUS_PROGRAM_CONTEXT_POP();
    return error;
  }

  NEXUS_PROGRAM_PRECONDITION(byte_length <= (uint_large)((size_t)-1), "file_read_at: byte_length exceeds this platform's size_t range");

  read_count      = fread(buffer, 1, (size_t)byte_length, file);
  *out_bytes_read = (uint_large)read_count;

  if (ferror(file) != 0) {
    error = NEXUS_PROGRAM_ERROR_FROM_ERRNO("failed to read from file");
    NEXUS_PROGRAM_CONTEXT_POP();
    return error;
  }

  NEXUS_PROGRAM_CONTEXT_POP();
  return &nexus_program_errors_success;
}

NexusError *nexus_filesystem_file_read_line(NexusFileHandle *file_handle, char *buffer, uint_large buffer_max_length, uint_large *out_bytes_read) {
  FILE       *file;
  NexusError *error;

  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_filesystem_file_read_line");
  NEXUS_PROGRAM_PRECONDITION(buffer_max_length > 0, "file_read_line: buffer_max_length must be > 0");

  file = (FILE *)file_handle;

  if (fgets(buffer, (int)buffer_max_length, file) == NULL) {
    *out_bytes_read = 0;

    if (feof(file) != 0) {
      NEXUS_PROGRAM_CONTEXT_POP();
      return &nexus_program_errors_success;
    }

    error = NEXUS_PROGRAM_ERROR_FROM_ERRNO("failed to read a line from file");
    NEXUS_PROGRAM_CONTEXT_POP();
    return error;
  }

  *out_bytes_read = nexus_strings_string_length_get(buffer);
  if (*out_bytes_read > 0 && buffer[*out_bytes_read - 1] == '\n') {
    buffer[*out_bytes_read - 1] = '\0';
    (*out_bytes_read)--;
  }

  NEXUS_PROGRAM_CONTEXT_POP();
  return &nexus_program_errors_success;
}

NexusError *nexus_filesystem_file_read_line_allocated(NexusFileHandle *file_handle, char **io_buffer, uint_large *io_buffer_capacity,
                                                      uint_large *out_bytes_read, boolean *out_reached_eof) {
  FILE       *file;
  char       *expanded_buffer;
  uint_large  count;
  uint_large  capacity;
  uint_large  expanded_capacity;
  int         character;
  NexusError *error;

  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_filesystem_file_read_line_allocated");

  file             = (FILE *)file_handle;
  count            = 0;
  capacity         = *io_buffer_capacity;
  *out_bytes_read  = 0;
  *out_reached_eof = FALSE;

  if (*io_buffer == NULL || capacity == 0) {
    capacity            = 256;
    *io_buffer          = (char *)nexus_memory_allocate(capacity);
    *io_buffer_capacity = capacity;
  }

  for (;;) {
    character = fgetc(file);

    if (character == EOF) {
      if (ferror(file) != 0) {
        error = NEXUS_PROGRAM_ERROR_FROM_ERRNO("failed to read a line from file");
        NEXUS_PROGRAM_CONTEXT_POP();
        return error;
      }
      *out_reached_eof = TRUE;
      break;
    }

    if (character == '\n') {
      break;
    }

    if (character == '\r') {
      int next_character;

      next_character = fgetc(file);
      if (next_character == EOF) {
        if (ferror(file) != 0) {
          error = NEXUS_PROGRAM_ERROR_FROM_ERRNO("failed to read a line from file");
          NEXUS_PROGRAM_CONTEXT_POP();
          return error;
        }
        *out_reached_eof = TRUE;
      } else if (next_character != '\n') {
        if (ungetc(next_character, file) == EOF) {
          error = NEXUS_PROGRAM_ERROR_FROM_ERRNO("failed to push back a character while reading a line");
          NEXUS_PROGRAM_CONTEXT_POP();
          return error;
        }
      }
      break;
    }

    NEXUS_PROGRAM_PRECONDITION_ALWAYS(count < UINT_LARGE_MAX_VAL - 1, "file_read_line_allocated: line exceeds the maximum representable length");

    if (count + 1 >= capacity) {
      expanded_capacity = (capacity > UINT_LARGE_MAX_VAL / 2) ? (count + 2) : (capacity * 2);

      NEXUS_PROGRAM_PRECONDITION_ALWAYS(expanded_capacity > count + 1, "file_read_line_allocated: capacity growth overflowed");

      expanded_buffer     = (char *)nexus_memory_reallocate(*io_buffer, expanded_capacity);
      *io_buffer          = expanded_buffer;
      *io_buffer_capacity = expanded_capacity;
      capacity            = expanded_capacity;
    }

    (*io_buffer)[count++] = (char)character;
  }

  if (count > 0 && (*io_buffer)[count - 1] == '\r') {
    count--;
  }

  (*io_buffer)[count] = '\0';
  *out_bytes_read     = count;
  NEXUS_PROGRAM_CONTEXT_POP();
  return &nexus_program_errors_success;
}

NexusError *nexus_filesystem_file_write(NexusFileHandle *file_handle, const byte *bytes, uint_large length, uint_large *out_bytes_written) {
  size_t      written_count;
  FILE       *file;
  NexusError *error;

  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_filesystem_file_write");
  NEXUS_PROGRAM_PRECONDITION(length <= (uint_large)((size_t)-1), "file_write: length exceeds this platform's size_t range");

  file               = (FILE *)file_handle;
  written_count      = fwrite(bytes, 1, (size_t)length, file);
  *out_bytes_written = (uint_large)written_count;

  if (written_count != (size_t)length && ferror(file) != 0) {
    error = NEXUS_PROGRAM_ERROR_FROM_ERRNO("failed to write to file");
    NEXUS_PROGRAM_CONTEXT_POP();
    return error;
  }

  NEXUS_PROGRAM_CONTEXT_POP();
  return &nexus_program_errors_success;
}

NexusError *nexus_filesystem_file_flush(NexusFileHandle *file_handle) {
  NexusError *error;

  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_filesystem_file_flush");

  if (file_handle == NULL) {
    NEXUS_PROGRAM_CONTEXT_POP();
    return &nexus_program_errors_success;
  }

  if (fflush((FILE *)file_handle) != 0) {
    error = NEXUS_PROGRAM_ERROR_FROM_ERRNO("failed to flush file");
    NEXUS_PROGRAM_CONTEXT_POP();
    return error;
  }

  NEXUS_PROGRAM_CONTEXT_POP();
  return &nexus_program_errors_success;
}

NexusError *nexus_filesystem_file_copy(const NexusPath *source_path, const NexusPath *destination_path) {
  NexusFileHandle *source_handle;
  NexusFileHandle *destination_handle;
  FILE            *source_file;
  byte             chunk[NEXUS_FILESYSTEM_COPY_CHUNK_SIZE];
  NexusError      *error;
  NexusError      *flush_error;

  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_filesystem_file_copy");
  NEXUS_PROGRAM_PRECONDITION(!nexus_strings_string_equals(source_path->buffer, destination_path->buffer),
                             "file_copy: source_path and destination_path must differ");

  error = nexus_filesystem_file_ensure_parent_directory(destination_path);
  if (error->code != GLOBAL_NEXUS_ERROR_SUCCESS) {
    NEXUS_PROGRAM_CONTEXT_POP();
    return error;
  }

  error = nexus_filesystem_file_open(source_path, NFM_READ_BINARY, &source_handle);
  if (error->code != GLOBAL_NEXUS_ERROR_SUCCESS) {
    NEXUS_PROGRAM_CONTEXT_POP();
    return error;
  }

  error = nexus_filesystem_file_open(destination_path, NFM_WRITE_BINARY, &destination_handle);
  if (error->code != GLOBAL_NEXUS_ERROR_SUCCESS) {
    (void)nexus_filesystem_file_close(source_handle);
    NEXUS_PROGRAM_CONTEXT_POP();
    return error;
  }

  source_file = (FILE *)source_handle;

  for (;;) {
    size_t     read_count;
    uint_large written_count;

    read_count = fread(chunk, 1, (size_t)NEXUS_FILESYSTEM_COPY_CHUNK_SIZE, source_file);

    if (read_count > 0) {
      error = nexus_filesystem_file_write(destination_handle, chunk, (uint_large)read_count, &written_count);
      if (error->code != GLOBAL_NEXUS_ERROR_SUCCESS || written_count != (uint_large)read_count) {
        break;
      }
    }

    if (read_count < (size_t)NEXUS_FILESYSTEM_COPY_CHUNK_SIZE) {
      if (ferror(source_file) != 0) {
        error = NEXUS_PROGRAM_ERROR_FROM_ERRNO("failed to read '%s' while copying", source_path->buffer);
      }
      break;
    }
  }

  flush_error = nexus_filesystem_file_flush(destination_handle);
  if (error->code == GLOBAL_NEXUS_ERROR_SUCCESS) {
    error = flush_error;
  }

  (void)nexus_filesystem_file_close(destination_handle);
  (void)nexus_filesystem_file_close(source_handle);

  NEXUS_PROGRAM_CONTEXT_POP();
  return error;
}

NexusError *nexus_filesystem_file_ensure_parent_directory(const NexusPath *file_path) {
  NexusPath parent_path;

  parent_path = nexus_filesystem_path_parent(file_path);

  if (nexus_strings_string_equals(parent_path.buffer, file_path->buffer)) {
    /* No separator in file_path at all -- nothing to create. */
    return &nexus_program_errors_success;
  }

  return nexus_filesystem_directory_create_parents(&parent_path);
}

/* ---------------------------------------------------------------------------- */
/* DIRECTORIES                                                                  */
/* ---------------------------------------------------------------------------- */

NexusError *nexus_filesystem_directory_create_parents(const NexusPath *directory_path) {
  NexusPath   parent_path;
  NexusError *error;

  parent_path = nexus_filesystem_path_parent(directory_path);

  if (!nexus_strings_string_equals(parent_path.buffer, directory_path->buffer)) {
    error = nexus_filesystem_directory_create_parents(&parent_path);
    if (error->code != GLOBAL_NEXUS_ERROR_SUCCESS) {
      return error;
    }
  }

  return nexus_filesystem_directory_create(directory_path);
}

/* Classify entries through lstat so a symlink remains a leaf instead of becoming a recursion
   point. Propagate classification failures rather than guessing an entry type. */
NexusError *nexus_filesystem_directory_delete(const NexusPath *directory_path, boolean recursive) {
  DIR           *directory;
  struct dirent *entry;
  NexusPath      current_path;
  NexusPathType  entry_type;
  NexusError    *error;

  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_filesystem_directory_delete");

  if (!recursive) {
    if (rmdir(directory_path->buffer) == 0 || errno == ENOENT) {
      NEXUS_PROGRAM_CONTEXT_POP();
      return &nexus_program_errors_success;
    }

    error = NEXUS_PROGRAM_ERROR_FROM_ERRNO("failed to delete directory '%s'", directory_path->buffer);
    NEXUS_PROGRAM_CONTEXT_POP();
    return error;
  }

  directory = opendir(directory_path->buffer);
  if (directory != NULL) {
    for (;;) {
      entry = readdir(directory);
      if (entry == NULL) {
        break;
      }

      if (entry->d_name[0] == '.' && (entry->d_name[1] == '\0' || (entry->d_name[1] == '.' && entry->d_name[2] == '\0'))) {
        continue;
      }

      current_path = *directory_path;
      nexus_filesystem_path_join(&current_path, entry->d_name);

      error = nexus_filesystem_path_type_get(&current_path, &entry_type);
      if (error->code != GLOBAL_NEXUS_ERROR_SUCCESS) {
        (void)closedir(directory);
        NEXUS_PROGRAM_CONTEXT_POP();
        return error;
      }

      error = (entry_type == NPT_DIRECTORY) ? nexus_filesystem_directory_delete(&current_path, TRUE) : nexus_filesystem_file_delete(&current_path);
      if (error->code != GLOBAL_NEXUS_ERROR_SUCCESS) {
        (void)closedir(directory);
        NEXUS_PROGRAM_CONTEXT_POP();
        return error;
      }
    }

    (void)closedir(directory);
  } else if (errno != ENOENT) {
    error = NEXUS_PROGRAM_ERROR_FROM_ERRNO("failed to open directory '%s'", directory_path->buffer);
    NEXUS_PROGRAM_CONTEXT_POP();
    return error;
  }

  if (rmdir(directory_path->buffer) == 0 || errno == ENOENT) {
    NEXUS_PROGRAM_CONTEXT_POP();
    return &nexus_program_errors_success;
  }

  error = NEXUS_PROGRAM_ERROR_FROM_ERRNO("failed to delete directory '%s'", directory_path->buffer);
  NEXUS_PROGRAM_CONTEXT_POP();
  return error;
}

NexusError *nexus_filesystem_directory_visit(const NexusPath *directory_path, NexusFilesystemVisitCallback *callback, void *user_data) {
  DIR           *directory;
  struct dirent *entry;
  NexusPath      entry_path;
  NexusError    *error;

  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_filesystem_directory_visit");

  directory = opendir(directory_path->buffer);
  if (directory == NULL) {
    error = NEXUS_PROGRAM_ERROR_FROM_ERRNO("failed to open directory '%s'", directory_path->buffer);
    NEXUS_PROGRAM_CONTEXT_POP();
    return error;
  }

  error = &nexus_program_errors_success;

  for (;;) {
    errno = 0;
    entry = readdir(directory);
    if (entry == NULL) {
      if (errno != 0) {
        error = NEXUS_PROGRAM_ERROR_FROM_ERRNO("failed to read directory '%s'", directory_path->buffer);
      }
      break;
    }

    if (entry->d_name[0] == '.' && (entry->d_name[1] == '\0' || (entry->d_name[1] == '.' && entry->d_name[2] == '\0'))) {
      continue;
    }

    entry_path = *directory_path;
    nexus_filesystem_path_join(&entry_path, entry->d_name);

    error = callback(&entry_path, user_data);
    if (error->code != GLOBAL_NEXUS_ERROR_SUCCESS) {
      break;
    }
  }

  if (closedir(directory) != 0 && error->code == GLOBAL_NEXUS_ERROR_SUCCESS) {
    error = NEXUS_PROGRAM_ERROR_FROM_ERRNO("failed to close directory '%s'", directory_path->buffer);
  }

  NEXUS_PROGRAM_CONTEXT_POP();
  return error;
}

/* Stop on the first callback failure, propagate directory errors, and use lstat-based entry
   classification so a symlink is never followed as a recursion point. */
NexusError *nexus_filesystem_directory_walk(const NexusPath *directory_path, boolean recursive, NexusFilesystemWalkCallback *callback,
                                            void *user_data) {
  DIR           *directory;
  struct dirent *entry;
  NexusPath      entry_path;
  NexusPathType  entry_type;
  NexusError    *error;

  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_filesystem_directory_walk");

  directory = opendir(directory_path->buffer);
  if (directory == NULL) {
    error = NEXUS_PROGRAM_ERROR_FROM_ERRNO("failed to open directory '%s'", directory_path->buffer);
    NEXUS_PROGRAM_CONTEXT_POP();
    return error;
  }

  error = &nexus_program_errors_success;

  for (;;) {
    errno = 0;
    entry = readdir(directory);
    if (entry == NULL) {
      if (errno != 0) {
        error = NEXUS_PROGRAM_ERROR_FROM_ERRNO("failed to read directory '%s'", directory_path->buffer);
      }
      break;
    }

    if (entry->d_name[0] == '.' && (entry->d_name[1] == '\0' || (entry->d_name[1] == '.' && entry->d_name[2] == '\0'))) {
      continue;
    }

    entry_path = *directory_path;
    nexus_filesystem_path_join(&entry_path, entry->d_name);

    error = nexus_filesystem_path_type_get(&entry_path, &entry_type);
    if (error->code != GLOBAL_NEXUS_ERROR_SUCCESS) {
      break;
    }

    error = callback(&entry_path, entry_type, user_data);
    if (error->code != GLOBAL_NEXUS_ERROR_SUCCESS) {
      break;
    }

    if (recursive && entry_type == NPT_DIRECTORY) {
      error = nexus_filesystem_directory_walk(&entry_path, TRUE, callback, user_data);
      if (error->code != GLOBAL_NEXUS_ERROR_SUCCESS) {
        break;
      }
    }
  }

  if (closedir(directory) != 0 && error->code == GLOBAL_NEXUS_ERROR_SUCCESS) {
    error = NEXUS_PROGRAM_ERROR_FROM_ERRNO("failed to close directory '%s'", directory_path->buffer);
  }

  NEXUS_PROGRAM_CONTEXT_POP();
  return error;
}

typedef enum NFilesystemCollectMode {
  N_FILESYSTEM_COLLECT_MODE_COUNT,
  N_FILESYSTEM_COLLECT_MODE_WRITE
} NFilesystemCollectMode;

typedef struct NFilesystemCollector {
  NexusFilesystemFilterCallback *filter;
  const void                    *filter_user_data;
  NexusPathList                 *path_list;
  NFilesystemCollectMode         mode;
  uint64                         write_index;
} NFilesystemCollector;

static NexusError *_nexus_filesystem_collector_callback(const NexusPath *entry_path, NexusPathType entry_type, void *user_data) {
  NFilesystemCollector *collector = (NFilesystemCollector *)user_data;

  NEXUS_PROGRAM_CONTEXT_PUSH("_nexus_filesystem_collector_callback");

  if (collector->filter != NULL && !collector->filter(entry_path, entry_type, collector->filter_user_data)) {
    NEXUS_PROGRAM_CONTEXT_POP();
    return &nexus_program_errors_success;
  }

  switch (collector->mode) {
  case N_FILESYSTEM_COLLECT_MODE_COUNT:
    collector->path_list->count++;
    break;

  case N_FILESYSTEM_COLLECT_MODE_WRITE:
    NEXUS_PROGRAM_PRECONDITION_ALWAYS(collector->write_index < collector->path_list->count,
                                      "directory_collect: destination path list is smaller than the match count");
    collector->path_list->paths[collector->write_index] = *entry_path;
    collector->write_index++;
    break;
  }

  NEXUS_PROGRAM_CONTEXT_POP();
  return &nexus_program_errors_success;
}

NexusError *nexus_filesystem_directory_collect(const NexusPath *directory_path, boolean recursive, NexusFilesystemFilterCallback *filter,
                                               const void *filter_user_data, NexusPathList *out_path_list) {
  NFilesystemCollector collector;

  collector.filter           = filter;
  collector.filter_user_data = filter_user_data;
  collector.path_list        = out_path_list;
  collector.write_index      = 0;

  if (out_path_list->paths == NULL) {
    out_path_list->count = 0;
    collector.mode       = N_FILESYSTEM_COLLECT_MODE_COUNT;
    return nexus_filesystem_directory_walk(directory_path, recursive, _nexus_filesystem_collector_callback, &collector);
  }

  collector.mode = N_FILESYSTEM_COLLECT_MODE_WRITE;
  return nexus_filesystem_directory_walk(directory_path, recursive, _nexus_filesystem_collector_callback, &collector);
}

NexusError *nexus_filesystem_directory_collect_allocated(const NexusPath *directory_path, boolean recursive, NexusFilesystemFilterCallback *filter,
                                                         const void *filter_user_data, NexusPathList *out_path_list) {
  NexusError *error;

  out_path_list->paths = NULL;
  out_path_list->count = 0;

  error = nexus_filesystem_directory_collect(directory_path, recursive, filter, filter_user_data, out_path_list);
  if (error->code != GLOBAL_NEXUS_ERROR_SUCCESS || out_path_list->count == 0) {
    return error;
  }

  out_path_list->paths = (NexusPath *)nexus_memory_allocate(out_path_list->count * NEXUS_SIZEOF(NexusPath));

  return nexus_filesystem_directory_collect(directory_path, recursive, filter, filter_user_data, out_path_list);
}

#  if !defined(NEXUS_FILESYSTEM_USE_BUILTIN_REGEX)
static boolean _nexus_filesystem_regex_filter_callback(const NexusPath *entry_path, NEXUS_MAYBE_UNUSED NexusPathType entry_type,
                                                       const void *user_data) {
  const regex_t *compiled_pattern = (const regex_t *)user_data;
  return regexec(compiled_pattern, entry_path->buffer, 0, NULL, 0) == 0;
}

NexusError *nexus_filesystem_directory_collect_matching_allocated(const NexusPath *directory_path, boolean recursive, const char *regex_pattern,
                                                                  NexusPathList *out_path_list) {
  regex_t     compiled_pattern;
  NexusError *error;
  int         compile_result;
  char        error_message[256];

  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_filesystem_directory_collect_matching_allocated");

  compile_result = regcomp(&compiled_pattern, regex_pattern, REG_EXTENDED | REG_NOSUB);
  if (compile_result != 0) {
    (void)regerror(compile_result, &compiled_pattern, error_message, NEXUS_SIZEOF(error_message));
    error = NEXUS_PROGRAM_ERROR_FAILURE(GLOBAL_NEXUS_FILESYSTEM_ERROR_REGEX_PATTERN_INVALID, "invalid regex pattern '%s': %s", regex_pattern,
                                        error_message);
    NEXUS_PROGRAM_CONTEXT_POP();
    return error;
  }

  error = nexus_filesystem_directory_collect_allocated(directory_path, recursive, _nexus_filesystem_regex_filter_callback, &compiled_pattern,
                                                       out_path_list);

  regfree(&compiled_pattern);

  NEXUS_PROGRAM_CONTEXT_POP();
  return error;
}

#  else /* NEXUS_FILESYSTEM_USE_BUILTIN_REGEX */

/*
A matcher for POSIX extended regular expressions: literals, `.`, bracket expressions (ranges, negation, [:class:]), `^` and `$`, grouping,
alternation `|`, and the repetitions `*` `+` `?` `{m}` `{m,}` `{m,n}`. A pattern matches a path when it matches anywhere in it, as regexec does.
It runs as a Thompson automaton: time is linear in the length of the path times the size of the program, however the pattern is written.
Not provided: back-references, collating elements and equivalence classes, and anything locale-dependent (classes are ASCII).
*/

#    define N_RX_MAX_NODES        1024U
#    define N_RX_MAX_INSTRUCTIONS 4096U
#    define N_RX_MAX_SETS         64U
#    define N_RX_MAX_REPEAT       255U
#    define N_RX_INFINITE         0xFFFFU

typedef enum NexusRegexNodeKind {
  N_RX_N_EMPTY,
  N_RX_N_CHAR,
  N_RX_N_ANY,
  N_RX_N_SET,
  N_RX_N_BOL,
  N_RX_N_EOL,
  N_RX_N_CAT,
  N_RX_N_ALT,
  N_RX_N_REPEAT
} NexusRegexNodeKind;

typedef struct NexusRegexNode {
  byte   kind;
  byte   ch;  /* N_RX_N_CHAR */
  uint16 a;   /* N_RX_N_SET: the set; CAT/ALT: left; REPEAT: the operand */
  uint16 b;   /* CAT/ALT: right */
  uint16 min; /* REPEAT */
  uint16 max; /* REPEAT, N_RX_INFINITE for none */
} NexusRegexNode;

typedef enum NexusRegexOp {
  N_RX_OP_CHAR,
  N_RX_OP_ANY,
  N_RX_OP_SET,
  N_RX_OP_SPLIT,
  N_RX_OP_JUMP,
  N_RX_OP_BOL,
  N_RX_OP_EOL,
  N_RX_OP_MATCH
} NexusRegexOp;

typedef struct NexusRegexInstruction {
  byte   op;
  byte   ch;
  uint16 x; /* SET: the set; SPLIT/JUMP: a target */
  uint16 y; /* SPLIT: the other target */
} NexusRegexInstruction;

typedef struct NexusRegex {
  const char           *pattern;
  uint_large            position;
  const char           *failure;
  uint32                node_count;
  NexusRegexNode        nodes[N_RX_MAX_NODES];
  uint32                set_count;
  byte                  sets[N_RX_MAX_SETS][32];
  uint32                length;
  NexusRegexInstruction program[N_RX_MAX_INSTRUCTIONS];
  /* Scratch of the matcher: two thread lists and the generation each program counter was last added in. */
  uint16 current[N_RX_MAX_INSTRUCTIONS];
  uint16 next[N_RX_MAX_INSTRUCTIONS];
  uint32 added[N_RX_MAX_INSTRUCTIONS];
  uint32 generation;
  uint16 stack[(2U * N_RX_MAX_INSTRUCTIONS) + 2U]; /* a program counter is processed once but may be pushed by several predecessors */
} NexusRegex;

static uint16 _nexus_regex_node_make(NexusRegex *regex, byte kind) {
  NexusRegexNode *node;

  if (regex->failure != NULL) {
    return 0;
  }

  if (regex->node_count >= N_RX_MAX_NODES) {
    regex->failure = "the pattern is too long";

    return 0;
  }

  node       = &regex->nodes[regex->node_count];
  node->kind = kind;
  node->ch   = 0;
  node->a    = 0;
  node->b    = 0;
  node->min  = 0;
  node->max  = 0;

  return (uint16)regex->node_count++;
}

static boolean _nexus_regex_class_has(const char *name, uint_large length, uint32 ch) {
  boolean upper;
  boolean lower;
  boolean digit;

  upper = ch >= 'A' && ch <= 'Z';
  lower = ch >= 'a' && ch <= 'z';
  digit = ch >= '0' && ch <= '9';

  if (length == 5 && nexus_strings_string_starts_with(name, "alpha")) {
    return upper || lower;
  }
  if (length == 5 && nexus_strings_string_starts_with(name, "digit")) {
    return digit;
  }
  if (length == 5 && nexus_strings_string_starts_with(name, "alnum")) {
    return upper || lower || digit;
  }
  if (length == 5 && nexus_strings_string_starts_with(name, "upper")) {
    return upper;
  }
  if (length == 5 && nexus_strings_string_starts_with(name, "lower")) {
    return lower;
  }
  if (length == 5 && nexus_strings_string_starts_with(name, "space")) {
    return ch == ' ' || (ch >= 9 && ch <= 13);
  }
  if (length == 5 && nexus_strings_string_starts_with(name, "blank")) {
    return ch == ' ' || ch == '\t';
  }
  if (length == 5 && nexus_strings_string_starts_with(name, "punct")) {
    return ch > 32 && ch < 127 && !(upper || lower || digit);
  }
  if (length == 5 && nexus_strings_string_starts_with(name, "print")) {
    return ch >= 32 && ch < 127;
  }
  if (length == 5 && nexus_strings_string_starts_with(name, "graph")) {
    return ch > 32 && ch < 127;
  }
  if (length == 5 && nexus_strings_string_starts_with(name, "cntrl")) {
    return ch < 32 || ch == 127;
  }
  if (length == 6 && nexus_strings_string_starts_with(name, "xdigit")) {
    return digit || (ch >= 'A' && ch <= 'F') || (ch >= 'a' && ch <= 'f');
  }

  return FALSE;
}

/* Summary: parses a bracket expression, the pattern position being just after `[`, into a new set; returns its node. */
static uint16 _nexus_regex_bracket_parse(NexusRegex *regex) {
  byte      *set;
  boolean    negate;
  boolean    first;
  uint32     ch;
  uint32     end;
  uint_large i;
  uint16     node;

  if (regex->set_count >= N_RX_MAX_SETS) {
    regex->failure = "too many bracket expressions";

    return 0;
  }

  set = regex->sets[regex->set_count];
  for (i = 0; i < 32; i++) {
    set[i] = 0;
  }

  negate = FALSE;
  first  = TRUE;
  if (regex->pattern[regex->position] == '^') {
    negate = TRUE;
    regex->position++;
  }

  for (;;) {
    ch = (byte)regex->pattern[regex->position];

    if (ch == '\0') {
      regex->failure = "a bracket expression is not closed";

      return 0;
    }

    if (ch == ']' && !first) {
      regex->position++;
      break;
    }

    first = FALSE;

    if (ch == '[' && regex->pattern[regex->position + 1] == ':') {
      const char *name;
      uint_large  length;
      boolean     found;

      name   = regex->pattern + regex->position + 2;
      length = 0;
      while (name[length] != '\0' && !(name[length] == ':' && name[length + 1] == ']')) {
        length++;
      }

      if (name[length] == '\0') {
        regex->failure = "a character class is not closed";

        return 0;
      }

      found = FALSE;
      for (end = 0; end < 256; end++) {
        if (_nexus_regex_class_has(name, length, end)) {
          set[end >> 3] = (byte)(set[end >> 3] | (1U << (end & 7U)));
          found         = TRUE;
        }
      }

      if (!found) {
        regex->failure = "unknown character class";

        return 0;
      }

      regex->position += length + 4;
      continue;
    }

    regex->position++;
    end = ch;

    if (regex->pattern[regex->position] == '-' && regex->pattern[regex->position + 1] != ']' && regex->pattern[regex->position + 1] != '\0') {
      end = (byte)regex->pattern[regex->position + 1];
      regex->position += 2;

      if (end < ch) {
        regex->failure = "a range of a bracket expression is reversed";

        return 0;
      }
    }

    for (; ch <= end; ch++) {
      set[ch >> 3] = (byte)(set[ch >> 3] | (1U << (ch & 7U)));
    }
  }

  if (negate) {
    for (i = 0; i < 32; i++) {
      set[i] = (byte)~set[i];
    }
  }

  node = _nexus_regex_node_make(regex, N_RX_N_SET);
  if (regex->failure == NULL) {
    regex->nodes[node].a = (uint16)regex->set_count++;
  }

  return node;
}

static uint16 _nexus_regex_alternation_parse(NexusRegex *regex, uint32 depth);

/* Summary: one atom of the pattern with its repetitions. */
static uint16 _nexus_regex_repeat_parse(NexusRegex *regex, uint32 depth) {
  uint16 atom;
  uint32 ch;

  ch = (byte)regex->pattern[regex->position];
  regex->position++;

  if (ch == '(') {
    if (depth >= 64) {
      regex->failure = "groups are nested too deeply";

      return 0;
    }

    atom = _nexus_regex_alternation_parse(regex, depth + 1);

    if (regex->failure == NULL && regex->pattern[regex->position] != ')') {
      regex->failure = "a group is not closed";

      return 0;
    }

    regex->position++;
  } else if (ch == '[') {
    atom = _nexus_regex_bracket_parse(regex);
  } else if (ch == '.') {
    atom = _nexus_regex_node_make(regex, N_RX_N_ANY);
  } else if (ch == '^') {
    atom = _nexus_regex_node_make(regex, N_RX_N_BOL);
  } else if (ch == '$') {
    atom = _nexus_regex_node_make(regex, N_RX_N_EOL);
  } else if (ch == '*' || ch == '+' || ch == '?' || ch == '{') {
    regex->failure = "a repetition has nothing to repeat";

    return 0;
  } else {
    if (ch == '\\') {
      ch = (byte)regex->pattern[regex->position];

      if (ch == '\0') {
        regex->failure = "the pattern ends with a backslash";

        return 0;
      }

      regex->position++;
    }

    atom = _nexus_regex_node_make(regex, N_RX_N_CHAR);
    if (regex->failure == NULL) {
      regex->nodes[atom].ch = (byte)ch;
    }
  }

  while (regex->failure == NULL) {
    uint32 min;
    uint32 max;
    uint32 count;
    uint16 node;

    ch = (byte)regex->pattern[regex->position];

    if (ch == '*') {
      min = 0;
      max = N_RX_INFINITE;
      regex->position++;
    } else if (ch == '+') {
      min = 1;
      max = N_RX_INFINITE;
      regex->position++;
    } else if (ch == '?') {
      min = 0;
      max = 1;
      regex->position++;
    } else if (ch == '{') {
      uint32 digits;

      regex->position++;
      min    = 0;
      digits = 0;
      while (regex->pattern[regex->position] >= '0' && regex->pattern[regex->position] <= '9') {
        min = (min * 10U) + (uint32)(regex->pattern[regex->position] - '0');
        digits++;
        regex->position++;
        if (min > N_RX_MAX_REPEAT) {
          regex->failure = "a repetition count is too large";

          return 0;
        }
      }

      if (digits == 0) {
        regex->failure = "a repetition count is missing";

        return 0;
      }

      max = min;
      if (regex->pattern[regex->position] == ',') {
        regex->position++;
        max    = N_RX_INFINITE;
        digits = 0;
        count  = 0;
        while (regex->pattern[regex->position] >= '0' && regex->pattern[regex->position] <= '9') {
          count = (count * 10U) + (uint32)(regex->pattern[regex->position] - '0');
          digits++;
          regex->position++;
          if (count > N_RX_MAX_REPEAT) {
            regex->failure = "a repetition count is too large";

            return 0;
          }
        }

        if (digits > 0) {
          max = count;
        }
      }

      if (regex->pattern[regex->position] != '}') {
        regex->failure = "a repetition is not closed";

        return 0;
      }

      regex->position++;

      if (max != N_RX_INFINITE && max < min) {
        regex->failure = "a repetition range is reversed";

        return 0;
      }
    } else {
      break;
    }

    node = _nexus_regex_node_make(regex, N_RX_N_REPEAT);
    if (regex->failure != NULL) {
      return 0;
    }

    regex->nodes[node].a   = atom;
    regex->nodes[node].min = (uint16)min;
    regex->nodes[node].max = (uint16)max;
    atom                   = node;
  }

  return atom;
}

static uint16 _nexus_regex_concatenation_parse(NexusRegex *regex, uint32 depth) {
  uint16 left;
  uint16 right;
  uint16 node;
  uint32 ch;

  left = _nexus_regex_node_make(regex, N_RX_N_EMPTY);

  for (;;) {
    ch = (byte)regex->pattern[regex->position];
    if (regex->failure != NULL || ch == '\0' || ch == '|' || ch == ')') {
      break;
    }

    right = _nexus_regex_repeat_parse(regex, depth);
    node  = _nexus_regex_node_make(regex, N_RX_N_CAT);
    if (regex->failure != NULL) {
      break;
    }

    regex->nodes[node].a = left;
    regex->nodes[node].b = right;
    left                 = node;
  }

  return left;
}

static uint16 _nexus_regex_alternation_parse(NexusRegex *regex, uint32 depth) {
  uint16 left;
  uint16 right;
  uint16 node;

  left = _nexus_regex_concatenation_parse(regex, depth);

  while (regex->failure == NULL && regex->pattern[regex->position] == '|') {
    regex->position++;
    right = _nexus_regex_concatenation_parse(regex, depth);
    node  = _nexus_regex_node_make(regex, N_RX_N_ALT);
    if (regex->failure != NULL) {
      break;
    }

    regex->nodes[node].a = left;
    regex->nodes[node].b = right;
    left                 = node;
  }

  return left;
}

static uint32 _nexus_regex_emit(NexusRegex *regex, byte operation, byte ch, uint16 x, uint16 y) {
  NexusRegexInstruction *instruction;

  if (regex->failure != NULL) {
    return 0;
  }

  if (regex->length >= N_RX_MAX_INSTRUCTIONS) {
    regex->failure = "the pattern is too complex";

    return 0;
  }

  instruction     = &regex->program[regex->length];
  instruction->op = operation;
  instruction->ch = ch;
  instruction->x  = x;
  instruction->y  = y;

  return regex->length++;
}

/* Summary: compiles a node into instructions that fall through to the instruction after them. */
static void _nexus_regex_compile(NexusRegex *regex, uint16 index, uint32 depth) {
  const NexusRegexNode *node;
  uint32                split;
  uint32                jump;
  uint32                start;
  uint32                count;
  uint32                ends[N_RX_MAX_REPEAT];

  if (regex->failure != NULL) {
    return;
  }

  if (depth > 256) {
    regex->failure = "the pattern is nested too deeply";

    return;
  }

  node = &regex->nodes[index];

  switch (node->kind) {
  case N_RX_N_EMPTY:
    break;
  case N_RX_N_CHAR:
    (void)_nexus_regex_emit(regex, N_RX_OP_CHAR, node->ch, 0, 0);
    break;
  case N_RX_N_ANY:
    (void)_nexus_regex_emit(regex, N_RX_OP_ANY, 0, 0, 0);
    break;
  case N_RX_N_SET:
    (void)_nexus_regex_emit(regex, N_RX_OP_SET, 0, node->a, 0);
    break;
  case N_RX_N_BOL:
    (void)_nexus_regex_emit(regex, N_RX_OP_BOL, 0, 0, 0);
    break;
  case N_RX_N_EOL:
    (void)_nexus_regex_emit(regex, N_RX_OP_EOL, 0, 0, 0);
    break;
  case N_RX_N_CAT:
    _nexus_regex_compile(regex, node->a, depth + 1);
    _nexus_regex_compile(regex, node->b, depth + 1);
    break;
  case N_RX_N_ALT:
    split = _nexus_regex_emit(regex, N_RX_OP_SPLIT, 0, 0, 0);
    _nexus_regex_compile(regex, node->a, depth + 1);
    jump = _nexus_regex_emit(regex, N_RX_OP_JUMP, 0, 0, 0);
    if (regex->failure == NULL) {
      regex->program[split].x = (uint16)(split + 1U);
      regex->program[split].y = (uint16)regex->length;
    }
    _nexus_regex_compile(regex, node->b, depth + 1);
    if (regex->failure == NULL) {
      regex->program[jump].x = (uint16)regex->length;
    }
    break;
  case N_RX_N_REPEAT:
    /* The operand is written `min` times, then either looped (no maximum) or made optional `max - min` times, each skipping to the end. */
    for (count = 0; count < N_RX_MAX_REPEAT; count++) {
      ends[count] = 0;
    }

    for (count = 0; count < node->min; count++) {
      _nexus_regex_compile(regex, node->a, depth + 1);
    }

    if (node->max == N_RX_INFINITE) {
      start = regex->length;
      split = _nexus_regex_emit(regex, N_RX_OP_SPLIT, 0, 0, 0);
      _nexus_regex_compile(regex, node->a, depth + 1);
      jump = _nexus_regex_emit(regex, N_RX_OP_JUMP, 0, (uint16)start, 0);
      if (regex->failure == NULL) {
        regex->program[split].x = (uint16)(split + 1U);
        regex->program[split].y = (uint16)regex->length;
      }
      (void)jump;
    } else {
      for (count = 0; count < (uint32)(node->max - node->min); count++) {
        ends[count] = _nexus_regex_emit(regex, N_RX_OP_SPLIT, 0, 0, 0);
        _nexus_regex_compile(regex, node->a, depth + 1);
        if (regex->failure == NULL) {
          regex->program[ends[count]].x = (uint16)(ends[count] + 1U);
        }
      }

      for (count = 0; regex->failure == NULL && count < (uint32)(node->max - node->min); count++) {
        regex->program[ends[count]].y = (uint16)regex->length;
      }
    }
    break;
  default:
    regex->failure = "internal error: unknown node";
    break;
  }
}

/* Summary: compiles pattern; NULL on success, otherwise what is wrong with it. */
static const char *_nexus_regex_create(NexusRegex *regex, const char *pattern) {
  uint16 root;

  regex->pattern    = pattern;
  regex->position   = 0;
  regex->failure    = NULL;
  regex->node_count = 0;
  regex->set_count  = 0;
  regex->length     = 0;
  regex->generation = 0;

  root = _nexus_regex_alternation_parse(regex, 0);

  if (regex->failure == NULL && regex->pattern[regex->position] != '\0') {
    regex->failure = "a closing parenthesis has no opening one";
  }

  _nexus_regex_compile(regex, root, 0);
  (void)_nexus_regex_emit(regex, N_RX_OP_MATCH, 0, 0, 0);

  return regex->failure;
}

/*
Summary: adds the thread at program counter start, and everything it reaches without consuming a byte, to a list; TRUE when that reaches MATCH.
Description: BOL and EOL are assertions about the position, so they are decided here, where the position is known.
*/
static boolean _nexus_regex_thread_add(NexusRegex *regex, uint16 *list, uint32 *list_length, uint32 start, uint_large position, uint_large size) {
  uint32  top;
  uint32  pc;
  boolean matched;

  matched             = FALSE;
  top                 = 0;
  regex->stack[top++] = (uint16)start;

  while (top > 0) {
    const NexusRegexInstruction *instruction;

    pc = regex->stack[--top];

    if (regex->added[pc] == regex->generation) {
      continue;
    }

    regex->added[pc] = regex->generation;
    instruction      = &regex->program[pc];

    switch (instruction->op) {
    case N_RX_OP_JUMP:
      regex->stack[top++] = instruction->x;
      break;
    case N_RX_OP_SPLIT:
      /* Pushed in reverse so the first branch is explored first; the order does not matter for a yes/no answer. */
      regex->stack[top++] = instruction->y;
      regex->stack[top++] = instruction->x;
      break;
    case N_RX_OP_BOL:
      if (position == 0) {
        regex->stack[top++] = (uint16)(pc + 1U);
      }
      break;
    case N_RX_OP_EOL:
      if (position == size) {
        regex->stack[top++] = (uint16)(pc + 1U);
      }
      break;
    case N_RX_OP_MATCH:
      matched = TRUE;
      break;
    default:
      list[(*list_length)++] = (uint16)pc;
      break;
    }
  }

  return matched;
}

/* Summary: whether the pattern matches anywhere in text. */
static boolean _nexus_regex_matches(NexusRegex *regex, const char *text) {
  uint_large size;
  uint_large position;
  uint32     current_length;
  uint32     next_length;
  uint32     i;
  uint16    *current;
  uint16    *next;
  uint16    *swap;

  size    = nexus_strings_string_length_get(text);
  current = regex->current;
  next    = regex->next;

  current_length = 0;

  /* Every call uses generations no earlier call used, so the marks never need clearing between paths; they are cleared when the counter nears its end. */
  if (regex->generation > 0xF0000000U - (uint32)(size < 0x0FFFFFFFU ? size : 0x0FFFFFFFU)) {
    for (i = 0; i < N_RX_MAX_INSTRUCTIONS; i++) {
      regex->added[i] = 0;
    }

    regex->generation = 0;
  }

  regex->generation++;

  for (position = 0; position <= size; position++) {
    /* A match may start at every position, so the start of the program joins the threads of every position. */
    /* The threads in `current` were added in the generation of this position, so adding more joins them. */
    if (_nexus_regex_thread_add(regex, current, &current_length, 0, position, size)) {
      return TRUE;
    }

    if (position == size) {
      break;
    }

    regex->generation++;
    next_length = 0;

    for (i = 0; i < current_length; i++) {
      const NexusRegexInstruction *instruction;
      boolean                      advance;
      uint32                       ch;

      instruction = &regex->program[current[i]];
      ch          = (byte)text[position];
      advance     = FALSE;

      if (instruction->op == N_RX_OP_CHAR) {
        advance = instruction->ch == ch;
      } else if (instruction->op == N_RX_OP_ANY) {
        advance = TRUE;
      } else if (instruction->op == N_RX_OP_SET) {
        advance = (regex->sets[instruction->x][ch >> 3] & (1U << (ch & 7U))) != 0;
      }

      if (advance && _nexus_regex_thread_add(regex, next, &next_length, (uint32)current[i] + 1U, position + 1, size)) {
        return TRUE;
      }
    }

    swap           = current;
    current        = next;
    next           = swap;
    current_length = next_length;
  }

  return FALSE;
}

/* The filter callback receives its user data as const, but the matcher keeps scratch state, so the data is a holder of the matcher's address. */
typedef struct NexusRegexHolder {
  NexusRegex *regex;
} NexusRegexHolder;

static boolean _nexus_filesystem_regex_filter_callback(const NexusPath *entry_path, NEXUS_MAYBE_UNUSED NexusPathType entry_type,
                                                       const void *user_data) {
  return _nexus_regex_matches(((const NexusRegexHolder *)user_data)->regex, entry_path->buffer);
}

NexusError *nexus_filesystem_directory_collect_matching_allocated(const NexusPath *directory_path, boolean recursive, const char *regex_pattern,
                                                                  NexusPathList *out_path_list) {
  NexusRegex      *regex;
  NexusRegexHolder holder;
  NexusError      *error;
  const char      *failure;
  uint32           failure_index;

  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_filesystem_directory_collect_matching_allocated");

  regex   = (NexusRegex *)nexus_memory_allocate(NEXUS_SIZEOF(*regex));
  failure = _nexus_regex_create(regex, regex_pattern);
  if (failure != NULL) {
    error =
        NEXUS_PROGRAM_ERROR_FAILURE(GLOBAL_NEXUS_FILESYSTEM_ERROR_REGEX_PATTERN_INVALID, "invalid regex pattern '%s': %s", regex_pattern, failure);
    nexus_memory_deallocate(regex);
    NEXUS_PROGRAM_CONTEXT_POP();
    return error;
  }

  for (failure_index = 0; failure_index < N_RX_MAX_INSTRUCTIONS; failure_index++) {
    regex->added[failure_index] = 0;
  }

  regex->generation = 0;

  holder.regex = regex;
  error = nexus_filesystem_directory_collect_allocated(directory_path, recursive, _nexus_filesystem_regex_filter_callback, &holder, out_path_list);

  nexus_memory_deallocate(regex);

  NEXUS_PROGRAM_CONTEXT_POP();
  return error;
}

#  endif /* NEXUS_FILESYSTEM_USE_BUILTIN_REGEX */

/* ---------------------------------------------------------------------------- */
/* SPECIAL DIRECTORIES                                                          */
/* ---------------------------------------------------------------------------- */

NexusError *nexus_filesystem_special_directory_get(NexusSpecialDirectory directory, NexusPath *out_path) {
  const char *value;
#  if NEXUS_PLATFORM_POSIX
  struct passwd *password_entry;
#  endif
  NexusPath   home_path;
  NexusError *result;

  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_filesystem_special_directory_get");

  switch (directory) {
  case NSD_HOME:
    value = getenv("HOME");
    if (value != NULL && value[0] != '\0') {
      *out_path = nexus_filesystem_path_create(value);
      result    = &nexus_program_errors_success;
      break;
    }

#  if NEXUS_PLATFORM_POSIX
    password_entry = getpwuid(getuid());
    if (password_entry != NULL && password_entry->pw_dir != NULL) {
      *out_path = nexus_filesystem_path_create(password_entry->pw_dir);
      result    = &nexus_program_errors_success;
      break;
    }
#  else
    value = getenv("USERPROFILE");
    if (value != NULL && value[0] != '\0') {
      *out_path = nexus_filesystem_path_create(value);
      result    = &nexus_program_errors_success;
      break;
    }
#  endif

    result = NEXUS_PROGRAM_ERROR_FROM_ERRNO("could not determine the home directory: no HOME and no passwd entry");
    break;

  case NSD_TEMP:
    value = getenv("TMPDIR");
    if (value == NULL || value[0] == '\0') {
      value = getenv("TMP");
    }
    if (value == NULL || value[0] == '\0') {
      value = getenv("TEMP");
    }
    if (value == NULL || value[0] == '\0') {
      value = "/tmp";
    }
    *out_path = nexus_filesystem_path_create(value);
    result    = &nexus_program_errors_success;
    break;

  case NSD_CONFIG:
  case NSD_CACHE:
  case NSD_DATA: {
    const char *xdg_variable;
    const char *fallback_suffix;

    switch (directory) {
    case NSD_CONFIG:
      xdg_variable    = "XDG_CONFIG_HOME";
      fallback_suffix = ".config";
      break;
    case NSD_CACHE:
      xdg_variable    = "XDG_CACHE_HOME";
      fallback_suffix = ".cache";
      break;
    default:
      xdg_variable    = "XDG_DATA_HOME";
      fallback_suffix = ".local/share";
      break;
    }

    value = getenv(xdg_variable);
    if (value != NULL && value[0] != '\0') {
      *out_path = nexus_filesystem_path_create(value);
      result    = &nexus_program_errors_success;
      break;
    }

    result = nexus_filesystem_special_directory_get(NSD_HOME, &home_path);
    if (result->code != GLOBAL_NEXUS_ERROR_SUCCESS) {
      break;
    }

    nexus_filesystem_path_join(&home_path, fallback_suffix);
    *out_path = home_path;
    result    = &nexus_program_errors_success;
    break;
  }

  case NSD_OPT:
    *out_path = nexus_filesystem_path_create("/opt");
    result    = &nexus_program_errors_success;
    break;

  default:
    NEXUS_PROGRAM_PRECONDITION_ALWAYS(FALSE, "invalid NexusSpecialDirectory: %d", (int)directory);
    result = &nexus_program_errors_success; /* unreachable -- PRECONDITION_ALWAYS aborts above */
    break;
  }

  NEXUS_PROGRAM_CONTEXT_POP();
  return result;
}

#else /* !NEXUS_PLATFORM_POSIX */

NexusError *nexus_filesystem_file_open(const NexusPath *file_path, NexusFileMode mode, NexusFileHandle **out_file_handle) {
  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_filesystem_file_open");
  NEXUS_PLATFORM_UNSUPPORTED_RAISE("filesystem_file_open");
}
NexusError *nexus_filesystem_file_anonymous_create(uint_large initial_size, NexusFileHandle **out_file_handle) {
  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_filesystem_file_anonymous_create");
  NEXUS_PLATFORM_UNSUPPORTED_RAISE("filesystem_file_anonymous_create");
}
int32 nexus_filesystem_file_descriptor_get(NexusFileHandle *file_handle) {
  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_filesystem_file_descriptor_get");
  NEXUS_PLATFORM_UNSUPPORTED_RAISE("filesystem_file_descriptor_get");
}
NexusError *nexus_filesystem_file_size_get(NexusFileHandle *file_handle, uint_large *out_size) {
  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_filesystem_file_size_get");
  NEXUS_PLATFORM_UNSUPPORTED_RAISE("filesystem_file_size_get");
}
NexusError *nexus_filesystem_file_resize(NexusFileHandle *file_handle, uint_large size) {
  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_filesystem_file_resize");
  NEXUS_PLATFORM_UNSUPPORTED_RAISE("filesystem_file_resize");
}
NexusError *nexus_filesystem_file_close(NexusFileHandle *file_handle) {
  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_filesystem_file_close");
  NEXUS_PLATFORM_UNSUPPORTED_RAISE("filesystem_file_close");
}
NexusError *nexus_filesystem_file_read(NexusFileHandle *file_handle, byte *buffer, uint_large byte_length, uint_large *out_bytes_read,
                                       boolean *out_reached_eof) {
  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_filesystem_file_read");
  NEXUS_PLATFORM_UNSUPPORTED_RAISE("filesystem_file_read");
}
NexusError *nexus_filesystem_file_read_at(NexusFileHandle *file_handle, byte *buffer, uint_large byte_length, uint32 start_byte,
                                          uint_large *out_bytes_read) {
  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_filesystem_file_read_at");
  NEXUS_PLATFORM_UNSUPPORTED_RAISE("filesystem_file_read_at");
}
NexusError *nexus_filesystem_file_read_line(NexusFileHandle *file_handle, char *buffer, uint_large buffer_max_length, uint_large *out_bytes_read) {
  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_filesystem_file_read_line");
  NEXUS_PLATFORM_UNSUPPORTED_RAISE("filesystem_file_read_line");
}
NexusError *nexus_filesystem_file_read_line_allocated(NexusFileHandle *file_handle, char **io_buffer, uint_large *io_buffer_capacity,
                                                      uint_large *out_bytes_read, boolean *out_reached_eof) {
  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_filesystem_file_read_line_allocated");
  NEXUS_PLATFORM_UNSUPPORTED_RAISE("filesystem_file_read_line_allocated");
}
NexusError *nexus_filesystem_file_write(NexusFileHandle *file_handle, const byte *bytes, uint_large length, uint_large *out_bytes_written) {
  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_filesystem_file_write");
  NEXUS_PLATFORM_UNSUPPORTED_RAISE("filesystem_file_write");
}
NexusError *nexus_filesystem_file_flush(NexusFileHandle *file_handle) {
  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_filesystem_file_flush");
  NEXUS_PLATFORM_UNSUPPORTED_RAISE("filesystem_file_flush");
}
NexusError *nexus_filesystem_file_copy(const NexusPath *source_path, const NexusPath *destination_path) {
  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_filesystem_file_copy");
  NEXUS_PLATFORM_UNSUPPORTED_RAISE("filesystem_file_copy");
}
NexusError *nexus_filesystem_file_ensure_parent_directory(const NexusPath *file_path) {
  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_filesystem_file_ensure_parent_directory");
  NEXUS_PLATFORM_UNSUPPORTED_RAISE("filesystem_file_ensure_parent_directory");
}
NexusError *nexus_filesystem_directory_create_parents(const NexusPath *directory_path) {
  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_filesystem_directory_create_parents");
  NEXUS_PLATFORM_UNSUPPORTED_RAISE("filesystem_directory_create_parents");
}
NexusError *nexus_filesystem_directory_delete(const NexusPath *directory_path, boolean recursive) {
  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_filesystem_directory_delete");
  NEXUS_PLATFORM_UNSUPPORTED_RAISE("filesystem_directory_delete");
}
NexusError *nexus_filesystem_directory_visit(const NexusPath *directory_path, NexusFilesystemVisitCallback *callback, void *user_data) {
  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_filesystem_directory_visit");
  NEXUS_PLATFORM_UNSUPPORTED_RAISE("filesystem_directory_visit");
}
NexusError *nexus_filesystem_directory_walk(const NexusPath *directory_path, boolean recursive, NexusFilesystemWalkCallback *callback,
                                            void *user_data) {
  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_filesystem_directory_walk");
  NEXUS_PLATFORM_UNSUPPORTED_RAISE("filesystem_directory_walk");
}
NexusError *nexus_filesystem_directory_collect(const NexusPath *directory_path, boolean recursive, NexusFilesystemFilterCallback *filter,
                                               const void *filter_user_data, NexusPathList *out_path_list) {
  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_filesystem_directory_collect");
  NEXUS_PLATFORM_UNSUPPORTED_RAISE("filesystem_directory_collect");
}
NexusError *nexus_filesystem_directory_collect_allocated(const NexusPath *directory_path, boolean recursive, NexusFilesystemFilterCallback *filter,
                                                         const void *filter_user_data, NexusPathList *out_path_list) {
  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_filesystem_directory_collect_allocated");
  NEXUS_PLATFORM_UNSUPPORTED_RAISE("filesystem_directory_collect_allocated");
}
NexusError *nexus_filesystem_directory_collect_matching_allocated(const NexusPath *directory_path, boolean recursive, const char *regex_pattern,
                                                                  NexusPathList *out_path_list) {
  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_filesystem_directory_collect_matching_allocated");
  NEXUS_PLATFORM_UNSUPPORTED_RAISE("filesystem_directory_collect_matching_allocated");
}
NexusError *nexus_filesystem_special_directory_get(NexusSpecialDirectory directory, NexusPath *out_path) {
  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_filesystem_special_directory_get");
  NEXUS_PLATFORM_UNSUPPORTED_RAISE("filesystem_special_directory_get");
}

#endif
