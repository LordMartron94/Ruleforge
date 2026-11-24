#include <nexus.h>

#include "./p_internal.h"

/* Folders that hold copies, dependencies or tool state rather than the user's sources are not entered. */
static const char *const p_settings_ignored_default[] = {"build", ".git", "external", "node_modules", "results", ".cache",
                                                         ".hg",   ".svn", "target",   "vendor",       "legacy"};

void pharos_settings_initialize(PharosSettings *settings) {
  settings->walk                = TRUE;
  settings->walk_max_files      = 20000;
  settings->walk_max_depth      = 24;
  settings->ignore              = p_settings_ignored_default;
  settings->ignore_count        = NEXUS_ARRAY_SIZE_ELEMENTS(p_settings_ignored_default);
  settings->cache               = TRUE;
  settings->cache_directory     = NULL;
  settings->cache_max_megabytes = 128;
  settings->text_prefilter      = TRUE;
}

void pharos_settings_release(PharosOwnedSettings *owned) {
  uint_large i;

  if (owned->cache_directory != NULL) {
    nexus_memory_deallocate(owned->cache_directory);
  }

  for (i = 0; i < owned->ignore_count; i++) {
    nexus_memory_deallocate(owned->ignore[i]);
  }

  if (owned->ignore != NULL) {
    nexus_memory_deallocate((void *)owned->ignore);
  }

  owned->cache_directory = NULL;
  owned->ignore          = NULL;
  owned->ignore_count    = 0;
}

void pharos_settings_apply(PharosServer *server, const PharosSettings *settings) {
  PharosOwnedSettings fresh;
  uint_large          i;

  /* The new settings may point into the old ones (a provider changes a copy of them), so they are copied before the old are released. */
  fresh.values                 = *settings;
  fresh.cache_directory        = settings->cache_directory != NULL ? pharos_paths_text_copy(settings->cache_directory) : NULL;
  fresh.values.cache_directory = fresh.cache_directory;
  fresh.ignore_count           = settings->ignore_count;
  fresh.ignore                 = (char **)nexus_memory_allocate(((uint_large)settings->ignore_count + 1) * NEXUS_EFFECTIVE_POINTER_SIZE_BYTES);

  for (i = 0; i < settings->ignore_count; i++) {
    fresh.ignore[i] = pharos_paths_text_copy(settings->ignore[i]);
  }

  fresh.values.ignore = (const char *const *)fresh.ignore;
  pharos_settings_release(&server->settings);
  server->settings = fresh;
}

void pharos_settings_default_apply(PharosServer *server) {
  PharosSettings settings;

  pharos_settings_initialize(&settings);
  pharos_settings_apply(server, &settings);
}

void pharos_server_settings_set(PharosServer *server, const PharosSettings *settings) {
  pharos_settings_apply(server, settings);
}
