# CL-Amiga: Overall Specification

## Vision

A Common Lisp environment built from scratch for AmigaOS 3+, running on both m68k and PPC hardware. The bytecode VM ensures architecture-agnostic execution — the same compiled Lisp code runs on both platforms. Starting with a minimal core and growing incrementally toward ANSI CL compliance.

## Target Hardware

| Parameter | Minimum | Recommended |
|-----------|---------|-------------|
| CPU | 68020 | 68030+ / PPC 603+ |
| RAM | 8MB fast | 16MB+ |
| OS | AmigaOS 3.0 | AmigaOS 3.1+ |
| Storage | Floppy | Hard drive |

## Key Decisions

| Decision | Choice | Rationale |
|----------|--------|-----------|
| Language | C (C89/C99) | Portable, VBCC/gcc support |
| Execution | Bytecode VM (stack-based) | Architecture-agnostic, compact code |
| Object size | 32-bit tagged values | Fits m68k registers, low memory use |
| Heap pointers | Arena-relative offsets | Works on 32-bit target and 64-bit host |
| GC | Mark-and-sweep | Simple, predictable pauses |
| Allocator | Bump + free-list | Fast allocation, low fragmentation |
| OS integration | Console I/O first, FFI later | Incremental complexity |
| Toolchain | gcc on host, bebbo's amiga-gcc or vbcc for cross | Proven Amiga cross-compilers |
| Testing | Host Linux builds + Amiga emulator (FS-UAE) | Fast iteration + real verification |

## Object Representation

```
CL_Obj (uint32_t):
  xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx1  = Fixnum (31-bit signed, ±1 billion)
  pppppppppppppppppppppppppppppp00  = Heap offset (4-byte aligned, arena-relative)
  cccccccccccccccccccccccc00001010  = Character (24-bit, tag 0x0A)
  00000000000000000000000000000000  = NIL

Heap object header (uint32_t):
  [type:8][gc_mark:1][size:23]       max object size = 8MB

Types: CONS, SYMBOL, STRING, FUNCTION, CLOSURE, BYTECODE, VECTOR, PACKAGE
```

## Memory Budget (8MB System)

| Component | Size |
|-----------|------|
| OS + Workbench | ~1.5MB |
| CL-Amiga binary | ~100KB |
| Heap arena | 4MB (configurable) |
| VM value stack | 64KB (16K entries) |
| VM call stack | 256 frames |
| C stack | 64KB |
| Free for OS | ~2.3MB |

4MB heap ≈ 340K cons cells max, ~100-200K practical working set.

## Bytecode VM

Stack-based, byte-oriented instruction encoding. 35 opcodes:

| Category | Opcodes |
|----------|---------|
| Constants/vars | CONST, LOAD, STORE, GLOAD, GSTORE, UPVAL, NIL, T, FLOAD |
| Stack | POP, DUP |
| List ops | CONS, CAR, CDR |
| Arithmetic | ADD, SUB, MUL, DIV |
| Comparison | EQ, LT, GT, LE, GE, NUMEQ |
| Logic | NOT |
| Control flow | JMP, JNIL, JTRUE |
| Functions | CALL, TAILCALL, RET, CLOSURE, APPLY |
| Misc | LIST, HALT |

## Compiler

Single-pass recursive compiler from S-expressions to bytecode:
- Lexical scope with compile-time environment chain
- Upvalue capture for closures
- Tail position tracking for tail call optimization
- Macro expansion before compilation

**Special forms (Phase 1):** `quote`, `if`, `progn`, `lambda`, `let`, `let*`, `setq`, `defun`, `defmacro`, `function (#')`, `block`, `return-from`

## Built-in Functions (Phase 1: 48 functions)

| Category | Functions |
|----------|-----------|
| Arithmetic | `+` `-` `*` `/` `mod` `1+` `1-` `abs` `max` `min` |
| Comparison | `=` `<` `>` `<=` `>=` |
| Predicates | `null` `consp` `atom` `listp` `numberp` `integerp` `symbolp` `stringp` `functionp` `zerop` `plusp` `minusp` |
| Equality | `eq` `eql` `equal` `not` |
| List ops | `cons` `car` `cdr` `first` `rest` `list` `length` `append` `reverse` `nth` |
| Higher-order | `mapcar` `apply` `funcall` |
| I/O | `print` `prin1` `princ` `terpri` `format` |
| Misc | `type-of` |

## Implementation Phases

### Phase 1: Working REPL ✅ (Current)

Core infrastructure and interactive environment:
- Tagged pointer type system
- Arena allocator + mark-and-sweep GC
- Symbol interning and CL/CL-USER/KEYWORD packages
- S-expression reader (lists, atoms, strings, chars, quote, comments)
- Object printer (prin1/princ)
- Bytecode instruction set and compiler
- Stack-based VM with tail calls
- 48 built-in functions
- Interactive REPL with error recovery
- 86 tests, all passing

### Phase 2: Language Completeness

- [ ] Runtime macro expansion (defmacro works but expansion not wired up)
- [ ] Closure upvalue capture (currently stubbed)
- [ ] Permanent GC roots for packages/symbols
- [ ] File loading (load boot.lisp at startup)
- [ ] `mapcar` with compiled functions
- [ ] `cond`, `case`, `when`, `unless`, `and`, `or` (via macros or special forms)
- [ ] `do`, `dolist`, `dotimes` loops
- [ ] `labels`, `flet` (local function binding)
- [ ] Multiple return values (`values`, `multiple-value-bind`)
- [ ] `&optional`, `&key` lambda list support
- [ ] String operations (`string=`, `subseq`, `concatenate`, etc.)
- [ ] `setf` with generalized places
- [ ] Dynamic variables (`defvar`, `defparameter`, special bindings)

### Phase 3: AmigaOS Integration

- [ ] `platform_amiga.c` implementation (AllocVec, dos.library I/O)
- [ ] Cross-compilation with bebbo's amiga-gcc or vbcc
- [ ] Testing on FS-UAE emulator
- [ ] Testing on real hardware (A1200)
- [ ] Amiga-specific FFI (calling library functions)
- [ ] Intuition/gadtools bindings for GUI (stretch goal)

### Phase 4: Standard Library & Compliance

- [ ] CLOS (subset — defclass, defmethod, defgeneric)
- [ ] Condition system (handler-bind, handler-case, restart-case)
- [ ] Streams (make-string-input-stream, with-output-to-string)
- [ ] Sequences (map, reduce, find, remove, sort)
- [ ] Hash tables
- [ ] Pathnames and file I/O
- [ ] Pretty printer
- [ ] Reader macros
- [ ] Packages (defpackage, use-package, import, export)
- [ ] Declarations and type checking
- [ ] Compiler optimizations (constant folding, inlining)

### Phase 5: Performance & Polish

- [ ] Bytecode optimizer
- [ ] Generational or incremental GC
- [ ] Bignums (arbitrary precision integers)
- [ ] Floats (optional, 68881/68882 FPU or software)
- [ ] Disassembler
- [ ] Debugger / stepper
- [ ] Line editing (history, tab completion)
- [ ] Documentation strings

## Project Structure

```
cl-amiga/
├── CLAUDE.md              # Dev conventions and quick reference
├── Makefile               # Build system
├── specs/
│   └── overview.md        # This file
├── src/
│   ├── main.c             # Entry point
│   ├── core/              # Language implementation (14 modules)
│   └── platform/          # OS abstraction (posix, amiga)
├── include/
│   └── clamiga.h          # Public umbrella header
├── lib/
│   └── boot.lisp          # Bootstrap macros/functions
├── tests/
│   ├── test.h             # Test framework
│   └── test_*.c           # Test suites (4 files, 86 tests)
├── build/                 # Build output (gitignored)
└── verify/                # AmigaOS system image for testing (gitignored)
```

## Verification Targets

1. `(+ 1 2)` → `3`
2. `(defun fact (n) (if (<= n 1) 1 (* n (fact (1- n)))))` then `(fact 10)` → `3628800`
3. `(mapcar #'1+ '(1 2 3))` → `(2 3 4)`
4. Cross-compile to m68k, run in FS-UAE
5. Run on real A1200 hardware
