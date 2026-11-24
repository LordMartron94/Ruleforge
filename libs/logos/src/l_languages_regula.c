#include <nexus.h>

#include "../logos.h"
#include "./l_internal.h"

/* Written by an LLM. */
static LRegulaOperation *l_languages_regula_operation_intern(LRegula *regula, const LRegulaOperation *operation) {
  LRegulaOperation *stored;
  uint_large        index;

  NEXUS_PROGRAM_PRECONDITION(regula != NULL && operation != NULL, "regula and operation must not be NULL");

  index                          = nexus_data_slabbed_list_length_get(regula->operations);
  stored                         = (LRegulaOperation *)nexus_data_slabbed_list_append(regula->operations, operation);
  stored->owner                  = regula;
  stored->storage_index          = index;
  stored->assembly_parent        = NULL;
  stored->assembly_active        = FALSE;
  stored->implicit_concatenation = FALSE;

  return stored;
}

LRegulaOperation *logos_languages_regula_regula_operation_add(LRegula *regula, LRegulaOperation *operation) {
  LRegulaOperation  concatenate_operation;
  LRegulaOperation *previous_root;

  NEXUS_PROGRAM_PRECONDITION(regula != NULL && operation != NULL, "regula and operation must not be NULL");
  NEXUS_PROGRAM_PRECONDITION(operation->owner == regula && !operation->assembly_active, "operation must be an inactive node owned by regula");

  if (regula->internal_constructing) {
    return operation;
  }

  operation->assembly_active = TRUE;

  if (regula->root_rule == NULL) {
    regula->root_rule = operation;
  } else {
    previous_root                                                = regula->root_rule;
    concatenate_operation.label                                  = NULL;
    concatenate_operation.kind                                   = LROK_CONCATENATE;
    concatenate_operation.operation_data.concatenate.operation_a = previous_root;
    concatenate_operation.operation_data.concatenate.operation_b = operation;

    regula->root_rule                         = l_languages_regula_operation_intern(regula, &concatenate_operation);
    regula->root_rule->assembly_active        = TRUE;
    regula->root_rule->implicit_concatenation = TRUE;
    previous_root->assembly_parent            = regula->root_rule;
    operation->assembly_parent                = regula->root_rule;
  }

  return operation;
}

/* Written by an LLM. Replaces active assembly spans without traversing compound interiors. */
static LRegulaOperation *l_languages_regula_operation_compose(LRegula *regula, LRegulaOperation *replacement, LRegulaOperation *operation_a,
                                                              LRegulaOperation *operation_b) {
  LRegulaOperation *walk_a;
  LRegulaOperation *walk_b;
  LRegulaOperation *common;
  LRegulaOperation *left_operand;
  LRegulaOperation *right_operand;
  LRegulaOperation *parent;
  LRegulaOperation *grandparent;
  LRegulaOperation *sibling;
  uint_large        depth_a;
  uint_large        depth_b;

  NEXUS_PROGRAM_PRECONDITION(regula != NULL && replacement != NULL && operation_a != NULL, "composition arguments must not be NULL");
  NEXUS_PROGRAM_PRECONDITION(replacement->owner == regula && operation_a->owner == regula && (operation_b == NULL || operation_b->owner == regula),
                             "operands must belong to regula");

  if (regula->internal_constructing) {
    return replacement;
  }

  NEXUS_PROGRAM_PRECONDITION(operation_a->assembly_active && (operation_b == NULL || operation_b->assembly_active),
                             "operands must be active expressions, not consumed children");

  depth_a = 0;

  for (walk_a = operation_a; walk_a->assembly_parent != NULL; walk_a = walk_a->assembly_parent) {
    depth_a++;
  }

  NEXUS_PROGRAM_PRECONDITION(walk_a == regula->root_rule, "operand must be in the current expression");
  left_operand  = operation_a;
  right_operand = NULL;

  if (operation_b != NULL && operation_b != operation_a) {
    depth_b = 0;

    for (walk_b = operation_b; walk_b->assembly_parent != NULL; walk_b = walk_b->assembly_parent) {
      depth_b++;
    }

    NEXUS_PROGRAM_PRECONDITION(walk_b == regula->root_rule, "operand must be in the current expression");
    walk_a = operation_a;
    walk_b = operation_b;

    while (depth_a > depth_b) {
      walk_a = walk_a->assembly_parent;
      depth_a--;
    }

    while (depth_b > depth_a) {
      walk_b = walk_b->assembly_parent;
      depth_b--;
    }

    while (walk_a != walk_b) {
      walk_a = walk_a->assembly_parent;
      walk_b = walk_b->assembly_parent;
    }

    common = walk_a;
    NEXUS_PROGRAM_PRECONDITION(common != operation_a && common != operation_b, "operands must not overlap");
    walk_a = operation_a;

    while (walk_a->assembly_parent != common) {
      walk_a = walk_a->assembly_parent;
    }

    if (common->operation_data.concatenate.operation_a == walk_a) {
      left_operand  = operation_a;
      right_operand = operation_b;
    } else {
      left_operand  = operation_b;
      right_operand = operation_a;
    }

    for (walk_a = left_operand; walk_a->assembly_parent != common; walk_a = walk_a->assembly_parent) {
      parent = walk_a->assembly_parent;
      NEXUS_PROGRAM_PRECONDITION(parent->implicit_concatenation && parent->operation_data.concatenate.operation_b == walk_a,
                                 "binary operands must be adjacent; intervening expressions cannot be discarded");
    }

    for (walk_b = right_operand; walk_b->assembly_parent != common; walk_b = walk_b->assembly_parent) {
      parent = walk_b->assembly_parent;
      NEXUS_PROGRAM_PRECONDITION(parent->implicit_concatenation && parent->operation_data.concatenate.operation_a == walk_b,
                                 "binary operands must be adjacent; intervening expressions cannot be discarded");
    }
  }

  parent                       = left_operand->assembly_parent;
  replacement->assembly_parent = parent;
  replacement->assembly_active = TRUE;

  if (parent == NULL) {
    regula->root_rule = replacement;
  } else if (parent->operation_data.concatenate.operation_a == left_operand) {
    parent->operation_data.concatenate.operation_a = replacement;
  } else {
    parent->operation_data.concatenate.operation_b = replacement;
  }

  left_operand->assembly_active = FALSE;
  left_operand->assembly_parent = NULL;

  if (right_operand != NULL) {
    parent                   = right_operand->assembly_parent;
    grandparent              = parent->assembly_parent;
    sibling                  = parent->operation_data.concatenate.operation_a == right_operand ? parent->operation_data.concatenate.operation_b
                                                                                               : parent->operation_data.concatenate.operation_a;
    sibling->assembly_parent = grandparent;

    if (grandparent == NULL) {
      regula->root_rule = sibling;
    } else if (grandparent->operation_data.concatenate.operation_a == parent) {
      grandparent->operation_data.concatenate.operation_a = sibling;
    } else {
      grandparent->operation_data.concatenate.operation_b = sibling;
    }

    parent->assembly_active        = FALSE;
    parent->assembly_parent        = NULL;
    right_operand->assembly_active = FALSE;
    right_operand->assembly_parent = NULL;
  }

  return replacement;
}

LRegulaOperation *logos_languages_regula_regula_operation_add_empty_language(const char *label, LRegula *regula) {
  LRegulaOperation operation;

  operation.label = label;
  operation.kind  = LROK_EMPTY_LANGUAGE;

  return logos_languages_regula_regula_operation_add(regula, l_languages_regula_operation_intern(regula, &operation));
}

LRegulaOperation *logos_languages_regula_regula_operation_add_empty_string(const char *label, LRegula *regula) {
  LRegulaOperation operation;

  operation.label = label;
  operation.kind  = LROK_EMPTY_STRING;

  return logos_languages_regula_regula_operation_add(regula, l_languages_regula_operation_intern(regula, &operation));
}

LRegulaOperation *logos_languages_regula_regula_operation_add_symbol(const char *label, LRegula *regula, byte symbol) {
  LRegulaOperation operation;

  operation.label = label;
  operation.kind  = LROK_SYMBOL;

  logos_languages_regula_observation_mask_clear(operation.operation_data.symbol_mask);
  logos_languages_regula_observation_mask_set(operation.operation_data.symbol_mask, symbol, TRUE);

  return logos_languages_regula_regula_operation_add(regula, l_languages_regula_operation_intern(regula, &operation));
}

LRegulaOperation *logos_languages_regula_regula_operation_add_symbol_range(const char *label, LRegula *regula, byte range_start, uint16 range_end) {
  LRegulaOperation operation;

  operation.label = label;
  operation.kind  = LROK_SYMBOL;

  logos_languages_regula_observation_mask_clear(operation.operation_data.symbol_mask);
  logos_languages_regula_observation_mask_set_range(operation.operation_data.symbol_mask, range_start, range_end, TRUE);

  return logos_languages_regula_regula_operation_add(regula, l_languages_regula_operation_intern(regula, &operation));
}

LRegulaOperation *logos_languages_regula_regula_operation_add_literal(const char *label, LRegula *regula, byte *literal, uint32 literal_size) {
  uint32            i;
  LRegulaOperation  symbol_operation;
  LRegulaOperation  concatenate_operation;
  LRegulaOperation *literal_operation;

  NEXUS_PROGRAM_PRECONDITION(regula != NULL && (literal != NULL || literal_size == 0), "regula and nonempty literal must not be NULL");

  if (literal_size == 0) {
    return logos_languages_regula_regula_operation_add_empty_string(label, regula);
  }

  symbol_operation.label = NULL;
  symbol_operation.kind  = LROK_SYMBOL;
  logos_languages_regula_observation_mask_clear(symbol_operation.operation_data.symbol_mask);
  logos_languages_regula_observation_mask_set(symbol_operation.operation_data.symbol_mask, literal[0], TRUE);
  literal_operation = l_languages_regula_operation_intern(regula, &symbol_operation);

  concatenate_operation.label = NULL;
  concatenate_operation.kind  = LROK_CONCATENATE;

  for (i = 1; i < literal_size; i++) {
    logos_languages_regula_observation_mask_clear(symbol_operation.operation_data.symbol_mask);
    logos_languages_regula_observation_mask_set(symbol_operation.operation_data.symbol_mask, literal[i], TRUE);

    concatenate_operation.operation_data.concatenate.operation_a = literal_operation;
    concatenate_operation.operation_data.concatenate.operation_b = l_languages_regula_operation_intern(regula, &symbol_operation);
    literal_operation                                            = l_languages_regula_operation_intern(regula, &concatenate_operation);
  }

  literal_operation->label = label;

  return logos_languages_regula_regula_operation_add(regula, literal_operation);
}

LRegulaOperation *logos_languages_regula_regula_operation_add_unionize(const char *label, LRegula *regula, LRegulaOperation *operation_a,
                                                                       LRegulaOperation *operation_b) {
  LRegulaOperation unionize_operation;

  unionize_operation.label                               = label;
  unionize_operation.kind                                = LROK_UNIONIZE;
  unionize_operation.operation_data.unionize.operation_a = operation_a;
  unionize_operation.operation_data.unionize.operation_b = operation_b;

  return l_languages_regula_operation_compose(regula, l_languages_regula_operation_intern(regula, &unionize_operation), operation_a, operation_b);
}

LRegulaOperation *logos_languages_regula_regula_operation_add_concatenate(const char *label, LRegula *regula, LRegulaOperation *operation_a,
                                                                          LRegulaOperation *operation_b) {
  LRegulaOperation concatenate_operation;

  concatenate_operation.label                                  = label;
  concatenate_operation.kind                                   = LROK_CONCATENATE;
  concatenate_operation.operation_data.concatenate.operation_a = operation_a;
  concatenate_operation.operation_data.concatenate.operation_b = operation_b;

  return l_languages_regula_operation_compose(regula, l_languages_regula_operation_intern(regula, &concatenate_operation), operation_a, operation_b);
}

LRegulaOperation *logos_languages_regula_regula_operation_add_star(const char *label, LRegula *regula, LRegulaOperation *operation) {
  LRegulaOperation star_operation;

  star_operation.label                         = label;
  star_operation.kind                          = LROK_STAR;
  star_operation.operation_data.operation_star = operation;

  return l_languages_regula_operation_compose(regula, l_languages_regula_operation_intern(regula, &star_operation), operation, NULL);
}

LRegulaOperation *logos_languages_regula_regula_operation_add_plus(const char *label, LRegula *regula, LRegulaOperation *operation) {
  LRegulaOperation star_operation;
  LRegulaOperation plus_operation;

  star_operation.label                         = NULL;
  star_operation.kind                          = LROK_STAR;
  star_operation.operation_data.operation_star = operation;

  plus_operation.label                                  = label;
  plus_operation.kind                                   = LROK_CONCATENATE;
  plus_operation.operation_data.concatenate.operation_a = operation;
  plus_operation.operation_data.concatenate.operation_b = l_languages_regula_operation_intern(regula, &star_operation);

  return l_languages_regula_operation_compose(regula, l_languages_regula_operation_intern(regula, &plus_operation), operation, NULL);
}

LRegulaOperation *logos_languages_regula_regula_operation_add_optional(const char *label, LRegula *regula, LRegulaOperation *operation) {
  LRegulaOperation optional_operation;
  LRegulaOperation empty_string_operation;

  empty_string_operation.label = NULL;
  empty_string_operation.kind  = LROK_EMPTY_STRING;

  optional_operation.label                               = label;
  optional_operation.kind                                = LROK_UNIONIZE;
  optional_operation.operation_data.unionize.operation_a = l_languages_regula_operation_intern(regula, &empty_string_operation);
  optional_operation.operation_data.unionize.operation_b = operation;

  return l_languages_regula_operation_compose(regula, l_languages_regula_operation_intern(regula, &optional_operation), operation, NULL);
}

LRegulaOperation *logos_languages_regula_regula_operation_add_repeat(const char *label, LRegula *regula, LRegulaOperation *operation, uint16 min,
                                                                     uint16 max, boolean has_min, boolean has_max) {
  uint32            i;
  uint32            minimum_count;
  LRegulaOperation  repeat_operation;
  LRegulaOperation  concatenate_operation;
  LRegulaOperation  suffix_operation;
  LRegulaOperation  empty_string_operation;
  LRegulaOperation *repeated_operation;
  LRegulaOperation *suffix;

  NEXUS_PROGRAM_PRECONDITION(regula != NULL, "regula must not be NULL");
  NEXUS_PROGRAM_PRECONDITION(operation != NULL, "operation must not be NULL");
  NEXUS_PROGRAM_PRECONDITION(!has_min || !has_max || min <= max, "repeat minimum must not exceed its maximum");

  minimum_count      = has_min ? min : 0;
  repeated_operation = minimum_count > 0 ? operation : NULL;

  concatenate_operation.label = NULL;
  concatenate_operation.kind  = LROK_CONCATENATE;

  for (i = 1; i < minimum_count; i++) {
    concatenate_operation.operation_data.concatenate.operation_a = repeated_operation;
    concatenate_operation.operation_data.concatenate.operation_b = operation;
    repeated_operation                                           = l_languages_regula_operation_intern(regula, &concatenate_operation);
  }

  if (has_max && max > minimum_count) {
    empty_string_operation.label = NULL;
    empty_string_operation.kind  = LROK_EMPTY_STRING;

    /* Every optional occurrence can share the same immutable subtree. */
    suffix_operation.label                               = NULL;
    suffix_operation.kind                                = LROK_UNIONIZE;
    suffix_operation.operation_data.unionize.operation_a = l_languages_regula_operation_intern(regula, &empty_string_operation);
    suffix_operation.operation_data.unionize.operation_b = operation;
    suffix                                               = l_languages_regula_operation_intern(regula, &suffix_operation);

    for (i = minimum_count; i < max; i++) {
      if (repeated_operation == NULL) {
        repeated_operation = suffix;
      } else {
        concatenate_operation.operation_data.concatenate.operation_a = repeated_operation;
        concatenate_operation.operation_data.concatenate.operation_b = suffix;
        repeated_operation                                           = l_languages_regula_operation_intern(regula, &concatenate_operation);
      }
    }
  } else if (!has_max) {
    suffix_operation.label                         = NULL;
    suffix_operation.kind                          = LROK_STAR;
    suffix_operation.operation_data.operation_star = operation;
    suffix                                         = l_languages_regula_operation_intern(regula, &suffix_operation);

    if (repeated_operation == NULL) {
      repeated_operation = suffix;
    } else {
      concatenate_operation.operation_data.concatenate.operation_a = repeated_operation;
      concatenate_operation.operation_data.concatenate.operation_b = suffix;
      repeated_operation                                           = l_languages_regula_operation_intern(regula, &concatenate_operation);
    }
  }

  if (repeated_operation == NULL) {
    empty_string_operation.label = NULL;
    empty_string_operation.kind  = LROK_EMPTY_STRING;
    repeated_operation           = l_languages_regula_operation_intern(regula, &empty_string_operation);
  }

  if (repeated_operation == operation) {
    /* An exact single occurrence must not relabel the borrowed operand. */
    repeat_operation       = *operation;
    repeat_operation.label = label;
    repeated_operation     = l_languages_regula_operation_intern(regula, &repeat_operation);
  }

  repeated_operation->label = label;

  return l_languages_regula_operation_compose(regula, repeated_operation, operation, NULL);
}

/* Written by an LLM. */
LRegulaOperation *logos_languages_regula_regula_operation_add_symbol_mask(const char *label, LRegula *regula, const LRegulaObservationMask mask,
                                                                          boolean negated) {
  LRegulaOperation operation;

  NEXUS_PROGRAM_PRECONDITION(regula != NULL && mask != NULL, "regula and mask must not be NULL");

  operation.label = label;
  operation.kind  = LROK_SYMBOL;
  nexus_memory_bytes_copy(mask, operation.operation_data.symbol_mask, NEXUS_SIZEOF(LRegulaObservationMask));

  if (negated) {
    logos_languages_regula_observation_mask_invert(operation.operation_data.symbol_mask);
  }

  return logos_languages_regula_regula_operation_add(regula, l_languages_regula_operation_intern(regula, &operation));
}

/* ---------------------------------------------------------------------------- */
/* DUPLICATION                                                                  */
/* ---------------------------------------------------------------------------- */

typedef struct LRegulaDuplicateWork {
  const LRegulaOperation *source;
  LRegulaOperation      **destination; /* Slot that receives the stored copy of source. */
} LRegulaDuplicateWork;

/* Written by an LLM. */
LRegulaOperation *logos_languages_regula_regula_operation_duplicate(LRegula *regula, const LRegulaOperation *operation) {
  NexusDataQueue      *work_list;
  LRegulaDuplicateWork current;
  LRegulaDuplicateWork child;
  LRegulaOperation    *root;
  LRegulaOperation    *stored;

  NEXUS_PROGRAM_PRECONDITION(regula != NULL && operation != NULL, "regula and operation must not be NULL");

  root                = NULL;
  current.source      = operation;
  current.destination = &root;
  work_list           = nexus_data_queue_create(NEXUS_SIZEOF(LRegulaDuplicateWork), 16);
  nexus_data_queue_push(work_list, &current);

  /* Storage is address-stable, so a destination slot inside an already stored copy stays valid while later copies are appended. */
  while (nexus_data_queue_pop(work_list, &current)) {
    NEXUS_PROGRAM_PRECONDITION(current.source != NULL, "operation graph must not contain a missing operand");

    stored               = l_languages_regula_operation_intern(regula, current.source);
    *current.destination = stored;

    switch (stored->kind) {
    case LROK_EMPTY_LANGUAGE:
    case LROK_EMPTY_STRING:
    case LROK_SYMBOL:
      break;
    case LROK_UNIONIZE:
      child.source      = current.source->operation_data.unionize.operation_a;
      child.destination = &stored->operation_data.unionize.operation_a;
      nexus_data_queue_push(work_list, &child);

      child.source      = current.source->operation_data.unionize.operation_b;
      child.destination = &stored->operation_data.unionize.operation_b;
      nexus_data_queue_push(work_list, &child);
      break;
    case LROK_CONCATENATE:
      child.source      = current.source->operation_data.concatenate.operation_a;
      child.destination = &stored->operation_data.concatenate.operation_a;
      nexus_data_queue_push(work_list, &child);

      child.source      = current.source->operation_data.concatenate.operation_b;
      child.destination = &stored->operation_data.concatenate.operation_b;
      nexus_data_queue_push(work_list, &child);
      break;
    case LROK_STAR:
      child.source      = current.source->operation_data.operation_star;
      child.destination = &stored->operation_data.operation_star;
      nexus_data_queue_push(work_list, &child);
      break;
    case LROK_COUNT:
    default:
      NEXUS_PROGRAM_PANIC("invalid operation kind");
    }
  }

  nexus_data_queue_destroy(work_list);

  return logos_languages_regula_regula_operation_add(regula, root);
}

/* Written by an LLM. */
LRegulaOperation *logos_languages_regula_regula_operation_duplicate_shallow(LRegula *regula, const LRegulaOperation *operation) {
  LRegulaOperation copy;

  NEXUS_PROGRAM_PRECONDITION(regula != NULL && operation != NULL, "regula and operation must not be NULL");

  copy = *operation;

  switch (copy.kind) {
  case LROK_EMPTY_LANGUAGE:
  case LROK_EMPTY_STRING:
  case LROK_SYMBOL:
    break;
  case LROK_UNIONIZE:
    copy.operation_data.unionize.operation_a = NULL;
    copy.operation_data.unionize.operation_b = NULL;
    break;
  case LROK_CONCATENATE:
    copy.operation_data.concatenate.operation_a = NULL;
    copy.operation_data.concatenate.operation_b = NULL;
    break;
  case LROK_STAR:
    copy.operation_data.operation_star = NULL;
    break;
  case LROK_COUNT:
  default:
    NEXUS_PROGRAM_PANIC("invalid operation kind");
  }

  return logos_languages_regula_regula_operation_add(regula, l_languages_regula_operation_intern(regula, &copy));
}

/* ---------------------------------------------------------------------------- */
/* REGEXP STRINGS                                                               */
/* ---------------------------------------------------------------------------- */

typedef struct LRegulaRegexpParser {
  LRegula    *regula;
  const char *text;
  uint_large  size;
  uint_large  position;
  uint32      depth;
  NexusError *error;
} LRegulaRegexpParser;

/* Written by an LLM. */
static void l_languages_regula_regexp_fail(LRegulaRegexpParser *parser, NexusErrorCode code, const char *message) {
  if (parser->error == NEXUS_PROGRAM_ERROR_SUCCESS()) {
    parser->error = NEXUS_PROGRAM_ERROR_FAILURE(code, "Regexp byte %llu: %s", (unsigned long long)parser->position, message);
  }
}

/* Written by an LLM. Reads one literal byte or shorthand class. */
static boolean l_languages_regula_regexp_unit(LRegulaRegexpParser *parser, boolean in_class, LRegulaObservationMask mask, byte *out_byte,
                                              boolean *out_scalar) {
  byte   observation;
  byte   escape;
  uint32 i;
  uint32 digit;
  uint32 value;

  if (parser->position == parser->size) {
    l_languages_regula_regexp_fail(parser, GLOBAL_LOGOS_REGULA_REGEXP_INVALID, "missing character or escape");
    return FALSE;
  }

  logos_languages_regula_observation_mask_clear(mask);
  observation = (byte)parser->text[parser->position++];
  *out_scalar = TRUE;

  if (observation == '\\') {
    if (parser->position == parser->size) {
      l_languages_regula_regexp_fail(parser, GLOBAL_LOGOS_REGULA_REGEXP_INVALID, "trailing backslash");
      return FALSE;
    }

    escape      = (byte)parser->text[parser->position++];
    observation = escape;

    if (escape == 'x') {
      if (parser->position < parser->size && parser->text[parser->position] == '{') {
        l_languages_regula_regexp_fail(parser, GLOBAL_LOGOS_REGULA_REGEXP_UNSUPPORTED, "code-point escapes are unsupported; use \\xHH");
        return FALSE;
      }

      value = 0;

      for (i = 0; i < 2; i++) {
        if (parser->position == parser->size) {
          l_languages_regula_regexp_fail(parser, GLOBAL_LOGOS_REGULA_REGEXP_INVALID, "hex escapes require two digits");
          return FALSE;
        }

        observation = (byte)parser->text[parser->position++];

        if (observation >= '0' && observation <= '9') {
          digit = observation - '0';
        } else if (observation >= 'a' && observation <= 'f') {
          digit = observation - 'a' + 10;
        } else if (observation >= 'A' && observation <= 'F') {
          digit = observation - 'A' + 10;
        } else {
          l_languages_regula_regexp_fail(parser, GLOBAL_LOGOS_REGULA_REGEXP_INVALID, "invalid hexadecimal digit");
          return FALSE;
        }

        value = (value * 16) + digit;
      }

      observation = (byte)value;
    } else if (escape == 'd' || escape == 'D' || escape == 'w' || escape == 'W' || escape == 's' || escape == 'S') {
      *out_scalar = FALSE;

      if (escape == 'd' || escape == 'D' || escape == 'w' || escape == 'W') {
        logos_languages_regula_observation_mask_set_range(mask, '0', '9' + 1, TRUE);
      }

      if (escape == 'w' || escape == 'W') {
        logos_languages_regula_observation_mask_set_range(mask, 'a', 'z' + 1, TRUE);
        logos_languages_regula_observation_mask_set_range(mask, 'A', 'Z' + 1, TRUE);
        logos_languages_regula_observation_mask_set(mask, '_', TRUE);
      }

      if (escape == 's' || escape == 'S') {
        logos_languages_regula_observation_mask_set_range(mask, '\t', '\r' + 1, TRUE);
        logos_languages_regula_observation_mask_set(mask, ' ', TRUE);
      }

      if (escape == 'D' || escape == 'W' || escape == 'S') {
        logos_languages_regula_observation_mask_invert(mask);
      }
    } else if (escape == 'n') {
      observation = '\n';
    } else if (escape == 'r') {
      observation = '\r';
    } else if (escape == 't') {
      observation = '\t';
    } else if (escape == 'f') {
      observation = '\f';
    } else if (escape == 'v') {
      observation = '\v';
    } else if (escape == 'b' && in_class) {
      observation = 8;
    } else if ((escape >= 'a' && escape <= 'z') || (escape >= 'A' && escape <= 'Z') || (escape >= '0' && escape <= '9')) {
      l_languages_regula_regexp_fail(parser, GLOBAL_LOGOS_REGULA_REGEXP_UNSUPPORTED, "unsupported escape, assertion, or backreference");
      return FALSE;
    }
  }

  *out_byte = observation;

  if (*out_scalar) {
    logos_languages_regula_observation_mask_set(mask, observation, TRUE);
  }

  return TRUE;
}

/* Written by an LLM. */
static LRegulaOperation *l_languages_regula_regexp_class(LRegulaRegexpParser *parser) {
  LRegulaObservationMask mask;
  LRegulaObservationMask unit;
  LRegulaObservationMask range_end_mask;
  boolean                negated;
  boolean                scalar;
  boolean                end_scalar;
  byte                   observation;
  byte                   range_end;
  uint32                 i;

  parser->position++;
  negated = parser->position < parser->size && parser->text[parser->position] == '^';

  if (negated) {
    parser->position++;
  }

  logos_languages_regula_observation_mask_clear(mask);

  while (parser->position < parser->size && parser->text[parser->position] != ']') {
    if (parser->text[parser->position] == '[' ||
        (parser->position + 1 < parser->size && ((parser->text[parser->position] == '&' && parser->text[parser->position + 1] == '&') ||
                                                 (parser->text[parser->position] == '-' && parser->text[parser->position + 1] == '-')))) {
      l_languages_regula_regexp_fail(parser, GLOBAL_LOGOS_REGULA_REGEXP_UNSUPPORTED, "nested, POSIX, and set-operation classes are unsupported");
      return NULL;
    }

    if (!l_languages_regula_regexp_unit(parser, TRUE, unit, &observation, &scalar)) {
      return NULL;
    }

    if (parser->position + 1 < parser->size && parser->text[parser->position] == '-' && parser->text[parser->position + 1] != ']') {
      parser->position++;

      if (!l_languages_regula_regexp_unit(parser, TRUE, range_end_mask, &range_end, &end_scalar)) {
        return NULL;
      }

      if (!scalar || !end_scalar || observation > range_end) {
        l_languages_regula_regexp_fail(parser, GLOBAL_LOGOS_REGULA_REGEXP_INVALID, "class ranges require ordered scalar byte endpoints");
        return NULL;
      }

      logos_languages_regula_observation_mask_set_range(mask, observation, (uint16)range_end + 1, TRUE);
    } else {
      for (i = 0; i < 4; i++) {
        mask[i] |= unit[i];
      }
    }
  }

  if (parser->position == parser->size) {
    l_languages_regula_regexp_fail(parser, GLOBAL_LOGOS_REGULA_REGEXP_INVALID, "unterminated character class");
    return NULL;
  }

  parser->position++;

  return logos_languages_regula_regula_operation_add_symbol_mask(NULL, parser->regula, mask, negated);
}

/* Written by an LLM. */
static boolean l_languages_regula_regexp_count(LRegulaRegexpParser *parser, uint16 *out_count) {
  uint32 value;

  value = 0;

  if (parser->position == parser->size || parser->text[parser->position] < '0' || parser->text[parser->position] > '9') {
    l_languages_regula_regexp_fail(parser, GLOBAL_LOGOS_REGULA_REGEXP_INVALID, "expected a repetition count");
    return FALSE;
  }

  while (parser->position < parser->size && parser->text[parser->position] >= '0' && parser->text[parser->position] <= '9') {
    value = (value * 10) + (uint32)(parser->text[parser->position++] - '0');

    if (value > 65535u) {
      l_languages_regula_regexp_fail(parser, GLOBAL_LOGOS_REGULA_REGEXP_LIMIT, "repetition count exceeds 65535");
      return FALSE;
    }
  }

  *out_count = (uint16)value;

  return TRUE;
}

/* Written by an LLM. Mutually recursive grammar productions have bounded group depth. */
static LRegulaOperation *l_languages_regula_regexp_expression(LRegulaRegexpParser *parser);

/* Written by an LLM. */
static LRegulaOperation *l_languages_regula_regexp_atom(LRegulaRegexpParser *parser) {
  LRegulaObservationMask mask;
  LRegulaOperation      *operation;
  boolean                scalar;
  byte                   observation;

  observation = (byte)parser->text[parser->position];

  if (observation == '(') {
    parser->position++;

    if (parser->position < parser->size && parser->text[parser->position] == '?') {
      parser->position++;

      if (parser->position == parser->size || parser->text[parser->position] != ':') {
        l_languages_regula_regexp_fail(parser, GLOBAL_LOGOS_REGULA_REGEXP_UNSUPPORTED, "lookaround, flags, and special groups are unsupported");
        return NULL;
      }

      parser->position++;
    } else if (parser->position < parser->size && parser->text[parser->position] == '*') {
      l_languages_regula_regexp_fail(parser, GLOBAL_LOGOS_REGULA_REGEXP_UNSUPPORTED, "control verbs are unsupported");
      return NULL;
    }

    if (parser->depth == LOGOS_REGULA_REGEXP_MAX_DEPTH) {
      l_languages_regula_regexp_fail(parser, GLOBAL_LOGOS_REGULA_REGEXP_LIMIT, "group nesting limit exceeded");
      return NULL;
    }

    parser->depth++;
    operation = l_languages_regula_regexp_expression(parser);
    parser->depth--;

    if (operation == NULL) {
      return NULL;
    }

    if (parser->position == parser->size || parser->text[parser->position] != ')') {
      l_languages_regula_regexp_fail(parser, GLOBAL_LOGOS_REGULA_REGEXP_INVALID, "unterminated group");
      return NULL;
    }

    parser->position++;

    return operation;
  }

  if (observation == '[') {
    return l_languages_regula_regexp_class(parser);
  }

  if (observation == '.') {
    parser->position++;
    logos_languages_regula_observation_mask_clear(mask);

    return logos_languages_regula_regula_operation_add_symbol_mask(NULL, parser->regula, mask, TRUE);
  }

  if (observation == '^' || observation == '$') {
    l_languages_regula_regexp_fail(parser, GLOBAL_LOGOS_REGULA_REGEXP_UNSUPPORTED, "anchors are unsupported; Regula describes complete languages");
    return NULL;
  }

  if (observation == '*' || observation == '+' || observation == '?' || observation == '{' || observation == '}' || observation == ']') {
    l_languages_regula_regexp_fail(parser, GLOBAL_LOGOS_REGULA_REGEXP_INVALID, "unexpected metacharacter");
    return NULL;
  }

  if (!l_languages_regula_regexp_unit(parser, FALSE, mask, &observation, &scalar)) {
    return NULL;
  }

  return logos_languages_regula_regula_operation_add_symbol_mask(NULL, parser->regula, mask, FALSE);
}

/* Written by an LLM. */
static LRegulaOperation *l_languages_regula_regexp_sequence(LRegulaRegexpParser *parser) {
  LRegulaOperation *sequence;
  LRegulaOperation *atom;
  byte              quantifier;
  uint16            minimum;
  uint16            maximum;
  boolean           has_minimum;
  boolean           has_maximum;

  sequence = NULL;

  while (parser->position < parser->size && parser->text[parser->position] != '|' && parser->text[parser->position] != ')') {
    atom = l_languages_regula_regexp_atom(parser);

    if (atom == NULL) {
      return NULL;
    }

    if (parser->position < parser->size) {
      quantifier = (byte)parser->text[parser->position];

      if (quantifier == '*' || quantifier == '+' || quantifier == '?' || quantifier == '{') {
        parser->position++;
        minimum     = 0;
        maximum     = 0;
        has_minimum = TRUE;
        has_maximum = FALSE;

        if (quantifier == '+') {
          minimum = 1;
        } else if (quantifier == '?') {
          maximum     = 1;
          has_maximum = TRUE;
        } else if (quantifier == '{') {
          has_minimum = parser->position < parser->size && parser->text[parser->position] != ',';

          if (has_minimum && !l_languages_regula_regexp_count(parser, &minimum)) {
            return NULL;
          }

          if (parser->position < parser->size && parser->text[parser->position] == ',') {
            parser->position++;
            has_maximum = parser->position < parser->size && parser->text[parser->position] != '}';

            if (has_maximum && !l_languages_regula_regexp_count(parser, &maximum)) {
              return NULL;
            }
          } else {
            maximum     = minimum;
            has_maximum = TRUE;
          }

          if ((!has_minimum && !has_maximum) || parser->position == parser->size || parser->text[parser->position] != '}' ||
              (has_minimum && has_maximum && minimum > maximum)) {
            l_languages_regula_regexp_fail(parser, GLOBAL_LOGOS_REGULA_REGEXP_INVALID, "invalid repetition bounds");
            return NULL;
          }

          parser->position++;
        }

        atom = logos_languages_regula_regula_operation_add_repeat(NULL, parser->regula, atom, minimum, maximum, has_minimum, has_maximum);

        if (parser->position < parser->size && (parser->text[parser->position] == '?' || parser->text[parser->position] == '+' ||
                                                parser->text[parser->position] == '*' || parser->text[parser->position] == '{')) {
          l_languages_regula_regexp_fail(parser, GLOBAL_LOGOS_REGULA_REGEXP_UNSUPPORTED, "lazy, possessive, and stacked quantifiers are unsupported");
          return NULL;
        }
      }
    }

    sequence = sequence == NULL ? atom : logos_languages_regula_regula_operation_add_concatenate(NULL, parser->regula, sequence, atom);

    if (nexus_data_slabbed_list_length_get(parser->regula->operations) > LOGOS_REGULA_REGEXP_MAX_OPERATIONS) {
      l_languages_regula_regexp_fail(parser, GLOBAL_LOGOS_REGULA_REGEXP_LIMIT, "expanded operation limit exceeded");
      return NULL;
    }
  }

  return sequence == NULL ? logos_languages_regula_regula_operation_add_empty_string(NULL, parser->regula) : sequence;
}

/* Written by an LLM. */
static LRegulaOperation *l_languages_regula_regexp_expression(LRegulaRegexpParser *parser) {
  LRegulaOperation *expression;
  LRegulaOperation *alternative;

  expression = l_languages_regula_regexp_sequence(parser);

  while (expression != NULL && parser->position < parser->size && parser->text[parser->position] == '|') {
    parser->position++;
    alternative = l_languages_regula_regexp_sequence(parser);

    if (alternative == NULL) {
      return NULL;
    }

    expression = logos_languages_regula_regula_operation_add_unionize(NULL, parser->regula, expression, alternative);

    if (nexus_data_slabbed_list_length_get(parser->regula->operations) > LOGOS_REGULA_REGEXP_MAX_OPERATIONS) {
      l_languages_regula_regexp_fail(parser, GLOBAL_LOGOS_REGULA_REGEXP_LIMIT, "expanded operation limit exceeded");
      return NULL;
    }
  }

  return expression;
}

/* Written by an LLM. */
NexusError *logos_languages_regula_regula_operation_add_regexp(const char *label, LRegula *regula, const char *regexp,
                                                               LRegulaOperation **out_operation) {
  LRegulaRegexpParser parser;
  LRegula            *staging;
  LRegulaOperation   *expression;
  LRegulaOperation   *source;
  LRegulaOperation    copied;
  uint_large          i;
  uint_large          offset;
  uint_large          count;

  NEXUS_PROGRAM_PRECONDITION(regula != NULL && regexp != NULL && out_operation != NULL, "regexp import arguments must not be NULL");
  NEXUS_PROGRAM_PRECONDITION(out_operation != &regula->root_rule, "regexp output must not alias the root");

  *out_operation = NULL;
  parser.size    = 0;

  while (regexp[parser.size] != '\0') {
    if (parser.size == LOGOS_REGULA_REGEXP_MAX_SIZE) {
      return NEXUS_PROGRAM_ERROR_FAILURE(GLOBAL_LOGOS_REGULA_REGEXP_LIMIT, "Regexp byte %llu: text limit exceeded", (unsigned long long)parser.size);
    }

    parser.size++;
  }

  staging                        = logos_languages_regula_regula_create(NULL, 0);
  staging->internal_constructing = TRUE;
  parser.regula                  = staging;
  parser.text                    = regexp;
  parser.position                = 0;
  parser.depth                   = 0;
  parser.error                   = NEXUS_PROGRAM_ERROR_SUCCESS();
  expression                     = l_languages_regula_regexp_expression(&parser);

  if (expression == NULL && parser.error == NEXUS_PROGRAM_ERROR_SUCCESS()) {
    l_languages_regula_regexp_fail(&parser, GLOBAL_LOGOS_REGULA_REGEXP_INVALID, "missing expression");
  }

  if (expression != NULL && parser.position != parser.size) {
    l_languages_regula_regexp_fail(&parser, GLOBAL_LOGOS_REGULA_REGEXP_INVALID, "unmatched closing parenthesis");
  }

  if (parser.error != NEXUS_PROGRAM_ERROR_SUCCESS()) {
    logos_languages_regula_regula_destroy(staging);
    return parser.error;
  }

  NEXUS_PROGRAM_INVARIANT(expression != NULL, "successful parsing must produce an expression");

  /* Staging construction never attaches nodes, so every child precedes its parent. */
  offset = nexus_data_slabbed_list_length_get(regula->operations);
  count  = nexus_data_slabbed_list_length_get(staging->operations);

  for (i = 0; i < count; i++) {
    source = (LRegulaOperation *)nexus_data_slabbed_list_at(staging->operations, i);
    copied = *source;

    switch (copied.kind) {
    case LROK_UNIONIZE:
      copied.operation_data.unionize.operation_a =
          (LRegulaOperation *)nexus_data_slabbed_list_at(regula->operations, offset + source->operation_data.unionize.operation_a->storage_index);
      copied.operation_data.unionize.operation_b =
          (LRegulaOperation *)nexus_data_slabbed_list_at(regula->operations, offset + source->operation_data.unionize.operation_b->storage_index);
      break;
    case LROK_CONCATENATE:
      copied.operation_data.concatenate.operation_a =
          (LRegulaOperation *)nexus_data_slabbed_list_at(regula->operations, offset + source->operation_data.concatenate.operation_a->storage_index);
      copied.operation_data.concatenate.operation_b =
          (LRegulaOperation *)nexus_data_slabbed_list_at(regula->operations, offset + source->operation_data.concatenate.operation_b->storage_index);
      break;
    case LROK_STAR:
      copied.operation_data.operation_star =
          (LRegulaOperation *)nexus_data_slabbed_list_at(regula->operations, offset + source->operation_data.operation_star->storage_index);
      break;
    case LROK_EMPTY_LANGUAGE:
    case LROK_EMPTY_STRING:
    case LROK_SYMBOL:
      break;
    case LROK_COUNT:
    default:
      NEXUS_PROGRAM_PANIC("invalid staging operation kind");
    }

    (void)l_languages_regula_operation_intern(regula, &copied);
  }

  *out_operation          = (LRegulaOperation *)nexus_data_slabbed_list_at(regula->operations, offset + expression->storage_index);
  (*out_operation)->label = label;
  logos_languages_regula_regula_destroy(staging);
  *out_operation = logos_languages_regula_regula_operation_add(regula, *out_operation);

  return NEXUS_PROGRAM_ERROR_SUCCESS();
}

typedef struct LRegulaRegexpWriteFrame {
  const LRegulaOperation *operation;
  uint8                   phase;
  uint8                   parent_precedence;
  boolean                 grouped;
} LRegulaRegexpWriteFrame;

/* Written by an LLM. The fixed scratch buffer covers every byte in a class. */
static uint_large l_languages_regula_regexp_mask_write(const LRegulaObservationMask mask, char *output) {
  static const char digits[] = "0123456789ABCDEF";
  uint16            observation;
  uint16            range_start;
  uint16            range_end;
  uint16            count;
  uint_large        position;

  count = 0;

  for (observation = 0; observation < 256; observation++) {
    if (logos_languages_regula_observation_mask_get(mask, (byte)observation)) {
      count++;
    }
  }

  position = 0;

  if (count != 1) {
    output[position++] = '[';
  }

  observation = 0;

  while (observation < 256) {
    if (!logos_languages_regula_observation_mask_get(mask, (byte)observation)) {
      observation++;
      continue;
    }

    range_start = observation;

    while (observation + 1 < 256 && logos_languages_regula_observation_mask_get(mask, (byte)(observation + 1))) {
      observation++;
    }

    range_end          = observation;
    output[position++] = '\\';
    output[position++] = 'x';
    output[position++] = digits[range_start >> 4];
    output[position++] = digits[range_start & 15];

    if (range_end > range_start) {
      if (range_end > range_start + 1) {
        output[position++] = '-';
      }

      output[position++] = '\\';
      output[position++] = 'x';
      output[position++] = digits[range_end >> 4];
      output[position++] = digits[range_end & 15];
    }

    observation++;
  }

  if (count != 1) {
    output[position++] = ']';
  }

  output[position] = '\0';

  return position;
}

/* Written by an LLM. Bounds expansion before delegating buffer growth to Nexus. */
static NexusError *l_languages_regula_regexp_write_append(NexusStringBuilder *builder, const char *text, uint_large size) {
  if (size > LOGOS_REGULA_REGEXP_MAX_SIZE - nexus_strings_string_builder_length_get(builder)) {
    return NEXUS_PROGRAM_ERROR_FAILURE(GLOBAL_LOGOS_REGULA_REGEXP_LIMIT, "regexp output size limit exceeded");
  }

  return nexus_strings_string_builder_append(builder, text);
}

/* Written by an LLM. */
NexusError *logos_languages_regula_regula_regexp_write(const LRegula *regula, char **out_regexp) {
  NexusStringBuilder       builder;
  LRegulaRegexpWriteFrame *frames;
  LRegulaRegexpWriteFrame *frame;
  const LRegulaOperation  *operation;
  const LRegulaOperation  *child;
  byte                    *active_nodes;
  uint_large               operation_count;
  uint_large               frame_count;
  uint_large               frame_capacity;
  uint_large               new_capacity;
  uint_large               index;
  uint_large               encoded_size;
  uint32                   group_depth;
  uint8                    precedence;
  uint8                    child_precedence;
  char                     encoded_mask[1027];
  NexusError              *error;

  NEXUS_PROGRAM_PRECONDITION(regula != NULL && out_regexp != NULL, "regexp write arguments must not be NULL");

  *out_regexp = NULL;

  if (regula->root_rule == NULL) {
    *out_regexp      = (char *)nexus_memory_allocate(3);
    (*out_regexp)[0] = '[';
    (*out_regexp)[1] = ']';
    (*out_regexp)[2] = '\0';

    return NEXUS_PROGRAM_ERROR_SUCCESS();
  }

  operation_count = nexus_data_slabbed_list_length_get(regula->operations);

  if (operation_count == 0 || operation_count > LOGOS_REGULA_REGEXP_MAX_OPERATIONS) {
    return NEXUS_PROGRAM_ERROR_FAILURE(GLOBAL_LOGOS_REGULA_REGEXP_LIMIT, "regexp workspace operation limit exceeded");
  }

  frame_capacity = operation_count < 64 ? operation_count + 1 : 64;
  frames         = (LRegulaRegexpWriteFrame *)nexus_memory_allocate(frame_capacity * NEXUS_SIZEOF(*frames));
  active_nodes   = (byte *)nexus_memory_allocate(operation_count);
  nexus_memory_bytes_clear(active_nodes, operation_count);
  builder                     = nexus_strings_string_builder_create_growable(64);
  frame_count                 = 1;
  frames[0].operation         = regula->root_rule;
  frames[0].phase             = 0;
  frames[0].parent_precedence = 0;
  group_depth                 = 0;
  error                       = NEXUS_PROGRAM_ERROR_SUCCESS();

  while (frame_count > 0 && error == NEXUS_PROGRAM_ERROR_SUCCESS()) {
    frame     = &frames[frame_count - 1];
    operation = frame->operation;

    if (operation == NULL || operation->owner != regula || operation->storage_index >= operation_count ||
        nexus_data_slabbed_list_at_const(regula->operations, operation->storage_index) != operation) {
      error = NEXUS_PROGRAM_ERROR_FAILURE(GLOBAL_LOGOS_REGULA_REGEXP_INVALID_GRAPH, "regexp graph contains a foreign or missing node");
      break;
    }

    index = operation->storage_index;
    child = NULL;

    if (frame->phase == 0) {
      if (active_nodes[index]) {
        error = NEXUS_PROGRAM_ERROR_FAILURE(GLOBAL_LOGOS_REGULA_REGEXP_INVALID_GRAPH, "regexp graph contains a cycle");
        break;
      }

      active_nodes[index] = TRUE;

      if (operation->kind == LROK_UNIONIZE) {
        precedence = 1;
      } else if (operation->kind == LROK_CONCATENATE) {
        precedence = 2;
      } else if (operation->kind == LROK_STAR) {
        precedence = 3;
      } else {
        precedence = 4;
      }

      frame->grouped = precedence < frame->parent_precedence;

      if (frame->grouped) {
        if (group_depth == LOGOS_REGULA_REGEXP_MAX_DEPTH) {
          error = NEXUS_PROGRAM_ERROR_FAILURE(GLOBAL_LOGOS_REGULA_REGEXP_LIMIT, "regexp output nesting limit exceeded");
          break;
        }

        group_depth++;
        error = l_languages_regula_regexp_write_append(&builder, "(?:", 3);

        if (error != NEXUS_PROGRAM_ERROR_SUCCESS()) {
          break;
        }
      }

      switch (operation->kind) {
      case LROK_EMPTY_LANGUAGE:
        error = l_languages_regula_regexp_write_append(&builder, "[]", 2);
        break;
      case LROK_EMPTY_STRING:
        if (group_depth == LOGOS_REGULA_REGEXP_MAX_DEPTH) {
          error = NEXUS_PROGRAM_ERROR_FAILURE(GLOBAL_LOGOS_REGULA_REGEXP_LIMIT, "regexp output nesting limit exceeded");
        } else {
          error = l_languages_regula_regexp_write_append(&builder, "(?:)", 4);
        }
        break;
      case LROK_SYMBOL:
        encoded_size = l_languages_regula_regexp_mask_write(operation->operation_data.symbol_mask, encoded_mask);
        error        = l_languages_regula_regexp_write_append(&builder, encoded_mask, encoded_size);
        break;
      case LROK_UNIONIZE:
        child = operation->operation_data.unionize.operation_a;
        break;
      case LROK_CONCATENATE:
        child = operation->operation_data.concatenate.operation_a;
        break;
      case LROK_STAR:
        child = operation->operation_data.operation_star;
        break;
      case LROK_COUNT:
      default:
        error = NEXUS_PROGRAM_ERROR_FAILURE(GLOBAL_LOGOS_REGULA_REGEXP_INVALID_GRAPH, "regexp graph contains an invalid operation kind");
        break;
      }

      frame->phase = 1;

      if ((operation->kind == LROK_UNIONIZE || operation->kind == LROK_CONCATENATE || operation->kind == LROK_STAR) && child == NULL) {
        error = NEXUS_PROGRAM_ERROR_FAILURE(GLOBAL_LOGOS_REGULA_REGEXP_INVALID_GRAPH, "regexp graph contains a missing operand");
      }

      if (child == NULL) {
        active_nodes[index] = FALSE;
        frame_count--;
      }
    } else if (frame->phase == 1 && operation->kind != LROK_STAR) {
      if (operation->kind == LROK_UNIONIZE) {
        error = l_languages_regula_regexp_write_append(&builder, "|", 1);
        child = operation->operation_data.unionize.operation_b;
      } else {
        child = operation->operation_data.concatenate.operation_b;
      }

      frame->phase = 2;

      if (child == NULL) {
        error = NEXUS_PROGRAM_ERROR_FAILURE(GLOBAL_LOGOS_REGULA_REGEXP_INVALID_GRAPH, "regexp graph contains a missing operand");
      }
    } else {
      if (operation->kind == LROK_STAR) {
        error = l_languages_regula_regexp_write_append(&builder, "*", 1);
      }

      if (frame->grouped && error == NEXUS_PROGRAM_ERROR_SUCCESS()) {
        error = l_languages_regula_regexp_write_append(&builder, ")", 1);
        group_depth--;
      }

      active_nodes[index] = FALSE;
      frame_count--;
    }

    if (child != NULL && error == NEXUS_PROGRAM_ERROR_SUCCESS()) {
      if (operation->kind == LROK_STAR) {
        child_precedence = 4;
      } else if (operation->kind == LROK_CONCATENATE) {
        child_precedence = 2;
      } else {
        child_precedence = 1;
      }
      if (frame_count == frame_capacity) {
        new_capacity = frame_capacity * 2;

        if (new_capacity > operation_count + 1) {
          new_capacity = operation_count + 1;
        }

        if (new_capacity <= frame_capacity) {
          error = NEXUS_PROGRAM_ERROR_FAILURE(GLOBAL_LOGOS_REGULA_REGEXP_INVALID_GRAPH, "regexp graph exceeds its node count");
          break;
        }

        frames         = (LRegulaRegexpWriteFrame *)nexus_memory_reallocate(frames, new_capacity * NEXUS_SIZEOF(*frames));
        frame_capacity = new_capacity;
      }

      frames[frame_count].operation         = child;
      frames[frame_count].phase             = 0;
      frames[frame_count].parent_precedence = child_precedence;
      frame_count++;
    }
  }

  if (error == NEXUS_PROGRAM_ERROR_SUCCESS()) {
    *out_regexp    = builder.buffer;
    builder.buffer = NULL;
  }

  nexus_strings_string_builder_destroy(&builder);
  nexus_memory_deallocate(active_nodes);
  nexus_memory_deallocate(frames);

  return error;
}
