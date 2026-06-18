# `GRAY` — Gray Streams Protocol

The Gray-streams protocol lets you define your own stream classes in Lisp by
subclassing the `fundamental-*-stream` classes and specializing the
`stream-*` generic functions. CL-Amiga's built-in stream functions (`read-char`,
`write-string`, `read-line`, …) dispatch to these generics for user-defined
streams. This is the foundation several ported libraries rely on — e.g. the
**chipz** decompressing stream and **drakma**/**Hunchentoot** flexi-streams — and
it is loaded by the SLY backend.

- **Package:** `GRAY` (nickname `...`), matching ECL/CLISP.
- **Load it:** `(require "gray-streams")` (resolves `lib/gray-streams.lisp`).
  The `GRAY` package must exist at read time before any `gray:`-qualified symbol
  is read, so load it in its own top-level form.

```lisp
(require "gray-streams")

(defclass uppercase-out (gray:fundamental-character-output-stream)
  ((target :initarg :target :reader target)))

(defmethod gray:stream-write-char ((s uppercase-out) ch)
  (write-char (char-upcase ch) (target s)))

(let ((s (make-instance 'uppercase-out :target *standard-output*)))
  (write-string "hello" s))     ; prints HELLO
```

## Fundamental stream classes

Subclass these to define a stream. `fundamental-stream` is the root.

| Class | Specialize for |
|-------|----------------|
| `fundamental-stream` | any stream |
| `fundamental-input-stream` / `fundamental-output-stream` | direction |
| `fundamental-character-stream` | character streams |
| `fundamental-character-input-stream` / `fundamental-character-output-stream` | character I/O |
| `fundamental-binary-stream` | binary streams |
| `fundamental-binary-input-stream` / `fundamental-binary-output-stream` | binary I/O |

## Generic functions to specialize

| Input | Output | Query / control |
|-------|--------|-----------------|
| `stream-read-char` | `stream-write-char` | `stream-line-column` |
| `stream-unread-char` | `stream-write-string` | `stream-start-line-p` |
| `stream-read-char-no-hang` | `stream-write-byte` | `stream-listen` |
| `stream-peek-char` | `stream-terpri` | `stream-clear-input` |
| `stream-read-line` | `stream-fresh-line` | `stream-clear-output` |
| `stream-read-byte` | `stream-finish-output` | `stream-advance-to-column` |
| `stream-read-sequence` | `stream-force-output` | |
| | `stream-write-sequence` | |

## Source of truth

`lib/gray-streams.lisp` is the implementation. The
`(typep gray-stream 'stream)` regression and a worked Gray-stream subclass live
in `tests/amiga/run-tests.lisp` (the "Gray streams" block). The
**chipz** CL-Amiga fork's `stream.lisp` `#+cl-amiga` branch is a real-world usage
example (see [Library forks](../README.md#library-forks-cl-amiga-backends)).
