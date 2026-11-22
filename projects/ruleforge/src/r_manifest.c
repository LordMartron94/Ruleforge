#include <nexus.h>

#include "../ruleforge.h"
#include "./r_internal.h"

/*
The target-game manifest: the Path of Exile 1 filter conditions and style properties Ruleforge can express. Scripts are
validated against this table, so an unknown condition or a malformed style argument is an error at its source location
instead of a filter line the game ignores.
*/

/* ---------------------------------------------------------------------------- */
/* CONDITIONS                                                                   */
/* ---------------------------------------------------------------------------- */

#define R_MANIFEST_CONDITION(name, keyword, type, ordered) {name, keyword, type, ordered}

static const RuleforgeConditionInfo r_manifest_conditions[] = {
    R_MANIFEST_CONDITION("areaLevel", "AreaLevel", RCT_INTEGER, TRUE),
    R_MANIFEST_CONDITION("itemLevel", "ItemLevel", RCT_INTEGER, TRUE),
    R_MANIFEST_CONDITION("dropLevel", "DropLevel", RCT_INTEGER, TRUE),
    R_MANIFEST_CONDITION("quality", "Quality", RCT_INTEGER, TRUE),
    R_MANIFEST_CONDITION("stackSize", "StackSize", RCT_INTEGER, TRUE),
    R_MANIFEST_CONDITION("gemLevel", "GemLevel", RCT_INTEGER, TRUE),
    R_MANIFEST_CONDITION("mapTier", "MapTier", RCT_INTEGER, TRUE),
    R_MANIFEST_CONDITION("height", "Height", RCT_INTEGER, TRUE),
    R_MANIFEST_CONDITION("width", "Width", RCT_INTEGER, TRUE),
    R_MANIFEST_CONDITION("sockets", "Sockets", RCT_INTEGER, TRUE),
    R_MANIFEST_CONDITION("linkedSockets", "LinkedSockets", RCT_INTEGER, TRUE),
    R_MANIFEST_CONDITION("socketGroup", "SocketGroup", RCT_STRING, FALSE),
    R_MANIFEST_CONDITION("rarity", "Rarity", RCT_RARITY, TRUE),
    R_MANIFEST_CONDITION("class", "Class", RCT_STRING, FALSE),
    R_MANIFEST_CONDITION("baseType", "BaseType", RCT_STRING, FALSE),
    R_MANIFEST_CONDITION("hasExplicitMod", "HasExplicitMod", RCT_STRING, FALSE),
    R_MANIFEST_CONDITION("hasImplicitMod", "HasImplicitMod", RCT_STRING, FALSE),
    R_MANIFEST_CONDITION("hasEnchantment", "HasEnchantment", RCT_STRING, FALSE),
    R_MANIFEST_CONDITION("hasInfluence", "HasInfluence", RCT_STRING, FALSE),
    R_MANIFEST_CONDITION("enchantmentPassiveNode", "EnchantmentPassiveNode", RCT_STRING, FALSE),
    R_MANIFEST_CONDITION("enchantmentPassiveNum", "EnchantmentPassiveNum", RCT_INTEGER, TRUE),
    R_MANIFEST_CONDITION("corruptedMods", "CorruptedMods", RCT_INTEGER, TRUE),
    R_MANIFEST_CONDITION("gemQualityType", "GemQualityType", RCT_STRING, FALSE),
    R_MANIFEST_CONDITION("baseDefencePercentile", "BaseDefencePercentile", RCT_INTEGER, TRUE),
    R_MANIFEST_CONDITION("baseArmour", "BaseArmour", RCT_INTEGER, TRUE),
    R_MANIFEST_CONDITION("baseEvasion", "BaseEvasion", RCT_INTEGER, TRUE),
    R_MANIFEST_CONDITION("baseEnergyShield", "BaseEnergyShield", RCT_INTEGER, TRUE),
    R_MANIFEST_CONDITION("baseWard", "BaseWard", RCT_INTEGER, TRUE),
    R_MANIFEST_CONDITION("replica", "Replica", RCT_BOOLEAN, FALSE),
    R_MANIFEST_CONDITION("identified", "Identified", RCT_BOOLEAN, FALSE),
    R_MANIFEST_CONDITION("corrupted", "Corrupted", RCT_BOOLEAN, FALSE),
    R_MANIFEST_CONDITION("mirrored", "Mirrored", RCT_BOOLEAN, FALSE),
    R_MANIFEST_CONDITION("fractured", "FracturedItem", RCT_BOOLEAN, FALSE),
    R_MANIFEST_CONDITION("synthesised", "SynthesisedItem", RCT_BOOLEAN, FALSE),
    R_MANIFEST_CONDITION("shaperItem", "ShaperItem", RCT_BOOLEAN, FALSE),
    R_MANIFEST_CONDITION("elderItem", "ElderItem", RCT_BOOLEAN, FALSE),
    R_MANIFEST_CONDITION("shapedMap", "ShapedMap", RCT_BOOLEAN, FALSE),
    R_MANIFEST_CONDITION("elderMap", "ElderMap", RCT_BOOLEAN, FALSE),
    R_MANIFEST_CONDITION("blightedMap", "BlightedMap", RCT_BOOLEAN, FALSE),
    R_MANIFEST_CONDITION("uberBlightedMap", "UberBlightedMap", RCT_BOOLEAN, FALSE),
    R_MANIFEST_CONDITION("anyEnchantment", "AnyEnchantment", RCT_BOOLEAN, FALSE),
    R_MANIFEST_CONDITION("scourged", "ScourgedItem", RCT_BOOLEAN, FALSE),
    R_MANIFEST_CONDITION("zanaMemory", "ZanaMemory", RCT_BOOLEAN, FALSE),
    R_MANIFEST_CONDITION("transfigured", "TransfiguredGem", RCT_BOOLEAN, FALSE),
    R_MANIFEST_CONDITION("alternateQuality", "AlternateQuality", RCT_BOOLEAN, FALSE),
    R_MANIFEST_CONDITION("hasCruciblePassiveTree", "HasCruciblePassiveTree", RCT_BOOLEAN, FALSE),
};

const RuleforgeConditionInfo *ruleforge_manifest_condition_find(const char *name) {
  uint32 i;

  NEXUS_PROGRAM_PRECONDITION(name != NULL, "ruleforge_manifest_condition_find: NULL name");

  for (i = 0; i < (uint32)(NEXUS_SIZEOF(r_manifest_conditions) / NEXUS_SIZEOF(r_manifest_conditions[0])); i++) {
    if (nexus_strings_string_equals(r_manifest_conditions[i].name, name)) {
      return &r_manifest_conditions[i];
    }
  }

  return NULL;
}

uint32 ruleforge_manifest_condition_count_get(void) {
  return (uint32)(NEXUS_SIZEOF(r_manifest_conditions) / NEXUS_SIZEOF(r_manifest_conditions[0]));
}

const RuleforgeConditionInfo *ruleforge_manifest_condition_at(uint32 index) {
  NEXUS_PROGRAM_PRECONDITION(index < ruleforge_manifest_condition_count_get(), "ruleforge_manifest_condition_at: index out of range");

  return &r_manifest_conditions[index];
}

/* ---------------------------------------------------------------------------- */
/* RARITIES AND OPERATORS                                                       */
/* ---------------------------------------------------------------------------- */

static const char *const r_manifest_rarities[] = {"Normal", "Magic", "Rare", "Unique"};

int32 ruleforge_manifest_rarity_parse(const char *text) {
  int32 i;

  NEXUS_PROGRAM_PRECONDITION(text != NULL, "ruleforge_manifest_rarity_parse: NULL text");

  for (i = 0; i < 4; i++) {
    if (nexus_strings_string_equals(r_manifest_rarities[i], text)) {
      return i;
    }
  }

  return -1;
}

const char *ruleforge_manifest_rarity_name_get(int64 rarity) {
  NEXUS_PROGRAM_PRECONDITION(rarity >= 0 && rarity < 4, "ruleforge_manifest_rarity_name_get: rarity out of range");

  return r_manifest_rarities[rarity];
}

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

    out_property->keyword = nexus_strings_string_equals(name, "Background") ? "SetBackgroundColor"
                            : nexus_strings_string_equals(name, "Border")   ? "SetBorderColor"
                                                                            : "SetTextColor";
    out_property->text    = text;

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

char *ruleforge_manifest_json_write(void) {
  NexusStringBuilder out;
  char              *result;
  uint32             i;

  out = nexus_strings_string_builder_create_growable(4096);
  (void)nexus_strings_string_builder_append(&out, "{\"format\":\"ruleforge-manifest\",\"version\":1,\"conditions\":[");

  for (i = 0; i < ruleforge_manifest_condition_count_get(); i++) {
    const RuleforgeConditionInfo *info;

    info = ruleforge_manifest_condition_at(i);

    if (i > 0) {
      (void)nexus_strings_string_builder_append(&out, ",");
    }

    (void)nexus_strings_string_builder_append_formatted(&out, "{\"name\":\"%s\",\"keyword\":\"%s\",\"type\":\"%s\",\"ordered\":%s}", info->name,
                                                        info->keyword,
                                                        info->type == RCT_INTEGER   ? "int"
                                                        : info->type == RCT_STRING  ? "string"
                                                        : info->type == RCT_BOOLEAN ? "bool"
                                                                                    : "rarity",
                                                        info->ordered ? "true" : "false");
  }

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

  (void)nexus_strings_string_builder_append(&out, "],\"rarities\":[");

  for (i = 0; i < 4; i++) {
    (void)nexus_strings_string_builder_append_formatted(&out, "%s\"%s\"", i > 0 ? "," : "", r_manifest_rarities[i]);
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
  ruleforge_stdlib_json_write(&out);
  (void)nexus_strings_string_builder_append(&out, "}");

  result = (char *)nexus_memory_allocate(nexus_strings_string_builder_length_get(&out) + 1);
  nexus_memory_bytes_copy(nexus_strings_string_builder_string_get(&out), result, nexus_strings_string_builder_length_get(&out) + 1);
  nexus_strings_string_builder_destroy(&out);

  return result;
}
