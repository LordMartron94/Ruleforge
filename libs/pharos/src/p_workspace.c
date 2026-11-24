#include <nexus.h>

#include "./p_internal.h"

/* Folders that hold copies, dependencies or tool state rather than the user's sources are not entered (the setting names them). */
static boolean p_workspace_folder_ignored(const PharosServer *server, const char *name) {
  uint_large i;

  for (i = 0; i < server->settings.ignore_count; i++) {
    if (nexus_strings_string_equals(name, server->settings.ignore[i])) {
      return TRUE;
    }
  }

  return FALSE;
}

typedef struct PWalk {
  PharosServer  *server;
  NexusDataList *paths;
  NexusDataList *folders; /* char *, found below the folder being read */
  uint_large     files;
} PWalk;

static NexusError *p_workspace_visit(const NexusPath *entry_path, NexusPathType entry_type, void *user_data) {
  PWalk      *walk;
  const char *name;
  const char *slash;

  walk  = (PWalk *)user_data;
  slash = entry_path->buffer;
  name  = slash;

  for (; *slash != '\0'; slash++) {
    if (*slash == '/') {
      name = slash + 1;
    }
  }

  if (entry_type == NPT_DIRECTORY) {
    if (!p_workspace_folder_ignored(walk->server, name) && name[0] != '.') {
      (void)nexus_data_list_append_pointer(walk->folders, pharos_paths_text_copy(entry_path->buffer));
    }
  } else if (entry_type == NPT_FILE && walk->files < walk->server->settings.values.walk_max_files) {
    boolean wanted;

    wanted =
        walk->server->provider.workspace_file != NULL ? walk->server->provider.workspace_file(walk->server->provider.user, entry_path->buffer) : TRUE;

    if (wanted) {
      char *path;

      path = pharos_paths_text_copy(entry_path->buffer);
      pharos_paths_path_normalize(path);
      (void)nexus_data_list_append_pointer(walk->paths, path);
      walk->files++;
    }
  }

  return NEXUS_PROGRAM_ERROR_SUCCESS();
}

static void p_workspace_folder_scan(PWalk *walk, const char *folder, uint32 depth) {
  NexusPath      directory;
  NexusDataList *below;
  uint_large     i;

  if (depth > walk->server->settings.values.walk_max_depth) {
    return;
  }

  below         = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 8);
  walk->folders = below;
  directory     = nexus_filesystem_path_create(folder);

  if (nexus_filesystem_directory_walk(&directory, FALSE, p_workspace_visit, walk) == NEXUS_PROGRAM_ERROR_SUCCESS()) {
    for (i = 0; i < nexus_data_list_length_get(below); i++) {
      NexusDataList *saved;

      saved = below;
      p_workspace_folder_scan(walk, (const char *)nexus_data_list_pointer_at(below, i), depth + 1);
      walk->folders = saved;
    }
  }

  for (i = 0; i < nexus_data_list_length_get(below); i++) {
    nexus_memory_deallocate(nexus_data_list_pointer_at(below, i));
  }

  nexus_data_list_destroy(below);
}

/* Summary: the walk, on its own thread: it only reads the file system and the provider's read-only answer about file names. */
static void p_workspace_thread(void *user) {
  PharosServer *server;
  PWalk         walk;
  uint_large    i;

  server       = (PharosServer *)user;
  walk.server  = server;
  walk.paths   = server->scan_paths;
  walk.files   = 0;
  walk.folders = NULL;

  if (!server->settings.values.walk) {
    return;
  }

  for (i = 0; i < nexus_data_list_length_get(server->folders); i++) {
    p_workspace_folder_scan(&walk, (const char *)nexus_data_list_pointer_at(server->folders, i), 0);
  }

  for (i = 0; i < nexus_data_list_length_get(server->roots); i++) {
    p_workspace_folder_scan(&walk, (const char *)nexus_data_list_pointer_at(server->roots, i), 0);
  }
}

void pharos_workspace_start(PharosServer *server) {
  if (server->scan_started) {
    return;
  }

  server->scan_started = TRUE;
  server->scan_paths   = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 256);

  if (nexus_threads_create(&server->scan_thread, p_workspace_thread, server) != NEXUS_PROGRAM_ERROR_SUCCESS()) {
    /* No thread: the walk happens when the files are first wanted. */
    server->scan_thread = NULL;
  }
}

void pharos_workspace_scan(PharosServer *server) {
  uint_large i;

  if (server->scanned) {
    return;
  }

  pharos_workspace_start(server);

  if (server->scan_thread != NULL) {
    (void)nexus_threads_join(server->scan_thread);
    server->scan_thread = NULL;
  } else {
    p_workspace_thread(server);
  }

  for (i = 0; i < nexus_data_list_length_get(server->scan_paths); i++) {
    char *path;

    path = (char *)nexus_data_list_pointer_at(server->scan_paths, i);
    (void)pharos_documents_file_add(server, path);
    nexus_memory_deallocate(path);
  }

  nexus_data_list_destroy(server->scan_paths);
  server->scan_paths = NULL;
  server->scanned    = TRUE;
}
