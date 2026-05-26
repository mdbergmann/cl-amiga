---
id: 0013-check-vector-auto-fill-pointer
type: bug
status: done
title: check vector auto-fill pointer
---

# check vector auto-fill pointer

The (Emacs)arglist/autodoc path fails with: VECTOR-PUSH-EXTEND: vector is not adjustable. It's a vector-push-extend call on a vector that wasn't created
  adjustable (with a fill pointer). In Emacs it doesn't abort anything, but it does prevent arglist/autodoc results from being produced.

Please check and fix if necessary.
