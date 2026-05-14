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
