---
id: 0010-reader-nil
type: feature
status: done
title: reader NIL
---

# reader NIL

Pinpointed — and it's a narrow reader bug:

  ┌─────────────────────────────────┬────────────────────────────────────────────┐
  │              probe              │                   result                   │
  ├─────────────────────────────────┼────────────────────────────────────────────┤
  │ (find-symbol "NIL" "CL") status │ :EXTERNAL ✓                                │
  ├─────────────────────────────────┼────────────────────────────────────────────┤
  │ (find-symbol "T" "CL") status   │ :EXTERNAL ✓                                │
  ├─────────────────────────────────┼────────────────────────────────────────────┤
  │ read cl:t                       │ T ✓                                        │
  ├─────────────────────────────────┼────────────────────────────────────────────┤
  │ read cl:car                     │ CAR ✓                                      │
  ├─────────────────────────────────┼────────────────────────────────────────────┤
  │ read cl:nil                     │ ERROR: "Symbol NIL not exported from CL" ✗ │
  └─────────────────────────────────┴────────────────────────────────────────────┘

  So NIL genuinely is external in CL (find-symbol confirms it), and other external symbols read fine with the cl: prefix — but the reader specifically rejects cl:nil.

  clamiga-side fix: the reader's single-colon (package:symbol) export check is wrong for NIL. Almost certainly a special-case path for the symbol named "NIL" (since NIL doubles as the empty list /
  boolean false) that doesn't report it as external — it should agree with find-symbol, which already says :EXTERNAL. Reading cl:nil should yield NIL, just like cl:t yields T.
