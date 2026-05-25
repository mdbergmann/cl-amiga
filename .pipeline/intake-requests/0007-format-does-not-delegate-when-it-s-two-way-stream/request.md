---
id: 0007-format-does-not-delegate-when-it-s-two-way-stream
type: bug
status: done
title: format does not delegate when it's two-way-stream
---

# format does not delegate when it's two-way-stream

format does not delegate to the output substream when its stream argument is a two-way stream. It returns normally but writes nothing. Every other
  printer resolves the two-way stream to its output substream correctly; format's stream-resolution doesn't.

  This matters for SLY because *debug-io*/*query-io*/*terminal-io* are bound to the two-way stream and the debugger/REPL prompts use format on them. (*standard-output* is bound to the plain Gray out,
  not the two-way stream, so normal REPL value printing should already be fine.)
