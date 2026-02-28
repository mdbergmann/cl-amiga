# CL-Amiga

Common Lisp environment for AmigaOS 3+ with bytecode VM, written in C (C89/C99).

## Target Platforms

- **Primary target**: AmigaOS 3+, 68020+ CPU, minimum 8MB RAM
- **Architectures**: m68k (32-bit) and PPC (32-bit)
- **Development host**: Linux x86-64

## Build

```
make host          # Build for Linux host (gcc)
make test          # Run all tests (mandatory — must pass before committing)
make clean         # Remove build artifacts
```

Cross-compilation (step 13, not yet set up):
- `verify/otherthenamiga/aos3/` contains AmigaOS system image with vbcc/SDKs
- `verify/otherthenamiga/verify.fs-uae` is the FS-UAE emulator config
- Plan to use bebbo's amiga-gcc for cross-compilation

## Architecture

### Object Representation
- `CL_Obj` = `uint32_t` tagged value
- Heap pointers are **arena-relative byte offsets** (not raw pointers) — this makes the 32-bit representation work on both 64-bit host and 32-bit Amiga
- `cl_arena_base` global converts between offsets and real pointers
- NIL = 0, Fixnum = bit 0 set, Character = tag 0x0A, Heap = low 2 bits 00

### Memory
- Single 4MB arena, bump allocator with free-list fallback after GC
- Mark-and-sweep GC with explicit mark stack (no C stack recursion)
- `CL_GC_PROTECT`/`CL_GC_UNPROTECT` for root protection

### Execution
- Single-pass recursive compiler: S-expressions → bytecode
- Stack-based bytecode VM with 35 opcodes
- Tail call optimization, closure support
- Bytecode/constants pools allocated via `platform_alloc` (outside GC arena)

### Platform Abstraction
All OS calls go through `platform.h`. Implementations:
- `platform_posix.c` — Linux (malloc, stdio)
- `platform_amiga.c` — AmigaOS (AllocVec, dos.library) — not yet written

## Coding Guidelines

- **Memory/CPU efficiency is critical** — target is 68020 @ 14MHz with 8MB RAM
- Keep allocations small, avoid unnecessary copies, minimize heap pressure
- No floating point in core (68020 has no FPU; use fixnum arithmetic)
- All structs must work correctly at 32-bit — no `size_t`, `uintptr_t`, or pointer-sized fields in heap objects (except `CL_CFunc` in `CL_Function`)
- Use `uint32_t`/`int32_t` explicitly, not `int` or `long` for sized data
- C89/C99 compatible — no C11+ features, no VLAs except flexible array members
- `let`/`let*` use flat local slots in the current env with `max_locals` tracking (not child envs)

## Tests

- **Tests are mandatory** — every new feature or bugfix must have tests
- Test framework: `tests/test.h` (ASSERT, ASSERT_EQ, ASSERT_EQ_INT, ASSERT_STR_EQ)
- `make test` must pass with zero failures before any commit
- Test files: `tests/test_*.c`, each has its own `main()` and setup/teardown

## Known Limitations (to fix incrementally)

- `defmacro` compiles but runtime macro expansion not wired up
- Closures with captured upvalues are stubbed (closures without free variables work)
- Packages/symbols not permanently GC-rooted (GC shouldn't be triggered during normal REPL use yet)
- No file loading mechanism (boot.lisp can't be loaded yet)
- `mapcar` only works with built-in functions, not compiled ones
