#include <nexus.h>
#include "../blaze.h"

/* Sharma, Wu and Dalal's full 34-pair supplementary dataset, including hue wrap and zero chroma.
   Source: https://hajim.rochester.edu/ece/sites/gsharma/ciede2000/dataNprograms/ciede2000testdata.txt */
static const real64 b_color_vectors[][7] = {
    {50.0000, 2.6772, -79.7751, 50.0000, 0.0000, -82.7485, 2.0425},   {50.0000, 3.1571, -77.2803, 50.0000, 0.0000, -82.7485, 2.8615},
    {50.0000, 2.8361, -74.0200, 50.0000, 0.0000, -82.7485, 3.4412},   {50.0000, -1.3802, -84.2814, 50.0000, 0.0000, -82.7485, 1.0000},
    {50.0000, -1.1848, -84.8006, 50.0000, 0.0000, -82.7485, 1.0000},  {50.0000, -0.9009, -85.5211, 50.0000, 0.0000, -82.7485, 1.0000},
    {50.0000, 0.0000, 0.0000, 50.0000, -1.0000, 2.0000, 2.3669},      {50.0000, -1.0000, 2.0000, 50.0000, 0.0000, 0.0000, 2.3669},
    {50.0000, 2.4900, -0.0010, 50.0000, -2.4900, 0.0009, 7.1792},     {50.0000, 2.4900, -0.0010, 50.0000, -2.4900, 0.0010, 7.1792},
    {50.0000, 2.4900, -0.0010, 50.0000, -2.4900, 0.0011, 7.2195},     {50.0000, 2.4900, -0.0010, 50.0000, -2.4900, 0.0012, 7.2195},
    {50.0000, -0.0010, 2.4900, 50.0000, 0.0009, -2.4900, 4.8045},     {50.0000, -0.0010, 2.4900, 50.0000, 0.0010, -2.4900, 4.8045},
    {50.0000, -0.0010, 2.4900, 50.0000, 0.0011, -2.4900, 4.7461},     {50.0000, 2.5000, 0.0000, 50.0000, 0.0000, -2.5000, 4.3065},
    {50.0000, 2.5000, 0.0000, 73.0000, 25.0000, -18.0000, 27.1492},   {50.0000, 2.5000, 0.0000, 61.0000, -5.0000, 29.0000, 22.8977},
    {50.0000, 2.5000, 0.0000, 56.0000, -27.0000, -3.0000, 31.9030},   {50.0000, 2.5000, 0.0000, 58.0000, 24.0000, 15.0000, 19.4535},
    {50.0000, 2.5000, 0.0000, 50.0000, 3.1736, 0.5854, 1.0000},       {50.0000, 2.5000, 0.0000, 50.0000, 3.2972, 0.0000, 1.0000},
    {50.0000, 2.5000, 0.0000, 50.0000, 1.8634, 0.5757, 1.0000},       {50.0000, 2.5000, 0.0000, 50.0000, 3.2592, 0.3350, 1.0000},
    {60.2574, -34.0099, 36.2677, 60.4626, -34.1751, 39.4387, 1.2644}, {63.0109, -31.0961, -5.8663, 62.8187, -29.7946, -4.0864, 1.2630},
    {61.2901, 3.7196, -5.3901, 61.4292, 2.2480, -4.9620, 1.8731},     {35.0831, -44.1164, 3.7933, 35.0232, -40.0716, 1.5901, 1.8645},
    {22.7233, 20.0904, -46.6940, 23.0331, 14.9730, -42.5619, 2.0373}, {36.4612, 47.8580, 18.3852, 36.2715, 50.5065, 21.2231, 1.4146},
    {90.8027, -2.0831, 1.4410, 91.1528, -1.6435, 0.0447, 1.4441},     {90.9257, -0.5406, -0.9208, 88.6381, -0.8985, -0.7239, 1.5381},
    {6.7747, -0.2908, -2.4247, 5.8714, -0.0985, -2.2286, 0.6377},     {2.0776, 0.0795, -1.1350, 0.9033, -0.0636, -0.5514, 0.9082}};

#define B_COLOR_REQUIRE(condition)                                                                                                                   \
  do {                                                                                                                                               \
    if (!(condition)) {                                                                                                                              \
      (void)nexus_stdio_stderr_write_formatted("Blaze colour test failed at line %d: %s\n", __LINE__, #condition);                                   \
      return 1;                                                                                                                                      \
    }                                                                                                                                                \
  } while (0)

int main(void) {
  uint32    index;
  BColorLab first;
  BColorLab second;
  real64    forward;
  real64    reverse;

  for (index = 0; index < 34; index++) {
    first.l  = b_color_vectors[index][0];
    first.a  = b_color_vectors[index][1];
    first.b  = b_color_vectors[index][2];
    second.l = b_color_vectors[index][3];
    second.a = b_color_vectors[index][4];
    second.b = b_color_vectors[index][5];
    forward  = blaze_color_lab_delta_e_2000_get(&first, &second);
    reverse  = blaze_color_lab_delta_e_2000_get(&second, &first);
    B_COLOR_REQUIRE(blaze_numerics_fabsf64(forward - b_color_vectors[index][6]) < 0.00005);
    B_COLOR_REQUIRE(blaze_numerics_fabsf64(forward - reverse) < 1e-12);
  }

  B_COLOR_REQUIRE(blaze_color_srgb_luminance_get(0x000000FFU) == 0.0);
  B_COLOR_REQUIRE(blaze_numerics_fabsf64(blaze_color_srgb_luminance_get(0xFFFFFFFFU) - 1.0) < 1e-12);
  B_COLOR_REQUIRE(blaze_numerics_fabsf64(blaze_color_srgb_wcag_contrast_get(0x000000FFU, 0xFFFFFFFFU) - 21.0) < 1e-12);
  B_COLOR_REQUIRE(blaze_color_srgb_wcag_contrast_get(0x123456FFU, 0x123456FFU) == 1.0);
  B_COLOR_REQUIRE(blaze_numerics_fabsf64(blaze_color_srgb_apca_contrast_get(0x000000FFU, 0xFFFFFFFFU) - 106.04067321268862) < 1e-10);
  B_COLOR_REQUIRE(blaze_numerics_fabsf64(blaze_color_srgb_apca_contrast_get(0xFFFFFFFFU, 0x000000FFU) + 107.88473318309848) < 1e-10);
  B_COLOR_REQUIRE(blaze_color_srgb_apca_contrast_get(0x050505FFU, 0x050505FFU) == 0.0);
  B_COLOR_REQUIRE(blaze_color_srgb_composite(0xFF000080U, 0x0000FFFFU) == 0x80007FFFU);
  B_COLOR_REQUIRE(blaze_color_srgb_composite(0xABCDEF00U, 0x123456FFU) == 0x123456FFU);
  B_COLOR_REQUIRE(blaze_color_srgb_delta_e_2000_get(0xFFFFFFFFU, 0xFFFFFFFFU) == 0.0);
  B_COLOR_REQUIRE(blaze_color_srgb_delta_e_2000_get(0x000000FFU, 0x000000FFU) == 0.0);
  blaze_color_srgb_lab_get(0xFF0000FFU, &first);
  (void)nexus_stdio_stdout_write_formatted("sRGB red D65 Lab: %.9f %.9f %.9f\n", first.l, first.a, first.b);
  B_COLOR_REQUIRE(blaze_numerics_fabsf64(first.l - 53.2408) < 0.001);
  B_COLOR_REQUIRE(blaze_numerics_fabsf64(first.a - 80.0925) < 0.001);
  B_COLOR_REQUIRE(blaze_numerics_fabsf64(first.b - 67.2032) < 0.001);
  (void)nexus_stdio_stdout_write_cstring("34 CIEDE2000 vectors, symmetry, WCAG, APCA, D65 and compositing passed\n");

  return 0;
}
