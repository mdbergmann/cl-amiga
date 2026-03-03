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

Types: CONS, SYMBOL, STRING, FUNCTION, CLOSURE, BYTECODE, VECTOR, PACKAGE, HASHTABLE, CONDITION, STRUCT, BIGNUM, SINGLE_FLOAT, DOUBLE_FLOAT, STREAM, ARRAY
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

Stack-based, byte-oriented instruction encoding. 56 opcodes:

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
| Condition handling | HANDLER_PUSH, HANDLER_POP, RESTART_PUSH, RESTART_POP |
| Type checking | ASSERT_TYPE |
| Misc | LIST, HALT, DEFMACRO, DEFTYPE, ARGC |

## Compiler

Single-pass recursive compiler from S-expressions to bytecode:
- Lexical scope with compile-time environment chain
- Flat closure model with upvalue capture (value semantics)
- Tail position tracking for tail call optimization
- Macro expansion before compilation
- Backward jump support for loop forms

**Special forms:** `quote`, `if`, `progn`, `lambda`, `let`, `let*`, `setq`, `setf`, `defun`, `defvar`, `defparameter`, `defconstant`, `defmacro`, `function (#')`, `block`, `return-from`, `return`, `and`, `or`, `cond`, `do`, `dolist`, `dotimes`, `case`, `ecase`, `typecase`, `etypecase`, `flet`, `labels`, `tagbody`, `go`, `catch`, `unwind-protect`, `multiple-value-bind`, `multiple-value-list`, `multiple-value-prog1`, `nth-value`, `eval-when`, `destructuring-bind`, `defsetf`, `deftype`, `trace`, `untrace`, `time`, `handler-bind`, `restart-case`, `in-package`, `macrolet`, `symbol-macrolet`, `the`, `declare`, `declaim`, `locally`

**Bootstrap macros:** `when`, `unless`, `prog1`, `prog2`, `push`, `pop`, `incf`, `decf`, `pushnew`, `handler-case`, `ignore-errors`, `with-simple-restart`, `define-condition`, `check-type`, `assert`, `defpackage`, `do-symbols`, `do-external-symbols`, `defstruct`, `with-open-file`, `with-output-to-string`, `with-input-from-string`, `with-standard-io-syntax`, `loop`

**Bootstrap functions:** `cadr`, `caar`, `cdar`, `cddr`, `caddr`, `cadar`, `cdddr`, `cadddr`, `identity`, `endp`, `member`, `intersection`, `union`, `set-difference`, `subsetp`, `cerror`, `break`, `read-from-string`, `prin1-to-string`, `princ-to-string`, `write-to-string`, `complement`, `constantly`, `tree-equal`, `list-length`, `tailp`, `ldiff`, `revappend`, `nreconc`, `assoc-if`, `assoc-if-not`, `rassoc-if`, `rassoc-if-not`, `pathname-name`, `pathname-type`, `pathname-directory`, `namestring`, `truename`, `make-pathname`, `merge-pathnames`, `enough-namestring`, `ensure-directories-exist`, `decode-universal-time`, `encode-universal-time`, `get-decoded-time`

## Built-in Functions (347 C functions + 35 boot.lisp functions)

| Category | Functions |
|----------|-----------|
| Arithmetic | `+` `-` `*` `/` `truncate` `floor` `ceiling` `round` `ftruncate` `ffloor` `fceiling` `fround` `rem` `mod` `1+` `1-` `abs` `max` `min` `gcd` `lcm` `expt` `isqrt` `sqrt` `exp` `log` `sin` `cos` `tan` `asin` `acos` `atan` |
| Bitwise | `ash` `logand` `logior` `logxor` `lognot` `integer-length` |
| Comparison | `=` `/=` `<` `>` `<=` `>=` |
| Predicates | `null` `consp` `atom` `listp` `numberp` `integerp` `floatp` `realp` `rationalp` `symbolp` `stringp` `functionp` `vectorp` `arrayp` `simple-vector-p` `adjustable-array-p` `zerop` `plusp` `minusp` `evenp` `oddp` `characterp` `keywordp` `hash-table-p` |
| Equality | `eq` `eql` `equal` `not` |
| List ops | `cons` `car` `cdr` `first` `rest` `list` `list*` `make-list` `length` `append` `reverse` `nth` `nthcdr` `last` `butlast` `copy-list` `copy-tree` |
| Alist/plist | `acons` `pairlis` `assoc` `rassoc` `getf` `adjoin` |
| Tree ops | `subst` `sublis` `nsubst` |
| Destructive | `nconc` `nreverse` `delete` `delete-if` |
| Mutation | `rplaca` `rplacd` `set` |
| Arrays | `make-array` `vector` `aref` `svref` `vectorp` `arrayp` `simple-vector-p` `adjustable-array-p` `array-dimensions` `array-rank` `array-dimension` `array-total-size` `array-row-major-index` `row-major-aref` `fill-pointer` `array-has-fill-pointer-p` `vector-push` `vector-push-extend` `adjust-array` |
| Symbol access | `symbol-value` `symbol-function` `symbol-name` `symbol-package` `boundp` `fboundp` `fdefinition` `make-symbol` |
| Higher-order | `mapcar` `mapc` `mapcan` `maplist` `mapl` `mapcon` `apply` `funcall` |
| Floats | `float` `float-digits` `float-radix` `float-sign` `decode-float` `integer-decode-float` `scale-float` |
| Characters | `char=` `char/=` `char<` `char>` `char<=` `char>=` `char-code` `code-char` `char-upcase` `char-downcase` `upper-case-p` `lower-case-p` `alpha-char-p` `digit-char-p` `char-name` `name-char` |
| Strings | `string=` `string-equal` `string/=` `string-not-equal` `string<` `string>` `string<=` `string>=` `string-upcase` `string-downcase` `string-capitalize` `nstring-upcase` `nstring-downcase` `nstring-capitalize` `string-trim` `string-left-trim` `string-right-trim` `subseq` `concatenate` `char` `schar` `string` `parse-integer` |
| Sequences | `find` `find-if` `find-if-not` `position` `position-if` `position-if-not` `count` `count-if` `count-if-not` `remove` `remove-if` `remove-if-not` `remove-duplicates` `substitute` `substitute-if` `substitute-if-not` `reduce` `fill` `replace` `every` `some` `notany` `notevery` `map` `map-into` `mismatch` `search` `sort` `stable-sort` `copy-seq` `elt` |
| I/O | `write` `print` `prin1` `princ` `pprint` `terpri` `format` `read` `load` `disassemble` `compile` |
| Streams | `streamp` `input-stream-p` `output-stream-p` `interactive-stream-p` `open-stream-p` `read-char` `write-char` `peek-char` `unread-char` `read-line` `write-string` `write-line` `fresh-line` `finish-output` `force-output` `clear-output` `close` `open` `make-string-input-stream` `make-string-output-stream` `get-output-stream-string` |
| Readtable | `readtablep` `get-macro-character` `set-macro-character` `make-dispatch-macro-character` `set-dispatch-macro-character` `get-dispatch-macro-character` `copy-readtable` |
| File system | `probe-file` `delete-file` `rename-file` `file-write-date` `file-namestring` `directory-namestring` `ensure-directories-exist` |
| Time | `get-universal-time` `get-internal-real-time` `sleep` |
| Eval/Macro | `eval` `macroexpand` `macroexpand-1` `proclaim` |
| Control | `throw` `values` `values-list` `error` `signal` `warn` `invoke-restart` `find-restart` `compute-restarts` `abort` `continue` `muffle-warning` `invoke-debugger` |
| Conditions | `make-condition` `conditionp` `condition-type-name` `type-error-datum` `type-error-expected-type` `simple-condition-format-control` `simple-condition-format-arguments` `%register-condition-type` `condition-slot-value` |
| Structures | `structurep` `%register-struct-type` `%make-struct` `%struct-ref` `%struct-set` `%copy-struct` `%struct-type-name` `%struct-slot-names` `%struct-slot-specs` |
| Hash tables | `make-hash-table` `gethash` `remhash` `maphash` `clrhash` `hash-table-count` `hash-table-p` `%hash-table-pairs` |
| Type system | `type-of` `typep` `coerce` `subtypep` |
| Packages | `make-package` `find-package` `delete-package` `rename-package` `export` `unexport` `import` `use-package` `unuse-package` `shadow` `find-symbol` `intern` `unintern` `package-name` `package-use-list` `package-nicknames` `list-all-packages` `%package-symbols` `%package-external-symbols` `package-local-nicknames` `add-package-local-nickname` `remove-package-local-nickname` |
| Misc | `gensym` |

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

### Phase 5: Standard Library ✅

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
- [x] `time` — measure and print execution time, bytes consed, GC cycles, and heap usage

289 host tests (4 suites), ~628 Amiga batch tests — all passing.

**Build improvements:**

- Split builtins.c into 7 modules and compiler.c into 3 modules (stay under vbcc TU size limits)
- Heap-allocate CL_Compiler structs (CL_Compiler is ~45KB due to tagbody arrays; two nested instances during compile_lambda overflowed the 65KB AmigaOS stack)

### Phase 6: Control & Error Handling ✅

Condition system, packages, and compiler completeness:

- [x] Condition types (`define-condition`, `make-condition`, `conditionp`, condition type hierarchy)
- [x] Handler binding stack (`handler-bind` special form, `OP_HANDLER_PUSH`/`OP_HANDLER_POP` opcodes)
- [x] Signaling (`signal`, `warn`, `error` with condition integration)
- [x] `handler-case` (boot macro using catch/throw + cons box pattern)
- [x] `ignore-errors` (boot macro)
- [x] Restarts (`restart-case` compiler special form, `invoke-restart`, `find-restart`, `compute-restarts`, `abort`, `continue`, `muffle-warning` builtins)
- [x] `with-simple-restart` (boot macro), `cerror` (boot function)
- [x] `define-condition` (boot macro), `check-type` (boot macro), `assert` (boot macro), `condition-slot-value` builtin
- [x] Package foundation — package registry, `CL_SYM_EXPORTED` flag, export-aware symbol inheritance, nicknames, 17 package builtins (`make-package`, `find-package`, `delete-package`, `rename-package`, `export`, `unexport`, `import`, `use-package`, `unuse-package`, `shadow`, `find-symbol`, `intern`, `unintern`, `package-name`, `package-use-list`, `package-nicknames`, `list-all-packages`)
- [x] Reader package-qualified syntax (`pkg:sym` external, `pkg::sym` internal, `#:sym` uninterned), printer package prefixes (`PKG:SYM`, `PKG::SYM`, `#:SYM`)
- [x] `defpackage` (boot macro), `in-package` (compiler special form), `do-symbols`, `do-external-symbols` (boot macros), `%package-symbols`, `%package-external-symbols` (internal builtins), `*PACKAGE*` special variable
- [x] CDR-10 package-local nicknames — `package-local-nicknames`, `add-package-local-nickname`, `remove-package-local-nickname` builtins, `:local-nicknames` in `make-package` and `defpackage`, scoped resolution in `cl_find_package`
- [x] Interactive debugger — on error, display condition, backtrace, and available restarts; prompt user to select restart or abort; `invoke-debugger`, `break`, `*debugger-hook*`

476 host tests (6 suites), 752 Amiga batch tests — all passing.

- [x] `macrolet`, `symbol-macrolet` — local macro bindings (compile-time only, no opcodes)
- [ ] Unused variable warnings with `ignore`/`ignorable` declaration support
- [x] `defconstant` — constant variable definitions (compile-time `setq` check, runtime `set` check; T, NIL, keywords marked constant)
- [ ] `multiple-value-call`, `multiple-value-setq` — MV completeness
- [ ] `progv` — dynamic binding with computed symbols
- [x] `the` — type declaration special form with `OP_ASSERT_TYPE` opcode; runtime type check when safety >= 1, no-op at safety 0; disables TCO when safety >= 1; `(the (values ...) ...)` for multiple values not yet supported
- [x] Enhanced `time` — reports bytes consed, GC cycles, and heap usage in addition to wall time; `total_consed` monotonic counter in `CL_Heap`; `%GET-BYTES-CONSED`, `%GET-GC-COUNT` internal builtins; optionally CPU time via `getrusage()` (POSIX) / `ReadEClock()` (AmigaOS) deferred
- [x] REPL history variables — `*`, `**`, `***` (last 3 results), `+`, `++`, `+++` (last 3 forms), `-` (current form being evaluated)
- [x] REPL startup ASCII art — ASCII art banner displayed when starting the interactive REPL

### Phase 7: I/O & Pathnames ✅

File system integration and stream abstraction:

- [x] Streams (input/output/bidirectional, string streams) — `CL_Stream` heap type with console/file/string subtypes, growable output buffers via platform side tables
- [x] Stream operations (`read-char`, `write-char`, `peek-char`, `unread-char`, `read-line`, `write-string`, `write-line`, `terpri`, `fresh-line`, `finish-output`, `force-output`, `clear-output`, `open-stream-p`, `close`, `streamp`, `input-stream-p`, `output-stream-p`, `interactive-stream-p`)
- [x] `with-open-file`, `open`, `close` — file I/O with `:direction`, `:if-exists`, `:if-does-not-exist` keywords
- [x] `read`, `read-from-string` — stream-aware reader
- [x] `write-to-string`, `prin1-to-string`, `princ-to-string` — boot.lisp functions using `with-output-to-string`
- [x] `with-output-to-string`, `with-input-from-string`, `make-string-input-stream`, `make-string-output-stream`, `get-output-stream-string`
- [x] `with-standard-io-syntax`
- [x] Pathnames (`make-pathname`, `merge-pathnames`, `namestring`, `truename`, `pathname-name`, `pathname-type`, `enough-namestring`, `file-namestring`, `directory-namestring`) — boot.lisp functions, string-based representation
- [x] File operations (`probe-file`, `file-write-date`, `delete-file`, `rename-file`, `%mkdir`)
- [x] Reader macros — readtable pool (4 structs indexed by fixnum), `*readtable*` special variable, `set-macro-character`, `get-macro-character`, `make-dispatch-macro-character`, `set-dispatch-macro-character`, `get-dispatch-macro-character`, `copy-readtable`, `readtablep`; reader consults readtable for user-defined macro and dispatch sub-characters
- [x] Feature conditionals (`#+`, `#-`, `*features*`) — `:cl-amiga`, `:common-lisp`, `:posix`/`:amigaos`/`:m68k`
- [x] `sleep` — `platform_sleep_ms` (usleep on POSIX, Delay on Amiga)
- [x] Time: `get-universal-time`, `decode-universal-time`, `encode-universal-time`, `get-decoded-time` — boot.lisp functions with epoch conversion
- [x] `compile` — compile lambda form or return named function binding; returns 3 values (fn, warnings-p, failure-p)
- [x] User init file — load `~/.clamigarc` (POSIX) or `S:clamiga.lisp` (AmigaOS) on REPL startup, after boot.lisp; skip with `--no-userinit` flag
- [x] `--load <file>` / `--eval <expr>` command-line options — load files or evaluate expressions before REPL (output appears after banner in interactive mode); `--script <file>` to run file and exit (no REPL); `--non-interactive` to process options and exit without entering REPL
- [x] `--heap <size>` — configurable arena size (default 4MB, e.g. `--heap 8M`); also `--stack <size>` for VM value stack, `--frames <n>` for call frame depth (default 256); `--help` prints usage; unknown `--` options show error + usage

Not yet implemented: broadcast/concatenated/two-way/echo streams, `read-preserving-whitespace`, `parse-namestring`, `wild-pathname-p`, `translate-pathname`, `directory`

814 host tests (10 suites), 1042 Amiga batch tests — all passing.

### Phase 8: Iteration, Format & Printer

Extended iteration, output formatting, printer control, and standard library completeness:
- [x] `loop` facility — extended LOOP macro in boot.lisp:
  - Iteration: `for`/`as` with `in`/`on`/`across`/`=`/`from`/`downfrom`/`upfrom`, `to`/`below`/`above`/`downto`/`upto`, `by`, `repeat`, `while`/`until`
  - Accumulation: `collect`/`append`/`nconc`/`sum`/`count`/`maximize`/`minimize`, `into` named variables
  - Conditionals: `when`/`if`/`unless` with `and`/`else`/`end`
  - Termination: `always`/`never`/`thereis`, `return`, `loop-finish`
  - Structure: `with`/`and`, `named`, `initially`/`finally`
  - BEING clauses: `hash-key[s]`/`hash-value[s]` of hash tables (with `using`), `symbol[s]`/`present-symbol[s]`/`external-symbol[s]` of packages
  - Destructuring: tree-shaped patterns in FOR variable position (`(a b)`, `(a . b)`, `(a (b c))`)
  - Helper builtins: `%hash-table-pairs` (C), `%package-symbols`/`%package-external-symbols`
- [x] Printer control variables — `*print-escape*`, `*print-readably*`, `*print-base*`, `*print-radix*`, `*print-level*`, `*print-length*`, `*print-case*`, `*print-gensym*`, `*print-array*`, `*print-circle*`, `*print-pretty*`, `*print-right-margin*`
- [x] `write` builtin with keyword args (`:stream`, `:escape`, `:readably`, `:base`, `:radix`, `:level`, `:length`, `:case`, `:gensym`, `:array`, `:circle`, `:pretty`, `:right-margin`)
- [x] Extended `format` directives — `~A`, `~S`, `~W`, `~D`, `~B`, `~O`, `~X`, `~C`, `~%`, `~&`, `~|`, `~~`
- [x] Missing list ops: `tree-equal`, `make-list`, `list*`, `list-length`, `tailp`, `ldiff`, `revappend`, `nreconc`, `assoc-if`, `assoc-if-not`, `rassoc-if`, `rassoc-if-not`
- [x] Missing string ops: `string-capitalize`, `nstring-upcase`, `nstring-downcase`, `nstring-capitalize`, `char-name`, `name-char`
- [x] Missing sequence ops: `map-into`, `copy-seq`, `elt`, `(setf elt)`
- [x] Higher-order: `complement`, `constantly`
- [ ] Full `format` directives (`~R`, `~T`, `~<~>`, `~{~}`, `~[~]`, `~?`, `~(~)`, `~*`, `~^`, column/padding params)
- [x] Pretty printer — `pprint` builtin, `*print-pretty*` / `*print-right-margin*` control variables, fill-style line breaking (greedy, no look-ahead), column tracking, indentation stack for lists/vectors/structs; `write`/`write-to-string` `:pretty`/`:right-margin` keywords
- [ ] Full pretty printer (`pprint-logical-block`, `pprint-newline`, `pprint-indent`, `*print-pprint-dispatch*`)
- [ ] Setf completeness: `rotatef`, `shiftf`, `define-modify-macro`, `defsetf` long form, `define-setf-expander`
- [ ] `psetq`, `psetf` — parallel assignment
- [ ] `load-time-value` — evaluate once at load time
- [ ] `remf` — destructive plist removal

1017 host tests (11 suites), 1389 Amiga batch tests — all passing.

### Phase 9: Numeric Tower ✅

Full CL numeric type hierarchy with arithmetic contagion:
- [x] Bignums — arbitrary precision integers, heap-allocated 16-bit limb arrays (little-endian); schoolbook multiplication; Knuth Algorithm D division; automatic fixnum↔bignum promotion/demotion; all arithmetic ops dispatch through `cl_arith_*` layer
- [x] Integer math: `gcd`, `lcm`, `expt` (binary exponentiation), `isqrt` (Newton's method), `ash`, `logand`, `logior`, `logxor`, `lognot`, `integer-length`, `evenp`, `oddp`, `truncate`, `rem`
- [x] Constants: `most-positive-fixnum`, `most-negative-fixnum`
- [x] Type hierarchy: `fixnum`/`bignum` < `integer` < `rational` < `real` < `number` in `typep` and `subtypep`
- [x] `eql`/`equal`/hash-table support for bignums (value equality)

Floats (steps 1-12, complete):
- [x] Single-float — IEEE 754 32-bit, heap-allocated (`CL_SingleFloat`, 8 bytes)
- [x] Double-float — IEEE 754 64-bit, heap-allocated (`CL_DoubleFloat`, 12 bytes)
- [x] Contagion: integer + float → float; single + double → double
- [x] Reader: `1.0`, `1.5e3` → single-float; `1.0d0`, `1.5d3` → double-float
- [x] Printer: single `%g` with `.0`; double `%.15g` with `d` exponent
- [x] Arithmetic dispatch: all `cl_arith_*` ops accept floats
- [x] Type system: `typep`, `subtypep`, `coerce`, `type-of` for float types
- [x] Equality/hash: `eql`/`equal` value comparison, bit-based hashing
- [x] Predicates: `floatp`, `realp`, `rationalp`, `numberp` updated
- [x] Float-specific: `float`, `float-digits`, `float-radix`, `float-sign`, `decode-float`, `integer-decode-float`, `scale-float`
- [x] Rounding: `floor`, `ceiling`, `round`, `truncate` (2 values), `ffloor`, `fceiling`, `ftruncate`, `fround`, `mod`/`rem` with floats
- [x] Math functions: `sqrt`, `exp`, `log` (1-2 args), `expt` (integer-exact + float paths)
- [x] Trigonometric: `sin`, `cos`, `tan`, `asin` (domain check), `acos` (domain check), `atan` (1-2 args via `atan2`)
- [x] Amiga build: `-lmieee` (software float); `FPU=1` → `-fpu=68881 -lm881` (hardware FPU)

**Known FPU issue:** 68881/68040 hardware FPU (`FPU=1`) has minor precision differences — `integer-decode-float` significand off-by-one on some values, and `scale-float` double formatting mismatch. Software float (`-lmieee`, default) passes all tests. 2/837 Amiga tests fail with `FPU=1`.

**Float limitations (not planned):**
- No ratios — `(/ 1 2)` returns `0` (integer truncation), not `1/2`
- No complex numbers — `(sqrt -1)` signals an error instead of returning `#C(0 1)`
- No `*read-default-float-format*` — unqualified literals always produce single-float
- No float limits constants (`most-positive-single-float`, `least-positive-single-float`, etc.)
- No `pi` constant
- No `random`/`*random-state*`

716 host tests (9 suites), 877 Amiga batch tests — all passing (software float) at end of Phase 9.

Remaining numeric features (deferred):
- [ ] Ratios — normalized numerator/denominator pairs (fixnum or bignum), GCD reduction
- [ ] Complex numbers — real + imaginary parts, any real type
- [ ] Numeric contagion: integer → ratio → single-float → double-float; complex promotion
- [ ] Reader syntax: ratios (`1/2`, `3/4`), complex (`#C(1 2)`)
- [ ] Division: `(/ 1 2)` → `1/2` (ratio)
- [ ] Ratio ops: `numerator`, `denominator`, `rational`, `rationalize`
- [ ] Complex ops: `realpart`, `imagpart`, `conjugate`, `phase`
- [ ] `logcount`
- [ ] Type predicates: `ratiop`, `complexp`
- [ ] Constants: `pi`, float limits, `*read-default-float-format*`
- [ ] `random`, `make-random-state`, `*random-state*` — pseudo-random number generation
- [ ] Bit manipulation: `ldb`, `dpb`, `byte`, `byte-size`, `byte-position`, `logbitp`, `logtest`, `boole`

### Phase 10: Structures & CLOS

Structures and Common Lisp Object System:
- [x] `defstruct` — structure definitions (slots with defaults, constructors with `&key`, copier, predicate, accessors with `setf`, `:conc-name`, `:constructor`, `:predicate`, `:copier`, `:include` inheritance, `typep` integration, `#S()` printing, `type-of`, `structurep`)
  - **Not yet implemented:** `:type list/vector`, `:print-function`/`:print-object`, `#S()` reader syntax, `:named`, `:read-only` slots, BOA constructors
- [x] Multi-dimensional arrays — `make-array` with list dimensions, `aref`/`(setf aref)` variadic row-major index, `#nA(...)` printer format, `#(...)` reader syntax
- [x] Adjustable arrays, fill pointers — `make-array` `:adjustable`/`:fill-pointer`/`:initial-element`/`:initial-contents`/`:element-type` keywords
- [x] `vector-push`, `vector-push-extend`, `adjust-array`, `fill-pointer`, `(setf fill-pointer)`, `array-has-fill-pointer-p`
- [x] Array query — `array-dimension`, `array-total-size`, `array-row-major-index`, `row-major-aref`, `(setf row-major-aref)`, `array-dimensions`, `array-rank`
- [x] Array type predicates — `arrayp`, `simple-vector-p`, `adjustable-array-p`; `typep`/`type-of`/`subtypep` properly differentiate ARRAY/SIMPLE-ARRAY/VECTOR/SIMPLE-VECTOR
- [x] `*print-array*` — controls readable vs unreadable array printing
- [ ] Displaced arrays (`:displaced-to`, `:displaced-index-offset`)
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
- [ ] Advanced debugger: single-stepper, frame inspection, local variable display in debug frames
- [ ] Line editing (history, tab completion)
- [ ] Amiga-specific FFI (calling library functions)
- [ ] Intuition/gadtools bindings for GUI (stretch goal)
- [ ] Green threads — VM-level cooperative threading: each thread is a `CL_Thread` with its own VM stack, call frames, dynamic bindings, handler/restart/NLX stacks, and MV state; scheduler yields at safe points (OP_CALL, backward jumps); `cl-amiga/threads` package with `make-thread`, `join-thread`, `destroy-thread`, `thread-yield`, `current-thread`, `all-threads`, `threadp`; locks (`make-lock`, `acquire-lock`, `release-lock`, `with-lock-held`), recursive locks, read-write locks; condition variables (`make-condition-variable`, `condition-wait`, `condition-notify`); same API on AmigaOS and POSIX (concurrent everywhere, optionally parallel on POSIX via M:N OS thread pool); cooperative scheduling makes GC stop-the-world trivial; bordeaux-threads compatibility layer as a separate package on top
- [ ] Image save/restore (`save-image`, `load-image`) — serialize heap arena, symbol tables, package state, and bytecode pools to disk; restore to skip boot.lisp loading and resume a saved environment
- [ ] Standalone executables — prepend runtime binary to a saved image to produce a single self-contained executable (like SBCL's `save-lisp-and-die :executable t`)
- [ ] 64-bit `CL_Obj` on 64-bit hosts — conditional `uint64_t` representation to break the ~4GB arena limit on Linux/macOS; `uint32_t` stays on Amiga; requires widening stack slots, bytecode operands, and GC tag logic; bytecode would no longer be binary-compatible across platforms (already different due to pointer width)

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
│   ├── core/              # Language implementation (35 .c + 20 .h modules)
│   └── platform/          # OS abstraction (posix, amiga)
├── include/
│   └── clamiga.h          # Public umbrella header
├── lib/
│   └── boot.lisp          # Bootstrap macros/functions
├── tests/
│   ├── test.h             # Test framework
│   ├── test_*.c           # Host test suites (11 files, 1017 tests)
│   └── amiga/
│       └── run-tests.lisp # AmigaOS batch tests (1389 tests)
├── build/                 # Build output (gitignored)
└── verify/
    └── realamiga/          # FS-UAE config + AmigaOS system image
```

## Known Bugs

- [ ] **Amiga crash on heap exhaustion** — when the heap is exhausted (e.g. `(fact 3000)` with tail-recursive bignum factorial), the error is reported but the application crashes instead of recovering gracefully back to the REPL. Should signal a `storage-condition` and return to the REPL prompt.
- [ ] **`--heap` overflow for sizes >= 4G** — `parse_size()` overflows `uint32_t` silently (e.g. `--heap 4G` wraps to 0). Should cap at ~3.5GB and give a clear error for larger values.

## Verification Targets

1. `(+ 1 2)` → `3` ✅
2. `(defun fact (n) (if (<= n 1) 1 (* n (fact (1- n)))))` then `(fact 10)` → `3628800` ✅ / `(fact 30)` → `265252859812191058636308480000000` (bignum) ✅
3. `(mapcar #'1+ '(1 2 3))` → `(2 3 4)` ✅
4. Cross-compile to m68k, run in FS-UAE ✅
5. Run on real A1200 hardware
6. Load and execute multi-file Lisp programs
7. `(require "asdf")` — load and run ASDF 3.3
