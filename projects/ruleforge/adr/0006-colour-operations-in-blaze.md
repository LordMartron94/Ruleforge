# 0006. Reusable colour computations belong to Blaze

## Status

Proposed; Mr. Hoorn finalizes the record.

## Decision

In the context of adding WCAG, APCA and CIEDE2000 checks to Studio, facing the requirement that colour computations remain useful
outside Ruleforge, we decided to put sRGB compositing, luminance, contrast, D65 Lab conversion and CIEDE2000 in Blaze's `color` system.
Ruleforge depends on Blaze and exposes documented intrinsic bindings in `std.color`; Studio calls those bindings through `ruleforge eval`.
The numerical implementation and its reference tests belong to Blaze. Fixed `real64` API types keep its ABI independent of clients'
`f_real` precision settings. We accept a Blaze dependency in the standalone engine and ordinary platform libm rounding.

A dedicated colour library was an alternative; Blaze already owns the required numerical primitives and provides the generic home
without introducing another library boundary. Ruleforge functions remain the preferred implementation for game-specific policy.

## Related Decisions

Extends [0004: standard library modules](./0004-standard-library-in-ruleforge.md): intrinsic bindings may expose algorithms owned by
another reusable library; their declarations and documentation still ship as Ruleforge modules.
