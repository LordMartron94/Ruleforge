#include "../nexus.h"

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

#if NEXUS_PLATFORM_POSIX
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

#else

/* MinGW has no regex.h; matching by pattern is the one directory operation Windows does not offer yet. */
NexusError *nexus_filesystem_directory_collect_matching_allocated(const NexusPath *directory_path, boolean recursive, const char *regex_pattern,
                                                                  NexusPathList *out_path_list) {
  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_filesystem_directory_collect_matching_allocated");
  (void)directory_path;
  (void)recursive;
  (void)regex_pattern;
  (void)out_path_list;
  NEXUS_PLATFORM_UNSUPPORTED_RAISE("filesystem_directory_collect_matching_allocated");
}

#endif

/* ---------------------------------------------------------------------------- */
/* SPECIAL DIRECTORIES                                                          */
/* ---------------------------------------------------------------------------- */

NexusError *nexus_filesystem_special_directory_get(NexusSpecialDirectory directory, NexusPath *out_path) {
  const char    *value;
#if NEXUS_PLATFORM_POSIX
  struct passwd *password_entry;
#endif
  NexusPath      home_path;
  NexusError    *result;

  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_filesystem_special_directory_get");

  switch (directory) {
  case NSD_HOME:
    value = getenv("HOME");
    if (value != NULL && value[0] != '\0') {
      *out_path = nexus_filesystem_path_create(value);
      result    = &nexus_program_errors_success;
      break;
    }

#if NEXUS_PLATFORM_POSIX
    password_entry = getpwuid(getuid());
    if (password_entry != NULL && password_entry->pw_dir != NULL) {
      *out_path = nexus_filesystem_path_create(password_entry->pw_dir);
      result    = &nexus_program_errors_success;
      break;
    }
#else
    value = getenv("USERPROFILE");
    if (value != NULL && value[0] != '\0') {
      *out_path = nexus_filesystem_path_create(value);
      result    = &nexus_program_errors_success;
      break;
    }
#endif

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
