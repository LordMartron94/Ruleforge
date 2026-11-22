#include <nexus.h>

#include "./p_internal.h"

const PharosName *pharos_names_name_at(const PharosDocument *document, uint_large offset) {
  uint_large i;

  for (i = 0; i < nexus_data_list_length_get(document->names); i++) {
    const PharosName *name;

    name = (const PharosName *)nexus_data_list_at_const(document->names, i);

    if (offset >= name->start && offset <= name->end) {
      return name;
    }
  }

  return NULL;
}

static boolean p_names_text_equal(const char *left, const char *right) {
  if (left == NULL || right == NULL) {
    return left == right;
  }

  return nexus_strings_string_equals(left, right);
}

boolean pharos_names_same(const PharosServer *server, const PharosName *left, const PharosName *right) {
  if (left->kind != right->kind || left->owner == NULL || right->owner == NULL || !nexus_strings_string_equals(left->owner, right->owner) ||
      !p_names_text_equal(left->scope, right->scope)) {
    return FALSE;
  }

  /* A kind whose occurrences may be spelled differently (an imported unit by its file name) is one thing whatever its text. */
  if (server->provider.kind_text != NULL && !server->provider.kind_text(server->provider.user, left->kind)) {
    return TRUE;
  }

  return nexus_strings_string_equals(left->name, right->name);
}

void pharos_names_occurrences_visit(PharosServer *server, const PharosName *name, boolean everything, PharosNamesVisitCallback *callback,
                                    void *user) {
  uint_large i;
  boolean    by_text;

  if (everything) {
    pharos_workspace_scan(server);
  }

  /* The declaring file is read first, wherever it is. */
  if (name->owner != NULL && name->owner[0] == '/') {
    PharosDocument *owner;

    owner = pharos_documents_load(server, name->owner);

    if (owner != NULL) {
      (void)pharos_documents_ensure(server, owner);
    }
  }

  by_text = server->provider.kind_text == NULL || server->provider.kind_text(server->provider.user, name->kind);

  /* Visiting loads no further documents, so the list is stable. */
  for (i = 0; i < nexus_data_list_length_get(server->documents); i++) {
    PharosDocument *document;
    uint_large      index;

    document = (PharosDocument *)nexus_data_list_pointer_at(server->documents, i);

    if (!document->open) {
      boolean owns;

      owns = name->owner != NULL && nexus_strings_string_equals(document->path, name->owner);

      if (!everything && !document->analyzed && !owns) {
        continue;
      }

      /* The cache can answer without the file being read: its names are those of the unchanged file on disk. */
      if (!document->analyzed && !owns && pharos_cache_attach(server, document)) {
        for (index = 0; index < nexus_data_list_length_get(document->names); index++) {
          const PharosName *candidate;

          candidate = (const PharosName *)nexus_data_list_at_const(document->names, index);

          if (pharos_names_same(server, name, candidate)) {
            /* A position needs the text of the file. */
            (void)pharos_documents_text_ensure(server, document);
            callback(document, candidate, user);
          }
        }

        continue;
      }

      if (!pharos_documents_text_ensure(server, document)) {
        continue;
      }

      /* A file that does not contain the text cannot mention the name: it is not even analysed. */
      if (!document->analyzed && by_text && !owns && server->settings.values.text_prefilter &&
          !nexus_strings_string_contains(document->text, name->name)) {
        continue;
      }
    }

    if (!pharos_documents_ensure(server, document)) {
      continue;
    }

    for (index = 0; index < nexus_data_list_length_get(document->names); index++) {
      const PharosName *candidate;

      candidate = (const PharosName *)nexus_data_list_at_const(document->names, index);

      if (pharos_names_same(server, name, candidate)) {
        callback(document, candidate, user);
      }
    }
  }
}

void pharos_names_mentions_visit(PharosServer *server, const PharosName *name, PharosNamesVisitCallback *callback, void *user) {
  PharosDocument *document;
  uint_large      index;

  if (name->owner == NULL || name->owner[0] != '/' || server->provider.mention == NULL) {
    return;
  }

  document = pharos_documents_load(server, name->owner);

  if (document == NULL || !pharos_documents_ensure(server, document)) {
    return;
  }

  for (index = 0; index < nexus_data_list_length_get(document->names); index++) {
    const PharosName *candidate;

    candidate = (const PharosName *)nexus_data_list_at_const(document->names, index);

    if (server->provider.mention(server->provider.user, name, candidate)) {
      callback(document, candidate, user);
    }
  }
}
