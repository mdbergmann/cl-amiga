# CL-Amiga Modularization

## Motivation

On the 8MB Amiga target, memory is precious. Not every program needs CLOS, the condition system, or sequence functions. Modularization allows:
- Smaller binaries for constrained systems
- Faster startup (load only what's needed)
- Incremental feature adoption (start minimal, grow as needed)

## Current Architecture

The codebase already has natural module boundaries:

```
src/core/
├── builtins.c              # Core list/predicate/higher-order (master init)
├── builtins_arith.c        # Arithmetic (+, -, *, /, mod, abs, max, min)
├── builtins_io.c           # I/O (print, format, load, disassemble)
├── builtins_mutation.c     # Mutation (rplaca, rplacd, aref, svref)
├── builtins_strings.c      # String operations (26 functions)
├── builtins_lists.c        # List utilities (15 functions)
├── builtins_hashtable.c    # Hash tables (7 functions)
├── builtins_sequence.c     # Sequence functions part 1 (19 functions)
├── builtins_sequence2.c    # Sequence functions part 2 (9 functions)
├── builtins_type.c         # Type system (typep, coerce, subtypep, deftype)
├── builtins_condition.c    # Condition system (signal, warn, error, restarts)
├── builtins_package.c      # Package operations (20 functions)
├── compiler.c              # Compiler core
├── compiler_special.c      # Special form compilation
├── compiler_extra.c        # Additional special forms
├── vm.c                    # Bytecode VM
├── mem.c                   # Arena allocator + GC
├── reader.c                # S-expression reader
├── printer.c               # Object printer
├── symbol.c                # Symbol interning
├── package.c               # Package system
├── error.c                 # Error handling
└── repl.c                  # REPL loop
```

Each builtin module follows the pattern:
- Static helper functions and builtins (`bi_foo`)
- Static keyword/symbol caches (initialized once)
- Public `cl_builtins_*_init(void)` that registers all functions via `defun()`

Master init in `builtins.c`:
```c
void cl_builtins_init(void) {
    /* core builtins registered here */
    cl_builtins_arith_init();
    cl_builtins_io_init();
    cl_builtins_mutation_init();
    cl_builtins_strings_init();
    cl_builtins_lists_init();
    cl_builtins_hashtable_init();
    cl_builtins_sequence_init();
    cl_builtins_sequence2_init();
    cl_builtins_type_init();
    cl_builtins_condition_init();
    cl_builtins_package_init();
}
```

## Obstacles to Modularization

### Global State Coupling
- `macro_table`, `setf_table`, `type_table` — global alists in compiler.c
- `cl_handler_stack[64]`, `cl_dyn_stack[256]` — global arrays
- `cl_heap` — single arena for all objects

### GC / Type System Coupling
- `gc_mark_children()` in mem.c must know all heap types
- `cl_print()` in printer.c must know how to print all types
- `typep_check()` in builtins_type.c must handle all types
- Adding a new type means touching multiple modules

### Single Binary
- All .o files linked statically into one executable
- No dynamic loading mechanism (no dlopen/LoadSeg integration)
- No way to unload a module's objects from the arena

### Symbol Table
- All symbols interned globally; no way to cleanly remove a module's symbols
- Well-known symbols (SYM_*, KW_*) are static globals initialized at startup

## Modularization Levels

### Level 1: Lisp Packages (Done)

Namespace isolation via CL package system. `defpackage`, `in-package`, `export`, `import` all work. This is the standard CL modularization mechanism.

### Level 2: Load on Demand (Partially Done)

`load` works for .lisp files. Missing pieces:
- `require` / `provide` — conditional loading (Phase 11)
- Autoload mechanism — load module on first reference to an undefined function
- System registry — map system names to file paths

### Level 3: FASL Files (Phase 11)

`compile-file` serializes bytecode + constants to disk. Loads much faster than re-parsing and re-compiling source. Needs:
- Bytecode serialization format (portable between host and Amiga)
- Constant pool serialization (symbols, strings, numbers, lists)
- Source location preservation
- Compatibility versioning

### Level 4: Build-Time Feature Selection

Compile-time `#ifdef` guards to exclude optional modules from the binary:

```c
/* In builtins.c */
void cl_builtins_init(void) {
    /* Always present */
    cl_builtins_arith_init();
    cl_builtins_io_init();
    cl_builtins_mutation_init();
    cl_builtins_lists_init();

    /* Optional modules */
    #ifdef CL_FEATURE_STRINGS
    cl_builtins_strings_init();
    #endif
    #ifdef CL_FEATURE_HASHTABLES
    cl_builtins_hashtable_init();
    #endif
    #ifdef CL_FEATURE_SEQUENCES
    cl_builtins_sequence_init();
    cl_builtins_sequence2_init();
    #endif
    #ifdef CL_FEATURE_CONDITIONS
    cl_builtins_condition_init();
    #endif
    #ifdef CL_FEATURE_PACKAGES
    cl_builtins_package_init();
    #endif
}
```

Makefile integration:
```makefile
# Full build (default)
FEATURES = -DCL_FEATURE_STRINGS -DCL_FEATURE_HASHTABLES \
           -DCL_FEATURE_SEQUENCES -DCL_FEATURE_CONDITIONS \
           -DCL_FEATURE_PACKAGES

# Minimal build for constrained systems
FEATURES_MINIMAL = -DCL_FEATURE_STRINGS
```

**Dependencies between modules:**
```
Core (always present):
  builtins.c, builtins_arith.c, builtins_io.c, builtins_mutation.c,
  compiler*.c, vm.c, mem.c, reader.c, printer.c, symbol.c, error.c, repl.c

builtins_strings.c     — no dependencies on other optional modules
builtins_lists.c       — no dependencies on other optional modules
builtins_hashtable.c   — no dependencies on other optional modules
builtins_sequence*.c   — no dependencies on other optional modules
builtins_type.c        — references condition types (soft dependency)
builtins_condition.c   — depends on builtins_type.c
builtins_package.c     — no dependencies on other optional modules
```

**What needs #ifdef guards:**
- `cl_builtins_init()` — conditional init calls
- `gc_mark_children()` — conditional type handling
- `cl_print()` — conditional print handlers
- `typep_check()` — conditional type predicates
- Compiler special forms — conditional dispatch entries

**Estimated savings:**
| Module | Approx Code Size | Functions |
|--------|-----------------|-----------|
| Sequences | ~15KB | 28 functions |
| Strings | ~8KB | 26 functions |
| Hash tables | ~5KB | 7 functions |
| Conditions | ~10KB | 15 functions |
| Packages | ~8KB | 20 functions |
| Type system | ~8KB | typep, coerce, subtypep, deftype |

Full binary ~100KB → minimal binary ~45KB (rough estimate).

### Level 5: Dynamic Module Loading (Future)

Load compiled C modules at runtime:
- POSIX: `dlopen()` / `dlsym()`
- AmigaOS: `LoadSeg()` or `.library` mechanism
- Each module exports a single `cl_module_init(CL_Runtime *rt)` entry point
- Runtime struct provides `defun`, `intern`, `alloc` function pointers (stable ABI)

**Challenges:**
- Stable ABI between core and modules
- GC must be able to mark objects from dynamically loaded modules
- Module unloading requires tracking which objects belong to which module
- AmigaOS `LoadSeg` loads relocatable hunks — need to define hunk format for modules

**Not recommended for initial implementation.** The complexity is high and the benefit over Level 4 is marginal for the target use case.

### Level 6: Unloadable Modules (Not Planned)

Full load/unload with memory reclamation. Would require:
- Per-module sub-arenas or generation tracking
- Reference counting or ownership tracking between modules
- Symbol uninterning and function unbinding on unload

This is impractical with the current single-arena GC design and is not planned.

## Recommendations

### Near Term (Phase 6-8)
1. **Level 2 completion**: Add `require`/`provide` (simple — conditional `load`)
2. **Level 4 groundwork**: Add `#ifdef CL_FEATURE_*` guards to `cl_builtins_init()` and the GC/printer/typep dispatch points. Low effort, immediate benefit for Amiga builds.

### Medium Term (Phase 11)
3. **Level 3**: FASL file format and `compile-file`. This is the highest-value modularization for real-world use.
4. **Level 4 completion**: Full build-time feature matrix with Makefile integration and documented dependencies.

### Long Term
5. **Level 5**: Consider dynamic loading only if there's a concrete need (e.g., FFI libraries, optional GUI bindings). The AmigaOS `.library` mechanism is a natural fit but requires significant infrastructure.

## ASDF Implications

ASDF (the ultimate validation target) requires:
- `compile-file` (Level 3)
- `require` / `provide` (Level 2)
- `*features*` for feature conditionals (already on roadmap, Phase 7)
- Logical pathnames (Phase 11)

ASDF does NOT require dynamic C module loading (Level 5), so that can remain a stretch goal.
