# Phase 7: I/O & Pathnames — Implementation Plan

## Context

Phase 7 adds CL-standard streams, file I/O, pathnames, feature conditionals, and CLI enhancements. This is the largest remaining gap for practical CL programs — without streams, user code can't do file I/O, redirect output, or use `format` to strings.

**Arrays are NOT a prerequisite.** Stream internal buffers use `platform_alloc`'d C memory, not CL vectors. The CL string returned by `get-output-stream-string` is created at extraction time.

## Step 1: Stream Heap Type + Platform File I/O

Add `TYPE_STREAM = 14` and extend the platform layer with handle-based file I/O.

**CL_Stream struct** (all fields `uint32_t`/`CL_Obj` for 32-bit safety):
- `direction` (INPUT=1, OUTPUT=2, IO=3)
- `stream_type` (CONSOLE=0, FILE=1, STRING=2)
- `flags` (bit 0: open, bit 1: eof)
- `handle_id` — index into side table of platform file handles (avoids 64-bit pointer in arena)
- `string_buf` — CL_Obj: source string for input streams, NIL for output
- `position` — read/write cursor
- `out_buf_handle` — index into side table for `platform_alloc`'d growable C buffer
- `out_buf_size`, `out_buf_len` — capacity and used length
- `unread_char` — -1 if none, else pushed-back char
- `element_type` — CL_Obj symbol (CHARACTER)

**Platform extensions** (`platform.h`):
```c
typedef uint32_t PlatformFile;
#define PLATFORM_FILE_INVALID 0
PlatformFile platform_file_open(const char *path, int mode); /* 0=read,1=write,2=append */
void  platform_file_close(PlatformFile fh);
int   platform_file_getchar(PlatformFile fh);
int   platform_file_write_string(PlatformFile fh, const char *str);
int   platform_file_write_char(PlatformFile fh, int ch);
int   platform_file_eof(PlatformFile fh);
```

POSIX impl: small side table (max 64 entries) mapping `uint32_t` → `FILE*`. Amiga impl: `Open()`/`Close()`/`FGetC()`/`FPutC()` with BPTR stored directly as uint32_t.

**Files to create:** `src/core/stream.h`, `src/core/stream.c`
**Files to modify:** `types.h` (enum), `types.c` (type_name), `mem.c` (alloc + gc_mark), `mem.h`, `printer.c` (`#<STREAM ...>`), `builtins_type.c` (typep), `platform.h`, `platform_posix.c`, `platform_amiga.c`, `Makefile`, `Makefile.amiga`, `clamiga.h`
**Tests:** `tests/test_stream.c` — allocation, GC marking, platform file open/read/write/close

---

## Step 2: Console Streams + Standard Stream Variables

Create singleton console streams and bind the 7 CL standard stream variables.

- `*standard-input*` (console input), `*standard-output*` (console output), `*error-output*` (console output)
- `*trace-output*`, `*debug-io*`, `*query-io*`, `*terminal-io*` — all alias console streams initially
- Mark as `CL_SYM_SPECIAL`
- Builtins: `streamp`, `input-stream-p`, `output-stream-p`, `interactive-stream-p`
- Well-known symbols: `SYM_STANDARD_INPUT`, `SYM_STANDARD_OUTPUT`, `SYM_ERROR_OUTPUT`, etc.

**Files to modify:** `stream.c/h`, `symbol.h/c`, `builtins_stream.c` (new), `builtins.c` (call init), `main.c` (init ordering), `Makefile`

---

## Step 3: Low-Level Stream C API

Dispatch functions in `stream.c` that route by `stream_type`:

- `cl_stream_read_char(CL_Obj stream)` → console: `platform_getchar`, file: `platform_file_getchar`, string: buf[pos++]
- `cl_stream_unread_char(CL_Obj stream, int ch)` → store in `unread_char` field
- `cl_stream_peek_char(CL_Obj stream)` → read + unread
- `cl_stream_write_char(CL_Obj stream, int ch)` → console: `platform_write_string`, file: `platform_file_write_char`, string: grow buffer
- `cl_stream_write_string(CL_Obj stream, const char *str)`
- `cl_stream_eof_p(CL_Obj stream)`

String output buffer: `platform_alloc`'d, doubles on overflow (start 256 bytes).

**Files:** `stream.c/h`
**Tests:** Read/write chars on all 3 stream types, unread-char, buffer growth

---

## Step 4: CL Stream Builtins

**C builtins** (core primitives that need platform dispatch, default to `*standard-input*`/`*standard-output*`):
`read-char`, `write-char`, `peek-char` (with peek-type), `unread-char`, `read-line`, `write-string`, `terpri`, `close`, `listen`, `finish-output`, `force-output`, `clear-output`

**Lisp functions** in boot.lisp (compositions of C primitives):
- `(defun write-line (string &optional stream) (write-string string stream) (terpri stream) string)`
- `(defun fresh-line (&optional stream) ...)` — write newline unless at start of line (needs column tracking or just always write)

**Files:** `builtins_stream.c`, `lib/boot.lisp`, `Makefile`
**Tests:** Host C tests + Amiga batch tests for each builtin

---

## Step 5: String Streams

**C builtins** (allocation/extraction needs C):
- `make-string-input-stream` — wraps a CL string
- `make-string-output-stream` — creates growable output buffer
- `get-output-stream-string` — extracts content as CL string, resets buffer

**Lisp** in boot.lisp (resource management macros):
- `with-output-to-string` — macro using `make-string-output-stream` + `unwind-protect` + `get-output-stream-string`
- `with-input-from-string` — macro using `make-string-input-stream` + `unwind-protect` + `close`

**Files:** `builtins_stream.c`, `lib/boot.lisp`
**Tests:** `(with-output-to-string (s) (write-string "hello" s))` → `"hello"`, round-trip read/write

---

## Step 6: Refactor Reader to Accept Streams

**Biggest refactoring step.** Replace static `use_stream`/`current_stream` globals with a stream-dispatched approach.

**C refactoring** (internal reader plumbing):
1. Add internal `reader_stream` global (`CL_Obj`) that holds the current read stream
2. Change `read_char()` and `unread_char()` to dispatch through `cl_stream_read_char(reader_stream)` / `cl_stream_unread_char(reader_stream, ch)`
3. `cl_read()` → sets `reader_stream` to `*standard-input*` console stream, calls core reader
4. `cl_read_from_string()` → creates temp string input stream, sets `reader_stream`, calls core reader
5. Add `cl_read_from_stream(CL_Obj stream)` — public, sets `reader_stream`

**C builtins:** `read` (optional stream, eof-error-p, eof-value)

**Lisp** in boot.lisp:
- `(defun read-from-string (string &optional eof-error-p eof-value) (let ((s (make-string-input-stream string))) (read s eof-error-p eof-value)))`

Backward compatibility: `cl_read()`, `cl_read_from_string()`, `cl_reader_eof()` all still work with same signatures. REPL, `load`, and tests unaffected.

**Files:** `reader.c/h`, `builtins_stream.c` or `builtins_io.c`, `lib/boot.lisp`
**Tests:** All 716+ existing host tests must still pass. New: `(read-from-string "(+ 1 2)")`, read from file stream

---

## Step 7: Refactor Printer to Accept Streams

Replace static `to_buffer`/`out_buf` globals with stream-dispatched output.

**C refactoring** (internal printer plumbing):
1. Add internal `printer_stream` global (`CL_Obj`) holding current output stream
2. Change `out_char()`/`out_str()` to dispatch through `cl_stream_write_char(printer_stream, ch)`
3. `cl_prin1()`/`cl_princ()` → set `printer_stream` to `*standard-output*`
4. `cl_prin1_to_string()`/`cl_princ_to_string()` → create temp string output stream
5. Add `cl_prin1_to_stream(CL_Obj obj, CL_Obj stream)`, `cl_princ_to_stream()`
6. Update `print`/`prin1`/`princ` builtins to accept optional stream arg
7. Update `format`: `T` = `*standard-output*`, `NIL` = returns string, stream = that stream

**Lisp** in boot.lisp (thin wrappers over stream-aware primitives):
- `(defun write-to-string (obj &rest keys) (with-output-to-string (s) (apply #'write obj :stream s keys)))`
- `(defun prin1-to-string (obj) (with-output-to-string (s) (prin1 obj s)))`
- `(defun princ-to-string (obj) (with-output-to-string (s) (princ obj s)))`
- These replace the current C `cl_prin1_to_string`/`cl_princ_to_string` buffer approach

**Files:** `printer.c/h`, `builtins_io.c`, `lib/boot.lisp`
**Tests:** All existing tests pass. `(format nil "~A" 42)` → `"42"`, `(prin1-to-string 42)` → `"42"`

---

## Step 8: File Streams — `open`, `close`, `with-open-file`

**C builtin** (needs platform_file_open dispatch):
- `open`: `(open path &key :direction :if-exists :if-does-not-exist)`
  - `:direction` — `:input` (default), `:output`, `:io`
  - `:if-exists` — `:supersede`, `:append`, `:error`
  - `:if-does-not-exist` — `:create`, `:error`, `:nil`
- `close` from Step 4 already handles `platform_file_close`

**Lisp** in boot.lisp:
- `with-open-file` macro: `(let ((var (open ,@open-args))) (unwind-protect (progn ,@body) (when var (close var))))`

**Files:** `builtins_stream.c`, `lib/boot.lisp`
**Tests:** Write then read a file, open nonexistent → error, with-open-file cleanup on error

---

## Step 9: Feature Conditionals (`*features*`, `#+`, `#-`) + `sleep`

**`*features*`:** special variable, list of keywords. Default: `(:CL-AMIGA :COMMON-LISP)` + platform-specific `(:POSIX)` or `(:AMIGAOS :M68K)`.

**Reader `#+`/`#-`:** In reader.c `#` dispatch:
- `#+` reads feature expr; if feature present, read next form; else skip form
- `#-` inverse
- Feature exprs: keyword atoms, `(and ...)`, `(or ...)`, `(not ...)`
- Skipping a form: call reader with a "skip" flag or read-and-discard

**`sleep`:** `(sleep seconds)` — `platform_sleep_ms()` — POSIX: `usleep()`, Amiga: `Delay()`

**`with-standard-io-syntax`:** boot.lisp macro, initially just executes body (no printer vars to rebind yet)

**Files:** `reader.c`, `symbol.h/c`, `stream.c` or `builtins_stream.c`, `platform.h`, `platform_posix.c`, `platform_amiga.c`, `lib/boot.lisp`
**Tests:** `#+cl-amiga 42` → 42, `#-cl-amiga 42` → skipped, `#+posix "yes"` on host, `(and ...)` / `(or ...)` / `(not ...)` exprs, `(sleep 0)` works

---

## Step 10: Time Functions + Minimal Pathnames

**C builtins** (need platform calls):
- `get-universal-time` — POSIX: `time(NULL) + 2208988800`, Amiga: DateStamp conversion
- `probe-file`, `delete-file`, `rename-file`, `file-write-date` — thin wrappers around platform layer
- `%mkdir` — internal primitive for `ensure-directories-exist`

**C platform extensions:** `platform_file_exists()`, `platform_file_delete()`, `platform_file_rename()`, `platform_file_mtime()`, `platform_universal_time()`, `platform_mkdir()`

**Lisp** in boot.lisp (pure computation — ideal for Lisp):
- `decode-universal-time` → 9 values via arithmetic (sec, min, hour, date, month, year, dow, dst, tz)
- `encode-universal-time` → universal time from components via arithmetic
- `get-decoded-time` → `(decode-universal-time (get-universal-time))`
- `pathname-name`, `pathname-type`, `pathname-directory` — string parsing with `position`, `subseq`
- `make-pathname` — string construction from components
- `merge-pathnames` — combine defaults with specified components
- `namestring` — identity (pathnames are strings)
- `truename` — identity for now (no symlink resolution)
- `enough-namestring` — relative path computation
- `ensure-directories-exist` — parse path, call `%mkdir` for each component

**Files:** `builtins_stream.c` (or new `builtins_file.c`), `lib/boot.lisp`, `platform.h`, `platform_posix.c`, `platform_amiga.c`
**Tests:** Time round-trip, pathname parsing, probe-file on existing/nonexistent files

---

## Step 11: CLI Options + User Init File

- `--load <file>` — load Lisp file before REPL (multiple allowed)
- `--eval <expr>` — evaluate expression before REPL (multiple allowed)
- `--script <file>` — load file and exit (no REPL)
- `--no-userinit` — skip user init file
- User init: `~/.clamigarc` (POSIX) / `S:clamiga.lisp` (Amiga), loaded after boot.lisp

**Files:** `main.c`, `repl.c/h`
**Tests:** Manual testing (CLI args), verify --help updated

---

## Step 12: Reader Macros + `compile`

- `*readtable*` — readtable object (can be a simple struct, not necessarily a heap type)
- `set-macro-character`, `get-macro-character`
- `set-dispatch-macro-character`, `get-dispatch-macro-character`
- `copy-readtable`
- Refactor reader's hardcoded char dispatch to consult readtable
- `compile` — mostly no-op since all functions are already compiled; compile a lambda if given

**Files:** new `readtable.h`, `reader.c`, `builtins_stream.c` or `builtins_io.c`
**Tests:** Custom reader macro, copy-readtable independence, `(compile nil (lambda (x) x))`

---

## Dependency Graph

```
Step 1 → Step 2 → Step 3 → Step 4 → Step 5 ─┬→ Step 6 → Step 9 → Step 12
                                               └→ Step 7 → Step 8 → Step 11
                                        Step 10 (independent after Step 4)
```

Steps 6 and 7 (reader/printer refactoring) can proceed in parallel.
Step 10 (time/pathnames) can proceed independently once Step 4 is done.

## Verification

After each step:
1. `make test` — all host tests pass (currently 716)
2. FS-UAE Amiga build + test (currently 877 tests)
3. New tests added for each step

After full phase:
- `(with-open-file (s "test.txt" :direction :output) (format s "Hello ~A~%" "world"))` writes file
- `(with-open-file (s "test.txt") (read-line s))` → `"Hello world"`
- `(with-output-to-string (s) (format s "~A + ~A = ~A" 1 2 3))` → `"1 + 2 = 3"`
- `#+cl-amiga (print "yes")` prints `"yes"`
- `(get-universal-time)` returns reasonable integer
- `(probe-file "lib/boot.lisp")` → truthy
