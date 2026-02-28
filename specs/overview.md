# CL-Amiga: Overall Specification

## Vision

A Common Lisp environment built from scratch for AmigaOS 3+, running on both m68k and PPC hardware. The bytecode VM ensures architecture-agnostic execution — the same compiled Lisp code runs on both platforms. Starting with a minimal core and growing incrementally toward full ANSI CL compliance, with ASDF as the ultimate validation target.

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
| Misc | LIST, HALT, DEFMACRO |

## Compiler

Single-pass recursive compiler from S-expressions to bytecode:
- Lexical scope with compile-time environment chain
- Flat closure model with upvalue capture (value semantics)
- Tail position tracking for tail call optimization
- Macro expansion before compilation
- Backward jump support for loop forms

**Special forms:** `quote`, `if`, `progn`, `lambda`, `let`, `let*`, `setq`, `defun`, `defmacro`, `function (#')`, `block`, `return-from`, `and`, `or`, `cond`, `do`, `dolist`, `dotimes`

**Bootstrap macros:** `when`, `unless`

## Built-in Functions (48 functions)

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

## Implementation Roadmap

The goal is full ANSI CL compliance, validated by running ASDF (~14K lines of CL). Phases are ordered by dependency — each tier builds on the previous.

### Phase 1: Core Infrastructure ✅

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
- AmigaOS platform layer (platform_amiga.c, AllocVec, dos.library I/O)
- Cross-compilation with vbcc, verified on FS-UAE (A4000/68040)

### Phase 2: Language Foundation ✅

Control flow, closures, macros, iteration:
- [x] Runtime macro expansion (`defmacro` with `cl_vm_apply`)
- [x] Closures with captured upvalues (flat closure model, value capture)
- [x] `mapcar`/`funcall`/`apply` with compiled functions and closures
- [x] `and`, `or`, `cond` (compiler special forms)
- [x] `when`, `unless` (bootstrap macros)
- [x] `do`, `dolist`, `dotimes` (compiler special forms with backward jumps)

57 host tests (4 suites), 163 Amiga batch tests — all passing.

### Phase 3: Macro Infrastructure

Quasiquote and tooling that makes real macro writing practical:
- [ ] Quasiquote (`` ` ``/`,`/`,@`) — reader and expander
- [ ] `gensym`, `gentemp` — hygienic macro support
- [ ] File `load` — load .lisp files at runtime
- [ ] Boot file loading (boot.lisp at startup)

### Phase 4: Core Language Completeness

Features needed for idiomatic CL programming:
- [ ] `&optional`, `&key`, `&allow-other-keys` lambda list support
- [ ] `flet`, `labels` — local function bindings
- [ ] `case`, `ecase`, `typecase`, `etypecase`
- [ ] `prog1`, `prog2`
- [ ] `return` (implicit block NIL)
- [ ] `tagbody`/`go` — low-level control flow
- [ ] `catch`/`throw` — dynamic non-local exits
- [ ] `unwind-protect` — cleanup forms
- [ ] `block`/`return-from` — full implementation (currently simplified)
- [ ] Multiple return values (`values`, `multiple-value-bind`, `multiple-value-list`, `multiple-value-prog1`, `nth-value`)
- [ ] Dynamic variables (`defvar`, `defparameter`, special declarations, dynamic binding)
- [ ] `setf` with generalized places (`defsetf`, `define-setf-expander`)
- [ ] Modify macros: `push`, `pop`, `pushnew`, `incf`, `decf`
- [ ] `destructuring-bind`
- [ ] `eval`, `macroexpand`, `macroexpand-1` (user-accessible)
- [ ] `eval-when` — compile-time evaluation control

### Phase 5: Standard Library

Data structures, sequences, strings, and I/O:
- [ ] Hash tables (`make-hash-table`, `gethash`, `remhash`, `maphash`, `clrhash`)
- [ ] Sequence functions (`find`, `find-if`, `remove`, `remove-if`, `remove-if-not`, `remove-duplicates`, `position`, `search`, `count`, `sort`, `stable-sort`, `substitute`, `reduce`, `map`, `every`, `some`, `notany`, `notevery`, `mismatch`)
- [ ] List utilities (`member`, `assoc`, `rassoc`, `intersection`, `union`, `set-difference`, `subsetp`, `adjoin`, `last`, `butlast`, `nthcdr`, `copy-list`, `copy-tree`, `sublis`, `subst`, `acons`, `pairlis`, `getf`)
- [ ] Destructive list ops (`nconc`, `nreverse`, `delete`, `delete-if`, `rplaca`, `rplacd`, `nsubst`)
- [ ] Mapping variants (`mapcan`, `mapc`, `maplist`, `mapl`, `mapcon`)
- [ ] String operations (`string=`, `string-equal`, `string<`, `string>`, `string-upcase`, `string-downcase`, `string-trim`, `string-left-trim`, `string-right-trim`, `subseq`, `concatenate`, `parse-integer`)
- [ ] Character functions (`char=`, `char<`, `char-code`, `code-char`, `upper-case-p`, `lower-case-p`, `alpha-char-p`, `digit-char-p`, `char-upcase`, `char-downcase`)
- [ ] Symbol functions (`symbol-name`, `symbol-value`, `symbol-function`, `symbol-package`, `boundp`, `fboundp`, `fdefinition`, `make-symbol`, `keywordp`)
- [ ] Array operations (`make-array`, `aref`, `vector`, `array-dimensions`, `array-rank`, `fill`, `replace`)
- [ ] Type system (`typep`, `coerce`, `deftype`, `subtypep`)
- [ ] `declare`, `declaim`, `proclaim` — declarations

### Phase 6: Control & Error Handling

Condition system and full package support:
- [ ] Condition system (`define-condition`, `make-condition`, `handler-case`, `handler-bind`, `signal`, `error`, `warn`, `cerror`)
- [ ] Restarts (`restart-case`, `invoke-restart`, `find-restart`, `with-simple-restart`, `abort`, `continue`, `muffle-warning`)
- [ ] `ignore-errors`, `check-type`, `assert`
- [ ] Full packages (`defpackage`, `in-package`, `use-package`, `export`, `import`, `shadow`, `shadowing-import-from`, `find-package`, `make-package`, `rename-package`, `delete-package`, `do-symbols`, `do-external-symbols`, `intern`, `unintern`)

### Phase 7: I/O & Pathnames

File system integration and stream abstraction:
- [ ] Streams (input/output/bidirectional, string streams, broadcast, concatenated, two-way)
- [ ] Stream operations (`read-char`, `write-char`, `peek-char`, `unread-char`, `read-line`, `write-string`, `write-line`, `terpri`, `fresh-line`, `finish-output`, `force-output`)
- [ ] `with-open-file`, `open`, `close`
- [ ] `read`, `read-from-string`, `read-preserving-whitespace`
- [ ] `write`, `write-to-string`, `prin1-to-string`, `princ-to-string`
- [ ] `with-output-to-string`, `with-input-from-string`, `make-string-output-stream`, `get-output-stream-string`
- [ ] `with-standard-io-syntax`
- [ ] Pathnames (`make-pathname`, `merge-pathnames`, `namestring`, `truename`, `pathname-name`, `pathname-type`, `pathname-directory`, `enough-namestring`, `parse-namestring`, `wild-pathname-p`, `translate-pathname`)
- [ ] File operations (`probe-file`, `file-write-date`, `delete-file`, `rename-file`, `ensure-directories-exist`, `directory`)
- [ ] Reader macros (`set-macro-character`, `set-dispatch-macro-character`, `*readtable*`, `copy-readtable`)
- [ ] Feature conditionals (`#+`, `#-`, `*features*`)

### Phase 8: Iteration & Format

Extended iteration and output formatting:
- [ ] `loop` facility (extended LOOP macro: `for`/`in`/`on`/`across`, `collect`/`append`/`nconc`, `when`/`unless`/`if`, `with`/`initially`/`finally`, `thereis`/`always`/`never`, `sum`/`count`/`maximize`/`minimize`)
- [ ] `format` full directives (`~A`, `~S`, `~D`, `~B`, `~O`, `~X`, `~R`, `~%`, `~&`, `~T`, `~<~>`, `~{~}`, `~[~]`, `~?`, `~(~)`, `~*`, `~^`)
- [ ] Pretty printer (`pprint`, `pprint-logical-block`, `pprint-newline`, `pprint-indent`)

### Phase 9: CLOS

Common Lisp Object System:
- [ ] `defclass` — class definition with slots, inheritance, metaclasses
- [ ] Slot options: `:initarg`, `:initform`, `:accessor`, `:reader`, `:writer`, `:allocation`, `:type`, `:documentation`
- [ ] `defgeneric`, `defmethod` — generic function dispatch
- [ ] Method qualifiers: `:before`, `:after`, `:around`, `call-next-method`
- [ ] `make-instance`, `initialize-instance`, `reinitialize-instance`, `shared-initialize`
- [ ] `slot-value`, `slot-boundp`, `slot-exists-p`, `slot-makunbound`
- [ ] `with-slots`, `with-accessors`
- [ ] `find-class`, `class-of`, `class-name`, `change-class`
- [ ] `print-object` — CLOS-based printing
- [ ] `defstruct` — structure definitions (subset or full)
- [ ] Multiple inheritance, standard method combination

### Phase 10: ASDF & Beyond

Validation and ecosystem:
- [ ] Run ASDF 3.3 (~14K lines) — ultimate compliance test
- [ ] `compile-file`, `load`, `require`, `provide`
- [ ] Logical pathnames
- [ ] `documentation` strings
- [ ] Compiler optimizations (constant folding, inlining)
- [ ] Bytecode optimizer
- [ ] Generational or incremental GC
- [ ] Bignums (arbitrary precision integers)
- [ ] Floats (optional, 68881/68882 FPU or software)
- [ ] Disassembler
- [ ] Debugger / stepper
- [ ] Line editing (history, tab completion)
- [ ] Amiga-specific FFI (calling library functions)
- [ ] Intuition/gadtools bindings for GUI (stretch goal)

## Project Structure

```
cl-amiga/
├── CLAUDE.md              # Dev conventions and quick reference
├── Makefile               # Build system (host)
├── Makefile.amiga         # Build system (AmigaOS/vbcc)
├── specs/
│   └── overview.md        # This file
├── src/
│   ├── main.c             # Entry point
│   ├── core/              # Language implementation (12 modules)
│   └── platform/          # OS abstraction (posix, amiga)
├── include/
│   └── clamiga.h          # Public umbrella header
├── lib/
│   └── boot.lisp          # Bootstrap macros/functions
├── tests/
│   ├── test.h             # Test framework
│   ├── test_*.c           # Host test suites (4 files, 57 tests)
│   └── amiga/
│       └── run-tests.lisp # AmigaOS batch tests (163 tests)
├── build/                 # Build output (gitignored)
└── verify/
    └── realamiga/          # FS-UAE config + AmigaOS system image
```

## Verification Targets

1. `(+ 1 2)` → `3` ✅
2. `(defun fact (n) (if (<= n 1) 1 (* n (fact (1- n)))))` then `(fact 10)` → `3628800` ✅
3. `(mapcar #'1+ '(1 2 3))` → `(2 3 4)` ✅
4. Cross-compile to m68k, run in FS-UAE ✅
5. Run on real A1200 hardware
6. Load and execute multi-file Lisp programs
7. `(require "asdf")` — load and run ASDF 3.3
