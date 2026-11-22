#include <nexus.h>

#include <logos.h>

#include <rf_bindings.h>

#include "../ruleforge.h"
#include "./r_internal.h"

/*
The AST builder reads the Logos syntax tree through the typed views that `genesis bind` generates from spec/ruleforge.gen
(rf_bindings.h): every rule is accessed by the rules and tokens the grammar says it holds, so a grammar change that removes
or renames one breaks this file at compile time instead of silently misreading a tree. The one thing the bindings do not
model is the operator structure of a binding-power expression, which is a Contexta operation rather than a rule; that part
(r_ast_operand) reads the green nodes directly and is the only place that does.
*/

typedef struct RAstBuilder {
  RuleforgeCompilation *compilation;
  uint32                file;
  const byte           *base;
  const LParserTree    *tree;
} RAstBuilder;

typedef struct RAstSpine {
  const LParserGreenNode *node[4];
  uint32                  count;
} RAstSpine;

static RuleforgeNode *r_ast_expr(RAstBuilder *builder, RfExpr view);
static RuleforgeNode *r_ast_generator(RAstBuilder *builder, RfGeneratorStmt view);
static RuleforgeNode *r_ast_rule_item(RAstBuilder *builder, RfRuleBodyItem view);
static RuleforgeNode *r_ast_rule_command(RAstBuilder *builder, RfRuleCommand view);

/* ---------------------------------------------------------------------------- */
/* NODES                                                                        */
/* ---------------------------------------------------------------------------- */

static const char *const r_ast_kind_names[RNK_COUNT] = {"Module",
                                                        "Version",
                                                        "Import",
                                                        "Schema",
                                                        "Field",
                                                        "Dataset",
                                                        "ThemeSchema",
                                                        "Key",
                                                        "ColorSet",
                                                        "ColorDef",
                                                        "Theme",
                                                        "Extends",
                                                        "Implements",
                                                        "ThemeStyle",
                                                        "ThemeState",
                                                        "StyleProperty",
                                                        "AlphaOverride",
                                                        "Template",
                                                        "Function",
                                                        "Ruleset",
                                                        "Description",
                                                        "CoreConditions",
                                                        "Configuration",
                                                        "ConfigAssign",
                                                        "Adapter",
                                                        "AdapterMap",
                                                        "Params",
                                                        "Param",
                                                        "Type",
                                                        "Block",
                                                        "Let",
                                                        "Assign",
                                                        "If",
                                                        "For",
                                                        "Where",
                                                        "Return",
                                                        "Include",
                                                        "With",
                                                        "WithBinding",
                                                        "Emit",
                                                        "Rule",
                                                        "Native",
                                                        "Scope",
                                                        "ScopeAssign",
                                                        "Baseline",
                                                        "Variants",
                                                        "When",
                                                        "Action",
                                                        "Style",
                                                        "Inherit",
                                                        "Int",
                                                        "Float",
                                                        "String",
                                                        "Bool",
                                                        "None",
                                                        "Color",
                                                        "List",
                                                        "Call",
                                                        "Arg",
                                                        "Ref",
                                                        "Index",
                                                        "Unary",
                                                        "Binary"};

const char *ruleforge_ast_kind_name_get(RuleforgeNodeKind kind) {
  NEXUS_PROGRAM_PRECONDITION((int)kind >= 0 && kind < RNK_COUNT, "ruleforge_ast_kind_name_get: invalid kind");

  return r_ast_kind_names[kind];
}

RuleforgeNode *ruleforge_ast_node_create(RuleforgeCompilation *compilation, RuleforgeNodeKind kind, RuleforgeSpan span) {
  RuleforgeNode *node;

  NEXUS_PROGRAM_PRECONDITION(compilation != NULL, "ruleforge_ast_node_create: NULL compilation");

  node       = (RuleforgeNode *)nexus_data_arena_allocate(&compilation->arena, NEXUS_SIZEOF(*node));
  node->kind = kind;
  node->span = span;

  return node;
}

void ruleforge_ast_child_add(RuleforgeCompilation *compilation, RuleforgeNode *parent, RuleforgeNode *child) {
  RuleforgeNode **grown;
  uint32          capacity;

  NEXUS_PROGRAM_PRECONDITION(compilation != NULL && parent != NULL && child != NULL, "ruleforge_ast_child_add: NULL argument");

  /* Capacity starts at 4 and doubles; it is implied by the count, so nodes carry no capacity field. */
  if (parent->child_count == 0) {
    parent->children = (RuleforgeNode **)nexus_data_arena_allocate(&compilation->arena, 4 * NEXUS_SIZEOF(RuleforgeNode *));
  } else if (parent->child_count >= 4 && (parent->child_count & (parent->child_count - 1)) == 0) {
    capacity = parent->child_count * 2;
    grown    = (RuleforgeNode **)nexus_data_arena_allocate(&compilation->arena, (uint_large)capacity * NEXUS_SIZEOF(RuleforgeNode *));
    nexus_memory_bytes_copy((const void *)parent->children, (void *)grown, (uint_large)parent->child_count * NEXUS_SIZEOF(RuleforgeNode *));
    parent->children = grown;
  }

  parent->children[parent->child_count] = child;
  parent->child_count++;
}

/* ---------------------------------------------------------------------------- */
/* GREEN NODE HELPERS                                                           */
/* ---------------------------------------------------------------------------- */

/* Summary: the byte range of the tokens below a node, leaving out the trivia in front of the first one. */
static boolean r_ast_node_range(const RAstBuilder *builder, const LParserGreenNode *node, uint32 *out_start, uint32 *out_end) {
  const LParserGreenAlternative *alternative;
  LParserGreenKind               kind;
  const byte                    *text;
  uint_large                     size;
  uint32                         i;
  boolean                        found;
  uint32                         start;
  uint32                         end;

  kind = logos_language_tooling_parsing_syntax_green_node_kind_get(node);

  if (kind == LPGK_TOKEN) {
    text = logos_language_tooling_parsing_syntax_green_node_text_get(builder->tree, node, &size);

    if (size == 0) {
      return FALSE;
    }

    *out_start = (uint32)(text - builder->base);
    *out_end   = *out_start + (uint32)size;

    return TRUE;
  }

  if (rf_node_is_rule(builder->tree, node, "trivia")) {
    return FALSE;
  }

  alternative = logos_language_tooling_parsing_syntax_green_node_alternative_first_get(builder->tree, node);
  found       = FALSE;

  if (alternative == NULL) {
    return FALSE;
  }

  for (i = 0; i < logos_language_tooling_parsing_syntax_alternative_child_count_get(alternative); i++) {
    if (r_ast_node_range(builder, logos_language_tooling_parsing_syntax_alternative_child_get(builder->tree, alternative, i), &start, &end)) {
      if (!found || start < *out_start) {
        *out_start = start;
      }

      if (!found || end > *out_end) {
        *out_end = end;
      }

      found = TRUE;
    }
  }

  return found;
}

static RuleforgeSpan r_ast_span(const RAstBuilder *builder, const LParserGreenNode *node) {
  uint32 start;
  uint32 end;

  start = 0;
  end   = 0;

  if (node == NULL || !r_ast_node_range(builder, node, &start, &end)) {
    return ruleforge_span_make(builder->file, 0, 0);
  }

  return ruleforge_span_make(builder->file, start, end);
}

/* Summary: the start offset of a node, or 0xFFFFFFFF when it holds no token. Orders siblings the views list by kind. */
static uint32 r_ast_start(const RAstBuilder *builder, const LParserGreenNode *node) {
  uint32 start;
  uint32 end;

  if (node == NULL || !r_ast_node_range(builder, node, &start, &end)) {
    return 0xFFFFFFFFU;
  }

  return start;
}

static char *r_ast_token_text(const RAstBuilder *builder, const LParserGreenNode *token) {
  const byte *text;
  uint_large  size;

  text = logos_language_tooling_parsing_syntax_green_node_text_get(builder->tree, token, &size);

  return nexus_data_arena_text_copy(&builder->compilation->arena, (const char *)text, size);
}

/* Summary: all tokens below a node written one after the other without trivia: a dotted name or a configuration key. */
static void r_ast_tokens_join(const RAstBuilder *builder, const LParserGreenNode *node, NexusStringBuilder *out) {
  const LParserGreenAlternative *alternative;
  const LParserGreenNode        *child;
  const byte                    *text;
  uint_large                     size;
  uint32                         i;

  if (logos_language_tooling_parsing_syntax_green_node_kind_get(node) == LPGK_TOKEN) {
    text = logos_language_tooling_parsing_syntax_green_node_text_get(builder->tree, node, &size);
    (void)nexus_strings_string_builder_append_formatted(out, "%.*s", (int)size, (const char *)text);

    return;
  }

  if (rf_node_is_rule(builder->tree, node, "trivia")) {
    return;
  }

  alternative = logos_language_tooling_parsing_syntax_green_node_alternative_first_get(builder->tree, node);

  if (alternative == NULL) {
    return;
  }

  for (i = 0; i < logos_language_tooling_parsing_syntax_alternative_child_count_get(alternative); i++) {
    child = logos_language_tooling_parsing_syntax_alternative_child_get(builder->tree, alternative, i);
    r_ast_tokens_join(builder, child, out);
  }
}

static char *r_ast_joined(const RAstBuilder *builder, const LParserGreenNode *node) {
  NexusStringBuilder out;
  char              *result;

  out = nexus_strings_string_builder_create_growable(32);
  r_ast_tokens_join(builder, node, &out);
  result = nexus_data_arena_text_copy(&builder->compilation->arena, nexus_strings_string_builder_string_get(&out),
                                      nexus_strings_string_builder_length_get(&out));
  nexus_strings_string_builder_destroy(&out);

  return result;
}

/* Summary: the unescaped value of a string literal token ("..." with backslash escapes). */
static char *r_ast_string_unescape(const RAstBuilder *builder, const LParserGreenNode *token) {
  const byte *source;
  uint_large  size;
  char       *result;
  uint32      i;
  uint32      length;

  source = logos_language_tooling_parsing_syntax_green_node_text_get(builder->tree, token, &size);
  result = (char *)nexus_data_arena_allocate(&builder->compilation->arena, size + 1);
  length = 0;

  for (i = 1; i + 1 < size; i++) {
    if (source[i] == '\\' && i + 2 < size) {
      i++;

      if (source[i] == 'n') {
        result[length++] = '\n';
      } else if (source[i] == 't') {
        result[length++] = '\t';
      } else if (source[i] == 'r') {
        result[length++] = '\r';
      } else {
        result[length++] = (char)source[i];
      }
    } else {
      result[length++] = (char)source[i];
    }
  }

  result[length] = '\0';

  return result;
}

/* ---------------------------------------------------------------------------- */
/* COLORS                                                                       */
/* ---------------------------------------------------------------------------- */

static uint32 r_ast_channel_pack(f_real red, f_real green, f_real blue, uint32 alpha) {
  uint32 red_byte;
  uint32 green_byte;
  uint32 blue_byte;

  red_byte   = (uint32)((red * 255.0) + 0.5);
  green_byte = (uint32)((green * 255.0) + 0.5);
  blue_byte  = (uint32)((blue * 255.0) + 0.5);

  if (red_byte > 255) {
    red_byte = 255;
  }

  if (green_byte > 255) {
    green_byte = 255;
  }

  if (blue_byte > 255) {
    blue_byte = 255;
  }

  return (red_byte << 24) | (green_byte << 16) | (blue_byte << 8) | (alpha & 0xFFU);
}

/* Summary: the number after a prefix of skip bytes in a token such as r255 (skip 1) or 12.5 (skip 0). */
static f_real r_ast_number_read(const RAstBuilder *builder, const LParserGreenNode *token, uint32 skip) {
  const byte *source;
  uint_large  size;
  f_real      value;
  f_real      scale;
  boolean     fraction;
  uint32      i;

  source   = logos_language_tooling_parsing_syntax_green_node_text_get(builder->tree, token, &size);
  value    = 0.0;
  scale    = 1.0;
  fraction = FALSE;

  for (i = skip; i < size; i++) {
    if (source[i] == '.') {
      fraction = TRUE;
    } else if (fraction) {
      scale *= 0.1;
      value += (f_real)(source[i] - '0') * scale;
    } else {
      value = (value * 10.0) + (f_real)(source[i] - '0');
    }
  }

  return value;
}

/* Summary: hue in degrees, saturation and lightness/value in percent, to red, green and blue in 0..1. */
static void r_ast_hue_convert(f_real hue, f_real saturation, f_real third, boolean lightness, f_real *out_red, f_real *out_green, f_real *out_blue) {
  f_real chroma;
  f_real sector;
  f_real remainder;
  f_real second;
  f_real match;
  f_real red;
  f_real green;
  f_real blue;
  f_real doubled;
  f_real distance;

  saturation /= 100.0;
  third /= 100.0;

  while (hue >= 360.0) {
    hue -= 360.0;
  }

  if (lightness) {
    doubled = (2.0 * third) - 1.0;
    chroma  = (1.0 - (doubled < 0.0 ? -doubled : doubled)) * saturation;
  } else {
    chroma = third * saturation;
  }

  sector    = hue / 60.0;
  remainder = sector;

  while (remainder >= 2.0) {
    remainder -= 2.0;
  }

  distance = remainder - 1.0;
  second   = chroma * (1.0 - (distance < 0.0 ? -distance : distance));

  if (sector < 1.0) {
    red = chroma, green = second, blue = 0.0;
  } else if (sector < 2.0) {
    red = second, green = chroma, blue = 0.0;
  } else if (sector < 3.0) {
    red = 0.0, green = chroma, blue = second;
  } else if (sector < 4.0) {
    red = 0.0, green = second, blue = chroma;
  } else if (sector < 5.0) {
    red = second, green = 0.0, blue = chroma;
  } else {
    red = chroma, green = 0.0, blue = second;
  }

  match      = lightness ? third - (chroma / 2.0) : third - chroma;
  *out_red   = red + match;
  *out_green = green + match;
  *out_blue  = blue + match;
}

static uint32 r_ast_hex_value(const byte *digits, uint32 count) {
  uint32 value;
  uint32 i;
  byte   digit;

  value = 0;

  for (i = 0; i < count; i++) {
    digit = digits[i];
    value <<= 4;

    if (digit >= '0' && digit <= '9') {
      value |= (uint32)(digit - '0');
    } else if (digit >= 'a' && digit <= 'f') {
      value |= (uint32)(digit - 'a' + 10);
    } else {
      value |= (uint32)(digit - 'A' + 10);
    }
  }

  return value;
}

static RuleforgeNode *r_ast_color(RAstBuilder *builder, RfColorLiteral view) {
  RuleforgeNode          *node;
  const LParserGreenNode *hex;
  RfRgbColor              rgb;
  RfHslColor              hsl;
  RfHsvColor              hsv;
  uint32                  alpha;
  f_real                  red;
  f_real                  green;
  f_real                  blue;
  boolean                 has_alpha;

  node       = ruleforge_ast_node_create(builder->compilation, RNK_COLOR, r_ast_span(builder, view.node));
  node->text = r_ast_joined(builder, view.node);
  alpha      = 255;
  has_alpha  = FALSE;
  red        = 0.0;
  green      = 0.0;
  blue       = 0.0;

  hex = rf_color_literal_token_hex_color(view, 0);
  rgb = rf_color_literal_rule_rgb_color(view, 0);
  hsl = rf_color_literal_rule_hsl_color(view, 0);
  hsv = rf_color_literal_rule_hsv_color(view, 0);

  if (rf_color_literal_count_rule_rgb_color_call(view) > 0) {
    rgb = rf_rgb_color_call_rule_rgb_color(rf_color_literal_rule_rgb_color_call(view, 0), 0);
  }

  if (hex != NULL) {
    const byte *digits;
    uint_large  size;

    digits = logos_language_tooling_parsing_syntax_green_node_text_get(builder->tree, hex, &size);
    red    = (f_real)r_ast_hex_value(digits + 1, 2) / 255.0;
    green  = (f_real)r_ast_hex_value(digits + 3, 2) / 255.0;
    blue   = (f_real)r_ast_hex_value(digits + 5, 2) / 255.0;

    if (size == 9) {
      alpha     = r_ast_hex_value(digits + 7, 2);
      has_alpha = TRUE;
    }
  } else if (rgb.node != NULL) {
    red   = r_ast_number_read(builder, rf_rgb_color_token_color_red(rgb, 0), 1) / 255.0;
    green = r_ast_number_read(builder, rf_rgb_color_token_color_green(rgb, 0), 1) / 255.0;
    blue  = r_ast_number_read(builder, rf_rgb_color_token_color_blue(rgb, 0), 1) / 255.0;

    if (rf_rgb_color_count_token_color_alpha(rgb) > 0) {
      alpha     = (uint32)r_ast_number_read(builder, rf_rgb_color_token_color_alpha(rgb, 0), 1);
      has_alpha = TRUE;
    }
  } else if (hsl.node != NULL) {
    r_ast_hue_convert(r_ast_number_read(builder, rf_hsl_color_token_color_hue(hsl, 0), 1),
                      r_ast_number_read(builder, rf_hsl_color_token_color_saturation(hsl, 0), 1),
                      r_ast_number_read(builder, rf_hsl_color_token_color_lightness(hsl, 0), 1), TRUE, &red, &green, &blue);

    if (rf_hsl_color_count_token_color_alpha(hsl) > 0) {
      alpha     = (uint32)r_ast_number_read(builder, rf_hsl_color_token_color_alpha(hsl, 0), 1);
      has_alpha = TRUE;
    }
  } else if (hsv.node != NULL) {
    r_ast_hue_convert(r_ast_number_read(builder, rf_hsv_color_token_color_hue(hsv, 0), 1),
                      r_ast_number_read(builder, rf_hsv_color_token_color_saturation(hsv, 0), 1),
                      r_ast_number_read(builder, rf_hsv_color_token_color_value_channel(hsv, 0), 1), FALSE, &red, &green, &blue);

    if (rf_hsv_color_count_token_color_alpha(hsv) > 0) {
      alpha     = (uint32)r_ast_number_read(builder, rf_hsv_color_token_color_alpha(hsv, 0), 1);
      has_alpha = TRUE;
    }
  }

  node->integer = (int64)r_ast_channel_pack(red, green, blue, alpha);
  node->flags   = has_alpha ? RNF_ALPHA : 0;

  return node;
}

/* ---------------------------------------------------------------------------- */
/* TYPES AND PARAMETERS                                                         */
/* ---------------------------------------------------------------------------- */

static RuleforgeNode *r_ast_type(RAstBuilder *builder, RfType view) {
  RuleforgeNode *node;
  RfBaseType     base;

  node = ruleforge_ast_node_create(builder->compilation, RNK_TYPE, r_ast_span(builder, view.node));
  base = rf_type_rule_base_type(view, 0);

  if (rf_type_count_token_question(view) > 0) {
    node->flags |= RNF_OPTIONAL;
  }

  if (rf_base_type_count_rule_type(base) > 0) {
    /* list<T> or dataset<T> */
    node->text = rf_base_type_count_token_kw_list(base) > 0 ? "list" : "dataset";
    ruleforge_ast_child_add(builder->compilation, node, r_ast_type(builder, rf_base_type_rule_type(base, 0)));
  } else if (rf_base_type_count_rule_qualified_name(base) > 0) {
    node->text = r_ast_joined(builder, rf_base_type_rule_qualified_name(base, 0).node);
  } else {
    node->text = r_ast_joined(builder, base.node);
  }

  return node;
}

static RuleforgeNode *r_ast_params(RAstBuilder *builder, RfParameters view) {
  RuleforgeNode *node;
  RuleforgeNode *param;
  RfParameter    parameter;
  uint32         i;

  node = ruleforge_ast_node_create(builder->compilation, RNK_PARAMS, r_ast_span(builder, view.node));

  for (i = 0; i < rf_parameters_count_rule_parameter(view); i++) {
    parameter   = rf_parameters_rule_parameter(view, i);
    param       = ruleforge_ast_node_create(builder->compilation, RNK_PARAM, r_ast_span(builder, parameter.node));
    param->text = r_ast_token_text(builder, rf_parameter_token_identifier(parameter, 0));
    ruleforge_ast_child_add(builder->compilation, param, r_ast_type(builder, rf_parameter_rule_type(parameter, 0)));
    ruleforge_ast_child_add(builder->compilation, node, param);
  }

  return node;
}

/* ---------------------------------------------------------------------------- */
/* EXPRESSIONS                                                                  */
/* ---------------------------------------------------------------------------- */

static RuleforgeNode *r_ast_reference(RAstBuilder *builder, RfReference view) {
  RuleforgeNode *node;
  char          *text;

  node = ruleforge_ast_node_create(builder->compilation, RNK_REF, r_ast_span(builder, view.node));
  text = r_ast_joined(builder, view.node);

  node->integer = RNK_REF_PLAIN;

  /* The first token carries the '$' or '@' prefix, which selects the namespace, not part of the name. */
  if (rf_reference_count_token_parameter_reference(view) > 0) {
    node->integer = RNK_REF_PARAMETER;
    text++;
  } else if (rf_reference_count_token_compiler_reference(view) > 0) {
    node->integer = RNK_REF_COMPILER;
    text++;
  }

  node->text = text;

  return node;
}

static RuleforgeNode *r_ast_arguments(RAstBuilder *builder, RuleforgeNode *parent, RfArguments view) {
  RuleforgeNode *node;
  RfArgument     argument;
  uint32         i;

  for (i = 0; i < rf_arguments_count_rule_argument(view); i++) {
    argument = rf_arguments_rule_argument(view, i);
    node     = ruleforge_ast_node_create(builder->compilation, RNK_ARG, r_ast_span(builder, argument.node));

    if (rf_argument_count_token_identifier(argument) > 0) {
      node->text = r_ast_token_text(builder, rf_argument_token_identifier(argument, 0));
    }

    ruleforge_ast_child_add(builder->compilation, node, r_ast_expr(builder, rf_argument_rule_expr(argument, 0)));
    ruleforge_ast_child_add(builder->compilation, parent, node);
  }

  return parent;
}

static RuleforgeNode *r_ast_call(RAstBuilder *builder, RfCall view) {
  RuleforgeNode *node;

  node = ruleforge_ast_node_create(builder->compilation, RNK_CALL, r_ast_span(builder, view.node));
  ruleforge_ast_child_add(builder->compilation, node, r_ast_reference(builder, rf_call_rule_reference(view, 0)));

  return r_ast_arguments(builder, node, rf_call_rule_arguments(view, 0));
}

static RuleforgeNode *r_ast_atom(RAstBuilder *builder, RfAtom view) {
  RuleforgeNode *node;
  uint32         i;

  if (rf_atom_count_rule_expr(view) > 0) {
    return r_ast_expr(builder, rf_atom_rule_expr(view, 0));
  }

  if (rf_atom_count_rule_color_literal(view) > 0) {
    return r_ast_color(builder, rf_atom_rule_color_literal(view, 0));
  }

  if (rf_atom_count_rule_list_literal(view) > 0) {
    RfListLiteral list;

    list = rf_atom_rule_list_literal(view, 0);
    node = ruleforge_ast_node_create(builder->compilation, RNK_LIST, r_ast_span(builder, list.node));

    return r_ast_arguments(builder, node, rf_list_literal_rule_arguments(list, 0));
  }

  if (rf_atom_count_rule_call(view) > 0) {
    return r_ast_call(builder, rf_atom_rule_call(view, 0));
  }

  if (rf_atom_count_rule_indexed_reference(view) > 0) {
    RfIndexedReference indexed;

    indexed = rf_atom_rule_indexed_reference(view, 0);
    node    = r_ast_reference(builder, rf_indexed_reference_rule_reference(indexed, 0));

    for (i = 0; i < rf_indexed_reference_count_rule_expr(indexed); i++) {
      RuleforgeNode *index;

      index = ruleforge_ast_node_create(builder->compilation, RNK_INDEX, r_ast_span(builder, indexed.node));
      ruleforge_ast_child_add(builder->compilation, index, node);
      ruleforge_ast_child_add(builder->compilation, index, r_ast_expr(builder, rf_indexed_reference_rule_expr(indexed, i)));
      node = index;
    }

    return node;
  }

  if (rf_atom_count_token_string_literal(view) > 0) {
    node       = ruleforge_ast_node_create(builder->compilation, RNK_STRING, r_ast_span(builder, view.node));
    node->text = r_ast_string_unescape(builder, rf_atom_token_string_literal(view, 0));

    return node;
  }

  if (rf_atom_count_token_integer_literal(view) > 0) {
    const LParserGreenNode *token;
    const byte             *digits;
    uint_large              size;

    token         = rf_atom_token_integer_literal(view, 0);
    digits        = logos_language_tooling_parsing_syntax_green_node_text_get(builder->tree, token, &size);
    node          = ruleforge_ast_node_create(builder->compilation, RNK_INT, r_ast_span(builder, view.node));
    node->text    = r_ast_token_text(builder, token);
    node->integer = 0;

    for (i = 0; i < size; i++) {
      node->integer = (node->integer * 10) + (int64)(digits[i] - '0');
    }

    return node;
  }

  if (rf_atom_count_token_float_literal(view) > 0) {
    node       = ruleforge_ast_node_create(builder->compilation, RNK_FLOAT, r_ast_span(builder, view.node));
    node->text = r_ast_token_text(builder, rf_atom_token_float_literal(view, 0));
    node->real = r_ast_number_read(builder, rf_atom_token_float_literal(view, 0), 0);

    return node;
  }

  if (rf_atom_count_token_kw_true(view) > 0 || rf_atom_count_token_kw_false(view) > 0) {
    node          = ruleforge_ast_node_create(builder->compilation, RNK_BOOL, r_ast_span(builder, view.node));
    node->integer = rf_atom_count_token_kw_true(view) > 0 ? 1 : 0;

    return node;
  }

  return ruleforge_ast_node_create(builder->compilation, RNK_NONE, r_ast_span(builder, view.node));
}

/*
Summary: the significant children of a green node: tokens, rules and Pratt operator nodes, looking through sequences.
Description: trivia is dropped. Used only for operator nodes, whose operand structure the typed bindings do not model.
*/
static void r_ast_spine_collect(const LParserTree *tree, const LParserGreenNode *node, RAstSpine *spine) {
  const LParserGreenAlternative *alternative;
  const LParserGreenNode        *child;
  LParserGreenKind               kind;
  uint32                         i;

  alternative = logos_language_tooling_parsing_syntax_green_node_alternative_first_get(tree, node);

  if (alternative == NULL) {
    return;
  }

  for (i = 0; i < logos_language_tooling_parsing_syntax_alternative_child_count_get(alternative); i++) {
    child = logos_language_tooling_parsing_syntax_alternative_child_get(tree, alternative, i);
    kind  = logos_language_tooling_parsing_syntax_green_node_kind_get(child);

    if (kind == LPGK_SEQUENCE) {
      r_ast_spine_collect(tree, child, spine);
    } else if (kind == LPGK_RULE && rf_node_is_rule(tree, child, "trivia")) {
      continue;
    } else if (spine->count < 4) {
      spine->node[spine->count++] = child;
    }
  }
}

/* Summary: an operand of an operator: an atom rule or a nested operator node. */
static RuleforgeNode *r_ast_operand(RAstBuilder *builder, const LParserGreenNode *node) {
  RuleforgeNode *result;
  RAstSpine      spine;

  if (rf_node_is_rule(builder->tree, node, "atom")) {
    return r_ast_atom(builder, rf_atom_cast(builder->tree, node));
  }

  if (rf_node_is_rule(builder->tree, node, "expr")) {
    return r_ast_expr(builder, rf_expr_cast(builder->tree, node));
  }

  /* A binding-power operator: prefix is [operator, operand], infix is [left, operator, right]. */
  spine.count = 0;
  r_ast_spine_collect(builder->tree, node, &spine);

  if (spine.count == 2) {
    result       = ruleforge_ast_node_create(builder->compilation, RNK_UNARY, r_ast_span(builder, node));
    result->text = r_ast_token_text(builder, spine.node[0]);
    ruleforge_ast_child_add(builder->compilation, result, r_ast_operand(builder, spine.node[1]));

    return result;
  }

  result       = ruleforge_ast_node_create(builder->compilation, RNK_BINARY, r_ast_span(builder, node));
  result->text = r_ast_token_text(builder, spine.node[1]);
  ruleforge_ast_child_add(builder->compilation, result, r_ast_operand(builder, spine.node[0]));
  ruleforge_ast_child_add(builder->compilation, result, r_ast_operand(builder, spine.node[2]));

  return result;
}

static RuleforgeNode *r_ast_expr(RAstBuilder *builder, RfExpr view) {
  RAstSpine spine;

  spine.count = 0;
  r_ast_spine_collect(builder->tree, view.node, &spine);

  return r_ast_operand(builder, spine.node[0]);
}

/* ---------------------------------------------------------------------------- */
/* RULES                                                                        */
/* ---------------------------------------------------------------------------- */

static RuleforgeNode *r_ast_rule_command(RAstBuilder *builder, RfRuleCommand view) {
  RuleforgeNode *node;

  if (rf_rule_command_count_rule_action_stmt(view) > 0) {
    RfActionStmt action;

    action     = rf_rule_command_rule_action_stmt(view, 0);
    node       = ruleforge_ast_node_create(builder->compilation, RNK_ACTION, r_ast_span(builder, action.node));
    node->text = rf_action_stmt_count_token_kw_show(action) > 0 ? "show" : "hide";

    return node;
  }

  if (rf_rule_command_count_rule_style_stmt(view) > 0) {
    RfStyleStmt style;

    style = rf_rule_command_rule_style_stmt(view, 0);
    node  = ruleforge_ast_node_create(builder->compilation, RNK_STYLE, r_ast_span(builder, style.node));
    ruleforge_ast_child_add(builder->compilation, node, r_ast_expr(builder, rf_style_stmt_rule_expr(style, 0)));

    return node;
  }

  node       = ruleforge_ast_node_create(builder->compilation, RNK_INHERIT, r_ast_span(builder, view.node));
  node->text = "action";

  return node;
}

static RuleforgeNode *r_ast_rule_item(RAstBuilder *builder, RfRuleBodyItem view) {
  RuleforgeNode *node;
  RuleforgeNode *entry;
  uint32         i;
  uint32         j;

  if (rf_rule_body_item_count_rule_rule_command(view) > 0) {
    return r_ast_rule_command(builder, rf_rule_body_item_rule_rule_command(view, 0));
  }

  if (rf_rule_body_item_count_rule_scope_stmt(view) > 0) {
    RfScopeStmt scope;

    scope = rf_rule_body_item_rule_scope_stmt(view, 0);
    node  = ruleforge_ast_node_create(builder->compilation, RNK_SCOPE, r_ast_span(builder, scope.node));

    for (i = 0; i < rf_scope_stmt_count_rule_scope_assignment(scope); i++) {
      RfScopeAssignment assignment;

      assignment  = rf_scope_stmt_rule_scope_assignment(scope, i);
      entry       = ruleforge_ast_node_create(builder->compilation, RNK_SCOPE_ASSIGN, r_ast_span(builder, assignment.node));
      entry->text = r_ast_joined(builder, rf_scope_assignment_rule_qualified_name(assignment, 0).node);
      ruleforge_ast_child_add(builder->compilation, entry, r_ast_expr(builder, rf_scope_assignment_rule_expr(assignment, 0)));
      ruleforge_ast_child_add(builder->compilation, node, entry);
    }

    return node;
  }

  if (rf_rule_body_item_count_rule_baseline_stmt(view) > 0) {
    RfBaselineStmt baseline;

    baseline = rf_rule_body_item_rule_baseline_stmt(view, 0);
    node     = ruleforge_ast_node_create(builder->compilation, RNK_BASELINE, r_ast_span(builder, baseline.node));

    for (i = 0; i < rf_baseline_stmt_count_rule_rule_command(baseline); i++) {
      ruleforge_ast_child_add(builder->compilation, node, r_ast_rule_command(builder, rf_baseline_stmt_rule_rule_command(baseline, i)));
    }

    return node;
  }

  {
    RfVariantsStmt variants;

    variants = rf_rule_body_item_rule_variants_stmt(view, 0);
    node     = ruleforge_ast_node_create(builder->compilation, RNK_VARIANTS, r_ast_span(builder, variants.node));

    for (i = 0; i < rf_variants_stmt_count_rule_when_stmt(variants); i++) {
      RfWhenStmt when;

      when  = rf_variants_stmt_rule_when_stmt(variants, i);
      entry = ruleforge_ast_node_create(builder->compilation, RNK_WHEN, r_ast_span(builder, when.node));
      ruleforge_ast_child_add(builder->compilation, entry, r_ast_expr(builder, rf_when_stmt_rule_expr(when, 0)));

      for (j = 0; j < rf_when_stmt_count_rule_rule_command(when); j++) {
        ruleforge_ast_child_add(builder->compilation, entry, r_ast_rule_command(builder, rf_when_stmt_rule_rule_command(when, j)));
      }

      ruleforge_ast_child_add(builder->compilation, node, entry);
    }
  }

  return node;
}

static RuleforgeNode *r_ast_rule(RAstBuilder *builder, RfRuleStmt view) {
  RuleforgeNode *node;
  uint32         i;

  node = ruleforge_ast_node_create(builder->compilation, RNK_RULE, r_ast_span(builder, view.node));

  if (rf_rule_stmt_count_token_identifier(view) > 0) {
    node->text = r_ast_token_text(builder, rf_rule_stmt_token_identifier(view, 0));
  }

  if (rf_rule_stmt_count_rule_call(view) > 0) {
    ruleforge_ast_child_add(builder->compilation, node, r_ast_call(builder, rf_rule_stmt_rule_call(view, 0)));
  }

  for (i = 0; i < rf_rule_stmt_count_rule_rule_body_item(view); i++) {
    ruleforge_ast_child_add(builder->compilation, node, r_ast_rule_item(builder, rf_rule_stmt_rule_rule_body_item(view, i)));
  }

  return node;
}

/* ---------------------------------------------------------------------------- */
/* STATEMENTS                                                                   */
/* ---------------------------------------------------------------------------- */

static RuleforgeNode *r_ast_if(RAstBuilder *builder, RfIfStmt view) {
  RuleforgeNode  *node;
  RuleforgeNode  *then_block;
  RuleforgeNode  *else_block;
  uint32          else_start;
  uint32          i;
  RfGeneratorStmt statement;

  node       = ruleforge_ast_node_create(builder->compilation, RNK_IF, r_ast_span(builder, view.node));
  then_block = ruleforge_ast_node_create(builder->compilation, RNK_BLOCK, r_ast_span(builder, view.node));
  else_block = NULL;
  else_start = rf_if_stmt_count_token_kw_else(view) > 0 ? r_ast_start(builder, rf_if_stmt_token_kw_else(view, 0)) : 0xFFFFFFFFU;

  ruleforge_ast_child_add(builder->compilation, node, r_ast_expr(builder, rf_if_stmt_rule_expr(view, 0)));

  /* The views list the statements of both branches together; the `else` token tells them apart by position. */
  for (i = 0; i < rf_if_stmt_count_rule_generator_stmt(view); i++) {
    statement = rf_if_stmt_rule_generator_stmt(view, i);

    if (r_ast_start(builder, statement.node) < else_start) {
      ruleforge_ast_child_add(builder->compilation, then_block, r_ast_generator(builder, statement));
    } else {
      if (else_block == NULL) {
        else_block = ruleforge_ast_node_create(builder->compilation, RNK_BLOCK, r_ast_span(builder, view.node));
      }

      ruleforge_ast_child_add(builder->compilation, else_block, r_ast_generator(builder, statement));
    }
  }

  ruleforge_ast_child_add(builder->compilation, node, then_block);

  if (rf_if_stmt_count_rule_if_stmt(view) > 0) {
    ruleforge_ast_child_add(builder->compilation, node, r_ast_if(builder, rf_if_stmt_rule_if_stmt(view, 0)));
  } else if (rf_if_stmt_count_token_kw_else(view) > 0) {
    if (else_block == NULL) {
      else_block = ruleforge_ast_node_create(builder->compilation, RNK_BLOCK, r_ast_span(builder, view.node));
    }

    ruleforge_ast_child_add(builder->compilation, node, else_block);
  }

  return node;
}

static RuleforgeNode *r_ast_generator(RAstBuilder *builder, RfGeneratorStmt view) {
  RuleforgeNode *node;
  RuleforgeNode *entry;
  uint32         i;

  if (rf_generator_stmt_count_rule_variable_stmt(view) > 0) {
    RfVariableStmt variable;

    variable    = rf_generator_stmt_rule_variable_stmt(view, 0);
    node        = ruleforge_ast_node_create(builder->compilation, RNK_LET, r_ast_span(builder, variable.node));
    node->text  = r_ast_token_text(builder, rf_variable_stmt_token_identifier(variable, 0));
    node->flags = rf_variable_stmt_count_token_kw_var(variable) > 0 ? RNF_MUTABLE : 0;

    if (rf_variable_stmt_count_rule_type(variable) > 0) {
      ruleforge_ast_child_add(builder->compilation, node, r_ast_type(builder, rf_variable_stmt_rule_type(variable, 0)));
    }

    ruleforge_ast_child_add(builder->compilation, node, r_ast_expr(builder, rf_variable_stmt_rule_expr(variable, 0)));

    return node;
  }

  if (rf_generator_stmt_count_rule_assignment_stmt(view) > 0) {
    RfAssignmentStmt assignment;

    assignment = rf_generator_stmt_rule_assignment_stmt(view, 0);
    node       = ruleforge_ast_node_create(builder->compilation, RNK_ASSIGN, r_ast_span(builder, assignment.node));
    node->text = r_ast_token_text(builder, rf_assignment_stmt_token_identifier(assignment, 0));
    ruleforge_ast_child_add(builder->compilation, node, r_ast_expr(builder, rf_assignment_stmt_rule_expr(assignment, 0)));

    return node;
  }

  if (rf_generator_stmt_count_rule_if_stmt(view) > 0) {
    return r_ast_if(builder, rf_generator_stmt_rule_if_stmt(view, 0));
  }

  if (rf_generator_stmt_count_rule_for_stmt(view) > 0) {
    RfForStmt      loop;
    RuleforgeNode *body;

    loop       = rf_generator_stmt_rule_for_stmt(view, 0);
    node       = ruleforge_ast_node_create(builder->compilation, RNK_FOR, r_ast_span(builder, loop.node));
    node->text = r_ast_token_text(builder, rf_for_stmt_token_identifier(loop, 0));
    ruleforge_ast_child_add(builder->compilation, node, r_ast_expr(builder, rf_for_stmt_rule_expr(loop, 0)));

    if (rf_for_stmt_count_token_kw_where(loop) > 0) {
      entry = ruleforge_ast_node_create(builder->compilation, RNK_WHERE, r_ast_span(builder, rf_for_stmt_rule_expr(loop, 1).node));
      ruleforge_ast_child_add(builder->compilation, entry, r_ast_expr(builder, rf_for_stmt_rule_expr(loop, 1)));
      ruleforge_ast_child_add(builder->compilation, node, entry);
    }

    body = ruleforge_ast_node_create(builder->compilation, RNK_BLOCK, r_ast_span(builder, loop.node));

    for (i = 0; i < rf_for_stmt_count_rule_generator_stmt(loop); i++) {
      ruleforge_ast_child_add(builder->compilation, body, r_ast_generator(builder, rf_for_stmt_rule_generator_stmt(loop, i)));
    }

    ruleforge_ast_child_add(builder->compilation, node, body);

    return node;
  }

  if (rf_generator_stmt_count_rule_return_stmt(view) > 0) {
    RfReturnStmt result;

    result = rf_generator_stmt_rule_return_stmt(view, 0);
    node   = ruleforge_ast_node_create(builder->compilation, RNK_RETURN, r_ast_span(builder, result.node));

    if (rf_return_stmt_count_rule_expr(result) > 0) {
      ruleforge_ast_child_add(builder->compilation, node, r_ast_expr(builder, rf_return_stmt_rule_expr(result, 0)));
    }

    return node;
  }

  if (rf_generator_stmt_count_rule_include_stmt(view) > 0) {
    RfIncludeStmt   include;
    RfIncludeTarget target;

    include = rf_generator_stmt_rule_include_stmt(view, 0);
    target  = rf_include_stmt_rule_include_target(include, 0);
    node    = ruleforge_ast_node_create(builder->compilation, RNK_INCLUDE, r_ast_span(builder, include.node));

    if (rf_include_target_count_rule_call(target) > 0) {
      ruleforge_ast_child_add(builder->compilation, node, r_ast_call(builder, rf_include_target_rule_call(target, 0)));
    } else {
      ruleforge_ast_child_add(builder->compilation, node, r_ast_reference(builder, rf_include_target_rule_reference(target, 0)));
    }

    if (rf_include_stmt_count_rule_with_clause(include) > 0) {
      RfWithClause with;

      with  = rf_include_stmt_rule_with_clause(include, 0);
      entry = ruleforge_ast_node_create(builder->compilation, RNK_WITH, r_ast_span(builder, with.node));

      for (i = 0; i < rf_with_clause_count_rule_with_binding(with); i++) {
        RfWithBinding  binding;
        RuleforgeNode *binding_node;

        binding            = rf_with_clause_rule_with_binding(with, i);
        binding_node       = ruleforge_ast_node_create(builder->compilation, RNK_WITH_BINDING, r_ast_span(builder, binding.node));
        binding_node->text = rf_with_binding_count_token_identifier(binding) > 0
                                 ? r_ast_token_text(builder, rf_with_binding_token_identifier(binding, 0))
                             : rf_with_binding_count_token_kw_theme(binding) > 0 ? "theme"
                                                                                 : "adapter";
        ruleforge_ast_child_add(builder->compilation, binding_node, r_ast_expr(builder, rf_with_binding_rule_expr(binding, 0)));
        ruleforge_ast_child_add(builder->compilation, entry, binding_node);
      }

      ruleforge_ast_child_add(builder->compilation, node, entry);
    }

    return node;
  }

  if (rf_generator_stmt_count_rule_emit_rule_stmt(view) > 0) {
    RfEmitRuleStmt emit;

    emit = rf_generator_stmt_rule_emit_rule_stmt(view, 0);
    node = ruleforge_ast_node_create(builder->compilation, RNK_EMIT, r_ast_span(builder, emit.node));
    ruleforge_ast_child_add(builder->compilation, node, r_ast_rule(builder, rf_emit_rule_stmt_rule_rule_stmt(emit, 0)));

    return node;
  }

  if (rf_generator_stmt_count_rule_rule_stmt(view) > 0) {
    return r_ast_rule(builder, rf_generator_stmt_rule_rule_stmt(view, 0));
  }

  {
    RfNativeStmt native;

    native     = rf_generator_stmt_rule_native_stmt(view, 0);
    node       = ruleforge_ast_node_create(builder->compilation, RNK_NATIVE, r_ast_span(builder, native.node));
    node->text = r_ast_string_unescape(builder, rf_native_stmt_token_string_literal(native, 0));

    return node;
  }
}

/* ---------------------------------------------------------------------------- */
/* DECLARATIONS                                                                 */
/* ---------------------------------------------------------------------------- */

static RuleforgeNode *r_ast_block_of(RAstBuilder *builder, const LParserGreenNode *span_node, RfFunctionStmt function) {
  RuleforgeNode *body;
  uint32         i;

  body = ruleforge_ast_node_create(builder->compilation, RNK_BLOCK, r_ast_span(builder, span_node));

  for (i = 0; i < rf_function_stmt_count_rule_generator_stmt(function); i++) {
    ruleforge_ast_child_add(builder->compilation, body, r_ast_generator(builder, rf_function_stmt_rule_generator_stmt(function, i)));
  }

  return body;
}

static RuleforgeNode *r_ast_theme(RAstBuilder *builder, RfThemeStmt view) {
  RuleforgeNode *node;
  RuleforgeNode *entry;
  uint32         extends_start;
  uint32         implements_start;
  uint32         i;
  uint32         j;
  uint32         k;

  node        = ruleforge_ast_node_create(builder->compilation, RNK_THEME, r_ast_span(builder, view.node));
  node->text  = r_ast_token_text(builder, rf_theme_stmt_token_identifier(view, 0));
  node->flags = rf_theme_stmt_count_token_kw_export(view) > 0 ? RNF_EXPORT : 0;

  extends_start = rf_theme_stmt_count_token_kw_extends(view) > 0 ? r_ast_start(builder, rf_theme_stmt_token_kw_extends(view, 0)) : 0xFFFFFFFFU;
  implements_start =
      rf_theme_stmt_count_token_kw_implements(view) > 0 ? r_ast_start(builder, rf_theme_stmt_token_kw_implements(view, 0)) : 0xFFFFFFFFU;

  /* The views list the qualified names of `extends` and `implements` together: the keyword before each tells which is which. */
  for (i = 0; i < rf_theme_stmt_count_rule_qualified_name(view); i++) {
    RfQualifiedName name;
    uint32          start;
    boolean         is_extends;

    name  = rf_theme_stmt_rule_qualified_name(view, i);
    start = r_ast_start(builder, name.node);

    is_extends = extends_start != 0xFFFFFFFFU && start > extends_start &&
                 (implements_start == 0xFFFFFFFFU || implements_start > start || implements_start < extends_start);

    entry       = ruleforge_ast_node_create(builder->compilation, is_extends ? RNK_EXTENDS : RNK_IMPLEMENTS, r_ast_span(builder, name.node));
    entry->text = r_ast_joined(builder, name.node);
    ruleforge_ast_child_add(builder->compilation, node, entry);
  }

  for (i = 0; i < rf_theme_stmt_count_rule_theme_style_stmt(view); i++) {
    RfThemeStyleStmt style;

    style        = rf_theme_stmt_rule_theme_style_stmt(view, i);
    entry        = ruleforge_ast_node_create(builder->compilation, RNK_THEME_STYLE, r_ast_span(builder, style.node));
    entry->flags = rf_theme_style_stmt_count_token_kw_override(style) > 0 ? RNF_OVERRIDE : 0;
    entry->text  = r_ast_token_text(builder, rf_theme_style_stmt_token_identifier(style, 0));

    for (j = 0; j < rf_theme_style_stmt_count_rule_theme_state_stmt(style); j++) {
      RfThemeStateStmt state;
      RuleforgeNode   *state_node;

      state            = rf_theme_style_stmt_rule_theme_state_stmt(style, j);
      state_node       = ruleforge_ast_node_create(builder->compilation, RNK_THEME_STATE, r_ast_span(builder, state.node));
      state_node->text = rf_theme_state_stmt_count_token_kw_on_show(state) > 0 ? "show" : "hide";

      for (k = 0; k < rf_theme_state_stmt_count_rule_style_property_stmt(state); k++) {
        RfStylePropertyStmt property;
        RuleforgeNode      *property_node;
        uint32              m;

        property            = rf_theme_state_stmt_rule_style_property_stmt(state, k);
        property_node       = ruleforge_ast_node_create(builder->compilation, RNK_STYLE_PROPERTY, r_ast_span(builder, property.node));
        property_node->text = r_ast_token_text(builder, rf_style_property_stmt_token_identifier(property, 0));

        for (m = 0; m < rf_style_property_stmt_count_rule_expr(property); m++) {
          ruleforge_ast_child_add(builder->compilation, property_node, r_ast_expr(builder, rf_style_property_stmt_rule_expr(property, m)));
        }

        if (rf_style_property_stmt_count_rule_color_override(property) > 0) {
          RuleforgeNode  *alpha;
          RfColorOverride override;

          override       = rf_style_property_stmt_rule_color_override(property, 0);
          alpha          = ruleforge_ast_node_create(builder->compilation, RNK_ALPHA_OVERRIDE, r_ast_span(builder, override.node));
          alpha->integer = (int64)r_ast_number_read(builder, rf_color_override_token_color_alpha(override, 0), 1);
          ruleforge_ast_child_add(builder->compilation, property_node, alpha);
        }

        ruleforge_ast_child_add(builder->compilation, state_node, property_node);
      }

      ruleforge_ast_child_add(builder->compilation, entry, state_node);
    }

    ruleforge_ast_child_add(builder->compilation, node, entry);
  }

  return node;
}

static RuleforgeNode *r_ast_declaration(RAstBuilder *builder, RfDeclaration view) {
  RuleforgeNode *node;
  RuleforgeNode *entry;
  uint32         i;

  if (rf_declaration_count_rule_schema_stmt(view) > 0) {
    RfSchemaStmt schema;

    schema      = rf_declaration_rule_schema_stmt(view, 0);
    node        = ruleforge_ast_node_create(builder->compilation, RNK_SCHEMA, r_ast_span(builder, schema.node));
    node->text  = r_ast_token_text(builder, rf_schema_stmt_token_identifier(schema, 0));
    node->flags = rf_schema_stmt_count_token_kw_export(schema) > 0 ? RNF_EXPORT : 0;

    for (i = 0; i < rf_schema_stmt_count_rule_schema_field(schema); i++) {
      RfSchemaField field;

      field       = rf_schema_stmt_rule_schema_field(schema, i);
      entry       = ruleforge_ast_node_create(builder->compilation, RNK_FIELD, r_ast_span(builder, field.node));
      entry->text = r_ast_token_text(builder, rf_schema_field_token_identifier(field, 0));
      ruleforge_ast_child_add(builder->compilation, entry, r_ast_type(builder, rf_schema_field_rule_type(field, 0)));
      ruleforge_ast_child_add(builder->compilation, node, entry);
    }

    return node;
  }

  if (rf_declaration_count_rule_dataset_stmt(view) > 0) {
    RfDatasetStmt dataset;

    dataset     = rf_declaration_rule_dataset_stmt(view, 0);
    node        = ruleforge_ast_node_create(builder->compilation, RNK_DATASET, r_ast_span(builder, dataset.node));
    node->text  = r_ast_token_text(builder, rf_dataset_stmt_token_identifier(dataset, 0));
    node->flags = rf_dataset_stmt_count_token_kw_export(dataset) > 0 ? RNF_EXPORT : 0;

    entry = ruleforge_ast_node_create(builder->compilation, RNK_TYPE, r_ast_span(builder, rf_dataset_stmt_rule_qualified_name(dataset, 0).node));
    entry->text = r_ast_joined(builder, rf_dataset_stmt_rule_qualified_name(dataset, 0).node);
    ruleforge_ast_child_add(builder->compilation, node, entry);

    entry       = ruleforge_ast_node_create(builder->compilation, RNK_STRING, r_ast_span(builder, rf_dataset_stmt_token_string_literal(dataset, 0)));
    entry->text = r_ast_string_unescape(builder, rf_dataset_stmt_token_string_literal(dataset, 0));
    ruleforge_ast_child_add(builder->compilation, node, entry);

    return node;
  }

  if (rf_declaration_count_rule_theme_schema_stmt(view) > 0) {
    RfThemeSchemaStmt schema;

    schema      = rf_declaration_rule_theme_schema_stmt(view, 0);
    node        = ruleforge_ast_node_create(builder->compilation, RNK_THEME_SCHEMA, r_ast_span(builder, schema.node));
    node->text  = r_ast_token_text(builder, rf_theme_schema_stmt_token_identifier(schema, 0));
    node->flags = rf_theme_schema_stmt_count_token_kw_export(schema) > 0 ? RNF_EXPORT : 0;

    for (i = 0; i < rf_theme_schema_stmt_count_rule_theme_schema_key(schema); i++) {
      RfThemeSchemaKey key;

      key         = rf_theme_schema_stmt_rule_theme_schema_key(schema, i);
      entry       = ruleforge_ast_node_create(builder->compilation, RNK_KEY, r_ast_span(builder, key.node));
      entry->text = r_ast_token_text(builder, rf_theme_schema_key_token_identifier(key, 0));
      ruleforge_ast_child_add(builder->compilation, node, entry);
    }

    return node;
  }

  if (rf_declaration_count_rule_color_set_stmt(view) > 0) {
    RfColorSetStmt set;

    set         = rf_declaration_rule_color_set_stmt(view, 0);
    node        = ruleforge_ast_node_create(builder->compilation, RNK_COLOR_SET, r_ast_span(builder, set.node));
    node->text  = r_ast_token_text(builder, rf_color_set_stmt_token_identifier(set, 0));
    node->flags = rf_color_set_stmt_count_token_kw_export(set) > 0 ? RNF_EXPORT : 0;

    for (i = 0; i < rf_color_set_stmt_count_rule_color_definition(set); i++) {
      RfColorDefinition definition;

      definition  = rf_color_set_stmt_rule_color_definition(set, i);
      entry       = ruleforge_ast_node_create(builder->compilation, RNK_COLOR_DEF, r_ast_span(builder, definition.node));
      entry->text = r_ast_token_text(builder, rf_color_definition_token_identifier(definition, 0));
      ruleforge_ast_child_add(builder->compilation, entry, r_ast_expr(builder, rf_color_definition_rule_expr(definition, 0)));
      ruleforge_ast_child_add(builder->compilation, node, entry);
    }

    return node;
  }

  if (rf_declaration_count_rule_theme_stmt(view) > 0) {
    return r_ast_theme(builder, rf_declaration_rule_theme_stmt(view, 0));
  }

  if (rf_declaration_count_rule_template_stmt(view) > 0) {
    RfTemplateStmt template_stmt;

    template_stmt = rf_declaration_rule_template_stmt(view, 0);
    node          = ruleforge_ast_node_create(builder->compilation, RNK_TEMPLATE, r_ast_span(builder, template_stmt.node));
    node->text    = r_ast_token_text(builder, rf_template_stmt_token_identifier(template_stmt, 0));
    node->flags   = rf_template_stmt_count_token_kw_export(template_stmt) > 0 ? RNF_EXPORT : 0;
    ruleforge_ast_child_add(builder->compilation, node, r_ast_params(builder, rf_template_stmt_rule_parameters(template_stmt, 0)));

    for (i = 0; i < rf_template_stmt_count_rule_rule_body_item(template_stmt); i++) {
      ruleforge_ast_child_add(builder->compilation, node, r_ast_rule_item(builder, rf_template_stmt_rule_rule_body_item(template_stmt, i)));
    }

    return node;
  }

  if (rf_declaration_count_rule_function_stmt(view) > 0) {
    RfFunctionStmt function;

    function    = rf_declaration_rule_function_stmt(view, 0);
    node        = ruleforge_ast_node_create(builder->compilation, RNK_FUNCTION, r_ast_span(builder, function.node));
    node->text  = r_ast_token_text(builder, rf_function_stmt_token_identifier(function, 0));
    node->flags = rf_function_stmt_count_token_kw_export(function) > 0 ? RNF_EXPORT : 0;
    ruleforge_ast_child_add(builder->compilation, node, r_ast_params(builder, rf_function_stmt_rule_parameters(function, 0)));

    if (rf_function_stmt_count_rule_type(function) > 0) {
      ruleforge_ast_child_add(builder->compilation, node, r_ast_type(builder, rf_function_stmt_rule_type(function, 0)));
    }

    ruleforge_ast_child_add(builder->compilation, node, r_ast_block_of(builder, function.node, function));

    return node;
  }

  if (rf_declaration_count_rule_ruleset_stmt(view) > 0) {
    RfRulesetStmt ruleset;

    ruleset     = rf_declaration_rule_ruleset_stmt(view, 0);
    node        = ruleforge_ast_node_create(builder->compilation, RNK_RULESET, r_ast_span(builder, ruleset.node));
    node->text  = r_ast_token_text(builder, rf_ruleset_stmt_token_identifier(ruleset, 0));
    node->flags = rf_ruleset_stmt_count_token_kw_export(ruleset) > 0 ? RNF_EXPORT : 0;

    for (i = 0; i < rf_ruleset_stmt_count_rule_ruleset_item(ruleset); i++) {
      RfRulesetItem item;

      item = rf_ruleset_stmt_rule_ruleset_item(ruleset, i);

      if (rf_ruleset_item_count_rule_description_stmt(item) > 0) {
        RfDescriptionStmt description;

        description = rf_ruleset_item_rule_description_stmt(item, 0);
        entry       = ruleforge_ast_node_create(builder->compilation, RNK_DESCRIPTION, r_ast_span(builder, description.node));
        entry->text = r_ast_string_unescape(builder, rf_description_stmt_token_string_literal(description, 0));
        ruleforge_ast_child_add(builder->compilation, node, entry);
      } else if (rf_ruleset_item_count_rule_core_conditions_stmt(item) > 0) {
        RfCoreConditionsStmt conditions;
        uint32               j;

        conditions = rf_ruleset_item_rule_core_conditions_stmt(item, 0);
        entry      = ruleforge_ast_node_create(builder->compilation, RNK_CORE_CONDITIONS, r_ast_span(builder, conditions.node));

        for (j = 0; j < rf_core_conditions_stmt_count_rule_expr(conditions); j++) {
          ruleforge_ast_child_add(builder->compilation, entry, r_ast_expr(builder, rf_core_conditions_stmt_rule_expr(conditions, j)));
        }

        ruleforge_ast_child_add(builder->compilation, node, entry);
      } else {
        ruleforge_ast_child_add(builder->compilation, node, r_ast_generator(builder, rf_ruleset_item_rule_generator_stmt(item, 0)));
      }
    }

    return node;
  }

  {
    RfConfigurationStmt configuration;

    configuration = rf_declaration_rule_configuration_stmt(view, 0);
    node          = ruleforge_ast_node_create(builder->compilation, RNK_CONFIGURATION, r_ast_span(builder, configuration.node));

    for (i = 0; i < rf_configuration_stmt_count_rule_configuration_item(configuration); i++) {
      RfConfigurationItem item;

      item = rf_configuration_stmt_rule_configuration_item(configuration, i);

      if (rf_configuration_item_count_rule_configuration_assignment(item) > 0) {
        RfConfigurationAssignment assignment;

        assignment  = rf_configuration_item_rule_configuration_assignment(item, 0);
        entry       = ruleforge_ast_node_create(builder->compilation, RNK_CONFIG_ASSIGN, r_ast_span(builder, assignment.node));
        entry->text = r_ast_joined(builder, rf_configuration_assignment_rule_configuration_key(assignment, 0).node);
        ruleforge_ast_child_add(builder->compilation, entry, r_ast_expr(builder, rf_configuration_assignment_rule_expr(assignment, 0)));
        ruleforge_ast_child_add(builder->compilation, node, entry);
      } else {
        RfAdapterStmt  adapter;
        RuleforgeNode *target;
        uint32         j;

        adapter     = rf_configuration_item_rule_adapter_stmt(item, 0);
        entry       = ruleforge_ast_node_create(builder->compilation, RNK_ADAPTER, r_ast_span(builder, adapter.node));
        entry->text = r_ast_token_text(builder, rf_adapter_stmt_token_identifier(adapter, 0));

        target = ruleforge_ast_node_create(builder->compilation, RNK_TYPE, r_ast_span(builder, rf_adapter_stmt_rule_qualified_name(adapter, 0).node));
        target->text = r_ast_joined(builder, rf_adapter_stmt_rule_qualified_name(adapter, 0).node);
        ruleforge_ast_child_add(builder->compilation, entry, target);

        for (j = 0; j < rf_adapter_stmt_count_rule_adapter_mapping(adapter); j++) {
          RfAdapterMapping mapping;
          RuleforgeNode   *mapping_node;

          mapping             = rf_adapter_stmt_rule_adapter_mapping(adapter, j);
          mapping_node        = ruleforge_ast_node_create(builder->compilation, RNK_ADAPTER_MAP, r_ast_span(builder, mapping.node));
          mapping_node->text  = r_ast_token_text(builder, rf_adapter_mapping_token_identifier(mapping, 0));
          mapping_node->text2 = r_ast_token_text(builder, rf_adapter_mapping_token_identifier(mapping, 1));
          ruleforge_ast_child_add(builder->compilation, entry, mapping_node);
        }

        ruleforge_ast_child_add(builder->compilation, node, entry);
      }
    }
  }

  return node;
}

/* ---------------------------------------------------------------------------- */
/* MODULE                                                                       */
/* ---------------------------------------------------------------------------- */

RuleforgeNode *ruleforge_ast_build(RuleforgeCompilation *compilation, uint32 file, const LParserTree *tree) {
  RAstBuilder    builder;
  RuleforgeNode *module;
  RuleforgeNode *node;
  RfScript       script;
  uint_large     size;
  uint32         i;

  NEXUS_PROGRAM_PRECONDITION(compilation != NULL && tree != NULL, "ruleforge_ast_build: NULL argument");

  builder.compilation = compilation;
  builder.file        = file;
  builder.base        = logos_language_tooling_parsing_syntax_tree_source_get(tree, &size);
  builder.tree        = tree;

  script = rf_root(tree);

  module = ruleforge_ast_node_create(compilation, RNK_MODULE, r_ast_span(&builder, script.node));

  if (rf_script_count_rule_module_stmt(script) > 0) {
    module->text = r_ast_joined(&builder, rf_module_stmt_rule_qualified_name(rf_script_rule_module_stmt(script, 0), 0).node);
  }

  if (rf_script_count_rule_version_stmt(script) > 0) {
    RfVersionStmt version;

    version    = rf_script_rule_version_stmt(script, 0);
    node       = ruleforge_ast_node_create(compilation, RNK_VERSION, r_ast_span(&builder, version.node));
    node->text = r_ast_token_text(&builder, rf_version_stmt_token_version(version, 0));
    ruleforge_ast_child_add(compilation, module, node);
  }

  for (i = 0; i < rf_script_count_rule_import_stmt(script); i++) {
    RfImportStmt import;

    import     = rf_script_rule_import_stmt(script, i);
    node       = ruleforge_ast_node_create(compilation, RNK_IMPORT, r_ast_span(&builder, import.node));
    node->text = r_ast_joined(&builder, rf_import_stmt_rule_qualified_name(import, 0).node);
    ruleforge_ast_child_add(compilation, module, node);
  }

  for (i = 0; i < rf_script_count_rule_declaration(script); i++) {
    ruleforge_ast_child_add(compilation, module, r_ast_declaration(&builder, rf_script_rule_declaration(script, i)));
  }

  return module;
}
