---
id: 0002-fix-restart-printing-and-behavior
type: bug
status: done
title: fix restart printing and behavior
---

# fix restart printing and behavior

Use ./build/host/clamiga to check.

Here is the error description:

  - The third restart's description prints as "#<RESTART ABORT>" instead of a description string.
  - throw-to-toplevel returns (:abort "NIL") (reason stringified as "NIL")
