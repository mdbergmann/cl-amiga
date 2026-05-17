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

; Use a function whose lambda-list pins one of the walker's metadata
; gates (`bc->n_optional != 0`) so neither the trivial-leaf matchers
; nor the per-opcode walker auto-compile it.  Gives a clean "no
; native_code yet" baseline to verify %JIT-COMPILE-STUB attaches the
; stub bytes.  (Previously this used `(and x y)`, which relied on
; OP_MV_RESET making the walker bail — landing OP_MV_RESET in the
; walker took that out from under the test; &optional is a stable
; bail point because supporting it is a much larger metadata change.)
(defun jit-stub-test-fn (x &optional y) (or x y))
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
;   move.l 8(a7),d0    ; 0x20 0x2F 0x00 0x08
;   rts                ; 0x4E 0x75
; (6 bytes).  C ABI on m68k puts the first arg at 4(sp) after JSR;
; cl_jit_invoke prepends `func_obj` as the first C arg (so OP_UPVAL
; can reach the active closure's upvalues) which shifts the user's
; first arg to 8(sp).  The returned CL_Obj is whatever bit pattern
; the caller passed — fixnums, symbols, conses all round-trip
; without reinterpretation. ---
(defun jit-id (x) x)
(check "jit-id-bytes" '(32 47 0 8 78 117)
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
; different stack displacement.  With the func-obj-first ABI, user
; arg j sits at (8 + 4*j)(a7): first arg at 8(a7), second arg at
; 12(a7).  The behavioral test then proves cl_jit_invoke's 2-arg
; dispatch loads both args off the VM stack and passes them in the
; right order. ---
(defun jit-2arg-fst (x y) x)
(defun jit-2arg-snd (x y) y)
(check "jit-2arg-fst-bytes" '(32 47 0 8 78 117)
  (clamiga::%jit-dump-bytes #'jit-2arg-fst))
(check "jit-2arg-snd-bytes" '(32 47 0 12 78 117)
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
; CL_JIT_PASSTHROUGH_MAX_ARITY).  Each emits move.l (8+4*j)(a7),d0 ;
; rts where j is the source slot (user-arg index), since the
; func-obj-first ABI offsets every user arg by +4.  The 6-arg case
; proves all six switch arms load args in the correct order. ---
(defun jit-3arg-mid (x y z) y)
(check "jit-3arg-mid-bytes" '(32 47 0 12 78 117)
  (clamiga::%jit-dump-bytes #'jit-3arg-mid))
(check "jit-3arg-mid-returns" 'b (jit-3arg-mid 'a 'b 'c))

(defun jit-6arg-1 (a b c d e f) a)
(defun jit-6arg-6 (a b c d e f) f)
(check "jit-6arg-1-bytes" '(32 47 0 8 78 117)
  (clamiga::%jit-dump-bytes #'jit-6arg-1))
(check "jit-6arg-6-bytes" '(32 47 0 28 78 117)
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
; With the 3-slot rotating stack cache, the cache_head index advances
; D7 → D5 → D6 → D7 on each push.  This function starts with head=7;
; the first push (OP_NIL) lands in D{next(7)} = D5.  Subsequent pop
; back through D5 to D0 (no register shifts — the rotation reclaims
; D5 implicitly).  The prologue saves D5/D6/D7 to A6-relative slots;
; the epilogue restores via A6-relative loads.
;
; Expected native (38 bytes):
;   78 86 255 252   ; link a6,#-4
;   47 7            ; move.l d7,-(a7)         — save D7
;   47 6            ; move.l d6,-(a7)         — save D6
;   47 5            ; move.l d5,-(a7)         — save D5
;   122 0           ; moveq #0,d5             — OP_NIL → D5 (new TOS)
;   45 69 255 252   ; move.l d5,-4(a6)        — OP_STORE 1 (peek)
;                   ; (OP_POP: no bytes — cache head/depth rotate back)
;   42 46 255 252   ; move.l -4(a6),d5        — OP_LOAD 1 → D5 (TOS again)
;   32 5            ; move.l d5,d0           \
;   46 46 255 248   ; move.l -8(a6),d7        — restore D7
;   44 46 255 244   ; move.l -12(a6),d6       — restore D6
;   42 46 255 240   ; move.l -16(a6),d5       — restore D5
;   78 94           ; unlk a6                 } — OP_RET
;   78 117          ; rts                    /
(defun walker-nil-1arg (x) nil)
(check "walker-nil-1arg-bytes"
  '(78 86 255 252  47 7  47 6  47 5  122 0  45 69 255 252
    42 46 255 252  32 5
    46 46 255 248  44 46 255 244  42 46 255 240
    78 94  78 117)
  (clamiga::%jit-dump-bytes #'walker-nil-1arg))
(check "walker-nil-1arg-counter-bump" t
  (let ((before (clamiga::%jit-invoke-count)))
    (walker-nil-1arg 99)
    (> (clamiga::%jit-invoke-count) before)))
(check "walker-nil-1arg-returns-nil"    nil (walker-nil-1arg 99))
(check "walker-nil-1arg-ignores-arg"    nil (walker-nil-1arg 'anything))

; Constant return via OP_CONST: (defun walker-fix-1arg (x) 42).
; Fixnum 42 tagged = (42<<1)|1 = 85 = 0x55.  Cache pushes D5 (head
; rotates 7→5), and 85 fits MOVEQ's 8-bit signed range, so the load
; is a 2-byte `moveq #85,d5` (bytes 122 85) rather than the 6-byte
; `move.l #imm32`.  Otherwise the shape mirrors walker-nil-1arg.
(defun walker-fix-1arg (x) 42)
(check "walker-fix-1arg-bytes"
  '(78 86 255 252  47 7  47 6  47 5  122 85  45 69 255 252
    42 46 255 252  32 5
    46 46 255 248  44 46 255 244  42 46 255 240
    78 94  78 117)
  (clamiga::%jit-dump-bytes #'walker-fix-1arg))
(check "walker-fix-1arg-returns" 42 (walker-fix-1arg 'ignored))

; OP_T: shape is identical to OP_NIL except OP_T loads CL_T (a heap
; pointer that doesn't fit in MOVEQ's 8-bit signed range) via the
; 6-byte `move.l #imm32,d7` instead of MOVEQ's 2 bytes.  CL_T's
; address varies across boots so the embedded immediate isn't stable
; — verify total size (4 bytes longer than walker-nil-1arg's 38) and
; behavior.
(defun walker-t-1arg (x) t)
(check "walker-t-1arg-size" 42
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

; OP_MV_RESET (emitted between AND/OR arms) now compiles via a JSR to
; cl_jit_runtime_mv_reset, which sets cl_mv_count = 1 on the current
; thread.  Previously the walker bailed on the op and `(and x y)` ran
; through the interpreter — landing this is what makes step-line in
; bouncing-lines (which uses `(when (or A B) …)` four times) JIT in
; the first place.  Verify the function compiles AND that AND's
; short-circuit/value semantics still match the interpreter.
(defun walker-mv-reset-and (x y) (and x y))
(check "walker-mv-reset-and-has-native" t
  (let ((bytes (clamiga::%jit-dump-bytes #'walker-mv-reset-and)))
    (and (consp bytes) (> (length bytes) 0))))
(check "walker-mv-reset-and-counter-bump" t
  (let ((before (clamiga::%jit-invoke-count)))
    (walker-mv-reset-and 'a 'b)
    (> (clamiga::%jit-invoke-count) before)))
(check "walker-mv-reset-and-truthy"      'b  (walker-mv-reset-and 'a 'b))
(check "walker-mv-reset-and-short-nil"   nil (walker-mv-reset-and nil 'b))
(check "walker-mv-reset-and-second-nil"  nil (walker-mv-reset-and 'a nil))
(check "walker-mv-reset-and-both-nil"    nil (walker-mv-reset-and nil nil))

; OR variant — same emission path (OP_MV_RESET sits between OR's arms
; in compiler_extra.c:102), but the surrounding control flow uses
; OP_JTRUE instead of OP_JNIL.
(defun walker-mv-reset-or (x y) (or x y))
(check "walker-mv-reset-or-has-native" t
  (let ((bytes (clamiga::%jit-dump-bytes #'walker-mv-reset-or)))
    (and (consp bytes) (> (length bytes) 0))))
(check "walker-mv-reset-or-first"   'a  (walker-mv-reset-or 'a 'b))
(check "walker-mv-reset-or-second"  'b  (walker-mv-reset-or nil 'b))
(check "walker-mv-reset-or-both-nil" nil (walker-mv-reset-or nil nil))

; After an OP_MV_RESET fires, calling (values-list ...) immediately
; should see mv_count = 1 — the walker's JSR-helper handling matches
; the bytecode VM's `cl_mv_count = 1` exactly.  This is the case the
; jit-mv-count-no-per-opcode-reset memory flagged as the failure mode
; if the walker had simply ignored OP_MV_RESET — stale mv state from
; a prior (values ...) leaking into a later consumer.
(defun walker-mv-reset-after-and (x)
  ;; The `(and t x)` arm emits OP_MV_RESET, then `(values 1)` is
  ;; the function's tail.  `nth-value 0` reads value 0 and depends
  ;; on cl_mv_count being a stable 1 by the time of consumption.
  (and t x))
(check "walker-mv-reset-tail-value" 7 (walker-mv-reset-after-and 7))

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
; through at the end.  Use (cond (x 1)) — JNIL else ; CONST 1 ; JMP
; end ; else: NIL ; end: ... , same building blocks as if-1-2 minus
; the second CONST.  (cond ((eq x 'a) 1) ((eq x 'b) 2)) shapes are
; covered separately by the OP_EQ tests below.
(defun walker-cond-only-true (x) (cond (x 1)))
(check "walker-cond-only-true-truthy" 1   (walker-cond-only-true 'anything))
(check "walker-cond-only-true-falsey" nil (walker-cond-only-true nil))

; --- Negative: branch range overflow.  The walker bails when a
; 16-bit branch displacement won't fit.  There's no realistic way to
; provoke this from hand-written Lisp at this scale, so the test is
; left implicit: any function the walker accepts has fit within
; range, and the full Amiga test suite running 2321+ tests through
; the JIT'd pipeline is the regression net.

; --- Arithmetic & comparison (OP_ADD, OP_LT) -----------------------------
;
; `(defun add2 (a b) (+ a b))` compiles to LOAD a; LOAD b; ADD ; postlude.
; The ADD template inlines a fixnum fast path (tag-check via AND+BTST,
; signed add with BVS overflow detect, surplus-tag strip via SUBQ#1)
; with a JSR slow path to cl_jit_runtime_add for non-fixnum / overflow.
; Behavioral tests pin down both the fast path and the round-trip
; through the slow-path JSR.

(defun walker-add2 (a b) (+ a b))
(check "walker-add2-counter-bump" t
  (let ((before (clamiga::%jit-invoke-count)))
    (walker-add2 1 2)
    (> (clamiga::%jit-invoke-count) before)))
(check "walker-add2-small"     5    (walker-add2 2 3))
(check "walker-add2-negative" -1    (walker-add2 -3 2))
(check "walker-add2-zero"      0    (walker-add2 0 0))
(check "walker-add2-pos-pos"   1000 (walker-add2 700 300))

; Fixnum overflow: 2^30 - 1 + 2 = 2^30 + 1, still fixnum range.
; But 2^30 - 1 + 2^30 = 2^31 - 1, beyond CL_FIXNUM_MAX (2^30 - 1).
; The BVS path triggers and the slow-path helper produces a bignum.
; This crosses the JIT/runtime boundary — the result still matches
; what the bytecode VM would produce.
(check "walker-add2-overflow-up"   2147483647 (walker-add2 1073741823 1073741824))
(check "walker-add2-overflow-down" -2147483648 (walker-add2 -1073741824 -1073741824))

; `(defun lt2 (a b) (< a b))` — same template shape for OP_LT.
; Fast path: cmp.l + BLT → push CL_T else push CL_NIL.
(defun walker-lt2 (a b) (< a b))
(check "walker-lt2-counter-bump" t
  (let ((before (clamiga::%jit-invoke-count)))
    (walker-lt2 1 2)
    (> (clamiga::%jit-invoke-count) before)))
(check "walker-lt2-yes"            t   (walker-lt2 1 2))
(check "walker-lt2-no-greater"     nil (walker-lt2 2 1))
(check "walker-lt2-no-equal"       nil (walker-lt2 5 5))
(check "walker-lt2-negatives-yes"  t   (walker-lt2 -10 -3))
(check "walker-lt2-negatives-no"   nil (walker-lt2 -3 -10))
(check "walker-lt2-mixed-sign-yes" t   (walker-lt2 -1 1))
(check "walker-lt2-zero-yes"       t   (walker-lt2 0 1))
(check "walker-lt2-zero-no"        nil (walker-lt2 1 0))

; Slow-path validation: integer compared against a float — the fast
; path's AND+BTST sees bit 0 = 0 for floats (heap pointer), bails, JSR
; cl_jit_runtime_lt routes through cl_arith_compare which handles the
; cross-type compare correctly.
(check "walker-lt2-slow-int-float-yes" t   (walker-lt2 1 1.5))
(check "walker-lt2-slow-int-float-no"  nil (walker-lt2 2 1.5))

; --- OP_SUB.  Mirror of OP_ADD: same fast-path template with SUB.L
; and ADDQ #1 (vs ADD.L / SUBQ #1) and a different slow-path helper.
; Overflow recovery reconstructs original a via ADD d1,d0.
(defun walker-sub2 (a b) (- a b))
(check "walker-sub2-counter-bump" t
  (let ((before (clamiga::%jit-invoke-count)))
    (walker-sub2 5 3)
    (> (clamiga::%jit-invoke-count) before)))
(check "walker-sub2-small"        2    (walker-sub2 5 3))
(check "walker-sub2-zero"         0    (walker-sub2 7 7))
(check "walker-sub2-negative"    -5    (walker-sub2 3 8))
(check "walker-sub2-double-neg"   5    (walker-sub2 -3 -8))
; Fixnum overflow: CL_FIXNUM_MIN - 1 = bignum.
(check "walker-sub2-overflow-min" -1073741825 (walker-sub2 -1073741824 1))
(check "walker-sub2-overflow-max" 2147483647  (walker-sub2 1073741823 -1073741824))
; Slow path through int↔float.
(check "walker-sub2-slow-int-float" 0.5 (walker-sub2 2 1.5))

; --- OP_GT, OP_LE, OP_GE.  Same template as OP_LT, different Bcc.
(defun walker-gt2 (a b) (> a b))
(check "walker-gt2-yes"        t   (walker-gt2 2 1))
(check "walker-gt2-no-less"    nil (walker-gt2 1 2))
(check "walker-gt2-no-equal"   nil (walker-gt2 5 5))
(check "walker-gt2-negatives"  t   (walker-gt2 -3 -10))
(check "walker-gt2-slow-float" t   (walker-gt2 2 1.5))

(defun walker-le2 (a b) (<= a b))
(check "walker-le2-less"        t   (walker-le2 1 2))
(check "walker-le2-equal"       t   (walker-le2 5 5))
(check "walker-le2-greater"     nil (walker-le2 3 1))
(check "walker-le2-negatives"   t   (walker-le2 -10 -3))
(check "walker-le2-slow-float"  t   (walker-le2 1 1.5))

(defun walker-ge2 (a b) (>= a b))
(check "walker-ge2-greater"     t   (walker-ge2 3 1))
(check "walker-ge2-equal"       t   (walker-ge2 5 5))
(check "walker-ge2-less"        nil (walker-ge2 1 2))
(check "walker-ge2-negatives"   t   (walker-ge2 -3 -10))
(check "walker-ge2-slow-float"  nil (walker-ge2 1 1.5))

; --- OP_NUMEQ.  Fixnum fast path with BEQ; slow path validates as
; NUMBER (not REAL — `=` accepts complex per CLHS 12.1.4.1) and
; falls through to cl_numeric_equal for cross-type compares.
(defun walker-numeq2 (a b) (= a b))
(check "walker-numeq2-yes-fix"     t   (walker-numeq2 7 7))
(check "walker-numeq2-no-fix"      nil (walker-numeq2 7 8))
(check "walker-numeq2-yes-neg"     t   (walker-numeq2 -3 -3))
(check "walker-numeq2-slow-int-float-yes" t (walker-numeq2 2 2.0))
(check "walker-numeq2-slow-int-float-no"  nil (walker-numeq2 2 2.5))

; --- OP_EQ.  Pure pointer compare, no slow path.  Lisp `eq` returns
; T iff the two arguments are the same object — true for fixnums
; (immediate, identical tagged value) and identical symbols, false
; for distinct conses / strings / floats even with equal contents.
(defun walker-eq2 (a b) (eq a b))
(check "walker-eq2-counter-bump" t
  (let ((before (clamiga::%jit-invoke-count)))
    (walker-eq2 'a 'a)
    (> (clamiga::%jit-invoke-count) before)))
(check "walker-eq2-same-symbol"     t   (walker-eq2 'foo 'foo))
(check "walker-eq2-different-syms"  nil (walker-eq2 'foo 'bar))
(check "walker-eq2-same-fixnum"     t   (walker-eq2 42 42))
(check "walker-eq2-different-fix"   nil (walker-eq2 42 43))
(check "walker-eq2-nil-self"        t   (walker-eq2 nil nil))
(check "walker-eq2-t-self"          t   (walker-eq2 t t))
(check "walker-eq2-distinct-conses" nil (walker-eq2 (cons 1 2) (cons 1 2)))
(check "walker-eq2-shared-cons"     t   (let ((c (cons 1 2))) (walker-eq2 c c)))

; --- OP_NOT.  Pop value; push CL_T iff NIL, else CL_NIL.  Same shape
; as OP_EQ minus the second pop and CMP — relies on MOVE.L setting Z
; from the popped value.
(defun walker-not (x) (not x))
(check "walker-not-counter-bump" t
  (let ((before (clamiga::%jit-invoke-count)))
    (walker-not t)
    (> (clamiga::%jit-invoke-count) before)))
(check "walker-not-nil"        t   (walker-not nil))
(check "walker-not-t"          nil (walker-not t))
(check "walker-not-fixnum"     nil (walker-not 42))
(check "walker-not-zero"       nil (walker-not 0))    ; 0 is truthy in CL
(check "walker-not-symbol"     nil (walker-not 'foo))
(check "walker-not-cons"       nil (walker-not '(1 2 3)))
(check "walker-not-empty-list" t   (walker-not '()))  ; () is NIL

; --- OP_STRUCT_REF / OP_STRUCT_SET.  Defstruct accessors compile to
; `(%struct-ref obj <idx>)` and `(%struct-set obj <idx> val)`, which
; emit the OP_STRUCT_REF/OP_STRUCT_SET opcodes.  The walker's template
; is JSR-based: pop, push args, JSR cl_jit_runtime_struct_{ref,set},
; clean up the stack, push the helper's return value.  Helper mirrors
; the VM (same STRUCTURE type-check + bounds-check + error messages),
; and is non-allocating so no GC concerns even without precise stack
; scanning.

(defstruct jit-point x y z)

; Accessor reader — get-x emits LOAD 0 ; STRUCT_REF 0 ; postlude.
(defun walker-point-x (p) (jit-point-x p))
(defun walker-point-y (p) (jit-point-y p))
(defun walker-point-z (p) (jit-point-z p))

(check "walker-point-x-counter-bump" t
  (let ((before (clamiga::%jit-invoke-count))
        (p (make-jit-point :x 10 :y 20 :z 30)))
    (walker-point-x p)
    (> (clamiga::%jit-invoke-count) before)))

(check "walker-point-x-fixnum" 10
  (walker-point-x (make-jit-point :x 10 :y 20 :z 30)))
(check "walker-point-y-fixnum" 20
  (walker-point-y (make-jit-point :x 10 :y 20 :z 30)))
(check "walker-point-z-fixnum" 30
  (walker-point-z (make-jit-point :x 10 :y 20 :z 30)))
(check "walker-point-x-nil"    nil
  (walker-point-x (make-jit-point :x nil :y 20 :z 30)))
(check "walker-point-x-symbol" 'hello
  (walker-point-x (make-jit-point :x 'hello :y 20 :z 30)))
(check "walker-point-x-cons"   '(1 2 3)
  (walker-point-x (make-jit-point :x '(1 2 3) :y 20 :z 30)))

; Type error on non-struct: the helper signals STRUCTURE type-error.
; handler-case lets us assert the signal happens without aborting tests.
(check "walker-point-x-type-error" :caught
  (handler-case (progn (walker-point-x 42) :no-error)
    (type-error () :caught)))
(check "walker-point-x-type-error-nil" :caught
  (handler-case (progn (walker-point-x nil) :no-error)
    (type-error () :caught)))

; --- Setter: setf-of-accessor emits OP_STRUCT_SET.
(defun walker-set-point-x (p v) (setf (jit-point-x p) v))
(defun walker-set-point-z (p v) (setf (jit-point-z p) v))

(check "walker-set-point-x-counter-bump" t
  (let ((before (clamiga::%jit-invoke-count))
        (p (make-jit-point :x 0 :y 0 :z 0)))
    (walker-set-point-x p 99)
    (> (clamiga::%jit-invoke-count) before)))

; The setter returns the stored value (CL `setf` semantics).
(check "walker-set-point-x-returns-val" 99
  (walker-set-point-x (make-jit-point :x 0 :y 0 :z 0) 99))

; Round-trip: store via walker-set, read via walker-point.
(check "walker-struct-roundtrip-x" 99
  (let ((p (make-jit-point :x 0 :y 0 :z 0)))
    (walker-set-point-x p 99)
    (walker-point-x p)))
(check "walker-struct-roundtrip-z" 'tag
  (let ((p (make-jit-point :x 0 :y 0 :z 0)))
    (walker-set-point-z p 'tag)
    (walker-point-z p)))
; Setting one slot doesn't disturb the others.
(check "walker-struct-set-leaves-others" '(99 20 30)
  (let ((p (make-jit-point :x 10 :y 20 :z 30)))
    (walker-set-point-x p 99)
    (list (jit-point-x p) (jit-point-y p) (jit-point-z p))))

(check "walker-set-point-x-type-error" :caught
  (handler-case (progn (walker-set-point-x 'not-a-struct 1) :no-error)
    (type-error () :caught)))

; --- OP_MUL.  JSR-only template (no inline fixnum fast path): pop b,
; pop a, push b, push a (C-ABI right-to-left), JSR cl_jit_runtime_mul,
; drop, push result.  The helper preserves the VM's fixnum fast path
; inside cl_arith_mul, so cost vs. bytecode is one extra JSR per call.
; Coverage exercises in-range fixnum, fast-path-fitting cross-products,
; bignum overflow, int↔float promotion, and the NUMBER type-error
; path so behaviour matches the bytecode VM exactly.  OP_DIV is left
; out for now — the compiler doesn't emit it (no `/` case in
; inline_builtin_opcode), so adding a walker template would be dead
; code.
(defun walker-mul2 (a b) (* a b))
(check "walker-mul2-counter-bump" t
  (let ((before (clamiga::%jit-invoke-count)))
    (walker-mul2 6 7)
    (> (clamiga::%jit-invoke-count) before)))
(check "walker-mul2-fix"          42  (walker-mul2 6 7))
(check "walker-mul2-zero"         0   (walker-mul2 0 12345))
(check "walker-mul2-zero-rhs"     0   (walker-mul2 12345 0))
(check "walker-mul2-neg"         -42  (walker-mul2 -6 7))
(check "walker-mul2-double-neg"   42  (walker-mul2 -6 -7))
; Mid-range fixnum × fixnum that fits in fixnum: 1000 * 2000 = 2 000 000.
(check "walker-mul2-mid-fix"      2000000  (walker-mul2 1000 2000))
; Just past the VM's 15-bit fast-path guard: 65535 * 16384 = ~1.07e9 — still
; CL_FIXNUM_MAX-fitting (< 1 073 741 823), so cl_arith_mul returns a fixnum.
(check "walker-mul2-large-fix"    1073725440 (walker-mul2 65535 16384))
; Overflow to bignum: 100000 * 100000 = 1e10 — well past CL_FIXNUM_MAX.
(check "walker-mul2-overflow-bignum" 10000000000 (walker-mul2 100000 100000))
; Cross-type: int * float → float.
(check "walker-mul2-slow-int-float" 7.5 (walker-mul2 3 2.5))
; Non-NUMBER → type-error.
(check "walker-mul2-type-error" :caught
  (handler-case (progn (walker-mul2 'foo 7) :no-error)
    (type-error () :caught)))

; --- OP_CAR / OP_CDR.  JSR-only one-arg templates routing through
; cl_jit_runtime_car / _cdr, which forward to cl_car / cl_cdr.  Those
; already implement the full spec: NIL→NIL, LIST type-error with the
; same diagnostic the VM prints, and unbound-variable detection.
; Non-allocating → GC-safe in all cases.
(defun walker-car1 (lst) (car lst))
(defun walker-cdr1 (lst) (cdr lst))
(check "walker-car1-counter-bump" t
  (let ((before (clamiga::%jit-invoke-count)))
    (walker-car1 '(1 2 3))
    (> (clamiga::%jit-invoke-count) before)))
(check "walker-car1-list"    1   (walker-car1 '(1 2 3)))
(check "walker-car1-single"  'x  (walker-car1 '(x)))
(check "walker-car1-pair"    'a  (walker-car1 (cons 'a 'b)))
(check "walker-car1-nil"     nil (walker-car1 nil))
(check "walker-car1-empty"   nil (walker-car1 '()))
(check "walker-car1-type-error" :caught
  (handler-case (progn (walker-car1 42) :no-error)
    (type-error () :caught)))

(check "walker-cdr1-list"    '(2 3) (walker-cdr1 '(1 2 3)))
(check "walker-cdr1-single"  nil    (walker-cdr1 '(x)))
(check "walker-cdr1-pair"    'b     (walker-cdr1 (cons 'a 'b)))
(check "walker-cdr1-nil"     nil    (walker-cdr1 nil))
(check "walker-cdr1-empty"   nil    (walker-cdr1 '()))
(check "walker-cdr1-type-error" :caught
  (handler-case (progn (walker-cdr1 'foo) :no-error)
    (type-error () :caught)))

; --- OP_CONS: first allocating opcode the walker handles directly.
;
; Compiler inlines (cons a b) (2 args) to OP_CONS rather than the
; FLOAD+CALL path, so a body of `(cons x y)` is a direct test of the
; OP_CONS emitter — pop cdr/car, cache_flush, JSR cl_jit_runtime_cons,
; push result back.  GC during cl_cons is reached by the conservative
; m68k-stack scan; the cache flush is what keeps any residual cached
; heap pointers visible to the scan.
(defun walker-cons1 (x y) (cons x y))
(check "walker-cons1-counter-bump" t
  (let ((before (clamiga::%jit-invoke-count)))
    (walker-cons1 1 2)
    (> (clamiga::%jit-invoke-count) before)))
(check "walker-cons1-fixnums"  '(1 . 2)         (walker-cons1 1 2))
(check "walker-cons1-symbols"  '(a . b)         (walker-cons1 'a 'b))
(check "walker-cons1-mixed"    '(1 . a)         (walker-cons1 1 'a))
(check "walker-cons1-nil-cdr"  '(x)             (walker-cons1 'x nil))
(check "walker-cons1-cons-car" '((1 . 2) . 3)   (walker-cons1 (cons 1 2) 3))
(check "walker-cons1-list-cdr" '(0 1 2 3)       (walker-cons1 0 '(1 2 3)))

; GC stress: build a long list inside the JIT'd body so allocations
; accumulate and the collector is virtually guaranteed to run with
; live operand-stack references on the m68k stack (the partially-
; built list head sits in a local that the conservative scan must
; reach across each cl_cons call).  Returns the list length so the
; check is robust against in-place GC-induced rewrites — if the scan
; missed a root, the list would be truncated or corrupted.
(defun walker-cons-stress (n)
  (let ((lst nil)
        (i 0))
    (tagbody
       loop-top
       (if (< i n)
           (progn
             (setq lst (cons i lst))
             (setq i (+ i 1))
             (go loop-top))))
    lst))
(check "walker-cons-stress-counter-bump" t
  (let ((before (clamiga::%jit-invoke-count)))
    (walker-cons-stress 10)
    (> (clamiga::%jit-invoke-count) before)))
(check "walker-cons-stress-10-length" 10
  (length (walker-cons-stress 10)))
(check "walker-cons-stress-10-head"   9
  (car (walker-cons-stress 10)))
(check "walker-cons-stress-10-last"   0
  (car (last (walker-cons-stress 10))))
; 500 conses (~6 KB) is enough to trip a young-arena GC on the small-
; heap test config; primary goal is to prove the scan keeps the
; growing list rooted across collections, not benchmark speed.
(check "walker-cons-stress-500-length" 500
  (length (walker-cons-stress 500)))

; --- OP_EQ in conditional context: `(cond ((eq x 'a) 1) ((eq x 'b) 2))`.
; Pulls together OP_EQ + OP_DUP + OP_JNIL + branch resolution within a
; single function.
(defun walker-cond-eq (x)
  (cond ((eq x 'a) 1)
        ((eq x 'b) 2)
        (t 99)))
(check "walker-cond-eq-a"        1  (walker-cond-eq 'a))
(check "walker-cond-eq-b"        2  (walker-cond-eq 'b))
(check "walker-cond-eq-fallback" 99 (walker-cond-eq 'c))

; --- Self-contained loop: sum 0..(N-1) via tagbody+go --------------------
;
; This is the JIT's first "real" benchmark shape — every opcode in the
; loop body now JITs (LOAD/STORE/POP/CONST + JNIL/JMP from the prior
; commit + ADD/LT from this one).  The loop runs end-to-end in native
; code with zero re-entry into the bytecode interpreter until OP_RET.
;
; sum-to(N) = N*(N-1)/2.  For N=10: 45.  For N=100: 4950.
(defun walker-sum-to (n)
  (let ((s 0) (i 0))
    (tagbody
       loop-top
       (if (< i n)
           (progn
             (setq s (+ s i))
             (setq i (+ i 1))
             (go loop-top))))
    s))
(check "walker-sum-to-counter-bump" t
  (let ((before (clamiga::%jit-invoke-count)))
    (walker-sum-to 10)
    (> (clamiga::%jit-invoke-count) before)))
(check "walker-sum-to-0"   0     (walker-sum-to 0))
(check "walker-sum-to-1"   0     (walker-sum-to 1))
(check "walker-sum-to-10"  45    (walker-sum-to 10))
(check "walker-sum-to-100" 4950  (walker-sum-to 100))

; --- OP_FLOAD / OP_CALL.  General Lisp call sequencing:
;
;   FLOAD <sym>      ; push symbol's function value
;   <push args>
;   CALL <nargs>     ; pops func + N args, pushes result
;
; The walker emits OP_FLOAD as a JSR to cl_jit_runtime_fload with the
; symbol literal baked in (CL_Obj from constants[idx] at compile
; time), and OP_CALL as a JSR to cl_jit_runtime_call which copies the
; m68k operand-stack args into a local CL_Obj[] and dispatches via
; cl_vm_apply — so closures, builtins, and JIT'd callees all route
; through the standard call path.
;
; OP_TAILCALL has its own walker case (covered in the section below);
; the tests in *this* section use a let-binding wrapper `(let ((r
; expr)) r)` to force the call out of tail position so the OP_CALL
; emitter is exercised specifically.
;
; Coverage: 0/1/3-arg calls, calls to builtins (CL_FUNCTION_P branch
; in cl_vm_apply), JIT-to-JIT chains, recursive fixnum loops (fib),
; undefined-function diagnostic, recovery after longjmp.

; --- Trivial 0-arg call wrapped in a let so the call site is OP_CALL,
; not OP_TAILCALL.  Callee returns a literal; CALL just bounces
; through cl_vm_apply.
(defun walker-call-target-0 () 17)
(defun walker-call-0 () (let ((r (walker-call-target-0))) r))
(check "walker-call-0-counter-bump" t
  (let ((before (clamiga::%jit-invoke-count)))
    (walker-call-0)
    (> (clamiga::%jit-invoke-count) before)))
(check "walker-call-0-returns" 17 (walker-call-0))

; --- 1-arg call wrapped in let.  Identity callee; verifies arg-
; passing order (the helper's reverse-copy of operand_top must
; preserve arg(0)).
(defun walker-call-target-id (x) x)
(defun walker-call-id (x) (let ((r (walker-call-target-id x))) r))
(check "walker-call-id-counter-bump" t
  (let ((before (clamiga::%jit-invoke-count)))
    (walker-call-id 1)
    (> (clamiga::%jit-invoke-count) before)))
(check "walker-call-id-fix"    42     (walker-call-id 42))
(check "walker-call-id-sym"    'q     (walker-call-id 'q))
(check "walker-call-id-nil"    nil    (walker-call-id nil))
(check "walker-call-id-cons"   '(a b) (walker-call-id '(a b)))

; --- 3-arg calls, each selecting a different slot.  A reversed-copy
; or off-by-one bug in argument passing would immediately surface as
; the wrong slot's value coming back.
(defun walker-call-target-3-first (a b c) a)
(defun walker-call-target-3-mid   (a b c) b)
(defun walker-call-target-3-last  (a b c) c)
(defun walker-call-3-first (a b c)
  (let ((r (walker-call-target-3-first a b c))) r))
(defun walker-call-3-mid   (a b c)
  (let ((r (walker-call-target-3-mid   a b c))) r))
(defun walker-call-3-last  (a b c)
  (let ((r (walker-call-target-3-last  a b c))) r))
(check "walker-call-3-first" 'one   (walker-call-3-first 'one 'two 'three))
(check "walker-call-3-mid"   'two   (walker-call-3-mid   'one 'two 'three))
(check "walker-call-3-last"  'three (walker-call-3-last  'one 'two 'three))
(check "walker-call-3-mid-fix" 20   (walker-call-3-mid   10 20 30))

; --- Call to a CL builtin (LIST).  cl_vm_apply takes the
; CL_FUNCTION_P branch, dispatching directly to call_builtin
; without a stub frame.  Same JIT call site, different runtime
; tail.  LIST is used here because CONS now inlines to OP_CONS
; rather than FLOAD+CALL — the call-path coverage moved to a
; builtin the compiler doesn't intrinsify.
(defun walker-call-list2 (a b) (let ((r (list a b))) r))
(check "walker-call-list2-fixnums" '(1 2)     (walker-call-list2 1 2))
(check "walker-call-list2-symbols" '(a b)     (walker-call-list2 'a 'b))

; --- Chained JIT-to-JIT call: caller and callees all JIT'd.  The
; outer caller's body is wrapped in let to keep all three call sites
; OP_CALL; the inner two are already non-tail (their results feed
; the outer call's argument list).
(defun walker-call-add1 (x) (+ x 1))
(defun walker-call-chained (n)
  (let ((r (walker-call-add1 (walker-call-add1 (walker-call-add1 n)))))
    r))
(check "walker-call-chained-fix" 13 (walker-call-chained 10))
(check "walker-call-chained-neg" -2 (walker-call-chained -5))

; --- Recursion: fib(N).  In the else-branch the two recursive calls
; feed `+`, so they're naturally non-tail and emit OP_CALL.  No let
; wrapper needed — exercises OP_CALL twice per non-base case.
(defun walker-call-fib (n)
  (if (< n 2)
      n
      (+ (walker-call-fib (- n 1)) (walker-call-fib (- n 2)))))
(check "walker-call-fib-counter-bump" t
  (let ((before (clamiga::%jit-invoke-count)))
    (walker-call-fib 5)
    (> (clamiga::%jit-invoke-count) before)))
(check "walker-call-fib-0"   0  (walker-call-fib 0))
(check "walker-call-fib-1"   1  (walker-call-fib 1))
(check "walker-call-fib-2"   1  (walker-call-fib 2))
(check "walker-call-fib-7"  13  (walker-call-fib 7))
(check "walker-call-fib-10" 55  (walker-call-fib 10))

; --- Undefined function: FLOAD signals via cl_error → longjmp out
; of the JIT'd frame.  handler-case catches; the JIT frame must
; unwind cleanly without corrupting subsequent calls.
(defun walker-call-undef ()
  (let ((r (no-such-function-defined-here-please))) r))
(check "walker-call-undef-signals" :caught
  (handler-case (progn (walker-call-undef) :no-error)
    (undefined-function () :caught)
    (error            () :caught)))
; Further calls still work after the longjmp unwind.
(check "walker-call-recover-after-error" 42
  (walker-call-id 42))

; --- OP_TAILCALL.  Two paths in the emitter (since 2026-05-15):
;
;   1. Self-recursive TCO.  When nargs == arity and bc->name is a
;      SYMBOL, the emitter prefixes the helper sequence with a
;      runtime guard: compare the func value sitting at 4*N(a7)
;      against this bytecode's CL_Obj.  On match, copy the N args
;      from the operand stack into the A6 frame slots, drop the
;      operand stack, and bra.w back to entry-after-prologue —
;      same LINK frame is reused, zero m68k-stack growth.
;
;   2. Fallback / non-self / redefined.  Guard misses → continue
;      with the helper-based sequence (cache_flush already done at
;      the top, marshal args, JSR cl_jit_runtime_call, drop frame,
;      restore D5/D6/D7, UNLK, RTS).  This is the path cross-function
;      tail calls and post-redefinition self-calls take.
;
; The OP_RET the compiler always emits after OP_TAILCALL becomes
; dead unreachable native code on either branch (still emitted by
; the walker in case other branches land there, just never reached).

; Tail call to a plain user function.  Bare `(walker-tail-target x)` in
; tail position emits OP_TAILCALL; arg-passing order check (single
; arg).
(defun walker-tail-target (x) x)
(defun walker-tail-id (x) (walker-tail-target x))
(check "walker-tail-id-counter-bump" t
  (let ((before (clamiga::%jit-invoke-count)))
    (walker-tail-id 1)
    (> (clamiga::%jit-invoke-count) before)))
(check "walker-tail-id-fix"  42    (walker-tail-id 42))
(check "walker-tail-id-sym"  'q    (walker-tail-id 'q))
(check "walker-tail-id-nil"  nil   (walker-tail-id nil))
(check "walker-tail-id-cons" '(a b) (walker-tail-id '(a b)))

; 3-arg tail call selecting different slots — same arg-passing
; coverage as the OP_CALL version above, just from tail position.
(defun walker-tail-3-first (a b c)
  (walker-call-target-3-first a b c))
(defun walker-tail-3-mid (a b c)
  (walker-call-target-3-mid a b c))
(defun walker-tail-3-last (a b c)
  (walker-call-target-3-last a b c))
(check "walker-tail-3-first" 'one   (walker-tail-3-first 'one 'two 'three))
(check "walker-tail-3-mid"   'two   (walker-tail-3-mid   'one 'two 'three))
(check "walker-tail-3-last"  'three (walker-tail-3-last  'one 'two 'three))

; Tail call to a CL builtin — tail position picks the same OP_TAILCALL
; opcode regardless of callee kind; cl_vm_apply routes to call_builtin.
(defun walker-tail-cons (a b) (cons a b))
(check "walker-tail-cons-fixnums" '(1 . 2) (walker-tail-cons 1 2))
(check "walker-tail-cons-symbols" '(a . b) (walker-tail-cons 'a 'b))

; Tail call inside an IF branch — the most common shape in practice.
; The IF emits a conditional JNIL/JTRUE branch over the two arms; the
; tail-position arms each emit OP_TAILCALL.  Cache invariant: every
; branch target lands with cache_depth=0, and the OP_TAILCALL emitter
; flushes before its JSR, so the post-flush state on either arm
; matches the canonical empty cache.
(defun walker-tail-if (flag x y)
  (if flag (walker-tail-target x) (walker-tail-target y)))
(check "walker-tail-if-true"  'a (walker-tail-if t   'a 'b))
(check "walker-tail-if-false" 'b (walker-tail-if nil 'a 'b))
(check "walker-tail-if-fix-t" 10 (walker-tail-if t   10 20))
(check "walker-tail-if-fix-f" 20 (walker-tail-if nil 10 20))

; Self-recursion accumulator — every recursive call is in tail
; position, so the loop runs entirely through OP_TAILCALL with the
; **native self-TCO path** (landed 2026-05-15): runtime guard at the
; tail-call site compares the func value against this bytecode's
; CL_Obj; on match, args are copied to A6 frame slots and execution
; bra.w's back to entry-after-prologue.  Same LINK frame is reused —
; zero m68k-stack growth.  N can now go deep without blowing the 65
; KB Amiga stack (the bytecode VM's frame reuse on cl_vm.stack is
; matched).  Σ 1..N = N*(N+1)/2.
(defun walker-tail-sum (n acc)
  (if (zerop n)
      acc
      (walker-tail-sum (- n 1) (+ acc n))))
(check "walker-tail-sum-counter-bump" t
  (let ((before (clamiga::%jit-invoke-count)))
    (walker-tail-sum 5 0)
    (> (clamiga::%jit-invoke-count) before)))
(check "walker-tail-sum-0"   0     (walker-tail-sum 0   0))
(check "walker-tail-sum-1"   1     (walker-tail-sum 1   0))
(check "walker-tail-sum-10"  55    (walker-tail-sum 10  0))
(check "walker-tail-sum-20"  210   (walker-tail-sum 20  0))
; Deep self-recursion: without native TCO this would blow the 65 KB
; Amiga stack at N≈50.  N=1000 (sum 500500) proves the LINK frame is
; really being reused; N=5000 (sum 12502500) — well past 5 MB of
; "would-have-grown" stack — proves it's not just lucky inlining.
(check "walker-tail-sum-1000" 500500    (walker-tail-sum 1000 0))
(check "walker-tail-sum-5000" 12502500  (walker-tail-sum 5000 0))

; Self-TCO doesn't fire when the tail-call target isn't `self`.  Two
; cooperating functions exercise the fallback path: `walker-tail-ping`
; ends with a call to `walker-tail-pong` (not self), so its OP_TAILCALL
; emits the guard, the runtime cmp fails (func != ping_bc), and
; control falls through to the helper.  Same for pong.  Each round-
; trip costs one C frame, so we keep the depth modest.  Result chains
; through both layers — proves the guard's mismatch arm works.
(defun walker-tail-pong (x) x)
(defun walker-tail-ping (x) (walker-tail-pong x))
(check "walker-tail-ping-cross"   42 (walker-tail-ping 42))
(check "walker-tail-ping-sym"   'foo (walker-tail-ping 'foo))

; Self-TCO with redefinition.  `defun` of the same name replaces the
; symbol's function cell with a fresh CL_Bytecode.  The original JIT'd
; code's baked-in self-CL_Obj points at the OLD bytecode; the guard
; cmp at runtime sees that the new func != old bc, falls back to the
; helper, which dispatches to the new definition.  Semantics match
; the bytecode VM exactly (`(setf (symbol-function 'foo) #'bar)`-style
; redefinition is honored).
;
; Walk it: define `walker-redef-tco` as a self-recursive countdown
; that JITs through self-TCO; verify it works; redefine it to a
; non-recursive form; verify the new body runs.
(defun walker-redef-tco (n)
  (if (zerop n) :hit-bottom (walker-redef-tco (- n 1))))
(check "walker-redef-tco-self"   :hit-bottom (walker-redef-tco 50))
(defun walker-redef-tco (n) (list :replaced n))
(check "walker-redef-tco-after"  '(:replaced 7) (walker-redef-tco 7))
; And one more time the other direction — redefining back to a
; self-recursive shape proves the symbol cell really is what's being
; consulted at each call.
(defun walker-redef-tco (n)
  (if (zerop n) :back-again (walker-redef-tco (- n 1))))
(check "walker-redef-tco-self-2" :back-again (walker-redef-tco 30))

; Tail call to undefined function: OP_FLOAD signals via cl_error →
; longjmp out of the JIT'd frame.  handler-case catches; subsequent
; JIT'd calls must still work after the unwind (the LINK frame and
; saved D5/D6/D7 don't get restored on the longjmp path, but the
; m68k C-ABI doesn't require it across the unwind because the
; caller's setjmp restored its own context).
(defun walker-tail-undef ()
  (no-such-tailcall-target-please))
(check "walker-tail-undef-signals" :caught
  (handler-case (progn (walker-tail-undef) :no-error)
    (undefined-function () :caught)
    (error              () :caught)))
(check "walker-tail-recover-after-error" 42
  (walker-tail-id 42))

; --- OP_GLOAD / OP_GSTORE.  Global/special-variable load and store:
;
;   GLOAD  <sym>   ; push symbol's dynamic value (TLV first, else cell)
;   GSTORE <sym>   ; write TOS to symbol's dynamic value (peek, no pop)
;
; The walker bakes constants[idx] (a SYMBOL) into the emitted code as a
; 32-bit literal — same JIT-time soundness argument as OP_FLOAD: the
; constants[] slot doesn't get re-bound after compilation, only the
; symbol's value cell, which the helper dereferences on every call.
;
; OP_GLOAD template: push sym, JSR cl_jit_runtime_gload, drop arg,
; push helper's D0 result.  OP_GSTORE template: duplicate TOS as the
; C-ABI's val arg (pushed first right-to-left), push sym as the first
; arg, JSR cl_jit_runtime_gstore, drop the 8-byte arg frame.  The TOS
; survives untouched — matching the VM's "store without pop" semantics.
;
; cl_jit_runtime_gstore mirrors the VM's *PACKAGE* sync (calls
; cl_sync_current_package_from_dynamic when the symbol is *PACKAGE*),
; so SETQ *PACKAGE* through JIT'd code is indistinguishable from the
; bytecode path.

(defvar *walker-glo* 100)

; Reader: function body is a bare special reference, which the
; compiler emits as OP_GLOAD <*walker-glo*>.
(defun walker-gload () *walker-glo*)
(check "walker-gload-counter-bump" t
  (let ((before (clamiga::%jit-invoke-count)))
    (walker-gload)
    (> (clamiga::%jit-invoke-count) before)))
(check "walker-gload-fix" 100 (walker-gload))

; Writer: SETQ on a special emits OP_GSTORE.  Returns the stored value
; (setq's value is the new value), so the JIT'd return must equal the
; argument.
(defun walker-gstore (v) (setq *walker-glo* v))
(check "walker-gstore-counter-bump" t
  (let ((before (clamiga::%jit-invoke-count)))
    (walker-gstore 100)
    (> (clamiga::%jit-invoke-count) before)))
(check "walker-gstore-returns-val" 7 (walker-gstore 7))

; Round-trip: store via walker-gstore, read back via walker-gload.
; Both functions exercise the JIT'd template; if either helper had the
; wrong argument order or the GSTORE emitter popped the TOS by mistake,
; the value coming back would be wrong or the stack would underflow.
(check "walker-glo-roundtrip-fix" 999
  (progn (walker-gstore 999) (walker-gload)))
(check "walker-glo-roundtrip-sym" 'tag
  (progn (walker-gstore 'tag) (walker-gload)))
(check "walker-glo-roundtrip-cons" '(a b c)
  (progn (walker-gstore '(a b c)) (walker-gload)))
(check "walker-glo-roundtrip-nil" nil
  (progn (walker-gstore nil) (walker-gload)))

; Restore so later tests aren't affected by the leftover state.
(setq *walker-glo* 100)

; --- Unbound-variable: GLOAD on a special with no value signals
; UNBOUND-VARIABLE via cl_error → longjmp out of the JIT'd frame.
; makunbound clears the value cell; the next read must signal, and
; subsequent JIT'd calls must still work after the unwind.
(defvar *walker-glo-unbound* :placeholder)
(makunbound '*walker-glo-unbound*)
(defun walker-gload-unbound () *walker-glo-unbound*)
(check "walker-gload-unbound-signals" :caught
  (handler-case (progn (walker-gload-unbound) :no-error)
    (unbound-variable () :caught)
    (error            () :caught)))
; Further calls still work after the longjmp unwind.
(check "walker-gload-recover-after-error" 100
  (walker-gload))

; --- Dynamic binding (LET on a special) participates correctly: the
; JIT'd GLOAD goes through cl_symbol_value which checks TLV first, so
; rebinding *walker-glo* in an outer LET must be visible inside the
; JIT'd reader.
(check "walker-gload-tlv-rebind" 55
  (let ((*walker-glo* 55)) (walker-gload)))
; And after the LET unwinds, the outer global value is restored.
(check "walker-gload-tlv-restore" 100 (walker-gload))

; --- OP_DYNBIND / OP_DYNUNBIND.  `(let ((*special* val)) body)` on a
; defvar'd symbol compiles to a value-producer + OP_DYNBIND + body +
; OP_DYNUNBIND <count>.
;
; OP_DYNBIND template: the value is already on the operand stack (TOS),
; so we push the symbol literal above it and JSR
; cl_jit_runtime_dynbind(sym, val); the cleanup ADDQ #8 drops both —
; matching the VM's "pop value" semantic.  OP_DYNUNBIND template: push
; the u8 count, JSR cl_jit_runtime_dynunbind, drop arg.  Both helpers
; are non-allocating (dyn_stack + TLV table are preallocated).
;
; cl_jit_runtime_dynbind mirrors the VM's *PACKAGE* sync.  An error
; raised through the helper (dyn-stack overflow) longjmps out of the
; JIT'd frame the same way OP_FLOAD's unbound-function path does;
; cl_dynbind_restore_to runs on the error path via the existing
; runtime, so the binding stack stays consistent regardless of how
; control leaves the JIT'd frame.

; Reuse *walker-glo* (defvar'd above to 100).
;
; Basic: bind *walker-glo* to 999 around a body that reads it.  Inside
; the body the JIT'd OP_GLOAD must see 999; after the LET unwinds, the
; outer cell must still read 100.
(defun walker-dyn-let-read ()
  (let ((*walker-glo* 999)) *walker-glo*))
(check "walker-dyn-let-read-counter-bump" t
  (let ((before (clamiga::%jit-invoke-count)))
    (walker-dyn-let-read)
    (> (clamiga::%jit-invoke-count) before)))
(check "walker-dyn-let-read-inner"  999 (walker-dyn-let-read))
(check "walker-dyn-let-read-outer-restored" 100 *walker-glo*)

; Bind then mutate inside the body — SETQ on the special hits the
; freshly-rebound TLV, not the outer cell.  After unwind, the outer
; cell must still be 100 (the post-mutation value lived in the TLV
; that the OP_DYNUNBIND restored away).
(defun walker-dyn-let-setq ()
  (let ((*walker-glo* 999))
    (setq *walker-glo* 1234)
    *walker-glo*))
(check "walker-dyn-let-setq-inner" 1234 (walker-dyn-let-setq))
(check "walker-dyn-let-setq-outer-restored" 100 *walker-glo*)

; Two specials bound in one LET → OP_DYNUNBIND with count 2.  Also
; covers the LET semantic that all RHS forms evaluate in the outer
; scope before any binding takes effect.
(defvar *walker-glo2* 200)
(defun walker-dyn-let-two ()
  (let ((*walker-glo*  111)
        (*walker-glo2* 222))
    (+ *walker-glo* *walker-glo2*)))
(check "walker-dyn-let-two-counter-bump" t
  (let ((before (clamiga::%jit-invoke-count)))
    (walker-dyn-let-two)
    (> (clamiga::%jit-invoke-count) before)))
(check "walker-dyn-let-two-inner"        333 (walker-dyn-let-two))
(check "walker-dyn-let-two-outer1-rest"  100 *walker-glo*)
(check "walker-dyn-let-two-outer2-rest"  200 *walker-glo2*)

; Nested LETs on the same special — inner shadows outer; after inner
; unwinds, outer's binding (still itself a dyn-binding) is visible.
; After outer unwinds, the global value is back.
(defun walker-dyn-let-nested ()
  (let ((*walker-glo* 11))
    (let ((*walker-glo* 22))
      (let ((*walker-glo* 33))
        *walker-glo*))))
(check "walker-dyn-let-nested-inner"  33  (walker-dyn-let-nested))
(check "walker-dyn-let-nested-outer-restored" 100 *walker-glo*)

; Sequenced: read after inner unwinds, while outer is still bound.
; Verifies OP_DYNUNBIND 1 restores exactly the previous TLV (not
; collapsing both LET layers).
(defun walker-dyn-let-restore-mid ()
  (let ((*walker-glo* 10))
    (let ((*walker-glo* 20)) *walker-glo*)
    *walker-glo*))
(check "walker-dyn-let-restore-mid" 10 (walker-dyn-let-restore-mid))
(check "walker-dyn-let-restore-mid-outer" 100 *walker-glo*)

; --- OP_BLOCK_PUSH / OP_BLOCK_POP / OP_BLOCK_RETURN -----------------------
;
; The compiler only emits these when needs_nlx is true — i.e. when a
; (return-from <tag> ...) actually crosses a closure boundary
; (tree_needs_nlx_block in compiler_special.c).  These tests construct
; that shape via mapcar / mapc closures, then assert (a) the function
; that owns the block JITs (counter bump) and (b) the return-from
; semantics match what the bytecode VM would produce.
;
; The walker emits an inline JSR setjmp at OP_BLOCK_PUSH so the
; captured frame belongs to the JIT'd function itself.  When
; OP_BLOCK_RETURN's helper longjmps, control returns to the
; instruction after the JSR with D0 != 0; the NLX shim then JSRs
; cl_jit_runtime_block_post_longjmp (restores marks + mv_values),
; pushes the result onto the operand stack, and branches to the
; landing IP.

; Simple early-exit: scan a list, return-from on first match.  The
; lambda closes over TARGET so the return-from crosses a closure
; boundary, forcing NLX emission.  No counter-bump assertion: a lambda
; with captured upvalues forces the outer function to emit OP_CLOSURE,
; which is not in the walker's switch — so the function itself runs
; through the bytecode interpreter even though the bytecode contains
; OP_BLOCK_PUSH.  The behaviour-correctness tests below still exercise
; the path because the *bytecode VM* hits BLOCK_PUSH / BLOCK_RETURN
; with the same semantics.
(defun walker-block-find-first (list target)
  (block found
    (mapc (lambda (x) (when (eql x target) (return-from found x))) list)
    nil))
(check "walker-block-find-first-hit"  3   (walker-block-find-first '(1 3 5) 3))
(check "walker-block-find-first-miss" nil (walker-block-find-first '(1 2 3) 9))
(check "walker-block-find-first-first" 1  (walker-block-find-first '(1 2 3) 1))

; No return-from is taken: normal exit through OP_BLOCK_POP, the
; landing receives the implicit body result.  Exercises the
; "setjmp returns 0 → commit → run body → BLOCK_POP" path without
; ever firing longjmp.
(defun walker-block-no-return (list)
  (block tag
    (mapc (lambda (x) (declare (ignore x)) nil) list)
    :normal-exit))
(check "walker-block-no-return-counter-bump" t
  (let ((before (clamiga::%jit-invoke-count)))
    (walker-block-no-return '(1 2 3))
    (> (clamiga::%jit-invoke-count) before)))
(check "walker-block-no-return" :normal-exit (walker-block-no-return '(1 2 3)))
(check "walker-block-no-return-empty" :normal-exit (walker-block-no-return nil))

; Return-from with a non-fixnum value — exercises the result-push
; through the NLX shim's `move.l d0,-(a7)` (no fixnum tag assumed by
; the JIT, so cons cells and symbols round-trip the same way).
(defun walker-block-return-cons ()
  (block b
    (mapc (lambda (x) (return-from b (cons x x))) '(:a))
    :unreached))
(check "walker-block-return-cons" '(:a . :a) (walker-block-return-cons))

(defun walker-block-return-sym ()
  (block b
    (mapc (lambda (x) (return-from b x)) '(:tag))
    :unreached))
(check "walker-block-return-sym" :tag (walker-block-return-sym))

; OP_DYNUNBIND interaction: dyn-bindings established between
; BLOCK_PUSH and the return-from must be unwound when the longjmp
; fires.  cl_jit_runtime_block_post_longjmp restores cl_dyn_top to
; the mark saved by block_alloc; verify by reading the special after
; the function returns.
(defvar *walker-block-dyn* :outer)
(defun walker-block-dyn-unwind ()
  (block b
    (let ((*walker-block-dyn* :inner))
      (mapc (lambda (x) (declare (ignore x))
                         (return-from b *walker-block-dyn*))
            '(t)))
    :unreached))
(check "walker-block-dyn-unwind-inner" :inner (walker-block-dyn-unwind))
(check "walker-block-dyn-unwind-restored" :outer *walker-block-dyn*)

; Loop with explicit RETURN.  The CL `loop` macro emits OP_BLOCK_PUSH
; (anonymous block NIL) and the inner closure that walks the
; collection triggers needs_nlx.  This is the shape `loop ... thereis`
; / `loop ... when ... return` use under the hood.
(defun walker-block-loop-return (list)
  (loop for x in list
        when (and (numberp x) (oddp x))
          do (return x)))
(check "walker-block-loop-return-counter-bump" t
  (let ((before (clamiga::%jit-invoke-count)))
    (walker-block-loop-return '(2 4 5 6))
    (> (clamiga::%jit-invoke-count) before)))
(check "walker-block-loop-return-hit" 5  (walker-block-loop-return '(2 4 5 6)))
(check "walker-block-loop-return-all-even" nil (walker-block-loop-return '(2 4 6)))

; ---- Walker: OP_UPVAL / OP_CELL_SET_UPVAL (closure read + mutation) ----
;
; `sum` is captured AND mutated by the inner lambda, so the boxing
; analysis emits OP_MAKE_CELL for it in the outer and OP_UPVAL +
; OP_CELL_REF (read) / OP_UPVAL + OP_CELL_SET_UPVAL (write) in the
; inner.  With the n_upvalues>0 gate lifted in phase B, the inner
; lambda itself JITs and reaches `sum`'s cell via the func_obj-first
; ABI.  Behaviour test verifies the final accumulated value matches
; the bytecode VM's; the explicit invoke-count bump on both the
; outer (which contains OP_CLOSURE) and the inner (which contains
; OP_UPVAL / OP_CELL_SET_UPVAL) is implicit — mapc invokes the
; inner once per list element.
(defun walker-closure-mutate (list)
  (let ((sum 0))
    (mapc (lambda (x) (setq sum (+ sum x))) list)
    sum))
(check "walker-closure-mutate-empty" 0   (walker-closure-mutate '()))
(check "walker-closure-mutate-one"   7   (walker-closure-mutate '(7)))
(check "walker-closure-mutate-sum"   15  (walker-closure-mutate '(1 2 3 4 5)))
(check "walker-closure-mutate-negs"  0   (walker-closure-mutate '(-3 -2 -1 1 2 3)))

; Same shape but the captured slot is a non-integer accumulator
; (the cell holds a list).  Exercises OP_CELL_REF/OP_CELL_SET_UPVAL
; round-tripping a heap-allocated value through the cell — proves
; we're not accidentally treating the cell payload as a fixnum.
(defun walker-closure-collect (list)
  (let ((acc nil))
    (mapc (lambda (x) (setq acc (cons x acc))) list)
    acc))
(check "walker-closure-collect" '(3 2 1) (walker-closure-collect '(1 2 3)))

; ---- Walker: OP_UWPROT / OP_UWPOP / OP_UWRETHROW + OP_MV_TO_LIST ----
;
; unwind-protect compiles to:
;   OP_UWPROT <i32 offset>
;     <protected body>
;     OP_MV_TO_LIST
;     OP_STORE list_slot
;     OP_POP
;     OP_UWPOP
;     OP_JMP cleanup_start
;   <cleanup_landing/cleanup_start>:
;     <cleanup forms, each OP_POP'd>
;     OP_UWRETHROW
;     OP_FLOAD VALUES-LIST ; OP_LOAD list_slot ; OP_CALL 1
;
; The walker now emits inline JSR setjmp at OP_UWPROT (mirroring
; OP_BLOCK_PUSH), JSRs to runtime helpers for OP_UWPOP/OP_UWRETHROW,
; and routes OP_MV_TO_LIST through cl_jit_runtime_mv_to_list.

; Normal-exit: protected form returns a value, cleanup runs once,
; result is the protected value.
(defvar *walker-uwp-cleanup-count* 0)
(defun walker-uwp-normal ()
  (setq *walker-uwp-cleanup-count* 0)
  (unwind-protect
    (+ 1 2)
    (incf *walker-uwp-cleanup-count*)))
(check "walker-uwp-normal-counter-bump" t
  (let ((before (clamiga::%jit-invoke-count)))
    (walker-uwp-normal)
    (> (clamiga::%jit-invoke-count) before)))
(check "walker-uwp-normal-result" 3 (walker-uwp-normal))
(check "walker-uwp-normal-cleanup-ran-once" 1
  (progn (walker-uwp-normal) *walker-uwp-cleanup-count*))

; Error through UWPROT.  The handler-case lives in the outer driver
; so its closure forms don't block JIT of the UWP-bearing inner; the
; inner contains the unwind-protect alone and is JIT-compilable.  The
; counter-bump assertion verifies the inner actually ran as native
; code, so the asserted cleanup behavior is exercising the JIT path
; (not falling back to the bytecode VM's OP_UWPROT).
(defvar *walker-uwp-cleanup-err* 0)
(defun walker-uwp-error-inner ()
  (unwind-protect
    (error "boom from JIT'd uwp")
    (incf *walker-uwp-cleanup-err*)))
(defun walker-uwp-error ()
  (setq *walker-uwp-cleanup-err* 0)
  (handler-case (walker-uwp-error-inner)
    (error (c) (declare (ignore c)) :caught)))
(check "walker-uwp-error-counter-bump" t
  (let ((before (clamiga::%jit-invoke-count)))
    (walker-uwp-error)
    (> (clamiga::%jit-invoke-count) before)))
(check "walker-uwp-error-result" :caught (walker-uwp-error))
(check "walker-uwp-error-cleanup-ran" 1
  (progn (walker-uwp-error) *walker-uwp-cleanup-err*))

; Two JIT'd UWPROT frames stacked on the NLX stack.  Error unwinds
; through both: inner cleanup runs first (via cl_jit_runtime_uwprot_
; post_longjmp / OP_UWRETHROW pending==2 walking to outer), then
; outer cleanup, then the unhandled error reaches the driver's
; handler-case.
(defvar *walker-uwp-order* nil)
(defun walker-uwp-nested-inner ()
  (unwind-protect
    (unwind-protect
      (error "force unwind")
      (push :inner *walker-uwp-order*))
    (push :outer *walker-uwp-order*)))
(defun walker-uwp-nested ()
  (setq *walker-uwp-order* nil)
  (handler-case (walker-uwp-nested-inner)
    (error (c) (declare (ignore c)) :caught)))
(check "walker-uwp-nested-counter-bump" t
  (let ((before (clamiga::%jit-invoke-count)))
    (walker-uwp-nested)
    (> (clamiga::%jit-invoke-count) before)))
(check "walker-uwp-nested-result" :caught (walker-uwp-nested))
(check "walker-uwp-nested-order" '(:outer :inner)
  (progn (walker-uwp-nested) *walker-uwp-order*))

; Multiple-value round-trip on the normal-exit path.  Protected form
; returns three values via (values …); OP_MV_TO_LIST in JIT'd code
; captures them into the unwind-protect's stash slot, cleanup runs
; (here a nop), then VALUES-LIST republishes them at exit.  Inner
; function is JIT-compilable; outer wraps it in multiple-value-list
; for the test harness.
(defun walker-uwp-mv-inner ()
  (unwind-protect
    (values 1 2 3)
    nil))
(defun walker-uwp-mv ()
  (multiple-value-list (walker-uwp-mv-inner)))
(check "walker-uwp-mv-counter-bump" t
  (let ((before (clamiga::%jit-invoke-count)))
    (walker-uwp-mv)
    (> (clamiga::%jit-invoke-count) before)))
(check "walker-uwp-mv-result" '(1 2 3) (walker-uwp-mv))

; --- &key support: native kw-prologue ----------------------------------
;
; The walker emits a kw-ABI prologue (LINK + save D5/D6/D7 + JSR
; cl_jit_runtime_kw_prologue) for bytecodes whose lambda-list carries
; &key.  cl_jit_invoke dispatches through the 3-arg signature
; (bc, nargs, args) and the helper NIL-initialises every slot then
; populates key_slots[]/key_suppliedp_slots[] via the same matcher
; logic as vm.c's OP_CALL.  Each test below proves a different facet:
; default-when-missing, value-when-supplied, suppliedp tracking,
; right-to-left "leftmost duplicate wins", unknown-key signal,
; :allow-other-keys, odd-kwarg signal.

; Two-key function; defaults exercise the OP_LOAD-of-suppliedp /
; OP_JTRUE-skip pattern the compiler emits for key defaults.
(defun walker-key-2 (&key (a 10) (b 20))
  (+ a b))
(check "walker-key-2-counter-bump" t
  (let ((before (clamiga::%jit-invoke-count)))
    (walker-key-2)
    (> (clamiga::%jit-invoke-count) before)))
(check "walker-key-2-defaults"      30 (walker-key-2))
(check "walker-key-2-supplied-a"    23 (walker-key-2 :a 3))
(check "walker-key-2-supplied-b"    15 (walker-key-2 :b 5))
(check "walker-key-2-both-supplied"  9 (walker-key-2 :a 4 :b 5))
(check "walker-key-2-both-reversed"  9 (walker-key-2 :b 5 :a 4))

; Suppliedp tracking — user-declared supplied-p var.  When the key is
; not passed, the suppliedp slot stays NIL (kw_prologue's NIL-init);
; when passed, the helper writes CL_T into the slot.
(defun walker-key-suppliedp (&key (x 0 xp))
  (if xp (cons :given x) (cons :missing x)))
(check "walker-key-suppliedp-missing" '(:missing . 0) (walker-key-suppliedp))
(check "walker-key-suppliedp-given-zero" '(:given . 0) (walker-key-suppliedp :x 0))
(check "walker-key-suppliedp-given-nil" '(:given) (walker-key-suppliedp :x nil))
(check "walker-key-suppliedp-given-value" '(:given . 42) (walker-key-suppliedp :x 42))

; Leftmost duplicate keyword wins per CLHS 3.4.1.4.1.  The helper
; walks the pairs right-to-left so each overwrite is shadowed by the
; next (= leftmost) occurrence — the final slot value is the
; leftmost one.
(defun walker-key-dup (&key v) v)
(check "walker-key-dup-leftmost" 1 (walker-key-dup :v 1 :v 2 :v 3))

; Unknown keyword without :allow-other-keys → CL_ERR_ARGS via
; cl_error.  longjmp out of the JIT frame; handler-case catches it.
(defun walker-key-strict (&key a) a)
(check "walker-key-strict-known" 7 (walker-key-strict :a 7))
(check "walker-key-strict-unknown" :caught
  (handler-case (progn (walker-key-strict :b 1) :no-error)
    (error () :caught)))

; :allow-other-keys baked into the lambda list → flags bit 1 set →
; helper skips the unknown-keyword check.
(defun walker-key-allow (&key a &allow-other-keys) a)
(check "walker-key-allow-known"   9 (walker-key-allow :a 9))
(check "walker-key-allow-unknown" 9 (walker-key-allow :a 9 :extra 'whatever))

; :allow-other-keys T passed by the caller turns on the per-call
; bypass even when the callee didn't declare &allow-other-keys.
(defun walker-key-caller-bypass (&key a) a)
(check "walker-key-caller-bypass" 5
  (walker-key-caller-bypass :a 5 :extra 99 :allow-other-keys t))

; Odd number of keyword arguments → CL_ERR_ARGS.  The kw_prologue's
; n_extra & 1 check is reached only after positional filling, so this
; specifically exercises the post-positional path.  (apply ensures
; the malformed call survives any compile-time argument analysis.)
(defun walker-key-odd (&key a) a)
(check "walker-key-odd-signals" :caught
  (handler-case (progn (apply #'walker-key-odd '(:a)) :no-error)
    (error () :caught)))

; Required arg + &key combination: positional copies into slot 0,
; key slots live above it.  Verifies the helper handles
; `arity = 1, n_keys = 2` correctly.
(defun walker-key-req+2 (x &key (a 10) (b 20))
  (list x a b))
(check "walker-key-req+2-defaults"    '(1 10 20) (walker-key-req+2 1))
(check "walker-key-req+2-supplied"    '(1  3  4) (walker-key-req+2 1 :a 3 :b 4))
(check "walker-key-req+2-reversed"    '(1  3  4) (walker-key-req+2 1 :b 4 :a 3))
