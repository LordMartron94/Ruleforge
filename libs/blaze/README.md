# Blaze

Numerical primitives, complex numbers, fixed-size vectors, and geometry in `blaze.h`.

## Numerics

`blaze_numerics_min`, `blaze_numerics_max`, and `blaze_numerics_clamp` operate
on `f_real`. Append `f32` / `f64`, `i8` / `i16` / `i32` / `i64`,
`u8` / `u16` / `u32` / `u64` / `u128`, or `ilarge` / `ularge` for the
corresponding Nexus numeric type. Aliases such as `byte` and `timestamp`
use their underlying integer family. Integer comparisons retain the full range
of their type, including both words of `uint128`.

Clamp takes `(value, minimum, maximum)` and includes both endpoints. Bounds
must be ordered; reversed or NaN bounds assert in developer mode, and the
precondition still applies in release builds. Floating clamp preserves a NaN
value and the sign of an in-range zero. Floating min/max retain the existing
`fmin` / `fmax` semantics: one NaN yields the numeric operand; two yield NaN.
The existing `blaze_numerics_fmin*` and `blaze_numerics_fmax*` wrappers remain
available.

`blaze_numerics_f32_tests` and `blaze_numerics_f64_tests` exercise every
numeric family with both `f_real` precision settings.

## Vectors

Two- and three-component vectors use `BVector2<suffix>` and `BVector3<suffix>`:

- `f`: Nexus's configurable `f_real`.
- `f32` / `f64`: `real32` / `real64`.
- `i32` / `i64`: `int32` / `int64`.
- `u32` / `u64`: `uint32` / `uint64`.

Each family provides `make`, `add`, `subtract`, `multiply`, `divide`,
`add_scalar`, `subtract_scalar`, `multiply_scalar`, `divide_scalar`,
`dot`, and `magnitude`, prefixed with `blaze_vector2<suffix>_` or
`blaze_vector3<suffix>_`. Arithmetic returns a new value. Vector multiplication
and division operate componentwise; scalar operations place the scalar on the
right and apply it to each component.

Dot products sum the component products and return the component type:
`blaze_vector2f_dot(a, b)` computes `a.x * b.x + a.y * b.y`; the 3D variant
also includes `a.z * b.z`.

```c
BVector3f velocity;
BVector3f displacement;

velocity = blaze_vector3f_make(3, 4, 0);
displacement = blaze_vector3f_multiply_scalar(velocity, 0.5);
```

Signed integer addition, subtraction, and multiplication require representable
results. Signed integer dot products require representable products and partial
sums. Unsigned operations wrap modulo the component width. Integer division
truncates toward zero. Zero divisors and signed minimum divided by `-1` are
caller errors checked by assertions in developer mode; the preconditions still
apply in release builds.

Magnitudes are Euclidean lengths computed using `hypot` to avoid unnecessary
intermediate overflow or underflow. Floating vectors return their component
type. Integer vectors return `real64`, converting components before arithmetic;
64-bit integer components may round during conversion.

Noesis's former `NWorldVector3f` is now `BVector3f`; its arithmetic functions
have moved from `noesis_world_vector3f_*` to `blaze_vector3f_*`. Noesis retains
its compass directions and uses `BVector3f64` for movement actions.

With CMake testing enabled, `blaze_vectors_f32_tests` and
`blaze_vectors_f64_tests` exercise both `f_real` precision settings.

## Geometry

`BGridPosition3` represents a position using `int64` unit-tile coordinates and
`real64` local offsets normalized to `[0, 1)`. `blaze_geometry_position3_move`
applies a displacement with checked carries while preserving local precision.

`blaze_geometry_box3_tiles_get` computes inclusive tile bounds for a half-open
box with positive integer dimensions. `blaze_geometry_interval_overlap` and
`blaze_geometry_box3_overlap` test actual overlap while allowing touching faces.
`blaze_geometry_tile3_next` traverses an inclusive tile range.

These operations are independent of world storage and simulation policy.
`blaze_geometry_tests` checks the geometry directly; Noesis also exercises it
through placement, occupancy, and movement resolution.

## Colour

`blaze_color_*` provides reusable sRGB compositing, WCAG luminance and contrast, signed APCA 0.0.98G-4g contrast, D65 CIELAB conversion
and CIEDE2000 differences. Inputs use packed `0xRRGGBBAA`; metrics require opaque colours, so composite alpha onto the intended
backdrop first. `BColorLab` and returned measurements use fixed `real64` precision independently of `f_real`.

There are no allocations, I/O or application dependencies in these operations. `blaze_color_tests` checks all 34 of the
[authors' CIEDE2000 vectors](https://hajim.rochester.edu/ece/sites/gsharma/ciede2000/dataNprograms/ciede2000testdata.txt), symmetry,
WCAG limits, APCA polarity, D65 conversion and compositing. See the public header for contracts and input ranges.
