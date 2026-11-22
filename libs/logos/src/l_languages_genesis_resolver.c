#include <nexus.h>

#include "../logos.h"
#include "./l_internal.h"

/* Summary: the items a library offers to import, as written in IMPORT: "$name", "#name" and enumeration names. */
static void l_genesis_resolver_offers(const LGUnit *unit, NexusDataList *out) {
  uint32     i;
  char      *text;
  uint_large length;

  for (i = 0; i < unit->decl_count; i++) {
    const LGDecl *decl;

    decl = unit->decls[i];

    if (decl->kind == LGD_TEMPLATE) {
      length  = nexus_strings_string_length_get(decl->name);
      text    = (char *)nexus_memory_allocate(length + 2);
      text[0] = decl->sigil;
      nexus_memory_bytes_copy(decl->name, text + 1, length + 1);
      (void)nexus_data_list_append_pointer(out, text);
    } else if (decl->kind == LGD_ENUM) {
      length = nexus_strings_string_length_get(decl->name);
      text   = (char *)nexus_memory_allocate(length + 1);
      nexus_memory_bytes_copy(decl->name, text, length + 1);
      (void)nexus_data_list_append_pointer(out, text);
    }
  }
}

static void l_genesis_resolver_offers_destroy(NexusDataList *offers) {
  uint_large i;

  for (i = 0; i < nexus_data_list_length_get(offers); i++) {
    nexus_memory_deallocate(nexus_data_list_pointer_at(offers, i));
  }

  nexus_data_list_destroy(offers);
}

static char *l_genesis_resolver_copy(const char *text) {
  uint_large length;
  char      *copy;

  length = nexus_strings_string_length_get(text);
  copy   = (char *)nexus_memory_allocate(length + 1);
  nexus_memory_bytes_copy(text, copy, length + 1);

  return copy;
}

boolean logos_languages_genesis_resolver_imports_resolve(LGenesisCompiler *compiler, LGUnit *unit, NexusDataList *out_imports) {
  LGRegistryEntry  *entry;
  LGImport         *import;
  const LGImport   *earlier;
  const LGDecl     *decl;
  PharosDiagnostic *diagnostic;
  NexusDataList    *offers;
  const char       *suggestion;
  const char       *label;
  PharosSpan        name_span;
  uint32            i;
  uint32            j;
  uint_large        k;
  boolean           ok;
  boolean           found;
  boolean           duplicate;

  ok = TRUE;

  for (i = 0; i < unit->decl_count; i++) {
    decl = unit->decls[i];

    if (decl->kind != LGD_IMPORT) {
      continue;
    }

    name_span       = decl->span;
    name_span.width = (uint32)nexus_strings_string_length_get(decl->name);
    name_span.offset += decl->span.width - name_span.width;
    label     = decl->alias != NULL ? decl->alias : decl->name;
    duplicate = FALSE;

    for (k = 0; k < nexus_data_list_length_get(out_imports); k++) {
      earlier = (const LGImport *)nexus_data_list_pointer_at(out_imports, k);

      if (nexus_strings_string_equals(earlier->alias != NULL ? earlier->alias : earlier->name, label)) {
        diagnostic = pharos_diagnostics_report((compiler)->reporting, PDS_ERROR, "G0103", decl->span, "`%s` is imported twice", label);
        pharos_diagnostics_label_add(diagnostic, decl->span, TRUE, "imported again here");
        pharos_diagnostics_label_add(diagnostic, earlier->span, FALSE, "first imported here");
        pharos_diagnostics_help_add(diagnostic, "give one of them a different name with `IMPORT %s AS Other { ... }`", decl->name);
        ok        = FALSE;
        duplicate = TRUE;
        break;
      }
    }

    if (duplicate) {
      continue;
    }

    entry = logos_languages_genesis_compiler_import_resolve(compiler, decl->name, name_span);

    if (entry == NULL) {
      ok = FALSE;
      continue;
    }

    import             = (LGImport *)nexus_memory_allocate(NEXUS_SIZEOF(*import));
    import->name       = l_genesis_resolver_copy(decl->name);
    import->alias      = decl->alias != NULL ? l_genesis_resolver_copy(decl->alias) : NULL;
    import->entry      = entry;
    import->span       = decl->span;
    import->item_count = decl->name_count;
    import->items      = (const char **)nexus_memory_allocate((decl->name_count > 0 ? decl->name_count : 1) * NEXUS_EFFECTIVE_POINTER_SIZE_BYTES);

    for (j = 0; j < decl->name_count; j++) {
      import->items[j] = decl->names[j];
    }

    (void)nexus_data_list_append_pointer(out_imports, import);

    /* Every imported item must exist in the source it is imported from. */
    for (j = 0; j < decl->name_count; j++) {
      found = FALSE;

      if (entry->kind == LGUK_LIBRARY) {
        const LGUnit *library;
        uint32        item_index;

        library = entry->unit;

        for (item_index = 0; library != NULL && item_index < library->decl_count && !found; item_index++) {
          const LGDecl *offered;

          offered = library->decls[item_index];

          if (offered->kind == LGD_TEMPLATE) {
            found = decl->names[j][0] == offered->sigil && nexus_strings_string_equals(decl->names[j] + 1, offered->name);
          } else if (offered->kind == LGD_ENUM) {
            found = nexus_strings_string_equals(decl->names[j], offered->name);
          }
        }
      } else if (entry->engine != NULL && entry->engine->contexta != NULL && decl->names[j][0] == '#') {
        uint_large rule_index;

        for (rule_index = 0; rule_index < logos_languages_contexta_contexta_rule_count_get(entry->engine->contexta) && !found; rule_index++) {
          const char *rule_label;

          rule_label =
              logos_languages_contexta_contexta_rule_label_get(logos_languages_contexta_contexta_rule_at(entry->engine->contexta, rule_index));
          found = rule_label != NULL && nexus_strings_string_equals(decl->names[j] + 1, rule_label);
        }
      }

      if (found) {
        continue;
      }

      ok     = FALSE;
      offers = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 8);

      if (entry->kind == LGUK_LIBRARY && entry->unit != NULL) {
        l_genesis_resolver_offers(entry->unit, offers);
      } else if (entry->engine != NULL && entry->engine->contexta != NULL) {
        uint_large rule_index;

        for (rule_index = 0; rule_index < logos_languages_contexta_contexta_rule_count_get(entry->engine->contexta); rule_index++) {
          const char *rule_label;
          char       *text;
          uint_large  length;

          rule_label =
              logos_languages_contexta_contexta_rule_label_get(logos_languages_contexta_contexta_rule_at(entry->engine->contexta, rule_index));

          if (rule_label == NULL) {
            continue;
          }

          length  = nexus_strings_string_length_get(rule_label);
          text    = (char *)nexus_memory_allocate(length + 2);
          text[0] = '#';
          nexus_memory_bytes_copy(rule_label, text + 1, length + 1);
          (void)nexus_data_list_append_pointer(offers, text);
        }
      }

      diagnostic = pharos_diagnostics_report((compiler)->reporting, PDS_ERROR, "G0104", decl->name_spans[j], "`%s` is not provided by `%s`",
                                             decl->names[j], decl->name);
      pharos_diagnostics_label_add(diagnostic, decl->name_spans[j], TRUE, "no such item in `%s`", decl->name);
      suggestion = nexus_strings_string_closest(decl->names[j], (const char *const *)nexus_data_list_data_get_const(offers),
                                                nexus_data_list_length_get(offers));

      if (suggestion != NULL) {
        pharos_diagnostics_help_add(diagnostic, "did you mean `%s`?", suggestion);
      } else if (nexus_data_list_length_get(offers) > 0) {
        pharos_diagnostics_note_add(diagnostic, "`%s` offers %u item%s; the first is `%s`", decl->name, (unsigned)nexus_data_list_length_get(offers),
                                    nexus_data_list_length_get(offers) == 1 ? "" : "s", (const char *)nexus_data_list_pointer_at(offers, 0));
      }

      l_genesis_resolver_offers_destroy(offers);
    }
  }

  return ok;
}
