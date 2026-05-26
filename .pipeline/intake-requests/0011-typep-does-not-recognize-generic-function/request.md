---
id: 0011-typep-does-not-recognize-generic-function
type: bug
status: ready
title: typep does not recognize generic-function
---

# typep does not recognize generic-function

GENERIC-FUNCTION is just a reserved symbol (interned for presence, no implementation) — not a CLOS class — so typep falls through to the error. The correct fix mirrors the existing FUNCTION case:
  treat GENERIC-FUNCTION/STANDARD-GENERIC-FUNCTION type specifiers via cl_funcallable_instance_p.
