#include <nexus.h>

#include <stdio.h>
#include <stdlib.h>

#include "../blaze.h"

#define BLAZE_GEOMETRY_CHECK(condition) b_geometry_test_check((boolean)(condition), #condition, __LINE__)

static void b_geometry_test_check(boolean condition, const char *expression, int line) {
  if (!condition) {
    (void)fprintf(stderr, "Blaze geometry test failed at line %d: %s\n", line, expression);
    exit(1);
  }
}

int main(void) {
  BGridPosition3 position;
  BGridPosition3 result;
  BGridPosition3 other_position;

  BVector3f64 movement;
  BVector3i32 size;
  BVector3i64 minimum;
  BVector3i64 maximum;
  BVector3i64 tile;
  BVector3i64 last;

  uint64 tile_count;
  uint64 visited;

  /* Negative movement borrows a tile; positive movement carries a tile. */
  position.tile   = blaze_vector3i64_make(0, 0, 0);
  position.offset = blaze_vector3f64_make(0.25, 0.5, 0.75);
  movement        = blaze_vector3f64_make(-0.5, 0.75, -1);

  BLAZE_GEOMETRY_CHECK(blaze_geometry_position3_move(&position, &movement, &result));
  BLAZE_GEOMETRY_CHECK(result.tile.x == -1 && result.tile.y == 1 && result.tile.z == -1);
  BLAZE_GEOMETRY_CHECK(result.offset.x == 0.75 && result.offset.y == 0.25 && result.offset.z == 0.75);

  /* In-place movement retains local precision across an enormous integer step. */
  position = result;
  movement = blaze_vector3f64_make(1152921504606846976.0, 0, 0);

  BLAZE_GEOMETRY_CHECK(blaze_geometry_position3_move(&position, &movement, &position));
  BLAZE_GEOMETRY_CHECK(position.tile.x == (((int64)1 << 60) - 1));
  BLAZE_GEOMETRY_CHECK(position.offset.x == 0.75);

  /* Failure on the final axis leaves every output field untouched. */
  position.tile   = blaze_vector3i64_make(0, 0, INT64_MAX_VAL);
  position.offset = blaze_vector3f64_make(0, 0, 0);
  result.tile     = blaze_vector3i64_make(123, 456, 789);
  result.offset   = blaze_vector3f64_make(0.125, 0.25, 0.5);
  movement        = blaze_vector3f64_make(1, 2, 1);

  BLAZE_GEOMETRY_CHECK(blaze_geometry_position3_move(&position, &movement, &result) == FALSE);
  BLAZE_GEOMETRY_CHECK(result.tile.x == 123 && result.tile.y == 456 && result.tile.z == 789);
  BLAZE_GEOMETRY_CHECK(result.offset.x == 0.125 && result.offset.y == 0.25 && result.offset.z == 0.5);

  position.offset.x = -0.25;
  BLAZE_GEOMETRY_CHECK(blaze_geometry_position3_move(&position, &movement, &result) == FALSE);

  position.offset.x = 0;
  movement.x        = blaze_numerics_sqrtf64(-1);
  BLAZE_GEOMETRY_CHECK(blaze_geometry_position3_move(&position, &movement, &result) == FALSE);

  /* Fractional corners occupy the partially covered far-face tiles. */
  position.tile   = blaze_vector3i64_make(-2, 3, -4);
  position.offset = blaze_vector3f64_make(0.75, 0, 0.5);
  size            = blaze_vector3i32_make(2, 1, 2);

  BLAZE_GEOMETRY_CHECK(blaze_geometry_box3_tiles_get(&position, &size, &minimum, &maximum, &tile_count));
  BLAZE_GEOMETRY_CHECK(minimum.x == -2 && minimum.y == 3 && minimum.z == -4);
  BLAZE_GEOMETRY_CHECK(maximum.x == 0 && maximum.y == 3 && maximum.z == -2);
  BLAZE_GEOMETRY_CHECK(tile_count == 9);

  /* An aligned unit box occupies one tile, including at int64 limits. */
  position.tile   = blaze_vector3i64_make(INT64_MIN_VAL, 0, INT64_MAX_VAL);
  position.offset = blaze_vector3f64_make(0, 0, 0);
  size            = blaze_vector3i32_make(1, 1, 1);

  BLAZE_GEOMETRY_CHECK(blaze_geometry_box3_tiles_get(&position, &size, &minimum, &maximum, &tile_count));
  BLAZE_GEOMETRY_CHECK(minimum.x == INT64_MIN_VAL && maximum.z == INT64_MAX_VAL && tile_count == 1);

  position.offset.z = 0.5;
  BLAZE_GEOMETRY_CHECK(blaze_geometry_box3_tiles_get(&position, &size, &minimum, &maximum, &tile_count) == FALSE);
  BLAZE_GEOMETRY_CHECK(minimum.x == INT64_MIN_VAL && maximum.z == INT64_MAX_VAL && tile_count == 1);

  position.tile   = blaze_vector3i64_make(0, 0, 0);
  position.offset = blaze_vector3f64_make(0, 0, 0);
  size            = blaze_vector3i32_make(INT32_MAX_VAL, INT32_MAX_VAL, INT32_MAX_VAL);

  BLAZE_GEOMETRY_CHECK(blaze_geometry_box3_tiles_get(&position, &size, &minimum, &maximum, &tile_count) == FALSE);
  BLAZE_GEOMETRY_CHECK(tile_count == 1);

  /* Overlap remains precise far from the origin; touching faces are allowed. */
  size                  = blaze_vector3i32_make(1, 1, 1);
  position.tile         = blaze_vector3i64_make((int64)1 << 60, 0, 0);
  position.offset       = blaze_vector3f64_make(0.125, 0, 0);
  other_position.tile   = blaze_vector3i64_make(((int64)1 << 60) + 1, 0, 0);
  other_position.offset = blaze_vector3f64_make(0, 0, 0);

  BLAZE_GEOMETRY_CHECK(blaze_geometry_box3_overlap(&position, &size, &other_position, &size));
  BLAZE_GEOMETRY_CHECK(blaze_geometry_box3_overlap(&other_position, &size, &position, &size));

  other_position.offset.x = 0.125;
  BLAZE_GEOMETRY_CHECK(blaze_geometry_box3_overlap(&position, &size, &other_position, &size) == FALSE);
  BLAZE_GEOMETRY_CHECK(blaze_geometry_box3_overlap(&other_position, &size, &position, &size) == FALSE);

  other_position.tile.x = INT64_MIN_VAL;
  BLAZE_GEOMETRY_CHECK(blaze_geometry_box3_overlap(&position, &size, &other_position, &size) == FALSE);
  BLAZE_GEOMETRY_CHECK(blaze_geometry_interval_overlap(-1, 0.5, 1, 0, 0.25, 1));
  BLAZE_GEOMETRY_CHECK(blaze_geometry_interval_overlap(-1, 0.5, 1, 0, 0.5, 1) == FALSE);

  position.tile         = blaze_vector3i64_make(0, 0, 0);
  position.offset       = blaze_vector3f64_make(1e-20, 0, 0);
  other_position.tile   = blaze_vector3i64_make(1, 0, 0);
  other_position.offset = blaze_vector3f64_make(0, 0, 0);
  BLAZE_GEOMETRY_CHECK(blaze_geometry_box3_overlap(&position, &size, &other_position, &size));

  /* Visit each coordinate once without incrementing past INT64_MAX. */
  minimum = blaze_vector3i64_make(INT64_MAX_VAL - 1, -1, 5);
  maximum = blaze_vector3i64_make(INT64_MAX_VAL, 0, 6);
  tile    = minimum;
  visited = 0;

  for (;;) {
    last = tile;
    visited++;

    if (blaze_geometry_tile3_next(&tile, &minimum, &maximum) == FALSE) {
      break;
    }
  }

  BLAZE_GEOMETRY_CHECK(visited == 8);
  BLAZE_GEOMETRY_CHECK(last.x == maximum.x && last.y == maximum.y && last.z == maximum.z);

  (void)puts("Blaze geometry tests passed.");

  return 0;
}
