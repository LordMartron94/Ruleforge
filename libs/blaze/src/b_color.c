#include <nexus.h>
#include "../blaze.h"

/* Standards and reference vectors: W3C WCAG 2.2 relative luminance; Myndex APCA 0.0.98G-4g;
   Sharma, Wu and Dalal (2005), https://hajim.rochester.edu/ece/sites/gsharma/ciede2000/ . */

static real64 b_color_linear(uint32 channel) {
  real64 unit;

  unit = (real64)channel / 255.0;

  if (unit <= 0.04045) {
    return unit / 12.92;
  }

  return blaze_numerics_powf64((unit + 0.055) / 1.055, 2.4);
}

uint32 blaze_color_srgb_composite(uint32 foreground, uint32 background) {
  uint32 alpha;
  uint32 result;
  uint32 shift;
  uint32 channel;

  NEXUS_PROGRAM_PRECONDITION((background & 255U) == 255U, "Background must be opaque");

  alpha  = foreground & 255U;
  result = 255U;

  for (shift = 8; shift <= 24; shift += 8) {
    channel = ((((foreground >> shift) & 255U) * alpha) + (((background >> shift) & 255U) * (255U - alpha)) + 127U) / 255U;
    result |= channel << shift;
  }

  return result;
}

real64 blaze_color_srgb_luminance_get(uint32 color) {
  NEXUS_PROGRAM_PRECONDITION((color & 255U) == 255U, "Colour must be opaque");

  return (0.2126 * b_color_linear(color >> 24)) + (0.7152 * b_color_linear((color >> 16) & 255U)) + (0.0722 * b_color_linear((color >> 8) & 255U));
}

real64 blaze_color_srgb_wcag_contrast_get(uint32 first, uint32 second) {
  real64 a;
  real64 b;

  NEXUS_PROGRAM_PRECONDITION((first & 255U) == 255U && (second & 255U) == 255U, "Colours must be opaque");

  a = blaze_color_srgb_luminance_get(first);
  b = blaze_color_srgb_luminance_get(second);

  return a >= b ? (a + 0.05) / (b + 0.05) : (b + 0.05) / (a + 0.05);
}

static real64 b_color_apca_y(uint32 color) {
  real64 y;

  y = (0.2126729 * blaze_numerics_powf64((real64)(color >> 24) / 255.0, 2.4)) +
      (0.7151522 * blaze_numerics_powf64((real64)((color >> 16) & 255U) / 255.0, 2.4)) +
      (0.0721750 * blaze_numerics_powf64((real64)((color >> 8) & 255U) / 255.0, 2.4));

  if (y < 0.022) {
    y += blaze_numerics_powf64(0.022 - y, 1.414);
  }

  return y;
}

real64 blaze_color_srgb_apca_contrast_get(uint32 text, uint32 background) {
  real64 txt;
  real64 bg;
  real64 contrast;

  NEXUS_PROGRAM_PRECONDITION((text & 255U) == 255U && (background & 255U) == 255U, "Colours must be opaque");

  txt = b_color_apca_y(text);
  bg  = b_color_apca_y(background);

  if (blaze_numerics_fabsf64(bg - txt) < 0.0005) {
    return 0.0;
  }

  if (bg > txt) {
    contrast = (blaze_numerics_powf64(bg, 0.56) - blaze_numerics_powf64(txt, 0.57)) * 1.14;

    return contrast < 0.1 ? 0.0 : (contrast - 0.027) * 100.0;
  }

  contrast = (blaze_numerics_powf64(bg, 0.65) - blaze_numerics_powf64(txt, 0.62)) * 1.14;

  return contrast > -0.1 ? 0.0 : (contrast + 0.027) * 100.0;
}

static real64 b_color_lab_f(real64 value) {
  return value > (216.0 / 24389.0) ? blaze_numerics_powf64(value, 1.0 / 3.0) : ((24389.0 / 27.0) * value + 16.0) / 116.0;
}

void blaze_color_srgb_lab_get(uint32 color, BColorLab *out) {
  real64 red;
  real64 green;
  real64 blue;
  real64 x;
  real64 y;
  real64 z;

  NEXUS_PROGRAM_PRECONDITION(out != NULL && (color & 255U) == 255U, "Output must be non-NULL and colour opaque");

  red    = b_color_linear(color >> 24);
  green  = b_color_linear((color >> 16) & 255U);
  blue   = b_color_linear((color >> 8) & 255U);
  x      = b_color_lab_f(((0.4124564 * red) + (0.3575761 * green) + (0.1804375 * blue)) / 0.95047);
  y      = b_color_lab_f((0.2126729 * red) + (0.7151522 * green) + (0.0721750 * blue));
  z      = b_color_lab_f(((0.0193339 * red) + (0.1191920 * green) + (0.9503041 * blue)) / 1.08883);
  out->l = (116.0 * y) - 16.0;
  if (out->l > 100.0) {
    out->l = 100.0;
  }

  out->a = 500.0 * (x - y);
  out->b = 200.0 * (y - z);
}

real64 blaze_color_lab_delta_e_2000_get(const BColorLab *first, const BColorLab *second) {
  real64       c1;
  real64       c2;
  real64       cm7;
  real64       adjustment;
  real64       a1;
  real64       a2;
  real64       cp1;
  real64       cp2;
  real64       hue_first;
  real64       hue_second;
  real64       hue_delta;
  real64       hm;
  real64       lm;
  real64       cm;
  real64       hue_weight;
  real64       ldiff;
  real64       lightness_scale;
  real64       sc;
  real64       hue_scale;
  real64       theta;
  real64       rotation;
  real64       lightness_delta;
  real64       cv;
  real64       hue_distance;
  real64       sum;
  const real64 radians = 0.017453292519943295769;

  NEXUS_PROGRAM_PRECONDITION(first != NULL && second != NULL, "Lab inputs must be non-NULL");
  NEXUS_PROGRAM_PRECONDITION(first->l >= 0.0 && first->l <= 100.0 && second->l >= 0.0 && second->l <= 100.0 && first->a >= -200.0 &&
                                 first->a <= 200.0 && first->b >= -200.0 && first->b <= 200.0 && second->a >= -200.0 && second->a <= 200.0 &&
                                 second->b >= -200.0 && second->b <= 200.0,
                             "Lab inputs must be finite and within the documented ranges");

  c1         = blaze_numerics_sqrtf64((first->a * first->a) + (first->b * first->b));
  c2         = blaze_numerics_sqrtf64((second->a * second->a) + (second->b * second->b));
  cm7        = blaze_numerics_powf64((c1 + c2) / 2.0, 7.0);
  adjustment = (1.0 - blaze_numerics_sqrtf64(cm7 / (cm7 + 6103515625.0))) / 2.0;
  a1         = (1.0 + adjustment) * first->a;
  a2         = (1.0 + adjustment) * second->a;
  cp1        = blaze_numerics_sqrtf64((a1 * a1) + (first->b * first->b));
  cp2        = blaze_numerics_sqrtf64((a2 * a2) + (second->b * second->b));
  hue_first  = cp1 == 0.0 ? 0.0 : blaze_numerics_atan2f64(first->b, a1) / radians;
  hue_second = cp2 == 0.0 ? 0.0 : blaze_numerics_atan2f64(second->b, a2) / radians;

  if (hue_first < 0.0) {
    hue_first += 360.0;
  }
  if (hue_second < 0.0) {
    hue_second += 360.0;
  }

  hue_delta = hue_second - hue_first;
  hm        = (hue_first + hue_second) / 2.0;

  if (cp1 * cp2 == 0.0) {
    hue_delta = 0.0;
    hm        = hue_first + hue_second;
  } else {
    if (hue_delta > 180.0) {
      hue_delta -= 360.0;
    }
    if (hue_delta < -180.0) {
      hue_delta += 360.0;
    }

    if (blaze_numerics_fabsf64(hue_first - hue_second) > 180.0) {
      hm += hue_first + hue_second < 360.0 ? 180.0 : -180.0;
    }
  }

  lm         = (first->l + second->l) / 2.0;
  cm         = (cp1 + cp2) / 2.0;
  hue_weight = 1.0 - (0.17 * blaze_numerics_cosf64((hm - 30.0) * radians)) + (0.24 * blaze_numerics_cosf64(2.0 * hm * radians)) +
               (0.32 * blaze_numerics_cosf64(((3.0 * hm) + 6.0) * radians)) - (0.20 * blaze_numerics_cosf64(((4.0 * hm) - 63.0) * radians));
  ldiff           = (lm - 50.0) * (lm - 50.0);
  lightness_scale = 1.0 + ((0.015 * ldiff) / blaze_numerics_sqrtf64(20.0 + ldiff));
  sc              = 1.0 + (0.045 * cm);
  hue_scale       = 1.0 + (0.015 * cm * hue_weight);
  cm7             = blaze_numerics_powf64(cm, 7.0);
  theta           = 30.0 * blaze_numerics_expf64(-((hm - 275.0) * (hm - 275.0)) / 625.0);
  rotation        = -2.0 * blaze_numerics_sqrtf64(cm7 / (cm7 + 6103515625.0)) * blaze_numerics_sinf64(2.0 * theta * radians);
  lightness_delta = (second->l - first->l) / lightness_scale;
  cv              = (cp2 - cp1) / sc;
  hue_distance    = (2.0 * blaze_numerics_sqrtf64(cp1 * cp2) * blaze_numerics_sinf64(hue_delta * radians / 2.0)) / hue_scale;
  sum             = (lightness_delta * lightness_delta) + (cv * cv) + (hue_distance * hue_distance) + (rotation * cv * hue_distance);

  return blaze_numerics_sqrtf64(sum < 0.0 ? 0.0 : sum);
}

real64 blaze_color_srgb_delta_e_2000_get(uint32 first, uint32 second) {
  BColorLab a;
  BColorLab b;

  NEXUS_PROGRAM_PRECONDITION((first & 255U) == 255U && (second & 255U) == 255U, "Colours must be opaque");

  blaze_color_srgb_lab_get(first, &a);
  blaze_color_srgb_lab_get(second, &b);

  return blaze_color_lab_delta_e_2000_get(&a, &b);
}
