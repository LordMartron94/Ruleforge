#include <nexus.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#if NEXUS_PLATFORM_POSIX
#  include <unistd.h>
#endif

#define N_MAPPING_TEST_CHECK(condition)                                                                                                              \
  do {                                                                                                                                               \
    if (!(condition)) {                                                                                                                              \
      (void)fprintf(stderr, "mapping test failed at line %d: %s\n", __LINE__, #condition);                                                           \
      goto cleanup;                                                                                                                                  \
    }                                                                                                                                                \
  } while (0)

#define N_MAPPING_TEST_OK(call) N_MAPPING_TEST_CHECK((call)->code == GLOBAL_NEXUS_ERROR_SUCCESS)

int main(void) {
#if NEXUS_PLATFORM_POSIX
  char             temporary_path[] = "/tmp/nexus-mapping-XXXXXX";
  NexusPath        path;
  NexusFileHandle *file = NULL;
  NexusError      *error;
  void            *mapping        = NULL;
  void            *second_mapping = NULL;
  void            *rejected;
  uint_large       page_size;
  uint_large       requested_size;
  uint_large       mapped_size = 0;
  uint_large       second_size = 0;
  uint_large       rejected_size;
  uint_large       file_size;
  uint_large       bytes_read;
  uint_large       bytes_written;
  byte             buffer[3];
  const byte       prefix[] = {17U, 23U, 31U};
  boolean          reached_eof;
  boolean          temporary_created = FALSE;
  int              descriptor;
  int              result = 1;

  page_size      = nexus_memory_page_size_get();
  requested_size = page_size + 3U;

  N_MAPPING_TEST_OK(nexus_memory_map(NULL, requested_size, NMP_READ_WRITE, NMS_PRIVATE, &mapping, &mapped_size));
  N_MAPPING_TEST_CHECK(mapped_size == requested_size && (uintptr_t)mapping % page_size == 0U);
  N_MAPPING_TEST_CHECK(((byte *)mapping)[0] == 0U && ((byte *)mapping)[requested_size - 1U] == 0U);
  ((byte *)mapping)[requested_size - 1U] = 71U;
  N_MAPPING_TEST_OK(nexus_memory_protect(mapping, mapped_size, NMP_READ));
  N_MAPPING_TEST_CHECK(((byte *)mapping)[requested_size - 1U] == 71U);
  N_MAPPING_TEST_OK(nexus_memory_protect(mapping, mapped_size, NMP_READ_WRITE));
  error = nexus_memory_protect((byte *)mapping + 1U, 1U, NMP_READ);
  N_MAPPING_TEST_CHECK(error->code != GLOBAL_NEXUS_ERROR_SUCCESS);
  error = nexus_memory_unmap((byte *)mapping + 1U, 1U);
  N_MAPPING_TEST_CHECK(error->code != GLOBAL_NEXUS_ERROR_SUCCESS);
  N_MAPPING_TEST_OK(nexus_memory_unmap(mapping, mapped_size));
  mapping = NULL;

  descriptor = mkstemp(temporary_path);
  N_MAPPING_TEST_CHECK(descriptor >= 0);
  temporary_created = TRUE;
  N_MAPPING_TEST_CHECK(close(descriptor) == 0);
  path = nexus_filesystem_path_create(temporary_path);
  N_MAPPING_TEST_OK(nexus_filesystem_file_open(&path, NFM_WRITE_BINARY_PLUS, &file));
  N_MAPPING_TEST_OK(nexus_filesystem_file_size_get(file, &file_size));
  N_MAPPING_TEST_CHECK(file_size == 0U);

  /* A truncated file must fail before any access could trigger SIGBUS. */
  rejected      = &buffer[0];
  rejected_size = 99U;
  error         = nexus_memory_map(file, requested_size, NMP_READ_WRITE, NMS_SHARED, &rejected, &rejected_size);
  N_MAPPING_TEST_CHECK(error->code == NEXUS_FILESYSTEM_ERROR_FILE_TOO_SMALL && rejected == NULL && rejected_size == 0U);
  N_MAPPING_TEST_OK(nexus_filesystem_file_write(file, prefix, NEXUS_SIZEOF(prefix), &bytes_written));
  N_MAPPING_TEST_CHECK(bytes_written == NEXUS_SIZEOF(prefix));
  /* Resizing must flush the buffered prefix without extending to a page boundary. */
  N_MAPPING_TEST_OK(nexus_filesystem_file_resize(file, requested_size));
  N_MAPPING_TEST_OK(nexus_filesystem_file_size_get(file, &file_size));
  N_MAPPING_TEST_CHECK(file_size == requested_size);
  N_MAPPING_TEST_OK(nexus_filesystem_file_read(file, buffer, 1U, &bytes_read, &reached_eof));
  N_MAPPING_TEST_CHECK(bytes_read == 1U && buffer[0] == 0U);
  N_MAPPING_TEST_OK(nexus_filesystem_file_size_get(file, &file_size));
  N_MAPPING_TEST_OK(nexus_filesystem_file_read(file, buffer, 1U, &bytes_read, &reached_eof));
  N_MAPPING_TEST_CHECK(bytes_read == 1U && buffer[0] == 0U);

  /* Mapping starts at file offset zero, independently of the stream position. */
  N_MAPPING_TEST_OK(nexus_memory_map(file, requested_size, NMP_READ_WRITE, NMS_SHARED, &mapping, &mapped_size));
  N_MAPPING_TEST_CHECK(mapped_size == requested_size && ((byte *)mapping)[0] == prefix[0] && ((byte *)mapping)[2] == prefix[2] &&
                       ((byte *)mapping)[requested_size - 1U] == 0U);
  N_MAPPING_TEST_OK(nexus_memory_map(file, requested_size, NMP_READ, NMS_SHARED, &second_mapping, &second_size));
  ((byte *)mapping)[requested_size - 1U] = 93U;
  N_MAPPING_TEST_CHECK(((byte *)second_mapping)[requested_size - 1U] == 93U);
  N_MAPPING_TEST_OK(nexus_memory_unmap(second_mapping, second_size));
  second_mapping = NULL;
  N_MAPPING_TEST_OK(nexus_memory_unmap(mapping, mapped_size));
  mapping = NULL;
  N_MAPPING_TEST_OK(nexus_filesystem_file_close(file));
  file = NULL;

  N_MAPPING_TEST_OK(nexus_filesystem_file_open(&path, NFM_READ_BINARY_PLUS, &file));
  N_MAPPING_TEST_OK(nexus_filesystem_file_size_get(file, &file_size));
  N_MAPPING_TEST_CHECK(file_size == requested_size);
  N_MAPPING_TEST_OK(nexus_filesystem_file_read_at(file, buffer, 1U, (uint32)(requested_size - 1U), &bytes_read));
  N_MAPPING_TEST_CHECK(bytes_read == 1U && buffer[0] == 93U);
  /* Even an overrun within the last page is rejected; mapping never grows the file. */
  error = nexus_memory_map(file, requested_size + 1U, NMP_READ_WRITE, NMS_SHARED, &rejected, &rejected_size);
  N_MAPPING_TEST_CHECK(error->code == NEXUS_FILESYSTEM_ERROR_FILE_TOO_SMALL && rejected == NULL && rejected_size == 0U);
  error = nexus_memory_map(file, requested_size + page_size, NMP_READ, NMS_PRIVATE, &rejected, &rejected_size);
  N_MAPPING_TEST_CHECK(error->code == NEXUS_FILESYSTEM_ERROR_FILE_TOO_SMALL);
  N_MAPPING_TEST_OK(nexus_filesystem_file_size_get(file, &file_size));
  N_MAPPING_TEST_CHECK(file_size == requested_size);
  error = nexus_filesystem_file_resize(file, UINT_LARGE_MAX_VAL);
  N_MAPPING_TEST_CHECK(error->code == NEXUS_FILESYSTEM_ERROR_FILE_SIZE_OUT_OF_RANGE);
  N_MAPPING_TEST_OK(nexus_filesystem_file_size_get(file, &file_size));
  N_MAPPING_TEST_CHECK(file_size == requested_size);

  /* Shrink after a buffered read: stale read-ahead must not survive. */
  N_MAPPING_TEST_OK(nexus_filesystem_file_read_at(file, buffer, 1U, 0U, &bytes_read));
  N_MAPPING_TEST_OK(nexus_filesystem_file_resize(file, 1U));
  N_MAPPING_TEST_OK(nexus_filesystem_file_read(file, buffer, 1U, &bytes_read, &reached_eof));
  N_MAPPING_TEST_CHECK(bytes_read == 0U && reached_eof);
  N_MAPPING_TEST_OK(nexus_filesystem_file_resize(file, 3U));
  N_MAPPING_TEST_OK(nexus_filesystem_file_read_at(file, buffer, 3U, 0U, &bytes_read));
  N_MAPPING_TEST_CHECK(bytes_read == 3U && buffer[0] == prefix[0] && buffer[1] == 0U && buffer[2] == 0U);
  N_MAPPING_TEST_OK(nexus_filesystem_file_resize(file, 0U));
  N_MAPPING_TEST_OK(nexus_filesystem_file_size_get(file, &file_size));
  N_MAPPING_TEST_CHECK(file_size == 0U);
  N_MAPPING_TEST_OK(nexus_filesystem_file_resize(file, requested_size));
  N_MAPPING_TEST_OK(nexus_filesystem_file_close(file));
  file = NULL;

  N_MAPPING_TEST_OK(nexus_filesystem_file_open(&path, NFM_READ_BINARY, &file));
  error = nexus_filesystem_file_resize(file, requested_size + 1U);
  N_MAPPING_TEST_CHECK(error->code != GLOBAL_NEXUS_ERROR_SUCCESS);
  N_MAPPING_TEST_OK(nexus_filesystem_file_size_get(file, &file_size));
  N_MAPPING_TEST_CHECK(file_size == requested_size);
  N_MAPPING_TEST_OK(nexus_memory_map(file, requested_size, NMP_READ_WRITE, NMS_PRIVATE, &mapping, &mapped_size));
  ((byte *)mapping)[0] = 47U;
  N_MAPPING_TEST_OK(nexus_memory_unmap(mapping, mapped_size));
  mapping = NULL;
  N_MAPPING_TEST_OK(nexus_filesystem_file_read_at(file, buffer, 1U, 0U, &bytes_read));
  N_MAPPING_TEST_CHECK(bytes_read == 1U && buffer[0] == 0U);
  error = nexus_memory_map(file, requested_size, NMP_READ_WRITE, NMS_SHARED, &rejected, &rejected_size);
  N_MAPPING_TEST_CHECK(error->code != GLOBAL_NEXUS_ERROR_SUCCESS && rejected == NULL && rejected_size == 0U);
  N_MAPPING_TEST_OK(nexus_filesystem_file_close(file));
  file = NULL;

  N_MAPPING_TEST_OK(nexus_filesystem_file_open(&path, NFM_WRITE_BINARY_PLUS, &file));
  N_MAPPING_TEST_OK(nexus_filesystem_file_size_get(file, &file_size));
  N_MAPPING_TEST_CHECK(file_size == 0U);
  N_MAPPING_TEST_OK(nexus_filesystem_file_close(file));
  file = NULL;

#  if NEXUS_PLATFORM_LINUX
  error = nexus_filesystem_file_anonymous_create(UINT_LARGE_MAX_VAL, &file);
  N_MAPPING_TEST_CHECK(error->code == NEXUS_FILESYSTEM_ERROR_FILE_SIZE_OUT_OF_RANGE && file == NULL);
  N_MAPPING_TEST_OK(nexus_filesystem_file_anonymous_create(1U, &file));
  error = nexus_memory_map(file, requested_size, NMP_READ_WRITE, NMS_SHARED, &rejected, &rejected_size);
  N_MAPPING_TEST_CHECK(error->code == NEXUS_FILESYSTEM_ERROR_FILE_TOO_SMALL);
  N_MAPPING_TEST_OK(nexus_filesystem_file_resize(file, requested_size));
  N_MAPPING_TEST_OK(nexus_memory_map(file, requested_size, NMP_READ_WRITE, NMS_SHARED, &mapping, &mapped_size));
  N_MAPPING_TEST_OK(nexus_filesystem_file_close(file));
  file = NULL;
  /* The mapping retains its backing file after the caller closes the handle. */
  ((byte *)mapping)[requested_size - 1U] = 29U;
  N_MAPPING_TEST_CHECK(((byte *)mapping)[requested_size - 1U] == 29U);
  N_MAPPING_TEST_OK(nexus_memory_unmap(mapping, mapped_size));
  mapping = NULL;
#  endif
  result = 0;

cleanup:
  if (second_mapping != NULL)
    (void)nexus_memory_unmap(second_mapping, second_size);
  if (mapping != NULL)
    (void)nexus_memory_unmap(mapping, mapped_size);
  if (file != NULL)
    (void)nexus_filesystem_file_close(file);
  if (temporary_created)
    (void)remove(temporary_path);
  return result;
#else
  return 77;
#endif
}
