#!/bin/sh
# GC-stress regression tests for unprotected-CL_Obj / stale-arena-pointer bugs.
#
# Each case reproduces a compaction-corruption bug that only fires when a
# moving GC runs *during* an allocating call.  Run against a binary built with
# -DDEBUG_GC_STRESS and with CLAMIGA_GC_STRESS=1, which forces a compacting GC
# before every allocation so the bug is deterministic (see CLAUDE.md "GC
# Safety" and the [GC-stress debug flag] memory note).
#
#   make test-gc-stress            # builds the stress binary and runs this
#   sh tests/test_gc_stress_regression.sh build/host-gcstress/clamiga
#
# The binary MUST be a DEBUG_GC_STRESS build — the env var is a no-op otherwise.

CLAMIGA="${1:-build/host-gcstress/clamiga}"
passed=0
failed=0
total=0
TMPDIR="${TMPDIR:-/tmp}"
WORK="$TMPDIR/clamiga_gcstress_$$"
mkdir -p "$WORK"
trap 'rm -rf "$WORK"' EXIT

if [ ! -x "$CLAMIGA" ]; then
    echo "  SKIP  GC-stress regression: no stress binary at $CLAMIGA"
    echo "0 passed, 0 failed, 0 total"
    exit 0
fi

# Compile a source file to a FASL with a CLEAN (non-stress) run, so each case
# exercises the *load* path under stress against a known-good FASL.  The env
# var must be fully UNSET (the runtime checks presence, not value — an empty
# CLAMIGA_GC_STRESS= still enables stress and would corrupt the FASL itself).
compile_fasl() {
    src="$1"; fasl="$2"
    env -u CLAMIGA_GC_STRESS timeout 60 "$CLAMIGA" --no-userinit --heap 64M \
        --non-interactive \
        --eval "(progn (compile-file \"$src\" :output-file \"$fasl\") (format t \"COMPILED~%\"))" \
        2>/dev/null | grep -q COMPILED
}

# Run under GC stress, capture stdout+stderr.
run_stress() {
    CLAMIGA_GC_STRESS=1 timeout 90 "$CLAMIGA" --no-userinit --heap 48M \
        --non-interactive --load "$1" 2>&1
}

check_contains() {
    desc="$1"; pattern="$2"; actual="$3"
    total=$((total + 1))
    if echo "$actual" | grep -q "$pattern"; then
        echo "  ok  $desc"
        passed=$((passed + 1))
    else
        echo "  FAIL  $desc (expected /$pattern/)"
        echo "$actual" | tail -4 | sed 's/^/      /'
        failed=$((failed + 1))
    fi
}

check_absent() {
    desc="$1"; pattern="$2"; actual="$3"
    total=$((total + 1))
    if echo "$actual" | grep -q "$pattern"; then
        echo "  FAIL  $desc (saw /$pattern/)"
        echo "$actual" | grep "$pattern" | head -3 | sed 's/^/      /'
        failed=$((failed + 1))
    else
        echo "  ok  $desc"
        passed=$((passed + 1))
    fi
}

# --- Case 1: COMPILE-FILE input pathname survives compaction ---------------
# Bug: cl_parse_namestring held a raw pointer into the arena string while
# allocating; compaction relocated it, truncating "/tmp/x.lisp" to "/".
cat > "$WORK/cf.lisp" <<'EOF'
(defun cf-fn () 42)
EOF
cat > "$WORK/cf-driver.lisp" <<EOF
(compile-file "$WORK/cf.lisp" :output-file "$WORK/cf.fasl")
(format t "CF-DONE~%")
EOF
out=$(run_stress "$WORK/cf-driver.lisp")
check_contains "compile-file resolves multi-component input path" "CF-DONE" "$out"
check_absent   "compile-file path not truncated to \"/\""           "cannot open file \"/\"" "$out"

# --- Case 2: PARSE-NAMESTRING / MERGE-PATHNAMES under stress ---------------
cat > "$WORK/pn.lisp" <<'EOF'
(format t "PN:~s~%" (namestring (parse-namestring "/tmp/sub/file.lisp")))
EOF
out=$(run_stress "$WORK/pn.lisp")
check_contains "parse-namestring keeps directory + name + type" 'PN:"/tmp/sub/file.lisp"' "$out"

# --- Case 3: defun loaded from FASL — OP_CLOSURE bytecode template --------
# Bug: OP_CLOSURE cached the bytecode-template CL_Obj across cl_alloc; a
# compaction relocated the template, so the closure's bytecode field went
# stale ("Corrupted closure: bytecode field is type 0") when later called.
cat > "$WORK/clo.lisp" <<'EOF'
(defpackage :gcstress-clo (:use :cl) (:export #:greet))
(in-package :gcstress-clo)
(defun greet (x) (format nil "Hi ~a" x))
EOF
if compile_fasl "$WORK/clo.lisp" "$WORK/clo.fasl"; then
    cat > "$WORK/clo-load.lisp" <<EOF
(load "$WORK/clo.fasl")
(format t "CLO:~a~%" (gcstress-clo:greet "world"))
EOF
    out=$(run_stress "$WORK/clo-load.lisp")
    check_contains "FASL-loaded defun is callable"             "CLO:Hi world" "$out"
    check_absent   "no corrupted-closure error"                "Corrupted closure" "$out"
else
    echo "  SKIP  defun FASL compile failed"
fi

# --- Case 4: FASL with gensyms + shared structure -------------------------
# Bug: the FASL reader's gensym_objs[]/shared_objs[] dedup tables were not
# GC-rooted, so a forward GENSYM_REF/OBJ_REF resolved to a relocated object's
# stale offset ("Undefined function: G<n>" / corrupted forms) on load.
cat > "$WORK/gs.lisp" <<'EOF'
(in-package :cl-user)
(defmacro gcstress-swap (a b)
  (let ((tmp (gensym "TMP")))
    `(let ((,tmp ,a)) (setf ,a ,b) (setf ,b ,tmp))))
(defun gcstress-use-swap (x y)
  (let ((p x) (q y)) (gcstress-swap p q) (list p q)))
(defvar *gcstress-shared* '(common . common))
(defun gcstress-shared () *gcstress-shared*)
EOF
if compile_fasl "$WORK/gs.lisp" "$WORK/gs.fasl"; then
    cat > "$WORK/gs-load.lisp" <<EOF
(load "$WORK/gs.fasl")
(format t "GS:~a ~a~%" (gcstress-use-swap 1 2) (gcstress-shared))
EOF
    out=$(run_stress "$WORK/gs-load.lisp")
    check_contains "FASL gensym/shared structure loads correctly" "GS:(2 1) (COMMON . COMMON)" "$out"
    check_absent   "no undefined-function from stale gensym ref"   "Undefined function" "$out"
else
    echo "  SKIP  gensym FASL compile failed"
fi

# --- Case 5: DEFINE-CONDITION under GC stress --------------------------------
# Bug: bi_register_condition_type cached `name`/`parent`/`slot_pairs` C locals
# before cl_cons() calls that can trigger compaction; stale offsets produced
# OOB-offset cons cells in condition_hierarchy / condition_slot_table.
cat > "$WORK/cond.lisp" <<'EOF'
(define-condition my-gcstress-condition (error)
  ((code :initarg :code :reader condition-code)
   (msg  :initarg :msg  :reader condition-msg)))
(let ((c (make-condition 'my-gcstress-condition :code 42 :msg "ok")))
  (format t "COND:~a ~a~%" (condition-code c) (condition-msg c)))
EOF
out=$(run_stress "$WORK/cond.lisp")
check_contains "define-condition works under GC stress"            "COND:42 ok" "$out"
check_absent   "no OOB offset in condition hierarchy/slot table"   "Undefined function\|type 0\|corrupted" "$out"

# --- Case 6: SETF GETHASH new key in EQ hashtable under GC stress -----------
# Bug: bi_setf_gethash captured bucket_idx + chain head before cl_cons();
# compaction relocated objects (EQ hashing is by offset) and rehashed the table,
# invalidating the stale bucket_idx — splicing onto wrong bucket cross-linked
# chains, corrupting subsequent GC walks of the table.
cat > "$WORK/ht.lisp" <<'EOF'
(let ((ht (make-hash-table :test 'eq)))
  (dotimes (i 20)
    (let ((k (make-symbol (format nil "K~a" i))))
      (setf (gethash k ht) (* i 10))))
  (let ((sum 0))
    (maphash (lambda (k v) (declare (ignore k)) (incf sum v)) ht)
    (format t "HT:~a~%" sum)))
EOF
out=$(run_stress "$WORK/ht.lisp")
check_contains "eq-hashtable setf-gethash new key correct under stress" "HT:1900" "$out"
check_absent   "no OOB offset in hash-table chains"                     "Undefined function\|type 0\|corrupted" "$out"

# --- Case 7: handler-case compiled under GC stress ---------------------------
# Bug: compile_lambda did not GC-protect inner->ll fields (CL_ParsedLambdaList)
# or inner->param_vars[].  Any compacting GC triggered inside compile_expr
# (for optional/key/aux defaults) or determine_boxed_vars left those CL_Obj
# values stale.  When the env lookup failed (eq mismatch after relocation),
# the parameter was compiled as a free/special variable, producing
# "Unbound variable: BOX<n>" at runtime.
# Fix: cl_compiler_gc_mark/update_thread now walks c->ll and c->param_vars.
cat > "$WORK/hcase.lisp" <<'EOF'
(let ((result (handler-case (error "x") (error (c) (format nil "~a" c)))))
  (format t "HCASE:~a~%" result))
EOF
out=$(run_stress "$WORK/hcase.lisp")
check_contains "handler-case compiles correctly under GC stress"  "HCASE:x" "$out"
check_absent   "no unbound-variable from stale lambda-list sym"   "Unbound variable" "$out"

# --- Case 8: LET*/LET binding-name symbol survives compaction ----------------
# Bug: local_specials (the list built by scan_local_specials) was not
# GC-protected during scan_body_for_boxing / compile_expr calls.  Compaction
# relocated its cons cells so var_is_special walked freed/moved memory,
# potentially misclassifying variables and emitting wrong OP_DYNBIND vs
# OP_STORE bytecode.  Fix: CL_GC_PROTECT(local_specials) after scan.
cat > "$WORK/let.lisp" <<'EOF'
(let* ((x (make-list 100))
       (y (length x)))
  (format t "LET*:~a~%" y))
(let ((a (make-list 50))
      (b (make-list 50)))
  (format t "LET:~a~%" (+ (length a) (length b))))
(let* ((x (make-list 100)))
  (declare (special x))
  (format t "SPEC:~a~%" (length x)))
EOF
out=$(run_stress "$WORK/let.lisp")
check_contains "let* binding-name symbol survives compaction" "LET*:100" "$out"
check_contains "let binding-name symbol survives compaction"  "LET:100" "$out"
check_contains "local special decl: local_specials cons survives compaction" "SPEC:100" "$out"
check_absent   "no unbound-variable from stale binding name"  "Unbound variable" "$out"

# --- Case 9: HANDLER-BIND compiled under GC stress ---------------------------
# Bug: compile_handler_bind did not protect the clause-list cursor (cl) across
# compile_expr, and extracted type_sym before compile_expr but used it after —
# after compaction type_sym held a stale arena offset, so a wrong symbol was
# stored in the bytecode constants (corrupting the handler-push type check).
# Fix: protect cl, re-read type_sym from cl_car(cl_car(cl)) after compile_expr.
#
# The handler runs (return-from done ...) to a block established LEXICALLY
# around the handler-bind — the idiomatic non-local-exit pattern (the block
# must be visible to the handler closure; a block inside the body is NOT, per
# CLHS).  Printing the condition inside the handler with ~A also exercises the
# condition-report + format-stream GC-stress fixes (see Cases 16-17).
cat > "$WORK/hbind.lisp" <<'EOF'
(let ((result
       (block done
         (handler-bind ((error (lambda (c) (return-from done (format nil "caught:~a" c)))))
           (error "hbind-test")))))
  (format t "HBIND:~a~%" result))
EOF
out=$(run_stress "$WORK/hbind.lisp")
hbind_exit=$?
check_contains "handler-bind type_sym survives compile_expr compaction" "HBIND:caught:hbind-test" "$out"
check_absent   "no unbound-variable from stale handler type symbol"     "Unbound variable\|Undefined" "$out"
total=$((total + 1))
if [ "$hbind_exit" -eq 0 ]; then
    echo "  ok  handler-bind exits clean"
    passed=$((passed + 1))
else
    echo "  FAIL  handler-bind exited $hbind_exit (crash?)"
    echo "$out" | tail -4 | sed 's/^/      /'
    failed=$((failed + 1))
fi

# --- Case 10: RESTART-CASE compiled under GC stress --------------------------
# Bug: compile_restart_case did not protect the clause-list cursor (cl_iter) or
# catch_tag across compile_expr/cl_cons calls inside the loop; restart_name was
# extracted before compile_expr but used after, going stale.
# Fix: protect cl_iter and catch_tag, re-read restart_name after compile_expr.
cat > "$WORK/rcase.lisp" <<'EOF'
(let ((result
       (restart-case (invoke-restart 'my-restart 99)
         (my-restart (v) (format nil "restarted:~a" v)))))
  (format t "RCASE:~a~%" result))
EOF
out=$(run_stress "$WORK/rcase.lisp")
check_contains "restart-case restart_name survives compile_expr compaction" "RCASE:restarted:99" "$out"
check_absent   "no undefined from stale restart-case catch_tag/name"        "Undefined\|Unbound" "$out"

# --- Case 11: CASE/TYPECASE compiled under GC stress -------------------------
# Bug: compile_case and compile_typecase did not protect keys/type_spec and body
# locals across compile_progn; a moving GC during body compilation could leave
# them stale if the code order were ever changed.
# Fix: CL_GC_PROTECT(keys/type_spec) + CL_GC_PROTECT(body) per iteration.
cat > "$WORK/case.lisp" <<'EOF'
(let ((x 2))
  (format t "CASE:~a~%"
    (case x
      (1 "one")
      (2 "two")
      (3 "three")
      (otherwise "other"))))
(let ((v "hello"))
  (format t "TYPECASE:~a~%"
    (typecase v
      (string (format nil "str:~a" v))
      (integer "int")
      (otherwise "other"))))
EOF
out=$(run_stress "$WORK/case.lisp")
check_contains "case compiles correctly under GC stress"     "CASE:two" "$out"
check_contains "typecase compiles correctly under GC stress" "TYPECASE:str:hello" "$out"
check_absent   "no unbound/undefined from stale case locals" "Unbound variable\|Undefined" "$out"

# --- Case 12: Quasiquote builder compiled under GC stress --------------------
# Bug: qq_expand's main list loop held `cursor` and `rest` as unprotected
# C-locals across qq_expand_list (which allocates).  cursor=rest at end of
# each iteration used the pre-allocation (stale) rest offset, so later
# elements were walked from corrupted memory.  Helper functions qq_list /
# qq_append / qq_quote nested cl_cons calls where outer args were read into
# unspecified-order temporaries before inner cl_cons could compact the heap
# and leave those temporaries stale.  compile_quasiquote itself did not
# protect tmpl across qq_expand.
# Fix: CL_GC_PROTECT(cursor) in the list loop, advance with cl_cdr(cursor)
# after each iteration; protect args in qq_list/qq_append; split all nested
# cl_cons into sequential statements; CL_GC_PROTECT(tmpl) in
# compile_quasiquote.
cat > "$WORK/qq.lisp" <<'EOF'
(defmacro make-pair (a b) `(cons ,a ,b))
(format t "QQ1:~a~%" (make-pair 1 2))

(defmacro with-items (&rest items) `(list ,@items))
(format t "QQ2:~a~%" (with-items 10 20 30))

(defun build-list (x y z)
  `(,x ,y ,z ,(+ x y z)))
(format t "QQ3:~a~%" (build-list 1 2 3))

(defmacro nested-qq (x)
  `(let ((v ,x))
     (list v (* v 2) (* v 3))))
(format t "QQ4:~a~%" (nested-qq 5))

(defmacro splice-test (&rest items)
  `(list 0 ,@items 99))
(format t "QQ5:~a~%" (splice-test 1 2 3))
EOF
out=$(run_stress "$WORK/qq.lisp")
check_contains "quasiquote cons pair builds correctly under GC stress"    "QQ1:(1 . 2)"    "$out"
check_contains "quasiquote splice list builds correctly under GC stress"  "QQ2:(10 20 30)" "$out"
check_contains "quasiquote unquote list builds correctly under GC stress" "QQ3:(1 2 3 6)"  "$out"
check_contains "nested quasiquote let form builds correctly under GC stress" "QQ4:(5 10 15)" "$out"
check_contains "quasiquote unquote-splicing builds correctly under GC stress" "QQ5:(0 1 2 3 99)" "$out"
check_absent   "no unbound/undefined from stale quasiquote cursor"        "Unbound variable\|Undefined" "$out"

# --- Case 13: DO* with result forms under GC stress -------------------------
# Bug: compile_do_star did not GC-protect result_forms.  After the body and
# step compile_expr loops (which can compact), result_forms held a stale
# arena-relative offset; compile_progn then walked freed/moved memory.
# Fix: CL_GC_PROTECT(result_forms) alongside var_clauses/body/end_test;
# CL_GC_UNPROTECT changed from 3 to 4.
cat > "$WORK/dostar.lisp" <<'EOF'
; DO* updates bindings sequentially: acc sees the already-updated i each step.
; i: 0->1->2->3->4->5; acc: 0, 0+1=1, 1+2=3, 3+3=6, 6+4=10, 10+5=15. Result: 15*2=30.
(let ((r (do* ((i 0 (1+ i))
               (acc 0 (+ acc i)))
              ((= i 5) (* acc 2)))))
  (format t "DOSTAR1:~a~%" r))
; x doubles each step: 1->2->4->8->16->32; exits when x>16. Result: 32.
(let ((r2 (do* ((x 1 (* x 2)))
               ((> x 16) x))))
  (format t "DOSTAR2:~a~%" r2))
EOF
out=$(run_stress "$WORK/dostar.lisp")
check_contains "do* result-form evaluates correctly under GC stress"   "DOSTAR1:30" "$out"
check_contains "do* result-form exit value correct under GC stress"    "DOSTAR2:32" "$out"
check_absent   "no crash/corruption in do* result_forms"               "Unbound variable\|Undefined\|type 0" "$out"

# --- Case 14: SETF AREF 1D under GC stress -----------------------------------
# Bug: in compile_setf_place's SETF_SYM_AREF 1D fast-path, `indices` was
# captured before CL_GC_PROTECT(val_form); after compile_expr(array) could
# compact, cl_car(indices) dereferenced a stale arena offset.
# Fix: CL_GC_PROTECT(place) added; index re-derived as cl_car(cl_cdr(cl_cdr(place))).
cat > "$WORK/aref-setf.lisp" <<'EOF'
(let ((v (make-array 5 :initial-element 0)))
  (dotimes (i 5)
    (setf (aref v i) (* i 10)))
  (format t "AREF:~a ~a ~a~%" (aref v 0) (aref v 2) (aref v 4)))
(let ((s (make-string 4 :initial-element #\space)))
  (setf (char s 0) #\H)
  (setf (char s 1) #\i)
  (format t "CHAR:~a~%" s))
EOF
out=$(run_stress "$WORK/aref-setf.lisp")
check_contains "setf aref 1D index survives compile_expr compaction"   "AREF:0 20 40" "$out"
check_contains "setf char index survives compile_expr compaction"      "CHAR:Hi  " "$out"
check_absent   "no crash in setf aref/char 1D path"                    "Unbound variable\|Undefined\|type 0" "$out"

# --- Case 15: SETF GETHASH / BIT multi-arg place under GC stress -------------
# Bug: in SETF_SYM_GETHASH / SETF_SYM_BIT / SETF_SYM_SBIT /
# SETF_SYM_ROW_MAJOR_AREF paths, place was read across two separate
# compile_expr calls without GC protection; the second cl_cdr(cl_cdr(place))
# dereference was a stale arena offset after the first compile_expr compacted.
# Fix: CL_GC_PROTECT(place) alongside val_form; CL_GC_UNPROTECT count updated.
cat > "$WORK/gethash-setf.lisp" <<'EOF'
(let ((ht (make-hash-table)))
  (dotimes (i 10)
    (setf (gethash i ht) (* i i)))
  (format t "GHASH:~a ~a ~a~%" (gethash 3 ht) (gethash 7 ht) (gethash 9 ht)))
(let ((bv (make-array 8 :element-type 'bit :initial-element 0)))
  (setf (bit bv 3) 1)
  (setf (bit bv 7) 1)
  (format t "BIT:~a ~a~%" (bit bv 3) (bit bv 7)))
EOF
out=$(run_stress "$WORK/gethash-setf.lisp")
check_contains "setf gethash second place arg survives compaction"     "GHASH:9 49 81" "$out"
check_contains "setf bit second place arg survives compaction"         "BIT:1 1" "$out"
check_absent   "no crash in setf gethash/bit multi-arg place"          "Unbound variable\|Undefined\|type 0" "$out"

# --- Case 16: nested env double-update in the compaction walk ----------------
# Bug: cl_compiler_gc_update_thread walked c->env AND its full env->parent
# chain for every compiler in the active chain.  Because each compiler's env
# parent mirrors the compiler->parent chain, every ancestor env was updated
# once per descendant compiler.  gc_update_slot is NOT idempotent (it forwards
# an arena offset), so a slot reached twice was double-forwarded, leaving a
# stale offset in env->locals; a closed-over gensym (e.g. handler-case's BOX)
# then failed the eq-compare in cl_env_lookup and was mis-emitted as a global
# load -> "Unbound variable: BOX<n>".  Repro needs a closure capturing an
# outer-LET gensym, built by a macro whose expansion allocates heavily (mapcar)
# so compaction fires while the env has >1 ancestor compiler — exactly what
# HANDLER-CASE's expansion does.  Fix: update only each compiler's own c->env.
cat > "$WORK/box.lisp" <<'EOF'
(defmacro gcs-box (form &rest clauses)
  (let ((tag (gensym "TG")) (box (gensym "BOX")))
    `(let ((,box (cons nil nil)))
       (let ((r (catch ',tag
                  (handler-bind
                    ,(mapcar (lambda (cl)
                               `(,(car cl) (lambda (c) (rplaca ,box c) (throw ',tag ',tag))))
                             clauses)
                    ,form))))
         (if (eq r ',tag)
             (typecase (car ,box)
               ,@(mapcar (lambda (cl)
                           `(,(car cl) (let ((,(car (cadr cl)) (car ,box))) ,@(cddr cl))))
                         clauses))
             r)))))
(format t "BOX:~a~%" (gcs-box (error "boom") (error (c) (format nil "got:~a" c))))
EOF
out=$(run_stress "$WORK/box.lisp")
check_contains "captured outer-let gensym resolves under compaction"   "BOX:got:boom" "$out"
check_absent   "no unbound-variable from double-forwarded env local"   "Unbound variable" "$out"

# --- Case 17: condition report string survives compaction in MAKE-CONDITION --
# Bug: bi_make_condition cached `key`/`val` C locals, then called cl_cons
# (which compacts) twice, then compared the STALE `key` against
# KW_FORMAT_CONTROL — the eq miss dropped :format-control, so report_string
# was never set and the condition printed as "#<CONDITION ...>" instead of its
# message.  Fix: read args[i]/args[i+1] (GC-rooted) after the cl_cons calls.
cat > "$WORK/cond-report.lisp" <<'EOF'
(let ((c (make-condition 'simple-error :format-control "the-report-msg")))
  (format t "REPORT:~a~%" c))
EOF
out=$(run_stress "$WORK/cond-report.lisp")
check_contains "make-condition keeps :format-control report under stress" "REPORT:the-report-msg" "$out"
check_absent   "condition does not lose its report string"                "REPORT:#<CONDITION" "$out"

# --- Case 18: FORMAT control string + destination stream survive compaction --
# Bug 1: FmtCtx.fmt/.pos were raw byte pointers into the control string's
#   inline arena data; a ~A/~S that printed an object whose printer compacted
#   relocated the string, dangling .pos and dropping trailing directives.
#   Fix: copy the control string into a non-arena buffer in fmt_str_as_utf8.
# Bug 2: FmtCtx.stream was a bare CL_Obj offset; printing an object that
#   compacted relocated a string-output-stream, so writes after the first ~A
#   were lost ("[~a]" -> "[").  Fix: CL_GC_PROTECT(ctx->stream) in fmt_run
#   (covers nested ~{ ~( ~[ ~? sub-contexts too).
# Printing a condition (heavy print-object-hook path) forces compaction.
cat > "$WORK/fmt-stream.lisp" <<'EOF'
(let ((c (make-condition 'simple-error :format-control "MID")))
  (format t "FMT:[~a]~%" (format nil "<~a>" c)))
(let ((cs (list (make-condition 'simple-error :format-control "x")
                (make-condition 'simple-error :format-control "y"))))
  (format t "FMTLIST:~{[~a]~}END~%" cs))
EOF
out=$(run_stress "$WORK/fmt-stream.lisp")
# NB: '[' and ']' are regex metacharacters here — escape them so check_contains
# (grep BRE) matches the literal brackets in the output.
check_contains "format trailing directive survives object-print compaction" 'FMT:\[<MID>\]' "$out"
check_contains "format string-stream survives nested ~{ object print"       'FMTLIST:\[x\]\[y\]END' "$out"

# --- Case 19: funcallable-instance check survives compaction (DEFCLASS redef) --
# Bug: cl_funcallable_instance_p cached the STANDARD-GENERIC-FUNCTION symbol in a
# static CL_Obj that was never registered as a GC root.  Symbols live in the
# moving arena, so after a compaction relocated that symbol the cached offset
# went stale; the type_desc == sym_sgf compare then failed for a live GF struct.
# The VM stopped recognizing the GF as funcallable and called it as a plain
# struct -> "Not a function: heap object type 10".  Manifestation was heap-layout
# dependent; the reliable trigger is DEFCLASS *redefinition*, whose %ensure-class
# path does (remove old-class (class-direct-subclasses standard-object)) — a
# REMOVE over a long list that shifts the live set enough to move the symbol,
# right before the next GF accessor call.  Fix: cl_gc_register_root(&sym_sgf).
# Also covers the related REMOVE fix (remove_from_list now protects its cursor +
# item/test/key locals across the cl_cons that builds the result).
cat > "$WORK/clos-redef.lisp" <<'EOF'
(defclass gcs-redef () ((id :initarg :id :accessor gcs-id)))
(format t "REDEF1:~a~%" (gcs-id (make-instance 'gcs-redef :id 7)))
(defclass gcs-redef () ((id :initarg :id :accessor gcs-id)
                        (tag :initarg :tag :accessor gcs-tag)))
(let ((o (make-instance 'gcs-redef :id 11 :tag 'hi)))
  (format t "REDEF2:~a ~a~%" (gcs-id o) (gcs-tag o)))
EOF
out=$(run_stress "$WORK/clos-redef.lisp")
check_contains "defclass first definition works under GC stress"        "REDEF1:7" "$out"
check_contains "defclass redefinition works under GC stress"            "REDEF2:11 HI" "$out"
check_absent   "no struct-as-function from stale funcallable-instance cache" "Not a function: heap object type" "$out"

# --- Case 20: two top-level DEFUNs — compile_defun lambda_list survives -------
# Bug: compile_defun read `lambda_list` (and `body`) from the defun form, then
# built block_body/lambda_form via cl_cons BEFORE consing lambda_list in.  Under
# a compacting GC those conses relocated the arena-resident param list out from
# under the unprotected `lambda_list` C local, so lambda_form baked in a stale
# offset; parse_lambda_list then read garbage params and miscounted the arity
# (e.g. `(n)` seen as 2 required) -> "Too few arguments".  Fix: GC-protect
# lambda_list and body at the top of compile_defun.
cat > "$WORK/two-defun.lisp" <<'EOF'
(defun gcs-f0 (n) (+ n 0))
(defun gcs-f1 (n) (+ n 1))
(format t "TWODEFUN:~a~%" (list (gcs-f0 10) (gcs-f1 10)))
EOF
out=$(run_stress "$WORK/two-defun.lisp")
check_contains "two separate top-level defuns get correct arity under stress" "TWODEFUN:(10 11)" "$out"
check_absent   "no arity miscount from stale defun lambda-list"               "Too few arguments\|Too many arguments" "$out"

# --- Case 21: direct special-var read at a LET tail after an allocating loop --
# Bug: compile_let walked the body form list with an UNPROTECTED C-local cursor
# `rest`.  The non-tail body forms are compiled with compile_expr (which
# allocates and can compact); the body's cons cells then relocate, but `rest`
# kept its pre-compaction offset, so `rest = cl_cdr(rest)` followed a stale
# offset and `tail = cl_car(rest)` read garbage.  When the tail form was a bare
# special-var read (`*x*`), the garbage tail was NIL, so compile_symbol's
# CL_NULL_P(sym) branch emitted OP_NIL instead of OP_GLOAD *x* — the let
# returned NIL at runtime instead of the dynamic value.  Manifests only when the
# body's cells are still moving (cold/first compile of the pattern).  Fix:
# CL_GC_PROTECT(rest) across the body-compile loop in compile_let.  (Same class
# of cursor bug also fixed in compile_call's inline-builtin / amiga-ffi arg
# loops and the SETF place-decomposition loop.)
cat > "$WORK/specloop.lisp" <<'EOF'
(defvar *gcs-sv* nil)
(format t "SPECLOOP1:~a~%"
  (let ((*gcs-sv* 7)) (dotimes (i 40) (make-list 10)) *gcs-sv*))
(format t "SPECLOOP2:~a~%"
  (let ((*gcs-sv* 99)) (dotimes (i 40) (make-list 10)) (list *gcs-sv* *gcs-sv*)))
EOF
out=$(run_stress "$WORK/specloop.lisp")
check_contains "direct special read at let tail after loop returns bound value" "SPECLOOP1:7" "$out"
check_contains "direct special read at let tail (in list) after loop correct"   "SPECLOOP2:(99 99)" "$out"
check_absent   "let special tail read not mis-compiled to OP_NIL"               "SPECLOOP1:NIL" "$out"

# --- Case 22: DOTIMES/DOLIST/TAGBODY + conditionals + setf-expansion macros --
# Several UNPROTECTED C-local cursors in the compiler corrupted code compiled
# under a moving GC:
#   (a) compile_tagbody's local-path loop walked `cursor` across compile_expr
#       (allocating); a compaction relocated the body and the stale cursor
#       re-read it, RE-EMITTING the body — a `(when ...)` inside a DOTIMES/DOLIST
#       ran twice per iteration (counted to 2x).
#   (b) compile_dotimes / compile_dolist held `binding`/`body`/`var`/
#       `result_form` across compile_expr(count/list-form) and the tagbody
#       compile; a compaction left them stale → cl_cdr(binding) faulted with
#       "CDR: argument is not of type LIST" while compiling SETF/SETQ bodies.
#   (c) determine_boxed_vars / scan_body_for_boxing / nlx_scan walked the body
#       with an unprotected cursor across the VM macroexpansion of INCF/DECF
#       (setf-expansion macros); with >=2 forms the stale cursor derailed.
#   (d) compile_tagbody ran tree_has_closure_forms(body) — which macroexpands
#       body macros in the VM — BEFORE protecting `body`.  The synthetic
#       DOTIMES/DOLIST tagbody shares its body cons cells with the loop form;
#       the unprotected `(incf n)` cell was swept and its slot reused by the
#       expansion's own `(setq n NEW)` cons, so the LET* binding wrapper was
#       dropped and the store gensym compiled as a global ("Unbound variable:
#       NEW<n>").
# Fixes: GC_PROTECT the cursors in compile_tagbody, compile_dotimes,
# compile_dolist, determine_boxed_vars, scan_body_for_boxing and nlx_scan, and
# protect `body` in compile_tagbody BEFORE the tree_has_closure_forms scan.
# All run cleanly compiled then executed under stress (probe via a defun) — the
# runtime is fine; only compile-under-compaction was affected.
cat > "$WORK/loopmacro.lisp" <<'EOF'
;; (a) when/unless inside a loop must run exactly once per iteration
(format t "WHENCNT:~a~%"
  (let ((n 0)) (dotimes (i 50) (when (= 1 1) (setf n (+ n 1)))) n))
(format t "UNLESSCNT:~a~%"
  (let ((n 0)) (dotimes (i 50) (unless (= 1 2) (setf n (+ n 1)))) n))
(format t "DOLISTCNT:~a~%"
  (let ((n 0)) (dolist (x (list 1 2 3 4 5)) (when t (setf n (+ n 1)))) n))
;; (b) setf/setq body in a loop must compile cleanly (no CDR type-error)
(format t "SETFLOOP:~a~%"
  (let ((n 0)) (dotimes (i 50) (setf n (+ n 1))) n))
;; dotimes as a NON-last form in a let body, trailing a local-var read
(format t "DTNONLAST:~a~%"
  (let ((n 0)) (dotimes (i 5) 1) n))
;; (c)+(d) incf/decf (setf-expansion gensyms) in loop / sequence bodies
(format t "INCFDT:~a~%"   (let ((n 0)) (dotimes (i 50) (incf n)) n))
(format t "INCFWHEN:~a~%" (let ((n 0)) (dotimes (i 50) (when (evenp i) (incf n))) n))
(format t "INCFDL:~a~%"   (let ((n 0)) (dolist (x (list 1 2 3)) (incf n)) n))
(format t "INCFDO:~a~%"   (let ((n 0)) (do ((i 0 (1+ i))) ((= i 3) n) (incf n))))
(format t "DECFDT:~a~%"   (let ((n 50)) (dotimes (i 5) (decf n)) n))
(format t "INCF2:~a~%"    (let ((n 0)) (incf n) (incf n) n))
(format t "INCFNEST:~a~%" (let ((n 0)) (dotimes (i 3) (dotimes (j 2) (incf n))) n))
EOF
out=$(run_stress "$WORK/loopmacro.lisp")
check_contains "when inside dotimes runs once per iteration"     "WHENCNT:50" "$out"
check_contains "unless inside dotimes runs once per iteration"   "UNLESSCNT:50" "$out"
check_contains "when inside dolist runs once per iteration"      "DOLISTCNT:5" "$out"
check_contains "setf body in dotimes compiles cleanly"           "SETFLOOP:50" "$out"
check_contains "dotimes non-last form, trailing local read"      "DTNONLAST:0" "$out"
check_contains "incf in dotimes (setf-expansion gensym)"         "INCFDT:50" "$out"
check_contains "incf in when-in-dotimes"                         "INCFWHEN:25" "$out"
check_contains "incf in dolist"                                  "INCFDL:3" "$out"
check_contains "incf in do"                                      "INCFDO:3" "$out"
check_contains "decf in dotimes"                                 "DECFDT:45" "$out"
check_contains "two sequential incf in a let body"              "INCF2:2" "$out"
check_contains "incf in nested dotimes"                          "INCFNEST:6" "$out"
check_absent   "no when/loop body double-emit (count not doubled)" "WHENCNT:100\|DOLISTCNT:10" "$out"
check_absent   "no CDR type-error compiling setf/loop body"      "CDR: argument is not of type" "$out"
check_absent   "no dropped LET* binding from setf-expansion"     "Unbound variable: NEW\|Unbound variable: DELTA" "$out"

# --- Case: FFI typed peek/poke + foreign calls + callbacks under GC stress ---
# The host FFI engine allocates on several paths that hold raw C pointers and
# CL_Obj values across allocating calls:
#   - typed peek boxes results (peek-u64 -> bignum, peek-double -> float,
#     peek-pointer -> register + foreign-pointer);
#   - call-foreign boxes its result and, for callbacks, the handler boxes each
#     C argument into a CL_Obj (CL_GC_PROTECT'd) before re-entering the VM;
#   - pointer+ / make-pointer register entries in the POSIX side table whose
#     slots the TYPE_FOREIGN_POINTER GC finalizer reclaims.
# A compaction forced inside any of these must not corrupt the in-flight
# values.  This is host-only (dlopen/libffi); on a non-host build the symbols
# resolve to NIL and the test is effectively skipped by the require guard.
# Like the FASL cases above, the test code is compiled with a CLEAN run and
# then LOADED + RUN under stress — this stresses the FFI *runtime* (the GC
# finalizer and the allocating boxing paths) without subjecting the compiler
# to compaction.  Every operation is a C builtin in the FFI package, so no
# Lisp library load is needed.
cat > "$WORK/ffi.lisp" <<'EOF'
(defpackage :gcstress-ffi (:use :cl) (:export #:run))
(in-package :gcstress-ffi)
(defun run ()
  ;; typed peek/poke whose reads allocate (bignum / float / foreign-pointer)
  (let ((p (ffi:alloc-foreign 16)))
    (ffi:poke-u64 p 4294967300) (ffi:poke-double p 6.25d0 8)
    (format t "FFI-MEM:~a ~a~%" (ffi:peek-u64 p) (ffi:peek-double p 8))
    (ffi:free-foreign p))
  ;; foreign-pointer registration + finalizer churn: each pointer+ allocates a
  ;; new foreign pointer (a compaction under stress); transient ones become
  ;; garbage and must be reclaimed by the TYPE_FOREIGN_POINTER finalizer.
  (let ((base (ffi:alloc-foreign 64)) (n 0))
    (dotimes (i 200)
      (when (ffi:foreign-pointer-p (ffi:pointer+ base (mod i 64)))
        (setf n (+ n 1))))
    (format t "FFI-PTRS:~a~%" n)
    (ffi:free-foreign base))
  ;; real foreign calls + a Lisp callback driven by qsort (the callback handler
  ;; boxes pointer args and the comparator allocates fixnums every compaction)
  (let ((abs* (ffi:symbol-pointer "abs"))
        (pow* (ffi:symbol-pointer "pow"))
        (qsort* (ffi:symbol-pointer "qsort")))
    (when (and abs* pow* qsort*)
      (format t "FFI-CALL:~a ~a~%"
              (ffi:call-foreign abs* :int32 '(:int32) '(-99))
              (ffi:call-foreign pow* :double '(:double :double) '(2.0d0 10.0d0)))
      (let ((arr (ffi:alloc-foreign 20))
            (cmp (ffi:make-callback :int32 '(:pointer :pointer)
                   (lambda (a b) (make-list 4) (- (ffi:peek-i32 a) (ffi:peek-i32 b))))))
        (loop for v in '(9 2 7 1 5) for i from 0 do (ffi:poke-i32 arr v (* i 4)))
        (ffi:call-foreign qsort* :void '(:pointer :uint64 :uint64 :pointer)
                          (list arr 5 4 cmp))
        (format t "FFI-SORT:~a~%"
                (loop for i below 5 collect (ffi:peek-i32 arr (* i 4))))
        (ffi:free-foreign arr)))))
EOF
if compile_fasl "$WORK/ffi.lisp" "$WORK/ffi.fasl"; then
    cat > "$WORK/ffi-load.lisp" <<EOF
(load "$WORK/ffi.fasl")
(gcstress-ffi:run)
EOF
    out=$(run_stress "$WORK/ffi-load.lisp")
    check_contains "ffi typed peek/poke boxes results under stress"  "FFI-MEM:4294967300 6.25" "$out"
    check_contains "ffi pointer+ registration/finalizer survives"    "FFI-PTRS:200" "$out"
    check_contains "ffi foreign calls return correct values"         "FFI-CALL:99 1024.0" "$out"
    check_contains "ffi callback comparator sorts under stress"      "FFI-SORT:(1 2 5 7 9)" "$out"
    check_absent   "no corruption in FFI alloc/call/callback paths"  "Unbound variable\|type 0\|corrupted\|Undefined function" "$out"
else
    echo "  SKIP  FFI gc-stress: clean FASL compile failed"
fi

# --- Case: READ-FROM-STRING / string-input-stream under GC stress -----------
# Bug: cl_make_string_input_stream called cl_make_stream() (which allocates the
# stream object and can compact) WITHOUT GC-protecting its `string` argument.
# Compaction relocated the source string, leaving `string` a stale offset, so
# st->string_buf pointed at moved/garbage memory.  Every subsequent char read
# from the stream came from the wrong place, so READ-FROM-STRING dropped or
# mangled list elements ("CDR: argument is not of type LIST" on >=3-elem lists,
# or a short list).  Also fixed: cl_read_from_stream wrote st->line through a
# raw CL_Stream* cached before read_expr() (which compacts), clobbering whatever
# object had moved into the stream's old slot.
cat > "$WORK/rfs.lisp" <<'EOF'
(format t "RFS3:~a~%" (length (read-from-string "(a b c)")))
(format t "RFS5:~a~%" (length (read-from-string "(a b c d e)")))
(format t "RFS-ELT:~a~%" (read-from-string "(a b c)"))
(format t "RFS-NEST:~a~%"
        (length (read-from-string "((1 2) (3 4) (5 6))")))
EOF
out=$(run_stress "$WORK/rfs.lisp")
check_contains "read-from-string 3-elem list length"            "RFS3:3" "$out"
check_contains "read-from-string 5-elem list length"            "RFS5:5" "$out"
check_contains "read-from-string list contents intact"          "RFS-ELT:(A B C)" "$out"
check_contains "read-from-string nested list length"            "RFS-NEST:3" "$out"
check_absent   "no CDR type-error from stale string-buf"        "not of type LIST" "$out"

# NB: the post-compaction rehash of EQUAL/EQL/EQUALP hash tables and of the
# per-thread TLV (dynamic-binding) table is NOT exercised here.  Those bugs
# require a *moving* compaction that relocates a live object to a genuinely new
# arena offset; the GC-stress harness compacts to a canonical layout every
# alloc, which keeps offsets stable enough that the stale-bucket never surfaces.
# They are covered deterministically by tests/test_gc_rehash.c (forced
# cl_gc_compact()) and end-to-end by the babel cold-load integration test.

# --- Case: #'(setf name) ref + LABELS with >32 functions ------------------
# Two compiler fixes exposed by chipz inflate.lisp's gunzip state machine:
#  (a) #'(setf accessor) used to FLOAD the raw (SETF ACCESSOR) cons (a
#      non-symbol) into the constant pool, which OP_FLOAD rejected.  Resolving
#      it to the setter symbol allocates (interns %SETF-<name> / adds a
#      constant) at compile time, so compile under stress to relocate it.
#  (b) a LABELS with >32 local functions silently failed to bind the overflow
#      ones (CL_MAX_LOCAL_FUNS was 32 -> now 64), so #'fn36 resolved to an
#      undefined global.  Build/compile a 36-function LABELS and call a late
#      one — all under GC stress so the constant pool and local-fun env get
#      compacted while being built.
cat > "$WORK/setf-labels.lisp" <<'EOF'
(in-package :cl-user)
(defparameter *slc* (list 0))
(defun (setf slc-acc) (v x) (setf (car x) v) v)
(defun slc-run ()
  ;; #'(setf slc-acc) must resolve + funcall.
  (funcall #'(setf slc-acc) 7 *slc*)
  ;; A 36-function LABELS; reference the 36th (FN35) via #'.
  (labels ((fn0 () 0)  (fn1 () 1)  (fn2 () 2)  (fn3 () 3)  (fn4 () 4)
           (fn5 () 5)  (fn6 () 6)  (fn7 () 7)  (fn8 () 8)  (fn9 () 9)
           (fn10 () 10) (fn11 () 11) (fn12 () 12) (fn13 () 13) (fn14 () 14)
           (fn15 () 15) (fn16 () 16) (fn17 () 17) (fn18 () 18) (fn19 () 19)
           (fn20 () 20) (fn21 () 21) (fn22 () 22) (fn23 () 23) (fn24 () 24)
           (fn25 () 25) (fn26 () 26) (fn27 () 27) (fn28 () 28) (fn29 () 29)
           (fn30 () 30) (fn31 () 31) (fn32 () 32) (fn33 () 33) (fn34 () 34)
           (fn35 () 35))
    (+ (car *slc*) (funcall #'fn35))))
EOF
cat > "$WORK/setf-labels-run.lisp" <<EOF
(load "$WORK/setf-labels.lisp")
(format t "SLC:~a~%" (slc-run))
EOF
out=$(run_stress "$WORK/setf-labels-run.lisp")
check_contains "#'(setf name) + >32-fn LABELS under stress" "SLC:42" "$out"
check_absent   "no corrupted constant pool from #'(setf)"   "corrupted constant pool" "$out"
check_absent   "no undefined-global from LABELS overflow"   "Undefined function: FN35" "$out"

# --- Case: MAKE-ARRAY :element-type deftype alias under GC stress -------------
# classify_array_elt_type calls cl_vm_apply (to evaluate the deftype expander),
# which can trigger a compacting GC.  After compaction the `element_type` C
# local in bi_make_array held a stale arena offset; the subsequent
# !CL_NULL_P(element_type) check at the "default initial-element to 0" site
# happened to be correct in practice (non-zero stale offset → non-NIL), but
# violated GC safety.
# Fix: capture has_element_type_spec = !CL_NULL_P(element_type) BEFORE calling
# classify_array_elt_type, and use the boolean instead of re-reading element_type.
cat > "$WORK/mkarr-deftype.lisp" <<'EOF'
;; deftype alias for CHARACTER -> classify_array_elt_type expands via cl_vm_apply
(deftype my-char () 'character)
(let ((s (make-array 5 :element-type 'my-char :initial-element #\a)))
  (format t "MKARR-CHAR:~a ~a~%" (stringp s) (length s)))
;; deftype alias for BIT -> classify expands
(deftype my-bit () 'bit)
(let ((bv (make-array 8 :element-type 'my-bit :initial-element 0)))
  (setf (bit bv 3) 1)
  (format t "MKARR-BIT:~a ~a~%" (bit-vector-p bv) (bit bv 3)))
;; deftype alias for T (general) -> no char/bit path, should still build a vector
(deftype my-t () t)
(let ((v (make-array 4 :element-type 'my-t)))
  (setf (aref v 0) 42)
  (format t "MKARR-T:~a ~a~%" (vectorp v) (aref v 0)))
EOF
out=$(run_stress "$WORK/mkarr-deftype.lisp")
check_contains "make-array with deftype CHARACTER alias makes a string"       "MKARR-CHAR:T 5"   "$out"
check_contains "make-array with deftype BIT alias makes a bit-vector"         "MKARR-BIT:T 1"    "$out"
check_contains "make-array with deftype T alias makes a vector"               "MKARR-T:T 42"     "$out"
check_absent   "no corruption from stale element_type across classify GC"     "Unbound variable\|type 0\|corrupted\|Undefined" "$out"

# --- Case: DESTRUCTURING-BIND &optional+&rest+&key under GC stress ----------
# Regression for the compile_destructure_pattern fix: the &rest handler nested
# in the &optional branch used to `goto done`, skipping a trailing &key (so the
# &key var was unbound).  This is the cl-who WITH-HTML-OUTPUT lambda-list shape.
# Exercise it under stress because destructuring-bind builds/holds CL_Obj
# pattern cursors across compile_expr (default-value) compaction.
cat > "$WORK/db-opt-rest-key.lisp" <<'EOF'
(defun dbtest (args)
  (destructuring-bind (var &optional stream &rest rest &key indent) args
    (format nil "~a/~a/~a/~a" var stream rest indent)))
;; Call many times so a compaction lands during the destructuring binds.
(let ((last nil))
  (dotimes (i 40)
    (setq last (dbtest (list (intern (format nil "V~a" i)) nil :indent i))))
  (format t "DB:~a~%" last))
EOF
out=$(run_stress "$WORK/db-opt-rest-key.lisp")
# NB: ~a (princ) drops the keyword colon, so (:INDENT 39) prints as (INDENT 39).
check_contains "destructuring &optional+&rest+&key binds keys under stress" "DB:V39/NIL/(INDENT 39)/39" "$out"
check_absent   "no unbound/corruption in opt+rest+key destructuring"        "Unbound variable\|type 0\|corrupted\|Undefined" "$out"

# --- Case: DEFINE-CONDITION :report function-name SYMBOL under GC stress -----
# Regression for the boot.lisp :report fix: a SYMBOL :report names a function;
# it must be funcalled as a function designator (',report), not spliced bare
# (read as a variable -> "Unbound variable").  Stress the make-condition +
# report-rendering allocation path.
cat > "$WORK/cond-report-symbol.lisp" <<'EOF'
(defun my-rep-fn (c s) (declare (ignore c)) (write-string "rep-ok" s))
(define-condition my-rep-gcs (error) ((x :initarg :x)) (:report my-rep-fn))
(let ((out nil))
  (dotimes (i 30)
    (setq out (format nil "~a" (make-condition 'my-rep-gcs :x i))))
  (format t "REPSYM:~a~%" out))
EOF
out=$(run_stress "$WORK/cond-report-symbol.lisp")
check_contains "define-condition :report symbol funcalls under stress"   "REPSYM:rep-ok" "$out"
check_absent   "no unbound-variable from :report symbol under stress"     "Unbound variable\|type 0\|corrupted" "$out"

# --- Case: long-form DEFSETF under GC stress --------------------------------
# Regression for the compile_defsetf long-form fix (was the hunchentoot
# "threaded type=0" misdiagnosis): the C compiler delegates the long form to
# clamiga::%defsetf-long, which builds a define-setf-expander.  Exercise that
# expansion + the (setf (place ...) v) compilation path under compaction so a
# stale form/lambda-list offset in the long-form expansion would surface.
#
# TWO places per run, deliberately.  Compiling a *second* define-setf-expander
# place under forced-every-alloc compaction used to trip a GC bug — the
# expander's gensym binding name was lost from the compile env so the body's
# reference mis-emitted as a global → "Unbound variable: TMP<n>/BS<n>".  Root
# cause: MAPCAR #'LIST in the C-side setf-expander wrapper called the builtin
# with an unprotected C arg array; LIST consed (compacting) while reading its
# args, splitting the shared `bs`/`val` gensym (one occurrence forwarded, one
# stale).  Fixed by GC-rooting builtin args in cl_vm_apply.  See the
# [defsetf-expander-gcstress-unbound] memory note.
cat > "$WORK/defsetf-long.lisp" <<'EOF'
(defvar *dsg* (make-hash-table :test 'equal))
(defun dsg (k &optional (tag :d)) (gethash (cons k tag) *dsg*))
(defsetf dsg (k &optional (tag :d)) (v)
  `(setf (gethash (cons ,k ,tag) *dsg*) ,v))
(setf (dsg "a") 42)
(setf (dsg "b") 99)
(format t "DSETFL:~a:~a:~a~%" (dsg "a") (dsg "b") (dsg "a" :d))
EOF
out=$(run_stress "$WORK/defsetf-long.lisp")
check_contains "long-form defsetf 2 places set/get correct under GC stress" "DSETFL:42:99:42" "$out"
check_absent   "no corrupted FLOAD/cons from stale defsetf expansion"     "not a symbol\|type 0\|corrupted\|Undefined\|Unbound" "$out"

# --- Case: shipped expander places (LDB/GETF/MASK-FIELD) twice under stress --
# Same class as the long-form defsetf two-place case: the shipped
# DEFINE-SETF-EXPANDERs for LDB/GETF/MASK-FIELD route through the same MAPCAR
# #'LIST wrapper.  Two places each previously split a gensym → "Unbound
# variable: BS<n>" / "CDR: corrupted pointer".
cat > "$WORK/expander-places.lisp" <<'EOF'
(let ((a 0) (b 0))
  (setf (ldb (byte 4 0) a) 5)
  (setf (ldb (byte 4 0) b) 7)
  (format t "LDB2:~a:~a~%" a b))
(let ((p1 (list :x 1)) (p2 (list :y 2)))
  (incf (getf p1 :x) 10)
  (incf (getf p2 :z 0) 20)
  (format t "GETF2:~a:~a~%" (getf p1 :x) (getf p2 :z)))
(let ((a 255) (b 255))
  (setf (mask-field (byte 4 0) a) 3)
  (setf (mask-field (byte 4 4) b) 0)
  (format t "MASK2:~a:~a~%" a b))
EOF
out=$(run_stress "$WORK/expander-places.lisp")
check_contains "ldb place x2 correct under GC stress"        "LDB2:5:7"    "$out"
check_contains "getf place x2 correct under GC stress"       "GETF2:11:20" "$out"
check_contains "mask-field place x2 correct under GC stress" "MASK2:243:15" "$out"
check_absent   "no split-gensym Unbound/corruption from expander places" "Unbound\|corrupted\|not a symbol\|type 0" "$out"

# --- Case: MAPCAR with an allocating builtin (#'LIST) under GC stress --------
# Direct regression for the cl_vm_apply builtin-arg-rooting fix: MAPCAR #'LIST
# over fresh gensyms must not split any element (LIST conses mid-read).
cat > "$WORK/mapcar-list.lisp" <<'EOF'
(let* ((xs (list 'a 'b 'c 'd))
       (ys (list 1 2 3 4))
       (zs (mapcar #'list xs ys)))
  ;; Each pair's car must still be eq to the original symbol (not a split ghost)
  (format t "MAPL:~a:~a~%"
          (every (lambda (pair orig) (eq (car pair) orig)) zs xs)
          zs))
EOF
out=$(run_stress "$WORK/mapcar-list.lisp")
check_contains "mapcar #'list preserves element identity under GC stress" "MAPL:T:" "$out"
check_absent   "no corruption from mapcar #'list under GC stress"         "corrupted\|not a symbol\|type 0\|Unbound" "$out"

# --- Case: string fns on fill-pointer strings under GC stress ---------------
# Regression for the CL_STRING_VECTOR_P string-fn fix: adjustable/fill-pointer
# character vectors are valid CL strings.  cl_string_copy materializes them
# (allocates) — exercise that + STRING-EQUAL/STRING-UPCASE under compaction.
cat > "$WORK/string-vec.lisp" <<'EOF'
(let ((hits 0))
  (dotimes (i 30)
    (let ((s (make-array 0 :element-type 'character :fill-pointer 0 :adjustable t)))
      (vector-push-extend #\H s) (vector-push-extend #\i s)
      (when (string-equal s "hi") (incf hits))
      (when (string= (string-upcase s) "HI") (incf hits))
      (with-output-to-string (o) (write-string s o))))
  (format t "STRVEC:~a~%" hits))
EOF
out=$(run_stress "$WORK/string-vec.lisp")
check_contains "string fns on fill-pointer string correct under GC stress" "STRVEC:60" "$out"
check_absent   "no not-a-string-designator from stale string-vector copy"  "not a string designator\|type 0\|corrupted" "$out"

# --- Case: LOOP mixed COLLECT/NCONC accumulation under GC stress ------------
# Regression for the loop-accumulation order fix: COLLECT/NCONC/APPEND now all
# build the result list reversed (push / NRECONC / REVAPPEND) under one final
# NREVERSE, so they can be mixed and still keep source order (CLHS 6.1.3).  The
# accumulator list is built incrementally with allocating ops; exercise it under
# forced-every-alloc compaction so a stale accumulator/tail offset would surface
# (this is the cl-ppcre normalize-var-list shape that reversed regex groups).
# NB: this exercises the if/else mix (the cl-ppcre normalize-var-list shape) —
# the accumulator is built via NRECONC/REVAPPEND/push and finalized with one
# NREVERSE.  (The separate two-clause / bare-COLLECT compiler-cursor bug it was
# once grouped with here is now fixed — see the next case.)
cat > "$WORK/loop-accum.lisp" <<'EOF'
(defun la-mix (xs)
  (loop for x in xs if (consp x) nconc (copy-list x) else collect x))
(let ((last nil))
  (dotimes (i 40)
    (setq last (la-mix (list 'a (list i (+ i 1)) 'b))))
  (format t "LACC:~a~%" last))
EOF
out=$(run_stress "$WORK/loop-accum.lisp")
check_contains "loop mixed collect/nconc keeps order under GC stress" "LACC:(A 39 40 B)" "$out"
check_absent   "no reversal/corruption in loop accumulation under stress" "corrupted\|type 0\|Unbound" "$out"

# --- Case: BLOCK body survives compaction during the NLX scan ----------------
# Regression for the compile_block GC-safety fix.  compile_block calls
# tree_needs_nlx_block(body, tag) — which MACROEXPANDS the body (nlx_scan_body
# sees through macros to detect a return-from crossing a closure) and therefore
# allocates — while `body`/`tag` were still unprotected C locals.  Under
# forced-every-alloc compaction the body cons relocated, the post-scan
# CL_GC_PROTECT pinned a STALE offset, and compile_nontail_body returned a
# freed-then-reused cell as the block's "tail": the real (let* ...) was dropped
# and a bare gensym surfaced as "Unbound variable: ITEM<n>/ACC<n>".  Every LOOP
# expands to (block nil (let* ...)), so a bare-COLLECT loop and a loop with two
# sequential accumulation clauses per iteration are the natural triggers.  Same
# class as the compile_tagbody/tree_has_closure_forms scan-before-protect fix.
cat > "$WORK/loop-block.lisp" <<'EOF'
;; Bare top-level COLLECT (was "Unbound variable: ACC<n>")
(format t "BARE:~a~%" (loop for x in '(1 2 3) collect x))
;; Two accumulation clauses per iteration (was "Unbound variable: ITEM<n>")
(format t "TWO:~a~%" (loop for x in '(1 2 3) collect x nconc (list (* x 10))))
;; Same shape inside a DEFUN, called repeatedly to churn the heap
(defun lb-two (xs) (loop for x in xs collect x nconc (list x)))
(let ((last nil))
  (dotimes (i 30) (setq last (lb-two (list i (+ i 100)))))
  (format t "DEFUN:~a~%" last))
EOF
out=$(run_stress "$WORK/loop-block.lisp")
check_contains "bare COLLECT loop compiles under GC stress"        "BARE:(1 2 3)" "$out"
check_contains "two-clause COLLECT/NCONC loop under GC stress"     "TWO:(1 10 2 20 3 30)" "$out"
check_contains "two-clause accum loop in DEFUN under GC stress"    "DEFUN:(29 29 129 129)" "$out"
check_absent   "no split ITEM/ACC gensym from stale BLOCK body"    "Unbound\|corrupted\|type 0\|not of type" "$out"

# --- Case: OPEN :external-format :latin-1 file round-trip under GC stress ----
# The latin-1 stream feature: a character file stream opened :external-format
# :latin-1 is 8-bit transparent (each code point 0..255 maps to one raw byte,
# no UTF-8).  The write/read paths themselves don't allocate, but OPEN parses
# the :external-format keyword (format_is_latin1 reads the keyword's symbol
# name) and the whole with-open-file form is compiled/evaluated here under
# forced-every-alloc compaction, so a stale keyword/stream offset on that path
# would surface.  We write the high byte 252 (ü) + 'A' through a latin-1 stream
# and read the file back as raw octets — must be exactly (252 65), NOT the
# 3-byte UTF-8 form a default character stream would have produced.
cat > "$WORK/latin1-open.lisp" <<'EOF'
(dotimes (i 8)
  (with-open-file (o "/tmp/clamiga_gcstress_latin1.tmp" :direction :output
                     :if-exists :supersede :external-format :latin-1)
    (write-char (code-char 252) o)
    (write-char #\A o)))
(let ((bytes (with-open-file (in "/tmp/clamiga_gcstress_latin1.tmp"
                                 :element-type '(unsigned-byte 8))
               (list (read-byte in) (read-byte in) (read-byte in nil :eof)))))
  (format t "LATIN1:~a~%" bytes))
EOF
out=$(run_stress "$WORK/latin1-open.lisp")
# NB: ~a (princ) prints the keyword :EOF without its colon -> EOF.
check_contains "latin-1 file stream byte-faithful under GC stress" "LATIN1:(252 65 EOF)" "$out"
check_absent   "no corruption on latin-1 OPEN keyword parse under stress" "Unbound\|corrupted\|type 0\|Undefined" "$out"

echo ""
echo "$passed passed, $failed failed, $total total"
[ "$failed" -eq 0 ]
