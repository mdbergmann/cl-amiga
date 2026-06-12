# CL-Amiga (Clamiga)

[![CI](https://github.com/mdbergmann/cl-amiga/actions/workflows/ci.yml/badge.svg)](https://github.com/mdbergmann/cl-amiga/actions/workflows/ci.yml)

A Common Lisp implementation for AmigaOS 3+, targeting 68020+ processors.

> **Alpha software** — CL-Amiga is under active development. The core language is functional and can run real-world CL libraries, but ANSI CL compliance is incomplete and APIs may change. See [Known Limitations](#known-limitations-and-future-work) for details.

CL-Amiga is a bytecode-compiled Common Lisp environment written in C (C89/C99). It aims for ANSI Common Lisp compatibility and runs on classic Amiga hardware (or emulators like FS-UAE) as well as modern POSIX hosts (macOS, Linux).

## Status

CL-Amiga can load **ASDF**, install and run **Quicklisp**, and successfully quickload libraries including **Alexandria**, **fiveam**, **FSet**, and **Sento** — their `asdf:test-system` suites pass end-to-end. Sento pulls in **lparallel**, **serapeum**, **bordeaux-threads**, **log4cl** and friends along the way.

**ANSI conformance** — the Paul Dietz ANSI test suite (`third_party/ansi-test/`) is the working spec. A bootstrap in `trunk/` runs it on host and Amiga, writing tallies to `build/load-and-test-logs/`:

- **CONS + SYMBOLS + NUMBERS** (`load-and-test-ansi.lisp`) — **4471 / 4471 (100%)** passing (3027 cons + symbols, 1444 numbers).

Over 2240 host tests and ~2525 Amiga tests cover the implementation, including threading, CLOS, conditions, the full numeric tower, FFI, the m68k JIT, and AmigaOS GUI (Intuition/Graphics/GadTools).

## Building

### Host (macOS / Linux)

```
make host          # Build for host (gcc)
make test          # Fast test tier (C unit + shell tests)
make test-plus     # Fast tier + host-cold-test (sento cold-load smoke test)
make test-extra    # Heavyweight trunk integration scripts
make clean         # Remove build artifacts
```

### Pre-commit hook (auto-review + tests)

Optional. A `pre-commit` hook reviews staged changes with a headless `claude`
(auto-fixing issues and re-staging), then runs the fast test tier
(`make test-fast` — no sento) and blocks the commit on failure. Activate once
per clone:

```
make install-hooks
```

Bypass a single commit with `git commit --no-verify`. See
[`scripts/review/README.md`](scripts/review/README.md) for the full flow,
toggles, and safety guarantees.

### Cross-compile for AmigaOS

First, install the `m68k-amigaos-gcc` cross toolchain:

```
./tools/setup-toolchain.sh          # auto-pick: download on macOS arm64, build elsewhere
./tools/setup-toolchain.sh --build   # force build-from-source on any host
./tools/setup-toolchain.sh --help    # all options
```

The toolchain itself is tracked as a git submodule
(`tools/m68k-amigaos-gcc` → [AmigaPorts/m68k-amigaos-gcc](https://github.com/AmigaPorts/m68k-amigaos-gcc),
pinned). On macOS arm64 the script downloads a prebuilt `prefix/` tarball
from the cl-amiga release; on every other host it runs `git submodule
update --init` and invokes the upstream `make all` (host build deps —
`gmp`, `mpfr`, `mpc`, `wget`, etc. — see `tools/m68k-amigaos-gcc/README.md`).

Then build CL-Amiga:

```
make -f Makefile.cross amiga        # Cross-compile with m68k-amigaos-gcc
make -f Makefile.cross test-amiga   # Build, deploy to FS-UAE, run Amiga tests
make -f Makefile.cross clean        # Remove cross-build artifacts
```

### Build inside AmigaOS (vbcc)

```
cd CLAmiga:
make -f Makefile.amiga
```

## Usage

```
./clamiga                      # Start REPL
./clamiga --load hello.lisp    # Same as above
./clamiga --heap 8M            # Start with 8 MB heap
```

### Heap and stack sizing

The default heap is **4 MB**. Larger workloads need more:

| Use case                        | Heap             | Amiga stack       |
|---------------------------------|------------------|-------------------|
| REPL / small programs           | 4M (default)     | 64K (default)     |
| Loading ASDF                    | `--heap 11M`     | 64K (default)     |
| Quicklisp + quickload libraries | `--heap 24M`     | `stack 128000`    |
| FSet (functional collections)   | `--heap 24M`     | `stack 128000`    |
| Fiveam (load + self-tests)      | `--heap 24M`     | `stack 128000`    |

On AmigaOS, the default 64K stack is sufficient for basic use. For Quicklisp/ASDF workloads with deep CLOS dispatch chains, increase the stack:

```
stack 400000
clamiga --heap 24M
```

### Quicklisp

Quicklisp runs on CL-Amiga, but the stock client doesn't recognise this implementation and pulls in libraries that assume features we don't have yet. So the project ships a small compat layer plus three library shims, and keeps the bootstrap entirely on its own side. The goal is to upstream all of it once the remaining API gaps close.

**Installing Quicklisp on a fresh system** (where `~/quicklisp/` — Amiga: `S:quicklisp/` — does not exist yet). Do this once:

```lisp
(require "asdf")
(load "lib/quicklisp-install.lisp")
(cl-amiga-ql:install)                 ; downloads + installs the QL client, patches networking
```

`cl-amiga-ql:install` runs the standard `quicklisp-quickstart:install`, catches the network error it raises (CL-Amiga isn't a registered `ql-impl` yet), loads the compat shim so networking works, and retries the dist install. The shims also need to be on disk: run `make install-shims` once from the repo root to symlink them into `~/quicklisp/local-projects`.

**Using Quicklisp** in any later session, once it is installed:

```lisp
(load #P"~/quicklisp/setup.lisp")
(load "lib/quicklisp-compat.lisp")
(ql:quickload "alexandria")
```

**What we patch** (the local changes shipped with the project):

- `lib/quicklisp-compat.lisp` — routes Quicklisp's networking through `ext:open-tcp-stream` and plain CL stream ops (working around generic-function dispatch limits in the stock `ql-network` interface), supplies a minimal `make-broadcast-stream` for its HTTP layer, adapts `directory-entries` to CL-Amiga's `directory`, and maps the bordeaux-threads v2 surface onto the `MP` package.
- `contrib/shims/` (installed by `make install-shims`) — `closer-mop` (re-exports CL-Amiga's AMOP subset under CLOSER-MOP names), `trivial-cltl2` (the CLtL2 functions serapeum/trivia call), and `trivial-garbage` (weak hash-tables). Downstream libraries `:use` these by name; the shims let them resolve via Quicklisp's local-projects searcher.
- `lib/asdf.lisp` — `#+cl-amiga` adaptations: real binary FASL compile/load for cross-session persistence, AmigaOS path/device handling, and `*asdf-session*` NULL-safety.

**Confirmed working** via `quickload` + `asdf:test-system` (`trunk/run-load-and-test-all.sh`; results land in `build/load-and-test-logs/`):

| Library | Heap  | Result                            |
|---------|-------|-----------------------------------|
| fiveam  | 24M   | 114 / 114                         |
| FSet    | 24M   | 16 / 17 (1 pre-existing failure)  |
| str     | 64M   | 400 / 400                         |
| Sento   | 192M¹ | 533 / 535 (2 timing-flaky)        |

¹ Cold cache, compiling the full dependency tree (~96–128M warm). Sento pulls in **alexandria, serapeum, lparallel, log4cl, bordeaux-threads** and friends, which load and run along the way. On Amiga use `stack 800000`.

### Integration test scripts

Reusable Lisp loaders in `trunk/` that load and exercise third-party libraries on both host and Amiga:

```
./build/host/clamiga --heap 24M  --load trunk/load-and-test-5am.lisp            # Fiveam
./build/host/clamiga --heap 24M  --load trunk/load-and-test-fset.lisp           # FSet
./build/host/clamiga --heap 64M  --load trunk/load-and-test-str.lisp            # str
./build/host/clamiga --heap 192M --load trunk/load-and-test-sento-system.lisp   # Sento (cold cache)
./build/host/clamiga --heap 96M  --load trunk/load-and-test-ansi.lisp           # ANSI cons + symbols + numbers
./build/host/clamiga --heap 256M --load trunk/load-and-test-cffi.lisp           # CFFI backend
./build/host/clamiga --heap 256M --load trunk/load-and-test-drakma.lisp         # drakma HTTP/HTTPS (host only)
./build/host/clamiga --heap 256M --load trunk/load-and-test-hunchentoot.lisp    # Hunchentoot server (host only)
```

`load-and-test-drakma.lisp` is the SSL-enabled HTTP path: it loads **drakma**
with **cl+ssl** (against the host's OpenSSL) over the **usocket** cl-amiga
backend and runs drakma's own fiveam suite. cl-amiga drives drakma as an
HTTP/HTTPS **client**, and that is what the run exercises and passes: plain
HTTP and HTTPS, GET and POST, streamed responses, and **cl+ssl certificate
verification** (the badssl.com `VERIFY.*` tests). cl+ssl works because cl-amiga
now reports the host OS/arch as `*features*` (`:darwin`/`:linux` +
`:arm64`/`:x86-64`), letting its `define-foreign-library` resolve the right
OpenSSL; the usocket backend (`usocket/backend/clamiga.lisp`, in the usocket
fork) maps usocket onto `ext:open-tcp-stream` / `ext:socket-listen`. drakma's
`:decode-content t` (streaming gunzip/inflate of compressed replies) also works:
the **chipz** fork (`mdbergmann/chipz`) has a `#+cl-amiga` Gray-stream branch so
`chipz:make-decompressing-stream` decodes through cl-amiga's Gray streams — the
google.com gzip-decode tests pass (see `trunk/test-chipz-stream.lisp` for a
focused, runnable gunzip example). cl-amiga's Gray streams expose
`gray:stream-read-sequence` / `gray:stream-write-sequence` (the
trivial-gray-streams `(stream sequence start end &key)` signature), so
`read-sequence`/`write-sequence` on a Gray stream dispatch to a single bulk
method instead of looping byte-by-byte: chipz decompresses straight into the
caller's buffer (see `trunk/test-gray-sequence.lisp` for the dispatch tests).
The suite also runs the form-POST/PUT tests, which stand up a local
**hunchentoot server** and POST to it: hunchentoot now runs as a server over
the usocket cl-amiga backend (its acceptor loop drives off `cl:listen` on the
listening socket — see below — and the cl-amiga portability shims in
`trunk/hunchentoot-clamiga.lisp`). The only remaining skipped tests depend on
the flaky httpbin.org service, orthogonal to the client+server goal, and are
skipped with documented reasons (`trunk/drakma-skip-tests.lisp`). It is
**host-only**: it needs a TCP/IP stack and network access, which the
Amiga/FS-UAE test harness lacks.

`load-and-test-hunchentoot.lisp` is the dedicated **server-side** counterpart:
cl-amiga itself *is* the web server. It starts a Hunchentoot `easy-acceptor`
over the usocket cl-amiga backend (so the accept loop runs on a cl-amiga MP
taskmaster thread) and runs Hunchentoot's own built-in confidence suite
(`hunchentoot/test`, `HUNCHENTOOT-TEST:TEST-HUNCHENTOOT`) against it, driving
**drakma** over loopback through cookies, sessions, multipart parameter
decoding, redirection and basic auth. It applies the same
`trunk/hunchentoot-clamiga.lisp` shims and renders its HTML test pages with
**cl-who**. Like the drakma script it is **host-only** (needs loopback TCP/IP).

Getting the session/cookie pages to render exposed and fixed three latent
conformance gaps (regression tests in `tests/test_vm.c`:
`eval_defsetf_long_form`, `eval_string_fns_on_fill_pointer_string`,
`eval_end_of_file_condition`, plus GC-stress coverage in
`tests/test_gc_stress_regression.sh`):

- **Long-form `defsetf`** (CLHS 5.5.5) — `(defsetf access (lambda-list)
  (store-vars) body...)` is now supported; previously only the short form
  worked, so `(setf (session-value x) v)` mis-compiled to a call of the access
  lambda list. The C compiler delegates the long form to the
  `clamiga::%defsetf-long` helper macro, which expands into a
  `define-setf-expander`.
- **String functions accept fill-pointer / adjustable strings** — `string=`,
  `string-equal`, `string<`…, `write-string`, `string-upcase`/`-downcase`/
  `-trim`/`-capitalize` now treat a `(vector character)` with a fill pointer
  (`CL_STRING_VECTOR_P`) as a valid string, via vector-aware `cl_string_length`
  / `cl_string_char_at` / `cl_string_copy`.
- **`end-of-file` condition** — `read-char`/`peek-char`/`read-line`/`read`/
  `read-byte` signal a proper `END-OF-FILE` condition at EOF (new `CL_ERR_EOF`
  + the gray-streams shadows), so `(handler-case … (end-of-file () …))` loops
  (e.g. hunchentoot's url-rewrite) terminate as the standard requires.

`cl:listen` works on socket streams (both connected sockets and listening
sockets): it reports a connected socket as ready when a byte is available and a
listening socket as ready when a client connection is pending, backed by a
non-blocking `platform_socket_data_available` (a zero-timeout `select` on
POSIX / `WaitSelect` via the reactor on Amiga). That is what lets
`usocket:wait-for-input` — and hence hunchentoot's accept loop — work. See
`tests/test_stream.c` (`socket_listen_reports_readiness`) for a runnable
round-trip.

### Loading source and FASL files

CL-Amiga ships a bytecode VM, so `compile-file` writes a `.fasl` and `load` can take either a `.lisp` source or a precompiled `.fasl`.

| Call                       | Behaviour                                                                                                                                                                                  |
|----------------------------|--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `(load "x.lisp")`          | Looks up a cached FASL in the per-user cache (see below) and loads it if its mtime ≥ the source. Otherwise loads the source and **auto-writes** a fresh FASL to the cache for next time.   |
| `(load "x.fasl")`          | Loads that exact file. The per-user cache is **not** consulted — `.fasl` inputs are already-compiled artifacts.                                                                            |
| `(require "name")`         | Searches `lib/name.fasl` and `lib/name.lisp` (and `PROGDIR:lib/...` on Amiga) and picks the FASL when its mtime ≥ source. Used internally for `clos`, `asdf`, etc.                         |
| `(compile-file "x.lisp")`  | Writes to the cache path (= what `compile-file-pathname` returns). `:output-file "x.fasl"` overrides.                                                                                      |

**Per-user cache locations** (keyed by `clamiga` version + FASL format version, so a version bump invalidates everything automatically):

- POSIX: `~/.cache/common-lisp/cl-amiga-<version>-fasl<n>/<source-path>.fasl`
- AmigaOS: `S:cl-amiga/faslcache/<version>-fasl<n>/<source-path>.fasl`

Pre-built `lib/boot.fasl` and `lib/clos.fasl` ship with the binary; on the lower-end 020 baseline this cuts cold boot from ~92 s to ~9 s. To regenerate them after editing `lib/*.lisp`:

```
./build/host/clamiga --non-interactive \
    --eval '(compile-file "lib/boot.lisp" :output-file "lib/boot.fasl")' \
    --eval '(compile-file "lib/clos.lisp" :output-file "lib/clos.fasl")'
```

Note: string literals in `lib/*.lisp` must stay ASCII-only — the Amiga build is compiled without `CL_WIDE_STRINGS` to save RAM and cannot read host FASLs that contain `FASL_TAG_WIDE_STRING`. The writer auto-downgrades all-ASCII wide strings to byte strings; non-ASCII chars in source string literals will fail Amiga boot with a `BAD_TAG` deserialize error. Comments are unaffected.

### Emacs (SLY) integration

CL-Amiga speaks the SLYNK protocol, so you can drive it from Emacs with [SLY](https://github.com/joaotavora/sly) — REPL, completion, `M-.`, the inspector, and the SLDB debugger. This targets the **host** build (`build/host/clamiga`) and needs a SLY checkout whose `slynk/backend/` includes the CL-Amiga backend (`clamiga.lisp`).

clamiga comes up exactly like every other implementation — there is no clamiga-specific Lisp startup file or init form. The backend (`slynk/backend/clamiga.lisp`) pulls in clamiga's Gray streams itself via `(require "gray-streams")`, so just run clamiga from its source root (or set SLY's `:directory`) so that resolves `lib/gray-streams.lisp`.

> **Heap sizing:** the 4 MB default thrashes the GC once SLYNK and its contribs load. Use **`--heap 96M` as a practical minimum** — that also carries a real application's dependency graph (e.g. `(asdf:load-system :sento)`). Give more headroom (`512M`) if you can.

#### Method A — auto-start with `M-x sly` (recommended)

Add a `clamiga` entry to `sly-lisp-implementations`. No custom `:init` is needed — `:directory` points clamiga at the source root so the backend's `(require "gray-streams")` resolves:

```elisp
(defvar my/clamiga-root "/path/to/cl-amiga")
(defvar my/clamiga-bin  (expand-file-name "build/host/clamiga" my/clamiga-root))

(with-eval-after-load 'sly
  (add-to-list 'sly-lisp-implementations
               `(clamiga (,my/clamiga-bin "--heap" "512M")
                         :directory ,my/clamiga-root)))
```

Then `M-x sly` and pick `clamiga` (or `C-u M-x sly` to choose). SLY starts a server on an OS-assigned port (via ASDF + `slynk.asd`, which includes the CL-Amiga backend) and connects automatically.

#### Method B — external server + `M-x sly-connect`

Start a server in a terminal, then connect to it (useful to keep the image alive across reconnects). A launcher ships with cl-amiga:

```sh
# From the cl-amiga repo root:
SLY_SLYNK_DIR=/path/to/sly/slynk \
  ./tools/sly/clamiga-slynk.sh                 # defaults: port 4005, heap 96M
# CLAMIGA_PORT=4006 and a trailing `--heap 192M' etc. also work.
```

It runs clamiga from the source root (so Gray streams resolve), loads `slynk-loader`, starts a server on the chosen port, and holds stdin open with `tail -f /dev/null` (otherwise the REPL reads EOF and exits, taking the server thread with it). Then in Emacs:

```
M-x sly-connect RET 127.0.0.1 RET 4005 RET
```

The equivalent by hand, without the script:

```sh
cd /path/to/cl-amiga
tail -f /dev/null | ./build/host/clamiga --heap 96M \
    --eval '(load "/path/to/sly/slynk/slynk-loader.lisp")' \
    --eval '(funcall (read-from-string "slynk-loader:init"))' \
    --eval '(funcall (read-from-string "slynk:create-server") :port 4005 :dont-close t)'
```

## AmigaOS Native GUI

CL-Amiga provides Lisp bindings for Intuition, Graphics, and GadTools — loaded on demand via `require` with zero binary size impact. A generic FFI layer (`FFI` package) provides foreign memory access on all platforms; the `AMIGA` package adds register-based library call dispatch via a 68k assembly trampoline.

### Opening a Window

```lisp
(require "amiga/intuition")
(require "amiga/graphics")

(amiga.intuition:with-window (win :title "Hello Amiga"
                                   :width 320 :height 200
                                   :idcmp (logior amiga.intuition:+idcmp-closewindow+
                                                  amiga.intuition:+idcmp-vanillakey+))
  (let ((rp (amiga.intuition:window-rastport win)))
    (amiga.gfx:set-a-pen rp 1)
    (amiga.gfx:move-to rp 20 40)
    (amiga.gfx:gfx-text rp "Hello from CL-Amiga!")
    (amiga.gfx:draw-line rp 20 50 300 50)
    ;; Wait for close gadget
    (amiga.intuition:event-loop win
      (#.amiga.intuition:+idcmp-closewindow+ (msg) (return)))))
```

### GadTools Gadgets

```lisp
(require "amiga/gadtools")

(amiga.intuition:with-pub-screen (scr)
  (amiga.gadtools:with-visual-info (vi scr)
    (amiga.gadtools:with-gadgets (glist ctx vi)
      (amiga.gadtools:create-gadget
        amiga.gadtools:+button-kind+ ctx vi
        :left 20 :top 30 :width 120 :height 16
        :text "Click Me" :gadget-id 1)
      (amiga.intuition:with-window (win :title "GadTools Demo"
                                         :width 320 :height 100
                                         :idcmp (logior amiga.intuition:+idcmp-closewindow+
                                                        amiga.gadtools:+buttonidcmp+))
        (amiga.intuition:add-gadget-list win
          (ffi:make-foreign-pointer (ffi:peek-u32 glist)))
        (amiga.gadtools:gt-refresh-window win)
        (amiga.intuition:event-loop win
          (#.amiga.intuition:+idcmp-closewindow+ (msg) (return))
          (#.amiga.intuition:+idcmp-gadgetup+ (msg)
            (format t "Button clicked!~%")))))))
```

### Raw FFI Access

When the abstractions aren't enough, drop to raw library calls:

```lisp
(require "ffi")

;; Call any AmigaOS library function by offset and register spec
(let ((dos (amiga:open-library "dos.library" 36)))
  ;; Delay(ticks) — dos.library offset -198, d1 = ticks
  (amiga:call-library dos -198 (list :d1 50))
  (amiga:close-library dos))
```

### Host FFI: dlopen + libffi + CFFI

On the POSIX dev host the `FFI` package additionally provides a real,
general-purpose foreign-function engine — dynamic library loading
(`ffi:load-library`/`ffi:symbol-pointer` via `dlopen`/`dlsym`), arbitrary C
calls with full argument/return marshaling (`ffi:call-foreign`, libffi-backed,
incl. variadics), Lisp-as-C callbacks (`ffi:make-callback`, libffi closures),
and typed memory access (`ffi:peek-i8/i16/i32/u64/i64/single/double/pointer`
and the matching `poke-*`).

```lisp
;; Resolve and call libc directly
(ffi:call-foreign (ffi:symbol-pointer "pow") :double '(:double :double) '(2d0 10d0))
;; => 1024.0d0
```

On top of this engine cl-amiga ships a **CFFI** backend (`cffi-clamiga.lisp`,
in the CFFI source tree), so the standard CFFI API — `defcfun`,
`foreign-funcall`, `mem-ref`, `defcallback`, `defcstruct`, foreign strings —
works on the host. This is what lets CFFI-dependent Quicklisp systems load.
Foreign calls/callbacks are host-only; on AmigaOS use the library-vector model
(`AMIGA.FFI`) instead. See `tests/test_ffi.c` and
`trunk/load-and-test-cffi.lisp` for runnable end-to-end examples.

### Available Amiga Modules

| Module | Package | Description |
|--------|---------|-------------|
| `(require "ffi")` | `FFI` | Foreign pointers, typed peek/poke, defcstruct (all platforms); dlopen/libffi calls + callbacks (host) |
| `(require "amiga/ffi")` | `AMIGA.FFI` | Tag lists, defcfun, with-library (AmigaOS) |
| `(require "amiga/intuition")` | `AMIGA.INTUITION` | Windows, screens, IDCMP events, public screens |
| `(require "amiga/graphics")` | `AMIGA.GFX` | Drawing: lines, rectangles, text, pen control |
| `(require "amiga/gadtools")` | `AMIGA.GADTOOLS` | Gadgets, menus, bevel boxes, VisualInfo |

## JIT (m68k)

On the AmigaOS build (68020+), CL-Amiga translates bytecode functions to native m68k machine code at definition time. The VM dispatcher jumps straight into the native body instead of interpreting bytecode. The translator (a single-pass bytecode walker) covers a broad core of the instruction set: integer arithmetic and comparisons (with fixnum fast paths), branches, `cons`/`car`/`cdr`/`rplaca`/`rplacd`/list building, struct slot access, function calls and self-recursive tail calls, closures, multiple-value flow, non-local exits (`block`/`return-from`, `catch`/`throw`, `unwind-protect`, `tagbody`/`go`, handlers/restarts), dynamic binding, `&key` parameters, and AmigaOS FFI (`amiga-call`). Opcodes it doesn't handle yet — and functions with `&optional`/`&rest` lambda lists or frames too large for a 16-bit displacement — fall back to the interpreter transparently.

The JIT is on by default. Pass `--no-jit` to keep functions bytecode-only (useful for A/B benchmarks or isolating a bug); at runtime `(clamiga::%jit-set-active nil|t)` toggles the flag around individual `defun`s. On host builds the JIT is compiled out entirely — its entry points become inline no-ops.

### Performance

Measured on the high-end FS-UAE config (A4000 / 68040 / Picasso96). The A/B microbenchmarks in `trunk/bench-jit-loop.lisp` run identical function bodies with the JIT toggled via `%jit-set-active`, so only the dispatch path differs:

| Benchmark     | Shape                          | Bytecode |   JIT  | Speedup |
|---------------|--------------------------------|---------:|-------:|--------:|
| `sum-to`      | `tagbody`/`go` fixnum loop     |   400 ms |  20 ms |  20.0×  |
| `struct-loop` | 2× struct-slot read per iter   |   260 ms |  20 ms |  13.0×  |
| `arith-chain` | chained binary ops             |   300 ms |  40 ms |   7.5×  |
| `call-loop`   | `OP_CALL` inside the loop body |   340 ms | 240 ms |  1.42×  |

Compute-bound code sees the largest wins; call-heavy code is bounded by the same per-call helper round-trip the interpreter pays. On the real-world `examples/gfx/bouncing-lines.lisp` demo (FFI-dominated — five lines drawn through `graphics.library` each frame), the JIT now reaches **~615 FPS** versus **~500 FPS** on the bytecode VM. That lead only materialised once native `amiga-call` dispatch and `defcfun` compiler-macro inlining landed (467 → 525 → 615 FPS as those merged), since the frame time is mostly FFI calls rather than arithmetic. The remaining gap to compiled ACE BASIC (~1900 FPS through the same ROM graphics calls) is the structural cost of a dynamic, GC'd, tagged-value language — per-argument unboxing, dispatch and symbol lookup per call, GC safepoints — not codegen.

The Amiga test suite passes **2525 / 2525** on the JIT config; per-opcode JIT coverage (counter-bump, value-correctness, and unwind-recovery assertions) lives in `tests/amiga/test-jit.lisp`.

## Architecture

- **Single-pass compiler** from S-expressions to bytecode, executed by a stack-based VM
- **Tagged 32-bit values** (`CL_Obj = uint32_t`) — heap pointers are arena-relative byte offsets
- **Memory-efficient** — bump allocator with free-list fallback, mark-and-sweep GC with sliding compaction (auto-triggered when fragmentation blocks an allocation that a normal GC couldn't satisfy); designed for 68020 @ 14 MHz with 8 MB RAM
- **Platform abstraction** — all OS calls go through `platform.h` (POSIX and AmigaOS implementations)
- **FFI** — generic foreign pointer type + peek/poke (all platforms); 68k assembly trampoline for AmigaOS register-based library calls
- **Threading** (MP package) — kernel threads, per-thread dynamic bindings (TLV), locks, named condition variables, thread interruption/destruction, type predicates; stop-the-world GC with safepoints; POSIX pthreads (with `__thread`-backed TLS) and AmigaOS processes/SignalSemaphores
- **TCP networking** — BSD sockets (POSIX) and bsdsocket.library (AmigaOS)

## Known Limitations and Future Work

- **Alpha status** — the core language works well enough to run real CL libraries, but corners of the ANSI CL spec remain unimplemented (logical pathnames, some `defstruct` options, full CLOS MOP)
- **Amiga GUI bindings are incomplete** — the Intuition/Graphics/GadTools abstractions cover common use cases (windows, drawing, gadgets, menus) but not the full API surface; more libraries (ASL requesters, Layers, Commodities) are not yet wrapped
- **Composite streams** — `make-two-way-stream` is implemented (with `two-way-stream-input-stream` / `two-way-stream-output-stream`); `make-broadcast-stream` and `make-concatenated-stream` are not yet implemented (`make-broadcast-stream` has a minimal workaround in `lib/quicklisp-compat.lisp`)
- **Threading** — `MP` package covers the core bordeaux-threads surface (threads with `interrupt`/`destroy`, mutex + recursive locks, named condition variables with timeout, `with-lock-held` / `with-recursive-lock-held`, type predicates). `(ql:quickload :bordeaux-threads)` and Quicklisp itself currently rely on local patches we ship — `lib/quicklisp-compat.lisp` (maps the BT v2 surface onto `MP`, adapts Quicklisp's network/HTTP layer) plus the shims in `contrib/shims/` symlinked into `~/quicklisp/local-projects` by `make install-shims`; the plan is to upstream these once the remaining API gaps close. Not yet covered: semaphores, atomic integers, `with-timeout`, `:timeout` on `acquire-lock`
- **ANSI CL gaps** — while major subsystems work (CLOS, conditions, packages, the full numeric tower, arrays, pathnames, streams, loop, format), some corners of the spec remain unimplemented

## TODO

- **CAS (compare-and-swap)** — atomic CAS primitive for lock-free data structures; on Amiga can possibly stay with lock-based implementation
- **Upstream `bordeaux-threads` and Quicklisp patches** — close the remaining `MP`/BT v2 API gaps (semaphores, atomic integers + place macros, `with-timeout`, `:timeout` on `acquire-lock`, `native-lock-p` / `native-recursive-lock-p` / `recursive-lock-p`) so the local-projects shim becomes an `impl-cl-amiga.lisp` mergeable upstream; same for the Quicklisp network/HTTP adaptations currently in `lib/quicklisp-compat.lisp`
- **Native MorphOS version** — PowerPC native build targeting MorphOS
- **Bignum performance** — optional GMP backend for faster arbitrary-precision arithmetic

## Project Structure

```
src/
  core/           Compiler, VM, builtins, GC, types, reader, printer, conditions
    builtins_*.c      Builtin functions, split by domain (arith, array, lists,
                      stream, format, hashtable, thread, pathname, ...)
    builtins_ffi.c    FFI package (platform-independent)
    builtins_amiga.c  AMIGA package (AmigaOS only)
    vm.c / compiler.c S-expr → bytecode compiler and stack VM
    mem.c             Arena allocator + mark-and-sweep / compacting GC
    fasl.c            FASL (compiled-file) reader/writer
  jit/            m68k JIT — bytecode→native translator (AmigaOS only)
    codegen_m68k.c    Single-pass bytecode walker → m68k machine code
    asm_m68k.c        m68k instruction encoder
    codebuf.c         Executable code buffer management
    runtime.c         JIT runtime helpers (calls, NLX, GC safepoints)
  platform/       OS abstraction (platform.h)
    platform_posix.c / platform_amiga.c          Files, I/O, time, sockets
    platform_thread_posix.c / _amiga.c           Threads, locks, atomics, TLS
    ffi_dispatch_m68k.s                          68k asm trampoline for library calls
  main.c          Entry point and REPL
include/
  clamiga.h       Public embedding header
lib/
  boot.lisp       Standard library bootstrap (+ prebuilt boot.fasl)
  clos.lisp       CLOS implementation (+ prebuilt clos.fasl)
  ffi.lisp        FFI utilities (defcstruct, with-foreign-alloc)
  gray-streams.lisp   Gray streams protocol
  asdf.lisp       ASDF (Another System Definition Facility, with CL-Amiga adaptations)
  quicklisp*.lisp Quicklisp install + compatibility shims
  amiga/          AmigaOS Lisp libraries (loaded on demand)
    ffi.lisp        Tag lists, defcfun, with-library
    intuition.lisp  Windows, screens, IDCMP events
    graphics.lisp   Drawing, text rendering
    gadtools.lisp   GadTools gadgets, menus
contrib/
  shims/          closer-mop / trivial-cltl2 / trivial-garbage shims for Quicklisp
examples/
  gfx/            Graphics demos (bouncing-lines.lisp)
tests/
  test_*.c        Host test suites (C)
  amiga/          Amiga test suite (Lisp, ~2525 tests)
trunk/            Integration test scripts (ANSI, Sento, FSet, fiveam, str, ...)
third_party/
  ansi-test/      Paul Dietz ANSI CL conformance test suite
specs/            Design notes (JIT, MOP, native backend, performance, ...)
scripts/
  review/         Pre-commit auto-review + test hook
githooks/         Git hooks installed by `make install-hooks`
tools/
  setup-toolchain.sh   m68k-amigaos-gcc cross toolchain installer
  m68k-amigaos-gcc/    Cross toolchain (git submodule)
  sly/                 SLY/SLYNK launcher scripts
verify/
  realamiga/      FS-UAE configuration and AmigaOS disk image
```

## License

Licensed under the Apache License, Version 2.0. See [LICENSE](LICENSE) for details.
