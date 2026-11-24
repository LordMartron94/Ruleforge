#include <nexus.h>

#include "../logos.h"
#include "./l_internal.h"

/*
Analysis validates what the compiler could otherwise only discover as a malformed lexer or a parser that never
terminates: unresolved names, fragments that loop, tokens that match nothing, left recursion, repetitions that make
no progress, and global rules that could match empty input. Every problem is a diagnostic with a code, labelled
spans, and, where a fix is known, a help line.
*/

/* ---------------------------------------------------------------------------- */
/* LOOKUPS                                                                       */
/* ---------------------------------------------------------------------------- */

LGToken *logos_languages_genesis_analysis_token_find(const LGModel *model, const char *name, const LGContextModel **out_context) {
  uint_large i;
  uint_large j;

  for (i = 0; i < nexus_data_list_length_get(model->contexts); i++) {
    LGContextModel *context;

    context = (LGContextModel *)nexus_data_list_pointer_at(model->contexts, i);

    for (j = 0; j < nexus_data_list_length_get(context->tokens); j++) {
      LGToken *token;

      token = (LGToken *)nexus_data_list_pointer_at(context->tokens, j);

      if (!token->fragment && nexus_strings_string_equals(token->name, name)) {
        if (out_context != NULL) {
          *out_context = context;
        }

        return token;
      }
    }
  }

  return NULL;
}

LGToken *logos_languages_genesis_analysis_fragment_find(const LGContextModel *context, const char *name) {
  uint_large j;

  for (j = 0; j < nexus_data_list_length_get(context->tokens); j++) {
    LGToken *token;

    token = (LGToken *)nexus_data_list_pointer_at(context->tokens, j);

    if (token->fragment && nexus_strings_string_equals(token->name, name)) {
      return token;
    }
  }

  return NULL;
}

LGRuleModel *logos_languages_genesis_analysis_rule_find(const LGModel *model, const char *name) {
  uint_large i;

  for (i = 0; i < nexus_data_list_length_get(model->rules); i++) {
    LGRuleModel *rule;

    rule = (LGRuleModel *)nexus_data_list_pointer_at(model->rules, i);

    if (nexus_strings_string_equals(rule->name, name)) {
      return rule;
    }
  }

  return NULL;
}

LGPrattModel *logos_languages_genesis_analysis_pratt_find(const LGModel *model, const char *name) {
  uint_large i;

  for (i = 0; i < nexus_data_list_length_get(model->pratts); i++) {
    LGPrattModel *pratt;

    pratt = (LGPrattModel *)nexus_data_list_pointer_at(model->pratts, i);

    if (nexus_strings_string_equals(pratt->name, name)) {
      return pratt;
    }
  }

  return NULL;
}

LGEmbedModel *logos_languages_genesis_analysis_embed_find(const LGModel *model, const char *name) {
  uint_large i;

  for (i = 0; i < nexus_data_list_length_get(model->embeds); i++) {
    LGEmbedModel *embed;

    embed = (LGEmbedModel *)nexus_data_list_pointer_at(model->embeds, i);

    if (nexus_strings_string_equals(embed->name, name)) {
      return embed;
    }
  }

  return NULL;
}

const LGImport *logos_languages_genesis_analysis_import_find(const LGModel *model, const char *language) {
  uint_large i;

  for (i = 0; i < nexus_data_list_length_get(model->imports); i++) {
    const LGImport *import;

    import = (const LGImport *)nexus_data_list_pointer_at(model->imports, i);

    if (nexus_strings_string_equals(import->alias != NULL ? import->alias : import->name, language)) {
      return import;
    }
  }

  return NULL;
}

/* ---------------------------------------------------------------------------- */
/* LIBRARIES                                                                     */
/* ---------------------------------------------------------------------------- */

static const char *const l_genesis_parameter_types[] = {"Token", "TokenName", "RuleName",    "Expression", "string",
                                                        "int",   "uint",      "LanguageRef", "RuleRef"};

static const char *const l_genesis_result_types[] = {"LexicalExpression", "TokenDefinition", "GrammarExpression", "PrattEntry", "RuleDefinition"};

typedef struct LGLibraryCheck {
  LGenesisCompiler *compiler;
  const LGUnit     *unit;
  NexusDataList    *imports;
  const LGDecl     *template_decl;
  boolean           ok;
} LGLibraryCheck;

static void l_genesis_library_expression(LGLibraryCheck *check, const LGExpr *expression);

static void l_genesis_library_expressions(LGLibraryCheck *check, LGExpr *const *items, uint32 count) {
  uint32 i;

  for (i = 0; i < count; i++) {
    l_genesis_library_expression(check, items[i]);
  }
}

static void l_genesis_library_unknown_parameter(LGLibraryCheck *check, const char *name, PharosSpan span) {
  PharosDiagnostic *diagnostic;
  const char      **candidates;
  const char       *suggestion;
  uint32            i;

  diagnostic = pharos_diagnostics_report((check->compiler)->reporting, PDS_ERROR, "G0109", span, "cannot find the template parameter `$$%s`", name);
  pharos_diagnostics_label_add(diagnostic, span, TRUE, "not a parameter of `%c%s`", check->template_decl->sigil, check->template_decl->name);
  pharos_diagnostics_label_add(diagnostic, check->template_decl->span, FALSE, "the template is declared here");

  if (check->template_decl->param_count > 0) {
    candidates = (const char **)nexus_memory_allocate(check->template_decl->param_count * NEXUS_EFFECTIVE_POINTER_SIZE_BYTES);

    for (i = 0; i < check->template_decl->param_count; i++) {
      candidates[i] = check->template_decl->params[i].name;
    }

    suggestion = nexus_strings_string_closest(name, candidates, check->template_decl->param_count);

    if (suggestion != NULL) {
      pharos_diagnostics_help_add(diagnostic, "did you mean `$$%s`?", suggestion);
    }

    nexus_memory_deallocate((void *)candidates);
  }

  check->ok = FALSE;
}

static boolean l_genesis_library_has_parameter(const LGLibraryCheck *check, const char *name) {
  uint32 i;

  for (i = 0; i < check->template_decl->param_count; i++) {
    if (nexus_strings_string_equals(check->template_decl->params[i].name, name)) {
      return TRUE;
    }
  }

  return FALSE;
}

static void l_genesis_library_expression(LGLibraryCheck *check, const LGExpr *expression) {
  uint32 i;

  if (expression == NULL) {
    return;
  }

  switch (expression->kind) {
  case LGE_PARAM:
    if (!l_genesis_library_has_parameter(check, expression->name)) {
      l_genesis_library_unknown_parameter(check, expression->name, expression->span);
    }
    break;
  case LGE_CALL:
    if (!logos_languages_genesis_template_visible(check->unit, check->imports, expression->sigil, expression->name) && expression->sigil != 0 &&
        !nexus_strings_string_equals(expression->name, "inner_bytes")) {
      PharosDiagnostic *diagnostic;

      diagnostic = pharos_diagnostics_report((check->compiler)->reporting, PDS_ERROR, "G0110", expression->span, "cannot find the template `%c%s`",
                                             expression->sigil, expression->name);
      pharos_diagnostics_label_add(diagnostic, expression->span, TRUE, "not declared or imported in this library");
      pharos_diagnostics_help_add(diagnostic, "declare it with `TEMPLATE <%c%s>(...)`, or import it from another library", expression->sigil,
                                  expression->name);
      check->ok = FALSE;
    }

    l_genesis_library_expressions(check, expression->items, expression->item_count);
    break;
  case LGE_UNION:
  case LGE_CONCAT:
  case LGE_COMPARE:
    l_genesis_library_expressions(check, expression->items, expression->item_count);
    break;
  case LGE_REPEAT:
    l_genesis_library_expression(check, expression->child);
    break;
  case LGE_GROUP:
    l_genesis_library_expression(check, expression->child);

    for (i = 0; i < expression->annotation_count; i++) {
      l_genesis_library_expressions(check, expression->annotations[i].arguments, expression->annotations[i].argument_count);
    }
    break;
  case LGE_IF:
    l_genesis_library_expressions(check, expression->items, expression->item_count);
    l_genesis_library_expression(check, expression->child);
    l_genesis_library_expression(check, expression->else_branch);
    break;
  case LGE_ANY:
    break;
  case LGE_NOT:
    l_genesis_library_expression(check, expression->child);
    break;
  case LGE_EMPTY:
  case LGE_STRING:
  case LGE_RANGE:
  case LGE_TOKEN:
  case LGE_FRAGMENT:
  case LGE_RULE:
  case LGE_NONE:
  case LGE_ENUM:
  case LGE_INT:
  case LGE_TEXT:
  case LGE_IDENT:
  default:
    break;
  }
}

static void l_genesis_library_declaration(LGLibraryCheck *check, const LGDecl *decl) {
  uint32 i;

  switch (decl->kind) {
  case LGD_TOKEN:
  case LGD_FRAGMENT:
  case LGD_RULE:
    if (decl->name_param != NULL && !l_genesis_library_has_parameter(check, decl->name_param->name)) {
      l_genesis_library_unknown_parameter(check, decl->name_param->name, decl->name_param->span);
    }

    l_genesis_library_expression(check, decl->expression);
    l_genesis_library_expression(check, decl->priority);

    for (i = 0; i < decl->annotation_count; i++) {
      l_genesis_library_expressions(check, decl->annotations[i].arguments, decl->annotations[i].argument_count);
    }
    break;
  case LGD_PRATT:
  case LGD_EMBED:
    if (decl->name_param != NULL && !l_genesis_library_has_parameter(check, decl->name_param->name)) {
      l_genesis_library_unknown_parameter(check, decl->name_param->name, decl->name_param->span);
    }

    for (i = 0; i < decl->child_count; i++) {
      l_genesis_library_declaration(check, decl->children[i]);
    }

    for (i = 0; i < decl->name_count && decl->values != NULL; i++) {
      l_genesis_library_expression(check, decl->values[i]);
    }
    break;
  case LGD_CALL:
    l_genesis_library_expression(check, decl->call);
    break;
  case LGD_ENTRY:
    l_genesis_library_expression(check, decl->entry_token);
    l_genesis_library_expression(check, decl->left_bp);
    l_genesis_library_expression(check, decl->right_bp);
    break;
  case LGD_IF:
    l_genesis_library_expression(check, decl->condition);

    for (i = 0; i < decl->child_count; i++) {
      l_genesis_library_declaration(check, decl->children[i]);
    }

    for (i = 0; i < decl->else_count; i++) {
      l_genesis_library_declaration(check, decl->else_children[i]);
    }
    break;
  case LGD_IMPORT:
  case LGD_ENUM:
  case LGD_PARSER:
  case LGD_CONTEXT:
  case LGD_TEMPLATE:
  default:
    break;
  }
}

boolean logos_languages_genesis_analysis_library_check(LGenesisCompiler *compiler, LGUnit *unit, NexusDataList *imports) {
  LGLibraryCheck    check;
  const LGDecl     *decl;
  const LGDecl     *earlier;
  PharosDiagnostic *diagnostic;
  const char       *suggestion;
  const char      **candidates;
  uint32            i;
  uint32            j;
  uint32            k;

  check.compiler      = compiler;
  check.unit          = unit;
  check.imports       = imports;
  check.template_decl = NULL;
  check.ok            = TRUE;

  for (i = 0; i < unit->decl_count; i++) {
    decl = unit->decls[i];

    switch (decl->kind) {
    case LGD_IMPORT:
      break;
    case LGD_ENUM:
      for (j = 0; j < i; j++) {
        earlier = unit->decls[j];

        if (earlier->kind == LGD_ENUM && nexus_strings_string_equals(earlier->name, decl->name)) {
          diagnostic =
              pharos_diagnostics_report((compiler)->reporting, PDS_ERROR, "G0108", decl->span, "the enumeration `%s` is declared twice", decl->name);
          pharos_diagnostics_label_add(diagnostic, decl->span, TRUE, "declared again here");
          pharos_diagnostics_label_add(diagnostic, earlier->span, FALSE, "first declared here");
          check.ok = FALSE;
        }
      }

      for (j = 0; j < decl->name_count; j++) {
        for (k = 0; k < j; k++) {
          if (nexus_strings_string_equals(decl->names[j], decl->names[k])) {
            diagnostic = pharos_diagnostics_report((compiler)->reporting, PDS_ERROR, "G0111", decl->name_spans[j],
                                                   "the member `%s` of `%s` is declared twice", decl->names[j], decl->name);
            pharos_diagnostics_label_add(diagnostic, decl->name_spans[j], TRUE, "declared again here");
            pharos_diagnostics_label_add(diagnostic, decl->name_spans[k], FALSE, "first declared here");
            check.ok = FALSE;
          }
        }
      }
      break;
    case LGD_TEMPLATE:
      check.template_decl = decl;

      for (j = 0; j < i; j++) {
        earlier = unit->decls[j];

        if (earlier->kind == LGD_TEMPLATE && earlier->sigil == decl->sigil && nexus_strings_string_equals(earlier->name, decl->name)) {
          diagnostic = pharos_diagnostics_report((compiler)->reporting, PDS_ERROR, "G0107", decl->span, "the template `%c%s` is declared twice",
                                                 decl->sigil, decl->name);
          pharos_diagnostics_label_add(diagnostic, decl->span, TRUE, "declared again here");
          pharos_diagnostics_label_add(diagnostic, earlier->span, FALSE, "first declared here");
          check.ok = FALSE;
        }
      }

      /* The result type is one of the five built-in result kinds. */
      {
        boolean known;

        known = FALSE;

        for (j = 0; j < NEXUS_ARRAY_SIZE_ELEMENTS(l_genesis_result_types); j++) {
          known = known || nexus_strings_string_equals(l_genesis_result_types[j], decl->result_type);
        }

        if (!known) {
          diagnostic = pharos_diagnostics_report((compiler)->reporting, PDS_ERROR, "G0106", decl->span, "unknown template result type `%s`",
                                                 decl->result_type);
          pharos_diagnostics_label_add(diagnostic, decl->span, TRUE, "declared here");
          suggestion = nexus_strings_string_closest(decl->result_type, l_genesis_result_types, NEXUS_ARRAY_SIZE_ELEMENTS(l_genesis_result_types));

          if (suggestion != NULL) {
            pharos_diagnostics_help_add(diagnostic, "did you mean `%s`?", suggestion);
          } else {
            pharos_diagnostics_note_add(diagnostic,
                                        "the result types are LexicalExpression, TokenDefinition, GrammarExpression, PrattEntry and RuleDefinition");
          }

          check.ok = FALSE;
        }
      }

      for (j = 0; j < decl->param_count; j++) {
        const LGParam *param;

        param = &decl->params[j];

        for (k = 0; k < j; k++) {
          if (nexus_strings_string_equals(decl->params[k].name, param->name)) {
            diagnostic = pharos_diagnostics_report((compiler)->reporting, PDS_ERROR, "G0112", param->span, "the parameter `$$%s` is declared twice",
                                                   param->name);
            pharos_diagnostics_label_add(diagnostic, param->span, TRUE, "declared again here");
            pharos_diagnostics_label_add(diagnostic, decl->params[k].span, FALSE, "first declared here");
            check.ok = FALSE;
          }
        }

        for (k = 0; k < param->type_count; k++) {
          boolean known;
          uint32  type_index;

          known = logos_languages_genesis_enum_visible(unit, imports, param->types[k], NULL, NULL);

          for (type_index = 0; type_index < NEXUS_ARRAY_SIZE_ELEMENTS(l_genesis_parameter_types) && !known; type_index++) {
            known = nexus_strings_string_equals(l_genesis_parameter_types[type_index], param->types[k]);
          }

          if (!known) {
            diagnostic =
                pharos_diagnostics_report((compiler)->reporting, PDS_ERROR, "G0105", param->span, "unknown parameter type `%s`", param->types[k]);
            pharos_diagnostics_label_add(diagnostic, param->span, TRUE, "in the declaration of `$$%s`", param->name);
            candidates = (const char **)nexus_memory_allocate((NEXUS_ARRAY_SIZE_ELEMENTS(l_genesis_parameter_types) + unit->decl_count + 1) *
                                                              NEXUS_EFFECTIVE_POINTER_SIZE_BYTES);
            type_index = 0;

            for (k = 0; k < NEXUS_ARRAY_SIZE_ELEMENTS(l_genesis_parameter_types); k++) {
              candidates[type_index++] = l_genesis_parameter_types[k];
            }

            suggestion = nexus_strings_string_closest(param->types[0], candidates, type_index);

            if (suggestion != NULL) {
              pharos_diagnostics_help_add(diagnostic, "did you mean `%s`?", suggestion);
            } else {
              pharos_diagnostics_note_add(
                  diagnostic, "a parameter is a Token, TokenName, RuleName, Expression, string, int, uint, LanguageRef, RuleRef or an enumeration");
            }

            nexus_memory_deallocate((void *)candidates);
            check.ok = FALSE;

            break;
          }
        }

        l_genesis_library_expression(&check, param->default_value);
      }

      for (j = 0; j < decl->child_count; j++) {
        l_genesis_library_declaration(&check, decl->children[j]);
      }
      break;
    case LGD_PARSER:
    case LGD_CONTEXT:
    case LGD_TOKEN:
    case LGD_FRAGMENT:
    case LGD_RULE:
    case LGD_PRATT:
    case LGD_EMBED:
    case LGD_CALL:
    case LGD_ENTRY:
    case LGD_IF:
    default: {
      const char *what;

      switch (decl->kind) {
      case LGD_PARSER:
        what = "a `PARSER` block";
        break;
      case LGD_CONTEXT:
        what = "a lexical `CONTEXT`";
        break;
      case LGD_TOKEN:
      case LGD_FRAGMENT:
        what = "a lexical token";
        break;
      case LGD_PRATT:
        what = "a `PRATT` rule";
        break;
      case LGD_EMBED:
        what = "an `EMBED` rule";
        break;
      default:
        what = "a grammar rule";
        break;
      }

      diagnostic = pharos_diagnostics_report((compiler)->reporting, PDS_ERROR, "G0101", decl->span, "a library cannot declare %s", what);
      pharos_diagnostics_label_add(diagnostic, decl->span, TRUE, "not allowed in `Library: %s`", unit->name);
      pharos_diagnostics_note_add(diagnostic, "a library holds imports, enumerations and templates; everything else is generated at the call site");
      pharos_diagnostics_help_add(diagnostic, "wrap it in a `TEMPLATE <...>(...) -> RuleDefinition { ... }` and instantiate that from a language");
      check.ok = FALSE;
    } break;
    }
  }

  return check.ok;
}

/* ---------------------------------------------------------------------------- */
/* LANGUAGE ANALYSIS                                                             */
/* ---------------------------------------------------------------------------- */

typedef struct LGAnalyzer {
  LGenesisCompiler *compiler;
  LGModel          *model;
  boolean           ok;
} LGAnalyzer;

static void l_genesis_error(LGAnalyzer *analyzer);

/* The most a counted repetition may count: the lexer's counters are 16 bits. */
#define LG_REPEAT_COUNT_MAX 65535U

/* Summary: checks the bounds of a repetition `x{n,m}`: n may not exceed m, and neither may be beyond what the lexer can count. */
static void l_genesis_repeat_check(LGAnalyzer *analyzer, const LGExpr *expression) {
  PharosDiagnostic *diagnostic;
  boolean           unbounded;

  if (expression->repeat_op != '{') {
    return;
  }

  unbounded = expression->hi == 0xFFFFFFFFU;

  if (!unbounded && expression->lo > expression->hi) {
    diagnostic = pharos_diagnostics_report((analyzer->compiler)->reporting, PDS_ERROR, "G0263", expression->span,
                                           "the repetition counts from %u up to %u", (unsigned)expression->lo, (unsigned)expression->hi);
    pharos_diagnostics_label_add(diagnostic, expression->span, TRUE, "the minimum is larger than the maximum");
    pharos_diagnostics_help_add(diagnostic, "write the smaller count first: `{%u,%u}`", (unsigned)expression->hi, (unsigned)expression->lo);
    l_genesis_error(analyzer);
  } else if (expression->lo > LG_REPEAT_COUNT_MAX || (!unbounded && expression->hi > LG_REPEAT_COUNT_MAX)) {
    diagnostic = pharos_diagnostics_report((analyzer->compiler)->reporting, PDS_ERROR, "G0263", expression->span,
                                           "a repetition count above %u is not supported", (unsigned)LG_REPEAT_COUNT_MAX);
    pharos_diagnostics_label_add(diagnostic, expression->span, TRUE, "too large");
    pharos_diagnostics_note_add(diagnostic, "use `+` or `*` for an unbounded repetition");
    l_genesis_error(analyzer);
  }
}

static void l_genesis_error(LGAnalyzer *analyzer) {
  analyzer->ok = FALSE;
}

/* Summary: the name candidates of the symbols of one kind, for did-you-mean hints. */
static const char **l_genesis_candidates(const LGAnalyzer *analyzer, char sigil, const LGContextModel *context, uint_large *out_count) {
  NexusDataList *names;
  const char   **array;
  uint_large     i;
  uint_large     j;
  uint_large     count;

  names = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 16);

  if (sigil == '#') {
    for (i = 0; i < nexus_data_list_length_get(analyzer->model->rules); i++) {
      (void)nexus_data_list_append_pointer(names, ((const LGRuleModel *)nexus_data_list_pointer_at(analyzer->model->rules, i))->name);
    }

    for (i = 0; i < nexus_data_list_length_get(analyzer->model->pratts); i++) {
      (void)nexus_data_list_append_pointer(names, ((const LGPrattModel *)nexus_data_list_pointer_at(analyzer->model->pratts, i))->name);
    }

    for (i = 0; i < nexus_data_list_length_get(analyzer->model->embeds); i++) {
      (void)nexus_data_list_append_pointer(names, ((const LGEmbedModel *)nexus_data_list_pointer_at(analyzer->model->embeds, i))->name);
    }
  } else {
    for (i = 0; i < nexus_data_list_length_get(analyzer->model->contexts); i++) {
      const LGContextModel *each;

      each = (const LGContextModel *)nexus_data_list_pointer_at(analyzer->model->contexts, i);

      if (context != NULL && each != context) {
        continue;
      }

      for (j = 0; j < nexus_data_list_length_get(each->tokens); j++) {
        const LGToken *token;

        token = (const LGToken *)nexus_data_list_pointer_at(each->tokens, j);

        if (context != NULL ? token->fragment : !token->fragment) {
          (void)nexus_data_list_append_pointer(names, token->name);
        }
      }
    }
  }

  count = nexus_data_list_length_get(names);
  array = (const char **)nexus_memory_allocate((count + 1) * NEXUS_EFFECTIVE_POINTER_SIZE_BYTES);

  for (i = 0; i < count; i++) {
    array[i] = (const char *)nexus_data_list_pointer_at(names, i);
  }

  nexus_data_list_destroy(names);
  *out_count = count;

  return array;
}

static void l_genesis_unknown_symbol(LGAnalyzer *analyzer, const LGExpr *reference, char sigil, const LGContextModel *context, const char *code,
                                     const char *what) {
  PharosDiagnostic *diagnostic;
  const char      **candidates;
  const char       *suggestion;
  uint_large        count;

  diagnostic = pharos_diagnostics_report((analyzer->compiler)->reporting, PDS_ERROR, code, reference->span, "cannot find %s `%c%s` in this language",
                                         what, sigil, reference->name);
  pharos_diagnostics_label_add(diagnostic, reference->span, TRUE, "not defined");
  candidates = l_genesis_candidates(analyzer, sigil, context, &count);
  suggestion = nexus_strings_string_closest(reference->name, candidates, count);

  if (suggestion != NULL) {
    pharos_diagnostics_help_add(diagnostic, "did you mean `%c%s`?", sigil, suggestion);
  } else if (sigil == '#') {
    pharos_diagnostics_help_add(diagnostic, "define it with `#%s = ...`", reference->name);
  } else if (context != NULL) {
    pharos_diagnostics_help_add(diagnostic, "define it in this context with `$%s = ...`", reference->name);
  } else {
    pharos_diagnostics_help_add(diagnostic, "define it inside a `CONTEXT <...> { ... }` block with `$%s = ...`", reference->name);
  }

  nexus_memory_deallocate((void *)candidates);
  l_genesis_error(analyzer);
}

/* ---- lexical expressions ---- */

typedef struct LGLexicalWalk {
  LGAnalyzer           *analyzer;
  const LGContextModel *context;
  LGToken              *owner;
} LGLexicalWalk;

static void l_genesis_lexical_walk(LGLexicalWalk *walk, const LGExpr *expression);

static void l_genesis_lexical_children(LGLexicalWalk *walk, LGExpr *const *items, uint32 count) {
  uint32 i;

  for (i = 0; i < count; i++) {
    l_genesis_lexical_walk(walk, items[i]);
  }
}

static void l_genesis_lexical_walk(LGLexicalWalk *walk, const LGExpr *expression) {
  PharosDiagnostic *diagnostic;
  LGToken          *fragment;

  if (expression == NULL) {
    return;
  }

  switch (expression->kind) {
  case LGE_EMPTY:
  case LGE_STRING:
  case LGE_RANGE:
  case LGE_TEXT:
  case LGE_ANY:
    break;
  case LGE_NOT: {
    NexusDataList *scratch;
    boolean        single;

    /* The operand must be one character or a set of them: what is left of the alphabet is then well defined. */
    scratch = nexus_data_list_create(NEXUS_SIZEOF(LGCodepointRange), 8);
    single  = logos_languages_genesis_analysis_charset_get(walk->context, expression->child, scratch);
    nexus_data_list_destroy(scratch);

    if (!single) {
      PharosDiagnostic *negation;

      negation = pharos_diagnostics_report((walk->analyzer->compiler)->reporting, PDS_ERROR, "G0264", expression->span,
                                           "`!` takes one character or a set of characters");
      pharos_diagnostics_label_add(negation, expression->child->span, TRUE, "this is not a single character or a set of them");
      pharos_diagnostics_help_add(negation, "write `!'x'`, `!('a'..'z' || '_')` or `!$_digit`; a string of several characters cannot be negated");
      l_genesis_error(walk->analyzer);
    }

    l_genesis_lexical_walk(walk, expression->child);
    break;
  }
  case LGE_FRAGMENT:
    fragment = logos_languages_genesis_analysis_fragment_find(walk->context, expression->name);

    if (fragment == NULL) {
      const LGContextModel *other;
      uint_large            i;

      other = NULL;

      for (i = 0; i < nexus_data_list_length_get(walk->analyzer->model->contexts) && other == NULL; i++) {
        const LGContextModel *each;

        each = (const LGContextModel *)nexus_data_list_pointer_at(walk->analyzer->model->contexts, i);

        if (each != walk->context && logos_languages_genesis_analysis_fragment_find(each, expression->name) != NULL) {
          other = each;
        }
      }

      l_genesis_unknown_symbol(walk->analyzer, expression, '$', walk->context, "G0231", "the fragment");

      if (other != NULL && pharos_diagnostics_count_get(walk->analyzer->compiler->reporting) > 0) {
        diagnostic = pharos_diagnostics_last_get(walk->analyzer->compiler->reporting);
        pharos_diagnostics_note_add(diagnostic, "`$%s` exists in the context `%s`; a fragment belongs to the context that declares it",
                                    expression->name, other->name);
      }
    } else {
      fragment->used = TRUE;
    }
    break;
  case LGE_TOKEN:
    diagnostic = pharos_diagnostics_report((walk->analyzer->compiler)->reporting, PDS_ERROR, "G0237", expression->span,
                                           "an emitting token cannot be used inside a lexical definition");
    pharos_diagnostics_label_add(diagnostic, expression->span, TRUE, "`$%s` is an emitting token", expression->name);
    pharos_diagnostics_note_add(diagnostic, "only fragments (`$_name`) can be reused by other lexical definitions");
    pharos_diagnostics_help_add(diagnostic, "declare it as a fragment: `$_%s = ...`, then reference `$_%s`", expression->name, expression->name);
    l_genesis_error(walk->analyzer);
    break;
  case LGE_RULE:
    diagnostic = pharos_diagnostics_report((walk->analyzer->compiler)->reporting, PDS_ERROR, "G0238", expression->span,
                                           "a grammar rule cannot be used inside a lexical definition");
    pharos_diagnostics_label_add(diagnostic, expression->span, TRUE, "`#%s` is a grammar rule", expression->name);
    pharos_diagnostics_note_add(diagnostic, "the lexer runs before the parser; tokens are made of characters only");
    l_genesis_error(walk->analyzer);
    break;
  case LGE_UNION:
  case LGE_CONCAT:
    l_genesis_lexical_children(walk, expression->items, expression->item_count);
    break;
  case LGE_REPEAT:
    l_genesis_repeat_check(walk->analyzer, expression);
    l_genesis_lexical_walk(walk, expression->child);
    break;
  case LGE_GROUP:
    l_genesis_lexical_walk(walk, expression->child);
    break;
  case LGE_PARAM:
  case LGE_CALL:
  case LGE_IF:
  case LGE_COMPARE:
  case LGE_NONE:
  case LGE_ENUM:
  case LGE_INT:
  case LGE_IDENT:
  default:
    diagnostic = pharos_diagnostics_report((walk->analyzer->compiler)->reporting, PDS_ERROR, "G0239", expression->span,
                                           "this is not valid in a lexical definition");
    pharos_diagnostics_label_add(diagnostic, expression->span, TRUE, "expected a quoted literal, a range, a fragment or a combination of them");
    l_genesis_error(walk->analyzer);
    break;
  }
}

/* Summary: whether a lexical expression can match the empty string; fragments are followed. */
static boolean l_genesis_lexical_nullable(const LGContextModel *context, const LGExpr *expression, uint32 depth) {
  uint32         i;
  const LGToken *fragment;

  if (expression == NULL || depth > 64) {
    return FALSE;
  }

  switch (expression->kind) {
  case LGE_EMPTY:
    return TRUE;
  case LGE_STRING:
    return expression->codepoint_count == 0;
  case LGE_TEXT:
    return expression->name[0] == '\0';
  case LGE_FRAGMENT:
    fragment = logos_languages_genesis_analysis_fragment_find(context, expression->name);

    return fragment != NULL && l_genesis_lexical_nullable(context, fragment->expression, depth + 1);
  case LGE_UNION:
    for (i = 0; i < expression->item_count; i++) {
      if (l_genesis_lexical_nullable(context, expression->items[i], depth + 1)) {
        return TRUE;
      }
    }

    return FALSE;
  case LGE_CONCAT:
    for (i = 0; i < expression->item_count; i++) {
      if (!l_genesis_lexical_nullable(context, expression->items[i], depth + 1)) {
        return FALSE;
      }
    }

    return TRUE;
  case LGE_REPEAT:
    if (expression->repeat_op == '{') {
      return expression->lo == 0 || l_genesis_lexical_nullable(context, expression->child, depth + 1);
    }

    return expression->repeat_op != '+' || l_genesis_lexical_nullable(context, expression->child, depth + 1);
  case LGE_GROUP:
    return l_genesis_lexical_nullable(context, expression->child, depth + 1);
  case LGE_ANY:
  case LGE_NOT:
  case LGE_RANGE:
  case LGE_TOKEN:
  case LGE_RULE:
  case LGE_PARAM:
  case LGE_CALL:
  case LGE_IF:
  case LGE_COMPARE:
  case LGE_NONE:
  case LGE_ENUM:
  case LGE_INT:
  case LGE_IDENT:
  default:
    return FALSE;
  }
}

/* Summary: finds fragment cycles; a fragment that reaches itself would expand forever. */
static boolean l_genesis_fragment_cycle(LGAnalyzer *analyzer, const LGContextModel *context, LGToken *fragment, NexusDataList *path);

static boolean l_genesis_fragment_cycle_expression(LGAnalyzer *analyzer, const LGContextModel *context, const LGExpr *expression,
                                                   NexusDataList *path) {
  uint32   i;
  LGToken *target;

  if (expression == NULL) {
    return FALSE;
  }

  switch (expression->kind) {
  case LGE_FRAGMENT:
    target = logos_languages_genesis_analysis_fragment_find(context, expression->name);

    return target != NULL && l_genesis_fragment_cycle(analyzer, context, target, path);
  case LGE_UNION:
  case LGE_CONCAT:
    for (i = 0; i < expression->item_count; i++) {
      if (l_genesis_fragment_cycle_expression(analyzer, context, expression->items[i], path)) {
        return TRUE;
      }
    }

    return FALSE;
  case LGE_REPEAT:
  case LGE_GROUP:
    return l_genesis_fragment_cycle_expression(analyzer, context, expression->child, path);
  default:
    return FALSE;
  }
}

static boolean l_genesis_fragment_cycle(LGAnalyzer *analyzer, const LGContextModel *context, LGToken *fragment, NexusDataList *path) {
  if (fragment->mark == 2) {
    return FALSE;
  }

  if (fragment->mark == 1) {
    PharosDiagnostic *diagnostic;
    uint_large        i;
    boolean           inside;

    diagnostic = pharos_diagnostics_report((analyzer->compiler)->reporting, PDS_ERROR, "G0232", fragment->span,
                                           "the fragment `$%s` is defined in terms of itself", fragment->name);
    pharos_diagnostics_label_add(diagnostic, fragment->span, TRUE, "this definition reaches itself");
    inside = FALSE;

    for (i = 0; i < nexus_data_list_length_get(path); i++) {
      const LGToken *step;

      step = (const LGToken *)nexus_data_list_pointer_at(path, i);

      if (step == fragment) {
        inside = TRUE;
      } else if (inside) {
        pharos_diagnostics_label_add(diagnostic, step->span, FALSE, "through `$%s`", step->name);
      }
    }

    pharos_diagnostics_note_add(
        diagnostic, "fragments are expanded into the tokens that use them, so they cannot be recursive; a lexer matches regular languages only");
    l_genesis_error(analyzer);

    return TRUE;
  }

  fragment->mark = 1;
  (void)nexus_data_list_append_pointer(path, fragment);

  if (l_genesis_fragment_cycle_expression(analyzer, context, fragment->expression, path)) {
    nexus_data_list_remove_pointer(path, nexus_data_list_length_get(path) - 1, NULL);
    fragment->mark = 2;

    return TRUE;
  }

  nexus_data_list_remove_pointer(path, nexus_data_list_length_get(path) - 1, NULL);
  fragment->mark = 2;

  return FALSE;
}

static boolean l_genesis_context_exists(const LGModel *model, const char *name) {
  uint_large i;

  for (i = 0; i < nexus_data_list_length_get(model->contexts); i++) {
    if (nexus_strings_string_equals(((const LGContextModel *)nexus_data_list_pointer_at(model->contexts, i))->name, name)) {
      return TRUE;
    }
  }

  return FALSE;
}

static void l_genesis_analyze_contexts(LGAnalyzer *analyzer) {
  LGModel          *model;
  PharosDiagnostic *diagnostic;
  uint_large        i;
  uint_large        j;
  boolean           root;
  LGLexicalWalk     walk;

  model = analyzer->model;
  root  = FALSE;

  for (i = 0; i < nexus_data_list_length_get(model->contexts); i++) {
    root = root || nexus_strings_string_equals(((const LGContextModel *)nexus_data_list_pointer_at(model->contexts, i))->name, "ROOT");
  }

  if (!root) {
    PharosSpan span;

    span = model->unit->name_span;

    if (nexus_data_list_length_get(model->contexts) > 0) {
      span = ((const LGContextModel *)nexus_data_list_pointer_at(model->contexts, 0))->span;
    }

    diagnostic = pharos_diagnostics_report((analyzer->compiler)->reporting, PDS_ERROR, "G0230", span, "the language has no `ROOT` lexical context");
    pharos_diagnostics_label_add(diagnostic, span, TRUE,
                                 nexus_data_list_length_get(model->contexts) > 0 ? "the first context here is not `ROOT`"
                                                                                 : "the language declares no context");
    pharos_diagnostics_note_add(diagnostic, "lexing starts in `ROOT`; it is the obligatory initial context");
    pharos_diagnostics_help_add(diagnostic, "add `CONTEXT <ROOT> { ... }` with the tokens of the language");
    l_genesis_error(analyzer);
  }

  for (i = 0; i < nexus_data_list_length_get(model->contexts); i++) {
    LGContextModel *context;
    NexusDataList  *path;

    context = (LGContextModel *)nexus_data_list_pointer_at(model->contexts, i);
    path    = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 8);

    for (j = 0; j < nexus_data_list_length_get(context->tokens); j++) {
      LGToken *token;

      token = (LGToken *)nexus_data_list_pointer_at(context->tokens, j);

      if (token->fragment) {
        if (token->priority != 0 || token->action != LGA_NONE) {
          diagnostic = pharos_diagnostics_report((analyzer->compiler)->reporting, PDS_ERROR, "G0236", token->span,
                                                 "a fragment cannot have a priority or a context action");
          pharos_diagnostics_label_add(diagnostic, token->span, TRUE, "`$%s` is a non-emitting fragment", token->name);
          pharos_diagnostics_help_add(diagnostic, "rename it to `$%s` to make it an emitting token", token->name + 1);
          l_genesis_error(analyzer);
        }

        (void)l_genesis_fragment_cycle(analyzer, context, token, path);
      }

      walk.analyzer = analyzer;
      walk.context  = context;
      walk.owner    = token;
      l_genesis_lexical_walk(&walk, token->expression);

      if (!token->fragment && token->mark == 0 && l_genesis_lexical_nullable(context, token->expression, 0)) {
        diagnostic = pharos_diagnostics_report((analyzer->compiler)->reporting, PDS_ERROR, "G0233", token->span,
                                               "the token `$%s` can match the empty string", token->name);
        pharos_diagnostics_label_add(diagnostic, token->expression != NULL ? token->expression->span : token->span, TRUE,
                                     "this can match nothing at all");
        pharos_diagnostics_note_add(diagnostic, "a token that matches no characters would make the lexer loop without progress");
        pharos_diagnostics_help_add(diagnostic, "require at least one character, for example by turning a trailing `*` into `+`");
        l_genesis_error(analyzer);
      }

      if (!token->fragment) {
        switch (token->action) {
        case LGA_PUSH:
        case LGA_SET:
        case LGA_REPLACE:
          if (!l_genesis_context_exists(model, token->target)) {
            const char **candidates;
            const char  *suggestion;
            uint_large   k;

            diagnostic = pharos_diagnostics_report((analyzer->compiler)->reporting, PDS_ERROR, "G0234", token->action_span,
                                                   "cannot find the context `%s`", token->target);
            pharos_diagnostics_label_add(diagnostic, token->action_span, TRUE, "no `CONTEXT <%s>` is declared", token->target);
            candidates = (const char **)nexus_memory_allocate((nexus_data_list_length_get(model->contexts) + 1) * NEXUS_EFFECTIVE_POINTER_SIZE_BYTES);

            for (k = 0; k < nexus_data_list_length_get(model->contexts); k++) {
              candidates[k] = ((const LGContextModel *)nexus_data_list_pointer_at(model->contexts, k))->name;
            }

            suggestion = nexus_strings_string_closest(token->target, candidates, nexus_data_list_length_get(model->contexts));

            if (suggestion != NULL) {
              pharos_diagnostics_help_add(diagnostic, "did you mean `%s`?", suggestion);
            }

            nexus_memory_deallocate((void *)candidates);
            l_genesis_error(analyzer);
          }
          break;
        case LGA_POP:
          if (token->amount < 1 || token->amount > 255) {
            diagnostic = pharos_diagnostics_report((analyzer->compiler)->reporting, PDS_ERROR, "G0235", token->action_span,
                                                   "`pop` takes a number from 1 to 255");
            pharos_diagnostics_label_add(diagnostic, token->action_span, TRUE, "cannot pop %lld contexts", (long long)token->amount);
            l_genesis_error(analyzer);
          }
          break;
        case LGA_NONE:
        default:
          break;
        }
      }
    }

    nexus_data_list_destroy(path);
  }
}

/* ---- grammar expressions ---- */

typedef struct LGGrammarWalk {
  LGAnalyzer *analyzer;
  boolean     in_embed;
} LGGrammarWalk;

static void l_genesis_grammar_walk(LGGrammarWalk *walk, const LGExpr *expression);

static void l_genesis_choice_mode_check(LGAnalyzer *analyzer, const LGExpr *value, LContextaChoiceMode *out_mode) {
  static const char *const members[] = {"ORDERED", "LONGEST", "UNAMBIGUOUS"};
  PharosDiagnostic        *diagnostic;
  const char              *suggestion;

  *out_mode = LCCM_DEFAULT;

  if (value->kind == LGE_ENUM && nexus_strings_string_equals(value->qualifier, "ChoiceMode")) {
    if (nexus_strings_string_equals(value->name, "ORDERED")) {
      *out_mode = LCCM_ORDERED;

      return;
    }

    if (nexus_strings_string_equals(value->name, "LONGEST")) {
      *out_mode = LCCM_LONGEST;

      return;
    }

    if (nexus_strings_string_equals(value->name, "UNAMBIGUOUS")) {
      *out_mode = LCCM_UNAMBIGUOUS;

      return;
    }

    diagnostic =
        pharos_diagnostics_report((analyzer->compiler)->reporting, PDS_ERROR, "G0224", value->span, "`ChoiceMode` has no member `%s`", value->name);
    pharos_diagnostics_label_add(diagnostic, value->span, TRUE, "unknown member");
    suggestion = nexus_strings_string_closest(value->name, members, 3);

    if (suggestion != NULL) {
      pharos_diagnostics_help_add(diagnostic, "did you mean `ChoiceMode.%s`?", suggestion);
    } else {
      pharos_diagnostics_note_add(diagnostic, "the members are ORDERED, LONGEST and UNAMBIGUOUS");
    }

    l_genesis_error(analyzer);

    return;
  }

  diagnostic = pharos_diagnostics_report((analyzer->compiler)->reporting, PDS_ERROR, "G0224", value->span, "expected a `ChoiceMode` member");
  pharos_diagnostics_label_add(diagnostic, value->span, TRUE, "not a `ChoiceMode` value");
  pharos_diagnostics_help_add(diagnostic, "write `ChoiceMode.ORDERED`, `ChoiceMode.LONGEST` or `ChoiceMode.UNAMBIGUOUS`");
  l_genesis_error(analyzer);
}

static void l_genesis_annotation_check(LGAnalyzer *analyzer, const LGAnnotation *annotation) {
  static const char *const known[] = {"choice", "exclude", "prec"};
  PharosDiagnostic        *diagnostic;
  LContextaChoiceMode      mode;
  const char              *suggestion;
  uint32                   i;

  if (nexus_strings_string_equals(annotation->name, (logos_languages_genesis_spec_word_text_get(LGSW_CHOICE) + 1))) {
    if (annotation->argument_count != 1) {
      diagnostic = pharos_diagnostics_report((analyzer->compiler)->reporting, PDS_ERROR, "G0249", annotation->span,
                                             "`@choice` takes exactly one `ChoiceMode` member");
      pharos_diagnostics_label_add(diagnostic, annotation->span, TRUE, "given %u arguments", (unsigned)annotation->argument_count);
      l_genesis_error(analyzer);

      return;
    }

    l_genesis_choice_mode_check(analyzer, annotation->arguments[0], &mode);

    return;
  }

  if (nexus_strings_string_equals(annotation->name, (logos_languages_genesis_spec_word_text_get(LGSW_EXCLUDE) + 1))) {
    for (i = 0; i < annotation->argument_count; i++) {
      const LGExpr *argument;

      argument = annotation->arguments[i];

      if (argument->kind != LGE_RULE || argument->qualifier != NULL) {
        diagnostic = pharos_diagnostics_report((analyzer->compiler)->reporting, PDS_ERROR, "G0249", argument->span,
                                               "`@exclude` takes the global rules to suppress");
        pharos_diagnostics_label_add(diagnostic, argument->span, TRUE, "expected a `#rule`");
        l_genesis_error(analyzer);
      } else if (logos_languages_genesis_analysis_rule_find(analyzer->model, argument->name) == NULL) {
        l_genesis_unknown_symbol(analyzer, argument, '#', NULL, "G0241", "the rule");
      }
    }

    return;
  }

  if (nexus_strings_string_equals(annotation->name, (logos_languages_genesis_spec_word_text_get(LGSW_PREC) + 1))) {
    boolean associativity_ok;

    associativity_ok = annotation->argument_count == 2 && annotation->arguments[0]->kind == LGE_IDENT &&
                       (nexus_strings_string_equals(annotation->arguments[0]->name, "left") ||
                        nexus_strings_string_equals(annotation->arguments[0]->name, "right") ||
                        nexus_strings_string_equals(annotation->arguments[0]->name, "nonassoc"));

    if (!associativity_ok || annotation->arguments[1]->kind != LGE_INT) {
      diagnostic = pharos_diagnostics_report((analyzer->compiler)->reporting, PDS_ERROR, "G0249", annotation->span,
                                             "`@prec` takes an associativity and a level");
      pharos_diagnostics_label_add(diagnostic, annotation->span, TRUE, "expected `left`, `right` or `nonassoc`, then an integer");
      pharos_diagnostics_help_add(diagnostic, "write `@prec(left, 30)`; a larger level binds tighter");
      l_genesis_error(analyzer);
    }

    return;
  }

  diagnostic =
      pharos_diagnostics_report((analyzer->compiler)->reporting, PDS_ERROR, "G0249", annotation->span, "unknown annotation `@%s`", annotation->name);
  pharos_diagnostics_label_add(diagnostic, annotation->span, TRUE, "not an annotation of rules or groups");
  suggestion = nexus_strings_string_closest(annotation->name, known, 2);

  if (suggestion != NULL) {
    pharos_diagnostics_help_add(diagnostic, "did you mean `@%s`?", suggestion);
  } else {
    pharos_diagnostics_note_add(diagnostic, "rules accept `@choice(...)`, `@exclude(...)` and `@prec(...)`; groups the first two");
  }

  l_genesis_error(analyzer);
}

static void l_genesis_grammar_children(LGGrammarWalk *walk, LGExpr *const *items, uint32 count) {
  uint32 i;

  for (i = 0; i < count; i++) {
    l_genesis_grammar_walk(walk, items[i]);
  }
}

static void l_genesis_grammar_walk(LGGrammarWalk *walk, const LGExpr *expression) {
  LGAnalyzer       *analyzer;
  PharosDiagnostic *diagnostic;
  LGToken          *token;
  LGRuleModel      *rule;
  uint32            i;

  if (expression == NULL) {
    return;
  }

  analyzer = walk->analyzer;

  switch (expression->kind) {
  case LGE_EMPTY:
  case LGE_NONE:
    break;
  case LGE_TOKEN:
    token = logos_languages_genesis_analysis_token_find(analyzer->model, expression->name, NULL);

    if (token == NULL) {
      l_genesis_unknown_symbol(analyzer, expression, '$', NULL, "G0240", "the token");
    } else {
      token->used = TRUE;
    }
    break;
  case LGE_FRAGMENT:
    diagnostic = pharos_diagnostics_report((analyzer->compiler)->reporting, PDS_ERROR, "G0243", expression->span,
                                           "a fragment cannot be used in a grammar rule");
    pharos_diagnostics_label_add(diagnostic, expression->span, TRUE, "`$%s` is a non-emitting fragment", expression->name);
    pharos_diagnostics_note_add(diagnostic, "fragments only build tokens; grammar rules consume emitted tokens");
    pharos_diagnostics_help_add(diagnostic, "define a token `$%s = ...` and reference that instead", expression->name + 1);
    l_genesis_error(analyzer);
    break;
  case LGE_ANY:
  case LGE_NOT:
  case LGE_STRING:
  case LGE_RANGE:
  case LGE_TEXT:
    diagnostic = pharos_diagnostics_report((analyzer->compiler)->reporting, PDS_ERROR, "G0243", expression->span,
                                           "a literal cannot be used directly in a grammar rule");
    pharos_diagnostics_label_add(diagnostic, expression->span, TRUE, "characters belong to the lexer");
    pharos_diagnostics_help_add(diagnostic, "declare a token for it, for example `$name = '...'`, and reference `$name` here");
    l_genesis_error(analyzer);
    break;
  case LGE_RULE:
    if (expression->qualifier != NULL) {
      diagnostic = pharos_diagnostics_report((analyzer->compiler)->reporting, PDS_ERROR, "G0242", expression->span,
                                             "`%s.#%s` can only be used as the entry of an `EMBED`", expression->qualifier, expression->name);
      pharos_diagnostics_label_add(diagnostic, expression->span, TRUE, "a rule of another language is not a rule of this one");
      pharos_diagnostics_note_add(diagnostic, "an imported language runs on its own lexer; embed it with `#embedded_region` or `#embedded_token`");
      l_genesis_error(analyzer);
      break;
    }

    rule = logos_languages_genesis_analysis_rule_find(analyzer->model, expression->name);

    if (rule == NULL && logos_languages_genesis_analysis_pratt_find(analyzer->model, expression->name) == NULL &&
        logos_languages_genesis_analysis_embed_find(analyzer->model, expression->name) == NULL) {
      l_genesis_unknown_symbol(analyzer, expression, '#', NULL, "G0241", "the rule");
    }
    break;
  case LGE_UNION:
  case LGE_CONCAT:
    l_genesis_grammar_children(walk, expression->items, expression->item_count);
    break;
  case LGE_REPEAT:
    l_genesis_repeat_check(walk->analyzer, expression);
    l_genesis_grammar_walk(walk, expression->child);
    break;
  case LGE_GROUP:
    l_genesis_grammar_walk(walk, expression->child);

    for (i = 0; i < expression->annotation_count; i++) {
      l_genesis_annotation_check(analyzer, &expression->annotations[i]);

      if (expression->child->kind != LGE_UNION &&
          nexus_strings_string_equals(expression->annotations[i].name, (logos_languages_genesis_spec_word_text_get(LGSW_CHOICE) + 1))) {
        diagnostic = pharos_diagnostics_report((analyzer->compiler)->reporting, PDS_WARNING, "W0307", expression->annotations[i].span,
                                               "`@choice` has no effect on a group without alternatives");
        pharos_diagnostics_label_add(diagnostic, expression->annotations[i].span, TRUE, "there is nothing to choose between");
        pharos_diagnostics_help_add(diagnostic, "remove it, or add alternatives with `||`");
      }
    }
    break;
  case LGE_PARAM:
  case LGE_CALL:
  case LGE_IF:
  case LGE_COMPARE:
  case LGE_ENUM:
  case LGE_INT:
  case LGE_IDENT:
  default:
    diagnostic =
        pharos_diagnostics_report((analyzer->compiler)->reporting, PDS_ERROR, "G0248", expression->span, "this is not valid in a grammar rule");
    pharos_diagnostics_label_add(diagnostic, expression->span, TRUE, "expected a token, a rule or a combination of them");
    l_genesis_error(analyzer);
    break;
  }
}

/* ---- nullability, left recursion, repetition progress ---- */

static boolean l_genesis_nullable(const LGModel *model, const LGExpr *expression) {
  uint32       i;
  LGRuleModel *rule;

  if (expression == NULL) {
    return TRUE;
  }

  switch (expression->kind) {
  case LGE_EMPTY:
  case LGE_NONE:
    return TRUE;
  case LGE_RULE:
    if (expression->qualifier != NULL) {
      return FALSE;
    }

    rule = logos_languages_genesis_analysis_rule_find(model, expression->name);

    return rule != NULL && rule->nullable;
  case LGE_UNION:
    for (i = 0; i < expression->item_count; i++) {
      if (l_genesis_nullable(model, expression->items[i])) {
        return TRUE;
      }
    }

    return FALSE;
  case LGE_CONCAT:
    for (i = 0; i < expression->item_count; i++) {
      if (!l_genesis_nullable(model, expression->items[i])) {
        return FALSE;
      }
    }

    return TRUE;
  case LGE_REPEAT:
    if (expression->repeat_op == '{') {
      return expression->lo == 0 || l_genesis_nullable(model, expression->child);
    }

    return expression->repeat_op != '+' || l_genesis_nullable(model, expression->child);
  case LGE_GROUP:
    return l_genesis_nullable(model, expression->child);
  default:
    return FALSE;
  }
}

static void l_genesis_nullable_compute(LGModel *model) {
  uint_large i;
  boolean    changed;

  do {
    changed = FALSE;

    for (i = 0; i < nexus_data_list_length_get(model->rules); i++) {
      LGRuleModel *rule;
      boolean      value;

      rule  = (LGRuleModel *)nexus_data_list_pointer_at(model->rules, i);
      value = l_genesis_nullable(model, rule->expression);

      if (value && !rule->nullable) {
        rule->nullable = TRUE;
        changed        = TRUE;
      }
    }
  } while (changed);
}

/* Summary: appends the rules that can start the expression, in the order they are reached. */
static void l_genesis_leftmost(const LGModel *model, const LGExpr *expression, NexusDataList *out) {
  uint32 i;

  if (expression == NULL) {
    return;
  }

  switch (expression->kind) {
  case LGE_RULE:
    if (expression->qualifier == NULL) {
      (void)nexus_data_list_append_pointer(out, expression);
    }
    break;
  case LGE_UNION:
    for (i = 0; i < expression->item_count; i++) {
      l_genesis_leftmost(model, expression->items[i], out);
    }
    break;
  case LGE_CONCAT:
    for (i = 0; i < expression->item_count; i++) {
      l_genesis_leftmost(model, expression->items[i], out);

      if (!l_genesis_nullable(model, expression->items[i])) {
        break;
      }
    }
    break;
  case LGE_REPEAT:
  case LGE_GROUP:
    l_genesis_leftmost(model, expression->child, out);
    break;
  default:
    break;
  }
}

static void l_genesis_left_edges(const LGModel *model, const char *name, NexusDataList *out) {
  const LGRuleModel  *rule;
  const LGPrattModel *pratt;

  rule = logos_languages_genesis_analysis_rule_find(model, name);

  if (rule != NULL) {
    l_genesis_leftmost(model, rule->expression, out);

    return;
  }

  pratt = logos_languages_genesis_analysis_pratt_find(model, name);

  if (pratt != NULL) {
    l_genesis_leftmost(model, pratt->atom, out);
  }
}

typedef struct LGLeftState {
  LGAnalyzer    *analyzer;
  NexusDataList *names; /* The rule names, parallel to marks. */
  NexusDataList *marks; /* int: 0 new, 1 on the path, 2 done */
  NexusDataList *path;  /* The names on the path. */
  boolean       *reported;
} LGLeftState;

static uint_large l_genesis_name_index(const NexusDataList *names, const char *name) {
  uint_large i;

  for (i = 0; i < nexus_data_list_length_get(names); i++) {
    if (nexus_strings_string_equals((const char *)nexus_data_list_pointer_at(names, i), name)) {
      return i;
    }
  }

  return (uint_large)-1;
}

static void l_genesis_left_visit(LGLeftState *state, const char *name) {
  uint_large     index;
  int32         *mark;
  NexusDataList *edges;
  uint_large     i;

  index = l_genesis_name_index(state->names, name);

  if (index == (uint_large)-1) {
    return;
  }

  mark = (int32 *)nexus_data_list_at(state->marks, index);

  if (*mark == 2) {
    return;
  }

  if (*mark == 1) {
    PharosDiagnostic  *diagnostic;
    PharosSpan         span;
    NexusDataList     *text;
    char               terminator;
    boolean            inside;
    const LGRuleModel *rule;

    if (state->reported[index]) {
      return;
    }

    rule = logos_languages_genesis_analysis_rule_find(state->analyzer->model, name);
    span = rule != NULL ? rule->span : logos_languages_genesis_analysis_pratt_find(state->analyzer->model, name)->span;
    diagnostic =
        pharos_diagnostics_report((state->analyzer->compiler)->reporting, PDS_ERROR, "G0244", span, "the rule `#%s` is left-recursive", name);
    pharos_diagnostics_label_add(diagnostic, span, TRUE, "this rule can start with itself");
    text       = nexus_data_list_create(NEXUS_SIZEOF(char), 64);
    terminator = '\0';
    inside     = FALSE;

    for (i = 0; i < nexus_data_list_length_get(state->path); i++) {
      const char *step;
      uint_large  step_index;

      step = (const char *)nexus_data_list_pointer_at(state->path, i);

      if (nexus_strings_string_equals(step, name)) {
        inside = TRUE;
      }

      if (inside) {
        const char *piece;

        piece = step;
        (void)nexus_data_list_append(text, "#");

        while (*piece != '\0') {
          (void)nexus_data_list_append(text, piece);
          piece++;
        }

        (void)nexus_data_list_append(text, " -> ");
        step_index = l_genesis_name_index(state->names, step);

        if (step_index != (uint_large)-1) {
          state->reported[step_index] = TRUE;
        }
      }
    }

    (void)nexus_data_list_append(text, "#");

    {
      const char *piece;

      piece = name;

      while (*piece != '\0') {
        (void)nexus_data_list_append(text, piece);
        piece++;
      }
    }

    (void)nexus_data_list_append(text, &terminator);
    pharos_diagnostics_note_add(diagnostic, "the cycle is %s", (const char *)nexus_data_list_data_get_const(text));
    pharos_diagnostics_help_add(
        diagnostic,
        "a PEG parser cannot grow a left-recursive rule; use repetition, for example `#list = #item ($comma #item)*`, or a PRATT rule for operators");
    nexus_data_list_destroy(text);
    l_genesis_error(state->analyzer);

    return;
  }

  *mark = 1;
  (void)nexus_data_list_append_pointer(state->path, name);
  edges = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 4);
  l_genesis_left_edges(state->analyzer->model, name, edges);

  for (i = 0; i < nexus_data_list_length_get(edges); i++) {
    l_genesis_left_visit(state, ((const LGExpr *)nexus_data_list_pointer_at(edges, i))->name);
  }

  nexus_data_list_destroy(edges);
  nexus_data_list_remove_pointer(state->path, nexus_data_list_length_get(state->path) - 1, NULL);
  mark  = (int32 *)nexus_data_list_at(state->marks, index);
  *mark = 2;
}

static void l_genesis_left_recursion(LGAnalyzer *analyzer) {
  LGLeftState state;
  uint_large  i;
  int32       zero;

  zero           = 0;
  state.analyzer = analyzer;
  state.names    = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 32);
  state.marks    = nexus_data_list_create(NEXUS_SIZEOF(int32), 32);
  state.path     = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 8);

  for (i = 0; i < nexus_data_list_length_get(analyzer->model->rules); i++) {
    (void)nexus_data_list_append_pointer(state.names, ((const LGRuleModel *)nexus_data_list_pointer_at(analyzer->model->rules, i))->name);
    (void)nexus_data_list_append(state.marks, &zero);
  }

  for (i = 0; i < nexus_data_list_length_get(analyzer->model->pratts); i++) {
    (void)nexus_data_list_append_pointer(state.names, ((const LGPrattModel *)nexus_data_list_pointer_at(analyzer->model->pratts, i))->name);
    (void)nexus_data_list_append(state.marks, &zero);
  }

  state.reported = (boolean *)nexus_memory_allocate((nexus_data_list_length_get(state.names) + 1) * NEXUS_SIZEOF(boolean));
  nexus_memory_bytes_clear(state.reported, (nexus_data_list_length_get(state.names) + 1) * NEXUS_SIZEOF(boolean));

  for (i = 0; i < nexus_data_list_length_get(state.names); i++) {
    l_genesis_left_visit(&state, (const char *)nexus_data_list_pointer_at(state.names, i));
  }

  nexus_memory_deallocate(state.reported);
  nexus_data_list_destroy(state.names);
  nexus_data_list_destroy(state.marks);
  nexus_data_list_destroy(state.path);
}

/* Summary: a repetition whose body can match nothing would loop without progress. */
static void l_genesis_progress_walk(LGAnalyzer *analyzer, const LGExpr *expression) {
  PharosDiagnostic *diagnostic;
  uint32            i;

  if (expression == NULL) {
    return;
  }

  switch (expression->kind) {
  case LGE_UNION:
  case LGE_CONCAT:
    for (i = 0; i < expression->item_count; i++) {
      l_genesis_progress_walk(analyzer, expression->items[i]);
    }
    break;
  case LGE_GROUP:
    l_genesis_progress_walk(analyzer, expression->child);
    break;
  case LGE_REPEAT:
    if (expression->repeat_op != '?' && !(expression->repeat_op == '{' && expression->hi <= 1) &&
        l_genesis_nullable(analyzer->model, expression->child)) {
      diagnostic = pharos_diagnostics_report((analyzer->compiler)->reporting, PDS_ERROR, "G0245", expression->span,
                                             "the repeated expression can match the empty input");
      pharos_diagnostics_label_add(diagnostic, expression->child->span, TRUE, "this can succeed without consuming a token");
      pharos_diagnostics_label_add(diagnostic, expression->span, FALSE, "so this repetition would never end");
      pharos_diagnostics_help_add(diagnostic, "repeat something that always consumes a token; move an `?` or `*` out of the repeated part");
      l_genesis_error(analyzer);
    }

    l_genesis_progress_walk(analyzer, expression->child);
    break;
  default:
    break;
  }
}

/* ---- shadowed alternatives ---- */

static boolean l_genesis_equal(const LGExpr *left, const LGExpr *right) {
  uint32 i;

  if (left == right) {
    return TRUE;
  }

  if (left == NULL || right == NULL || left->kind != right->kind) {
    return FALSE;
  }

  switch (left->kind) {
  case LGE_TOKEN:
  case LGE_RULE:
  case LGE_FRAGMENT:
    return nexus_strings_string_equals(left->name, right->name) &&
           ((left->qualifier == NULL && right->qualifier == NULL) ||
            (left->qualifier != NULL && right->qualifier != NULL && nexus_strings_string_equals(left->qualifier, right->qualifier)));
  case LGE_UNION:
  case LGE_CONCAT:
    if (left->item_count != right->item_count) {
      return FALSE;
    }

    for (i = 0; i < left->item_count; i++) {
      if (!l_genesis_equal(left->items[i], right->items[i])) {
        return FALSE;
      }
    }

    return TRUE;
  case LGE_REPEAT:
    return left->repeat_op == right->repeat_op && left->lo == right->lo && left->hi == right->hi && l_genesis_equal(left->child, right->child);
  case LGE_GROUP:
    return l_genesis_equal(left->child, right->child);
  case LGE_EMPTY:
  case LGE_NONE:
    return TRUE;
  default:
    return FALSE;
  }
}

/* Summary: the sequence an alternative consists of: a concatenation's items, or the alternative itself. */
static uint32 l_genesis_sequence(const LGExpr *alternative, const LGExpr *const **out_items, const LGExpr **out_single) {
  if (alternative->kind == LGE_CONCAT) {
    *out_items = (const LGExpr *const *)alternative->items;

    return alternative->item_count;
  }

  *out_single = alternative;
  *out_items  = out_single;

  return 1;
}

static void l_genesis_shadow_check(LGAnalyzer *analyzer, const LGExpr *choice, LContextaChoiceMode mode) {
  uint32 i;
  uint32 j;

  if (mode != LCCM_ORDERED && mode != LCCM_DEFAULT) {
    return;
  }

  for (i = 0; i < choice->item_count; i++) {
    for (j = i + 1; j < choice->item_count; j++) {
      const LGExpr *const *first;
      const LGExpr *const *second;
      const LGExpr        *first_single;
      const LGExpr        *second_single;
      uint32               first_count;
      uint32               second_count;
      uint32               k;
      boolean              prefix;

      first_single  = NULL;
      second_single = NULL;
      first_count   = l_genesis_sequence(choice->items[i], &first, &first_single);
      second_count  = l_genesis_sequence(choice->items[j], &second, &second_single);

      if (first_count > second_count ||
          (first_count == second_count && l_genesis_nullable(analyzer->model, choice->items[i]) == FALSE && first_count == second_count)) {
        if (first_count > second_count || !l_genesis_equal(choice->items[i], choice->items[j])) {
          continue;
        }
      }

      prefix = TRUE;

      for (k = 0; k < first_count && prefix; k++) {
        prefix = l_genesis_equal(first[k], second[k]);
      }

      if (prefix) {
        PharosDiagnostic *diagnostic;

        diagnostic = pharos_diagnostics_report((analyzer->compiler)->reporting, PDS_WARNING, "W0304", choice->items[j]->span,
                                               "this alternative can never be chosen");
        pharos_diagnostics_label_add(diagnostic, choice->items[j]->span, TRUE, "it begins with everything the earlier alternative accepts");
        pharos_diagnostics_label_add(diagnostic, choice->items[i]->span, FALSE, "this alternative is tried first, and its success is final");
        pharos_diagnostics_help_add(diagnostic, "put the longer alternative first, or use `@choice(ChoiceMode.LONGEST)` on the group");

        return;
      }
    }
  }
}

static void l_genesis_shadow_walk(LGAnalyzer *analyzer, const LGExpr *expression, LContextaChoiceMode inherited) {
  LContextaChoiceMode mode;
  uint32              i;

  if (expression == NULL) {
    return;
  }

  switch (expression->kind) {
  case LGE_UNION:
    l_genesis_shadow_check(analyzer, expression, inherited);

    for (i = 0; i < expression->item_count; i++) {
      l_genesis_shadow_walk(analyzer, expression->items[i], LCCM_DEFAULT);
    }
    break;
  case LGE_CONCAT:
    for (i = 0; i < expression->item_count; i++) {
      l_genesis_shadow_walk(analyzer, expression->items[i], LCCM_DEFAULT);
    }
    break;
  case LGE_REPEAT:
    l_genesis_shadow_walk(analyzer, expression->child, LCCM_DEFAULT);
    break;
  case LGE_GROUP:
    mode = LCCM_DEFAULT;

    for (i = 0; i < expression->annotation_count; i++) {
      if (nexus_strings_string_equals(expression->annotations[i].name, (logos_languages_genesis_spec_word_text_get(LGSW_CHOICE) + 1)) &&
          expression->annotations[i].argument_count == 1) {
        mode = LCCM_DEFAULT;

        if (expression->annotations[i].arguments[0]->kind == LGE_ENUM) {
          if (nexus_strings_string_equals(expression->annotations[i].arguments[0]->name, "LONGEST")) {
            mode = LCCM_LONGEST;
          } else if (nexus_strings_string_equals(expression->annotations[i].arguments[0]->name, "UNAMBIGUOUS")) {
            mode = LCCM_UNAMBIGUOUS;
          } else {
            mode = LCCM_ORDERED;
          }
        }
      }
    }

    l_genesis_shadow_walk(analyzer, expression->child, mode);
    break;
  default:
    break;
  }
}

/* ---- rule-level checks ---- */

static LContextaChoiceMode l_genesis_rule_choice(const LGRuleModel *rule) {
  uint32 i;

  for (i = 0; i < rule->annotation_count; i++) {
    if (nexus_strings_string_equals(rule->annotations[i].name, (logos_languages_genesis_spec_word_text_get(LGSW_CHOICE) + 1)) &&
        rule->annotations[i].argument_count == 1 && rule->annotations[i].arguments[0]->kind == LGE_ENUM) {
      if (nexus_strings_string_equals(rule->annotations[i].arguments[0]->name, "LONGEST")) {
        return LCCM_LONGEST;
      }

      if (nexus_strings_string_equals(rule->annotations[i].arguments[0]->name, "UNAMBIGUOUS")) {
        return LCCM_UNAMBIGUOUS;
      }

      return LCCM_ORDERED;
    }
  }

  return LCCM_DEFAULT;
}

static void l_genesis_analyze_rules(LGAnalyzer *analyzer) {
  LGModel      *model;
  LGRuleModel  *rule;
  LGGrammarWalk walk;
  uint_large    i;
  uint32        j;

  model         = analyzer->model;
  walk.analyzer = analyzer;
  walk.in_embed = FALSE;

  for (i = 0; i < nexus_data_list_length_get(model->rules); i++) {
    rule = (LGRuleModel *)nexus_data_list_pointer_at(model->rules, i);
    l_genesis_grammar_walk(&walk, rule->expression);

    for (j = 0; j < rule->annotation_count; j++) {
      l_genesis_annotation_check(analyzer, &rule->annotations[j]);
    }
  }

  for (i = 0; i < nexus_data_list_length_get(model->pratts); i++) {
    LGPrattModel     *pratt;
    PharosDiagnostic *diagnostic;
    uint_large        k;

    pratt = (LGPrattModel *)nexus_data_list_pointer_at(model->pratts, i);
    l_genesis_grammar_walk(&walk, pratt->atom);

    if (pratt->atom != NULL && l_genesis_nullable(model, pratt->atom)) {
      diagnostic = pharos_diagnostics_report((analyzer->compiler)->reporting, PDS_ERROR, "G0246", pratt->atom->span,
                                             "the atom of `#%s` can match the empty input", pratt->name);
      pharos_diagnostics_label_add(diagnostic, pratt->atom->span, TRUE, "an operand must consume at least one token");
      l_genesis_error(analyzer);
    }

    for (k = 0; k < nexus_data_list_length_get(pratt->entries); k++) {
      const LGEntryModel *entry;
      LGToken            *token;
      uint_large          earlier;

      entry = (const LGEntryModel *)nexus_data_list_pointer_at(pratt->entries, k);
      l_genesis_grammar_walk(&walk, entry->token);
      token = entry->token->kind == LGE_TOKEN ? logos_languages_genesis_analysis_token_find(model, entry->token->name, NULL) : NULL;

      for (earlier = 0; earlier < k; earlier++) {
        const LGEntryModel *other;

        other = (const LGEntryModel *)nexus_data_list_pointer_at(pratt->entries, earlier);

        if (token != NULL && other->token->kind == LGE_TOKEN && nexus_strings_string_equals(other->token->name, entry->token->name) &&
            (nexus_strings_string_equals(other->kind, entry->kind) ||
             (!nexus_strings_string_equals(other->kind, "prefix") && !nexus_strings_string_equals(entry->kind, "prefix")))) {
          diagnostic = pharos_diagnostics_report((analyzer->compiler)->reporting, PDS_ERROR, "G0247", entry->span,
                                                 "`$%s` is an operator of `#%s` more than once", entry->token->name, pratt->name);
          pharos_diagnostics_label_add(diagnostic, entry->span, TRUE, "declared again here");
          pharos_diagnostics_label_add(diagnostic, other->span, FALSE, "first declared here");
          pharos_diagnostics_note_add(diagnostic,
                                      "a token can be one prefix operator and one infix or postfix operator, but not two of the same role");
          l_genesis_error(analyzer);
        }
      }

      if (nexus_strings_string_equals(entry->kind, "infix") && entry->left_bp > entry->right_bp + 1) {
        diagnostic = pharos_diagnostics_report((analyzer->compiler)->reporting, PDS_WARNING, "W0305", entry->span,
                                               "`left_bp` is much higher than `right_bp` on this infix operator");
        pharos_diagnostics_label_add(diagnostic, entry->span, TRUE, "left_bp: %lld, right_bp: %lld", (long long)entry->left_bp,
                                     (long long)entry->right_bp);
        pharos_diagnostics_note_add(
            diagnostic, "(30, 31) groups left, (80, 80) groups right; a left power above the right one makes the operator hard to reason about");
      }
    }
  }
}

/* ---- reachability ---- */

static void l_genesis_mark_expression(LGModel *model, const LGExpr *expression);

static void l_genesis_mark_rule(LGModel *model, const char *name) {
  LGRuleModel  *rule;
  LGPrattModel *pratt;
  LGEmbedModel *embed;
  uint_large    i;

  rule = logos_languages_genesis_analysis_rule_find(model, name);

  if (rule != NULL) {
    if (!rule->used) {
      rule->used = TRUE;
      l_genesis_mark_expression(model, rule->expression);
    }

    return;
  }

  pratt = logos_languages_genesis_analysis_pratt_find(model, name);

  if (pratt != NULL) {
    if (!pratt->used) {
      pratt->used = TRUE;
      l_genesis_mark_expression(model, pratt->atom);

      for (i = 0; i < nexus_data_list_length_get(pratt->entries); i++) {
        l_genesis_mark_expression(model, ((const LGEntryModel *)nexus_data_list_pointer_at(pratt->entries, i))->token);
      }
    }

    return;
  }

  embed = logos_languages_genesis_analysis_embed_find(model, name);

  if (embed != NULL) {
    embed->used = TRUE;
  }
}

static void l_genesis_mark_expression(LGModel *model, const LGExpr *expression) {
  uint32 i;

  if (expression == NULL) {
    return;
  }

  switch (expression->kind) {
  case LGE_RULE:
    if (expression->qualifier == NULL) {
      l_genesis_mark_rule(model, expression->name);
    }
    break;
  case LGE_UNION:
  case LGE_CONCAT:
    for (i = 0; i < expression->item_count; i++) {
      l_genesis_mark_expression(model, expression->items[i]);
    }
    break;
  case LGE_REPEAT:
    l_genesis_mark_expression(model, expression->child);
    break;
  case LGE_GROUP:
    l_genesis_mark_expression(model, expression->child);

    for (i = 0; i < expression->annotation_count; i++) {
      uint32 a;

      for (a = 0; a < expression->annotations[i].argument_count; a++) {
        l_genesis_mark_expression(model, expression->annotations[i].arguments[a]);
      }
    }
    break;
  default:
    break;
  }
}

/* ---------------------------------------------------------------------------- */
/* THE PARSER BLOCK                                                              */
/* ---------------------------------------------------------------------------- */

static const char *const l_genesis_parser_keys[] = {"entry", "global", "choice", "lexical_selection", "engine"};

static void l_genesis_analyze_parser(LGAnalyzer *analyzer, NexusDataList *globals) {
  LGModel            *model;
  PharosDiagnostic   *diagnostic;
  const LGExpr       *entry;
  uint32              i;
  uint32              j;
  const char         *suggestion;
  LContextaChoiceMode mode;

  model = analyzer->model;
  entry = NULL;

  if (!model->has_parser) {
    diagnostic =
        pharos_diagnostics_report((analyzer->compiler)->reporting, PDS_ERROR, "G0220", model->unit->name_span, "the language has no `PARSER` block");
    pharos_diagnostics_label_add(diagnostic, model->unit->name_span, TRUE, "`%s` needs a parser configuration", model->unit->name);
    pharos_diagnostics_help_add(diagnostic, "add `PARSER { entry = #your_start_rule }`");
    l_genesis_error(analyzer);

    return;
  }

  for (i = 0; i < model->parser_count; i++) {
    const char   *key;
    const LGExpr *value;

    key   = model->parser_keys[i];
    value = model->parser_values[i];

    for (j = 0; j < i; j++) {
      if (nexus_strings_string_equals(model->parser_keys[j], key)) {
        diagnostic = pharos_diagnostics_report((analyzer->compiler)->reporting, PDS_ERROR, "G0227", value->span,
                                               "`%s` is set more than once in the PARSER block", key);
        pharos_diagnostics_label_add(diagnostic, value->span, TRUE, "set again here");
        pharos_diagnostics_label_add(diagnostic, model->parser_values[j]->span, FALSE, "first set here");
        l_genesis_error(analyzer);
      }
    }

    if (nexus_strings_string_equals(key, "entry")) {
      entry = value;

      if (value->kind != LGE_RULE || value->qualifier != NULL) {
        diagnostic = pharos_diagnostics_report((analyzer->compiler)->reporting, PDS_ERROR, "G0222", value->span, "`entry` must name a grammar rule");
        pharos_diagnostics_label_add(diagnostic, value->span, TRUE, "expected a `#rule`");
        l_genesis_error(analyzer);
      } else if (logos_languages_genesis_analysis_rule_find(model, value->name) == NULL &&
                 logos_languages_genesis_analysis_pratt_find(model, value->name) == NULL &&
                 logos_languages_genesis_analysis_embed_find(model, value->name) == NULL) {
        l_genesis_unknown_symbol(analyzer, value, '#', NULL, "G0222", "the entry rule");
      }
    } else if (nexus_strings_string_equals(key, "global")) {
      const LGExpr *const *items;
      const LGExpr        *single;
      uint32               count;

      single = value;
      items  = &single;
      count  = 1;

      if (value->kind == LGE_CONCAT) {
        items = (const LGExpr *const *)value->items;
        count = value->item_count;
      }

      for (j = 0; j < count; j++) {
        const LGExpr *item;
        LGRuleModel  *rule;

        item = items[j];

        if (item->kind != LGE_RULE || item->qualifier != NULL) {
          diagnostic = pharos_diagnostics_report((analyzer->compiler)->reporting, PDS_ERROR, "G0222", item->span, "`global` lists grammar rules");
          pharos_diagnostics_label_add(diagnostic, item->span, TRUE, "expected a `#rule`");
          l_genesis_error(analyzer);
          continue;
        }

        rule = logos_languages_genesis_analysis_rule_find(model, item->name);

        if (rule == NULL) {
          l_genesis_unknown_symbol(analyzer, item, '#', NULL, "G0222", "the global rule");
          continue;
        }

        (void)nexus_data_list_append_pointer(globals, rule);
      }
    } else if (nexus_strings_string_equals(key, "choice")) {
      l_genesis_choice_mode_check(analyzer, value, &mode);
    } else if (nexus_strings_string_equals(key, "lexical_selection")) {
      if (value->kind != LGE_IDENT ||
          (!nexus_strings_string_equals(value->name, "grammar_expected") && !nexus_strings_string_equals(value->name, "context"))) {
        diagnostic = pharos_diagnostics_report((analyzer->compiler)->reporting, PDS_ERROR, "G0225", value->span, "unknown `lexical_selection`");
        pharos_diagnostics_label_add(diagnostic, value->span, TRUE, "expected `grammar_expected` or `context`");
        pharos_diagnostics_note_add(diagnostic, "`context` (the default) lets every token of the active context compete; `grammar_expected` "
                                                "restricts them to the tokens the grammar accepts at that position");
        l_genesis_error(analyzer);
      }
    } else if (nexus_strings_string_equals(key, "engine")) {
      if (value->kind != LGE_IDENT ||
          (!nexus_strings_string_equals(value->name, "backtracking") && !nexus_strings_string_equals(value->name, "earley"))) {
        diagnostic = pharos_diagnostics_report((analyzer->compiler)->reporting, PDS_ERROR, "G0227", value->span, "unknown `engine`");
        pharos_diagnostics_label_add(diagnostic, value->span, TRUE, "expected `backtracking` or `earley`");
        pharos_diagnostics_note_add(diagnostic,
                                    "`backtracking` (the default) is a parser with PEG semantics and ordered, longest or unambiguous choice; "
                                    "`earley` accepts any context-free grammar, keeps every derivation and is for imported grammars");
        l_genesis_error(analyzer);
      }
    } else {
      diagnostic = pharos_diagnostics_report((analyzer->compiler)->reporting, PDS_ERROR, "G0226", model->parser_values[i]->span,
                                             "unknown PARSER setting `%s`", key);
      pharos_diagnostics_label_add(diagnostic, model->parser_values[i]->span, TRUE, "not a setting");
      suggestion = nexus_strings_string_closest(key, l_genesis_parser_keys, 5);

      if (suggestion != NULL) {
        pharos_diagnostics_help_add(diagnostic, "did you mean `%s`?", suggestion);
      } else {
        pharos_diagnostics_note_add(diagnostic, "the settings are entry, global, choice, lexical_selection and engine");
      }

      l_genesis_error(analyzer);
    }
  }

  if (entry == NULL) {
    diagnostic =
        pharos_diagnostics_report((analyzer->compiler)->reporting, PDS_ERROR, "G0221", model->parser_span, "the PARSER block has no `entry`");
    pharos_diagnostics_label_add(diagnostic, model->parser_span, TRUE, "parsing needs a start rule");
    pharos_diagnostics_help_add(diagnostic, "add `entry = #document` naming the rule parsing starts at");
    l_genesis_error(analyzer);
  }
}

/* ---------------------------------------------------------------------------- */
/* EMBEDS                                                                        */
/* ---------------------------------------------------------------------------- */

static const LGExpr *l_genesis_embed_property(const LGEmbedModel *embed, const char *key) {
  uint32 i;

  for (i = 0; i < embed->count; i++) {
    if (nexus_strings_string_equals(embed->keys[i], key)) {
      return embed->values[i];
    }
  }

  return NULL;
}

static void l_genesis_analyze_embeds(LGAnalyzer *analyzer) {
  static const char *const keys[] = {"mode", "language", "entry", "open", "close", "close_boundary", "content", "source"};
  LGModel                 *model;
  uint_large               i;
  uint32                   j;

  model = analyzer->model;

  for (i = 0; i < nexus_data_list_length_get(model->embeds); i++) {
    LGEmbedModel     *embed;
    PharosDiagnostic *diagnostic;
    const LGExpr     *mode;
    const LGExpr     *language;
    const LGExpr     *entry;
    const LGImport   *import;
    boolean           region;
    const char       *suggestion;
    uint32            k;

    embed = (LGEmbedModel *)nexus_data_list_pointer_at(model->embeds, i);

    for (j = 0; j < embed->count; j++) {
      boolean known;

      known = FALSE;

      for (k = 0; k < NEXUS_ARRAY_SIZE_ELEMENTS(keys); k++) {
        known = known || nexus_strings_string_equals(keys[k], embed->keys[j]);
      }

      if (!known) {
        diagnostic = pharos_diagnostics_report((analyzer->compiler)->reporting, PDS_ERROR, "G0250", embed->values[j]->span,
                                               "unknown EMBED setting `%s`", embed->keys[j]);
        pharos_diagnostics_label_add(diagnostic, embed->values[j]->span, TRUE, "not a setting");
        suggestion = nexus_strings_string_closest(embed->keys[j], keys, NEXUS_ARRAY_SIZE_ELEMENTS(keys));

        if (suggestion != NULL) {
          pharos_diagnostics_help_add(diagnostic, "did you mean `%s`?", suggestion);
        }

        l_genesis_error(analyzer);
      }
    }

    mode     = l_genesis_embed_property(embed, "mode");
    language = l_genesis_embed_property(embed, "language");
    entry    = l_genesis_embed_property(embed, "entry");

    if (mode == NULL || mode->kind != LGE_IDENT ||
        (!nexus_strings_string_equals(mode->name, "region") && !nexus_strings_string_equals(mode->name, "token"))) {
      diagnostic = pharos_diagnostics_report((analyzer->compiler)->reporting, PDS_ERROR, "G0251", mode != NULL ? mode->span : embed->span,
                                             "an EMBED needs `mode = region` or `mode = token`");
      pharos_diagnostics_label_add(diagnostic, mode != NULL ? mode->span : embed->span, TRUE, "the mode is missing or unknown");
      l_genesis_error(analyzer);

      continue;
    }

    region = nexus_strings_string_equals(mode->name, "region");

    if (language == NULL || language->kind != LGE_IDENT || entry == NULL || entry->kind != LGE_RULE || entry->qualifier == NULL) {
      diagnostic = pharos_diagnostics_report((analyzer->compiler)->reporting, PDS_ERROR, "G0252", embed->span,
                                             "an EMBED needs `language = Name` and `entry = Name.#rule`");
      pharos_diagnostics_label_add(diagnostic, embed->span, TRUE, "the guest language or its entry rule is missing");
      l_genesis_error(analyzer);

      continue;
    }

    import = logos_languages_genesis_analysis_import_find(model, language->name);

    if (import == NULL || import->entry->kind != LGUK_LANGUAGE) {
      diagnostic = pharos_diagnostics_report((analyzer->compiler)->reporting, PDS_ERROR, "G0253", language->span, "`%s` is not an imported language",
                                             language->name);
      pharos_diagnostics_label_add(diagnostic, language->span, TRUE, "no `IMPORT ... AS %s` of a language", language->name);
      pharos_diagnostics_help_add(diagnostic, "import the guest language first: `IMPORT %s AS %s { #document }`", language->name, language->name);
      l_genesis_error(analyzer);

      continue;
    }

    if (!nexus_strings_string_equals(entry->qualifier, language->name)) {
      diagnostic = pharos_diagnostics_report((analyzer->compiler)->reporting, PDS_ERROR, "G0254", entry->span,
                                             "the entry rule belongs to `%s`, not to the language `%s`", entry->qualifier, language->name);
      pharos_diagnostics_label_add(diagnostic, entry->span, TRUE, "qualified with a different language");
      pharos_diagnostics_label_add(diagnostic, language->span, FALSE, "the embedded language is `%s`", language->name);
      l_genesis_error(analyzer);

      continue;
    }

    {
      boolean listed;

      listed = FALSE;

      for (k = 0; k < import->item_count; k++) {
        listed = listed || (import->items[k][0] == '#' && nexus_strings_string_equals(import->items[k] + 1, entry->name));
      }

      if (!listed) {
        diagnostic = pharos_diagnostics_report((analyzer->compiler)->reporting, PDS_ERROR, "G0255", entry->span, "`#%s` is not imported from `%s`",
                                               entry->name, import->name);
        pharos_diagnostics_label_add(diagnostic, entry->span, TRUE, "used here");
        pharos_diagnostics_label_add(diagnostic, import->span, FALSE, "but not listed in this import");
        pharos_diagnostics_help_add(diagnostic, "add `#%s` to the import list: `IMPORT %s { ..., #%s }`", entry->name, import->name, entry->name);
        l_genesis_error(analyzer);
      }
    }

    if (region) {
      static const char *const needed[] = {"open", "close"};

      for (k = 0; k < 2; k++) {
        const LGExpr *token;

        token = l_genesis_embed_property(embed, needed[k]);

        if (token == NULL || token->kind != LGE_TOKEN) {
          diagnostic = pharos_diagnostics_report((analyzer->compiler)->reporting, PDS_ERROR, "G0256", token != NULL ? token->span : embed->span,
                                                 "a region EMBED needs `%s = $token`", needed[k]);
          pharos_diagnostics_label_add(diagnostic, token != NULL ? token->span : embed->span, TRUE, "the %s token is missing", needed[k]);
          l_genesis_error(analyzer);
        } else if (logos_languages_genesis_analysis_token_find(model, token->name, NULL) == NULL) {
          l_genesis_unknown_symbol(analyzer, token, '$', NULL, "G0240", "the token");
        } else {
          logos_languages_genesis_analysis_token_find(model, token->name, NULL)->used = TRUE;
        }
      }
    } else {
      const LGExpr *token;
      const LGExpr *content;

      token   = l_genesis_embed_property(embed, "source");
      content = l_genesis_embed_property(embed, "content");

      if (token == NULL || token->kind != LGE_TOKEN) {
        diagnostic = pharos_diagnostics_report((analyzer->compiler)->reporting, PDS_ERROR, "G0256", token != NULL ? token->span : embed->span,
                                               "a token EMBED needs `source = $token`");
        pharos_diagnostics_label_add(diagnostic, token != NULL ? token->span : embed->span, TRUE, "the source token is missing");
        l_genesis_error(analyzer);
      } else if (logos_languages_genesis_analysis_token_find(model, token->name, NULL) == NULL) {
        l_genesis_unknown_symbol(analyzer, token, '$', NULL, "G0240", "the token");
      } else {
        logos_languages_genesis_analysis_token_find(model, token->name, NULL)->used = TRUE;
      }

      if (content == NULL || content->kind != LGE_CALL || !nexus_strings_string_equals(content->name, "inner_bytes") || content->item_count != 2 ||
          content->items[0]->kind != LGE_INT || content->items[1]->kind != LGE_INT || content->items[0]->integer < 0 ||
          content->items[1]->integer < 0) {
        diagnostic = pharos_diagnostics_report((analyzer->compiler)->reporting, PDS_ERROR, "G0257", content != NULL ? content->span : embed->span,
                                               "a token EMBED needs `content = inner_bytes(prefix, suffix)`");
        pharos_diagnostics_label_add(diagnostic, content != NULL ? content->span : embed->span, TRUE, "expected two byte counts");
        l_genesis_error(analyzer);
      }
    }
  }
}

/* ---------------------------------------------------------------------------- */
/* ENTRY                                                                         */
/* ---------------------------------------------------------------------------- */

boolean logos_languages_genesis_analysis_language_check(LGenesisCompiler *compiler, LGModel *model) {
  LGAnalyzer     analyzer;
  NexusDataList *globals;
  uint_large     i;
  uint_large     j;

  analyzer.compiler = compiler;
  analyzer.model    = model;
  analyzer.ok       = TRUE;
  globals           = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 4);

  l_genesis_analyze_contexts(&analyzer);
  l_genesis_analyze_rules(&analyzer);
  l_genesis_analyze_embeds(&analyzer);
  l_genesis_analyze_parser(&analyzer, globals);

  if (analyzer.ok) {
    PharosDiagnostic *diagnostic;

    l_genesis_nullable_compute(model);

    for (i = 0; i < nexus_data_list_length_get(globals); i++) {
      const LGRuleModel *rule;

      rule = (const LGRuleModel *)nexus_data_list_pointer_at(globals, i);

      if (rule->nullable) {
        diagnostic = pharos_diagnostics_report((compiler)->reporting, PDS_ERROR, "G0223", rule->span,
                                               "the global rule `#%s` can match the empty input", rule->name);
        pharos_diagnostics_label_add(diagnostic, rule->span, TRUE, "a global rule must consume at least one token");
        pharos_diagnostics_note_add(diagnostic, "it is tried at every token boundary; matching nothing would make no progress");
        pharos_diagnostics_help_add(diagnostic, "make the rule non-nullable, for example `#%s = $whitespace` instead of `$whitespace*`", rule->name);
        analyzer.ok = FALSE;
      }
    }

    for (i = 0; i < nexus_data_list_length_get(model->rules); i++) {
      l_genesis_progress_walk(&analyzer, ((const LGRuleModel *)nexus_data_list_pointer_at(model->rules, i))->expression);
    }

    /* Only a PEG parser cannot grow a left-recursive rule; the Earley engine takes any context-free grammar. */
    {
      boolean earley;
      uint32  setting;

      earley = FALSE;

      for (setting = 0; setting < model->parser_count; setting++) {
        earley = earley || (nexus_strings_string_equals(model->parser_keys[setting], "engine") && model->parser_values[setting]->kind == LGE_IDENT &&
                            nexus_strings_string_equals(model->parser_values[setting]->name, "earley"));
      }

      if (!earley) {
        l_genesis_left_recursion(&analyzer);
      }

      /* Precedence filters the derivations of the Earley engine; a PEG parser takes the first alternative that succeeds and ignores it. */
      for (i = 0; i < nexus_data_list_length_get(model->rules) && !earley; i++) {
        const LGRuleModel *rule;
        uint32             a;

        rule = (const LGRuleModel *)nexus_data_list_pointer_at(model->rules, i);

        for (a = 0; a < rule->annotation_count; a++) {
          if (nexus_strings_string_equals(rule->annotations[a].name, (logos_languages_genesis_spec_word_text_get(LGSW_PREC) + 1))) {
            diagnostic = pharos_diagnostics_report((compiler)->reporting, PDS_WARNING, "W0308", rule->annotations[a].span,
                                                   "`@prec` has no effect without the Earley engine");
            pharos_diagnostics_label_add(diagnostic, rule->annotations[a].span, TRUE, "the backtracking engine ignores it");
            pharos_diagnostics_help_add(diagnostic, "select the engine with `PARSER { engine = earley }`, or order the alternatives");
          }
        }
      }
    }

    for (i = 0; i < nexus_data_list_length_get(model->rules); i++) {
      const LGRuleModel *rule;

      rule = (const LGRuleModel *)nexus_data_list_pointer_at(model->rules, i);
      l_genesis_shadow_walk(&analyzer, rule->expression, l_genesis_rule_choice(rule));
    }

    /* Reachability warnings: the entry, the global rules and everything they reference are used. */
    for (j = 0; j < model->parser_count; j++) {
      if (nexus_strings_string_equals(model->parser_keys[j], "entry") && model->parser_values[j]->kind == LGE_RULE) {
        l_genesis_mark_rule(model, model->parser_values[j]->name);
      }
    }

    for (i = 0; i < nexus_data_list_length_get(globals); i++) {
      l_genesis_mark_rule(model, ((const LGRuleModel *)nexus_data_list_pointer_at(globals, i))->name);
    }

    for (i = 0; i < nexus_data_list_length_get(model->rules); i++) {
      const LGRuleModel *rule;
      uint32             a;

      rule = (const LGRuleModel *)nexus_data_list_pointer_at(model->rules, i);

      if (rule->used) {
        for (a = 0; a < rule->annotation_count; a++) {
          uint32 b;

          for (b = 0; b < rule->annotations[a].argument_count; b++) {
            l_genesis_mark_expression(model, rule->annotations[a].arguments[b]);
          }
        }
      }
    }

    for (i = 0; i < nexus_data_list_length_get(model->rules); i++) {
      const LGRuleModel *rule;

      rule = (const LGRuleModel *)nexus_data_list_pointer_at(model->rules, i);

      if (!rule->used && rule->name[0] != '\0' && !nexus_strings_string_contains(rule->name, "@")) {
        diagnostic = pharos_diagnostics_report((compiler)->reporting, PDS_WARNING, "W0303", rule->span, "the rule `#%s` is never used", rule->name);
        pharos_diagnostics_label_add(diagnostic, rule->span, TRUE, "not reachable from the entry or a global rule");
        pharos_diagnostics_help_add(diagnostic, "remove it, or reference it from a rule that is used");
      }
    }

    for (i = 0; i < nexus_data_list_length_get(model->contexts); i++) {
      const LGContextModel *context;

      context = (const LGContextModel *)nexus_data_list_pointer_at(model->contexts, i);

      for (j = 0; j < nexus_data_list_length_get(context->tokens); j++) {
        const LGToken *token;

        token = (const LGToken *)nexus_data_list_pointer_at(context->tokens, j);

        if (!token->fragment && !token->used && !nexus_strings_string_contains(token->name, "@")) {
          diagnostic = pharos_diagnostics_report((compiler)->reporting, PDS_WARNING, "W0301", token->span,
                                                 "the token `$%s` is never used by a grammar rule", token->name);
          pharos_diagnostics_label_add(diagnostic, token->span, TRUE, "no rule references it");
          pharos_diagnostics_note_add(diagnostic, "it still competes in lexing, so it can swallow input that another token should match");
        }
      }
    }
  }

  nexus_data_list_destroy(globals);

  return analyzer.ok;
}

/* ---------------------------------------------------------------------------- */
/* SETS OF CHARACTERS                                                            */
/* ---------------------------------------------------------------------------- */

#define LG_CODEPOINT_MAX 0x10FFFFU

static int32 l_genesis_range_compare(const void *left, const void *right, void *user_data) {
  const LGCodepointRange *a;
  const LGCodepointRange *b;

  (void)user_data;
  a = (const LGCodepointRange *)left;
  b = (const LGCodepointRange *)right;

  return (a->low > b->low) - (a->low < b->low);
}

/* Summary: sorts the ranges of list and merges those that overlap or touch. */
static void l_genesis_ranges_normalize(NexusDataList *list) {
  NexusDataArray    view;
  uint_large        i;
  uint_large        out;
  LGCodepointRange *ranges;

  if (nexus_data_list_length_get(list) < 2) {
    return;
  }

  ranges = (LGCodepointRange *)nexus_data_list_at(list, 0);
  view   = nexus_data_array_view_create(ranges, NEXUS_SIZEOF(LGCodepointRange), nexus_data_list_length_get(list));
  nexus_data_array_sort(&view, l_genesis_range_compare, NULL);
  out = 0;

  for (i = 1; i < nexus_data_list_length_get(list); i++) {
    if (ranges[i].low <= ranges[out].high + 1 && ranges[out].high != 0xFFFFFFFFU) {
      if (ranges[i].high > ranges[out].high) {
        ranges[out].high = ranges[i].high;
      }
    } else {
      out++;
      ranges[out] = ranges[i];
    }
  }

  while (nexus_data_list_length_get(list) > out + 1) {
    LGCodepointRange dropped;

    (void)nexus_data_list_pop(list, &dropped);
  }
}

static boolean l_genesis_charset_collect(const LGContextModel *context, const LGExpr *expression, NexusDataList *out, uint32 depth) {
  LGCodepointRange range;
  uint32           i;

  if (expression == NULL || depth > 64) {
    return FALSE;
  }

  switch (expression->kind) {
  case LGE_STRING:
    if (expression->codepoint_count != 1) {
      return FALSE;
    }

    range.low  = expression->codepoints[0];
    range.high = expression->codepoints[0];
    (void)nexus_data_list_append(out, &range);

    return TRUE;
  case LGE_RANGE:
    range.low  = expression->lo;
    range.high = expression->hi;
    (void)nexus_data_list_append(out, &range);

    return TRUE;
  case LGE_ANY:
    range.low  = 0;
    range.high = LG_CODEPOINT_MAX;
    (void)nexus_data_list_append(out, &range);

    return TRUE;
  case LGE_UNION:
    for (i = 0; i < expression->item_count; i++) {
      if (!l_genesis_charset_collect(context, expression->items[i], out, depth + 1)) {
        return FALSE;
      }
    }

    return TRUE;
  case LGE_GROUP:
    return l_genesis_charset_collect(context, expression->child, out, depth + 1);
  case LGE_FRAGMENT: {
    const LGToken *fragment;

    fragment = logos_languages_genesis_analysis_fragment_find(context, expression->name);

    return fragment != NULL && l_genesis_charset_collect(context, fragment->expression, out, depth + 1);
  }
  case LGE_NOT: {
    NexusDataList *inner;
    uint32         next;
    uint_large     k;
    boolean        ok;

    inner = nexus_data_list_create(NEXUS_SIZEOF(LGCodepointRange), 8);
    ok    = l_genesis_charset_collect(context, expression->child, inner, depth + 1);

    if (ok) {
      l_genesis_ranges_normalize(inner);
      next = 0;

      /* What the sorted ranges leave of 0..10FFFF. */
      for (k = 0; k < nexus_data_list_length_get(inner); k++) {
        const LGCodepointRange *each;

        each = (const LGCodepointRange *)nexus_data_list_at_const(inner, k);

        if (each->low > next) {
          range.low  = next;
          range.high = each->low - 1;
          (void)nexus_data_list_append(out, &range);
        }

        next = each->high >= LG_CODEPOINT_MAX ? LG_CODEPOINT_MAX + 1 : each->high + 1;
      }

      if (next <= LG_CODEPOINT_MAX) {
        range.low  = next;
        range.high = LG_CODEPOINT_MAX;
        (void)nexus_data_list_append(out, &range);
      }
    }

    nexus_data_list_destroy(inner);

    return ok;
  }
  case LGE_EMPTY:
  case LGE_TOKEN:
  case LGE_RULE:
  case LGE_PARAM:
  case LGE_CALL:
  case LGE_CONCAT:
  case LGE_REPEAT:
  case LGE_IF:
  case LGE_COMPARE:
  case LGE_NONE:
  case LGE_ENUM:
  case LGE_INT:
  case LGE_TEXT:
  case LGE_IDENT:
  default:
    return FALSE;
  }
}

boolean logos_languages_genesis_analysis_charset_get(const LGContextModel *context, const LGExpr *expression, NexusDataList *out) {
  if (!l_genesis_charset_collect(context, expression, out, 0)) {
    return FALSE;
  }

  l_genesis_ranges_normalize(out);

  return TRUE;
}
