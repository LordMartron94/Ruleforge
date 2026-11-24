#include <nexus.h>

#include "../blaze.h"

boolean blaze_geometry_position3_move(const BGridPosition3 *position, const BVector3f64 *movement, BGridPosition3 *result) {
  int64  tiles[3];
  real64 offsets[3];
  real64 displacements[3];

  real64 integral;
  real64 local;
  int64  carry;
  uint32 axis;

  NEXUS_PROGRAM_PRECONDITION(position != NULL && movement != NULL && result != NULL, "Position pointers must not be NULL");

  tiles[0] = position->tile.x;
  tiles[1] = position->tile.y;
  tiles[2] = position->tile.z;

  offsets[0] = position->offset.x;
  offsets[1] = position->offset.y;
  offsets[2] = position->offset.z;

  displacements[0] = movement->x;
  displacements[1] = movement->y;
  displacements[2] = movement->z;

  for (axis = 0; axis < 3; axis++) {
    if (!(offsets[axis] >= 0 && offsets[axis] < 1) ||
        !(displacements[axis] >= -9223372036854775808.0 && displacements[axis] < 9223372036854775808.0)) {
      return FALSE;
    }

    /* Preserve a signed local remainder instead of adding a large displacement to the offset. */
    integral = blaze_numerics_truncf64(displacements[axis]);
    carry    = (int64)integral;
    local    = offsets[axis] + (displacements[axis] - integral);

    /* Borrowing implements floor normalization for negative local coordinates. */
    if (local < 0) {
      if (carry == INT64_MIN_VAL) {
        return FALSE;
      }

      carry--;
      local += 1;
    }

    for (;;) {
      if (local < 1) {
        break;
      }

      if (carry == INT64_MAX_VAL) {
        return FALSE;
      }

      carry++;
      local -= 1;
    }

    if ((carry > 0 && tiles[axis] > INT64_MAX_VAL - carry) || (carry < 0 && tiles[axis] < INT64_MIN_VAL - carry)) {
      return FALSE;
    }

    tiles[axis] += carry;
    offsets[axis] = local;
  }

  result->tile   = blaze_vector3i64_make(tiles[0], tiles[1], tiles[2]);
  result->offset = blaze_vector3f64_make(offsets[0], offsets[1], offsets[2]);

  return TRUE;
}

boolean blaze_geometry_box3_tiles_get(const BGridPosition3 *position, const BVector3i32 *size, BVector3i64 *minimum, BVector3i64 *maximum,
                                      uint64 *tile_count) {
  real64 offsets[3];
  int32  dimensions[3];
  int64  lower[3];
  int64  upper[3];
  uint64 count;
  uint64 span;
  int64  extent;
  uint32 axis;

  NEXUS_PROGRAM_PRECONDITION(position != NULL && size != NULL && minimum != NULL && maximum != NULL && tile_count != NULL,
                             "Box bound pointers must not be NULL");
  NEXUS_PROGRAM_PRECONDITION(minimum != maximum, "Minimum and maximum outputs must be distinct");

  offsets[0] = position->offset.x;
  offsets[1] = position->offset.y;
  offsets[2] = position->offset.z;

  lower[0] = position->tile.x;
  lower[1] = position->tile.y;
  lower[2] = position->tile.z;

  dimensions[0] = size->x;
  dimensions[1] = size->y;
  dimensions[2] = size->z;
  count         = 1;

  for (axis = 0; axis < 3; axis++) {
    if (!(offsets[axis] >= 0 && offsets[axis] < 1) || dimensions[axis] <= 0) {
      return FALSE;
    }

    extent = (int64)dimensions[axis] - 1;

    if (offsets[axis] > 0) {
      extent++;
    }

    if (lower[axis] > INT64_MAX_VAL - extent) {
      return FALSE;
    }

    upper[axis] = lower[axis] + extent;
    span        = (uint64)extent + 1;

    if (count > UINT64_MAX_VAL / span) {
      return FALSE;
    }

    count *= span;
  }

  minimum->x  = lower[0];
  minimum->y  = lower[1];
  minimum->z  = lower[2];
  maximum->x  = upper[0];
  maximum->y  = upper[1];
  maximum->z  = upper[2];
  *tile_count = count;

  return TRUE;
}
