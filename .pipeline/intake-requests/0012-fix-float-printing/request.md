---
id: 0012-fix-float-printing
type: bug
status: done
title: fix float printing
---

# fix float printing

clamiga bug: double-float printer ignores *read-default-float-format* and always emits a d0 exponent marker

  Defect

  When clamiga prints a double-float, it unconditionally appends the d0 exponent marker (e.g. 0.0146877998486161d0), even when *read-default-float-format* is bound to double-float.

  Per CLHS 22.1.3.1.3 (Printing Floats), the exponent marker must be omitted when the float's format matches the current *read-default-float-format*. So with that variable bound to double-float, the
  value above must print as 0.0146877998486161 — no marker. The marker is only correct when the float's format differs from the default (then d0 for doubles, f0 for singles).

  Reproduction

  * (let ((*read-default-float-format* 'double-float))
      (prin1-to-string 0.0146878d0))
  - Expected: "0.0146877998486161"
  - Actual:   "0.0146877998486161d0"

  Verified behavior

  - Default state (*read-default-float-format* = single-float): single-floats print correctly without a marker.
  - The value is genuinely correct (a single coerced to double via coerce/switch-to-double-floats is the right double); only the printed representation is wrong — the marker should not be there when
  the format matches the default.

  Location

  src/core/printer.c, function print_double_float. It calls sprintf(buf, "%.15g", value) and then always tacks on the d0 marker (replacing e→d in the scientific case, or appending d0/.0d0 otherwise).
  It never consults *read-default-float-format*.

  Related latent defect (same area)

  print_single_float is the mirror case: it never emits any marker. That's fine while *read-default-float-format* is single-float (the default), but when the default is double-float, a single-float
  must print with an f0 marker so it reads back as a single rather than a double. Currently it would print markerless and be misread as a double-float.

  Correct rule for both printers

  Emit an exponent marker only when the float's format ≠ *read-default-float-format*:
  - default single-float: singles markerless, doubles get d0
  - default double-float: doubles markerless, singles get f0
  - scientific notation: marker letter is e when the format matches the default, otherwise f/d
