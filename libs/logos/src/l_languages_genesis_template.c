#include <nexus.h>

#include "../logos.h"
#include "./l_internal.h"

/*
Expansion turns a parsed language into a flat model: contexts of tokens and fragments, rules, Pratt rules and
embeds, with every template call replaced by what it generates. A call binds its arguments to the template's
typed parameters, instantiates the template body in the template's own scope, renames the helpers the body
declares so two instances never collide (hygiene), and evaluates @if against the bound values. An expression
template stands for its namesake: the helper of the same name that its body declares.
*/

#define LG_EXPAND_DEPTH_MAX 64

typedef struct LGScope {
  const LGUnit        *unit;
  const NexusDataList *imports; /* LGImport * */
} LGScope;

typedef struct LGBind {
  const char    *name;
  const LGParam *param;
  LGExpr        *value;
} LGBind;

typedef struct LGRename {
  char        sigil;
  const char *from;
  const char *to;
} LGRename;

typedef struct LGFrame {
  LGScope        scope;
  LGBind        *binds;
  uint32         bind_count;
  NexusDataList *renames; /* LGRename */
  const LGDecl  *template_decl;
  PharosSpan     call_span;
  uint32         depth;
} LGFrame;

typedef struct LGOutputs {
  LGContextModel *context;
  LGPrattModel   *pratt;
} LGOutputs;

typedef struct LGMemo {
  const LGDecl *template_decl;
  char         *key;
  LGExpr       *result;
} LGMemo;

typedef struct LGExpander {
  LGenesisCompiler *compiler;
  NexusDataArena   *arena;
  LGModel          *model;
  NexusDataList    *memo; /* LGMemo */
  boolean           ok;
} LGExpander;

static const char *const l_genesis_builtin_choice_members[] = {"ORDERED", "LONGEST", "UNAMBIGUOUS"};

static LGExpr *l_genesis_expand_expression(LGExpander *expander, const LGFrame *frame, const LGOutputs *outputs, LGExpr *expression);
static boolean l_genesis_expand_declaration(LGExpander *expander, const LGFrame *frame, const LGOutputs *outputs, const LGDecl *decl);

/* ---------------------------------------------------------------------------- */
/* SUPPORT                                                                       */
/* ---------------------------------------------------------------------------- */

static void *l_genesis_expand_allocate(LGExpander *expander, uint_large size) {
  return nexus_data_arena_allocate(expander->arena, size);
}

static char *l_genesis_expand_text(LGExpander *expander, const char *text) {
  return nexus_data_arena_text_copy(expander->arena, text, nexus_strings_string_length_get(text));
}

static LGExpr *l_genesis_expand_new(LGExpander *expander, LGExprKind kind, PharosSpan span) {
  LGExpr *expression;

  expression       = (LGExpr *)l_genesis_expand_allocate(expander, NEXUS_SIZEOF(*expression));
  expression->kind = kind;
  expression->span = span;

  return expression;
}

static PharosSpan l_genesis_span_none(void) {
  PharosSpan span;

  span.source = 0;
  span.offset = 0;
  span.width  = 0;

  return span;
}

/* Summary: appends the source-like text of an expression to a growing byte list; used for argument keys and messages. */
static void l_genesis_print(NexusDataList *out, const LGExpr *expression);

static void l_genesis_print_text(NexusDataList *out, const char *text) {
  uint_large i;
  uint_large length;

  length = nexus_strings_string_length_get(text);

  for (i = 0; i < length; i++) {
    (void)nexus_data_list_append(out, &text[i]);
  }
}

static void l_genesis_print_list(NexusDataList *out, const LGExpr *expression, const char *separator) {
  uint32 i;

  for (i = 0; i < expression->item_count; i++) {
    if (i > 0) {
      l_genesis_print_text(out, separator);
    }

    l_genesis_print(out, expression->items[i]);
  }
}

static void l_genesis_print(NexusDataList *out, const LGExpr *expression) {
  char   buffer[32];
  uint32 i;

  switch (expression->kind) {
  case LGE_EMPTY:
    break;
  case LGE_NONE:
    l_genesis_print_text(out, "none");
    break;
  case LGE_STRING:
    l_genesis_print_text(out, "'");

    for (i = 0; i < expression->codepoint_count; i++) {
      (void)stbsp_snprintf(buffer, (int)sizeof(buffer), "\\u{%X}", (unsigned)expression->codepoints[i]);
      l_genesis_print_text(out, buffer);
    }

    l_genesis_print_text(out, "'");
    break;
  case LGE_RANGE:
    (void)stbsp_snprintf(buffer, (int)sizeof(buffer), "\\u{%X}..\\u{%X}", (unsigned)expression->lo, (unsigned)expression->hi);
    l_genesis_print_text(out, buffer);
    break;
  case LGE_ANY:
    l_genesis_print_text(out, ".");
    break;
  case LGE_NOT:
    l_genesis_print_text(out, "!");
    l_genesis_print(out, expression->child);
    break;
  case LGE_TOKEN:
  case LGE_FRAGMENT:
    l_genesis_print_text(out, "$");
    l_genesis_print_text(out, expression->name);
    break;
  case LGE_RULE:
    if (expression->qualifier != NULL) {
      l_genesis_print_text(out, expression->qualifier);
      l_genesis_print_text(out, ".");
    }

    l_genesis_print_text(out, "#");
    l_genesis_print_text(out, expression->name);
    break;
  case LGE_PARAM:
    l_genesis_print_text(out, "$$");
    l_genesis_print_text(out, expression->name);
    break;
  case LGE_CALL:
    if (expression->sigil == '#') {
      l_genesis_print_text(out, "#");
    } else if (expression->sigil != 0) {
      l_genesis_print_text(out, "$");
    }

    l_genesis_print_text(out, expression->name);
    l_genesis_print_text(out, "(");

    for (i = 0; i < expression->item_count; i++) {
      if (i > 0) {
        l_genesis_print_text(out, ", ");
      }

      if (expression->arg_names != NULL && expression->arg_names[i] != NULL) {
        l_genesis_print_text(out, expression->arg_names[i]);
        l_genesis_print_text(out, ": ");
      }

      l_genesis_print(out, expression->items[i]);
    }

    l_genesis_print_text(out, ")");
    break;
  case LGE_UNION:
    l_genesis_print_list(out, expression, " || ");
    break;
  case LGE_CONCAT:
    l_genesis_print_list(out, expression, " ");
    break;
  case LGE_REPEAT:
    l_genesis_print(out, expression->child);

    if (expression->repeat_op == '{') {
      char counts[48];

      if (expression->hi == 0xFFFFFFFFU) {
        (void)stbsp_snprintf(counts, (int)sizeof(counts), "{%u,}", (unsigned)expression->lo);
      } else if (expression->hi == expression->lo) {
        (void)stbsp_snprintf(counts, (int)sizeof(counts), "{%u}", (unsigned)expression->lo);
      } else {
        (void)stbsp_snprintf(counts, (int)sizeof(counts), "{%u,%u}", (unsigned)expression->lo, (unsigned)expression->hi);
      }

      l_genesis_print_text(out, counts);
    } else {
      (void)nexus_data_list_append(out, &expression->repeat_op);
    }

    break;
  case LGE_GROUP:
    l_genesis_print_text(out, "(");
    l_genesis_print(out, expression->child);
    l_genesis_print_text(out, ")");
    break;
  case LGE_IF:
  case LGE_COMPARE:
    l_genesis_print_text(out, "@if");
    break;
  case LGE_ENUM:
    l_genesis_print_text(out, expression->qualifier);
    l_genesis_print_text(out, ".");
    l_genesis_print_text(out, expression->name);
    break;
  case LGE_INT:
    (void)stbsp_snprintf(buffer, (int)sizeof(buffer), "%lld", (long long)expression->integer);
    l_genesis_print_text(out, buffer);
    break;
  case LGE_TEXT:
    l_genesis_print_text(out, "\"");
    l_genesis_print_text(out, expression->name);
    l_genesis_print_text(out, "\"");
    break;
  case LGE_IDENT:
    l_genesis_print_text(out, expression->name);
    break;
  default:
    break;
  }
}

/* Summary: the text of an expression in the arena. */
static char *l_genesis_expand_render(LGExpander *expander, const LGExpr *expression) {
  NexusDataList *buffer;
  char          *text;
  char           terminator;

  buffer     = nexus_data_list_create(NEXUS_SIZEOF(char), 64);
  terminator = '\0';
  l_genesis_print(buffer, expression);
  (void)nexus_data_list_append(buffer, &terminator);
  text = l_genesis_expand_text(expander, (const char *)nexus_data_list_data_get_const(buffer));
  nexus_data_list_destroy(buffer);

  return text;
}

/* ---------------------------------------------------------------------------- */
/* SCOPE LOOKUP                                                                  */
/* ---------------------------------------------------------------------------- */

typedef struct LGTemplateRef {
  const LGDecl *decl;
  LGScope       scope;
} LGTemplateRef;

static LGScope l_genesis_scope_of_entry(const LGRegistryEntry *entry) {
  LGScope scope;

  scope.unit    = entry->unit;
  scope.imports = entry->imports;

  return scope;
}

static boolean l_genesis_scope_imports_item(const LGImport *import, char sigil, const char *name) {
  uint32 i;

  for (i = 0; i < import->item_count; i++) {
    if (sigil != 0) {
      if (import->items[i][0] == sigil && nexus_strings_string_equals(import->items[i] + 1, name)) {
        return TRUE;
      }
    } else if (nexus_strings_string_equals(import->items[i], name)) {
      return TRUE;
    }
  }

  return FALSE;
}

static boolean l_genesis_find_template(const LGScope *scope, char sigil, const char *name, LGTemplateRef *out) {
  uint32          i;
  uint_large      k;
  const LGImport *import;

  if (scope->unit != NULL) {
    for (i = 0; i < scope->unit->decl_count; i++) {
      if (scope->unit->decls[i]->kind == LGD_TEMPLATE && scope->unit->decls[i]->sigil == sigil &&
          nexus_strings_string_equals(scope->unit->decls[i]->name, name)) {
        out->decl  = scope->unit->decls[i];
        out->scope = *scope;

        return TRUE;
      }
    }
  }

  if (scope->imports != NULL) {
    for (k = 0; k < nexus_data_list_length_get(scope->imports); k++) {
      import = (const LGImport *)nexus_data_list_pointer_at(scope->imports, k);

      if (import->entry->kind != LGUK_LIBRARY || !l_genesis_scope_imports_item(import, sigil, name)) {
        continue;
      }

      for (i = 0; import->entry->unit != NULL && i < import->entry->unit->decl_count; i++) {
        const LGDecl *offered;

        offered = import->entry->unit->decls[i];

        if (offered->kind == LGD_TEMPLATE && offered->sigil == sigil && nexus_strings_string_equals(offered->name, name)) {
          out->decl  = offered;
          out->scope = l_genesis_scope_of_entry(import->entry);

          return TRUE;
        }
      }
    }
  }

  return FALSE;
}

/* Summary: finds an enumeration visible in a scope; its members are returned through out_members. */
static boolean l_genesis_find_enum(const LGScope *scope, const char *name, const char *const **out_members, uint32 *out_count) {
  uint32          i;
  uint_large      k;
  const LGImport *import;

  if (nexus_strings_string_equals(name, "ChoiceMode")) {
    *out_members = l_genesis_builtin_choice_members;
    *out_count   = 3;

    return TRUE;
  }

  if (scope->unit != NULL) {
    for (i = 0; i < scope->unit->decl_count; i++) {
      if (scope->unit->decls[i]->kind == LGD_ENUM && nexus_strings_string_equals(scope->unit->decls[i]->name, name)) {
        *out_members = scope->unit->decls[i]->names;
        *out_count   = scope->unit->decls[i]->name_count;

        return TRUE;
      }
    }
  }

  if (scope->imports != NULL) {
    for (k = 0; k < nexus_data_list_length_get(scope->imports); k++) {
      import = (const LGImport *)nexus_data_list_pointer_at(scope->imports, k);

      if (import->entry->kind != LGUK_LIBRARY || !l_genesis_scope_imports_item(import, 0, name)) {
        continue;
      }

      for (i = 0; import->entry->unit != NULL && i < import->entry->unit->decl_count; i++) {
        if (import->entry->unit->decls[i]->kind == LGD_ENUM && nexus_strings_string_equals(import->entry->unit->decls[i]->name, name)) {
          *out_members = import->entry->unit->decls[i]->names;
          *out_count   = import->entry->unit->decls[i]->name_count;

          return TRUE;
        }
      }
    }
  }

  return FALSE;
}

/* ---------------------------------------------------------------------------- */
/* ARGUMENT TYPES                                                                */
/* ---------------------------------------------------------------------------- */

static boolean l_genesis_is_expression_kind(LGExprKind kind) {
  return kind == LGE_RULE || kind == LGE_TOKEN || kind == LGE_FRAGMENT || kind == LGE_GROUP || kind == LGE_UNION || kind == LGE_CONCAT ||
         kind == LGE_REPEAT || kind == LGE_STRING || kind == LGE_RANGE || kind == LGE_EMPTY || kind == LGE_ANY || kind == LGE_NOT;
}

/* Summary: tells whether a bound value satisfies one declared type name. */
static boolean l_genesis_type_accepts(const LGScope *scope, const char *type, const LGExpr *value) {
  const char *const *members;
  uint32             count;
  uint32             i;

  if (nexus_strings_string_equals(type, "Token")) {
    return value->kind == LGE_TOKEN;
  }

  if (nexus_strings_string_equals(type, "TokenName")) {
    return value->kind == LGE_TOKEN;
  }

  if (nexus_strings_string_equals(type, "RuleName")) {
    return value->kind == LGE_RULE && value->qualifier == NULL;
  }

  if (nexus_strings_string_equals(type, "Expression")) {
    return l_genesis_is_expression_kind(value->kind);
  }

  if (nexus_strings_string_equals(type, "string")) {
    return value->kind == LGE_TEXT;
  }

  if (nexus_strings_string_equals(type, "int")) {
    return value->kind == LGE_INT;
  }

  if (nexus_strings_string_equals(type, "uint")) {
    return value->kind == LGE_INT && value->integer >= 0;
  }

  if (nexus_strings_string_equals(type, "LanguageRef")) {
    return value->kind == LGE_IDENT;
  }

  if (nexus_strings_string_equals(type, "RuleRef")) {
    return value->kind == LGE_RULE && value->qualifier != NULL;
  }

  if (l_genesis_find_enum(scope, type, &members, &count)) {
    if (value->kind != LGE_ENUM || !nexus_strings_string_equals(value->qualifier, type)) {
      return FALSE;
    }

    for (i = 0; i < count; i++) {
      if (nexus_strings_string_equals(members[i], value->name)) {
        return TRUE;
      }
    }
  }

  return FALSE;
}

static const char *l_genesis_describe_value(const LGExpr *value) {
  switch (value->kind) {
  case LGE_TOKEN:
    return "a `$` token";
  case LGE_FRAGMENT:
    return "a `$_` fragment";
  case LGE_RULE:
    return value->qualifier != NULL ? "a qualified rule" : "a `#` rule";
  case LGE_TEXT:
    return "a text value";
  case LGE_INT:
    return "a number";
  case LGE_ENUM:
    return "an enumeration value";
  case LGE_NONE:
    return "`none`";
  case LGE_IDENT:
    return "a bare name";
  case LGE_STRING:
  case LGE_RANGE:
  case LGE_ANY:
  case LGE_NOT:
    return "a lexical literal";
  default:
    return "an expression";
  }
}

/* ---------------------------------------------------------------------------- */
/* EXPRESSIONS                                                                   */
/* ---------------------------------------------------------------------------- */

static const LGBind *l_genesis_frame_bind(const LGFrame *frame, const char *name) {
  uint32 i;

  for (i = 0; i < frame->bind_count; i++) {
    if (nexus_strings_string_equals(frame->binds[i].name, name)) {
      return &frame->binds[i];
    }
  }

  return NULL;
}

static const char *l_genesis_frame_rename(const LGFrame *frame, char sigil, const char *name) {
  uint_large      i;
  const LGRename *rename;

  if (frame->renames == NULL) {
    return name;
  }

  for (i = 0; i < nexus_data_list_length_get(frame->renames); i++) {
    rename = (const LGRename *)nexus_data_list_at_const(frame->renames, i);

    if (rename->sigil == sigil && nexus_strings_string_equals(rename->from, name)) {
      return rename->to;
    }
  }

  return name;
}

/* Summary: evaluates a comparison of bound values. */
static boolean l_genesis_compare(LGExpander *expander, const LGFrame *frame, const LGExpr *condition) {
  LGExpr *left;
  LGExpr *right;
  boolean equal;

  left  = l_genesis_expand_expression(expander, frame, NULL, condition->items[0]);
  right = l_genesis_expand_expression(expander, frame, NULL, condition->items[1]);
  equal = FALSE;

  if (left->kind == right->kind) {
    switch (left->kind) {
    case LGE_NONE:
      equal = TRUE;
      break;
    case LGE_INT:
      equal = left->integer == right->integer;
      break;
    case LGE_ENUM:
      equal = nexus_strings_string_equals(left->qualifier, right->qualifier) && nexus_strings_string_equals(left->name, right->name);
      break;
    case LGE_TEXT:
    case LGE_IDENT:
    case LGE_TOKEN:
    case LGE_RULE:
    case LGE_FRAGMENT:
      equal = nexus_strings_string_equals(left->name, right->name);
      break;
    default:
      break;
    }
  }

  return condition->compare_op == '=' ? equal : !equal;
}

static void l_genesis_report_unknown_name(LGExpander *expander, const LGFrame *frame, const LGExpr *expression, const char *what, const char *code) {
  PharosDiagnostic *diagnostic;
  const char      **candidates;
  const char       *suggestion;
  uint32            i;

  diagnostic = pharos_diagnostics_report((expander->compiler)->reporting, PDS_ERROR, code, expression->span, "cannot find %s `%s%s` in this scope",
                                         what, expression->kind == LGE_PARAM ? "$$" : "", expression->name);
  pharos_diagnostics_label_add(diagnostic, expression->span, TRUE, "not found");

  if (frame->template_decl != NULL && frame->bind_count > 0) {
    candidates = (const char **)l_genesis_expand_allocate(expander, frame->bind_count * NEXUS_EFFECTIVE_POINTER_SIZE_BYTES);

    for (i = 0; i < frame->bind_count; i++) {
      candidates[i] = frame->binds[i].name;
    }

    suggestion = nexus_strings_string_closest(expression->name, candidates, frame->bind_count);

    if (suggestion != NULL) {
      pharos_diagnostics_help_add(diagnostic, "did you mean `$$%s`?", suggestion);
    }
  } else if (expression->kind == LGE_PARAM) {
    pharos_diagnostics_note_add(diagnostic, "`$$name` refers to a template parameter and is only valid inside a template body");
  }

  expander->ok = FALSE;
}

/* Summary: instantiates a template call. Declarations it generates go to the model; an expression template returns its namesake reference. */
static LGExpr *l_genesis_instantiate(LGExpander *expander, const LGFrame *caller, const LGOutputs *outputs, const LGExpr *call,
                                     boolean as_expression);

static LGExpr *l_genesis_expand_expression(LGExpander *expander, const LGFrame *frame, const LGOutputs *outputs, LGExpr *expression) {
  LGExpr       *copy;
  LGExpr       *child;
  const LGBind *bind;
  const char   *renamed;
  uint32        i;
  uint32        j;
  boolean       changed;
  LGAnnotation *annotations;

  if (expression == NULL) {
    return NULL;
  }

  switch (expression->kind) {
  case LGE_PARAM:
    bind = l_genesis_frame_bind(frame, expression->name);

    if (bind == NULL) {
      l_genesis_report_unknown_name(expander, frame, expression, "template parameter", "G0307");

      return l_genesis_expand_new(expander, LGE_EMPTY, expression->span);
    }

    return bind->value;
  case LGE_TOKEN:
  case LGE_FRAGMENT:
  case LGE_RULE:
    renamed =
        expression->qualifier == NULL ? l_genesis_frame_rename(frame, expression->kind == LGE_RULE ? '#' : '$', expression->name) : expression->name;

    if (renamed == expression->name) {
      return expression;
    }

    copy        = l_genesis_expand_new(expander, expression->kind, expression->span);
    copy->name  = renamed;
    copy->sigil = expression->sigil;

    return copy;
  case LGE_CALL:
    if (expression->sigil == 0) {
      /* A built-in value constructor such as inner_bytes(4, 3): its arguments are expanded, the call itself stays. */
      copy        = l_genesis_expand_new(expander, LGE_CALL, expression->span);
      copy->name  = expression->name;
      copy->items = (LGExpr **)l_genesis_expand_allocate(expander, (expression->item_count + 1) * NEXUS_EFFECTIVE_POINTER_SIZE_BYTES);

      for (i = 0; i < expression->item_count; i++) {
        copy->items[i] = l_genesis_expand_expression(expander, frame, outputs, expression->items[i]);
      }

      copy->item_count = expression->item_count;

      return copy;
    }

    return l_genesis_instantiate(expander, frame, outputs, expression, TRUE);
  case LGE_IF:
    if (l_genesis_compare(expander, frame, expression->items[0])) {
      return l_genesis_expand_expression(expander, frame, outputs, expression->child);
    }

    if (expression->else_branch != NULL) {
      return l_genesis_expand_expression(expander, frame, outputs, expression->else_branch);
    }

    return l_genesis_expand_new(expander, LGE_EMPTY, expression->span);
  case LGE_UNION:
  case LGE_CONCAT:
    copy        = l_genesis_expand_new(expander, expression->kind, expression->span);
    copy->items = (LGExpr **)l_genesis_expand_allocate(expander, (expression->item_count + 1) * NEXUS_EFFECTIVE_POINTER_SIZE_BYTES);
    j           = 0;

    for (i = 0; i < expression->item_count; i++) {
      child = l_genesis_expand_expression(expander, frame, outputs, expression->items[i]);

      /* An item that expanded to nothing (a false @if) drops out; a nested group of the same kind stays a group. */
      if (child != NULL && child->kind != LGE_EMPTY) {
        copy->items[j++] = child;
      }
    }

    copy->item_count = j;

    if (j == 0) {
      return l_genesis_expand_new(expander, LGE_EMPTY, expression->span);
    }

    if (j == 1) {
      return copy->items[0];
    }

    return copy;
  case LGE_REPEAT:
    child = l_genesis_expand_expression(expander, frame, outputs, expression->child);

    if (child == NULL || child->kind == LGE_EMPTY || child->kind == LGE_NONE) {
      return l_genesis_expand_new(expander, LGE_EMPTY, expression->span);
    }

    copy            = l_genesis_expand_new(expander, LGE_REPEAT, expression->span);
    copy->child     = child;
    copy->repeat_op = expression->repeat_op;
    copy->lo        = expression->lo;
    copy->hi        = expression->hi;

    return copy;
  case LGE_GROUP:
    child = l_genesis_expand_expression(expander, frame, outputs, expression->child);

    if (child == NULL || child->kind == LGE_EMPTY) {
      return l_genesis_expand_new(expander, LGE_EMPTY, expression->span);
    }

    copy        = l_genesis_expand_new(expander, LGE_GROUP, expression->span);
    copy->child = child;
    changed     = expression->annotation_count > 0;

    if (changed) {
      annotations = (LGAnnotation *)l_genesis_expand_allocate(expander, expression->annotation_count * NEXUS_SIZEOF(LGAnnotation));

      for (i = 0; i < expression->annotation_count; i++) {
        annotations[i] = expression->annotations[i];
        annotations[i].arguments =
            (LGExpr **)l_genesis_expand_allocate(expander, (annotations[i].argument_count + 1) * NEXUS_EFFECTIVE_POINTER_SIZE_BYTES);

        for (j = 0; j < annotations[i].argument_count; j++) {
          annotations[i].arguments[j] = l_genesis_expand_expression(expander, frame, outputs, expression->annotations[i].arguments[j]);
        }
      }

      copy->annotations      = annotations;
      copy->annotation_count = expression->annotation_count;
    }

    return copy;
  case LGE_NOT:
    child = l_genesis_expand_expression(expander, frame, outputs, expression->child);

    if (child == NULL) {
      return l_genesis_expand_new(expander, LGE_EMPTY, expression->span);
    }

    copy        = l_genesis_expand_new(expander, LGE_NOT, expression->span);
    copy->child = child;

    return copy;
  case LGE_ANY:
  case LGE_STRING:
  case LGE_RANGE:
  case LGE_NONE:
  case LGE_ENUM:
  case LGE_INT:
  case LGE_TEXT:
  case LGE_IDENT:
  case LGE_EMPTY:
  case LGE_COMPARE:
  default:
    return expression;
  }
}

/* ---------------------------------------------------------------------------- */
/* MODEL CONSTRUCTION                                                            */
/* ---------------------------------------------------------------------------- */

static LGContextModel *l_genesis_context_find(const LGModel *model, const char *name) {
  uint_large      i;
  LGContextModel *context;

  for (i = 0; i < nexus_data_list_length_get(model->contexts); i++) {
    context = (LGContextModel *)nexus_data_list_pointer_at(model->contexts, i);

    if (nexus_strings_string_equals(context->name, name)) {
      return context;
    }
  }

  return NULL;
}

static int64 l_genesis_expand_integer(LGExpander *expander, const LGFrame *frame, LGExpr *expression, int64 fallback, const char *what) {
  LGExpr           *value;
  PharosDiagnostic *diagnostic;

  if (expression == NULL) {
    return fallback;
  }

  value = l_genesis_expand_expression(expander, frame, NULL, expression);

  if (value->kind != LGE_INT) {
    diagnostic = pharos_diagnostics_report((expander->compiler)->reporting, PDS_ERROR, "G0308", expression->span, "%s must be a number", what);
    pharos_diagnostics_label_add(diagnostic, expression->span, TRUE, "found %s", l_genesis_describe_value(value));
    expander->ok = FALSE;

    return fallback;
  }

  return value->integer;
}

/* Summary: the declared name of a token, fragment or rule definition once parameters and hygiene are applied. */
static const char *l_genesis_declared_name(LGExpander *expander, const LGFrame *frame, const LGDecl *decl) {
  const LGBind     *bind;
  PharosDiagnostic *diagnostic;

  if (decl->name_param != NULL) {
    bind = l_genesis_frame_bind(frame, decl->name_param->name);

    if (bind == NULL || (bind->value->kind != LGE_TOKEN && bind->value->kind != LGE_RULE)) {
      diagnostic = pharos_diagnostics_report((expander->compiler)->reporting, PDS_ERROR, "G0309", decl->span,
                                             "the name `$$%s` is not bound to a token or rule name", decl->name_param->name);
      pharos_diagnostics_label_add(diagnostic, decl->span, TRUE, "declared here");
      expander->ok = FALSE;

      return decl->name;
    }

    return bind->value->name;
  }

  return l_genesis_frame_rename(frame, decl->sigil == '#' ? '#' : '$', decl->name);
}

static void l_genesis_report_duplicate(LGExpander *expander, const char *what, const char *name, PharosSpan span, PharosSpan earlier) {
  PharosDiagnostic *diagnostic;

  diagnostic =
      pharos_diagnostics_report((expander->compiler)->reporting, PDS_ERROR, "G0201", span, "the %s `%s` is defined more than once", what, name);
  pharos_diagnostics_label_add(diagnostic, span, TRUE, "redefined here");
  pharos_diagnostics_label_add(diagnostic, earlier, FALSE, "first defined here");
  pharos_diagnostics_help_add(diagnostic, "rename one of them, or remove the duplicate");
  expander->ok = FALSE;
}

static boolean l_genesis_expand_token(LGExpander *expander, const LGFrame *frame, const LGOutputs *outputs, const LGDecl *decl) {
  LGToken          *token;
  const char       *name;
  uint_large        i;
  PharosDiagnostic *diagnostic;

  if (outputs == NULL || outputs->context == NULL) {
    diagnostic = pharos_diagnostics_report((expander->compiler)->reporting, PDS_ERROR, "G0310", decl->span,
                                           "a token or fragment can only be declared inside a CONTEXT");
    pharos_diagnostics_label_add(diagnostic, decl->span, TRUE, "declared outside any context");
    pharos_diagnostics_help_add(diagnostic, "move this definition into a `CONTEXT <ROOT> { ... }` block");
    expander->ok = FALSE;

    return FALSE;
  }

  name = l_genesis_declared_name(expander, frame, decl);

  for (i = 0; i < nexus_data_list_length_get(outputs->context->tokens); i++) {
    const LGToken *existing;

    existing = (const LGToken *)nexus_data_list_pointer_at(outputs->context->tokens, i);

    if (existing->fragment == (decl->kind == LGD_FRAGMENT) && nexus_strings_string_equals(existing->name, name)) {
      l_genesis_report_duplicate(expander, decl->kind == LGD_FRAGMENT ? "fragment" : "token", name, decl->span, existing->span);

      return FALSE;
    }
  }

  token              = (LGToken *)l_genesis_expand_allocate(expander, NEXUS_SIZEOF(*token));
  token->name        = l_genesis_expand_text(expander, name);
  token->fragment    = decl->kind == LGD_FRAGMENT;
  token->expression  = l_genesis_expand_expression(expander, frame, outputs, decl->expression);
  token->priority    = l_genesis_expand_integer(expander, frame, decl->priority, 0, "a priority");
  token->action      = decl->action;
  token->target      = decl->action_target;
  token->amount      = decl->action_amount;
  token->span        = decl->span;
  token->action_span = decl->action_span;
  (void)nexus_data_list_append_pointer(outputs->context->tokens, token);

  return TRUE;
}

static boolean l_genesis_expand_rule(LGExpander *expander, const LGFrame *frame, const LGOutputs *outputs, const LGDecl *decl) {
  LGRuleModel *rule;
  const char  *name;
  uint_large   i;
  uint32       j;
  uint32       k;

  name = l_genesis_declared_name(expander, frame, decl);

  for (i = 0; i < nexus_data_list_length_get(expander->model->rules); i++) {
    const LGRuleModel *existing;

    existing = (const LGRuleModel *)nexus_data_list_pointer_at(expander->model->rules, i);

    if (nexus_strings_string_equals(existing->name, name)) {
      l_genesis_report_duplicate(expander, "rule", name, decl->span, existing->span);

      return FALSE;
    }
  }

  rule             = (LGRuleModel *)l_genesis_expand_allocate(expander, NEXUS_SIZEOF(*rule));
  rule->name       = l_genesis_expand_text(expander, name);
  rule->span       = decl->span;
  rule->expression = l_genesis_expand_expression(expander, frame, outputs, decl->expression);

  if (decl->annotation_count > 0) {
    rule->annotations      = (LGAnnotation *)l_genesis_expand_allocate(expander, decl->annotation_count * NEXUS_SIZEOF(LGAnnotation));
    rule->annotation_count = decl->annotation_count;

    for (j = 0; j < decl->annotation_count; j++) {
      rule->annotations[j] = decl->annotations[j];
      rule->annotations[j].arguments =
          (LGExpr **)l_genesis_expand_allocate(expander, (decl->annotations[j].argument_count + 1) * NEXUS_EFFECTIVE_POINTER_SIZE_BYTES);

      for (k = 0; k < decl->annotations[j].argument_count; k++) {
        rule->annotations[j].arguments[k] = l_genesis_expand_expression(expander, frame, outputs, decl->annotations[j].arguments[k]);
      }
    }
  }

  (void)nexus_data_list_append_pointer(expander->model->rules, rule);

  return TRUE;
}

static boolean l_genesis_expand_entry(LGExpander *expander, const LGFrame *frame, const LGOutputs *outputs, const LGDecl *decl) {
  LGEntryModel     *entry;
  LGExpr           *token;
  PharosDiagnostic *diagnostic;

  if (outputs == NULL || outputs->pratt == NULL) {
    diagnostic = pharos_diagnostics_report((expander->compiler)->reporting, PDS_ERROR, "G0311", decl->span,
                                           "an operator entry can only appear inside a PRATT block");
    pharos_diagnostics_label_add(diagnostic, decl->span, TRUE, "not inside a PRATT block");
    expander->ok = FALSE;

    return FALSE;
  }

  token = l_genesis_expand_expression(expander, frame, outputs, decl->entry_token);

  if (token->kind != LGE_TOKEN) {
    diagnostic =
        pharos_diagnostics_report((expander->compiler)->reporting, PDS_ERROR, "G0312", decl->entry_token->span, "an operator must be a `$` token");
    pharos_diagnostics_label_add(diagnostic, decl->entry_token->span, TRUE, "found %s", l_genesis_describe_value(token));
    expander->ok = FALSE;

    return FALSE;
  }

  entry           = (LGEntryModel *)l_genesis_expand_allocate(expander, NEXUS_SIZEOF(*entry));
  entry->kind     = decl->entry_kind;
  entry->token    = token;
  entry->span     = decl->span;
  entry->left_bp  = l_genesis_expand_integer(expander, frame, decl->left_bp, 0, "`left_bp`");
  entry->right_bp = l_genesis_expand_integer(expander, frame, decl->right_bp, 0, "`right_bp`");

  if (nexus_strings_string_equals(entry->kind, "infix") && (decl->left_bp == NULL || decl->right_bp == NULL)) {
    diagnostic = pharos_diagnostics_report((expander->compiler)->reporting, PDS_ERROR, "G0313", decl->span,
                                           "an infix operator needs both `left_bp` and `right_bp`");
    pharos_diagnostics_label_add(diagnostic, decl->span, TRUE, "binding powers are missing");
    pharos_diagnostics_help_add(diagnostic, "write `infix $op (left_bp: 30, right_bp: 31)` for a left-associative operator");
    expander->ok = FALSE;
  } else if (nexus_strings_string_equals(entry->kind, "prefix") && decl->right_bp == NULL) {
    diagnostic = pharos_diagnostics_report((expander->compiler)->reporting, PDS_ERROR, "G0313", decl->span, "a prefix operator needs `right_bp`");
    pharos_diagnostics_label_add(diagnostic, decl->span, TRUE, "the binding power is missing");
    expander->ok = FALSE;
  } else if (nexus_strings_string_equals(entry->kind, "postfix") && decl->left_bp == NULL) {
    diagnostic = pharos_diagnostics_report((expander->compiler)->reporting, PDS_ERROR, "G0313", decl->span, "a postfix operator needs `left_bp`");
    pharos_diagnostics_label_add(diagnostic, decl->span, TRUE, "the binding power is missing");
    expander->ok = FALSE;
  }

  (void)nexus_data_list_append_pointer(outputs->pratt->entries, entry);

  return TRUE;
}

static boolean l_genesis_expand_pratt(LGExpander *expander, const LGFrame *frame, const LGOutputs *outputs, const LGDecl *decl) {
  LGPrattModel *pratt;
  LGOutputs     inner;
  const char   *name;
  uint32        i;
  uint_large    k;

  name = l_genesis_declared_name(expander, frame, decl);

  for (k = 0; k < nexus_data_list_length_get(expander->model->rules); k++) {
    const LGRuleModel *existing;

    existing = (const LGRuleModel *)nexus_data_list_pointer_at(expander->model->rules, k);

    if (nexus_strings_string_equals(existing->name, name)) {
      l_genesis_report_duplicate(expander, "rule", name, decl->span, existing->span);

      return FALSE;
    }
  }

  pratt          = (LGPrattModel *)l_genesis_expand_allocate(expander, NEXUS_SIZEOF(*pratt));
  pratt->name    = l_genesis_expand_text(expander, name);
  pratt->entries = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 8);
  pratt->span    = decl->span;
  (void)nexus_data_list_append_pointer(expander->model->pratts, pratt);

  inner.context = outputs != NULL ? outputs->context : NULL;
  inner.pratt   = pratt;

  for (i = 0; i < decl->child_count; i++) {
    const LGDecl *child;

    child = decl->children[i];

    if (child->kind == LGD_RULE && child->name != NULL && nexus_strings_string_equals(child->name, "atom") && child->sigil == 0) {
      pratt->atom = l_genesis_expand_expression(expander, frame, &inner, child->expression);
      continue;
    }

    (void)l_genesis_expand_declaration(expander, frame, &inner, child);
  }

  if (pratt->atom == NULL) {
    PharosDiagnostic *diagnostic;

    diagnostic =
        pharos_diagnostics_report((expander->compiler)->reporting, PDS_ERROR, "G0314", decl->span, "the PRATT rule `#%s` has no `atom`", name);
    pharos_diagnostics_label_add(diagnostic, decl->span, TRUE, "operands are undefined");
    pharos_diagnostics_help_add(diagnostic, "add `atom = #some_rule` to say what an operand is");
    expander->ok = FALSE;
  }

  return TRUE;
}

static boolean l_genesis_expand_embed(LGExpander *expander, const LGFrame *frame, const LGOutputs *outputs, const LGDecl *decl) {
  LGEmbedModel *embed;
  const char   *name;
  uint32        i;

  name          = l_genesis_declared_name(expander, frame, decl);
  embed         = (LGEmbedModel *)l_genesis_expand_allocate(expander, NEXUS_SIZEOF(*embed));
  embed->name   = l_genesis_expand_text(expander, name);
  embed->span   = decl->span;
  embed->count  = decl->name_count;
  embed->keys   = (const char **)l_genesis_expand_allocate(expander, (decl->name_count + 1) * NEXUS_EFFECTIVE_POINTER_SIZE_BYTES);
  embed->values = (LGExpr **)l_genesis_expand_allocate(expander, (decl->name_count + 1) * NEXUS_EFFECTIVE_POINTER_SIZE_BYTES);

  for (i = 0; i < decl->name_count; i++) {
    embed->keys[i]   = decl->names[i];
    embed->values[i] = l_genesis_expand_expression(expander, frame, outputs, decl->values[i]);
  }

  (void)nexus_data_list_append_pointer(expander->model->embeds, embed);

  return TRUE;
}

static boolean l_genesis_expand_declaration(LGExpander *expander, const LGFrame *frame, const LGOutputs *outputs, const LGDecl *decl) {
  uint32 i;

  switch (decl->kind) {
  case LGD_TOKEN:
  case LGD_FRAGMENT:
    return l_genesis_expand_token(expander, frame, outputs, decl);
  case LGD_RULE:
    return l_genesis_expand_rule(expander, frame, outputs, decl);
  case LGD_ENTRY:
    return l_genesis_expand_entry(expander, frame, outputs, decl);
  case LGD_PRATT:
    return l_genesis_expand_pratt(expander, frame, outputs, decl);
  case LGD_EMBED:
    return l_genesis_expand_embed(expander, frame, outputs, decl);
  case LGD_CALL:
    (void)l_genesis_instantiate(expander, frame, outputs, decl->call, FALSE);

    return expander->ok;
  case LGD_IF:
    if (l_genesis_compare(expander, frame, decl->condition)) {
      for (i = 0; i < decl->child_count; i++) {
        (void)l_genesis_expand_declaration(expander, frame, outputs, decl->children[i]);
      }
    } else {
      for (i = 0; i < decl->else_count; i++) {
        (void)l_genesis_expand_declaration(expander, frame, outputs, decl->else_children[i]);
      }
    }

    return expander->ok;
  case LGD_IMPORT:
  case LGD_ENUM:
  case LGD_PARSER:
  case LGD_CONTEXT:
  case LGD_TEMPLATE:
  default:
    return TRUE;
  }
}

/* ---------------------------------------------------------------------------- */
/* TEMPLATE INSTANTIATION                                                        */
/* ---------------------------------------------------------------------------- */

/* Summary: the declarations of a template body that are active under the bindings, flattening declaration-level @if. */
static void l_genesis_active_declarations(LGExpander *expander, const LGFrame *frame, LGDecl *const *decls, uint32 count, NexusDataList *out) {
  uint32 i;
  uint32 j;

  for (i = 0; i < count; i++) {
    if (decls[i]->kind == LGD_IF) {
      if (l_genesis_compare(expander, frame, decls[i]->condition)) {
        l_genesis_active_declarations(expander, frame, decls[i]->children, decls[i]->child_count, out);
      } else {
        l_genesis_active_declarations(expander, frame, decls[i]->else_children, decls[i]->else_count, out);
      }

      continue;
    }

    (void)j;
    (void)nexus_data_list_append_pointer(out, decls[i]);
  }
}

static const char *l_genesis_template_types(LGExpander *expander, const LGParam *param) {
  NexusDataList *buffer;
  char           terminator;
  char          *text;
  uint32         i;

  buffer     = nexus_data_list_create(NEXUS_SIZEOF(char), 32);
  terminator = '\0';

  for (i = 0; i < param->type_count; i++) {
    if (i > 0) {
      l_genesis_print_text(buffer, " | ");
    }

    l_genesis_print_text(buffer, param->types[i]);
  }

  if (param->optional) {
    l_genesis_print_text(buffer, "?");
  }

  (void)nexus_data_list_append(buffer, &terminator);
  text = l_genesis_expand_text(expander, (const char *)nexus_data_list_data_get_const(buffer));
  nexus_data_list_destroy(buffer);

  return text;
}

static LGExpr *l_genesis_instantiate(LGExpander *expander, const LGFrame *caller, const LGOutputs *outputs, const LGExpr *call,
                                     boolean as_expression) {
  LGTemplateRef     reference;
  LGFrame           frame;
  LGBind           *binds;
  LGExpr          **arguments;
  boolean          *used;
  const LGDecl     *template_decl;
  NexusDataList    *active;
  PharosDiagnostic *diagnostic;
  LGExpr           *result;
  char              sigil;
  char             *key;
  uint32            i;
  uint32            j;
  uint_large        k;
  boolean           found;
  boolean           matched;
  const char      **candidates;
  const char       *suggestion;

  sigil = call->sigil;

  if (caller->depth >= LG_EXPAND_DEPTH_MAX) {
    diagnostic = pharos_diagnostics_report((expander->compiler)->reporting, PDS_ERROR, "G0306", call->span,
                                           "template expansion is nested more than %u levels deep", (unsigned)LG_EXPAND_DEPTH_MAX);
    pharos_diagnostics_label_add(diagnostic, call->span, TRUE, "while expanding `%c%s`", sigil, call->name);
    pharos_diagnostics_note_add(diagnostic, "a template that invokes itself without making progress never finishes expanding");
    expander->ok = FALSE;

    return l_genesis_expand_new(expander, LGE_EMPTY, call->span);
  }

  if (!l_genesis_find_template(&caller->scope, sigil, call->name, &reference)) {
    diagnostic = pharos_diagnostics_report((expander->compiler)->reporting, PDS_ERROR, "G0301", call->span, "cannot find the template `%c%s`", sigil,
                                           call->name);
    pharos_diagnostics_label_add(diagnostic, call->span, TRUE, "no template with this name is in scope");

    {
      NexusDataList *names;
      uint32         index;
      char          *text;
      uint_large     length;

      names = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 8);

      if (caller->scope.unit != NULL) {
        for (index = 0; index < caller->scope.unit->decl_count; index++) {
          if (caller->scope.unit->decls[index]->kind == LGD_TEMPLATE) {
            length = nexus_strings_string_length_get(caller->scope.unit->decls[index]->name);
            text   = (char *)l_genesis_expand_allocate(expander, length + 1);
            nexus_memory_bytes_copy(caller->scope.unit->decls[index]->name, text, length + 1);
            (void)nexus_data_list_append_pointer(names, text);
          }
        }
      }

      if (caller->scope.imports != NULL) {
        for (k = 0; k < nexus_data_list_length_get(caller->scope.imports); k++) {
          const LGImport *import;

          import = (const LGImport *)nexus_data_list_pointer_at(caller->scope.imports, k);

          for (index = 0; index < import->item_count; index++) {
            if (import->items[index][0] == sigil) {
              (void)nexus_data_list_append_pointer(names, import->items[index] + 1);
            }
          }
        }
      }

      suggestion =
          nexus_strings_string_closest(call->name, (const char *const *)nexus_data_list_data_get_const(names), nexus_data_list_length_get(names));

      if (suggestion != NULL) {
        pharos_diagnostics_help_add(diagnostic, "did you mean `%c%s`?", sigil, suggestion);
      } else {
        pharos_diagnostics_help_add(diagnostic, "import it from a library: `IMPORT Library { %c%s }`", sigil, call->name);
      }

      nexus_data_list_destroy(names);
    }

    expander->ok = FALSE;

    return l_genesis_expand_new(expander, LGE_EMPTY, call->span);
  }

  template_decl = reference.decl;

  /* The result type must fit how the call is used. */
  if (as_expression && (nexus_strings_string_equals(template_decl->result_type, "RuleDefinition") ||
                        nexus_strings_string_equals(template_decl->result_type, "TokenDefinition") ||
                        nexus_strings_string_equals(template_decl->result_type, "PrattEntry"))) {
    diagnostic =
        pharos_diagnostics_report((expander->compiler)->reporting, PDS_ERROR, "G0315", call->span,
                                  "`%c%s` produces a %s and cannot be used inside an expression", sigil, call->name, template_decl->result_type);
    pharos_diagnostics_label_add(diagnostic, call->span, TRUE, "used as an expression");
    pharos_diagnostics_label_add(diagnostic, template_decl->span, FALSE, "declared to produce `%s`", template_decl->result_type);
    expander->ok = FALSE;

    return l_genesis_expand_new(expander, LGE_EMPTY, call->span);
  }

  if (!as_expression && (nexus_strings_string_equals(template_decl->result_type, "GrammarExpression") ||
                         nexus_strings_string_equals(template_decl->result_type, "LexicalExpression"))) {
    diagnostic = pharos_diagnostics_report((expander->compiler)->reporting, PDS_ERROR, "G0315", call->span,
                                           "`%c%s` produces an expression, not a declaration", sigil, call->name);
    pharos_diagnostics_label_add(diagnostic, call->span, TRUE, "used as a declaration");
    pharos_diagnostics_label_add(diagnostic, template_decl->span, FALSE, "declared to produce `%s`", template_decl->result_type);
    pharos_diagnostics_help_add(diagnostic, "use it inside a rule or token definition, for example `#name = %c%s(...)`", sigil, call->name);
    expander->ok = FALSE;

    return l_genesis_expand_new(expander, LGE_EMPTY, call->span);
  }

  /* Arguments are expanded in the caller's frame first, then bound and checked against the parameters. */
  arguments = (LGExpr **)l_genesis_expand_allocate(expander, (call->item_count + 1) * NEXUS_EFFECTIVE_POINTER_SIZE_BYTES);
  used      = (boolean *)l_genesis_expand_allocate(expander, (call->item_count + 1) * NEXUS_SIZEOF(boolean));

  for (i = 0; i < call->item_count; i++) {
    arguments[i] = l_genesis_expand_expression(expander, caller, outputs, call->items[i]);
  }

  binds = (LGBind *)l_genesis_expand_allocate(expander, (template_decl->param_count + 1) * NEXUS_SIZEOF(LGBind));

  frame.scope         = reference.scope;
  frame.binds         = binds;
  frame.bind_count    = 0;
  frame.renames       = NULL;
  frame.template_decl = template_decl;
  frame.call_span     = call->span;
  frame.depth         = caller->depth + 1;

  for (i = 0; i < call->item_count; i++) {
    if (call->arg_names[i] == NULL) {
      diagnostic =
          pharos_diagnostics_report((expander->compiler)->reporting, PDS_ERROR, "G0316", call->items[i]->span, "template arguments must be named");
      pharos_diagnostics_label_add(diagnostic, call->items[i]->span, TRUE, "write `name: value`");
      expander->ok = FALSE;
      used[i]      = TRUE;
      continue;
    }

    found = FALSE;

    for (j = 0; j < template_decl->param_count; j++) {
      if (nexus_strings_string_equals(template_decl->params[j].name, call->arg_names[i])) {
        found = TRUE;
        break;
      }
    }

    if (!found) {
      diagnostic = pharos_diagnostics_report((expander->compiler)->reporting, PDS_ERROR, "G0303", call->arg_spans[i],
                                             "`%c%s` has no parameter named `%s`", sigil, call->name, call->arg_names[i]);
      pharos_diagnostics_label_add(diagnostic, call->arg_spans[i], TRUE, "unknown parameter");
      pharos_diagnostics_label_add(diagnostic, template_decl->span, FALSE, "the template is declared here");
      candidates = (const char **)l_genesis_expand_allocate(expander, (template_decl->param_count + 1) * NEXUS_EFFECTIVE_POINTER_SIZE_BYTES);

      for (j = 0; j < template_decl->param_count; j++) {
        candidates[j] = template_decl->params[j].name;
      }

      suggestion = nexus_strings_string_closest(call->arg_names[i], candidates, template_decl->param_count);

      if (suggestion != NULL) {
        pharos_diagnostics_help_add(diagnostic, "did you mean `%s`?", suggestion);
      }

      expander->ok = FALSE;
      used[i]      = TRUE;
    }
  }

  for (j = 0; j < template_decl->param_count; j++) {
    const LGParam *param;
    LGExpr        *value;

    param = &template_decl->params[j];
    value = NULL;

    for (i = 0; i < call->item_count; i++) {
      if (call->arg_names[i] != NULL && nexus_strings_string_equals(call->arg_names[i], param->name)) {
        if (value != NULL) {
          diagnostic = pharos_diagnostics_report((expander->compiler)->reporting, PDS_ERROR, "G0317", call->arg_spans[i],
                                                 "the argument `%s` is given twice", param->name);
          pharos_diagnostics_label_add(diagnostic, call->arg_spans[i], TRUE, "given again here");
          expander->ok = FALSE;
        }

        value   = arguments[i];
        used[i] = TRUE;
      }
    }

    if (value == NULL) {
      if (param->default_value != NULL) {
        binds[frame.bind_count].name  = param->name;
        binds[frame.bind_count].param = param;
        binds[frame.bind_count].value = NULL;
        frame.bind_count++;
        value = l_genesis_expand_expression(expander, &frame, outputs, param->default_value);
        frame.bind_count--;
      } else if (param->optional) {
        value = l_genesis_expand_new(expander, LGE_NONE, call->span);
      } else {
        diagnostic = pharos_diagnostics_report((expander->compiler)->reporting, PDS_ERROR, "G0302", call->span,
                                               "missing argument `%s` in the call of `%c%s`", param->name, sigil, call->name);
        pharos_diagnostics_label_add(diagnostic, call->span, TRUE, "`%s` is required", param->name);
        pharos_diagnostics_label_add(diagnostic, param->span, FALSE, "parameter declared here, of type `%s`",
                                     l_genesis_template_types(expander, param));
        pharos_diagnostics_help_add(diagnostic, "add `%s: ...` to the call, or give the parameter a default with `= value`", param->name);
        expander->ok = FALSE;
        value        = l_genesis_expand_new(expander, LGE_EMPTY, call->span);
      }
    } else {
      matched = FALSE;

      if (value->kind == LGE_NONE) {
        matched = param->optional;
      }

      for (i = 0; i < param->type_count && !matched; i++) {
        matched = l_genesis_type_accepts(&frame.scope, param->types[i], value);
      }

      if (!matched) {
        PharosSpan value_span;

        value_span = value->span;

        for (i = 0; i < call->item_count; i++) {
          if (call->arg_names[i] != NULL && nexus_strings_string_equals(call->arg_names[i], param->name)) {
            value_span = call->items[i]->span;
          }
        }

        diagnostic = pharos_diagnostics_report((expander->compiler)->reporting, PDS_ERROR, "G0304", value_span,
                                               "mismatched types for the argument `%s` of `%c%s`", param->name, sigil, call->name);
        pharos_diagnostics_label_add(diagnostic, value_span, TRUE, "expected `%s`, found %s `%s`", l_genesis_template_types(expander, param),
                                     l_genesis_describe_value(value), l_genesis_expand_render(expander, value));
        pharos_diagnostics_label_add(diagnostic, param->span, FALSE, "parameter declared here");

        if (value->kind == LGE_NONE) {
          pharos_diagnostics_help_add(diagnostic, "`%s` is required; only parameters declared with a trailing `?` accept `none`", param->name);
        }

        expander->ok = FALSE;
      }
    }

    binds[frame.bind_count].name  = param->name;
    binds[frame.bind_count].param = param;
    binds[frame.bind_count].value = value;
    frame.bind_count++;
  }

  if (!expander->ok) {
    return l_genesis_expand_new(expander, LGE_EMPTY, call->span);
  }

  /* The helpers the body declares get instance-unique names; names declared through a $$parameter keep the caller's name. */
  expander->model->next_instance++;
  frame.renames = nexus_data_list_create(NEXUS_SIZEOF(LGRename), 8);
  active        = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 8);
  l_genesis_active_declarations(expander, &frame, template_decl->children, template_decl->child_count, active);

  for (k = 0; k < nexus_data_list_length_get(active); k++) {
    const LGDecl *body;
    LGRename      rename;
    char          suffix[24];
    uint_large    length;
    char         *renamed;

    body = (const LGDecl *)nexus_data_list_pointer_at(active, k);

    if ((body->kind != LGD_TOKEN && body->kind != LGD_FRAGMENT && body->kind != LGD_RULE && body->kind != LGD_PRATT && body->kind != LGD_EMBED) ||
        body->name_param != NULL || body->name == NULL) {
      continue;
    }

    (void)stbsp_snprintf(suffix, (int)sizeof(suffix), "@%u", (unsigned)expander->model->next_instance);
    length  = nexus_strings_string_length_get(body->name);
    renamed = (char *)l_genesis_expand_allocate(expander, length + nexus_strings_string_length_get(suffix) + 1);
    nexus_memory_bytes_copy(body->name, renamed, length);
    nexus_memory_bytes_copy(suffix, renamed + length, nexus_strings_string_length_get(suffix) + 1);
    rename.sigil = body->kind == LGD_RULE || body->kind == LGD_PRATT || body->kind == LGD_EMBED ? '#' : '$';
    rename.from  = body->name;
    rename.to    = renamed;
    (void)nexus_data_list_append(frame.renames, &rename);
  }

  /* An expression template with identical arguments in the same place reuses its earlier instance. */
  key = NULL;

  if (as_expression) {
    NexusDataList *buffer;
    char           terminator;
    char           pointer_text[40];

    buffer     = nexus_data_list_create(NEXUS_SIZEOF(char), 64);
    terminator = '\0';
    (void)stbsp_snprintf(pointer_text, (int)sizeof(pointer_text), "%p|", (const void *)(outputs != NULL ? outputs->context : NULL));
    l_genesis_print_text(buffer, pointer_text);

    for (j = 0; j < frame.bind_count; j++) {
      l_genesis_print_text(buffer, binds[j].name);
      l_genesis_print_text(buffer, "=");
      l_genesis_print(buffer, binds[j].value);
      l_genesis_print_text(buffer, ";");
    }

    (void)nexus_data_list_append(buffer, &terminator);
    key = l_genesis_expand_text(expander, (const char *)nexus_data_list_data_get_const(buffer));
    nexus_data_list_destroy(buffer);

    for (k = 0; k < nexus_data_list_length_get(expander->memo); k++) {
      const LGMemo *memo;

      memo = (const LGMemo *)nexus_data_list_at_const(expander->memo, k);

      if (memo->template_decl == template_decl && nexus_strings_string_equals(memo->key, key)) {
        nexus_data_list_destroy(frame.renames);
        nexus_data_list_destroy(active);

        return memo->result;
      }
    }
  }

  result = l_genesis_expand_new(expander, LGE_EMPTY, call->span);

  for (k = 0; k < nexus_data_list_length_get(active); k++) {
    (void)l_genesis_expand_declaration(expander, &frame, outputs, (const LGDecl *)nexus_data_list_pointer_at(active, k));
  }

  if (as_expression) {
    LGMemo     memo;
    LGExprKind result_kind;

    /* The namesake: the helper whose name is the template's own. */
    found = FALSE;

    for (k = 0; k < nexus_data_list_length_get(active); k++) {
      const LGDecl *body;

      body = (const LGDecl *)nexus_data_list_pointer_at(active, k);

      if (body->name != NULL && body->name_param == NULL && body->sigil == sigil && nexus_strings_string_equals(body->name, template_decl->name)) {
        result_kind = LGE_TOKEN;

        if (sigil == '#') {
          result_kind = LGE_RULE;
        } else if (template_decl->name[0] == '_') {
          result_kind = LGE_FRAGMENT;
        }

        result        = l_genesis_expand_new(expander, result_kind, call->span);
        result->name  = l_genesis_frame_rename(&frame, sigil == '#' ? '#' : '$', body->name);
        result->sigil = sigil;
        found         = TRUE;
        break;
      }
    }

    if (!found) {
      diagnostic =
          pharos_diagnostics_report((expander->compiler)->reporting, PDS_ERROR, "G0305", call->span,
                                    "`%c%s` does not define a `%c%s` to stand for it in an expression", sigil, call->name, sigil, call->name);
      pharos_diagnostics_label_add(diagnostic, call->span, TRUE, "used as an expression");
      pharos_diagnostics_label_add(diagnostic, template_decl->span, FALSE, "the body declares no `%c%s`", sigil, template_decl->name);
      pharos_diagnostics_help_add(diagnostic, "an expression template exposes the definition named like the template itself");
      expander->ok = FALSE;
    }

    memo.template_decl = template_decl;
    memo.key           = key;
    memo.result        = result;
    (void)nexus_data_list_append(expander->memo, &memo);
  }

  nexus_data_list_destroy(frame.renames);
  nexus_data_list_destroy(active);

  return result;
}

/* ---------------------------------------------------------------------------- */
/* LANGUAGE EXPANSION                                                            */
/* ---------------------------------------------------------------------------- */

LGModel *logos_languages_genesis_template_expand(LGenesisCompiler *compiler, NexusDataArena *arena, LGUnit *unit, NexusDataList *imports) {
  LGExpander expander;
  LGModel   *model;
  LGFrame    frame;
  LGOutputs  outputs;
  LGDecl    *decl;
  uint32     i;
  uint32     j;

  NEXUS_PROGRAM_PRECONDITION(compiler != NULL && arena != NULL && unit != NULL && imports != NULL, "arguments must not be NULL");

  model = (LGModel *)nexus_memory_allocate(NEXUS_SIZEOF(*model));
  nexus_memory_bytes_clear(model, NEXUS_SIZEOF(*model));
  model->unit     = unit;
  model->arena    = arena;
  model->contexts = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 4);
  model->rules    = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 16);
  model->pratts   = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 2);
  model->embeds   = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 2);
  model->enums    = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 2);
  model->imports  = imports;

  expander.compiler = compiler;
  expander.arena    = arena;
  expander.model    = model;
  expander.memo     = nexus_data_list_create(NEXUS_SIZEOF(LGMemo), 8);
  expander.ok       = TRUE;

  frame.scope.unit    = unit;
  frame.scope.imports = imports;
  frame.binds         = NULL;
  frame.bind_count    = 0;
  frame.renames       = NULL;
  frame.template_decl = NULL;
  frame.call_span     = l_genesis_span_none();
  frame.depth         = 0;

  for (i = 0; i < unit->decl_count; i++) {
    decl = unit->decls[i];

    switch (decl->kind) {
    case LGD_PARSER:
      if (model->has_parser) {
        PharosDiagnostic *diagnostic;

        diagnostic = pharos_diagnostics_report((compiler)->reporting, PDS_ERROR, "G0202", decl->span, "a language has one PARSER block");
        pharos_diagnostics_label_add(diagnostic, decl->span, TRUE, "second PARSER block");
        pharos_diagnostics_label_add(diagnostic, model->parser_span, FALSE, "first PARSER block");
        expander.ok = FALSE;
        break;
      }

      model->has_parser    = TRUE;
      model->parser_span   = decl->span;
      model->parser_keys   = decl->names;
      model->parser_values = decl->values;
      model->parser_count  = decl->name_count;
      break;
    case LGD_CONTEXT: {
      LGContextModel *context;

      context = l_genesis_context_find(model, decl->name);

      if (context != NULL) {
        l_genesis_report_duplicate(&expander, "context", decl->name, decl->span, context->span);
        break;
      }

      context         = (LGContextModel *)nexus_data_arena_allocate(arena, NEXUS_SIZEOF(*context));
      context->name   = l_genesis_expand_text(&expander, decl->name);
      context->span   = decl->span;
      context->tokens = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 16);
      (void)nexus_data_list_append_pointer(model->contexts, context);
      outputs.context = context;
      outputs.pratt   = NULL;

      for (j = 0; j < decl->child_count; j++) {
        (void)l_genesis_expand_declaration(&expander, &frame, &outputs, decl->children[j]);
      }
    } break;
    case LGD_ENUM: {
      LGEnumModel *enumeration;

      enumeration          = (LGEnumModel *)nexus_data_arena_allocate(arena, NEXUS_SIZEOF(*enumeration));
      enumeration->name    = l_genesis_expand_text(&expander, decl->name);
      enumeration->members = decl->names;
      enumeration->count   = decl->name_count;
      (void)nexus_data_list_append_pointer(model->enums, enumeration);
    } break;
    case LGD_RULE:
    case LGD_PRATT:
    case LGD_EMBED:
    case LGD_CALL:
    case LGD_TOKEN:
    case LGD_FRAGMENT:
      outputs.context = NULL;
      outputs.pratt   = NULL;
      (void)l_genesis_expand_declaration(&expander, &frame, &outputs, decl);
      break;
    case LGD_IMPORT:
    case LGD_TEMPLATE:
    case LGD_ENTRY:
    case LGD_IF:
    default:
      break;
    }
  }

  nexus_data_list_destroy(expander.memo);

  if (!expander.ok) {
    /* The model is incomplete: nothing downstream should run on it. */
    model->imports = NULL;
    logos_languages_genesis_model_destroy(model);

    return NULL;
  }

  return model;
}

/* ---------------------------------------------------------------------------- */
/* QUERIES AND TEARDOWN                                                          */
/* ---------------------------------------------------------------------------- */

boolean logos_languages_genesis_template_visible(const LGUnit *unit, const NexusDataList *imports, char sigil, const char *name) {
  LGScope       scope;
  LGTemplateRef reference;

  scope.unit    = unit;
  scope.imports = imports;

  return l_genesis_find_template(&scope, sigil, name, &reference);
}

boolean logos_languages_genesis_enum_visible(const LGUnit *unit, const NexusDataList *imports, const char *name, const char *const **out_members,
                                             uint32 *out_count) {
  LGScope            scope;
  const char *const *members;
  uint32             count;

  scope.unit    = unit;
  scope.imports = imports;

  if (!l_genesis_find_enum(&scope, name, &members, &count)) {
    return FALSE;
  }

  if (out_members != NULL) {
    *out_members = members;
  }

  if (out_count != NULL) {
    *out_count = count;
  }

  return TRUE;
}

void logos_languages_genesis_model_destroy(LGModel *model) {
  uint_large i;

  if (model == NULL) {
    return;
  }

  for (i = 0; i < nexus_data_list_length_get(model->contexts); i++) {
    nexus_data_list_destroy(((LGContextModel *)nexus_data_list_pointer_at(model->contexts, i))->tokens);
  }

  for (i = 0; i < nexus_data_list_length_get(model->pratts); i++) {
    nexus_data_list_destroy(((LGPrattModel *)nexus_data_list_pointer_at(model->pratts, i))->entries);
  }

  nexus_data_list_destroy(model->contexts);
  nexus_data_list_destroy(model->rules);
  nexus_data_list_destroy(model->pratts);
  nexus_data_list_destroy(model->embeds);
  nexus_data_list_destroy(model->enums);
  nexus_memory_deallocate(model);
}
