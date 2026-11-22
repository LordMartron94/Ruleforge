#include <nexus.h>
#include <echo.h>

#include <logos.h>
#include <rf_bindings.h>

#include "../ruleforge.h"
#include "./r_internal.h"

/*
Colors. The grammar recognizes the forms of a color literal (#RRGGBB, r255 g128 b64, rgb(...), hsl(...), hsv(...)); what the numbers mean
is interpretation, so it is done here: red, green and blue are packed as 0xRRGGBBAA, and HSL and HSV are converted to RGB.
*/

static uint32 r_color_pack(f_real red, f_real green, f_real blue, uint32 alpha) {
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

/* Summary: the number after the one-letter prefix of a channel token such as r255 or h210.5. */
static f_real r_color_channel_read(const LParserTree *tree, const LParserGreenNode *token) {
  const char *source;
  uint_large  size;
  f_real      value;
  f_real      scale;
  boolean     fraction;
  uint_large  i;

  source   = rf_token_text(tree, token, &size);
  value    = 0.0;
  scale    = 1.0;
  fraction = FALSE;

  for (i = 1; i < size; i++) {
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

/* Summary: hue in degrees, saturation and lightness (or value) in percent, to red, green and blue in 0..1. */
static void r_color_hue_convert(f_real hue, f_real saturation, f_real third, boolean lightness, f_real *out_red, f_real *out_green,
                                f_real *out_blue) {
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
    red   = chroma;
    green = second;
    blue  = 0.0;
  } else if (sector < 2.0) {
    red   = second;
    green = chroma;
    blue  = 0.0;
  } else if (sector < 3.0) {
    red   = 0.0;
    green = chroma;
    blue  = second;
  } else if (sector < 4.0) {
    red   = 0.0;
    green = second;
    blue  = chroma;
  } else if (sector < 5.0) {
    red   = second;
    green = 0.0;
    blue  = chroma;
  } else {
    red   = chroma;
    green = 0.0;
    blue  = second;
  }

  match      = lightness ? third - (chroma / 2.0) : third - chroma;
  *out_red   = red + match;
  *out_green = green + match;
  *out_blue  = blue + match;
}

static uint32 r_color_hex_value(const char *digits, uint32 count) {
  uint32 value;
  uint32 i;
  char   digit;

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

uint32 ruleforge_color_value_get(RfColorLiteral view, boolean *out_has_alpha) {
  RfRgbColor rgb;
  RfHslColor hsl;
  RfHsvColor hsv;
  uint32     alpha;
  f_real     red;
  f_real     green;
  f_real     blue;

  NEXUS_PROGRAM_PRECONDITION(view.node != NULL && out_has_alpha != NULL, "ruleforge_color_value_get: NULL argument");

  alpha          = 255;
  *out_has_alpha = FALSE;
  red            = 0.0;
  green          = 0.0;
  blue           = 0.0;
  rgb            = rf_color_literal_rule_rgb_color(view, 0);
  hsl            = rf_color_literal_rule_hsl_color(view, 0);
  hsv            = rf_color_literal_rule_hsv_color(view, 0);

  if (rf_color_literal_count_rule_rgb_color_call(view) > 0) {
    rgb = rf_rgb_color_call_rule_rgb_color(rf_color_literal_rule_rgb_color_call(view, 0), 0);
  }

  if (rf_color_literal_count_token_hex_color(view) > 0) {
    const char *digits;
    uint_large  size;

    digits = rf_token_text(view.tree, rf_color_literal_token_hex_color(view, 0), &size);
    red    = (f_real)r_color_hex_value(digits + 1, 2) / 255.0;
    green  = (f_real)r_color_hex_value(digits + 3, 2) / 255.0;
    blue   = (f_real)r_color_hex_value(digits + 5, 2) / 255.0;

    if (size == 9) {
      alpha          = r_color_hex_value(digits + 7, 2);
      *out_has_alpha = TRUE;
    }
  } else if (rgb.node != NULL) {
    red   = r_color_channel_read(view.tree, rf_rgb_color_token_color_red(rgb, 0)) / 255.0;
    green = r_color_channel_read(view.tree, rf_rgb_color_token_color_green(rgb, 0)) / 255.0;
    blue  = r_color_channel_read(view.tree, rf_rgb_color_token_color_blue(rgb, 0)) / 255.0;

    if (rf_rgb_color_count_token_color_alpha(rgb) > 0) {
      alpha          = (uint32)r_color_channel_read(view.tree, rf_rgb_color_token_color_alpha(rgb, 0));
      *out_has_alpha = TRUE;
    }
  } else if (hsl.node != NULL) {
    r_color_hue_convert(r_color_channel_read(view.tree, rf_hsl_color_token_color_hue(hsl, 0)),
                        r_color_channel_read(view.tree, rf_hsl_color_token_color_saturation(hsl, 0)),
                        r_color_channel_read(view.tree, rf_hsl_color_token_color_lightness(hsl, 0)), TRUE, &red, &green, &blue);

    if (rf_hsl_color_count_token_color_alpha(hsl) > 0) {
      alpha          = (uint32)r_color_channel_read(view.tree, rf_hsl_color_token_color_alpha(hsl, 0));
      *out_has_alpha = TRUE;
    }
  } else if (hsv.node != NULL) {
    r_color_hue_convert(r_color_channel_read(view.tree, rf_hsv_color_token_color_hue(hsv, 0)),
                        r_color_channel_read(view.tree, rf_hsv_color_token_color_saturation(hsv, 0)),
                        r_color_channel_read(view.tree, rf_hsv_color_token_color_value_channel(hsv, 0)), FALSE, &red, &green, &blue);

    if (rf_hsv_color_count_token_color_alpha(hsv) > 0) {
      alpha          = (uint32)r_color_channel_read(view.tree, rf_hsv_color_token_color_alpha(hsv, 0));
      *out_has_alpha = TRUE;
    }
  }

  return r_color_pack(red, green, blue, alpha);
}

uint32 ruleforge_color_alpha_override_get(RfColorOverride view) {
  NEXUS_PROGRAM_PRECONDITION(view.node != NULL, "ruleforge_color_alpha_override_get: NULL view");

  return (uint32)r_color_channel_read(view.tree, rf_color_override_token_color_alpha(view, 0));
}
