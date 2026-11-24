#include "../nexus.h"

#include <math.h>

/* All three round entry points delegate to one real64 implementation: real64 exactly
   represents a promoted real32, so duplicating the switch would add no precision. */

real64 nexus_real64_round(real64 value, NexusRealRoundMode mode) {
  real64 floor_value;
  real64 fractional;
  real64 result;

  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_real64_round");

  switch (mode) {
  case NRRM_FLOOR:
    result = floor(value);
    break;

  case NRRM_CEIL:
    result = ceil(value);
    break;

  case NRRM_TRUNC:
    result = (value >= 0.0) ? floor(value) : ceil(value);
    break;

  case NRRM_ROUND:
    result = (value >= 0.0) ? floor(value + 0.5) : ceil(value - 0.5);
    break;

  case NRRM_ROUND_EVEN:
    floor_value = floor(value);
    fractional  = value - floor_value;

    if (fractional < 0.5) {
      result = floor_value;
    } else if (fractional > 0.5) {
      result = floor_value + 1.0;
    } else {
      /* Exactly halfway -- move to the nearest even candidate (banker's rounding). */
      result = (fmod(floor_value, 2.0) == 0.0) ? floor_value : floor_value + 1.0;
    }
    break;

  default:
    NEXUS_PROGRAM_PRECONDITION_ALWAYS(FALSE, "invalid NexusRealRoundMode: %d", (int)mode);
    result = value; /* unreachable -- PRECONDITION_ALWAYS aborts above */
    break;
  }

  NEXUS_PROGRAM_CONTEXT_POP();
  return result;
}

real32 nexus_real32_round(real32 value, NexusRealRoundMode mode) {
  return (real32)nexus_real64_round((real64)value, mode);
}

f_real nexus_real_round(f_real value, NexusRealRoundMode mode) {
  return (f_real)nexus_real64_round((real64)value, mode);
}

f_real nexus_real_round_to_decimals(f_real value, uint32 decimal_places, NexusRealRoundMode mode) {
  static const real64 powers_of_ten[18] = {1e0, 1e1, 1e2, 1e3, 1e4, 1e5, 1e6, 1e7, 1e8, 1e9, 1e10, 1e11, 1e12, 1e13, 1e14, 1e15, 1e16, 1e17};
  real64              scale;
  real64              scaled_value;

  if (decimal_places > 17) {
    decimal_places = 17;
  }

  scale        = powers_of_ten[decimal_places];
  scaled_value = (real64)value * scale;
  scaled_value = nexus_real64_round(scaled_value, mode);

  return (f_real)(scaled_value / scale);
}
