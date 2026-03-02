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

Types: CONS, SYMBOL, STRING, FUNCTION, CLOSURE, BYTECODE, VECTOR, PACKAGE, HASHTABLE, CONDITION
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

Stack-based, byte-oriented instruction encoding. 46 opcodes:

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
| NLX | CATCH, UNCATCH, UWPROT, UWPOP, UWRETHROW |
| Multiple values | MV_LOAD, MV_TO_LIST, NTH_VALUE |
| Dynamic binding | DYNBIND, DYNUNBIND |
| Mutation | RPLACA, RPLACD, ASET |
| Condition handling | HANDLER_PUSH, HANDLER_POP |
| Misc | LIST, HALT, DEFMACRO, ARGC |

## Compiler

Single-pass recursive compiler from S-expressions to bytecode:
- Lexical scope with compile-time environment chain
- Flat closure model with upvalue capture (value semantics)
- Tail position tracking for tail call optimization
- Macro expansion before compilation
- Backward jump support for loop forms

**Special forms:** `quote`, `if`, `progn`, `lambda`, `let`, `let*`, `setq`, `setf`, `defun`, `defvar`, `defparameter`, `defmacro`, `function (#')`, `block`, `return-from`, `return`, `and`, `or`, `cond`, `do`, `dolist`, `dotimes`, `case`, `ecase`, `typecase`, `etypecase`, `flet`, `labels`, `tagbody`, `go`, `catch`, `unwind-protect`, `multiple-value-bind`, `multiple-value-list`, `multiple-value-prog1`, `nth-value`, `eval-when`, `destructuring-bind`, `defsetf`, `trace`, `untrace`, `time`, `handler-bind`

**Bootstrap macros:** `when`, `unless`, `prog1`, `prog2`, `push`, `pop`, `incf`, `decf`, `pushnew`, `handler-case`, `ignore-errors`

**Bootstrap functions:** `cadr`, `caar`, `cdar`, `cddr`, `caddr`, `cadar`, `identity`, `endp`, `member`, `intersection`, `union`, `set-difference`, `subsetp`

## Built-in Functions (191 functions)

| Category | Functions |
|----------|-----------|
| Arithmetic | `+` `-` `*` `/` `mod` `1+` `1-` `abs` `max` `min` |
| Comparison | `=` `<` `>` `<=` `>=` |
| Predicates | `null` `consp` `atom` `listp` `numberp` `integerp` `symbolp` `stringp` `functionp` `vectorp` `zerop` `plusp` `minusp` `characterp` `keywordp` `hash-table-p` |
| Equality | `eq` `eql` `equal` `not` |
| List ops | `cons` `car` `cdr` `first` `rest` `list` `length` `append` `reverse` `nth` `nthcdr` `last` `butlast` `copy-list` `copy-tree` |
| Alist/plist | `acons` `pairlis` `assoc` `rassoc` `getf` `adjoin` |
| Tree ops | `subst` `sublis` `nsubst` |
| Destructive | `nconc` `nreverse` `delete` `delete-if` |
| Mutation | `rplaca` `rplacd` `aref` `svref` `make-array` `vector` `array-dimensions` `array-rank` `set` |
| Symbol access | `symbol-value` `symbol-function` `symbol-name` `symbol-package` `boundp` `fboundp` `fdefinition` `make-symbol` |
| Higher-order | `mapcar` `mapc` `mapcan` `maplist` `mapl` `mapcon` `apply` `funcall` |
| Characters | `char=` `char/=` `char<` `char>` `char<=` `char>=` `char-code` `code-char` `char-upcase` `char-downcase` `upper-case-p` `lower-case-p` `alpha-char-p` `digit-char-p` |
| Strings | `string=` `string-equal` `string<` `string>` `string<=` `string>=` `string-upcase` `string-downcase` `string-trim` `string-left-trim` `string-right-trim` `subseq` `concatenate` `char` `schar` `string` `parse-integer` `write-to-string` `prin1-to-string` `princ-to-string` |
| I/O | `print` `prin1` `princ` `terpri` `format` `load` `disassemble` |
| Eval/Macro | `eval` `macroexpand` `macroexpand-1` |
| Control | `throw` `values` `values-list` `error` `signal` `warn` |
| Conditions | `make-condition` `conditionp` `condition-type-name` `type-error-datum` `type-error-expected-type` `simple-condition-format-control` `simple-condition-format-arguments` `define-condition` |
| Hash tables | `make-hash-table` `gethash` `remhash` `maphash` `clrhash` `hash-table-count` `hash-table-p` |
| Type system | `typep` `coerce` |
| Timing | `get-internal-real-time` |
| Misc | `type-of` `gensym` |

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

### Phase 3: Macro Infrastructure ✅

Quasiquote and tooling that makes real macro writing practical:
- [x] Quasiquote (`` ` ``/`,`/`,@`) — reader and expander
- [x] `gensym` — hygienic macro support
- [x] File `load` — load .lisp files at runtime
- [x] Boot file loading (boot.lisp at startup)

89 host tests (4 suites), 222 Amiga batch tests — all passing.

### Phase 4: Core Language Completeness ✅

Features needed for idiomatic CL programming:
- [x] `&optional`, `&key`, `&allow-other-keys` lambda list support
- [x] `flet`, `labels` — local function bindings
- [x] `case`, `ecase`, `typecase`, `etypecase`
- [x] `prog1`, `prog2`
- [x] `block`/`return-from`/`return`
- [x] `eval`, `macroexpand`, `macroexpand-1` (user-accessible)
- [x] `tagbody`/`go` — low-level control flow
- [x] `catch`/`throw` — dynamic non-local exits
- [x] `unwind-protect` — cleanup forms
- [x] Multiple return values (`values`, `multiple-value-bind`, `multiple-value-list`, `multiple-value-prog1`, `nth-value`)
- [x] Dynamic variables (`defvar`, `defparameter`, special declarations, shallow binding)
- [x] `setf` with generalized places (car/cdr/first/rest/nth/aref/svref/symbol-value/symbol-function/gethash)
- [x] Modify macros: `push`, `pop`, `incf`, `decf`
- [x] Mutation builtins: `rplaca`, `rplacd`, `aref`, `svref`, `make-array`, `set`
- [x] `destructuring-bind`
- [x] `eval-when` — compile-time evaluation control
- [x] `member`, `pushnew`
- [x] `defsetf` — user-extensible setf places (short form)
- [x] `defun` implicit named block (CL spec: `return-from` works inside `defun`)
- [x] `funcall` symbol resolution (CL spec: `funcall` accepts symbols)

154 host tests (4 suites), ~290 Amiga batch tests — all passing.

### Phase 5: Standard Library (in progress)

Data structures, sequences, strings, and I/O:
- [x] Character functions (`characterp`, `char=`, `char/=`, `char<`, `char>`, `char<=`, `char>=`, `char-code`, `code-char`, `upper-case-p`, `lower-case-p`, `alpha-char-p`, `digit-char-p`, `char-upcase`, `char-downcase`)
- [x] Symbol functions (`symbol-name`, `symbol-package`, `fboundp`, `fdefinition`, `make-symbol`, `keywordp`)
- [x] String operations (`string=`, `string-equal`, `string<`, `string>`, `string<=`, `string>=`, `string-upcase`, `string-downcase`, `string-trim`, `string-left-trim`, `string-right-trim`, `subseq`, `concatenate`, `char`, `schar`, `string`, `parse-integer`, `write-to-string`, `prin1-to-string`, `princ-to-string`)
- [x] List utilities (`nthcdr`, `last`, `acons`, `copy-list`, `pairlis`, `assoc`, `rassoc`, `getf`, `subst`, `sublis`, `adjoin`, `butlast`, `copy-tree`) — `member` done in Phase 4
- [x] Destructive list ops (`nconc`, `nreverse`, `delete`, `delete-if`, `nsubst`)
- [x] Mapping variants (`mapc`, `mapcan`, `maplist`, `mapl`, `mapcon`)
- [x] Set operations in Lisp (`intersection`, `union`, `set-difference`, `subsetp`)
- [x] Hash tables (`make-hash-table`, `gethash`, `remhash`, `maphash`, `clrhash`, `hash-table-count`, `hash-table-p`, `(setf gethash)`)
- [x] Sequence functions (`find`, `find-if`, `find-if-not`, `position`, `position-if`, `position-if-not`, `count`, `count-if`, `count-if-not`, `remove`, `remove-if`, `remove-if-not`, `remove-duplicates`, `substitute`, `substitute-if`, `substitute-if-not`, `reduce`, `fill`, `replace`, `every`, `some`, `notany`, `notevery`, `map`, `mismatch`, `search`, `sort`, `stable-sort`)
- [x] Array operations (`vector`, `array-dimensions`, `array-rank`, `fill`, `replace`)
- [x] Type predicates (`typep`, `coerce`)
- [x] Advanced types (`deftype`, `subtypep`, compound type specifiers)
- [x] `declare`, `declaim`, `proclaim`, `locally` — declarations
- [x] `disassemble` — print bytecode disassembly of compiled functions
- [x] `trace`, `untrace` — function call tracing for debugging
- [x] Stack traces on error — walk VM call frames, print function names and call chain
- [x] Source location tracking — reader tracks line numbers, compiler attaches to bytecode, errors include file:line context
- [x] `time` — macro to measure and print execution time of an expression

289 host tests (4 suites), ~628 Amiga batch tests — all passing.

**Build improvements:**
- Split builtins.c into 7 modules and compiler.c into 3 modules (stay under vbcc TU size limits)
- Heap-allocate CL_Compiler structs (CL_Compiler is ~45KB due to tagbody arrays; two nested instances during compile_lambda overflowed the 65KB AmigaOS stack)

### Phase 6: Control & Error Handling (in progress)

Condition system, packages, and compiler completeness:
- [x] Condition types (`define-condition`, `make-condition`, `conditionp`, condition type hierarchy)
- [x] Handler binding stack (`handler-bind` special form, `OP_HANDLER_PUSH`/`OP_HANDLER_POP` opcodes)
- [x] Signaling (`signal`, `warn`, `error` with condition integration)
- [x] `handler-case` (boot macro using catch/throw + cons box pattern)
- [x] `ignore-errors` (boot macro)
- [ ] Restarts (`restart-case`, `invoke-restart`, `find-restart`, `with-simple-restart`, `abort`, `continue`, `muffle-warning`)
- [ ] `cerror`, `check-type`, `assert`

371 host tests (5 suites), ~640 Amiga batch tests — all passing.
- [ ] Full packages (`defpackage`, `in-package`, `use-package`, `export`, `import`, `shadow`, `shadowing-import-from`, `find-package`, `make-package`, `rename-package`, `delete-package`, `do-symbols`, `do-external-symbols`, `intern`, `unintern`)
- [ ] `macrolet`, `symbol-macrolet` — local macro bindings (compile-time only, no opcodes)
- [ ] Unused variable warnings with `ignore`/`ignorable` declaration support
- [ ] `defconstant` — constant variable definitions
- [ ] `multiple-value-call`, `multiple-value-setq` — MV completeness
- [ ] `progv` — dynamic binding with computed symbols
- [ ] `the` — type declaration special form (initially no-op, validates later)

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
- [ ] `sleep` — platform sleep function
- [ ] Time: `get-universal-time`, `decode-universal-time`, `encode-universal-time`, `get-decoded-time`
- [ ] `compile` — compile function at runtime

### Phase 8: Iteration & Format

Extended iteration, output formatting, and standard library completeness:
- [ ] `loop` facility (extended LOOP macro: `for`/`in`/`on`/`across`, `collect`/`append`/`nconc`, `when`/`unless`/`if`, `with`/`initially`/`finally`, `thereis`/`always`/`never`, `sum`/`count`/`maximize`/`minimize`)
- [ ] `format` full directives (`~A`, `~S`, `~D`, `~B`, `~O`, `~X`, `~R`, `~%`, `~&`, `~T`, `~<~>`, `~{~}`, `~[~]`, `~?`, `~(~)`, `~*`, `~^`)
- [ ] Pretty printer (`pprint`, `pprint-logical-block`, `pprint-newline`, `pprint-indent`)
- [ ] Setf completeness: `rotatef`, `shiftf`, `define-modify-macro`, `defsetf` long form, `define-setf-expander`
- [ ] `psetq`, `psetf` — parallel assignment
- [ ] `load-time-value` — evaluate once at load time
- [ ] Missing list ops: `tree-equal`, `make-list`, `list*`, `list-length`, `tailp`, `ldiff`, `revappend`, `nreconc`, `assoc-if`, `rassoc-if`, `remf`
- [ ] Missing string ops: `string-capitalize`, `nstring-upcase`, `nstring-downcase`, `nstring-capitalize`, `char-name`, `name-char`
- [ ] Missing sequence ops: `map-into`, `copy-seq`, `elt`, `(setf elt)`
- [ ] Higher-order: `complement`, `constantly`

### Phase 9: Numeric Tower

Full CL numeric type hierarchy with arithmetic contagion:
- [ ] Bignums — arbitrary precision integers, heap-allocated variable-length digit arrays
- [ ] Ratios — normalized numerator/denominator pairs (fixnum or bignum), GCD reduction
- [ ] Single-float — IEEE 754 32-bit, heap-allocated; software implementation (optional 68881/68882 FPU fast path)
- [ ] Double-float — IEEE 754 64-bit, heap-allocated; software implementation (optional FPU)
- [ ] Complex numbers — real + imaginary parts, any real type
- [ ] Numeric contagion: integer → ratio → single-float → double-float; complex promotion
- [ ] Reader syntax: ratios (`1/2`, `3/4`), floats (`1.0`, `1.5e3`, `1.0d0`), complex (`#C(1 2)`)
- [ ] Division: `(/ 1 2)` → `1/2` (ratio), `(/ 1.0 2)` → `0.5` (float)
- [ ] All arithmetic ops (`+` `-` `*` `/` `mod` `abs` `max` `min` `1+` `1-`) extended for full tower
- [ ] Rounding: `floor`, `ceiling`, `truncate`, `round`, `ffloor`, `fceiling`, `ftruncate`, `fround`
- [ ] Ratio ops: `numerator`, `denominator`, `rational`, `rationalize`
- [ ] Float ops: `float`, `float-digits`, `float-radix`, `float-sign`, `decode-float`, `integer-decode-float`, `scale-float`
- [ ] Complex ops: `realpart`, `imagpart`, `conjugate`, `phase`
- [ ] Math: `sqrt`, `isqrt`, `expt`, `log`, `exp`, `gcd`, `lcm`, `ash`, `logand`, `logior`, `logxor`, `lognot`, `logcount`
- [ ] Trig: `sin`, `cos`, `tan`, `asin`, `acos`, `atan` (software float)
- [ ] Type predicates: `rationalp`, `ratiop`, `realp`, `complexp`, `floatp`, `single-float-p`, `double-float-p`
- [ ] Constants: `most-positive-fixnum`, `most-negative-fixnum`, `pi`, float limits
- [ ] `random`, `make-random-state`, `*random-state*` — pseudo-random number generation
- [ ] Bit manipulation: `ldb`, `dpb`, `byte`, `byte-size`, `byte-position`, `integer-length`, `logbitp`, `logtest`, `boole`

### Phase 10: CLOS

Structures and Common Lisp Object System:
- [ ] `defstruct` — structure definitions (slots, constructors, copier, predicate, `:include` inheritance, `:type list/vector`)
- [ ] `copy-structure`, struct accessors, `#S()` reader syntax
- [ ] Multi-dimensional arrays, adjustable arrays, fill pointers, displaced arrays
- [ ] `vector-push`, `vector-push-extend`, `adjust-array`, `make-array` keyword extensions (`:adjustable`, `:fill-pointer`, `:displaced-to`)
- [ ] Bit vectors: `bit-and`, `bit-or`, `bit-xor`, `bit-not`, `bit-vector-p`, `make-array :element-type 'bit`
- [ ] `defclass` — class definition with slots, inheritance, metaclasses
- [ ] Slot options: `:initarg`, `:initform`, `:accessor`, `:reader`, `:writer`, `:allocation`, `:type`, `:documentation`
- [ ] `defgeneric`, `defmethod` — generic function dispatch
- [ ] Method qualifiers: `:before`, `:after`, `:around`, `call-next-method`
- [ ] `make-instance`, `initialize-instance`, `reinitialize-instance`, `shared-initialize`
- [ ] `slot-value`, `slot-boundp`, `slot-exists-p`, `slot-makunbound`
- [ ] `with-slots`, `with-accessors`
- [ ] `find-class`, `class-of`, `class-name`, `change-class`
- [ ] `print-object`, `print-unreadable-object` — CLOS-based printing
- [ ] Multiple inheritance, standard method combination
- [ ] `define-method-combination`

### Phase 11: ASDF & Beyond

Validation and ecosystem:
- [ ] Run ASDF 3.3 (~14K lines) — ultimate compliance test
- [ ] `compile-file`, `load`, `require`, `provide`
- [ ] Logical pathnames
- [ ] `documentation` strings
- [ ] `define-compiler-macro`, `compiler-macro-function`
- [ ] `make-load-form`, `make-load-form-saving-slots`
- [ ] Environment: `lisp-implementation-type`, `lisp-implementation-version`, `machine-type`, `machine-version`, `software-type`, `software-version`, `room`
- [ ] Introspection: `describe`, `inspect`, `apropos`, `apropos-list`
- [ ] Symbol utilities: `copy-symbol`, `gentemp`, `*gensym-counter*`
- [ ] Bytecode peephole optimizer — second pass over `c->code[]` buffer before creating `CL_Bytecode`, linear scan with in-place rewrite:
  - Fused opcodes: `STORE+POP` → `STORE_POP`, `LOAD+POP` elimination
  - Constant folding: `CONST 1, CONST 2, CALL +` → `CONST 3` for known arithmetic
  - Known-function specialization: `FLOAD +, CONST x, CONST y, CALL 2` → `CONST x, CONST y, OP_ADD` when function is known at compile time
  - Dead code elimination: unreachable branches after `JTRUE`/`JNIL` with constant conditions
  - Jump threading: `JMP` to `JMP` → direct jump to final target
  - Redundant load elimination: `STORE slot, LOAD slot` → `STORE slot, DUP`
- [ ] Compiler optimizations (constant folding during compilation, function inlining)
- [ ] Generational or incremental GC
- [ ] Debugger / stepper
- [ ] Line editing (history, tab completion)
- [ ] Amiga-specific FFI (calling library functions)
- [ ] Intuition/gadtools bindings for GUI (stretch goal)
- [ ] Image save/restore (`save-image`, `load-image`) — serialize heap arena, symbol tables, package state, and bytecode pools to disk; restore to skip boot.lisp loading and resume a saved environment
- [ ] Standalone executables — prepend runtime binary to a saved image to produce a single self-contained executable (like SBCL's `save-lisp-and-die :executable t`)

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
│   ├── core/              # Language implementation (21 modules)
│   └── platform/          # OS abstraction (posix, amiga)
├── include/
│   └── clamiga.h          # Public umbrella header
├── lib/
│   └── boot.lisp          # Bootstrap macros/functions
├── tests/
│   ├── test.h             # Test framework
│   ├── test_*.c           # Host test suites (5 files, 371 tests)
│   └── amiga/
│       └── run-tests.lisp # AmigaOS batch tests (~640 tests)
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
