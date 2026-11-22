#include <nexus.h>
#include <rf_bindings.h>

#include "../ruleforge.h"
#include "./r_internal.h"

/*
Game-time conditions. An expression in `when`, `coreConditions` or `scope` mixes two kinds of operands: compile-time values
(parameters, locals, @settings, dataset fields), which the evaluator folds away, and manifest conditions such as areaLevel or
baseType, which can only be decided by the game. This file partially evaluates such an expression into constants and
residual game conditions in disjunctive normal form, because a filter block is a conjunction and `or` means several blocks.
*/

#define R_CONDITION_CLAUSE_LIMIT 256U

/* ---------------------------------------------------------------------------- */
/* CLAUSES                                                                      */
/* ---------------------------------------------------------------------------- */

static RuleforgeOperator r_condition_operator_negate(RuleforgeOperator operation) {
  switch (operation) {
  case ROP_EQ:
    return ROP_NE;
  case ROP_NE:
    return ROP_EQ;
  case ROP_LT:
    return ROP_GE;
  case ROP_LE:
    return ROP_GT;
  case ROP_GT:
    return ROP_LE;
  case ROP_GE:
    return ROP_LT;
  }

  return ROP_NE;
}

static RuleforgeOperator r_condition_operator_flip(RuleforgeOperator operation) {
  switch (operation) {
  case ROP_LT:
    return ROP_GT;
  case ROP_LE:
    return ROP_GE;
  case ROP_GT:
    return ROP_LT;
  case ROP_GE:
    return ROP_LE;
  default:
    return operation;
  }
}

static void r_condition_constant(RuleforgeDnf *out, boolean value) {
  out->constant = TRUE;
  out->value    = value;
  out->count    = 0;
  out->clauses  = NULL;
}

static void r_condition_single(REval *eval, RuleforgeDnf *out, RuleforgeCondition *condition) {
  out->constant                 = FALSE;
  out->value                    = FALSE;
  out->count                    = 1;
  out->clauses                  = (RuleforgeClause *)nexus_data_arena_allocate(&eval->compilation->arena, NEXUS_SIZEOF(RuleforgeClause));
  out->clauses[0].count         = 1;
  out->clauses[0].conditions    = (RuleforgeCondition **)nexus_data_arena_allocate(&eval->compilation->arena, NEXUS_SIZEOF(RuleforgeCondition *));
  out->clauses[0].conditions[0] = condition;
}

/* Summary: a AND b. */
static boolean r_condition_and(REval *eval, const RuleforgeDnf *left, const RuleforgeDnf *right, RuleforgeSpan span, RuleforgeDnf *out) {
  uint32 i;
  uint32 j;
  uint32 k;
  uint32 index;

  if (left->constant) {
    if (!left->value) {
      r_condition_constant(out, FALSE);
    } else {
      *out = *right;
    }

    return TRUE;
  }

  if (right->constant) {
    if (!right->value) {
      r_condition_constant(out, FALSE);
    } else {
      *out = *left;
    }

    return TRUE;
  }

  if ((uint64)left->count * (uint64)right->count > R_CONDITION_CLAUSE_LIMIT) {
    ruleforge_eval_fail(eval, "RF0350", span, "split the condition into separate variants", "this condition expands to more than %lu alternatives",
                        (unsigned long)R_CONDITION_CLAUSE_LIMIT);

    return FALSE;
  }

  out->constant = FALSE;
  out->value    = FALSE;
  out->count    = left->count * right->count;
  out->clauses =
      (RuleforgeClause *)nexus_data_arena_allocate(&eval->compilation->arena, ((uint_large)out->count + 1U) * NEXUS_SIZEOF(RuleforgeClause));
  index = 0;

  for (i = 0; i < left->count; i++) {
    for (j = 0; j < right->count; j++) {
      RuleforgeClause *clause;

      clause             = &out->clauses[index++];
      clause->count      = left->clauses[i].count + right->clauses[j].count;
      clause->conditions = (RuleforgeCondition **)nexus_data_arena_allocate(&eval->compilation->arena,
                                                                            ((uint_large)clause->count + 1U) * NEXUS_SIZEOF(RuleforgeCondition *));

      for (k = 0; k < left->clauses[i].count; k++) {
        clause->conditions[k] = left->clauses[i].conditions[k];
      }

      for (k = 0; k < right->clauses[j].count; k++) {
        clause->conditions[left->clauses[i].count + k] = right->clauses[j].conditions[k];
      }
    }
  }

  return TRUE;
}

/* Summary: a OR b. */
static boolean r_condition_or(REval *eval, const RuleforgeDnf *left, const RuleforgeDnf *right, RuleforgeSpan span, RuleforgeDnf *out) {
  uint32 i;

  if (left->constant) {
    if (left->value) {
      r_condition_constant(out, TRUE);
    } else {
      *out = *right;
    }

    return TRUE;
  }

  if (right->constant) {
    if (right->value) {
      r_condition_constant(out, TRUE);
    } else {
      *out = *left;
    }

    return TRUE;
  }

  if (left->count + right->count > R_CONDITION_CLAUSE_LIMIT) {
    ruleforge_eval_fail(eval, "RF0350", span, "split the condition into separate variants", "this condition expands to more than %lu alternatives",
                        (unsigned long)R_CONDITION_CLAUSE_LIMIT);

    return FALSE;
  }

  out->constant = FALSE;
  out->value    = FALSE;
  out->count    = left->count + right->count;
  out->clauses =
      (RuleforgeClause *)nexus_data_arena_allocate(&eval->compilation->arena, ((uint_large)out->count + 1U) * NEXUS_SIZEOF(RuleforgeClause));

  for (i = 0; i < left->count; i++) {
    out->clauses[i] = left->clauses[i];
  }

  for (i = 0; i < right->count; i++) {
    out->clauses[left->count + i] = right->clauses[i];
  }

  return TRUE;
}

/* ---------------------------------------------------------------------------- */
/* VALUES TO CONDITIONS                                                         */
/* ---------------------------------------------------------------------------- */

/*
Summary: the name of the game condition an expression is, or NULL.
Description: a game name is a bare reference to a manifest condition such as `rarity`, not `rarity.x`. Locals and declarations shadow it.
*/
static const char *r_condition_game_name_get(REval *eval, RfExpr view) {
  RfAtom             atom;
  RfIndexedReference indexed;
  RfReference        reference;
  const char        *name;
  RuleforgeValue     value;
  RuleforgeModule   *owner;

  if (rf_expr_operand_count(view) != 0) {
    return NULL;
  }

  atom = rf_expr_atom(view);

  if (rf_atom_count_rule_indexed_reference(atom) == 0) {
    return NULL;
  }

  indexed = rf_atom_rule_indexed_reference(atom, 0);

  if (rf_indexed_reference_count_rule_expr(indexed) != 0) {
    return NULL;
  }

  reference = rf_indexed_reference_rule_reference(indexed, 0);

  if (rf_reference_count_token_identifier(reference) != 1 || rf_reference_count_token_parameter_reference(reference) != 0 ||
      rf_reference_count_token_compiler_reference(reference) != 0) {
    return NULL;
  }

  name = ruleforge_eval_text_get(eval, reference.tree, rf_reference_token_identifier(reference, 0));

  if (ruleforge_manifest_condition_find(name) == NULL) {
    return NULL;
  }

  if (ruleforge_eval_variable_find(eval, name, FALSE, &value) || ruleforge_eval_symbol_find(eval, eval->module, name, &owner) != NULL) {
    return NULL;
  }

  return name;
}

/* Summary: builds the condition `info operation value`; reports a value of the wrong type or an operator the condition lacks. */
static RuleforgeCondition *r_condition_make(REval *eval, RuleforgeSpan span, const RuleforgeConditionInfo *info, RuleforgeOperator operation,
                                            const RuleforgeValue *value) {
  RuleforgeCondition *condition;
  uint32              count;
  uint32              i;
  RuleforgeValue      single;

  if (operation != ROP_EQ && operation != ROP_NE && !info->ordered) {
    ruleforge_eval_fail(eval, "RF0351", span, "this condition can only be compared with == or !=", "`%s` cannot be compared with `%s`", info->name,
                        ruleforge_manifest_operator_text_get(operation));

    return NULL;
  }

  if (value->kind == RVK_NONE) {
    ruleforge_eval_fail(eval, "RF0352", span, "an optional value must be checked with `!= none` before it is used in a condition",
                        "the value compared with `%s` is none", info->name);

    return NULL;
  }

  count = value->kind == RVK_LIST ? value->as.list->count : 1;

  if (count == 0) {
    ruleforge_eval_fail(eval, "RF0353", span, NULL, "`%s` is compared with an empty list", info->name);

    return NULL;
  }

  if (count > 1 && (info->type == RCT_INTEGER || info->type == RCT_BOOLEAN)) {
    ruleforge_eval_fail(eval, "RF0354", span, NULL, "`%s` takes one value, got a list", info->name);

    return NULL;
  }

  condition = ruleforge_eval_condition_create(eval, info, operation, count);

  for (i = 0; i < count; i++) {
    single = value->kind == RVK_LIST ? value->as.list->items[i] : *value;

    if (info->type == RCT_INTEGER) {
      if (single.kind != RVK_INT) {
        ruleforge_eval_fail(eval, "RF0355", span, single.kind == RVK_FLOAT ? "use an int; round with math.Round" : NULL,
                            "`%s` is compared with a %s, but needs an int", info->name, ruleforge_value_type_name_get(single.kind));

        return NULL;
      }

      condition->numbers[i] = single.as.integer;
    } else if (info->type == RCT_BOOLEAN) {
      if (single.kind != RVK_BOOL) {
        ruleforge_eval_fail(eval, "RF0355", span, NULL, "`%s` is compared with a %s, but needs a bool", info->name,
                            ruleforge_value_type_name_get(single.kind));

        return NULL;
      }

      condition->numbers[i] = single.as.flag ? 1 : 0;
    } else if (info->type == RCT_RARITY) {
      int32 rarity;

      rarity = single.kind == RVK_STRING ? ruleforge_manifest_rarity_parse(single.as.string) : -1;

      if (rarity < 0) {
        ruleforge_eval_fail(eval, "RF0356", span, "rarities: Normal, Magic, Rare, Unique", "`%s` needs a rarity name", info->name);

        return NULL;
      }

      condition->numbers[i] = rarity;
    } else {
      if (single.kind != RVK_STRING) {
        ruleforge_eval_fail(eval, "RF0355", span, NULL, "`%s` is compared with a %s, but needs a string", info->name,
                            ruleforge_value_type_name_get(single.kind));

        return NULL;
      }

      condition->strings[i] = single.as.string;
    }
  }

  /* A boolean condition is always written == value: `!= true` is `== false`. */
  if (info->type == RCT_BOOLEAN && operation == ROP_NE) {
    condition->op         = ROP_EQ;
    condition->numbers[0] = condition->numbers[0] == 0 ? 1 : 0;
  }

  return condition;
}

boolean ruleforge_condition_scope_build(REval *eval, RfScopeAssignment assignment, RuleforgeCondition **out_condition) {
  const RuleforgeConditionInfo *info;
  RuleforgeValue                value;
  const char                   *key;
  RuleforgeSpan                 span;

  *out_condition = NULL;
  key            = ruleforge_syntax_name_get(eval->compilation, rf_scope_assignment_rule_qualified_name(assignment, 0));
  span           = ruleforge_eval_span_get(eval, assignment.tree, assignment.node);
  info           = ruleforge_manifest_condition_find(key);

  if (info == NULL) {
    const char *closest;
    char        note[200];
    uint32      i;
    const char *names[128];
    uint32      count;

    count = ruleforge_manifest_condition_count_get();

    for (i = 0; i < count && i < 128; i++) {
      names[i] = ruleforge_manifest_condition_at(i)->name;
    }

    closest = nexus_strings_string_closest(key, names, count);

    if (closest != NULL) {
      (void)stbsp_snprintf(note, (int)NEXUS_SIZEOF(note), "did you mean `%s`?", closest);
    }

    ruleforge_eval_fail(eval, "RF0357", span,
                        closest == NULL ? "see docs/manifest.md for the conditions"
                                        : nexus_data_arena_text_copy(&eval->compilation->arena, note, nexus_strings_string_length_get(note)),
                        "`%s` is not a game condition", key);

    return FALSE;
  }

  value = ruleforge_eval_expression(eval, rf_scope_assignment_rule_expr(assignment, 0));

  if (eval->failed) {
    return FALSE;
  }

  *out_condition = r_condition_make(eval, span, info, ROP_EQ, &value);

  return *out_condition != NULL;
}

RuleforgeCondition *ruleforge_condition_value_make(REval *eval, RuleforgeSpan span, const RuleforgeConditionInfo *info, RuleforgeOperator operation,
                                                   const RuleforgeValue *value) {
  return r_condition_make(eval, span, info, operation, value);
}

/* ---------------------------------------------------------------------------- */
/* EXPRESSIONS                                                                  */
/* ---------------------------------------------------------------------------- */

/* Summary: the comparison operator a lexer token stands for; FALSE when the token is not a comparison. */
static boolean r_condition_operator_get(LLexerTokenKind kind, RuleforgeOperator *out) {
  if (kind == RF_TOKEN_EQUALS_EQUALS) {
    *out = ROP_EQ;
  } else if (kind == RF_TOKEN_NOT_EQUALS) {
    *out = ROP_NE;
  } else if (kind == RF_TOKEN_LESS) {
    *out = ROP_LT;
  } else if (kind == RF_TOKEN_LESS_EQUAL) {
    *out = ROP_LE;
  } else if (kind == RF_TOKEN_GREATER) {
    *out = ROP_GT;
  } else if (kind == RF_TOKEN_GREATER_EQUAL) {
    *out = ROP_GE;
  } else {
    return FALSE;
  }

  return TRUE;
}

boolean ruleforge_condition_evaluate(REval *eval, RfExpr view, boolean negate, RuleforgeDnf *out) {
  RuleforgeDnf      left;
  RuleforgeDnf      right;
  uint32            operands;
  LLexerTokenKind   kind;
  RuleforgeSpan     span;
  const char       *game_name;
  RuleforgeOperator operator_value;

  span     = ruleforge_eval_span_get(eval, view.tree, view.node);
  operands = rf_expr_operand_count(view);
  kind     = operands == 0 ? (LLexerTokenKind)L_LEXER_TOKEN_KIND_ERROR : rf_expr_operator_kind(view);

  if (operands == 1 && kind == RF_TOKEN_KW_NOT) {
    return ruleforge_condition_evaluate(eval, rf_expr_operand(view, 0), !negate, out);
  }

  if (operands == 2 && (kind == RF_TOKEN_KW_AND || kind == RF_TOKEN_KW_OR)) {
    boolean is_and;

    /* De Morgan: under a negation `and` becomes `or` and the other way round. */
    is_and = kind == RF_TOKEN_KW_AND;

    if (negate) {
      is_and = !is_and;
    }

    if (!ruleforge_condition_evaluate(eval, rf_expr_operand(view, 0), negate, &left)) {
      return FALSE;
    }

    /* A constant left side decides `and`/`or` without evaluating the right, so `x != none and x >= 80` stays safe. */
    if (left.constant && ((is_and && !left.value) || (!is_and && left.value))) {
      *out = left;

      return TRUE;
    }

    if (!ruleforge_condition_evaluate(eval, rf_expr_operand(view, 1), negate, &right)) {
      return FALSE;
    }

    return is_and ? r_condition_and(eval, &left, &right, span, out) : r_condition_or(eval, &left, &right, span, out);
  }

  if (operands == 2 && r_condition_operator_get(kind, &operator_value)) {
    const char *left_name;
    const char *right_name;

    left_name  = r_condition_game_name_get(eval, rf_expr_operand(view, 0));
    right_name = r_condition_game_name_get(eval, rf_expr_operand(view, 1));

    if (left_name != NULL && right_name != NULL) {
      ruleforge_eval_fail(eval, "RF0358", span, NULL, "a condition cannot compare two game values");

      return FALSE;
    }

    if (left_name != NULL || right_name != NULL) {
      RuleforgeValue      value;
      RuleforgeCondition *condition;

      if (right_name != NULL) {
        operator_value = r_condition_operator_flip(operator_value);
      }

      if (negate) {
        operator_value = r_condition_operator_negate(operator_value);
      }

      value = ruleforge_eval_expression(eval, rf_expr_operand(view, left_name != NULL ? 1 : 0));

      if (eval->failed) {
        return FALSE;
      }

      condition = r_condition_make(eval, span, ruleforge_manifest_condition_find(left_name != NULL ? left_name : right_name), operator_value, &value);

      if (condition == NULL) {
        return FALSE;
      }

      r_condition_single(eval, out, condition);

      return TRUE;
    }
  }

  game_name = r_condition_game_name_get(eval, view);

  if (game_name != NULL) {
    const RuleforgeConditionInfo *info;
    RuleforgeValue                truth;
    RuleforgeCondition           *condition;

    info = ruleforge_manifest_condition_find(game_name);

    if (info->type != RCT_BOOLEAN) {
      ruleforge_eval_fail(eval, "RF0359", span, "compare it with a value, e.g. `rarity == \"Rare\"`", "`%s` is not a true/false condition",
                          info->name);

      return FALSE;
    }

    truth     = ruleforge_value_flag(!negate);
    condition = r_condition_make(eval, span, info, ROP_EQ, &truth);

    if (condition == NULL) {
      return FALSE;
    }

    r_condition_single(eval, out, condition);

    return TRUE;
  }

  /* Anything else is a compile-time boolean. */
  {
    boolean flag;

    if (!ruleforge_eval_boolean_get(eval, view, &flag)) {
      return FALSE;
    }

    r_condition_constant(out, negate ? !flag : flag);
  }

  return TRUE;
}
