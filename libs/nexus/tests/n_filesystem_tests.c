/*
Filesystem behavior that differs between platforms: replacing rename, a directory link that must never be followed, and the pattern
matcher of nexus_filesystem_directory_collect_matching_allocated (the one Nexus carries itself where the C library has none; build this
file with NEXUS_FILESYSTEM_BUILTIN_REGEX to run it on any platform).
*/
#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#endif

#include <nexus.h>

#include <stdio.h>

#if NEXUS_PLATFORM_POSIX
#  include <unistd.h>
#endif

static int nexus_filesystem_test_fail(int code) {
  (void)fprintf(stderr, "Nexus filesystem test failed: %d\n", code);

  return code;
}

static boolean nexus_filesystem_test_ok(NexusError *error) {
  return error->code == GLOBAL_NEXUS_ERROR_SUCCESS;
}

static boolean nexus_filesystem_test_exists(const NexusPath *path) {
  boolean exists;

  exists = FALSE;

  return nexus_filesystem_test_ok(nexus_filesystem_path_exists(path, &exists, FALSE)) && exists;
}

static boolean nexus_filesystem_test_write(const NexusPath *path, const char *text) {
  NexusFileHandle *handle;
  uint_large       written;

  if (!nexus_filesystem_test_ok(nexus_filesystem_file_open(path, NFM_WRITE_BINARY, &handle))) {
    return FALSE;
  }

  if (!nexus_filesystem_test_ok(nexus_filesystem_file_write(handle, (const byte *)text, nexus_strings_string_length_get(text), &written))) {
    (void)nexus_filesystem_file_close(handle);

    return FALSE;
  }

  return nexus_filesystem_test_ok(nexus_filesystem_file_close(handle)) && written == nexus_strings_string_length_get(text);
}

static boolean nexus_filesystem_test_read(const NexusPath *path, char *buffer, uint_large size) {
  NexusFileHandle *handle;
  uint_large       count;
  boolean          eof;

  if (!nexus_filesystem_test_ok(nexus_filesystem_file_open(path, NFM_READ_BINARY, &handle))) {
    return FALSE;
  }

  count = 0;
  eof   = FALSE;
  if (!nexus_filesystem_test_ok(nexus_filesystem_file_read(handle, (byte *)buffer, size - 1, &count, &eof))) {
    (void)nexus_filesystem_file_close(handle);

    return FALSE;
  }

  buffer[count] = '\0';

  return nexus_filesystem_test_ok(nexus_filesystem_file_close(handle));
}

/* A scratch directory of this run, emptied first so a failed earlier run leaves nothing behind. */
static boolean nexus_filesystem_test_scratch(NexusPath *out_root) {
  NexusPath temp;

  if (!nexus_filesystem_test_ok(nexus_filesystem_special_directory_get(NSD_TEMP, &temp))) {
    return FALSE;
  }

  nexus_filesystem_path_join(&temp, "nexus_filesystem_tests");
  *out_root = temp;

  return nexus_filesystem_test_ok(nexus_filesystem_directory_delete(out_root, TRUE)) &&
         nexus_filesystem_test_ok(nexus_filesystem_directory_create(out_root));
}

static int nexus_filesystem_test_rename_replaces(const NexusPath *root) {
  NexusPath source;
  NexusPath destination;
  char      text[16];

  source = *root;
  nexus_filesystem_path_join(&source, "source.txt");
  destination = *root;
  nexus_filesystem_path_join(&destination, "destination.txt");

  if (!nexus_filesystem_test_write(&source, "new") || !nexus_filesystem_test_write(&destination, "old")) {
    return nexus_filesystem_test_fail(100);
  }

  if (!nexus_filesystem_test_ok(nexus_filesystem_path_rename(&source, &destination))) {
    return nexus_filesystem_test_fail(101);
  }

  if (nexus_filesystem_test_exists(&source) || !nexus_filesystem_test_read(&destination, text, NEXUS_SIZEOF(text)) ||
      !nexus_strings_string_equals(text, "new")) {
    return nexus_filesystem_test_fail(102);
  }

  return 0;
}

#define NEXUS_FILESYSTEM_TEST_PATTERN_FILES 9U

static int nexus_filesystem_test_count(const NexusPath *directory, const char *pattern, uint64 expected, int code) {
  NexusPathList list;
  NexusError   *error;
  uint64        count;

  list.paths = NULL;
  list.count = 0;
  error      = nexus_filesystem_directory_collect_matching_allocated(directory, TRUE, pattern, &list);
  if (!nexus_filesystem_test_ok(error)) {
    (void)fprintf(stderr, "pattern %s: %s\n", pattern, error->message == NULL ? "error" : error->message);

    return nexus_filesystem_test_fail(code);
  }

  count = list.count;
  if (list.paths != NULL) {
    nexus_memory_deallocate(list.paths);
  }

  if (count != expected) {
    (void)fprintf(stderr, "pattern %s matched %lu, expected %lu\n", pattern, (unsigned long)count, (unsigned long)expected);

    return nexus_filesystem_test_fail(code);
  }

  return 0;
}

static int nexus_filesystem_test_patterns(const NexusPath *root) {
  static const char *names[NEXUS_FILESYSTEM_TEST_PATTERN_FILES] = {"abc", "abbbc", "ac", "xyz", "a1b22", "AbC", "foo.bar", "x", "aaaa"};
  NexusPath          directory;
  NexusPath          file;
  NexusPathList      list;
  uint32             i;
  int                result;

  directory = *root;
  nexus_filesystem_path_join(&directory, "patterns");
  if (!nexus_filesystem_test_ok(nexus_filesystem_directory_create(&directory))) {
    return nexus_filesystem_test_fail(200);
  }

  for (i = 0; i < NEXUS_FILESYSTEM_TEST_PATTERN_FILES; i++) {
    file = directory;
    nexus_filesystem_path_join(&file, names[i]);
    if (!nexus_filesystem_test_write(&file, "x")) {
      return nexus_filesystem_test_fail(201);
    }
  }

  /* Every pattern is anchored to the last path component, so the scratch folder's own name cannot take part. */
  result = nexus_filesystem_test_count(&directory, "/ab+c$", 2, 210);                            /* abc abbbc */
  result = result != 0 ? result : nexus_filesystem_test_count(&directory, "/a(b|x)?c$", 2, 211); /* abc ac */
  result = result != 0 ? result : nexus_filesystem_test_count(&directory, "/[[:upper:]][a-z]C$", 1, 212);
  result = result != 0 ? result : nexus_filesystem_test_count(&directory, "/a[0-9]{1,2}b[0-9]+$", 1, 213);
  result = result != 0 ? result : nexus_filesystem_test_count(&directory, "/foo\\.bar$", 1, 214);
  result = result != 0 ? result : nexus_filesystem_test_count(&directory, "/[^a-z/][^/]*$", 1, 215); /* AbC */
  result = result != 0 ? result : nexus_filesystem_test_count(&directory, "/a{4}$", 1, 216);         /* aaaa */
  result = result != 0 ? result : nexus_filesystem_test_count(&directory, "/a{3}$", 0, 216);         /* exactly three, from the slash */
  result = result != 0 ? result : nexus_filesystem_test_count(&directory, "/a{2,}$", 1, 217);
  result = result != 0 ? result : nexus_filesystem_test_count(&directory, "/(abc|xyz|x)$", 3, 218);
  result = result != 0 ? result : nexus_filesystem_test_count(&directory, "/[a-c]+$", 4, 219); /* abc abbbc ac aaaa */
  result = result != 0 ? result
                       : nexus_filesystem_test_count(&directory, "^[^/]*/", 9, 220); /* an anchor at the start: every path begins with something */
  result = result != 0 ? result : nexus_filesystem_test_count(&directory, "/nomatch$", 0, 221);
  if (result != 0) {
    return result;
  }

  /* A pattern that is wrong is refused with the invalid-pattern error, never matched against. */
  list.paths = NULL;
  list.count = 0;
  if (nexus_filesystem_test_ok(nexus_filesystem_directory_collect_matching_allocated(&directory, TRUE, "(", &list)) ||
      nexus_filesystem_test_ok(nexus_filesystem_directory_collect_matching_allocated(&directory, TRUE, "a{2,1}", &list)) ||
      nexus_filesystem_test_ok(nexus_filesystem_directory_collect_matching_allocated(&directory, TRUE, "[abc", &list)) ||
      nexus_filesystem_test_ok(nexus_filesystem_directory_collect_matching_allocated(&directory, TRUE, "*a", &list)) ||
      nexus_filesystem_test_ok(nexus_filesystem_directory_collect_matching_allocated(&directory, TRUE, "a\\", &list))) {
    return nexus_filesystem_test_fail(230);
  }

  return 0;
}

/* Makes link a directory link to target; FALSE when the platform or the privileges do not allow it (the test is then skipped). */
static boolean nexus_filesystem_test_link(const char *target, const char *link) {
#if NEXUS_PLATFORM_POSIX
  return symlink(target, link) == 0;
#elif defined(_WIN32)
  /* 0x2 is SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE, 0x1 SYMBOLIC_LINK_FLAG_DIRECTORY. */
  return CreateSymbolicLinkA(link, target, 0x3UL) != 0;
#else
  (void)target;
  (void)link;

  return FALSE;
#endif
}

static int nexus_filesystem_test_links(const NexusPath *root) {
  NexusPath     target;
  NexusPath     keep;
  NexusPath     victim;
  NexusPath     link;
  boolean       is_link;
  NexusPathType type;

  target = *root;
  nexus_filesystem_path_join(&target, "target");
  keep = target;
  nexus_filesystem_path_join(&keep, "keep.txt");
  victim = *root;
  nexus_filesystem_path_join(&victim, "victim");
  link = victim;
  nexus_filesystem_path_join(&link, "link");

  if (!nexus_filesystem_test_ok(nexus_filesystem_directory_create(&target)) || !nexus_filesystem_test_write(&keep, "keep") ||
      !nexus_filesystem_test_ok(nexus_filesystem_directory_create(&victim))) {
    return nexus_filesystem_test_fail(300);
  }

  if (!nexus_filesystem_test_link(target.buffer, link.buffer)) {
    (void)fprintf(stderr, "directory links are not available here; the link test is skipped\n");

    return 0;
  }

  is_link = FALSE;
  type    = NPT_FILE;
  if (!nexus_filesystem_test_ok(nexus_filesystem_path_is_symlink(&link, &is_link)) || !is_link ||
      !nexus_filesystem_test_ok(nexus_filesystem_path_type_get(&link, &type)) || type != NPT_SYMLINK) {
    return nexus_filesystem_test_fail(301);
  }

  /* Deleting the folder that holds the link removes the link and leaves what it points at alone. */
  if (!nexus_filesystem_test_ok(nexus_filesystem_directory_delete(&victim, TRUE))) {
    return nexus_filesystem_test_fail(302);
  }

  if (nexus_filesystem_test_exists(&victim) || !nexus_filesystem_test_exists(&keep)) {
    return nexus_filesystem_test_fail(303);
  }

  return 0;
}

int main(void) {
  NexusPath root;
  int       result;

  if (!nexus_filesystem_test_scratch(&root)) {
    return nexus_filesystem_test_fail(1);
  }

  result = nexus_filesystem_test_rename_replaces(&root);
  result = result != 0 ? result : nexus_filesystem_test_patterns(&root);
  result = result != 0 ? result : nexus_filesystem_test_links(&root);

  (void)nexus_filesystem_directory_delete(&root, TRUE);

  return result;
}
