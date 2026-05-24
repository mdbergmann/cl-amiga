---
id: 0001-gray-streams-fix-on-clamiga-for-slynk
type: bug
status: done
title: gray streams fix on clamiga (for slynk)
---

# gray streams fix on clamiga (for slynk)

clamiga's printer rejects Gray streams

  This is the cause of the #<SIMPLE-ERROR> instead of "boom" in SLDB. The inconsistency:

  (streamp g)          => T
  (output-stream-p g)  => T
  (typep g 'stream)    => NIL      ← root cause

  For a SLY Gray output stream:
  - write-char / write-string / terpri / write-line → work (dispatch via the Gray protocol)
  - princ / prin1 / print → TYPE-ERROR: argument is not a stream
  - format / write → silently misdirect (output leaks to the real terminal instead of the stream)

  Fix (clamiga): make (typep gray-stream 'stream) return T, and have the high-level printer functions (princ/prin1/print/write/format) accept and dispatch to Gray-stream instances the same way
  write-string already does. The high-level printers are evidently doing a (typep x 'stream)-style check (or a C-level native-stream tag check) that excludes Gray streams.

  Right now this only surfaces cosmetically (SLDB shows #<SIMPLE-ERROR> rather than the real message text, because %%condition-message princ's the condition to a truncating Gray stream). But it will
  matter more once we wire up the mrepl REPL — value/*standard-output* printing there relies on the same path.
