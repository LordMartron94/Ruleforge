#include <nexus.h>

#include "../ruleforge.h"
#include "./r_internal.h"

/*
The target-game manifest: the style properties of Path of Exile 1 filters and what the engine can express. The conditions are not here:
they are declared in Ruleforge (std.conditions, or a module of the project), and the manifest lists them from there. Scripts are
validated against these tables, so a malformed style argument is an error at its source location instead of a filter line the game
ignores.
*/

/* ---------------------------------------------------------------------------- */
/* OPERATORS                                                                   */
/* ---------------------------------------------------------------------------- */

const char *ruleforge_manifest_operator_text_get(RuleforgeOperator operation) {
  switch (operation) {
  case ROP_EQ:
    return "==";
  case ROP_NE:
    return "!=";
  case ROP_LT:
    return "<";
  case ROP_LE:
    return "<=";
  case ROP_GT:
    return ">";
  case ROP_GE:
    return ">=";
  }

  return "==";
}

/* ---------------------------------------------------------------------------- */
/* STYLE PROPERTIES                                                             */
/* ---------------------------------------------------------------------------- */

static const char *const r_manifest_colors[] = {"Red", "Green", "Blue", "Brown", "White", "Yellow", "Cyan", "Grey", "Orange", "Pink", "Purple"};
static const char *const r_manifest_shapes[] = {"Circle", "Diamond", "Hexagon",  "Square", "Star",     "Triangle",
                                                "Cross",  "Moon",    "Raindrop", "Kite",   "Pentagon", "UpsideDownHouse"};

static boolean r_manifest_one_of(const char *text, const char *const *names, uint32 count) {
  uint32 i;

  for (i = 0; i < count; i++) {
    if (nexus_strings_string_equals(names[i], text)) {
      return TRUE;
    }
  }

  return FALSE;
}

/* Summary: a color argument as "R G B" or "R G B A" (alpha written only when it is not opaque). */
static boolean r_manifest_color_text(RuleforgeCompilation *compilation, RuleforgeSpan span, const char *property, const RuleforgeValue *value,
                                     int32 alpha_override, char **out_text) {
  char   buffer[64];
  uint32 alpha;

  if (value->kind != RVK_COLOR) {
    ruleforge_diagnostic_report(compilation, RDS_ERROR, "RF0420", span, "write a color such as #FFD700, r255 g215 b0 or hsl(h51 s100 l50)",
                                "style property `%s` needs a color", property);

    return FALSE;
  }

  alpha = value->as.color & 0xFFU;

  if (alpha_override >= 0) {
    alpha = (uint32)alpha_override;
  }

  if (alpha == 255) {
    (void)stbsp_snprintf(buffer, (int)NEXUS_SIZEOF(buffer), "%lu %lu %lu", (unsigned long)((value->as.color >> 24) & 0xFFU),
                         (unsigned long)((value->as.color >> 16) & 0xFFU), (unsigned long)((value->as.color >> 8) & 0xFFU));
  } else {
    (void)stbsp_snprintf(buffer, (int)NEXUS_SIZEOF(buffer), "%lu %lu %lu %lu", (unsigned long)((value->as.color >> 24) & 0xFFU),
                         (unsigned long)((value->as.color >> 16) & 0xFFU), (unsigned long)((value->as.color >> 8) & 0xFFU), (unsigned long)alpha);
  }

  *out_text = nexus_data_arena_text_copy(&compilation->arena, buffer, nexus_strings_string_length_get(buffer));

  return TRUE;
}

boolean ruleforge_manifest_property_lower(RuleforgeCompilation *compilation, RuleforgeSpan span, const char *name, const RuleforgeValue *arguments,
                                          uint32 count, int32 alpha_override, RuleforgeStyleProperty *out_property) {
  char   *text;
  char    buffer[200];
  boolean is_color_property;

  NEXUS_PROGRAM_PRECONDITION(compilation != NULL && name != NULL && out_property != NULL, "ruleforge_manifest_property_lower: NULL argument");

  out_property->name    = nexus_data_arena_text_copy(&compilation->arena, name, nexus_strings_string_length_get(name));
  out_property->keyword = NULL;
  out_property->text    = NULL;
  text                  = NULL;

  is_color_property =
      nexus_strings_string_equals(name, "Background") || nexus_strings_string_equals(name, "Border") || nexus_strings_string_equals(name, "Text");

  if (is_color_property) {
    if (count != 1) {
      ruleforge_diagnostic_report(compilation, RDS_ERROR, "RF0421", span, NULL, "style property `%s` takes exactly one color", name);

      return FALSE;
    }

    if (!r_manifest_color_text(compilation, span, name, &arguments[0], alpha_override, &text)) {
      return FALSE;
    }

    out_property->keyword = "SetTextColor";

    if (nexus_strings_string_equals(name, "Background")) {
      out_property->keyword = "SetBackgroundColor";
    } else if (nexus_strings_string_equals(name, "Border")) {
      out_property->keyword = "SetBorderColor";
    }

    out_property->text = text;

    return TRUE;
  }

  if (alpha_override >= 0) {
    ruleforge_diagnostic_report(compilation, RDS_ERROR, "RF0422", span, "only Background, Border and Text colors take an alpha override",
                                "`override a%ld` does not apply to `%s`", (long)alpha_override, name);

    return FALSE;
  }

  if (nexus_strings_string_equals(name, "FontSize")) {
    if (count != 1 || arguments[0].kind != RVK_INT || arguments[0].as.integer < 1 || arguments[0].as.integer > 65) {
      ruleforge_diagnostic_report(compilation, RDS_ERROR, "RF0423", span, NULL, "`FontSize` takes one integer from 1 to 65");

      return FALSE;
    }

    (void)stbsp_snprintf(buffer, (int)NEXUS_SIZEOF(buffer), "%ld", (long)arguments[0].as.integer);
    out_property->keyword = "SetFontSize";
  } else if (nexus_strings_string_equals(name, "Beam")) {
    if ((count != 1 && count != 2) || arguments[0].kind != RVK_STRING || !r_manifest_one_of(arguments[0].as.string, r_manifest_colors, 11) ||
        (count == 2 && arguments[1].kind != RVK_BOOL)) {
      ruleforge_diagnostic_report(compilation, RDS_ERROR, "RF0424", span,
                                  "colors: Red, Green, Blue, Brown, White, Yellow, Cyan, Grey, Orange, Pink, Purple",
                                  "`Beam` takes a color name and an optional temporary flag");

      return FALSE;
    }

    (void)stbsp_snprintf(buffer, (int)NEXUS_SIZEOF(buffer), "%s%s", arguments[0].as.string, count == 2 && arguments[1].as.flag ? " Temp" : "");
    out_property->keyword = "PlayEffect";
  } else if (nexus_strings_string_equals(name, "MinimapIcon")) {
    if (count != 3 || arguments[0].kind != RVK_INT || arguments[0].as.integer < 0 || arguments[0].as.integer > 2 || arguments[1].kind != RVK_STRING ||
        !r_manifest_one_of(arguments[1].as.string, r_manifest_colors, 11) || arguments[2].kind != RVK_STRING ||
        !r_manifest_one_of(arguments[2].as.string, r_manifest_shapes, 12)) {
      ruleforge_diagnostic_report(compilation, RDS_ERROR, "RF0425", span, "MinimapIcon <size 0..2>, <Color>, <Shape>",
                                  "`MinimapIcon` takes a size, a color name and a shape name");

      return FALSE;
    }

    (void)stbsp_snprintf(buffer, (int)NEXUS_SIZEOF(buffer), "%ld %s %s", (long)arguments[0].as.integer, arguments[1].as.string,
                         arguments[2].as.string);
    out_property->keyword = "MinimapIcon";
  } else if (nexus_strings_string_equals(name, "Sound") || nexus_strings_string_equals(name, "PositionalSound")) {
    if ((count != 1 && count != 2) || arguments[0].kind != RVK_INT || arguments[0].as.integer < 1 || arguments[0].as.integer > 16 ||
        (count == 2 && (arguments[1].kind != RVK_INT || arguments[1].as.integer < 0 || arguments[1].as.integer > 300))) {
      ruleforge_diagnostic_report(compilation, RDS_ERROR, "RF0426", span, NULL,
                                  "`%s` takes a sound id from 1 to 16 and an optional volume from 0 to 300", name);

      return FALSE;
    }

    if (count == 2) {
      (void)stbsp_snprintf(buffer, (int)NEXUS_SIZEOF(buffer), "%ld %ld", (long)arguments[0].as.integer, (long)arguments[1].as.integer);
    } else {
      (void)stbsp_snprintf(buffer, (int)NEXUS_SIZEOF(buffer), "%ld", (long)arguments[0].as.integer);
    }

    out_property->keyword = nexus_strings_string_equals(name, "Sound") ? "PlayAlertSound" : "PlayAlertSoundPositional";
  } else if (nexus_strings_string_equals(name, "CustomSound")) {
    if ((count != 1 && count != 2) || arguments[0].kind != RVK_STRING || arguments[0].is_identifier ||
        (count == 2 && (arguments[1].kind != RVK_INT || arguments[1].as.integer < 0 || arguments[1].as.integer > 300))) {
      ruleforge_diagnostic_report(compilation, RDS_ERROR, "RF0427", span, NULL, "`CustomSound` takes a file name string and an optional volume");

      return FALSE;
    }

    if (count == 2) {
      (void)stbsp_snprintf(buffer, (int)NEXUS_SIZEOF(buffer), "\"%s\" %ld", arguments[0].as.string, (long)arguments[1].as.integer);
    } else {
      (void)stbsp_snprintf(buffer, (int)NEXUS_SIZEOF(buffer), "\"%s\"", arguments[0].as.string);
    }

    out_property->keyword = "CustomAlertSound";
  } else if (nexus_strings_string_equals(name, "DisableSound")) {
    if (count != 1 || arguments[0].kind != RVK_BOOL) {
      ruleforge_diagnostic_report(compilation, RDS_ERROR, "RF0428", span, NULL, "`DisableSound` takes true or false");

      return FALSE;
    }

    (void)stbsp_snprintf(buffer, (int)NEXUS_SIZEOF(buffer), "%s", arguments[0].as.flag ? "True" : "False");
    out_property->keyword = "DisableDropSound";
  } else {
    ruleforge_diagnostic_report(
        compilation, RDS_ERROR, "RF0429", span,
        "known properties: Background, Border, Text, FontSize, Beam, MinimapIcon, Sound, PositionalSound, CustomSound, DisableSound",
        "unknown style property `%s`", name);

    return FALSE;
  }

  out_property->text = nexus_data_arena_text_copy(&compilation->arena, buffer, nexus_strings_string_length_get(buffer));

  return TRUE;
}

/* ---------------------------------------------------------------------------- */
/* MANIFEST AS DATA                                                             */
/* ---------------------------------------------------------------------------- */

typedef struct RManifestProperty {
  const char *name;
  const char *keyword;
  const char *signature;
  const char *summary;
} RManifestProperty;

static const RManifestProperty r_manifest_properties[] = {
    {"Background", "SetBackgroundColor", "color [override aN]", "Background color of the item label."},
    {"Border", "SetBorderColor", "color [override aN]", "Border color of the item label."},
    {"Text", "SetTextColor", "color [override aN]", "Text color of the item label."},
    {"FontSize", "SetFontSize", "int 1..65", "Font size of the item label."},
    {"Beam", "PlayEffect", "ColorName [, temporary bool]", "Light beam above the item."},
    {"MinimapIcon", "MinimapIcon", "size 0..2, ColorName, ShapeName", "Icon on the minimap."},
    {"Sound", "PlayAlertSound", "id 1..16 [, volume 0..300]", "Alert sound."},
    {"PositionalSound", "PlayAlertSoundPositional", "id 1..16 [, volume 0..300]", "Alert sound with stereo position."},
    {"CustomSound", "CustomAlertSound", "\"file\" [, volume 0..300]", "Custom alert sound file."},
    {"DisableSound", "DisableDropSound", "bool", "Silence the drop sound."},
};

static const char *r_manifest_condition_type_name(RuleforgeConditionType type) {
  switch (type) {
  case RCT_INTEGER:
    return "int";
  case RCT_STRING:
    return "string";
  case RCT_BOOLEAN:
    return "bool";
  default:
    break;
  }

  return "enum";
}

/* Summary: appends the declared conditions of every loaded module as JSON objects. */
static void r_manifest_conditions_write(RuleforgeCompilation *compilation, NexusStringBuilder *out) {
  uint_large m;
  uint32     i;
  boolean    first;

  first = TRUE;

  for (m = 0; m < nexus_data_list_length_get(compilation->modules); m++) {
    const RuleforgeModule *module;

    module = (const RuleforgeModule *)nexus_data_list_pointer_at(compilation->modules, m);

    for (i = 0; i < module->declaration_count; i++) {
      const RuleforgeDeclaration   *declaration;
      const RuleforgeConditionInfo *info;
      uint32                        value;

      declaration = &module->declarations[i];
      info        = declaration->condition;

      if (info == NULL || !declaration->exported) {
        continue;
      }

      (void)nexus_strings_string_builder_append_formatted(
          out, "%s{\"name\":\"%s\",\"module\":\"%s\",\"keyword\":\"%s\",\"type\":\"%s\",\"ordered\":%s,\"order\":%lld", first ? "" : ",", info->name,
          module->name, info->keyword == NULL ? "" : info->keyword, r_manifest_condition_type_name(info->type), info->ordered ? "true" : "false",
          (long long)info->order);

      if (info->type == RCT_ENUM) {
        (void)nexus_strings_string_builder_append(out, ",\"values\":[");

        for (value = 0; value < info->value_count; value++) {
          (void)nexus_strings_string_builder_append_formatted(out, "%s\"%s\"", value > 0 ? "," : "", info->values[value]);
        }

        (void)nexus_strings_string_builder_append(out, "]");
      }

      (void)nexus_strings_string_builder_append(out, ",\"documentation\":");
      (void)nexus_json_quote_append(out, ruleforge_compilation_documentation_get(compilation, module, declaration));
      (void)nexus_strings_string_builder_append(out, "}");
      first = FALSE;
    }
  }
}

/* Summary: appends the exported functions of the loaded standard-library modules: parameters, result, documentation, and whether native. */
static void r_manifest_functions_write(RuleforgeCompilation *compilation, NexusStringBuilder *out) {
  uint_large m;
  uint32     i;
  boolean    first;

  first = TRUE;

  for (m = 0; m < nexus_data_list_length_get(compilation->modules); m++) {
    const RuleforgeModule *module;

    module = (const RuleforgeModule *)nexus_data_list_pointer_at(compilation->modules, m);

    if (!ruleforge_source_get(compilation, module->file)->embedded) {
      continue;
    }

    for (i = 0; i < module->declaration_count; i++) {
      const RuleforgeDeclaration *declaration;
      RfFunctionStmt              function;
      RfParameters                parameters;
      uint32                      p;

      declaration = &module->declarations[i];

      if (!declaration->exported || rf_declaration_count_rule_function_stmt(declaration->view) == 0) {
        continue;
      }

      function   = rf_declaration_rule_function_stmt(declaration->view, 0);
      parameters = rf_function_stmt_rule_parameters(function, 0);
      (void)nexus_strings_string_builder_append_formatted(out, "%s{\"module\":\"%s\",\"name\":\"%s\",\"intrinsic\":%s,\"parameters\":[",
                                                          first ? "" : ",", module->name, declaration->name,
                                                          rf_function_stmt_count_token_kw_intrinsic(function) > 0 ? "true" : "false");

      for (p = 0; p < rf_parameters_count_rule_parameter(parameters); p++) {
        RfParameter parameter;

        parameter = rf_parameters_rule_parameter(parameters, p);
        (void)nexus_strings_string_builder_append_formatted(out, "%s{\"name\":", p > 0 ? "," : "");
        (void)nexus_json_quote_append(out, ruleforge_syntax_text_get(compilation, parameter.tree, rf_parameter_token_identifier(parameter, 0)));
        (void)nexus_strings_string_builder_append(out, ",\"type\":");
        (void)nexus_json_quote_append(out,
                                      ruleforge_syntax_source_get(compilation, module, parameter.tree, rf_parameter_rule_type(parameter, 0).node));
        (void)nexus_strings_string_builder_append(out, "}");
      }

      (void)nexus_strings_string_builder_append(out, "],\"returns\":");

      if (rf_function_stmt_count_rule_type(function) > 0) {
        (void)nexus_json_quote_append(out,
                                      ruleforge_syntax_source_get(compilation, module, function.tree, rf_function_stmt_rule_type(function, 0).node));
      } else {
        (void)nexus_strings_string_builder_append(out, "null");
      }

      (void)nexus_strings_string_builder_append(out, ",\"documentation\":");
      (void)nexus_json_quote_append(out, ruleforge_compilation_documentation_get(compilation, module, declaration));
      (void)nexus_strings_string_builder_append(out, "}");
      first = FALSE;
    }
  }
}

char *ruleforge_manifest_json_write(RuleforgeRuntime *runtime) {
  NexusStringBuilder    out;
  char                 *result;
  uint32                i;
  RuleforgeCompilation *compilation;

  NEXUS_PROGRAM_PRECONDITION(runtime != NULL, "ruleforge_manifest_json_write: NULL runtime");

  /* The conditions and the library are Ruleforge: load the embedded modules and read what they declare. */
  compilation = ruleforge_compilation_create(runtime);
  (void)ruleforge_compilation_library_load(compilation);

  out = nexus_strings_string_builder_create_growable(4096);
  (void)nexus_strings_string_builder_append(&out, "{\"format\":\"ruleforge-manifest\",\"version\":2,\"conditions\":[");
  r_manifest_conditions_write(compilation, &out);
  (void)nexus_strings_string_builder_append(&out, "],\"properties\":[");

  for (i = 0; i < (uint32)(NEXUS_SIZEOF(r_manifest_properties) / NEXUS_SIZEOF(r_manifest_properties[0])); i++) {
    if (i > 0) {
      (void)nexus_strings_string_builder_append(&out, ",");
    }

    (void)nexus_strings_string_builder_append_formatted(&out, "{\"name\":\"%s\",\"keyword\":\"%s\",\"signature\":", r_manifest_properties[i].name,
                                                        r_manifest_properties[i].keyword);
    (void)nexus_json_quote_append(&out, r_manifest_properties[i].signature);
    (void)nexus_strings_string_builder_append(&out, ",\"summary\":");
    (void)nexus_json_quote_append(&out, r_manifest_properties[i].summary);
    (void)nexus_strings_string_builder_append(&out, "}");
  }

  (void)nexus_strings_string_builder_append(&out, "],\"colors\":[");

  for (i = 0; i < 11; i++) {
    (void)nexus_strings_string_builder_append_formatted(&out, "%s\"%s\"", i > 0 ? "," : "", r_manifest_colors[i]);
  }

  (void)nexus_strings_string_builder_append(&out, "],\"shapes\":[");

  for (i = 0; i < 12; i++) {
    (void)nexus_strings_string_builder_append_formatted(&out, "%s\"%s\"", i > 0 ? "," : "", r_manifest_shapes[i]);
  }

  (void)nexus_strings_string_builder_append(&out, "],");
  (void)nexus_strings_string_builder_append(&out, "\"functions\":[");
  r_manifest_functions_write(compilation, &out);
  (void)nexus_strings_string_builder_append(&out, "]}");
  ruleforge_compilation_destroy(compilation);

  result = (char *)nexus_memory_allocate(nexus_strings_string_builder_length_get(&out) + 1);
  nexus_memory_bytes_copy(nexus_strings_string_builder_string_get(&out), result, nexus_strings_string_builder_length_get(&out) + 1);
  nexus_strings_string_builder_destroy(&out);

  return result;
}
