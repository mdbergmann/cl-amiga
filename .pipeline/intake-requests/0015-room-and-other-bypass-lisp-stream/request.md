---
id: 0015-room-and-other-bypass-lisp-stream
type: bug
status: done
title: ROOM and other bypass lisp stream
---

# ROOM and other bypass lisp stream

The (room) mystery — explained

  It's a clamiga bug, not SLY. (room) printed nothing in your REPL because bi_room (src/core/builtins_describe.c:588) writes via platform_write_string() — straight to the C-level console (fd 1) —
  bypassing the Lisp *standard-output* stream that SLY redirects through its channel. Its output went to the server log, where I found it:

  Heap:
    19059416 / 33554432 bytes used (56%)
    ...

  No SLY redirection can ever catch that. Normal output is fine — the printer (printer.c:311) uses the Lisp stream when one is set, so (format t ...), (princ ...) and value echo reach SLY correctly.
  Same bypass affects disassemble, the ; Loading/; Compiling lines, and the debugger.

  The clamiga-side fix would be to have bi_room print to *standard-output* instead of platform_write_string.

Also search for others functions that bypass the lisp stream and fix them (i.e. disassemble).
