# CL-Amiga

A Common Lisp implementation for AmigaOS 3+, targeting 68020+ processors.

CL-Amiga is a bytecode-compiled Common Lisp environment written in C (C89/C99). It aims for ANSI Common Lisp compatibility and runs on classic Amiga hardware (or emulators like FS-UAE) as well as modern POSIX hosts (macOS, Linux).

## Status

CL-Amiga can load **ASDF**, install and run **Quicklisp**, and successfully quickload and run the **fiveam** test framework (57/57 self-tests passing). The **Alexandria** library loads without issues.

Over 620 host tests cover the implementation.

## Building

### Host (macOS / Linux)

```
make host          # Build for host (gcc)
make test          # Run all tests
make clean         # Remove build artifacts
```

### Cross-compile for AmigaOS

```
make -f Makefile.cross amiga        # Cross-compile with m68k-amigaos-gcc
make -f Makefile.cross test-amiga   # Build, deploy to FS-UAE, run Amiga tests
make -f Makefile.cross clean        # Remove cross-build artifacts
```

Requires the `m68k-amigaos-gcc` toolchain in `tools/m68k-amigaos-gcc/prefix`.

### Build inside AmigaOS (vbcc)

```
cd CLAmiga:
make -f Makefile.amiga
```

## Usage

```
./clamiga                      # Start REPL
./clamiga hello.lisp           # Load and execute a file
./clamiga --load hello.lisp    # Same as above
./clamiga --heap 8M            # Start with 8 MB heap
```

### Heap and stack sizing

The default heap is **4 MB**. Larger workloads need more:

| Use case                        | Heap             | Amiga stack       |
|---------------------------------|------------------|-------------------|
| REPL / small programs           | 4M (default)     | `stack 65000`     |
| Loading ASDF                    | `--heap 8M`      | `stack 65000`     |
| Quicklisp + quickload libraries | `--heap 24M`     | `stack 400000`    |
| Fiveam (load + self-tests)      | `--heap 24M`     | `stack 600000`    |

On AmigaOS, set the process stack before launching (the default 4 KB is too small):

```
stack 400000
clamiga --heap 24M
```

### Loading Quicklisp

```
./clamiga --heap 24M
CL-USER> (load "quicklisp/setup.lisp")
CL-USER> (ql:quickload "alexandria")
CL-USER> (ql:quickload "fiveam")
```

## Architecture

- **Single-pass compiler** from S-expressions to bytecode, executed by a stack-based VM
- **Tagged 32-bit values** (`CL_Obj = uint32_t`) — heap pointers are arena-relative byte offsets
- **Memory-efficient** — bump allocator with free-list fallback, mark-and-sweep GC; designed for 68020 @ 14 MHz with 8 MB RAM
- **Platform abstraction** — all OS calls go through `platform.h` (POSIX and AmigaOS implementations)
- **TCP networking** — BSD sockets (POSIX) and bsdsocket.library (AmigaOS)

## Known Limitations and Future Work

- **Threading** — no threading support yet; green threads are a possible future direction
- **Buffered socket I/O** — network streams currently use byte-at-a-time recv/send, making Quicklisp downloads on Amiga very slow
- **compile-file** — currently copies source as-is; no proper fasl/bytecode file format yet
- **ANSI CL gaps** — while major subsystems work (CLOS, conditions, packages, the full numeric tower, arrays, pathnames, streams, loop, format), some corners of the spec remain unimplemented
- **Amiga networking** — bsdsocket.library integration is partial
- **Full AmigaOS API support** — Intuition, Graphics, and other AmigaOS libraries for native GUI and multimedia applications
- **Native MorphOS port** — planned as a future target platform

## Project Structure

```
src/
  core/           Compiler, VM, builtins, GC, types
  platform/       OS abstraction (platform_posix.c, platform_amiga.c)
  main.c          Entry point and REPL
lib/
  boot.lisp       Standard library bootstrap
  clos.lisp       CLOS implementation
  asdf.lisp       ASDF (Another System Definition Facility)
tests/
  test_*.c        Host test suites (C)
  amiga/          Amiga test suite (Lisp)
verify/
  realamiga/      FS-UAE configuration and AmigaOS disk image
```

## License

Licensed under the Apache License, Version 2.0. See [LICENSE](LICENSE) for details.
