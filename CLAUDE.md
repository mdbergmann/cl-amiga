# CL-Amiga

Common Lisp environment for AmigaOS 3+ with bytecode VM, written in C (C89/C99).

## Target Platforms

- **Primary target**: AmigaOS 3+, 68020+ CPU
- **Development host**: macOS / Linux

## Build

```
make host          # Build for host (gcc)
make test          # Run all tests (mandatory — must pass before committing)
make clean         # Remove build artifacts
```

Cross-compile for Amiga and test via FS-UAE:
```
make -f Makefile.cross amiga        # Cross-compile to build/cross/clamiga (m68k-amigaos-gcc)
make -f Makefile.cross test-amiga   # Cross-compile, copy binary, launch FS-UAE, verify results
make -f Makefile.cross clean        # Remove cross-build artifacts
```
- Uses `m68k-amigaos-gcc` toolchain from `tools/m68k-amigaos-gcc/prefix`
- **Preferred method** for building the Amiga binary — faster than compiling inside the emulator with vbcc
- `test-amiga` places the binary in `build/amiga/`, boots FS-UAE, runs the Amiga test suite, and verifies results
- FS-UAE must be closed manually after tests finish; results are checked automatically

## Architecture

- `CL_Obj` = `uint32_t` tagged value; heap pointers are **arena-relative byte offsets** (not raw pointers)
- Single arena, bump allocator with free-list fallback, mark-and-sweep GC
- Single-pass compiler: S-expressions → bytecode; stack-based VM
- All OS calls go through `platform.h` (`platform_posix.c` / `platform_amiga.c`)

## Coding Guidelines

- **Memory/CPU efficiency is critical** — target is 68020 @ 14MHz with 8MB RAM
- All structs must work at 32-bit — no `size_t` or pointer-sized fields in heap objects
- Use `uint32_t`/`int32_t` explicitly, not `int` or `long` for sized data
- C89/C99 compatible — no C11+ features

## Tests

- **Tests are our specification** — every new feature or bugfix must have both host and Amiga tests
- **Every bug fix must include a regression test** that reproduces the bug and verifies the fix
- Host tests: `tests/test_*.c` using framework in `tests/test.h`; `make test` must pass before any commit
- Amiga tests: `tests/amiga/run-tests.lisp` — Lisp-based test suite run on AmigaOS via FS-UAE

## Usability

- **Clear error, warning, and info messages from the compiler and runtime are very important**
- Strive for helpful, precise diagnostics that guide the user to the problem and solution

## Reference

- **Common Lisp HyperSpec**: https://www.lispworks.com/documentation/HyperSpec/Front/
