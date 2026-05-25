---
id: 0009-add-missing-file-stream
type: bug
status: ready
title: add missing file-stream
---

# add missing file-stream

In CLAmiga, one class is missing: FILE-STREAM. All the others (including stream, stream-error, and the freshly-added logical-pathname) resolve fine.

  clamiga-side fix: define FILE-STREAM as a class, subclass of STREAM (that's the standard CL hierarchy — file-stream is a subtype of stream). STREAM is already a STANDARD-CLASS in clamiga, so it's
  the same pattern as LOGICAL-PATHNAME.
