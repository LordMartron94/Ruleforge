/* <io.h> pulls in memcpy/memset, which nexus.h poisons, so on Windows it comes first. */
#ifdef _WIN32
#  include <io.h>
#endif

#include "../nexus.h"

#if NEXUS_PLATFORM_WINDOWS
#elif NEXUS_PLATFORM_POSIX
#  include <unistd.h>
#endif

static NexusError *n_stdio_stream_write(FILE *stream, const byte *bytes, uint_large byte_length, uint_large *out_bytes_written) {
  size_t written_count;

  NEXUS_PROGRAM_PRECONDITION(bytes != NULL, "stream_write: bytes must not be NULL");
  NEXUS_PROGRAM_PRECONDITION(out_bytes_written != NULL, "stream_write: out_bytes_written must not be NULL");
  NEXUS_PROGRAM_PRECONDITION(byte_length <= (uint_large)((size_t)-1), "stream_write: byte_length exceeds this platform's size_t range");

  written_count      = fwrite(bytes, 1, (size_t)byte_length, stream);
  *out_bytes_written = (uint_large)written_count;

  if (written_count != (size_t)byte_length && ferror(stream) != 0) {
    return NEXUS_PROGRAM_ERROR_FROM_ERRNO("failed to write to stream");
  }

  return &nexus_program_errors_success;
}

static NexusError *n_stdio_stream_write_cstring(FILE *stream, const char *text) {
  uint_large byte_length;
  uint_large bytes_written;

  NEXUS_PROGRAM_PRECONDITION(text != NULL, "stream_write_cstring: text must not be NULL");

  byte_length = nexus_strings_string_length_get(text);

  return n_stdio_stream_write(stream, (const byte *)text, byte_length, &bytes_written);
}

static NexusError *n_stdio_stream_write_formatted(FILE *stream, const char *format, va_list args) {
  char message_buffer[4096];

  NEXUS_PROGRAM_PRECONDITION(format != NULL, "stream_write_formatted: format must not be NULL");

  /* _with_truncation never fails -- it just reports whether it had to truncate -- so there's no
     format-side error branch here, only the write below. */
  (void)nexus_strings_vstring_format_with_truncation(message_buffer, NEXUS_SIZEOF(message_buffer), format, args);

  return n_stdio_stream_write_cstring(stream, message_buffer);
}

static NexusError *n_stdio_stream_flush(FILE *stream) {
  if (fflush(stream) != 0) {
    return NEXUS_PROGRAM_ERROR_FROM_ERRNO("failed to flush stream");
  }

  return &nexus_program_errors_success;
}

NexusError *nexus_stdio_stdout_write(const byte *bytes, uint_large byte_length, uint_large *out_bytes_written) {
  return n_stdio_stream_write(stdout, bytes, byte_length, out_bytes_written);
}

NexusError *nexus_stdio_stdout_write_cstring(const char *text) {
  return n_stdio_stream_write_cstring(stdout, text);
}

NexusError *nexus_stdio_stdout_write_formatted(const char *format, ...) {
  va_list     args;
  NexusError *error;

  va_start(args, format);
  error = n_stdio_stream_write_formatted(stdout, format, args);
  va_end(args);

  return error;
}

NexusError *nexus_stdio_stdout_flush(void) {
  return n_stdio_stream_flush(stdout);
}

NexusError *nexus_stdio_stderr_write(const byte *bytes, uint_large byte_length, uint_large *out_bytes_written) {
  return n_stdio_stream_write(stderr, bytes, byte_length, out_bytes_written);
}

NexusError *nexus_stdio_stderr_write_cstring(const char *text) {
  return n_stdio_stream_write_cstring(stderr, text);
}

NexusError *nexus_stdio_stderr_write_formatted(const char *format, ...) {
  va_list     args;
  NexusError *error;

  va_start(args, format);
  error = n_stdio_stream_write_formatted(stderr, format, args);
  va_end(args);

  return error;
}

NexusError *nexus_stdio_stderr_flush(void) {
  return n_stdio_stream_flush(stderr);
}

NexusError *nexus_stdio_stdin_read_line(char *buffer, uint_large buffer_max_length, boolean *out_reached_eof) {
  uint_large length;

  NEXUS_PROGRAM_PRECONDITION(buffer != NULL, "stdin_read_line: buffer must not be NULL");
  NEXUS_PROGRAM_PRECONDITION(out_reached_eof != NULL, "stdin_read_line: out_reached_eof must not be NULL");
  NEXUS_PROGRAM_PRECONDITION(buffer_max_length > 0, "stdin_read_line: buffer_max_length must be > 0");
  NEXUS_PROGRAM_PRECONDITION(buffer_max_length <= (uint_large)INT32_MAX_VAL,
                             "stdin_read_line: buffer_max_length exceeds fgets's int capacity parameter");

  /* Best-effort: a prompt printed via stdout should appear before this blocks on input. A flush
     failure here shouldn't stop the read itself. */
  (void)nexus_stdio_stdout_flush();

  *out_reached_eof = FALSE;

  if (fgets(buffer, (int)buffer_max_length, stdin) == NULL) {
    if (ferror(stdin) != 0) {
      return NEXUS_PROGRAM_ERROR_FROM_ERRNO("failed to read from stdin");
    }

    *out_reached_eof = TRUE;
    return &nexus_program_errors_success;
  }

  length = nexus_strings_string_length_get(buffer);

  if (length > 0 && buffer[length - 1] == '\n') {
    buffer[length - 1] = '\0';
    length--;
  }

  if (length > 0 && buffer[length - 1] == '\r') {
    buffer[length - 1] = '\0';
  }

  return &nexus_program_errors_success;
}

NexusError *nexus_stdio_stdin_read(byte *buffer, uint_large byte_length, uint_large *out_bytes_read) {
  NEXUS_PROGRAM_PRECONDITION(buffer != NULL || byte_length == 0, "stdin_read: buffer must not be NULL");
  NEXUS_PROGRAM_PRECONDITION(out_bytes_read != NULL, "stdin_read: out_bytes_read must not be NULL");

  (void)nexus_stdio_stdout_flush();

  *out_bytes_read = byte_length == 0 ? 0 : (uint_large)fread(buffer, 1, (size_t)byte_length, stdin);

  if (*out_bytes_read < byte_length && ferror(stdin) != 0) {
    return NEXUS_PROGRAM_ERROR_FROM_ERRNO("failed to read from stdin");
  }

  return &nexus_program_errors_success;
}

boolean nexus_stdio_stdin_is_terminal(void) {
#if NEXUS_PLATFORM_WINDOWS
  return _isatty(_fileno(stdin)) != 0 ? TRUE : FALSE;
#elif NEXUS_PLATFORM_POSIX
  return isatty(STDIN_FILENO) != 0 ? TRUE : FALSE;
#else
  return FALSE;
#endif
}
