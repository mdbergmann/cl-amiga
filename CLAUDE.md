# CL-Amiga

Common Lisp environment for AmigaOS 3+ with bytecode VM, written in C (C89/C99).

## Target Platforms

- **Primary target**: AmigaOS 3+, 68020+ CPU
- **Development host**: macOS / Linux

## Build

```
make host          # Build for host (gcc)
make test          # Fast test tier: C unit tests + shell tests (mandatory — must pass before committing)
make test-plus     # Fast tier + host-cold-test (sento cold-load smoke test)
make test-extra    # Heavyweight trunk integration scripts (quicklisp/ansi-tests)
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
- Runs fully unattended: FS-UAE auto-quits (`C:UAEquit`) when the suite finishes, and a host-side watchdog (`verify/realamiga/run-fs-uae.sh`) kills it if clamiga hangs; results are checked automatically

## Architecture

- `CL_Obj` = `uint32_t` tagged value; heap pointers are **arena-relative byte offsets** (not raw pointers)
- Single arena, bump allocator with free-list fallback, mark-and-sweep GC with compaction (moving) when fragmented — see GC Safety below
- Single-pass compiler: S-expressions → bytecode; stack-based VM
- All OS calls go through `platform.h` (`platform_posix.c` / `platform_amiga.c`)
- **Threading** (MP package): kernel threads with per-thread VM, TLV dynamic bindings, stop-the-world GC coordination, locks, condition variables
  - POSIX: pthreads, pthread_rwlock, `__sync_*` atomics
  - AmigaOS: `CreateNewProc()`, `SignalSemaphore` (shared/exclusive), custom condvars via signal bits, `Forbid()`/`Permit()` atomics, `tc_UserData` TLS

## Coding Guidelines

- **All Common Lisp code in the runtime and compiler must conform to the HyperSpec** — when implementing or modifying CL functions, macros, or special forms, consult the [HyperSpec](https://www.lispworks.com/documentation/HyperSpec/Front/) as the authoritative reference
- **Tests must be written and verified against the HyperSpec** — test cases should validate behavior as specified by the standard, not just observed behavior
- **Memory/CPU efficiency is critical** — target is 68020 @ 14MHz with 8MB RAM
- All structs must work at 32-bit — no `size_t` or pointer-sized fields in heap objects
- Use `uint32_t`/`int32_t` explicitly, not `int` or `long` for sized data
- C89/C99 compatible — no C11+ features

### FASL Versioning (Critical)

Whenever you change FASL serialization — or make any change that results in a different on-disk FASL byte layout — you **must** bump `CL_FASL_VERSION` in `src/core/fasl.h`.

- This covers: adding/removing/reordering FASL tags, changing how any object type is serialized, adding new bytecode opcodes or changing operand encoding, changing the bytecode/closure/struct wire format, or anything else that alters the bytes written by the FASL writer.
- Increment the version number and update the trailing comment to describe what changed (see the existing `v10:` comment as the format to follow).
- **Why it matters**: the version is checked on load; bumping it forces stale `.fasl` files to be rejected/recompiled instead of being misread as the new format, which would silently corrupt the VM. Forgetting to bump it is a memory-corruption bug, not a cosmetic one.
- After bumping, run `make fasl` to regenerate the bundled FASLs and run the full test suite (host + Amiga) to confirm load/recompile works.

### GC Safety (Critical)

Any C code that holds `CL_Obj` values across allocating calls **must** GC-protect them:

- **Allocating functions**: `cl_alloc`, `cl_cons`, `cl_make_string`, `cl_make_vector`, `cl_make_struct`, `cl_make_symbol`, and any function that calls these (including `cl_vm_apply`)
- **The pattern to watch for**: iterative list building with `result`/`tail` local variables and `cl_cons()` in a loop — the partially-built list is invisible to GC unless protected
- **Fix**: wrap with `CL_GC_PROTECT(var)` before the loop and `CL_GC_UNPROTECT(n)` after
- **Why it matters**: this is a **compacting (moving) GC** — when fragmentation forces compaction (which can be triggered inside any allocating call), live objects are relocated and arena-relative offsets are rewritten. A `CL_Obj` C local that isn't GC-protected will (a) be swept/freed if unreachable, or (b) hold a stale offset after objects move — either way silently corrupting memory. `CL_GC_PROTECT` registers the variable's address so the compactor forwards it.
- **Note**: values on the VM stack (`cl_vm.stack`) and in `args[]` (builtin function arguments) are already GC-rooted — no need to protect those

## Debugging & Troubleshooting

- **Debug instrumentation** should be guarded by preprocessor flags (e.g. `#ifdef DEBUG_GC`, `#ifdef DEBUG_COMPILER`, `#ifdef DEBUG_VM`) — never leave unconditional debug output in the code
- Activate debug instrumentation via make flags: `make host DEBUG_FLAGS="-DDEBUG_GC -DDEBUG_COMPILER"` (or any combination of flags)
- The Makefile passes `$(DEBUG_FLAGS)` to `CFLAGS`, so any `-D` defines can be added without editing the Makefile
- Keep debug output behind these guards so it compiles to nothing in normal builds — zero overhead when not debugging
- **When fixing bugs and troubleshooting**, maximize the diagnostic and bug-source visibility built into clamiga itself — add clear error messages, runtime checks, assertions, and debug instrumentation so that problems can be diagnosed from clamiga's own output rather than requiring external tools or guesswork

## Tests

- **Tests are our specification** — every new feature or bugfix must have both host and Amiga tests
- **Every bug fix must include a regression test** that reproduces the bug and verifies the fix
- Host tests: `tests/test_*.c` using framework in `tests/test.h`; `make test` must pass before any commit
- Amiga tests: `tests/amiga/run-tests.lisp` — Lisp-based test suite run on AmigaOS via FS-UAE
- **Tests must be tight on production code** — test the exact behavior, not just the happy path; cover edge cases, boundary conditions, and error paths thoroughly
- **Target 90% test coverage** — aim for at least 90% coverage across the codebase
- **The gc-stress test suite is critical for the stability and resiliency of clamiga** (`make test-gc-stress`, forces compaction every alloc via `DEBUG_GC_STRESS`/`CLAMIGA_GC_STRESS=1` to make unprotected-`CL_Obj` GC bugs deterministic). When changing or adding any code, make sure there is coverage not only in the unit tests but **also in the gc-stress suite** — exercise new allocating paths under GC stress so compaction/relocation bugs surface in CI rather than in the field.

## Documentation

- **When adding or changing a feature, check `README.md` for outdated or missing content** and update it as part of the change — the README must not drift from what the code actually does.
- **Keep README changes user-facing and high-level.** Document what a feature is and how to use it. Do **not** add changelog-style notes, fixed-bug descriptions, internal implementation details, root-cause analyses, or other low-level information that isn't relevant to someone using the project. Those belong in commit messages, tests, or memory — not the README.
- For a new feature, add a short section describing how it works. Keep prose minimal: the best documentation is a comprehensive, runnable test file that demonstrates the feature end-to-end. Create such a test file if one doesn't already exist, then have the README point to it (e.g. "see `tests/test_ffi.c` / `tests/amiga/ffi-tests.lisp` for usage examples").
- Prefer linking to authoritative, executable examples over long explanatory text — the test file is the source of truth and stays current because `make test` runs it.

## Usability

- **Clear error, warning, and info messages from the compiler and runtime are very important**
- Strive for helpful, precise diagnostics that guide the user to the problem and solution
- When loading Lisp code and fixing bugs, **improve compiler error messages** — include source location (file, line), the offending form, and actionable guidance where possible

## Running clamiga

- When running `clamiga` to capture output (e.g. for debugging or verification), use **small timeouts (10 seconds)** and check periodically — the process may hang or run indefinitely on certain inputs

## Amiga Stack Requirements

- **64K** (AmigaOS default) — sufficient for the core runtime and most of the test suite, but **~12K short** for the GUI test path: `(require "amiga/gadtools")` nests `bi_load` frames and source-compiles gadtools.lisp (deeply-nested macros → deep `cl_compile` recursion), overflowing 64K and silently corrupting memory. Bisected threshold: 65000 fails, 77000 passes — the test runner uses **77000**.
- **128K** — sufficient for Quicklisp/FSet/fiveam (deep CLOS dispatch chains)

The `stack` CLI command sets the stack before launching clamiga.

## Integration Test Scripts

Reusable Lisp scripts in `trunk/` for loading and testing third-party libraries:

```
./build/host/clamiga --heap 24M --load trunk/load-and-test-5am.lisp    # Fiveam (114/114 tests)
./build/host/clamiga --heap 24M --load trunk/load-and-test-fset.lisp   # FSet (17/17 tests)
./build/host/clamiga --heap 64M --load trunk/load-and-test-str.lisp    # str (399/400 tests)
```

- These scripts work on both host and Amiga (use `#+amigaos`/`#-amigaos` for platform differences)
- On Amiga, use `--heap 48M` and `stack 800000` for quicklisp-based tests

## Reference

- **Common Lisp HyperSpec**: https://www.lispworks.com/documentation/HyperSpec/Front/
