---
id: 0006-unblock-gray-stream-for-two-way-stream
type: bug
status: ready
title: unblock gray-stream for two-way-stream
---

# unblock gray-stream for two-way-stream

So two clamiga-side gaps remain (both the same recurring pattern — native primitives use a native-struct type check that excludes Gray CLOS streams; your earlier fix covered streamp/typep
  'stream/the printers but not these):

  1. input-stream-p / output-stream-p error instead of returning NIL for the "wrong" direction of a Gray stream. They return T correctly when the answer is yes, but when the answer should be NIL (e.g.
   input-stream-p on a Gray output stream) they signal "argument is not a stream" instead. Compare the native streams, which correctly return NIL.
  2. make-two-way-stream rejects Gray streams — "first argument is not a stream" — even though (streamp gray-in) and (input-stream-p gray-in) are both T. Its argument check is using a native-only
  stream-tag test rather than streamp/input-stream-p/output-stream-p. (And the resulting two-way stream's read/write need to delegate through the generic dispatch that already handles Gray streams —
  the same path your printer fix uses.)

  SLY's listener passes Gray streams to make-two-way-stream (slynk.lisp:558), so fixing both unblocks the mrepl. The native make-two-way-stream itself is correct — it works perfectly with native
  string streams (verified: read/write/accessors/typep all good); it just needs to accept Gray streams too.
