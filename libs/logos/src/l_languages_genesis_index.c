#include <nexus.h>

#include "../logos.h"
#include "./l_internal.h"

/*
The symbol index of a Genesis source for editors: every name with the range it occupies and the declaration it means,
following imports into the files they come from. It reads the syntax tree only; nothing is expanded or lowered, so it
works on sources that do not compile and costs about a parse.
*/

struct LGenesisIndex {
  NexusDataArena  arena;
  char           *unit_name;
  LGenesisSymbol *symbols;
  uint_large      count;
};

/* What the index knows of an IMPORT: where it points, and its syntax tree (only that, nothing is compiled). */
typedef struct LGIndexImport {
  const char   *name;
  const char   *alias;
  const char   *path;
  const LGUnit *unit;
  const char  **items;
  uint32        item_count;
} LGIndexImport;

typedef struct LGIndexBuild {
  LGenesisCompiler *compiler;
  NexusDataArena   *arena;
  const LGUnit     *unit;
  const char       *path;
  const byte       *text;
  uint_large        size;
  NexusDataList    *symbols;    /* LGenesisSymbol */
  NexusDataList    *qualifiers; /* const char *, parallel: the alias or enumeration written before a name, or NULL */
  NexusDataList    *imports;    /* LGIndexImport * */
} LGIndexBuild;

/* ---------------------------------------------------------------------------- */
/* LOCATING NAMES IN THE TEXT                                                    */
/* ---------------------------------------------------------------------------- */

static boolean l_index_name_byte(byte value) {
  return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') || (value >= '0' && value <= '9') || value == '_';
}

/* Summary: the offset of the first whole-word occurrence of a name within [from, limit); `limit` itself when there is none. */
static uint_large l_index_find(const LGIndexBuild *build, uint_large from, uint_large limit, const char *name) {
  uint_large length;
  uint_large i;

  length = nexus_strings_string_length_get(name);

  if (limit > build->size) {
    limit = build->size;
  }

  for (i = from; i + length <= limit; i++) {
    if (nexus_memory_bytes_equal(build->text + i, name, length) && (i == 0 || !l_index_name_byte(build->text[i - 1])) &&
        (i + length >= build->size || !l_index_name_byte(build->text[i + length]))) {
      return i;
    }
  }

  return limit;
}

static char *l_index_copy(LGIndexBuild *build, const char *text) {
  return text == NULL ? NULL : nexus_data_arena_text_copy(build->arena, text, nexus_strings_string_length_get(text));
}

static void l_index_add(LGIndexBuild *build, LGenesisSymbolKind kind, const char *name, uint_large offset, boolean declaration, const char *scope,
                        const char *qualifier) {
  LGenesisSymbol symbol;

  symbol.kind        = kind;
  symbol.name        = l_index_copy(build, name);
  symbol.offset      = (uint32)offset;
  symbol.width       = (uint32)nexus_strings_string_length_get(name);
  symbol.declaration = declaration;
  symbol.owner       = declaration ? build->path : NULL;
  symbol.scope       = l_index_copy(build, scope);
  (void)nexus_data_list_append(build->symbols, &symbol);
  (void)nexus_data_list_append_pointer(build->qualifiers, l_index_copy(build, qualifier));
}

/* ---------------------------------------------------------------------------- */
/* EXPRESSIONS                                                                   */
/* ---------------------------------------------------------------------------- */

/* Summary: the template a call names: one of this source, or one a library import offers. */
static const LGDecl *l_index_template_find(const LGIndexBuild *build, const char *name) {
  uint32     i;
  uint_large k;

  for (i = 0; build->unit != NULL && i < build->unit->decl_count; i++) {
    if (build->unit->decls[i]->kind == LGD_TEMPLATE && nexus_strings_string_equals(build->unit->decls[i]->name, name)) {
      return build->unit->decls[i];
    }
  }

  for (k = 0; k < nexus_data_list_length_get(build->imports); k++) {
    const LGIndexImport *import;

    import = (const LGIndexImport *)nexus_data_list_pointer_at(build->imports, k);

    if (import->unit == NULL) {
      continue;
    }

    for (i = 0; i < import->unit->decl_count; i++) {
      const LGDecl *offered;

      offered = import->unit->decls[i];

      if (offered->kind == LGD_TEMPLATE && nexus_strings_string_equals(offered->name, name)) {
        return offered;
      }
    }
  }

  return NULL;
}

/* Summary: whether a template's parameter takes the name of something the call declares (a token or rule name). */
static boolean l_index_parameter_declares(const LGDecl *callee, const char *parameter) {
  uint32 i;
  uint32 j;

  for (i = 0; callee != NULL && i < callee->param_count; i++) {
    if (!nexus_strings_string_equals(callee->params[i].name, parameter)) {
      continue;
    }

    for (j = 0; j < callee->params[i].type_count; j++) {
      if (nexus_strings_string_equals(callee->params[i].types[j], "TokenName") ||
          nexus_strings_string_equals(callee->params[i].types[j], "RuleName")) {
        return TRUE;
      }
    }
  }

  return FALSE;
}

static void l_index_expression(LGIndexBuild *build, const LGExpr *expression, const char *scope);

static void l_index_annotations(LGIndexBuild *build, const LGAnnotation *annotations, uint32 count, const char *scope) {
  uint32 i;
  uint32 j;

  for (i = 0; i < count; i++) {
    for (j = 0; j < annotations[i].argument_count; j++) {
      l_index_expression(build, annotations[i].arguments[j], scope);
    }
  }
}

static void l_index_expression(LGIndexBuild *build, const LGExpr *expression, const char *scope) {
  uint32      i;
  uint_large  length;
  const char *fragment_scope;

  if (expression == NULL) {
    return;
  }

  /* A fragment belongs to the context its use stands in; the scope passed down is that context. */
  fragment_scope = scope;

  switch (expression->kind) {
  case LGE_TOKEN:
    l_index_add(build, LGSK_TOKEN, expression->name, expression->span.offset + 1, FALSE, NULL, NULL);
    break;
  case LGE_FRAGMENT:
    l_index_add(build, LGSK_FRAGMENT, expression->name, expression->span.offset + 1, FALSE, fragment_scope, NULL);
    break;
  case LGE_PARAM:
    l_index_add(build, LGSK_PARAMETER, expression->name, expression->span.offset + 2, FALSE, scope, NULL);
    break;
  case LGE_RULE:
    length = nexus_strings_string_length_get(expression->name);

    if (expression->qualifier != NULL) {
      l_index_add(build, LGSK_ALIAS, expression->qualifier, expression->span.offset, FALSE, NULL, NULL);
    }

    l_index_add(build, LGSK_RULE, expression->name, expression->span.offset + expression->span.width - length, FALSE, NULL, expression->qualifier);
    break;
  case LGE_ENUM:
    length = nexus_strings_string_length_get(expression->name);
    l_index_add(build, LGSK_ENUM, expression->qualifier, expression->span.offset, FALSE, NULL, NULL);
    l_index_add(build, LGSK_ENUM_MEMBER, expression->name, expression->span.offset + expression->span.width - length, FALSE, expression->qualifier,
                NULL);
    break;
  case LGE_CALL:
    if (expression->sigil != '\0') {
      const LGDecl *template_decl;

      /* A template call: its name touches the sigil, and each argument name is a parameter of that template. */
      l_index_add(build, LGSK_TEMPLATE, expression->name, expression->span.offset + 1, FALSE, NULL, NULL);
      template_decl = l_index_template_find(build, expression->name);

      for (i = 0; i < expression->item_count; i++) {
        const LGExpr *value;

        value = expression->items[i];

        if (expression->arg_names != NULL && expression->arg_names[i] != NULL && expression->arg_spans != NULL) {
          l_index_add(build, LGSK_PARAMETER, expression->arg_names[i], expression->arg_spans[i].offset, FALSE, expression->name, NULL);
        }

        /* An argument that names a token or a rule the template generates declares it here. */
        if (expression->arg_names != NULL && expression->arg_names[i] != NULL && value != NULL &&
            (value->kind == LGE_TOKEN || value->kind == LGE_RULE) && l_index_parameter_declares(template_decl, expression->arg_names[i])) {
          if (value->kind == LGE_TOKEN) {
            l_index_add(build, LGSK_TOKEN, value->name, value->span.offset + 1, TRUE, NULL, NULL);
          } else {
            l_index_add(build, LGSK_RULE, value->name, value->span.offset + 1, TRUE, NULL, NULL);
          }
        } else {
          l_index_expression(build, value, scope);
        }
      }

      return;
    }

    break;
  default:
    break;
  }

  for (i = 0; i < expression->item_count; i++) {
    l_index_expression(build, expression->items[i], scope);
  }

  l_index_expression(build, expression->child, scope);
  l_index_expression(build, expression->else_branch, scope);
  l_index_annotations(build, expression->annotations, expression->annotation_count, scope);
}

/* ---------------------------------------------------------------------------- */
/* DECLARATIONS                                                                  */
/* ---------------------------------------------------------------------------- */

static void l_index_declaration(LGIndexBuild *build, const LGDecl *decl, const char *scope);

static void l_index_declarations(LGIndexBuild *build, LGDecl *const *decls, uint32 count, const char *scope) {
  uint32 i;

  for (i = 0; i < count; i++) {
    l_index_declaration(build, decls[i], scope);
  }
}

/* Summary: the context a push, set or replace action names. */
static void l_index_action(LGIndexBuild *build, const LGDecl *decl) {
  uint_large at;

  if (decl->action_target == NULL || decl->action == LGA_NONE) {
    return;
  }

  at = l_index_find(build, decl->action_span.offset, (uint_large)decl->action_span.offset + decl->action_span.width, decl->action_target);

  if (at < (uint_large)decl->action_span.offset + decl->action_span.width) {
    l_index_add(build, LGSK_CONTEXT, decl->action_target, at, FALSE, NULL, NULL);
  }
}

/* Summary: declares a name written as sigil + name inside the declaration's span. */
static void l_index_named(LGIndexBuild *build, const LGDecl *decl, LGenesisSymbolKind kind, const char *scope) {
  char       written[256];
  uint_large at;
  uint_large end;

  (void)stbsp_snprintf(written, (int)sizeof(written), "%c%s", decl->sigil == '\0' ? '#' : decl->sigil, decl->name);
  end = (uint_large)decl->span.offset + decl->span.width;
  at  = l_index_find(build, decl->span.offset, end, written);

  /* The sigil is not a word character, so the search matched the sigil's neighbour; the name starts right after it. */
  if (at < end) {
    l_index_add(build, kind, decl->name, at + 1, TRUE, scope, NULL);
  }
}

static void l_index_declaration(LGIndexBuild *build, const LGDecl *decl, const char *scope) {
  uint32      i;
  uint_large  at;
  uint_large  end;
  const char *inner;

  if (decl == NULL) {
    return;
  }

  end = (uint_large)decl->span.offset + decl->span.width;

  switch (decl->kind) {
  case LGD_RULE:
    if (decl->name_param != NULL) {
      l_index_expression(build, decl->name_param, scope);
    } else if (decl->name != NULL) {
      l_index_named(build, decl, LGSK_RULE, NULL);
    }

    l_index_annotations(build, decl->annotations, decl->annotation_count, scope);
    l_index_expression(build, decl->expression, scope);
    break;
  case LGD_TOKEN:
    if (decl->name_param != NULL) {
      l_index_expression(build, decl->name_param, scope);
    } else if (decl->name != NULL) {
      l_index_named(build, decl, LGSK_TOKEN, NULL);
    }

    l_index_annotations(build, decl->annotations, decl->annotation_count, scope);
    l_index_expression(build, decl->expression, scope);
    l_index_expression(build, decl->priority, scope);
    l_index_action(build, decl);
    break;
  case LGD_FRAGMENT:
    if (decl->name != NULL) {
      l_index_named(build, decl, LGSK_FRAGMENT, scope);
    }

    l_index_expression(build, decl->expression, scope);
    break;
  case LGD_CONTEXT:
    if (decl->name != NULL) {
      /* CONTEXT <NAME> { ... }: the first word after the keyword. */
      at = l_index_find(build, decl->span.offset, end, decl->name);

      if (at < end) {
        l_index_add(build, LGSK_CONTEXT, decl->name, at, TRUE, NULL, NULL);
      }
    }

    l_index_declarations(build, decl->children, decl->child_count, decl->name);
    break;
  case LGD_ENUM:
    at = l_index_find(build, decl->span.offset, end, decl->name);

    if (at < end) {
      l_index_add(build, LGSK_ENUM, decl->name, at, TRUE, NULL, NULL);
    }

    for (i = 0; i < decl->name_count; i++) {
      l_index_add(build, LGSK_ENUM_MEMBER, decl->names[i], decl->name_spans[i].offset, TRUE, decl->name, NULL);
    }

    break;
  case LGD_PARSER:
    for (i = 0; i < decl->name_count; i++) {
      l_index_expression(build, decl->values[i], scope);
    }

    break;
  case LGD_PRATT:
  case LGD_EMBED:
    if (decl->name != NULL) {
      l_index_named(build, decl, LGSK_RULE, NULL);
    }

    for (i = 0; i < decl->name_count; i++) {
      l_index_expression(build, decl->values[i], scope);
    }

    l_index_declarations(build, decl->children, decl->child_count, scope);
    break;
  case LGD_ENTRY:
    l_index_expression(build, decl->entry_token, scope);
    l_index_expression(build, decl->left_bp, scope);
    l_index_expression(build, decl->right_bp, scope);
    l_index_expression(build, decl->call, scope);
    l_index_expression(build, decl->expression, scope);
    break;
  case LGD_TEMPLATE:
    if (decl->name != NULL) {
      l_index_named(build, decl, LGSK_TEMPLATE, NULL);
    }

    for (i = 0; i < decl->param_count; i++) {
      inner = decl->params[i].name;
      /* The parameter's span covers `$$name`; the name is its tail. */
      l_index_add(build, LGSK_PARAMETER, inner,
                  (uint_large)decl->params[i].span.offset + decl->params[i].span.width - nexus_strings_string_length_get(inner), TRUE, decl->name,
                  NULL);
      l_index_expression(build, decl->params[i].default_value, decl->name);
    }

    l_index_declarations(build, decl->children, decl->child_count, decl->name);
    l_index_expression(build, decl->expression, decl->name);
    break;
  case LGD_CALL:
    l_index_expression(build, decl->call, scope);
    break;
  case LGD_IF:
    l_index_expression(build, decl->condition, scope);
    l_index_declarations(build, decl->children, decl->child_count, scope);
    l_index_declarations(build, decl->else_children, decl->else_count, scope);
    break;
  case LGD_IMPORT:
  default:
    break;
  }
}

/* ---------------------------------------------------------------------------- */
/* IMPORTS                                                                       */
/* ---------------------------------------------------------------------------- */

/* Summary: finds every IMPORT of the unit in the search folders and reads its syntax tree; what cannot be found stays unresolved. */
static void l_index_imports_locate(LGIndexBuild *build, const LGUnit *unit) {
  uint32 i;

  for (i = 0; i < unit->decl_count; i++) {
    const LGDecl  *decl;
    LGIndexImport *import;
    char          *path;
    LGUnit        *parsed;

    decl = unit->decls[i];

    if (decl->kind != LGD_IMPORT) {
      continue;
    }

    import             = (LGIndexImport *)nexus_data_arena_allocate(build->arena, NEXUS_SIZEOF(*import));
    import->name       = decl->name;
    import->alias      = decl->alias;
    import->items      = (const char **)decl->names;
    import->item_count = decl->name_count;
    path               = logos_languages_genesis_compiler_import_locate(build->compiler, decl->name);
    parsed             = NULL;

    if (path != NULL) {
      import->path = nexus_data_arena_text_copy(build->arena, path, nexus_strings_string_length_get(path));
      (void)logos_languages_genesis_compiler_file_parse(build->compiler, build->arena, path, &parsed);
      nexus_memory_deallocate(path);
    }

    import->unit = parsed;
    (void)nexus_data_list_append_pointer(build->imports, import);
  }
}

static const LGIndexImport *l_index_import_by_label(const LGIndexBuild *build, const char *label);

static void l_index_imports(LGIndexBuild *build, const LGUnit *unit) {
  uint32 i;
  uint32 j;

  for (i = 0; i < unit->decl_count; i++) {
    const LGDecl *decl;
    uint_large    end;
    uint_large    at;
    uint_large    name_end;

    decl = unit->decls[i];

    if (decl->kind != LGD_IMPORT) {
      continue;
    }

    /* The unit's name ends the keyword part of the declaration. */
    end = (uint_large)decl->span.offset + decl->span.width;
    at  = l_index_find(build, decl->span.offset, end, decl->name);

    if (at < end) {
      l_index_add(build, LGSK_UNIT, decl->name, at, FALSE, NULL, NULL);
      name_end = at + nexus_strings_string_length_get(decl->name);

      if (decl->alias != NULL) {
        /* The alias follows AS, before the item list or the end of the line. */
        uint_large stop;

        stop = name_end;

        while (stop < build->size && build->text[stop] != '{' && build->text[stop] != '\n') {
          stop++;
        }

        at = l_index_find(build, name_end, stop, decl->alias);

        if (at < stop) {
          l_index_add(build, LGSK_ALIAS, decl->alias, at, TRUE, NULL, NULL);
        }
      }
    }

    for (j = 0; j < decl->name_count; j++) {
      const char *item;

      item = decl->names[j];

      /* What a library offers with a sigil is a template; what a language offers is a rule. */
      if (item[0] == '#' || item[0] == '$') {
        const LGIndexImport *target;

        target = l_index_import_by_label(build, decl->alias != NULL ? decl->alias : decl->name);
        l_index_add(build, target != NULL && target->unit != NULL && target->unit->kind == LGUK_LIBRARY ? LGSK_TEMPLATE : LGSK_RULE, item + 1,
                    (uint_large)decl->name_spans[j].offset + 1, FALSE, NULL, decl->alias != NULL ? decl->alias : decl->name);
      } else {
        l_index_add(build, LGSK_ENUM, item, decl->name_spans[j].offset, FALSE, NULL, decl->alias != NULL ? decl->alias : decl->name);
      }
    }
  }
}

/* ---------------------------------------------------------------------------- */
/* RESOLUTION                                                                    */
/* ---------------------------------------------------------------------------- */

static boolean l_index_text_equal(const char *left, const char *right) {
  if (left == NULL || right == NULL) {
    return left == right;
  }

  return nexus_strings_string_equals(left, right);
}

static const LGIndexImport *l_index_import_by_label(const LGIndexBuild *build, const char *label) {
  uint_large i;

  for (i = 0; i < nexus_data_list_length_get(build->imports); i++) {
    const LGIndexImport *import;

    import = (const LGIndexImport *)nexus_data_list_pointer_at(build->imports, i);

    if (nexus_strings_string_equals(import->alias != NULL ? import->alias : import->name, label)) {
      return import;
    }
  }

  return NULL;
}

/* Summary: the import that lists an item (written with its sigil) among those it brings in. */
static const LGIndexImport *l_index_import_by_item(const LGIndexBuild *build, const char *item) {
  uint_large i;
  uint32     j;

  for (i = 0; i < nexus_data_list_length_get(build->imports); i++) {
    const LGIndexImport *import;

    import = (const LGIndexImport *)nexus_data_list_pointer_at(build->imports, i);

    for (j = 0; j < import->item_count; j++) {
      if (nexus_strings_string_equals(import->items[j], item)) {
        return import;
      }
    }
  }

  return NULL;
}

/* Summary: the declaration, among the declarations of this source, that an occurrence means. */
static const LGenesisSymbol *l_index_local(const LGIndexBuild *build, const LGenesisSymbol *use, LGenesisSymbolKind kind, const char *scope) {
  uint_large i;

  for (i = 0; i < nexus_data_list_length_get(build->symbols); i++) {
    const LGenesisSymbol *candidate;

    candidate = (const LGenesisSymbol *)nexus_data_list_at_const(build->symbols, i);

    if (candidate->declaration && candidate->kind == kind && nexus_strings_string_equals(candidate->name, use->name) &&
        l_index_text_equal(candidate->scope, scope)) {
      return candidate;
    }
  }

  return NULL;
}

static const char *l_index_entry_path(const LGIndexImport *import) {
  return import != NULL ? import->path : NULL;
}

/* Summary: the owner of a callee, rule or enumeration used by name: this source, or the import that offers it. */
static const char *l_index_owner_of_item(const LGIndexBuild *build, const LGenesisSymbol *use, const char *qualifier) {
  char                 item[260];
  const LGIndexImport *found;

  if (qualifier != NULL) {
    return l_index_entry_path(l_index_import_by_label(build, qualifier));
  }

  if (l_index_local(build, use, use->kind, NULL) != NULL) {
    return build->path;
  }

  if (use->kind == LGSK_ENUM) {
    (void)stbsp_snprintf(item, (int)sizeof(item), "%s", use->name);

    return l_index_entry_path(l_index_import_by_item(build, item));
  }

  /* A template is written with either sigil, and an import lists it the way its library declares it. */
  (void)stbsp_snprintf(item, (int)sizeof(item), "#%s", use->name);
  found = l_index_import_by_item(build, item);

  if (found == NULL && use->kind != LGSK_RULE) {
    (void)stbsp_snprintf(item, (int)sizeof(item), "$%s", use->name);
    found = l_index_import_by_item(build, item);
  }

  return l_index_entry_path(found);
}

static void l_index_resolve(LGIndexBuild *build) {
  uint_large i;
  uint_large count;

  count = nexus_data_list_length_get(build->symbols);

  /* Enumerations and templates first: members and arguments take their owner from them. */
  for (i = 0; i < count; i++) {
    LGenesisSymbol *use;

    use = (LGenesisSymbol *)nexus_data_list_at(build->symbols, i);

    if (use->declaration) {
      continue;
    }

    switch (use->kind) {
    case LGSK_UNIT:
      use->owner = l_index_entry_path(l_index_import_by_label(build, use->name));

      if (use->owner == NULL) {
        uint_large k;

        for (k = 0; k < nexus_data_list_length_get(build->imports); k++) {
          const LGIndexImport *import;

          import = (const LGIndexImport *)nexus_data_list_pointer_at(build->imports, k);

          if (nexus_strings_string_equals(import->name, use->name)) {
            use->owner = l_index_entry_path(import);
          }
        }
      }

      break;
    case LGSK_ALIAS:
      use->owner = l_index_local(build, use, LGSK_ALIAS, NULL) != NULL ? build->path : NULL;
      break;
    case LGSK_CONTEXT:
      use->owner = l_index_local(build, use, LGSK_CONTEXT, NULL) != NULL ? build->path : NULL;
      break;
    case LGSK_TOKEN:
      use->owner = l_index_local(build, use, LGSK_TOKEN, NULL) != NULL ? build->path : NULL;
      break;
    case LGSK_FRAGMENT:
      use->owner = l_index_local(build, use, LGSK_FRAGMENT, use->scope) != NULL ? build->path : NULL;

      if (use->owner == NULL && l_index_local(build, use, LGSK_FRAGMENT, NULL) != NULL) {
        use->scope = NULL;
        use->owner = build->path;
      }

      break;
    case LGSK_RULE:
    case LGSK_TEMPLATE:
    case LGSK_ENUM:
      use->owner = l_index_owner_of_item(build, use, (const char *)nexus_data_list_pointer_at(build->qualifiers, i));
      break;
    default:
      break;
    }
  }

  for (i = 0; i < count; i++) {
    LGenesisSymbol *use;

    use = (LGenesisSymbol *)nexus_data_list_at(build->symbols, i);

    if (use->declaration) {
      continue;
    }

    if (use->kind == LGSK_ENUM_MEMBER) {
      /* The enumeration written before the member decides where the member lives. */
      LGenesisSymbol enumeration;

      enumeration.name = use->scope;
      enumeration.kind = LGSK_ENUM;
      use->owner       = l_index_owner_of_item(build, &enumeration, NULL);
    } else if (use->kind == LGSK_PARAMETER) {
      if (use->scope == NULL) {
        use->owner = NULL;
      } else {
        LGenesisSymbol template_use;

        template_use.name = use->scope;
        template_use.kind = LGSK_TEMPLATE;

        /* A parameter used inside its own template resolves locally; an argument name of a call resolves to the callee. */
        use->owner = l_index_local(build, use, LGSK_PARAMETER, use->scope) != NULL ? build->path : l_index_owner_of_item(build, &template_use, NULL);
      }
    }
  }
}

/* ---------------------------------------------------------------------------- */
/* PUBLIC                                                                        */
/* ---------------------------------------------------------------------------- */

LGenesisIndex *logos_languages_genesis_index_create(LGenesisCompiler *compiler, const char *source_name, const byte *text, uint_large size) {
  LGenesisIndex *index;
  LGIndexBuild   build;
  LGUnit        *unit;
  uint32         source;

  NEXUS_PROGRAM_PRECONDITION(compiler != NULL && source_name != NULL && (text != NULL || size == 0), "index arguments must not be NULL");

  index = (LGenesisIndex *)nexus_memory_allocate(NEXUS_SIZEOF(*index));
  nexus_data_arena_initialize(&index->arena);
  index->count = 0;

  build.compiler   = compiler;
  build.arena      = &index->arena;
  build.unit       = NULL;
  build.path       = nexus_data_arena_text_copy(&index->arena, source_name, nexus_strings_string_length_get(source_name));
  build.text       = text;
  build.size       = size;
  build.symbols    = nexus_data_list_create(NEXUS_SIZEOF(LGenesisSymbol), 128);
  build.qualifiers = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 128);
  build.imports    = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 4);
  index->unit_name = NULL;
  unit             = NULL;
  source           = logos_languages_genesis_compiler_source_add(compiler, source_name, text, size);

  /* The source copied by the compiler is what the spans refer to; the text given is the same bytes. */
  (void)logos_languages_genesis_syntax_parse(compiler, &index->arena, source, &unit);

  if (unit != NULL) {
    build.unit       = unit;
    index->unit_name = nexus_data_arena_text_copy(&index->arena, unit->name, nexus_strings_string_length_get(unit->name));
    l_index_add(&build, LGSK_UNIT, unit->name, unit->name_span.offset, TRUE, NULL, NULL);
    l_index_imports_locate(&build, unit);
    l_index_imports(&build, unit);
    l_index_declarations(&build, unit->decls, unit->decl_count, NULL);
    l_index_resolve(&build);

    index->count   = nexus_data_list_length_get(build.symbols);
    index->symbols = (LGenesisSymbol *)nexus_data_arena_allocate(&index->arena, (index->count + 1) * NEXUS_SIZEOF(LGenesisSymbol));

    if (index->count > 0) {
      nexus_memory_bytes_copy(nexus_data_list_data_get_const(build.symbols), index->symbols, index->count * NEXUS_SIZEOF(LGenesisSymbol));
    }

  } else {
    index->symbols = (LGenesisSymbol *)nexus_data_arena_allocate(&index->arena, NEXUS_SIZEOF(LGenesisSymbol));
  }

  nexus_data_list_destroy(build.symbols);
  nexus_data_list_destroy(build.qualifiers);
  nexus_data_list_destroy(build.imports);

  return index;
}

void logos_languages_genesis_index_destroy(LGenesisIndex *index) {
  if (index == NULL) {
    return;
  }

  nexus_data_arena_release(&index->arena);
  nexus_memory_deallocate(index);
}

uint_large logos_languages_genesis_index_symbol_count_get(const LGenesisIndex *index) {
  NEXUS_PROGRAM_PRECONDITION(index != NULL, "index must not be NULL");

  return index->count;
}

const LGenesisSymbol *logos_languages_genesis_index_symbol_at(const LGenesisIndex *index, uint_large position) {
  NEXUS_PROGRAM_PRECONDITION(index != NULL && position < index->count, "symbol position must be in bounds");

  return &index->symbols[position];
}

const char *logos_languages_genesis_index_unit_name_get(const LGenesisIndex *index) {
  NEXUS_PROGRAM_PRECONDITION(index != NULL, "index must not be NULL");

  return index->unit_name;
}
