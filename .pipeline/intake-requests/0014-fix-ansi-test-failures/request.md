---
id: 0014-fix-ansi-test-failures
type: bug
status: done
title: fix ansi test failures
---

# fix ansi test failures

Run 'build/host/clamiga --heap 96M --non-interactive --load "trunk/load-and-test-ansi.lisp"'

There are 6 tests failing, please fix them. Fixing them will result in extending or changing the compiler.

Make sure you are doing really small steps or we will have bigger regression issues.
