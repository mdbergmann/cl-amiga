# CL-Amiga (Clamiga)

[![CI](https://github.com/mdbergmann/cl-amiga/actions/workflows/ci.yml/badge.svg)](https://github.com/mdbergmann/cl-amiga/actions/workflows/ci.yml)

A Common Lisp implementation for AmigaOS 3+, targeting 68020+ processors.

> **Alpha software** — CL-Amiga is under active development. The core language is functional and can run real-world CL libraries, but ANSI CL compliance is incomplete and APIs may change. See [Known Limitations](#known-limitations-and-future-work) for details.

CL-Amiga is a bytecode-compiled Common Lisp environment written in C (C89/C99). It aims for ANSI Common Lisp compatibility and runs on classic Amiga hardware (or emulators like FS-UAE) as well as modern POSIX hosts (macOS, Linux).

## Status

CL-Amiga can load **ASDF**, install and run **Quicklisp**, and successfully quickload libraries including **Alexandria**, **fiveam** (57/57 self-tests passing), **FSet** (17/17 tests passing), and **Sento** — the full `(asdf:test-system :sento)` suite runs end-to-end (511/513, two timing-flaky cases), pulling in **lparallel**, **serapeum**, **bordeaux-threads**, **log4cl** and friends along the way.

**ANSI conformance** — the Paul Dietz ANSI test suite is the working spec. Current scores against the upstream suites:

- **CONS chapter** — 1882 / 1882 (100%)
- **SYMBOLS chapter** — 3020 / 3020 (100%)
- **NUMBERS chapter** — work in progress

ANSI bootstraps live in `trunk/load-and-test-ansi*.lisp` and run on host and Amiga.

Over 2240 host tests and ~2250 Amiga tests cover the implementation, including threading, CLOS, conditions, the full numeric tower, FFI, and AmigaOS GUI (Intuition/Graphics/GadTools).

## Building

### Host (macOS / Linux)

```
make host          # Build for host (gcc)
make test          # Run all tests
make clean         # Remove build artifacts
```

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

### Loading Quicklisp

```
./clamiga --heap 24M
CL-USER> (require "asdf")
CL-USER> (load "quicklisp/setup.lisp")
CL-USER> (load "lib/quicklisp-compat.lisp")
CL-USER> (ql:quickload "alexandria")
CL-USER> (ql:quickload "fiveam")
CL-USER> (ql:quickload "fset")
CL-USER> (ql:quickload "sento")    ; pulls in lparallel + serapeum
```

### Integration test scripts

Reusable Lisp loaders in `trunk/` that load and exercise third-party libraries on both host and Amiga:

```
./build/host/clamiga --heap 24M  --load trunk/load-and-test-5am.lisp            # Fiveam
./build/host/clamiga --heap 24M  --load trunk/load-and-test-fset.lisp           # FSet
./build/host/clamiga --heap 64M  --load trunk/load-and-test-str.lisp            # str
./build/host/clamiga --heap 192M --load trunk/load-and-test-sento-system.lisp   # Sento (cold cache)
./build/host/clamiga --heap 96M  --load trunk/load-and-test-ansi.lisp           # ANSI cons + symbols
./build/host/clamiga --heap 96M  --load trunk/load-and-test-ansi-numbers.lisp   # ANSI numbers
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

### Available Amiga Modules

| Module | Package | Description |
|--------|---------|-------------|
| `(require "ffi")` | `FFI` | Foreign pointers, peek/poke, defcstruct (all platforms) |
| `(require "amiga/ffi")` | `AMIGA.FFI` | Tag lists, defcfun, with-library (AmigaOS) |
| `(require "amiga/intuition")` | `AMIGA.INTUITION` | Windows, screens, IDCMP events, public screens |
| `(require "amiga/graphics")` | `AMIGA.GFX` | Drawing: lines, rectangles, text, pen control |
| `(require "amiga/gadtools")` | `AMIGA.GADTOOLS` | Gadgets, menus, bevel boxes, VisualInfo |

## Architecture

- **Single-pass compiler** from S-expressions to bytecode, executed by a stack-based VM
- **Tagged 32-bit values** (`CL_Obj = uint32_t`) — heap pointers are arena-relative byte offsets
- **Memory-efficient** — bump allocator with free-list fallback, mark-and-sweep GC; designed for 68020 @ 14 MHz with 8 MB RAM
- **Platform abstraction** — all OS calls go through `platform.h` (POSIX and AmigaOS implementations)
- **FFI** — generic foreign pointer type + peek/poke (all platforms); 68k assembly trampoline for AmigaOS register-based library calls
- **Threading** (MP package) — kernel threads, per-thread dynamic bindings (TLV), locks, named condition variables, thread interruption/destruction, type predicates; stop-the-world GC with safepoints; POSIX pthreads (with `__thread`-backed TLS) and AmigaOS processes/SignalSemaphores
- **TCP networking** — BSD sockets (POSIX) and bsdsocket.library (AmigaOS)

## Known Limitations and Future Work

- **Alpha status** — the core language works well enough to run real CL libraries, but corners of the ANSI CL spec remain unimplemented (broadcast / two-way / concatenated streams, logical pathnames, some `defstruct` options, full CLOS MOP)
- **Amiga GUI bindings are incomplete** — the Intuition/Graphics/GadTools abstractions cover common use cases (windows, drawing, gadgets, menus) but not the full API surface; more libraries (ASL requesters, Layers, Commodities) are not yet wrapped
- **Threading** — MP package covers core bordeaux-threads API (threads, locks, named condvars, interrupt/destroy-thread, type predicates); some gaps remain (semaphores, atomic integers, `with-timeout`)
- **Buffered socket I/O** — network streams currently use byte-at-a-time recv/send, making Quicklisp downloads on Amiga very slow
- **ANSI CL gaps** — while major subsystems work (CLOS, conditions, packages, the full numeric tower, arrays, pathnames, streams, loop, format), some corners of the spec remain unimplemented

## TODO

- **CAS (compare-and-swap)** — atomic CAS primitive for lock-free data structures; on Amiga can possibly stay with lock-based implementation
- **Full bordeaux-threads support** — remaining API gaps (semaphores, atomic integers, `with-timeout`)
- **Native MorphOS version** — PowerPC native build targeting MorphOS
- **Bignum performance** — optional GMP backend for faster arbitrary-precision arithmetic

## Project Structure

```
src/
  core/           Compiler, VM, builtins, GC, types
    builtins_ffi.c    FFI package (platform-independent)
    builtins_amiga.c  AMIGA package (AmigaOS only)
  platform/       OS abstraction (platform_posix.c, platform_amiga.c)
    ffi_dispatch_m68k.s  68k asm trampoline for library calls
  main.c          Entry point and REPL
lib/
  boot.lisp       Standard library bootstrap
  clos.lisp       CLOS implementation
  ffi.lisp        FFI utilities (defcstruct, with-foreign-alloc)
  asdf.lisp       ASDF (Another System Definition Facility, with CL-Amiga adaptations)
  amiga/          AmigaOS Lisp libraries (loaded on demand)
    ffi.lisp        Tag lists, defcfun, with-library
    intuition.lisp  Windows, screens, IDCMP events
    graphics.lisp   Drawing, text rendering
    gadtools.lisp   GadTools gadgets, menus
tests/
  test_*.c        Host test suites (C)
  amiga/          Amiga test suite (Lisp, ~2250 tests)
trunk/            Integration test scripts (ANSI, Sento, FSet, fiveam, str, ...)
verify/
  realamiga/      FS-UAE configuration and AmigaOS disk image
```

## License

Licensed under the Apache License, Version 2.0. See [LICENSE](LICENSE) for details.
