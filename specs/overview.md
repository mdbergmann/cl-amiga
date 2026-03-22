# CL-Amiga: Overall Specification

## Vision

A Common Lisp environment built from scratch for AmigaOS 3+, running on both m68k and PPC hardware. The bytecode VM ensures architecture-agnostic execution — the same compiled Lisp code runs on both platforms. Starting with a minimal core and growing incrementally toward full ANSI CL compliance. ASDF, Quicklisp, Alexandria, FSet, and fiveam all load and pass their test suites on both host and Amiga.

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

Types: CONS, SYMBOL, STRING, FUNCTION, CLOSURE, BYTECODE, VECTOR, PACKAGE, HASHTABLE, CONDITION, STRUCT, BIGNUM, SINGLE_FLOAT, DOUBLE_FLOAT, RATIO, STREAM, ARRAY, RANDOM_STATE, BIT_VECTOR, PATHNAME, COMPLEX, THREAD, LOCK, CONDVAR, FOREIGN_POINTER
```

## Memory Budget (8MB System)

| Component | Size |
|-----------|------|
| OS + Workbench | ~1.5MB |
| CL-Amiga binary | ~535KB |
| Heap arena | 4MB (configurable) |
| VM value stack | 64KB (16K entries) |
| VM call stack | 256 frames |
| C stack | 64KB |
| Free for OS | ~2.3MB |

4MB heap ≈ 340K cons cells max, ~100-200K practical working set.

## Bytecode VM

Stack-based, byte-oriented instruction encoding. 72 opcodes:

| Category | Opcodes |
|----------|---------|
| Constants/vars | CONST, LOAD, STORE, GLOAD, GSTORE, UPVAL, NIL, T, FLOAD, FSTORE, DEFVAR |
| Stack | POP, DUP |
| List ops | CONS, CAR, CDR |
| Arithmetic | ADD, SUB, MUL, DIV |
| Comparison | EQ, LT, GT, LE, GE, NUMEQ |
| Logic | NOT |
| Control flow | JMP, JNIL, JTRUE |
| Functions | CALL, TAILCALL, RET, CLOSURE, APPLY |
| NLX | CATCH, UNCATCH, UWPROT, UWPOP, UWRETHROW |
| Block/return | BLOCK_PUSH, BLOCK_POP, BLOCK_RETURN |
| Tagbody/go | TAGBODY_PUSH, TAGBODY_POP, TAGBODY_GO |
| Multiple values | MV_LOAD, MV_TO_LIST, NTH_VALUE, MV_RESET |
| Dynamic binding | DYNBIND, DYNUNBIND, PROGV_BIND, PROGV_UNBIND |
| Mutation | RPLACA, RPLACD, ASET |
| Heap-boxed cells | MAKE_CELL, CELL_REF, CELL_SET_LOCAL, CELL_SET_UPVAL |
| Condition handling | HANDLER_PUSH, HANDLER_POP, RESTART_PUSH, RESTART_POP |
| Type checking | ASSERT_TYPE |
| Setf | DEFSETF |
| Misc | LIST, HALT, DEFMACRO, DEFTYPE, ARGC |

## Compiler

Single-pass recursive compiler from S-expressions to bytecode:
- Lexical scope with compile-time environment chain
- Flat closure model with upvalue capture (value semantics)
- Tail position tracking for tail call optimization
- Macro expansion before compilation (defmacro destructuring lambda lists supported)
- Backward jump support for loop forms

**Special forms:** `quote`, `if`, `progn`, `lambda`, `let`, `let*`, `setq`, `setf`, `defun`, `defvar`, `defparameter`, `defconstant`, `defmacro`, `function (#')`, `block`, `return-from`, `return`, `and`, `or`, `cond`, `do`, `dolist`, `dotimes`, `case`, `ecase`, `typecase`, `etypecase`, `flet`, `labels`, `tagbody`, `go`, `catch`, `unwind-protect`, `progv`, `multiple-value-bind`, `multiple-value-list`, `multiple-value-prog1`, `nth-value`, `eval-when`, `destructuring-bind`, `defsetf`, `deftype`, `trace`, `untrace`, `time`, `handler-bind`, `restart-case`, `in-package`, `macrolet`, `symbol-macrolet`, `the`, `declare`, `declaim`, `locally`

**Bootstrap macros (boot.lisp):** `when`, `unless`, `prog1`, `prog2`, `push`, `pop`, `incf`, `decf`, `pushnew`, `handler-case`, `ignore-errors`, `with-simple-restart`, `define-condition`, `check-type`, `assert`, `defpackage`, `do-symbols`, `do-external-symbols`, `defstruct`, `with-open-file`, `with-output-to-string`, `with-input-from-string`, `with-standard-io-syntax`, `loop`, `define-modify-macro`

**CLOS (lib/clos.lisp, loaded via require):** `defclass`, `make-instance`, `slot-value`, `(setf slot-value)`, `slot-boundp`, `slot-makunbound`, `slot-exists-p`, `defgeneric`, `defmethod`, `call-next-method`, `next-method-p`, `with-slots`, `class-of`, `find-class`, `(setf find-class)`, `ensure-generic-function`, `allocate-instance`, `initialize-instance`, `shared-initialize`, `reinitialize-instance`, `change-class`, `print-object`, `slot-unbound` (GF), multiple `:accessor`/`:reader`/`:writer` per slot

## Built-in Functions (519 C functions + ~135 boot.lisp functions/macros)

| Category | Functions |
|----------|-----------|
| Arithmetic | `+` `-` `*` `/` `truncate` `floor` `ceiling` `round` `ftruncate` `ffloor` `fceiling` `fround` `rem` `mod` `1+` `1-` `abs` `max` `min` `gcd` `lcm` `expt` `isqrt` `sqrt` `exp` `log` `sin` `cos` `tan` `asin` `acos` `atan` |
| Bitwise | `ash` `logand` `logior` `logxor` `lognot` `integer-length` `logcount` `logbitp` `logtest` `boole` `byte` `byte-size` `byte-position` `ldb` `dpb` |
| Bit vectors | `bit-vector-p` `simple-bit-vector-p` `bit` `sbit` `bit-and` `bit-ior` `bit-xor` `bit-eqv` `bit-nand` `bit-nor` `bit-andc1` `bit-andc2` `bit-orc1` `bit-orc2` `bit-not` |
| Random | `random` `make-random-state` `random-state-p` |
| Comparison | `=` `/=` `<` `>` `<=` `>=` |
| Predicates | `null` `consp` `atom` `listp` `numberp` `integerp` `floatp` `realp` `rationalp` `symbolp` `stringp` `functionp` `vectorp` `arrayp` `simple-vector-p` `adjustable-array-p` `bit-vector-p` `simple-bit-vector-p` `zerop` `plusp` `minusp` `evenp` `oddp` `characterp` `keywordp` `hash-table-p` `random-state-p` `pathnamep` |
| Equality | `eq` `eql` `equal` `not` |
| List ops | `cons` `car` `cdr` `first` `rest` `list` `list*` `make-list` `length` `append` `reverse` `nth` `nthcdr` `last` `butlast` `copy-list` `copy-tree` |
| Alist/plist | `acons` `pairlis` `assoc` `rassoc` `getf` `adjoin` |
| Tree ops | `subst` `sublis` `nsubst` |
| Destructive | `nconc` `nreverse` `delete` `delete-if` |
| Mutation | `rplaca` `rplacd` `set` |
| Arrays | `make-array` `vector` `aref` `svref` `vectorp` `arrayp` `simple-vector-p` `adjustable-array-p` `array-dimensions` `array-rank` `array-dimension` `array-total-size` `array-row-major-index` `row-major-aref` `fill-pointer` `array-has-fill-pointer-p` `vector-push` `vector-push-extend` `adjust-array` |
| Symbol access | `symbol-value` `symbol-function` `symbol-name` `symbol-package` `boundp` `fboundp` `fdefinition` `make-symbol` |
| Higher-order | `mapcar` `mapc` `mapcan` `maplist` `mapl` `mapcon` `apply` `funcall` |
| Ratios | `numerator` `denominator` `rational` `rationalize` |
| Floats | `float` `float-digits` `float-radix` `float-sign` `decode-float` `integer-decode-float` `scale-float` |
| Characters | `char=` `char/=` `char<` `char>` `char<=` `char>=` `char-code` `code-char` `char-upcase` `char-downcase` `upper-case-p` `lower-case-p` `alpha-char-p` `digit-char-p` `char-name` `name-char` |
| Strings | `string=` `string-equal` `string/=` `string-not-equal` `string<` `string>` `string<=` `string>=` `string-upcase` `string-downcase` `string-capitalize` `nstring-upcase` `nstring-downcase` `nstring-capitalize` `string-trim` `string-left-trim` `string-right-trim` `subseq` `concatenate` `char` `schar` `string` `parse-integer` |
| Sequences | `find` `find-if` `find-if-not` `position` `position-if` `position-if-not` `count` `count-if` `count-if-not` `remove` `remove-if` `remove-if-not` `remove-duplicates` `substitute` `substitute-if` `substitute-if-not` `reduce` `fill` `replace` `every` `some` `notany` `notevery` `map` `map-into` `mismatch` `search` `sort` `stable-sort` `copy-seq` `elt` |
| I/O | `write` `print` `prin1` `princ` `pprint` `terpri` `format` `read` `load` `provide` `require` `disassemble` `compile` |
| Streams | `streamp` `input-stream-p` `output-stream-p` `interactive-stream-p` `open-stream-p` `read-char` `write-char` `peek-char` `unread-char` `read-line` `write-string` `write-line` `fresh-line` `finish-output` `force-output` `clear-output` `close` `open` `make-string-input-stream` `make-string-output-stream` `get-output-stream-string` |
| Readtable | `readtablep` `get-macro-character` `set-macro-character` `make-dispatch-macro-character` `set-dispatch-macro-character` `get-dispatch-macro-character` `copy-readtable` |
| Pathnames | `pathname` `pathnamep` `parse-namestring` `namestring` `make-pathname` `merge-pathnames` `pathname-host` `pathname-device` `pathname-directory` `pathname-name` `pathname-type` `pathname-version` `file-namestring` `directory-namestring` `enough-namestring` `user-homedir-pathname` |
| File system | `probe-file` `delete-file` `rename-file` `file-write-date` `ensure-directories-exist` |
| Time | `get-universal-time` `get-internal-real-time` `sleep` |
| Eval/Macro | `eval` `macroexpand` `macroexpand-1` `proclaim` |
| Control | `throw` `values` `values-list` `error` `signal` `warn` `invoke-restart` `find-restart` `compute-restarts` `abort` `continue` `muffle-warning` `invoke-debugger` |
| Conditions | `make-condition` `conditionp` `condition-type-name` `type-error-datum` `type-error-expected-type` `simple-condition-format-control` `simple-condition-format-arguments` `%register-condition-type` `condition-slot-value` |
| Structures | `structurep` `%register-struct-type` `%make-struct` `%struct-ref` `%struct-set` `%copy-struct` `%struct-type-name` `%struct-slot-names` `%struct-slot-specs` `%class-of` |
| Hash tables | `make-hash-table` `gethash` `remhash` `maphash` `clrhash` `hash-table-count` `hash-table-p` `%hash-table-pairs` |
| Type system | `type-of` `typep` `coerce` `subtypep` |
| Packages | `make-package` `find-package` `delete-package` `rename-package` `export` `unexport` `import` `use-package` `unuse-package` `shadow` `find-symbol` `intern` `unintern` `package-name` `package-use-list` `package-nicknames` `list-all-packages` `%package-symbols` `%package-external-symbols` `package-local-nicknames` `add-package-local-nickname` `remove-package-local-nickname` |
| Introspection | `describe` |
| Complex | `complex` `complexp` `realpart` `imagpart` `conjugate` |
| Threading (MP) | `mp:make-thread` `mp:join-thread` `mp:thread-alive-p` `mp:current-thread` `mp:all-threads` `mp:thread-name` `mp:thread-yield` `mp:threadp` `mp:make-lock` `mp:acquire-lock` `mp:release-lock` `mp:lock-name` `mp:lockp` `mp:make-condition-variable` `mp:condition-wait` `mp:condition-notify` `mp:condition-broadcast` `mp:condition-name` `mp:condition-variable-p` `mp:interrupt-thread` `mp:destroy-thread` |
| Sockets | `ext:open-tcp-stream` |
| FFI | `ffi:make-foreign-pointer` `ffi:foreign-pointer-address` `ffi:foreign-pointer-p` `ffi:null-pointer-p` `ffi:alloc-foreign` `ffi:free-foreign` `ffi:peek-u32` `ffi:peek-u16` `ffi:peek-u8` `ffi:poke-u32` `ffi:poke-u16` `ffi:poke-u8` `ffi:foreign-string` `ffi:foreign-to-string` `ffi:pointer+` |
| Amiga | `amiga:open-library` `amiga:close-library` `amiga:call-library` `amiga:alloc-chip` `amiga:free-chip` |
| Compilation | `compile-file` |
| Misc | `gensym` |

## Current Status

1798+ host tests (26 suites), 2077 Amiga batch tests — all passing.

### Validation Milestones

- **ASDF 3.3.7** (~14K lines of CL) — loads with zero errors at 11M heap
- **Quicklisp** — installs, downloads, and loads packages on both host and Amiga
- **Alexandria** — loads via quickload at 24M heap (552 symbols)
- **FSet** — loads and passes 17/17 tests at 24M heap
- **fiveam** — loads and passes 57/57 self-tests on host (24M) and Amiga (48M + 800K C stack)

### What's Implemented

Phases 1-10 are complete. The system has:

- Full numeric tower: fixnums, bignums (arbitrary precision), ratios, single/double floats, complex numbers, bit operations, random numbers
- Complete control flow: closures (flat + heap-boxed cells for mutated bindings), macros, tagbody/go, catch/throw, unwind-protect, progv, multiple values, dynamic variables, block/return-from
- Condition system: handler-bind/handler-case, restart-case, interactive debugger with backtrace and source locations
- Package system: full CL packages with CDR-10 local nicknames, reader/printer package qualification
- CLOS: defclass, make-instance, defgeneric/defmethod (multiple dispatch, method combination, EQL specializers), change-class, print-object, slot-boundp/slot-makunbound/slot-unbound
- I/O: streams (console/file/string/synonym/socket), readtable, pathnames (Amiga/POSIX, tilde expansion), format directives, pretty printer
- Extended LOOP: for/as, collect/append/nconc/sum/count/maximize/minimize, when/if/unless with nesting, hash-table/package iteration, destructuring
- defstruct, arrays (multi-dim, adjustable, fill pointers, displaced), bit vectors, hash tables (eq/eql/equal/equalp)
- Type system: typep, subtypep, coerce, deftype, compound type specifiers
- Printer control, trace/untrace, time, disassemble, compile, compile-file, describe
- TCP sockets: `ext:open-tcp-stream` (POSIX BSD sockets, Amiga bsdsocket.library)
- Threading (MP package): kernel threads with per-thread VM, TLV dynamic bindings, locks, named condition variables, interrupt/destroy-thread, type predicates (`threadp`, `lockp`, `condition-variable-p`), stop-the-world GC coordination with safepoint-based interruption
- FFI (Foreign Function Interface): TYPE_FOREIGN_POINTER, peek/poke memory access, foreign string conversion, platform-independent `FFI` package
- AmigaOS native API (`AMIGA` package): register-based library call dispatch via 68k asm trampoline, chip memory allocation, `defcstruct` for C struct access
- AmigaOS GUI libraries (loaded on demand via `require`, zero binary impact):
  - `AMIGA.INTUITION`: windows, screens, IDCMP event loop, public screen management
  - `AMIGA.GFX`: RastPort drawing (Move, Draw, SetAPen, RectFill, Text)
  - `AMIGA.GADTOOLS`: GadTools gadgets (button, checkbox, string, slider, etc.), menus, bevel boxes, VisualInfo
  - `AMIGA.FFI`: tag list construction, `defcfun` for named library function wrappers
- Nested quasiquote (depth-tracking expansion)
- i32 jump offsets (256KB bytecode limit per function)
- REPL with error recovery, debugger, history variables, ASCII banner, --load/--eval/--script CLI options

### What's Not Yet Implemented

**Numeric:**
- `phase` (complex argument)
- Constants: `pi`, float limits, `*read-default-float-format*`

**Control / Compiler:**
- `multiple-value-call`, `multiple-value-setq`
- `psetq`, `psetf` — parallel assignment
- `load-time-value`
- Unused variable warnings with `ignore`/`ignorable`

**Format:**
- Justification `~<~>` — remaining unimplemented directive

**Structures:**
- `defstruct` `:type list/vector`, `:print-function`/`:print-object`, `#S()` reader, `:named`, `:read-only`, BOA constructors

**CLOS:**
- `print-unreadable-object`
- `describe` integration for CLOS objects
- `defclass` `:type`, `:documentation` slot options
- Dispatch caching for performance

**Streams:**
- Broadcast, concatenated, two-way, echo streams
- `read-preserving-whitespace`

**Pathnames:**
- Logical pathnames
- `~user` expansion (only `~` and `~/` are currently supported)

**Environment:**
- `lisp-implementation-type`, `lisp-implementation-version`, `machine-type`, `machine-version`, `software-type`, `software-version`, `room`
- `documentation` strings
- `make-load-form`, `make-load-form-saving-slots`
- `inspect`, `apropos`, `apropos-list`
- `gentemp`, `*gensym-counter*`

**Optimization:**
- Bytecode peephole optimizer (fused opcodes, constant folding, dead code elimination, jump threading)
- Compiler optimizations (function inlining)
- Generational or incremental GC
- Dispatch caching for generic functions

**Loading / Compilation feedback:**
- Progress output when loading Lisp files or ASDF systems — show which file is currently being compiled/loaded (e.g. `; Loading foo.lisp ...`, `; Compiling bar.lisp ...`)
- Useful for debugging slow loads and understanding load order in complex systems

**Aspirational:**
- Advanced debugger: single-stepper, frame inspection, local variable display
- Line editing (history, tab completion)
- Image save/restore (`save-image`, `load-image`)
- Standalone executables (prepend runtime to saved image)
- 64-bit `CL_Obj` on 64-bit hosts (break ~4GB arena limit)

### TODO

- **CAS (compare-and-swap)** — atomic CAS primitive for lock-free data structures; on Amiga can possibly stay with lock-based implementation due to cooperative multitasking / `Forbid()`/`Permit()` semantics
- **Full bordeaux-threads support** — remaining gaps: semaphores (`make-semaphore`, `signal-semaphore`, `wait-on-semaphore`), atomic integers, `with-timeout`, `*default-special-bindings*`

## Project Structure

```
cl-amiga/
├── CLAUDE.md              # Dev conventions and quick reference
├── Makefile               # Build system (host)
├── Makefile.cross         # Build system (cross-compile, m68k-amigaos-gcc)
├── Makefile.amiga         # Build system (AmigaOS/vbcc)
├── specs/
│   └── overview.md        # This file
├── src/
│   ├── main.c             # Entry point
│   ├── core/              # Language implementation (46 .c + 23 .h modules)
│   │   ├── builtins_ffi.c # FFI package builtins (platform-independent)
│   │   └── builtins_amiga.c # AMIGA package builtins (AmigaOS only)
│   └── platform/          # OS abstraction (posix, amiga)
│       └── ffi_dispatch_m68k.s  # 68k asm trampoline for library calls
├── include/
│   └── clamiga.h          # Public umbrella header
├── lib/
│   ├── boot.lisp          # Bootstrap macros/functions (~135 defs)
│   ├── clos.lisp          # CLOS implementation (loaded via require)
│   ├── ffi.lisp           # FFI utilities: defcstruct, with-foreign-alloc
│   ├── asdf.lisp          # ASDF 3.3.7 build system
│   ├── quicklisp.lisp     # Quicklisp package manager
│   ├── quicklisp-compat.lisp  # Quicklisp compatibility layer
│   └── amiga/             # AmigaOS-specific Lisp libraries
│       ├── ffi.lisp       # with-library, with-tag-list, defcfun
│       ├── intuition.lisp # Windows, screens, IDCMP, pub screens
│       ├── graphics.lisp  # RastPort drawing, text rendering
│       └── gadtools.lisp  # GadTools gadgets, menus, bevel boxes
├── tests/
│   ├── test.h             # Test framework
│   ├── test_*.c           # Host test suites (26 files, 1798+ tests)
│   └── amiga/
│       ├── run-tests.lisp # AmigaOS batch tests (2077 tests)
│       └── test-gui.lisp  # Intuition/Graphics/GadTools tests
├── build/                 # Build output (gitignored)
└── verify/
    └── realamiga/          # FS-UAE config + AmigaOS system image
```

## Known Bugs

- **Amiga crash on heap exhaustion** — when the heap is exhausted (e.g. `(fact 3000)` with tail-recursive bignum factorial), the error is reported but the application crashes instead of recovering gracefully back to the REPL. Should signal a `storage-condition` and return to the REPL prompt.
- **`--heap` overflow for sizes >= 4G** — `parse_size()` overflows `uint32_t` silently (e.g. `--heap 4G` wraps to 0). Should cap at ~3.5GB and give a clear error for larger values.
- **68881/68040 FPU precision** — hardware FPU (`FPU=1`) has minor precision differences in `integer-decode-float` and `scale-float`. Software float (`-lmieee`, default) passes all tests.

## Verification Targets

1. `(+ 1 2)` → `3` ✅
2. `(defun fact (n) (if (<= n 1) 1 (* n (fact (1- n)))))` then `(fact 10)` → `3628800` ✅ / `(fact 30)` → `265252859812191058636308480000000` (bignum) ✅
3. `(mapcar #'1+ '(1 2 3))` → `(2 3 4)` ✅
4. Cross-compile to m68k, run in FS-UAE ✅
5. Run on real A1200 hardware
6. Load and execute multi-file Lisp programs ✅
7. `(require "asdf")` — ASDF 3.3.7 loads with zero errors ✅
8. Quicklisp installs, downloads, and loads packages ✅
9. Alexandria loads (552 symbols) ✅
10. fiveam loads and passes 57/57 self-tests (host + Amiga) ✅
11. FSet loads and passes 17/17 tests ✅
