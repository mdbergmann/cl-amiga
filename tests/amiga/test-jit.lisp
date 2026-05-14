;;; test-jit.lisp — m68k template JIT verification.
;;;
;;; Loaded from run-tests.lisp via (load ...) inside the #+amigaos
;;; block.  Kept separate so JIT coverage can grow on its own cadence
;;; without bloating the core test file, and so it can be run in
;;; isolation while iterating on codegen.
;;;
;;; Tests cover three things end-to-end on real m68k:
;;;   - byte emission (encoder pipeline writes the expected opcodes)
;;;   - dispatch engagement (cl_jit_invoke counter bumps on call)
;;;   - behavioral correctness (native return value matches bytecode)
;;;
;;; Relies on `*pass-count*` / `*fail-count*` and the `check` helper
;;; established by run-tests.lisp.

; --- Byte-pipeline smoke: %JIT-COMPILE-STUB writes NOP+RTS into a
; function's native_code slot, %JIT-DUMP-BYTES reads it back. ---

; Use a 2-arg function so neither the trivial-leaf nor the 1-arg
; identity matcher auto-compiles it; we need a clean "no native_code
; yet" baseline to verify %JIT-COMPILE-STUB attaches the stub bytes.
(defun jit-stub-test-fn (x y) (cons x y))
(check "jit-dump-before-stub" nil (clamiga::%jit-dump-bytes #'jit-stub-test-fn))
(check "jit-compile-stub-succeeds" t (clamiga::%jit-compile-stub #'jit-stub-test-fn))
; NOP = 0x4E71, RTS = 0x4E75 → bytes 78 113 78 117
(check "jit-dump-after-stub" '(78 113 78 117) (clamiga::%jit-dump-bytes #'jit-stub-test-fn))

; --- Round-trip: trivial `() -> NIL` function actually runs as
; native code.  Compiler emits moveq #0,d0 ; rts; OP_CALL dispatches
; into it; counter bumps prove the native path was taken (since the
; bytecode interpreter would return the same value). ---
(defun jit-roundtrip-nil () nil)
; MOVEQ #0,d0 = 0x7000 → 0x70 0x00 = 112 0
; RTS         = 0x4E75 → 0x4E 0x75 = 78 117
(check "jit-roundtrip-bytes" '(112 0 78 117)
  (clamiga::%jit-dump-bytes #'jit-roundtrip-nil))
(check "jit-roundtrip-counter-bump" t
  (let ((before (clamiga::%jit-invoke-count)))
    (jit-roundtrip-nil)
    (> (clamiga::%jit-invoke-count) before)))
(check "jit-roundtrip-returns-nil" nil (jit-roundtrip-nil))

; --- Literal-leaf coverage: OP_T, OP_CONST fixnum (fits moveq),
; OP_CONST fixnum (needs move.l), negative fixnum (sign-extension
; round-trip through moveq).  Each verifies exact emitted bytes plus
; behavioral correctness. ---

; Small positive fixnum 42: tagged = (42<<1)|1 = 85 = 0x55.
; Fits signed 8-bit → moveq #85,d0 ; rts → bytes 0x70 0x55 0x4E 0x75.
(defun jit-rt-fix-small () 42)
(check "jit-rt-fix-small-bytes" '(112 85 78 117)
  (clamiga::%jit-dump-bytes #'jit-rt-fix-small))
(check "jit-rt-fix-small-returns" 42 (jit-rt-fix-small))

; Negative fixnum -5: tagged = ((-5)<<1)|1 = 0xFFFFFFF7 = signed -9.
; Fits signed 8-bit as 0xF7 → moveq #-9,d0 ; rts → bytes
; 0x70 0xF7 0x4E 0x75 (sign-extended back to 0xFFFFFFF7 on execute).
(defun jit-rt-fix-neg () -5)
(check "jit-rt-fix-neg-bytes" '(112 247 78 117)
  (clamiga::%jit-dump-bytes #'jit-rt-fix-neg))
(check "jit-rt-fix-neg-returns" -5 (jit-rt-fix-neg))

; Larger fixnum 1000: tagged = 2001 = 0x000007D1.  Doesn't fit
; signed 8-bit, falls back to move.l #imm32,d0 ; rts:
;   0x20 0x3C  0x00 0x00 0x07 0xD1  0x4E 0x75
(defun jit-rt-fix-big () 1000)
(check "jit-rt-fix-big-bytes" '(32 60 0 0 7 209 78 117)
  (clamiga::%jit-dump-bytes #'jit-rt-fix-big))
(check "jit-rt-fix-big-returns" 1000 (jit-rt-fix-big))

; OP_T → CL_T is a heap pointer (runtime-allocated symbol), so the
; exact bytes vary across runs.  Verify the *shape*: 8 bytes,
; starts with the move.l-immediate-to-d0 opcode (0x203C), ends with
; RTS (0x4E75) — and that the function actually returns T.
(defun jit-rt-t () t)
(check "jit-rt-t-returns" t (jit-rt-t))
(check "jit-rt-t-shape" t
  (let ((bs (clamiga::%jit-dump-bytes #'jit-rt-t)))
    (and (= 8 (length bs))
         (= 32 (nth 0 bs)) (= 60 (nth 1 bs))   ; 0x20 0x3C
         (= 78 (nth 6 bs)) (= 117 (nth 7 bs))))) ; 0x4E 0x75

; --- 1-arg identity: (defun f (x) x) compiles to
;   move.l 4(a7),d0    ; 0x20 0x2F 0x00 0x04
;   rts                ; 0x4E 0x75
; (6 bytes).  C ABI on m68k puts the first arg at 4(sp) after JSR;
; cl_jit_invoke casts native_code to (CL_Obj (*)(CL_Obj)) and passes
; the arg directly.  The returned CL_Obj is whatever bit pattern the
; caller passed — fixnums, symbols, conses all round-trip without
; reinterpretation. ---
(defun jit-id (x) x)
(check "jit-id-bytes" '(32 47 0 4 78 117)
  (clamiga::%jit-dump-bytes #'jit-id))
(check "jit-id-counter-bump" t
  (let ((before (clamiga::%jit-invoke-count)))
    (jit-id 42)
    (> (clamiga::%jit-invoke-count) before)))
(check "jit-id-fixnum-small" 7   (jit-id 7))
(check "jit-id-fixnum-neg"   -3  (jit-id -3))
(check "jit-id-fixnum-big"   1000 (jit-id 1000))
(check "jit-id-nil"          nil (jit-id nil))
(check "jit-id-t"            t   (jit-id t))
(check "jit-id-symbol"       'foo (jit-id 'foo))
(check "jit-id-cons"         '(1 2 3) (jit-id '(1 2 3)))
(check "jit-id-string"       "hello" (jit-id "hello"))

; --- 2-arg pass-through: same template as 1-arg identity, just a
; different stack displacement.  Returning the first arg emits the
; same 6 bytes as 1-arg identity (move.l 4(a7),d0 ; rts); returning
; the second arg shifts the displacement to 8(a7) → bytes
; 0x20 0x2F 0x00 0x08 0x4E 0x75.  The behavioral test then proves
; cl_jit_invoke's 2-arg dispatch loads both args off the VM stack and
; passes them in the right order. ---
(defun jit-2arg-fst (x y) x)
(defun jit-2arg-snd (x y) y)
(check "jit-2arg-fst-bytes" '(32 47 0 4 78 117)
  (clamiga::%jit-dump-bytes #'jit-2arg-fst))
(check "jit-2arg-snd-bytes" '(32 47 0 8 78 117)
  (clamiga::%jit-dump-bytes #'jit-2arg-snd))
(check "jit-2arg-counter-bump" t
  (let ((before (clamiga::%jit-invoke-count)))
    (jit-2arg-fst 1 2)
    (jit-2arg-snd 3 4)
    (= (+ before 2) (clamiga::%jit-invoke-count))))
(check "jit-2arg-fst-fixnum" 11 (jit-2arg-fst 11 22))
(check "jit-2arg-snd-fixnum" 22 (jit-2arg-snd 11 22))
(check "jit-2arg-fst-mixed" 'a   (jit-2arg-fst 'a "b"))
(check "jit-2arg-snd-mixed" "b"  (jit-2arg-snd 'a "b"))
(check "jit-2arg-fst-distinguishes-args"
       'left  (jit-2arg-fst 'left 'right))
(check "jit-2arg-snd-distinguishes-args"
       'right (jit-2arg-snd 'left 'right))

; --- Higher arities: same matcher / template, different switch case
; in cl_jit_invoke.  Cover arity 3 (middle slot) and arity 6 (the cap,
; CL_JIT_PASSTHROUGH_MAX_ARITY).  Each emits move.l (4+4*j)(a7),d0 ;
; rts where j is the source slot.  The 6-arg case proves all six
; switch arms load args in the correct order. ---
(defun jit-3arg-mid (x y z) y)
(check "jit-3arg-mid-bytes" '(32 47 0 8 78 117)
  (clamiga::%jit-dump-bytes #'jit-3arg-mid))
(check "jit-3arg-mid-returns" 'b (jit-3arg-mid 'a 'b 'c))

(defun jit-6arg-1 (a b c d e f) a)
(defun jit-6arg-6 (a b c d e f) f)
(check "jit-6arg-1-bytes" '(32 47 0 4 78 117)
  (clamiga::%jit-dump-bytes #'jit-6arg-1))
(check "jit-6arg-6-bytes" '(32 47 0 24 78 117)
  (clamiga::%jit-dump-bytes #'jit-6arg-6))
(check "jit-6arg-1-returns" 'first  (jit-6arg-1 'first 2 3 4 5 'last))
(check "jit-6arg-6-returns" 'last   (jit-6arg-6 'first 2 3 4 5 'last))

; --- Per-opcode walker.  Fires only for shapes the whole-function
; matchers reject.  Uses LINK/UNLK to set up an A6 frame and the m68k
; hardware stack as the operand stack — much larger native code per
; function (≥22 bytes vs the 4–8 bytes the matchers produce) but
; covers arbitrary compositions of the supported opcodes (OP_NIL,
; OP_T, OP_CONST, OP_LOAD, OP_STORE, OP_POP, OP_RET).
;
; (defun walker-nil-1arg (x) nil) bytecode:
;   NIL ; STORE 1 ; POP ; LOAD 1 ; RET   (7 bytes)
; arity=1, n_locals=2 → 1 extra local → LINK A6,#-4.
; Slot 1 is the block-return local at -4(a6).
;
; Expected native (22 bytes):
;   78 86 255 252   ; link a6,#-4
;   66 167          ; clr.l -(a7)             — OP_NIL
;   45 87 255 252   ; move.l (a7),-4(a6)      — OP_STORE 1
;   88 143          ; addq.l #4,a7            — OP_POP
;   47 46 255 252   ; move.l -4(a6),-(a7)     — OP_LOAD 1
;   32 31           ; move.l (a7)+,d0        \
;   78 94           ; unlk a6                 } — OP_RET
;   78 117          ; rts                    /
(defun walker-nil-1arg (x) nil)
(check "walker-nil-1arg-bytes"
  '(78 86 255 252  66 167  45 87 255 252  88 143  47 46 255 252
    32 31  78 94  78 117)
  (clamiga::%jit-dump-bytes #'walker-nil-1arg))
(check "walker-nil-1arg-counter-bump" t
  (let ((before (clamiga::%jit-invoke-count)))
    (walker-nil-1arg 99)
    (> (clamiga::%jit-invoke-count) before)))
(check "walker-nil-1arg-returns-nil"    nil (walker-nil-1arg 99))
(check "walker-nil-1arg-ignores-arg"    nil (walker-nil-1arg 'anything))

; Constant return via OP_CONST: (defun walker-fix-1arg (x) 42).
; Fixnum 42 tagged = (42<<1)|1 = 85 = 0x55, embedded as 32-bit
; big-endian immediate in MOVE.L #imm32,-(a7) → bytes 47 60 0 0 0 85.
(defun walker-fix-1arg (x) 42)
(check "walker-fix-1arg-bytes"
  '(78 86 255 252  47 60 0 0 0 85  45 87 255 252  88 143
    47 46 255 252  32 31 78 94 78 117)
  (clamiga::%jit-dump-bytes #'walker-fix-1arg))
(check "walker-fix-1arg-returns" 42 (walker-fix-1arg 'ignored))

; OP_T: shape is identical to OP_NIL+1 except OP_T is 6 bytes (MOVE.L
; #CL_T,-(a7)) vs OP_NIL's 2 bytes (CLR.L -(a7)).  CL_T is a runtime
; pointer so its 4-byte value varies across boots — verify size and
; behavior, not the embedded immediate.
(defun walker-t-1arg (x) t)
(check "walker-t-1arg-size" 26
  (length (clamiga::%jit-dump-bytes #'walker-t-1arg)))
(check "walker-t-1arg-returns-t" t (walker-t-1arg nil))

; Real local-slot use: LET binds an extra slot above the block-return.
; (defun walker-let-id (x) (let ((y x)) y))
;   arity=1, n_locals=3 (x=slot 0, block-return=slot 1, y=slot 2)
;   bytecode: LOAD 0 ; STORE 2 ; POP ; LOAD 2 ; STORE 1 ; POP ; LOAD 1 ; RET
; Exercises the parameter-slot path (slot 0 at 8(a6)) AND extra-local
; path (slots 1 & 2 below a6) within the same function.  Byte-exact
; would be brittle; behavior is what matters.
(defun walker-let-id (x) (let ((y x)) y))
(check "walker-let-id-fixnum" 7        (walker-let-id 7))
(check "walker-let-id-symbol" 'banana  (walker-let-id 'banana))
(check "walker-let-id-cons"   '(1 . 2) (walker-let-id (cons 1 2)))
(check "walker-let-id-counter-bump" t
  (let ((before (clamiga::%jit-invoke-count)))
    (walker-let-id 1)
    (> (clamiga::%jit-invoke-count) before)))

; Negative: a function whose body contains an opcode the walker
; doesn't handle (OP_CONS) must leave native_code NULL and run via
; the interpreter.  walker-cons-fallback duplicates jit-stub-test-fn's
; rejection shape with a distinct name so the test reads cleanly.
(defun walker-cons-fallback (x y) (cons x y))
(check "walker-cons-fallback-no-native"
  nil (clamiga::%jit-dump-bytes #'walker-cons-fallback))
(check "walker-cons-fallback-still-works"
  '(1 . 2) (walker-cons-fallback 1 2))
(check "walker-cons-fallback-no-counter-bump" t
  (let ((before (clamiga::%jit-invoke-count)))
    (walker-cons-fallback 3 4)
    (= before (clamiga::%jit-invoke-count))))

; --- Branches (OP_JMP / OP_JNIL / OP_JTRUE) ------------------------------
;
; `(if x 1 2)` compiles to LOAD 0 ; JNIL else ; CONST 1 ; JMP end ;
; else: CONST 2 ; end: STORE/POP/LOAD/RET — i.e. exercises both
; forward JNIL and forward JMP plus the patch-resolution loop.
;
; CONST indices and tagged-fixnum bytes depend on the constant pool's
; layout, so byte-exact would be brittle.  Instead verify two stable
; properties: (1) the function JITs (counter bumps), (2) both
; branch arms produce the right return value across argument types
; that exercise the truthiness path (NIL → else, anything else →
; then).

(defun walker-if-1-2 (x) (if x 1 2))
(check "walker-if-1-2-counter-bump" t
  (let ((before (clamiga::%jit-invoke-count)))
    (walker-if-1-2 t)
    (> (clamiga::%jit-invoke-count) before)))
(check "walker-if-1-2-then-t"      1 (walker-if-1-2 t))
(check "walker-if-1-2-then-fixnum" 1 (walker-if-1-2 42))
(check "walker-if-1-2-then-symbol" 1 (walker-if-1-2 'anything))
(check "walker-if-1-2-then-cons"   1 (walker-if-1-2 (cons 1 2)))
(check "walker-if-1-2-else-nil"    2 (walker-if-1-2 nil))

; `(if x x y)` — then-branch returns the test value (so JIT'd code
; threads the same arg through both LOAD-and-test and the result),
; else-branch returns y (a second parameter that lives at 12(a6)).
(defun walker-if-x-y (x y) (if x x y))
(check "walker-if-x-y-then-fixnum" 7   (walker-if-x-y 7 99))
(check "walker-if-x-y-then-symbol" 'a  (walker-if-x-y 'a 'b))
(check "walker-if-x-y-then-cons"   '(1 . 2) (walker-if-x-y '(1 . 2) nil))
(check "walker-if-x-y-else-fixnum" 99  (walker-if-x-y nil 99))
(check "walker-if-x-y-else-sym"    'b  (walker-if-x-y nil 'b))

; `(when x v)` collapses to (if x v nil) — the NIL else-branch
; exercises the OP_NIL walker case as the else-target rather than
; OP_CONST.  Returns v on truthy x, NIL otherwise.
(defun walker-when-v (x) (when x 'taken))
(check "walker-when-v-truthy" 'taken (walker-when-v t))
(check "walker-when-v-nil"    nil    (walker-when-v nil))

; `(unless x v)` → (if x nil v).  Symmetric coverage to when.
(defun walker-unless-v (x) (unless x 'skipped))
(check "walker-unless-v-truthy" nil      (walker-unless-v t))
(check "walker-unless-v-nil"    'skipped (walker-unless-v nil))

; --- OP_DUP behavioral coverage via cond's empty-body clause ---------
;
; `(cond (x))` compiles to LOAD x ; DUP ; JNIL else ; JMP end ;
; else: NIL ; end: ... — so the test value is returned itself when
; truthy, NIL otherwise.  This is the only Lisp shape that emits
; OP_DUP without also emitting OP_MV_RESET (which the walker still
; rejects), so it doubles as the OP_DUP regression test.
(defun walker-cond-empty-body (x) (cond (x)))
(check "walker-cond-empty-body-counter-bump" t
  (let ((before (clamiga::%jit-invoke-count)))
    (walker-cond-empty-body t)
    (> (clamiga::%jit-invoke-count) before)))
(check "walker-cond-empty-body-truthy-t"      t      (walker-cond-empty-body t))
(check "walker-cond-empty-body-truthy-fixnum" 7      (walker-cond-empty-body 7))
(check "walker-cond-empty-body-truthy-sym"    'foo   (walker-cond-empty-body 'foo))
(check "walker-cond-empty-body-falsey-nil"    nil    (walker-cond-empty-body nil))

; Two-clause cond — exercises the cascade of JMPs plus a NIL fall-
; through at the end.  `(cond ((eq x 'a) 1) ((eq x 'b) 2))` would
; normally include OP_EQ which we don't handle yet; use (cond (x 1))
; instead — JNIL else ; CONST 1 ; JMP end ; else: NIL ; end: ... ,
; same building blocks as if-1-2 minus the second CONST.
(defun walker-cond-only-true (x) (cond (x 1)))
(check "walker-cond-only-true-truthy" 1   (walker-cond-only-true 'anything))
(check "walker-cond-only-true-falsey" nil (walker-cond-only-true nil))

; --- Negative: branch range overflow.  The walker bails when a
; 16-bit branch displacement won't fit.  There's no realistic way to
; provoke this from hand-written Lisp at this scale, so the test is
; left implicit: any function the walker accepts has fit within
; range, and the full Amiga test suite running 2321+ tests through
; the JIT'd pipeline is the regression net.
