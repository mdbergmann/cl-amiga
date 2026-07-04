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

# --- Case 5b: DEFINE-CONDITION :default-initargs under GC stress -------------
# Bug: merge_default_initargs conses (initarg . value) pairs onto the slots
# alist while walking condition_default_initargs / the parent chain; an
# unprotected cursor or table head would leave a stale offset after the
# compaction those cons'es can trigger, dropping the merged default.
cat > "$WORK/di.lisp" <<'EOF'
(define-condition my-gcstress-di (simple-condition) ()
  (:default-initargs :format-control "merged default"))
(define-condition my-gcstress-di-sub (my-gcstress-di) ())
(let ((c (make-condition 'my-gcstress-di))
      (s (make-condition 'my-gcstress-di-sub)))
  (format t "DI:~a~%" (simple-condition-format-control c))
  (format t "DISUB:~a~%" (simple-condition-format-control s))
  (format t "DIERR:~a~%"
          (handler-case (error 'my-gcstress-di)
            (my-gcstress-di (e) (simple-condition-format-control e)))))
EOF
out=$(run_stress "$WORK/di.lisp")
check_contains "default-initargs merged via make-condition under stress" "DI:merged default" "$out"
check_contains "default-initargs inherited by subclass under stress"     "DISUB:merged default" "$out"
check_contains "default-initargs merged via error/signal under stress"   "DIERR:merged default" "$out"

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

# --- Case 9b: condition type that is ALSO a (&key) macro under GC stress -----
# Bug: scan_body_for_boxing (boxing analysis) and nlx_scan (block NLX analysis)
# had no handler-bind/handler-case case, so their general fall-through walked a
# handler clause (TYPE handler-fn) as a form and speculatively macroexpanded
# TYPE.  When TYPE is a condition name that is ALSO a (&key ...) macro (serapeum's
# DISPATCH-CASE-ERROR / fiveam SIGNALS shape), the expansion got one positional
# arg → "odd number of keyword arguments", aborting the compile.  Both scanners
# now skip the clause type spec; the new handler-bind cursor is GC-protected.
cat > "$WORK/cond_macro_type.lisp" <<'EOF'
(defmacro %gcs-dce (&key type datum) (declare (ignore type datum)) nil)
(define-condition %gcs-dce (error) ())
;; handler-case path (boxing scan walks the captured-BOX let)
(format t "HC-MAC:~a~%"
  (handler-case (error '%gcs-dce)
    (%gcs-dce (c) (declare (ignore c)) :caught)))
;; signals-style handler-bind inside block/return-from (NLX scan)
(format t "HB-MAC:~a~%"
  (block blk
    (handler-bind ((%gcs-dce (lambda (c) (declare (ignore c))
                               (return-from blk :caught))))
      (error '%gcs-dce))
    :fell-through))
EOF
out=$(run_stress "$WORK/cond_macro_type.lisp")
check_contains "handler-case macro-named cond type compiles under GC stress" "HC-MAC:CAUGHT" "$out"
check_contains "handler-bind macro-named cond type compiles under GC stress" "HB-MAC:CAUGHT" "$out"
check_absent   "no odd-keyword-args from speculative type macroexpand"       "odd number of keyword" "$out"

# --- Case 9c: handler-bind BODY scan: return-from in body, macro in handler expr -
# Bug: nlx_scan's SYM_HANDLER_BIND branch computed body via cl_cdr(rest) AFTER
# the while loop, but `rest` was set at the top of nlx_scan (a bare CL_Obj C
# local); the while loop calls nlx_scan recursively, macroexpanding handler
# expressions and potentially compacting — leaving `rest` stale.  When the
# return-from is in the HANDLER EXPRESSION (Case 9b shape), r=1 causes a break
# BEFORE cl_cdr(rest) is evaluated.  When return-from is in the BODY (not any
# handler expression), r stays 0 and the stale cl_cdr(rest) is dereferenced,
# walking garbage memory.
# Fix: pre-compute body=cl_cdr(rest) before CL_GC_PROTECT(clauses), add a
# second CL_GC_PROTECT(body), use body in nlx_scan_body(); CL_GC_UNPROTECT(2).
cat > "$WORK/hb-body-rf.lisp" <<'EOF'
(defmacro %hb-body-nop (&rest args) (declare (ignore args)) nil)
(let ((result
       (block done
         (handler-bind
           ;; Handler expression uses a user macro (macroexpand + compact under
           ;; GC stress) but does NOT contain return-from, so r=0 after the
           ;; while loop and nlx_scan_body(body,...) is called on the stale rest.
           ((error (lambda (c) (declare (ignore c)) (%hb-body-nop 1 2 3 4 5))))
           ;; return-from is in the BODY of handler-bind, not in any handler expr
           (return-from done :body-rf)))))
  (format t "HB-BODY:~a~%" result))
EOF
out=$(run_stress "$WORK/hb-body-rf.lisp")
check_contains "handler-bind body return-from (not in handler expr) under GC stress" "HB-BODY:BODY-RF" "$out"
check_absent   "no crash/corruption from stale rest in handler-bind body scan" "Unbound variable\|type 0\|corrupted\|not of type" "$out"

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

# --- Case 11b: ECASE/ETYPECASE fall-through TYPE-ERROR built under GC stress -
# The fall-through TYPE-ERROR for ECASE/ETYPECASE pre-scans the clause keys to
# build a (member ...)/(or ...) expected-type at compile time.  That cl_cons
# loop must keep the accumulator (keylist/typelist), clause cursor, and the
# resulting expected spec GC-protected across the main clause walk, or a
# compaction mid-compile leaves a stale spec / corrupt error.
cat > "$WORK/ecase_err.lisp" <<'EOF'
(format t "ECASE-TE:~a~%"
  (handler-case (ecase 5 (1 'a) (2 'b) (3 'c))
    (type-error (e) (list (type-error-datum e) (type-error-expected-type e)))
    (error () 'wrong-class)))
(format t "ETYPECASE-TE:~a~%"
  (handler-case (etypecase 5 (string 's) (symbol 'y))
    (type-error () 'te)
    (error () 'wrong-class)))
EOF
out=$(run_stress "$WORK/ecase_err.lisp")
check_contains "ecase fall-through type-error under GC stress" "ECASE-TE:(5 (MEMBER" "$out"
check_contains "etypecase fall-through type-error under GC stress" "ETYPECASE-TE:TE" "$out"
check_absent   "no wrong condition class from stale ecase spec" "wrong-class\|WRONG-CLASS" "$out"

# --- Case 11b-match: ECASE/ETYPECASE matching key compiled under GC stress ---
# Bug (HIGH): compile_case/compile_typecase did not protect 'clauses' before
# the pre-scan cl_cons loop; a compaction during that loop staled 'clauses' so
# that the subsequent CL_GC_PROTECT at the main-walk start registered the stale
# offset.  Under gc-stress, the matching path produced corrupt bytecode (stale
# key constants) so a matching ecase returned wrong results.
# Fix: CL_GC_PROTECT(clauses) moved to before the pre-scan block.
cat > "$WORK/ecase_match.lisp" <<'EOF'
(format t "ECASE-M:~a~%" (ecase 1 (1 'a) (2 'b) (3 'c)))
(format t "ECASE-M2:~a~%" (ecase 2 ((1 2) :low) ((3 4) :high)))
(format t "ETYPECASE-M:~a~%"
  (etypecase "hello" (string :str) (integer :int)))
(format t "ETYPECASE-M2:~a~%"
  (etypecase 42 (string :str) ((integer 0 100) :small-int)))
EOF
out=$(run_stress "$WORK/ecase_match.lisp")
check_contains "ecase matching key correct under GC stress"           "ECASE-M:A"       "$out"
check_contains "ecase multi-key match correct under GC stress"        "ECASE-M2:LOW"    "$out"
check_contains "etypecase string match correct under GC stress"       "ETYPECASE-M:STR" "$out"
check_contains "etypecase compound type match correct under GC stress" "ETYPECASE-M2:SMALL-INT" "$out"
check_absent   "no error from matching ecase/etypecase under stress"  "Error\|Unbound"  "$out"

# --- Case 11c: COERCE to (vector/array bit ...) under GC stress --------------
# (coerce list '(vector bit N)) must allocate and return a real bit-vector even
# when a compaction fires inside the bit-vector allocation path.
cat > "$WORK/coerce_bit.lisp" <<'EOF'
(dotimes (i 4)
  (let ((bv (coerce (list 1 0 1 1 1 1 0 0) '(vector bit 8))))
    (format t "COERCE-BIT:~a:~a~%" (bit-vector-p bv) bv)))
EOF
out=$(run_stress "$WORK/coerce_bit.lisp")
# Pattern is a regex; "#*" would be a metachar, so assert the bit-vector-p=T
# result (exact value #*10111100 is covered by the host/Amiga unit tests).
check_contains "coerce to (vector bit N) yields bit-vector under GC stress" "COERCE-BIT:T:" "$out"

# --- Case 11d: (defun (setf accessor)) hidden symbol under GC stress ---------
# compile_defun synthesizes the package-qualified %SETF-<pkg>::<name> storage
# symbol via cl_setf_store_symbol (an interning, allocating call) and conses
# it into setf_fn_table.  The accessor local must stay protected across that,
# and two same-named accessors in different packages must register distinct
# storage symbols.
cat > "$WORK/setf_pkg.lisp" <<'EOF'
(defpackage :gcs-a (:use :cl))
(defpackage :gcs-b (:use :cl))
(defun (setf gcs-a::acc) (v x) (setf (car x) (list :a v)) v)
(defun (setf gcs-b::acc) (v x) (setf (car x) (list :b v)) v)
(let ((c1 (list 0)) (c2 (list 0)))
  (funcall #'(setf gcs-a::acc) 11 c1)
  (funcall #'(setf gcs-b::acc) 22 c2)
  (format t "SETF-PKG:~a~%" (list (car c1) (car c2))))
EOF
out=$(run_stress "$WORK/setf_pkg.lisp")
# ~A (princ) prints keywords without the leading colon, so :A/:B render as A/B.
check_contains "(setf accessor) distinct per package under GC stress" "SETF-PKG:((A 11) (B 22))" "$out"

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

# --- Case 19b: custom generic-function metaclass under GC stress -------------
# :generic-function-class registers the custom type symbol in the C-side
# cl_gf_type_syms[] root set and creates the GF via %make-struct of that type
# + an initialize-instance call (the snooze defroute self-registration shape).
# Under stress the registered type symbol and the GF struct relocate; if the
# new root slots or the funcallable check were not GC-safe, the GF would stop
# being recognized as funcallable ("Not a function: heap object type") or its
# type_desc would go stale (wrong TYPE-OF / dispatch miss).
cat > "$WORK/clos-gfclass.lisp" <<'EOF'
(defclass gcs-rgf (cl:standard-generic-function) ()
  (:metaclass funcallable-standard-class))
(defvar *gcs-rgf-reg* (make-hash-table))
(defmethod initialize-instance :after ((gf gcs-rgf) &rest args)
  (declare (ignore args))
  (setf (gethash (generic-function-name gf) *gcs-rgf-reg*) gf))
;; Define several custom-class GFs so a compaction lands across the
;; %make-struct / initialize-instance / first-call sequence.
(dotimes (i 8)
  (let ((name (intern (format nil "GCS-ROUTE-~a" i))))
    (eval `(defgeneric ,name (x) (:generic-function-class gcs-rgf)
             (:method ((x integer)) (+ x ,i))))))
(let ((gf (symbol-function 'gcs-route-5)))
  (format t "GFCLASS:~a/~a/~a/~a~%"
          (type-of gf) (functionp gf)
          (funcall gf 100)
          (and (eq (gethash 'gcs-route-5 *gcs-rgf-reg*) gf) t)))
EOF
out=$(run_stress "$WORK/clos-gfclass.lisp")
check_contains "custom gf-class type/functionp/call/register under GC stress" "GFCLASS:GCS-RGF/T/105/T" "$out"
check_absent   "no stale funcallable-instance for custom gf-class under stress" "Not a function: heap object type" "$out"

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

# --- Case: macrolet/symbol-macrolet setf boxing under GC stress ---------------
# scan_body_for_boxing must install a MACROLET's expanders into the compiler env
# (allocates: cl_compile + cl_cons) and expand them while scanning a FLET/LABELS
# body, so a `(setf outer-var ...)` hidden inside the macro is seen and the var
# is boxed.  The SYMBOL-MACROLET place path conses up a `(setf <expansion> val)`
# form.  Both run during the boxing pre-scan, so compile this under stress to
# force a compacting GC through those allocating paths.  Without the fix the
# closure's write is dropped (the var keeps its init value); the local-time
# parse-timestring shape that exposed this then yields NIL month.
cat > "$WORK/mlet-box.lisp" <<'EOF'
(in-package :cl-user)
;; macrolet-defined SETM hides a (setf m ...) of the enclosing M inside the
;; LABELS closure S; the boxing pre-scan must expand SETM to see the write.
(defun mlet-box-run ()
  (let ((m 0))
    (macrolet ((setm (x) `(setf m ,x)))
      (labels ((s () (setm 99)))
        (s)
        m))))
;; macrolet nested *inside* the LABELS function body
(defun mlet-box-run2 ()
  (let ((m 0))
    (labels ((s () (macrolet ((setm (x) `(setf m ,x))) (setm 7))))
      (s)
      m)))
;; symbol-macrolet analogue: setf through the symbol-macro Q (-> M)
(defun smlet-box-run ()
  (let ((m 0))
    (labels ((s () (symbol-macrolet ((q m)) (setf q 42))))
      (s)
      m)))
;; In-place macrolet (no FLET/LABELS) whose expansion contains BOTH a
;; (setf place v) and a later reference to a *global* macro stub (SETF/PROGN
;; are macro-functions for MACROEXPAND conformance).  Compiling the inner
;; (setf y v) re-enters the global-macro check, whose cl_build_lex_env conses
;; a NON-EMPTY lexical env (the enclosing macrolet binding) -> a compaction.
;; compile_expr_step's unprotected local `expr`/`head` then went stale and the
;; setf was dispatched with the relocated, aliased enclosing PROGN form,
;; yielding "%SETF-SETF" / the wrong (hi) value.  Must read 2026, not 9999.
(defun mlet-into-run ()
  (let ((y 0))
    (macrolet ((into (place v lo hi)
                 `(progn (setf ,place ,v)
                         (unless (<= ,lo ,place ,hi) (error "range"))
                         (values))))
      (into y 2026 1 9999))
    y))
EOF
cat > "$WORK/mlet-box-run.lisp" <<EOF
(load "$WORK/mlet-box.lisp")
(format t "MLET:~a~%" (mlet-box-run))
(format t "MLET2:~a~%" (mlet-box-run2))
(format t "SMLET:~a~%" (smlet-box-run))
(format t "MINTO:~a~%" (mlet-into-run))
EOF
out=$(run_stress "$WORK/mlet-box-run.lisp")
check_contains "macrolet setf boxing in labels under stress" "MLET:99" "$out"
check_contains "macrolet-in-labels setf boxing under stress" "MLET2:7" "$out"
check_contains "symbol-macrolet setf boxing in labels under stress" "SMLET:42" "$out"
check_contains "in-place macrolet setf+stub keeps live expr under stress" "MINTO:2026" "$out"
check_absent   "no %SETF-SETF from stale expr in macrolet body"           "%SETF-SETF" "$out"

# --- Case: macrolet &environment (non-first) inside a binding form ------------
# cl_macrolet_install_expanders runs TWICE on the same shared lambda-list: the
# boxing pre-scan installs the expanders to see through macrolet calls, then the
# real compile_macrolet installs them again.  The &environment strip must be
# NON-DESTRUCTIVE — the old code spliced &environment out by mutating a prior
# cons's cdr, so the 2nd install saw no &environment and never re-bound the env
# var, making the expander body "Unbound variable: ENV".  This is ironclad
# sha3.lisp's STATE-AREF shape (&environment last, used inside a LET).  The
# rebuild conses a fresh lambda-list, so exercise it under compaction.
cat > "$WORK/mlet-env.lisp" <<'EOF'
(in-package :cl-user)
(defun mlet-env-run ()
  (let ((v (make-array 3 :initial-element 0)))
    (macrolet ((setit (i val &environment env)
                 `(setf (aref v ,(macroexpand i env)) ,val)))
      (symbol-macrolet ((idx 1))
        (setit idx 99)
        (aref v 1)))))
EOF
cat > "$WORK/mlet-env-run.lisp" <<EOF
(load "$WORK/mlet-env.lisp")
(format t "MLETENV:~a~%" (mlet-env-run))
EOF
out=$(run_stress "$WORK/mlet-env-run.lisp")
check_contains "macrolet non-first &environment in binding form under stress" "MLETENV:99" "$out"
check_absent   "no unbound ENV from destructive &environment strip"           "Unbound variable: ENV" "$out"

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

# --- Case: specialized element type preserved through seq fns under GC stress -
# copy_array_seq / bi_subseq allocate a fresh result vector (which can compact)
# and then copy the source data and element-type code across.  Exercise the
# element-type propagation under stress so a stale source/result CL_Vector*
# (wrong elt_type byte) would surface as a mismatched ARRAY-ELEMENT-TYPE.
cat > "$WORK/seq-elt-type.lisp" <<'EOF'
(dotimes (i 30)
  (let* ((v (make-array 6 :element-type 'single-float
                          :initial-contents '(1.0 2.0 3.0 4.0 5.0 6.0)))
         (c (copy-seq v))
         (s (subseq v 1 5))
         (m (make-sequence '(vector single-float) 4 :initial-element 1.0)))
    (declare (ignore i))
    (format t "SEQ-ELT:~a ~a ~a ~a~%"
            (array-element-type c) (array-element-type s)
            (array-element-type m) (equalp v c))))
EOF
out=$(run_stress "$WORK/seq-elt-type.lisp")
check_contains "copy-seq/subseq/make-sequence preserve SINGLE-FLOAT under stress" \
               "SEQ-ELT:SINGLE-FLOAT SINGLE-FLOAT SINGLE-FLOAT T" "$out"
check_absent   "no element-type corruption from seq fns under stress" \
               "Unbound variable\|type 0\|corrupted\|Undefined" "$out"

# --- Case: CLAMIGA::%TYPE-EXPANDER-driven TYPEXPAND under GC stress ----------
# %type-expander returns the deftype expander closure; a portable typexpand
# applies it to the compound type's args.  That apply runs user code that
# conses the expansion, so a compaction can land mid-expand.  Exercise the
# atom-alias (0-arg) and parameterized (N-arg) apply paths in a loop, plus the
# fully-resolving fixpoint loop, to surface any stale-CL_Obj bug.  This is the
# mechanism serapeum's EXPLODE-TYPE relies on (introspect-environment shim).
cat > "$WORK/type-expander.lisp" <<'EOF'
(defun tx1 (type)
  (multiple-value-bind (head args)
      (if (consp type) (values (car type) (cdr type)) (values type nil))
    (if (symbolp head)
        (let ((ex (clamiga::%type-expander head)))
          (if ex (values (apply ex args) t) (values type nil)))
        (values type nil))))
(defun tx (type)
  (loop (multiple-value-bind (new exp) (tx1 type)
          (if exp (setf type new) (return type)))))
(deftype tx-disj () '(or string number (member :x :y)))
(deftype tx-range (lo hi) `(integer ,lo ,hi))
(let ((a nil) (b nil) (c nil))
  (dotimes (i 30)
    (setq a (tx 'tx-disj))
    (setq b (funcall (clamiga::%type-expander 'tx-range) i (+ i 1)))
    (setq c (clamiga::%type-expander 'list)))
  (format t "TX-DISJ:~a~%" a)
  (format t "TX-RANGE:~a~%" b)
  (format t "TX-BUILTIN:~a~%" c))
EOF
out=$(run_stress "$WORK/type-expander.lisp")
# NB: ~a (princ) drops the keyword colon, so (MEMBER :X :Y) prints as (MEMBER X Y);
# the dotimes loop ends at i=29, so the last tx-range apply is (29 30).
check_contains "%type-expander atom alias fully expands under stress"  "TX-DISJ:(OR STRING NUMBER (MEMBER X Y))" "$out"
check_contains "%type-expander parameterized alias applies under stress" "TX-RANGE:(INTEGER 29 30)" "$out"
check_contains "%type-expander returns NIL for a built-in type name"   "TX-BUILTIN:NIL" "$out"
check_absent   "no corruption from %type-expander apply under stress"  "Unbound variable\|type 0\|corrupted\|Undefined" "$out"

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

# --- Case: DESTRUCTURING-BIND arity guards under GC stress -------------------
# Regression for the arity-strictness fix: compile_destructure_pattern now emits
# `(error "...")` guards for too-few/too-many elements.  Building that error form
# (cl_make_string + cl_cons) allocates at compile time, so the `pattern` cursor
# must stay GC-protected across it.  Compile + run the guarded forms under stress
# and confirm the error fires cleanly (no stale-offset corruption / wrong result).
cat > "$WORK/db-arity.lisp" <<'EOF'
(defun arity-probe ()
  (let ((few  (handler-case (destructuring-bind (a b) '(1) (list a b))
                (error () :few)))
        (many (handler-case (destructuring-bind (a b) '(1 2 3) (list a b))
                (error () :many)))
        (nest (handler-case (destructuring-bind (a (b c)) '(1 (2)) (list a b c))
                (error () :nest)))
        (ok   (destructuring-bind (a b) '(1 2) (list a b))))
    (format nil "~a/~a/~a/~a" few many nest ok)))
(let ((last nil))
  (dotimes (i 30) (setq last (arity-probe)))
  (format t "DBARITY:~a~%" last))
EOF
out=$(run_stress "$WORK/db-arity.lisp")
check_contains "destructuring-bind arity guards fire correctly under stress" "DBARITY:FEW/MANY/NEST/(1 2)" "$out"
check_absent   "no corruption from stale pattern cursor in arity guards"     "Unbound variable\|type 0\|corrupted\|Undefined" "$out"

# --- Case: defstruct with MULTIPLE (:constructor ...) options under GC stress -
# Regression for the defstruct fix: every (:constructor ...) option must be
# emitted (was: only the last survived).  Compile a struct with two BOA
# constructors over (:include)d slots (esrap's failed-parse shape) to a clean
# FASL, then exercise both constructors under stress — the whole defstruct
# expansion (ctor-specs list, slot-init mapcar, %make-struct) allocates.
cat > "$WORK/multi-ctor.lisp" <<'EOF'
(defstruct (mcb (:constructor nil)) (e nil) (p 0) (d nil))
(defstruct (mcl (:include mcb)
            (:constructor mcl-full (e p d))
            (:constructor mcl/no-pos (e d))))
(defun ctor-probe ()
  (let ((a (mcl-full 'x 7 'z))
        (b (mcl/no-pos 'q 'w)))
    (format nil "~a/~a/~a/~a"
            (mcb-e a) (mcb-p a) (mcb-p b) (mcb-d b))))
(let ((last nil))
  (dotimes (i 30) (setq last (ctor-probe)))
  (format t "MULTICTOR:~a~%" last))
EOF
compile_fasl "$WORK/multi-ctor.lisp" "$WORK/multi-ctor.fasl"
cat > "$WORK/multi-ctor-load.lisp" <<EOF
(load "$WORK/multi-ctor.fasl")
EOF
out=$(run_stress "$WORK/multi-ctor-load.lisp")
check_contains "defstruct emits all (:constructor) options under stress" "MULTICTOR:X/7/0/W" "$out"
check_absent   "no dropped-constructor/corruption in multi-ctor defstruct" "Undefined\|Unbound variable\|type 0\|corrupted" "$out"

# --- Case: define-compiler-macro with &environment under GC stress -----------
# Regression for the boot.lisp fix: define-compiler-macro must strip
# &environment from its lambda list before building the inner
# destructuring-bind.  The expansion (%dcm-split-env / %dcm-split-whole +
# nested list building) allocates; under stress a stale cursor would either
# corrupt the cleaned lambda list or re-leak &environment as a required param
# (-> spurious "too few elements" while compiling the define-compiler-macro).
# Loaded as SOURCE so the expansion+compile happens under stress; the
# subsequent EVAL fires the now-registered compiler macro (folds 21 -> 42).
cat > "$WORK/dcm-env.lisp" <<'EOF'
(defun dcm-fn (x) x)
(define-compiler-macro dcm-fn (&whole form x &environment env)
  (declare (ignorable env)) (if (integerp x) (* 2 x) form))
(defun dcm2-fn (a b) (list a b))
(define-compiler-macro dcm2-fn (a b &environment env)
  (declare (ignorable env)) (list 'list b a))
(format t "DCMENV:~a~%" (eval '(dcm-fn 21)))
(format t "DCMENV2:~a~%" (eval '(dcm2-fn 1 2)))
EOF
out=$(run_stress "$WORK/dcm-env.lisp")
check_contains "define-compiler-macro strips &environment (folds) under stress" "DCMENV:42" "$out"
check_contains "define-compiler-macro &environment without &whole under stress" "DCMENV2:(2 1)" "$out"
check_absent   "no too-few/corruption from &environment in compiler-macro" "too few\|Unbound variable\|type 0\|corrupted\|Undefined" "$out"

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

# --- Case: sequence ops returning a STRING result for string-vector inputs ---
# REVERSE / SUBSTITUTE / REMOVE-IF / COPY-SEQ on an adjustable character vector
# allocate a fresh STRING result (make_seq_result_like / copy_array_seq) while the
# source string-vector must stay GC-rooted across the allocation.  A stale source
# offset would corrupt the copied characters under compaction.
cat > "$WORK/strvec-seq.lisp" <<'EOF'
(let ((bad nil))
  (dotimes (i 40)
    (let ((s (make-array 5 :element-type 'character
                         :initial-contents "abcab" :adjustable t)))
      (unless (and (stringp (reverse s)) (string= (reverse s) "bacba")
                   (stringp (substitute #\x #\a s)) (string= (substitute #\x #\a s) "xbcxb")
                   (stringp (remove-if #'alpha-char-p s))
                   (simple-string-p (copy-seq s)) (string= (copy-seq s) "abcab"))
        (setq bad :strvec-seq))
      (when bad (return))))
  (if bad (format t "STRVECSEQ-BAD:~s~%" bad) (format t "STRVECSEQ-OK~%")))
EOF
out=$(run_stress "$WORK/strvec-seq.lisp")
check_contains "string-vector sequence ops return strings under GC stress" "STRVECSEQ-OK" "$out"
check_absent   "no corrupted string-vector sequence result"                "STRVECSEQ-BAD" "$out"

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

# --- Case: DEFTYPE/DEFVAR/DEFPARAMETER name symbol survives compile ----------
# Regression for the compile_deftype (and sibling compile_defvar/defparameter/
# defconstant) GC-safety fix.  These read the definition NAME as a bare C local
# out of `form`, then call compile_expr to compile the expander lambda / init
# form — heavy allocation.  cl_compile_env does NOT re-protect the form it
# forwards, so an unprotected `name` went stale across that compaction: the
# symbol relocated but `name` kept the OLD offset (an orphan ~1 object off), and
# cl_add_constant baked that stale offset into the constant pool.  OP_DEFTYPE
# then registered a NON-canonical (garbage-named) symbol in type_table, so
# cl_get_type_expander's `cl_car(pair) == name` identity check never matched the
# interned symbol and SUBTYPEP/UPGRADED-ARRAY-ELEMENT-TYPE silently returned
# NIL/T for the deftype.  Only the SECOND+ definition in a file corrupts (heap
# pressure must build), so several are defined; loaded from SOURCE under stress
# (the read-compile-run path is where the corruption fires — a clean FASL load
# deserializes symbols correctly and would mask it).  See the
# [deftype-symbol-dup-gcstress] memory note (root cause was this, not interning).
cat > "$WORK/deftype-defs.lisp" <<'EOF'
(defvar *gcs-dv* (list 10 20 30))
(defparameter *gcs-dp* (+ 40 2))
(defconstant +gcs-dc+ (* 6 7))
(deftype gcs-ty1 () 'character)
(deftype gcs-ty2 () '(unsigned-byte 8))
(deftype gcs-ty3 () 'integer)
(format t "DT:~a:~a:~a~%"
  (and (subtypep 'gcs-ty1 'character) t)
  (and (subtypep 'gcs-ty2 'integer) t)
  (and (subtypep 'gcs-ty3 'integer) t))
(format t "UAET:~a~%" (upgraded-array-element-type 'gcs-ty1))
;; defvar/defparameter/defconstant: special flag + value land on the canonical
;; symbol (a stale OP_DEFVAR/OP_GSTORE name would mark/store the wrong symbol).
(format t "DV:~a:~a:~a~%" *gcs-dv* *gcs-dp* +gcs-dc+)
(format t "DVSPEC:~a~%" (let ((*gcs-dv* :rebound)) *gcs-dv*))
EOF
out=$(run_stress "$WORK/deftype-defs.lisp")
check_contains "deftype name symbol survives compile under GC stress (subtypep)" "DT:T:T:T" "$out"
check_contains "deftype upgraded-array-element-type correct under GC stress"     "UAET:CHARACTER" "$out"
check_contains "defvar/defparameter/defconstant values correct under GC stress"  "DV:(10 20 30):42:42" "$out"
check_contains "defvar special binding works under GC stress"                    "DVSPEC:REBOUND" "$out"
check_absent   "no stale/garbage deftype name (type_table lookup miss)"          "Unbound\|corrupted\|type 0\|Undefined" "$out"

# --- Case: socket read timeout + accessor under GC stress -------------------
# Exercises the new allocating paths added with EXT:SOCKET-STREAM-TIMEOUT:
#  - the accessor returns the timeout as a freshly-consed double-float;
#  - a timed-out READ-CHAR builds an EXT:SOCKET-TIMEOUT condition
#    (cl_make_string + cons for the report) and formats it with ~A.
# A stale CL_Obj on any of those paths would surface here as a wrong value,
# a wrong condition type, or a corrupted report string under forced compaction.
# 0.5s is exactly representable, so the numeric = round-trip is float-safe.
cat > "$WORK/sockto.lisp" <<'EOF'
(let* ((l (ext:socket-listen 0 t))
       (p (ext:socket-local-port l))
       (c (ext:open-tcp-stream "127.0.0.1" p)))
  (setf (ext:socket-stream-timeout c :input) 0.5)
  (format t "STO-RT:~a~%" (= 0.5 (ext:socket-stream-timeout c :input)))
  (format t "STO:~a~%"
    (handler-case (progn (read-char c) :no-timeout)
      (ext:socket-timeout (e)
        (if (and (typep e 'stream-error) (stringp (format nil "~a" e)))
            :timeout-ok :timeout-bad))))
  (close c)
  (close l))
EOF
out=$(run_stress "$WORK/sockto.lisp")
check_contains "socket-stream-timeout accessor double-float round-trips under stress" "STO-RT:T" "$out"
check_contains "timed-out read-char signals EXT:SOCKET-TIMEOUT under stress"          "STO:TIMEOUT-OK" "$out"
check_absent   "no corruption from stale CL_Obj in timeout condition path"            "TIMEOUT-BAD\|corrupted\|type 0\|Unbound\|Undefined" "$out"

# NOTE: there is no write-timeout GC-stress case — a write timeout cannot be
# reliably triggered over host loopback (macOS buffers it effectively without
# bound, so the write never blocks).  The read-timeout case above already
# exercises the EXT:SOCKET-TIMEOUT condition-build/format allocation path under
# forced compaction; the write path builds the identical condition.

# --- Case: global symbol-macro (DEFINE-SYMBOL-MACRO) lookup under stress -----
# Bug: cl_lookup_global_symbol_macro_p cached the %SYMBOL-MACRO-EXPANSION plist
# indicator in an UNREGISTERED `static CL_Obj`.  Compaction relocated the
# interned indicator symbol, but the stale cached offset no longer matched the
# (forwarded) indicator stored on a symbol's plist, so every global
# symbol-macro lookup spuriously missed.  A DEFINE-SYMBOL-MACRO compiled just
# before a use stopped expanding — the use compiled as a plain free variable
# and errored "Unbound variable" at load/run (hunchentoot/drakma specials.lisp
# *SUPPORTS-THREADS-P*).  Fix: cl_gc_register_root(&indicator).
# NOTE: this runs the *source* directly under stress (like the handler-case /
# let cases) so the symbol-macro use is COMPILED while compaction is forced —
# pre-compiling a clean FASL would bake in the (correct) expansion and miss the
# compile-time lookup entirely.
cat > "$WORK/symmac.lisp" <<'EOF'
(defpackage :gcstress-sm-target (:use :cl) (:export #:*backing*))
(in-package :gcstress-sm-target)
(defvar *backing* :expanded-ok)
(defpackage :gcstress-sm (:use :cl) (:export #:probe))
(in-package :gcstress-sm)
(eval-when (:compile-toplevel :load-toplevel :execute)
  (define-symbol-macro *the-macro* gcstress-sm-target:*backing*))
(defvar *captured* (load-time-value (and *the-macro* :have-it)))
(defun probe () (list *the-macro* *captured*))
(format t "SYMMAC:~a~%" (probe))
EOF
out=$(run_stress "$WORK/symmac.lisp")
check_contains "global symbol-macro expands (compiled under stress)" "SYMMAC:(EXPANDED-OK HAVE-IT)" "$out"
check_absent   "no unbound-var from stale symbol-macro indicator"    "Unbound variable" "$out"

# --- Case: IEEE float-bits builtins under stress ---------------------------
# clamiga:double-float-bits returns a 4-limb bignum (allocates) and
# bits-double-float rebuilds the double; compaction before every allocation
# would expose any stale CL_Obj in the conversion helpers.  These back
# float-features' :cl-amiga branch (jzon float serialisation).
cat > "$WORK/fbits.lisp" <<'EOF'
(let ((ok t))
  (dotimes (i 60)
    (let ((b (clamiga:double-float-bits 25.5d0)))
      (unless (and (= b 4627870829588250624)
                   (= 25.5d0 (clamiga:bits-double-float b)))
        (setf ok nil)))
    (unless (eql -2.5 (clamiga:bits-single-float (clamiga:single-float-bits -2.5)))
      (setf ok nil)))
  (format t "FBITS:~a~%" (if ok "ok" "CORRUPT")))
EOF
out=$(run_stress "$WORK/fbits.lisp")
check_contains "IEEE float-bits round-trip exact under GC stress" "FBITS:ok" "$out"
check_absent   "no corruption/error in float-bits under stress"   "CORRUPT\|not an integer\|not a float" "$out"

# --- Case: large quasiquote template (chunked APPEND) under stress ---------
# A quasiquote with > 255 top-level elements expands to a single APPEND call,
# which qq_append (compiler_extra.c) splits into nested chunks of <= 128 args
# so the OP_CALL byte count can't overflow.  The chunk builder conses a fresh
# group list per chunk plus the wrapping APPEND forms — many allocations during
# COMPILE; under GC stress an unprotected CL_Obj there would stale and corrupt
# the generated code.  Mirrors ironclad threefish.lisp's 1024-bit ARX round
# macro (setf template ~289 elements).  The source is generated with 300
# (literal . unquote) pairs = 600 top-level elements.
{
    printf '(defun qqbig ()\n  (let ((x 7))\n    `('
    i=0
    while [ "$i" -lt 300 ]; do printf 'k%d ,x ' "$i"; i=$((i + 1)); done
    printf ')))\n'
    printf '(let ((r (qqbig)))\n  (format t "QQBIG:~a:~a:~a~%%" (length r) (nth 257 r) (nth 399 r)))\n'
} > "$WORK/qqbig.lisp"
out=$(run_stress "$WORK/qqbig.lisp")
# 600 elements; odd indices are the unquoted x (=7), at/after a 128-arg chunk
# boundary -> chunking must preserve both order and the unquoted value.
check_contains "large quasiquote (chunked APPEND) compiles+runs under GC stress" "QQBIG:600:7:7" "$out"
check_absent   "no bad-callee corruption from chunked quasiquote under stress"   "Not a function\|heap object type" "$out"

# --- Case: compile_call nargs>255 error reports correct function name under GC stress ---
# Bug: func in compile_call is an unprotected CL_Obj; compile_expr calls in the
# argument loop can compact the heap, making func (and its name string) stale.
# The nargs>255 error path then dereferences a relocated arena address for the
# function name, producing a garbled name or a crash under stress.
# Fix: snapshot func's name into a stack char buffer before the argument loop so
# the error message uses stable stack data regardless of subsequent compaction.
#
# Note: this is necessarily a *compile-time* diagnostic, not a runtime
# condition.  OP_CALL/OP_TAILCALL encode the argument count in a single byte,
# so a >255-arg call cannot be represented in bytecode at all -- there is no
# runtime call to signal a program-error from.  The compiler therefore rejects
# the form while compiling it, which happens before any enclosing HANDLER-CASE
# runtime handler is installed (the whole top-level form is compiled, then
# run).  So we check the top-level error output directly rather than wrapping
# in HANDLER-CASE -- the regression we guard against is a garbled/stale
# function NAME in that message under compaction, which this still exercises.
{
    printf '(defun nargs255-target (&rest args) (length args))\n'
    printf '(nargs255-target '
    i=0
    while [ "$i" -lt 260 ]; do printf '(make-list 1) '; i=$((i + 1)); done
    printf ')\n'
} > "$WORK/nargs255.lisp"
out=$(run_stress "$WORK/nargs255.lisp")
check_contains "nargs>255 error names correct function under GC stress" "Too many arguments in call to NARGS255-TARGET (260, max 255)" "$out"
check_absent   "no crash or garbage name in nargs>255 error path"        "type 0\|corrupted\|Undefined\|Unbound" "$out"

# --- Case: scanner registers symbol-macrolet bindings before speculative
#     macroexpansion (ironclad sha3.lisp:65 "Unbound variable: X") ---
# The closure/NLX pre-scanners (scan_body_for_boxing, nlx_scan) register an
# enclosing symbol-macrolet's bindings into the active compiler env before
# speculatively expanding an &environment-aware global macro, so the expander
# can resolve the symbol-macro via (macroexpand x env).  Those bindings are
# stored as CL_Obj offsets in the platform-alloc'd compiler env; under stress
# every macroexpand-driven allocation compacts the heap, so the env's
# symbol_macros[] offsets must be GC-forwarded (cl_compiler_gc_update_thread).
# A stale offset there would corrupt the expansion or reintroduce the
# "Unbound variable: X" failure mid-scan.  The function is compiled inside a
# HANDLER-CASE (as ASDF does), so any scan-time error escapes and yields -1.
cat > "$WORK/smm.lisp" <<'EOF'
(defun smm-tmexpand (form env)
  (let ((real (macroexpand form env)))
    (if (atom real) real
        (cons (car real)
              (mapcar (lambda (x) (smm-tmexpand x env)) (cdr real))))))
(defmacro smm-unrolled ((var limit) &body body &environment env)
  (loop for i from 0 below (eval (smm-tmexpand limit env))
        collect (list 'symbol-macrolet (list (list var i))
                      (cons 'progn body)) into forms
        finally (return (cons 'progn forms))))
(defparameter *smm-offs*
  (make-array '(2 2) :initial-contents '((10 11) (12 13))))
(defmacro smm-get (x y &environment env)
  (aref *smm-offs* (eval (smm-tmexpand x env)) (eval (smm-tmexpand y env))))
(format t "SMM:~a~%"
  (handler-case
      (progn
        (eval '(defun smm-keccak (acc)
                 (smm-unrolled (x 2)
                   (smm-unrolled (y 2)
                     (setf acc (+ acc (smm-get x y)))))))
        (smm-keccak 0))
    (error (e) (declare (ignore e)) -1)))
EOF
out=$(run_stress "$WORK/smm.lisp")
check_contains "symbol-macrolet bindings survive scanner expansion under GC stress" "SMM:46" "$out"
check_absent   "no Unbound-variable from scanner expansion under GC stress" "Unbound variable" "$out"

# --- Case: CONCATENATE with a compound result-type specifier --------------
# ironclad's ED448-DOM uses (concatenate '(simple-array (unsigned-byte 8) (*))
# ...).  Normalizing the compound type-spec expands deftypes (cl_vm_apply) and
# interns STRING/VECTOR before allocating the result — exercise that the
# result-type local and the freshly-built vector survive compaction.
cat > "$WORK/cat.lisp" <<'EOF'
(format t "CAT:~a~%"
  (handler-case
      (let ((v (concatenate '(simple-array (unsigned-byte 8) (*))
                            (map 'vector #'char-code "AB")
                            (vector 0) (vector 2))))
        (format nil "~a:~a:~a:~a" (aref v 0) (aref v 1) (aref v 2) (aref v 3)))
    (error (e) (declare (ignore e)) -1)))
(format t "CATS:~a~%"
  (handler-case (concatenate '(vector character) "ab" "cd")
    (error (e) (declare (ignore e)) -1)))
EOF
out=$(run_stress "$WORK/cat.lisp")
check_contains "concatenate compound byte-array result survives GC stress" "CAT:65:66:0:2" "$out"
check_contains "concatenate compound char-array result survives GC stress" "CATS:abcd" "$out"

# --- Case: continuable CCASE STORE-VALUE retry under GC stress ---------------
# Bug: scan_body_for_boxing did not descend RESTART-CASE clause bodies at
# closure depth, so a (setf place ...) inside a STORE-VALUE handler — as CCASE/
# CTYPECASE require — left the variable unboxed; the handler updated a private
# copy and the loop re-read the stale value, retrying forever.
# Fix: scan restart-case clause bodies at closure_depth + 1 so the mutated var
# is boxed and shared with the loop's re-test.  Exercise compile under stress.
cat > "$WORK/ccase.lisp" <<'EOF'
(format t "CCASE:~a~%"
  (let ((x 'zzz))
    (handler-bind ((type-error (lambda (c) (declare (ignore c))
                                 (store-value 'b))))
      (ccase x (a 'A) (b 'B) (c 'C)))))
(format t "CTYPE:~a~%"
  (let ((x 'sym))
    (handler-bind ((type-error (lambda (c) (declare (ignore c))
                                 (store-value 42))))
      (ctypecase x (string 's) (integer 'i)))))
EOF
out=$(run_stress "$WORK/ccase.lisp")
check_contains "ccase store-value retry survives GC stress"      "CCASE:B" "$out"
check_contains "ctypecase store-value retry survives GC stress"  "CTYPE:I" "$out"
check_absent   "no infinite retry / stale-box from ccase under stress" "RUNAWAY\|Unbound" "$out"

# --- Case: macro-expanding-to-RESTART-CASE compiled inside a LET under stress -
# Two compile-time GC bugs (surfaced by continuable CCASE/CTYPECASE, which are
# macros expanding to a LET-over-RESTART-CASE):
#  1. compile_restart_case read main_form/clauses before protecting them, so the
#     catch-tag cl_cons / cl_emit_const between read and protect could compact
#     and stale them — fatal when restart-case arrives via a (fresh-arena) macro
#     expansion.
#  2. scan_body_for_boxing's LET/CASE/FLET/etc. handlers walked their body with
#     an unprotected cursor across the recursive scan (which macroexpands and
#     compacts), so cl_cdr followed a stale offset ("CDR: argument is not of
#     type LIST" at compile time).
# Both fire when a LET body is a macro that expands to a restart-case under heap
# pressure.  Use a fresh user macro so it isn't confused with CCASE itself.
cat > "$WORK/macro-rcase.lisp" <<'EOF'
(defmacro mrc (place)
  (let ((g (gensym)) (b (gensym)) (n (gensym)))
    `(let ((,g ,place))
       (block ,b
         (loop
           (cond ((eql ,g 5) (return-from ,b 'ok))
                 (t (restart-case (error "x")
                      (store-value (,n)
                        :report "r" :interactive (lambda () (list 1))
                        (setf ,place ,n) (setq ,g ,n))))))))))
(format t "MRC:~a~%" (let ((x 5)) (mrc x)))
EOF
out=$(run_stress "$WORK/macro-rcase.lisp")
check_contains "macro->let->restart-case compiles+runs under GC stress" "MRC:OK" "$out"
check_absent   "no stale-cursor CDR fault compiling restart-case under stress" "not of type LIST\|#<unknown>" "$out"

# --- Case: RETURN-FROM over an intervening CATCH promotes BLOCK to NLX -------
# Bug: a local RETURN-FROM that jumps over an intervening CATCH/HANDLER-BIND
# took the cheap local-jump path, skipping OP_UNCATCH/OP_HANDLER_POP and
# leaking the catch/handler frame on every NORMAL exit — overflowing the NLX
# stack on hot paths (handler-case expands to
# (block B (catch 'T (return-from B (handler-bind ... form))))).
# Fix: nlx_scan promotes such a block to the NLX path; the promotion decision
# macroexpands the body (allocates), so the scan's `body`/`tag` cursors must
# survive compaction.  Compile to a clean FASL, then load+run under GC stress.
cat > "$WORK/nlxcatch.lisp" <<'EOF'
(defun nlxc-loop (n)
  (let ((acc 0))
    (dotimes (i n acc)
      (setq acc (handler-case (block blk (catch 'tg (return-from blk 1)))
                  (error () -1))))))
(format t "NLXC:~a~%" (nlxc-loop 5000))
(format t "NLXCMV:~a~%"
  (multiple-value-list
    (dotimes (i 3000 (handler-case (values 1 2 3) (error () nil))))))
EOF
out=$(run_stress "$WORK/nlxcatch.lisp")
check_contains "return-from over catch: no NLX leak under GC stress" "NLXC:1" "$out"
check_contains "handler-case mv through promoted block under GC stress" "NLXCMV:(1 2 3)" "$out"
check_absent   "no NLX overflow from leaked catch frame" "NLX stack overflow" "$out"

# --- Case: STORE-VALUE / USE-VALUE restart-name comparison survives compaction --
# Bug: SYM_STORE_VALUE and SYM_USE_VALUE were unregistered C-global CL_Obj values.
# After GC compaction cl_restart_stack[i].name was forwarded to the new offset, but
# the C globals still held the pre-compaction offset, so the name comparison in
# invoke_value_restart always failed — store-value/use-value silently returned NIL.
# Also: the local `value`/`name`/`condition` copies inside invoke_value_restart were
# not GC-protected, so any compaction triggered by restart_applicable (for :test fns)
# left them stale.
# Fix: cl_gc_register_root(&SYM_STORE_VALUE/USE_VALUE) in cl_builtins_condition_init;
# CL_GC_PROTECT(value/name/condition) in invoke_value_restart.
cat > "$WORK/sv.lisp" <<'EOF'
;; Force enough allocation to trigger compaction under GC stress, then
;; exercise store-value and use-value restart lookup.
(defun sv-test ()
  (let ((pad (make-list 5000)))
    (declare (ignore pad)))
  (list
   (restart-case (store-value 7)
     (store-value (v) (* v 6)))
   (restart-case (use-value 21)
     (use-value (v) (* v 2)))))
(format t "SV:~a~%" (sv-test))
EOF
out=$(run_stress "$WORK/sv.lisp")
check_contains "store-value finds restart after compaction"          "SV:(42 42)" "$out"
check_absent   "store-value/use-value did not return NIL post-compact" "SV:(NIL\|SV:NIL" "$out"

# --- Case: MAKE-LOAD-FORM reconstruction under GC stress ---------------------
# The FASL MAKE-LOAD-FORM path has two GC-sensitive halves:
#   (writer) cl_fasl_mlf_prepass walks the constant graph and calls the user
#     MAKE-LOAD-FORM method (clamiga::%FASL-LOAD-FORM) per literal object — that
#     runs arbitrary Lisp and compacts.  Its worklist/seen arrays and the
#     (creation . init) result cache hold raw CL_Obj offsets that must be
#     GC-rooted+forwarded (cl_fasl_gc_mark_mlf / cl_fasl_gc_update_mlf).
#   (reader) FASL_TAG_LOAD_FORM compiles+evaluates the creation form, registers
#     the object at the OBJ_DEF id (so the init form's OBJ_REF self-loop
#     resolves), then compiles+evaluates the init form — all allocating.
# Two probes: (1) compile a clean FASL and LOAD it under stress (reader half);
# (2) compile the source UNDER stress (writer half / pre-pass walk), then load
# it and check the reconstruction is intact, including the EQ self-reference.
cat > "$WORK/mlf.lisp" <<'EOF'
(defpackage :gcstress-mlf (:use :cl))
(in-package :gcstress-mlf)
(defclass node ()
  ((label :initarg :label :accessor node-label)
   (n     :initarg :n     :accessor node-n)
   (self  :accessor node-self)))
(defmethod make-load-form ((x node) &optional env)
  (declare (ignore env))
  (make-load-form-saving-slots x))
(defvar *node*
  #.(let ((x (make-instance 'node :label "hi" :n 7)))
      (setf (node-self x) x)
      x))
(defun mlf-probe ()
  (list (node-label *node*) (node-n *node*) (eq *node* (node-self *node*))))
EOF

# (1) reader half: clean compile, load under stress.
if compile_fasl "$WORK/mlf.lisp" "$WORK/mlf.fasl"; then
    cat > "$WORK/mlf-load.lisp" <<EOF
(load "$WORK/mlf.fasl")
(format t "MLF-LOAD:~a~%" (gcstress-mlf::mlf-probe))
EOF
    out=$(run_stress "$WORK/mlf-load.lisp")
    check_contains "make-load-form reconstruction loads under GC stress"   "MLF-LOAD:(hi 7 T)" "$out"
    check_absent   "no corruption reconstructing MLF object on load"        "Undefined\|Unbound\|type 0\|corrupted" "$out"
else
    echo "  SKIP  make-load-form clean FASL compile failed"
fi

# (2) writer half: compile the source UNDER stress (exercises the pre-pass walk
#     + %FASL-LOAD-FORM call + serialization under compaction), then load clean.
cat > "$WORK/mlf-compile.lisp" <<EOF
(compile-file "$WORK/mlf.lisp" :output-file "$WORK/mlf-s.fasl")
(format t "MLF-COMPILED~%")
EOF
out=$(run_stress "$WORK/mlf-compile.lisp")
check_contains "make-load-form pre-pass compiles under GC stress" "MLF-COMPILED" "$out"
if echo "$out" | grep -q "MLF-COMPILED"; then
    cat > "$WORK/mlf-s-load.lisp" <<EOF
(load "$WORK/mlf-s.fasl")
(format t "MLF-SLOAD:~a~%" (gcstress-mlf::mlf-probe))
EOF
    out2=$(env -u CLAMIGA_GC_STRESS timeout 60 "$CLAMIGA" --no-userinit --heap 48M \
               --non-interactive --load "$WORK/mlf-s-load.lisp" 2>&1)
    check_contains "FASL written under GC-stress compile reconstructs correctly" "MLF-SLOAD:(hi 7 T)" "$out2"
    check_absent   "no corruption in FASL written by pre-pass under stress"      "Undefined\|Unbound\|type 0\|corrupted" "$out2"
fi

# --- Case: FORMAT ~<...~> justification under stress ----------------------
# fmt_justify renders each segment into a fresh string-output-stream and
# copies the result's code points out of the arena.  Under compaction the
# protected sstream and the result string can relocate mid-render; if the
# copy used a stale arena pointer the padded scale line would be corrupted.
# Reproduces the cl-spark vspark scale line (wide glyphs ˫ + ˧).
cat > "$WORK/just.lisp" <<'EOF'
(dotimes (i 50)
  (let ((s (format nil "~30,,,'-<~A~;~A~;~A~>" "0" "75" "150")))
    (unless (string= s "0------------75------------150")
      (format t "JUST-BAD:~s~%" s) (return)))
  (let ((w (format nil "~10,,,'-<~A~;~A~;~A~>"
                   (code-char 747) #\+ (code-char 743))))
    (unless (and (= (char-code (char w 0)) 747)
                 (= (char-code (char w (1- (length w)))) 743))
      (format t "JUST-WIDE-BAD:~s~%" w) (return))))
(format t "JUST-OK~%")
EOF
out=$(run_stress "$WORK/just.lisp")
check_contains "FORMAT ~< justification stable under GC stress" "JUST-OK" "$out"
check_absent   "no corrupted justification output"              "JUST-BAD\|JUST-WIDE-BAD" "$out"

# --- Case: sequence functions snapshot allocation under stress ------------
# SUBSTITUTE/NSUBSTITUTE/REMOVE-DUPLICATES/SORT/MERGE/CONCATENATE snapshot the
# input elements into heap vectors / C buffers and call user :test/:key
# functions that can compact mid-operation.  If a snapshot or result handle
# went stale, the produced sequence would be corrupted.
cat > "$WORK/seqfns.lisp" <<'EOF'
(let ((bad nil))
  (dotimes (i 80)
    (unless (equalp (substitute 0 1 (copy-seq #*010101)) #*000000) (setq bad :subst-bv))
    (unless (string= (substitute #\Z #\a (copy-seq "banana")) "bZnZnZ") (setq bad :subst-str))
    (unless (equalp (nsubstitute 'b 'a (copy-seq #(a b a c)) :count 1 :from-end t) #(a b b c))
      (setq bad :nsubst))
    (unless (equalp (remove-duplicates (copy-seq #(1 2 1 3 2 4))) #(1 3 2 4)) (setq bad :remdup))
    (unless (equalp (sort (copy-seq #*10011101) #'<) #*00011111) (setq bad :sort-bv))
    (unless (equalp (merge 'bit-vector (copy-seq #*011) (copy-seq #*001) #'<) #*000111)
      (setq bad :merge-bv))
    (unless (equalp (concatenate 'bit-vector #*01 #*10) #*0110) (setq bad :concat-bv))
    (when bad (return)))
  (if bad (format t "SEQFN-BAD:~s~%" bad) (format t "SEQFN-OK~%")))
EOF
out=$(run_stress "$WORK/seqfns.lisp")
check_contains "sequence functions stable under GC stress" "SEQFN-OK" "$out"
check_absent   "no corrupted sequence results"             "SEQFN-BAD" "$out"

# --- Case: MAP-INTO into a list keeps its write cursor live under stress ----
# MAP-INTO calls the mapping function once per element and stores the result
# through a GC-protected list cursor (res_cur).  If that cursor went stale
# across the allocating mapping call, the destination list would be corrupted.
cat > "$WORK/mapinto.lisp" <<'EOF'
(let ((bad nil))
  (dotimes (i 80)
    (unless (equalp (let ((a (copy-seq (list 0 0 0 0))))
                      (map-into a (lambda (x) (list x x)) (list 1 2 3 4)))
                    (list (list 1 1) (list 2 2) (list 3 3) (list 4 4)))
      (setq bad :map-into-list))
    (when bad (return)))
  (if bad (format t "MAPINTO-BAD:~s~%" bad) (format t "MAPINTO-OK~%")))
EOF
out=$(run_stress "$WORK/mapinto.lisp")
check_contains "map-into list cursor stable under GC stress" "MAPINTO-OK" "$out"
check_absent   "no corrupted map-into list"                  "MAPINTO-BAD" "$out"

# --- Case: compile_if re-derives then/else AFTER compiling the test --------
# Bug: compile_if read then_form/else_form from `form` BEFORE compile_expr(test),
# which allocates and can compact (moving GC).  Only `form` was GC-protected, so
# the then_form/else_form C locals held stale pre-move offsets; the returned
# (stale) then_form was trampolined and emitted as a bogus self-evaluating
# constant — an interior pointer into an unrelated object — corrupting the
# constant pool.  Surfaced as "malformed call form (dotted pair)" / "CDR not a
# list" compiling a WHEN/UNLESS/IF body inside a LET/DOTIMES under GC stress.
# The corruption is layout-sensitive (~40% per process), so run the form several
# times and require EVERY run to produce the correct result.
cat > "$WORK/compile-if.lisp" <<'EOF'
(let ((bad nil))
  (dotimes (i 200)
    (unless (equalp (let ((a (copy-seq (list 0 0 0 0))))
                      (map-into a (lambda (x) (list x x)) (list 1 2 3 4)))
                    (list (list 1 1) (list 2 2) (list 3 3) (list 4 4)))
      (setq bad :compile-if))
    (when bad (return)))
  (if bad (format t "COMPILEIF-BAD:~s~%" bad) (format t "COMPILEIF-OK~%")))
EOF
compileif_ok=1
for _r in 1 2 3 4 5 6 7 8; do
    o=$(run_stress "$WORK/compile-if.lisp")
    if ! echo "$o" | grep -q "COMPILEIF-OK"; then
        compileif_ok=0
        compileif_out="$o"
        break
    fi
done
if [ "$compileif_ok" -eq 1 ]; then
    out="COMPILEIF-OK"
else
    out="$compileif_out"
fi
check_contains "compile_if then/else stable under GC stress (8 runs)" "COMPILEIF-OK" "$out"
check_absent   "no corrupted constant pool from stale if-branch"      "COMPILEIF-BAD" "$out"

# --- Case: apply_condition_slot_initforms :initform thunk under GC stress -----
# Bug path: apply_condition_slot_initforms calls cl_vm_apply(thunk, NULL, 0) to
# evaluate the :initform lambda, which can trigger a compacting GC.  The function
# GC-protects `type_sym`, `slots`, `hierarchy`, `if_table`, `type`, and the inner
# `specs` cursor, and re-reads `slot_nm` from the protected chain after the call.
# Exercise a no-:initarg slot (keyed by slot name) whose initform captures a
# dynamic variable rebound at make-condition time, over both the make-condition
# and the error/signal paths (bi_signal funnels through the same helper).
# NOTE: kept deliberately single-slot to keep this case focused on the initform
# path.  The *multi*-slot define-condition-after-defvar corruption it used to
# avoid was a separate bug (map builtins not protecting their list cursors) and
# is now fixed and covered by the dedicated "map cursor" case below.
cat > "$WORK/slot-initform.lisp" <<'EOF'
(defparameter *gcs-captured* :default)
(define-condition my-gcstress-initform (error)
  ((val :reader initform-val :initform *gcs-captured*)))  ; no :initarg → keyed by slot name
;; Call many times so a compaction lands inside cl_vm_apply during initform eval.
(let ((bad nil))
  (dotimes (i 50)
    (let ((c (let ((*gcs-captured* i)) (make-condition 'my-gcstress-initform))))
      (unless (eql (initform-val c) i)
        (setq bad (list :val-mismatch i (initform-val c))) (return))))
  (if bad
      (format t "SINITFORM-BAD:~s~%" bad)
      (format t "SINITFORM-OK~%")))
;; Also verify via the error/signal path (bi_signal uses apply_condition_slot_initforms).
(let ((*gcs-captured* :via-error))
  (format t "SINITFORM-ERR:~a~%"
    (handler-case (error 'my-gcstress-initform)
      (my-gcstress-initform (e) (initform-val e)))))
EOF
out=$(run_stress "$WORK/slot-initform.lisp")
check_contains "slot :initform thunk evaluated correctly under GC stress (make-condition)" "SINITFORM-OK" "$out"
check_contains "slot :initform thunk captures rebinding correctly under GC stress (error)" "SINITFORM-ERR:VIA-ERROR" "$out"
check_absent   "no corruption in apply_condition_slot_initforms under stress" "SINITFORM-BAD\|Unbound\|type 0\|corrupted\|Undefined" "$out"

# --- Case: MAPCAR/MAP family list cursors survive compaction -----------------
# Bug: bi_mapcar/bi_mapc/bi_mapcan/bi_maplist/bi_mapl/bi_mapcon and bi_map /
# bi_map_into held their `lists[]` / `seqs[]` list-cursor C arrays UNPROTECTED
# across the mapped-function call (cl_vm_apply/call_func).  When the mapped
# function allocates (e.g. conses), the moving GC relocates the list being
# walked, leaving the cursor at a stale offset — the next cl_car(cursor) reads
# a relocated/garbage cell ("CAR: argument is not of type LIST").
#
# This surfaced as the define-condition-after-defvar heisenbug: define-condition
# macroexpands to `(mapcar (lambda (spec) ... (list spec)) slot-specs)` (the
# `(list spec)` cons triggers the relocating compaction), so a multi-slot
# condition compiled right after a cons-allocating top-level form (defvar,
# defparameter, (list ...)) corrupted the slot walk and the type never
# registered.  Reproduce that exact shape, plus direct mapcar/map-into probes.
cat > "$WORK/mapcursor.lisp" <<'EOF'
;; (1) define-condition after a cons-allocating top-level form — the original
;;     heisenbug.  These MUST be separate top-level forms (the defparameter has
;;     to *execute* before the define-condition is compiled).
(defparameter *mc-x* :default)
(define-condition mc-cond (error)
  ((a :reader mc-a) (b :reader mc-b) (c :reader mc-c)))
(format t "MC-COND:~a~%" (typep (make-condition 'mc-cond) 'mc-cond))

;; (2) mapcar with an allocating mapped function over a freshly-consed list —
;;     every (list e) relocates the cursor.  Result must be intact and correct.
(let ((r (mapcar (lambda (e) (list e e)) (list 1 2 3 4 5 6 7 8))))
  (format t "MC-MAPCAR:~a~%" (equal r '((1 1)(2 2)(3 3)(4 4)(5 5)(6 6)(7 7)(8 8)))))

;; (3) two-list mapcar (two cursors) with an allocating function.
(let ((r (mapcar (lambda (x y) (cons x y)) (list 1 2 3) (list 'a 'b 'c))))
  (format t "MC-MAPCAR2:~a~%" (equal r '((1 . a)(2 . b)(3 . c)))))

;; (4) map 'list over a consed list with an allocating function.
(let ((r (map 'list (lambda (e) (list e)) (list 10 20 30 40))))
  (format t "MC-MAP:~a~%" (equal r '((10)(20)(30)(40)))))

;; (5) map-into a fresh list from a consed source with an allocating function.
(let ((dst (make-list 4)))
  (map-into dst (lambda (e) (list e)) (list 5 6 7 8))
  (format t "MC-MAPINTO:~a~%" (equal dst '((5)(6)(7)(8)))))

;; (6) mapcan with an allocating function (builds + nconcs fresh lists).
(let ((r (mapcan (lambda (e) (list e (* e 10))) (list 1 2 3))))
  (format t "MC-MAPCAN:~a~%" (equal r '(1 10 2 20 3 30))))

;; (7) mapc / mapl / maplist / mapcon — the remaining list-map cursors.
(let ((acc nil))
  (mapc (lambda (e) (push (list e) acc)) (list 1 2 3 4))
  (format t "MC-MAPC:~a~%" (equal (nreverse acc) '((1)(2)(3)(4)))))
(let ((acc nil))
  (mapl (lambda (tl) (push (list (car tl)) acc)) (list 5 6 7))
  (format t "MC-MAPL:~a~%" (equal (nreverse acc) '((5)(6)(7)))))
(let ((r (maplist (lambda (tl) (list (length tl))) (list 1 2 3 4))))
  (format t "MC-MAPLIST:~a~%" (equal r '((4)(3)(2)(1)))))
(let ((r (mapcon (lambda (tl) (list (car tl) (* (car tl) 10))) (list 1 2 3))))
  (format t "MC-MAPCON:~a~%" (equal r '(1 10 2 20 3 30))))

;; (8) every / some / notevery / notany with predicates that CONS (allocate),
;;     walking a freshly-consed list — the same cursor-relocation path.
(format t "MC-EVERY:~a~%"
  (every (lambda (e) (car (list (plusp e)))) (list 1 2 3 4 5 6)))
(format t "MC-SOME:~a~%"
  (eql 4 (some (lambda (e) (car (list (and (evenp e) e)))) (list 1 3 4 5))))
(format t "MC-NOTEVERY:~a~%"
  (notevery (lambda (e) (car (list (oddp e)))) (list 1 3 4)))
(format t "MC-NOTANY:~a~%"
  (notany (lambda (e) (car (list (> e 100)))) (list 1 2 3)))

;; (9) reduce with a :key function and an allocating reducer, both forward and
;;     :from-end, over freshly-consed lists (forward `cur` and :from-end vector).
(format t "MC-REDUCE:~a~%"
  (eql 30 (reduce (lambda (a b) (car (list (+ a b))))
                  (list '(1) '(2) '(3) '(4)) :key #'car :initial-value 20)))
(format t "MC-REDUCE-FE:~a~%"
  (eql 24 (reduce (lambda (e a) (car (list (+ e a))))
                  (list '(1) '(2) '(3) '(4)) :key #'car :from-end t :initial-value 14)))
EOF
out=$(run_stress "$WORK/mapcursor.lisp")
check_contains "define-condition after defparameter registers under GC stress" "MC-COND:T" "$out"
check_contains "mapcar allocating fn keeps list cursor under GC stress"        "MC-MAPCAR:T" "$out"
check_contains "two-list mapcar keeps both cursors under GC stress"            "MC-MAPCAR2:T" "$out"
check_contains "map 'list allocating fn keeps cursor under GC stress"          "MC-MAP:T" "$out"
check_contains "map-into allocating fn keeps source cursor under GC stress"    "MC-MAPINTO:T" "$out"
check_contains "mapcan allocating fn keeps cursor under GC stress"             "MC-MAPCAN:T" "$out"
check_contains "mapc allocating fn keeps cursor under GC stress"               "MC-MAPC:T" "$out"
check_contains "mapl allocating fn keeps cursor under GC stress"               "MC-MAPL:T" "$out"
check_contains "maplist allocating fn keeps cursor under GC stress"            "MC-MAPLIST:T" "$out"
check_contains "mapcon allocating fn keeps cursor under GC stress"             "MC-MAPCON:T" "$out"
check_contains "every consing predicate keeps cursor under GC stress"          "MC-EVERY:T" "$out"
check_contains "some consing predicate keeps cursor under GC stress"           "MC-SOME:T" "$out"
check_contains "notevery consing predicate keeps cursor under GC stress"       "MC-NOTEVERY:T" "$out"
check_contains "notany consing predicate keeps cursor under GC stress"         "MC-NOTANY:T" "$out"
check_contains "reduce :key + allocating reducer keeps cursor under GC stress" "MC-REDUCE:T" "$out"
check_contains "reduce :from-end :key keeps vector elements under GC stress"   "MC-REDUCE-FE:T" "$out"
check_absent   "no corrupted cursor in map family under stress" "argument is not of type LIST\|unknown type specifier\|corrupted\|type 0" "$out"

# --- Case: #S(...) structure-literal reader under GC stress ----------------
# The #S reader conses a helper-call form and constructs the struct at read
# time.  Under compaction every alloc relocates, so the spec list, the quoted
# form, and the constructor args must all stay GC-protected; a stale offset
# would corrupt the slot values.
cat > "$WORK/sreader.lisp" <<'EOF'
(defstruct sgs-pt x y)
(defstruct (sgs-pt3 (:include sgs-pt)) z)
;; Read many #S literals so a compaction lands mid-read.
(let ((ok t))
  (dotimes (i 200)
    (let ((p #S(sgs-pt :x "alpha" :y "beta"))
          (q #S(sgs-pt3 :x 1 :y 2 :z 3)))
      (unless (and (equal (sgs-pt-x p) "alpha")
                   (equal (sgs-pt-y p) "beta")
                   (eql (sgs-pt3-z q) 3)
                   (eql (sgs-pt-x q) 1))
        (setq ok nil))))
  (format t "SREADER:~a~%" ok))
EOF
out=$(run_stress "$WORK/sreader.lisp")
check_contains "#S reader builds correct struct under GC stress" "SREADER:T" "$out"
check_absent   "#S reader no corruption under GC stress" \
  "not of type\|unknown type specifier\|corrupted\|type 0\|Unbound" "$out"

# --- Case: handler matcher expanding a deftype alias under GC stress -------
# cl_condition_type_matches now calls a clause type's deftype expander (via
# cl_vm_apply) so a handler clause that is a deftype alias of a condition class
# still matches.  That allocates, so it can compact mid-signal.  The signal
# loop in cl_signal_condition must re-derive its `cond` pointer (and keep
# `condition` GC-rooted) afterwards — otherwise, once the non-matching alias
# clause was probed, the loop checked the OUTER handler through a relocated
# condition object (stale arena offset), silently failing to catch.  Here the
# inner clause is an alias that does NOT match a SIMPLE-ERROR, forcing the loop
# to continue to the outer ERROR handler through the just-compacted state.
cat > "$WORK/hcalias.lisp" <<'EOF'
(deftype gcs-warn-alias () 'simple-warning)   ; a SIMPLE-ERROR is NOT this
(let ((ok t))
  (dotimes (i 200)
    (let ((r (handler-case
                 (handler-case (error "boom")
                   (gcs-warn-alias () :wrong))   ; probe expands deftype -> compacts
               (error () :outer-caught))))       ; must still catch via valid cond
      (unless (eq r :outer-caught) (setq ok nil))))
  (format t "HCALIAS:~a~%" ok))
EOF
out=$(run_stress "$WORK/hcalias.lisp")
check_contains "handler matcher deftype-alias keeps cond valid under GC stress" "HCALIAS:T" "$out"
check_absent   "no stale condition after deftype-alias probe under GC stress" \
  "not of type\|unknown type specifier\|corrupted\|type 0\|Unbound\|No catch" "$out"

# --- Case: cond_type_matches_depth OR/AND cursor and parent-chain GC safety ---
# Bug (HIGH): in the OR/AND traversal loop of cond_type_matches_depth, `cond_type`
# and the list cursor `rest` were unprotected C locals across the recursive
# cond_type_matches_depth call.  When the handler type is a deftype alias that
# expands to (or ...), each branch check reaches the deftype expansion path and
# calls cl_vm_apply, which allocates.  Under GC stress that compacts the heap,
# making `rest` (and `cond_type`) stale so the next cl_cdr(rest) dereferences
# a relocated or freed offset.
# Similarly, in the parent-chain while loop, `parents` and `handler_type` were
# unprotected, so a handler with multiple-parent conditions (e.g. a condition
# inheriting from both a domain class and ERROR) could stale the loop cursor.
# Fix: CL_GC_PROTECT(cond_type)+CL_GC_PROTECT(rest) in OR/AND; likewise for
# the parent chain.
cat > "$WORK/ctype_gc.lisp" <<'EOF'
; deftype that expands to (or ...) — forces the OR traversal branch
(deftype %gcs-ctype-or () '(or type-error simple-warning))
; multi-parent condition (parent-chain traversal): error is the 2nd parent
(define-condition %gcs-mc-base (condition) ())
(define-condition %gcs-mc-err (%gcs-mc-base error) ())
; repeat many times so a compaction fires mid-loop
(let ((ok t))
  (dotimes (i 200)
    ; OR branch, first alternative (type-error)
    (let ((r (handler-case
                 (error 'type-error :datum 1 :expected-type 'string)
               (%gcs-ctype-or () :caught-te)
               (error () :wrong-te))))
      (unless (eq r :caught-te) (setf ok nil)))
    ; OR branch, second alternative (simple-warning)
    (let ((r (handler-case
                 (signal (make-condition 'simple-warning :format-control "w"))
               (%gcs-ctype-or () :caught-sw)
               (warning () :wrong-sw))))
      (unless (eq r :caught-sw) (setf ok nil)))
    ; parent-chain: multi-parent condition, error is 2nd parent
    (let ((r (handler-case
                 (error '%gcs-mc-err)
               (error () :caught-mc)
               (condition () :wrong-mc))))
      (unless (eq r :caught-mc) (setf ok nil))))
  (format t "CTYPE-GC:~a~%" ok))
EOF
out=$(run_stress "$WORK/ctype_gc.lisp")
check_contains "cond-type-matches OR branch type-error side correct under GC stress" "CTYPE-GC:T" "$out"
check_absent   "no wrong-branch or corruption in cond_type_matches_depth OR/parent" \
  "wrong-te\|wrong-sw\|wrong-mc\|corrupted\|type 0\|Unbound\|not of type" "$out"

# --- Case: displaced multi-dimensional arrays survive compaction -----------
# Bug class: a displaced multi-dim array stores its backing ref at data[rank]
# (after the dimension fixnums), unlike a 1-D displaced vector (data[0]).  The
# GC mark/relocate paths and cl_vector_data_fn must locate the backing at the
# rank-adjusted slot; if compaction relocates the backing and the ref isn't
# forwarded, element reads return stale data (or NIL).  serapeum RESHAPE.
cat > "$WORK/dispmd.lisp" <<'EOF'
(let ((ok t))
  (dotimes (i 200)
    ;; Fresh backing + a 2-D and a nested 1-D-over-multi-dim view each iter,
    ;; consing in between to force a compaction with the views live.
    (let* ((a (make-array 48))
           (junk nil))
      (dotimes (k 48) (setf (aref a k) (1+ k)))
      (setf junk (make-list 20))
      (let* ((m (make-array '(2 3 3 2) :displaced-to a :displaced-index-offset 0))
             (more (make-list 20))
             (r (make-array 36 :displaced-to m)))
        (declare (ignore junk more))
        ;; 2-D read through one level, nested read through two levels.
        (unless (and (= (aref m 1 2 2 1) 36)
                     (= (row-major-aref m 0) 1)
                     (= (aref r 35) 36)
                     (equal (array-dimensions m) '(2 3 3 2)))
          (setf ok nil)))))
  (format t "DISPMD-GC:~a~%" ok))
EOF
out=$(run_stress "$WORK/dispmd.lisp")
check_contains "displaced multi-dim arrays read correctly under GC stress" "DISPMD-GC:T" "$out"
check_absent   "no stale/NIL element or corruption in displaced multi-dim path" \
  "DISPMD-GC:NIL\|not of type\|corrupted\|type 0\|out of range" "$out"

# --- Case: APPLY spreads a large arglist; &rest list survives compaction ---
# Bug class: OP_APPLY spreads the arglist onto the VM stack, then conses the
# surplus into the callee's &rest list.  cl_cons() can compact mid-build, so
# the partially-built rest list, the call_func, and the raw callee_bc pointer
# must all survive relocation (call_func is GC-protected and callee_bc is
# re-derived after the cons loop).  A stale arg or callee_bc would corrupt the
# call.  Covers the CALL-ARGUMENTS-LIMIT change (serapeum string+).
cat > "$WORK/apply_rest.lisp" <<'EOF'
(defun %gcs-ar (&rest xs) (reduce #'+ xs :initial-value 0))
(defun %gcs-arr (a b &rest xs) (list a b (length xs) (reduce #'+ xs :initial-value 0)))
(let ((ok t))
  (dotimes (i 60)
    ;; 1..100 spread to a &rest fn: count and ordered-sum must both hold.
    (let* ((args (loop for k from 1 to 100 collect k))
           (junk (make-list 30)))
      (declare (ignore junk))
      (unless (= (apply #'%gcs-ar args) 5050) (setf ok nil)))
    ;; required + &rest, surplus past the old 64 cap.
    (let* ((args (loop for k from 1 to 200 collect k)))
      (unless (equal (apply #'%gcs-arr args) '(1 2 198 20097)) (setf ok nil))))
  (format t "APPLY-REST-GC:~a~%" ok))
EOF
out=$(run_stress "$WORK/apply_rest.lisp")
check_contains "APPLY large &rest list correct under GC stress" "APPLY-REST-GC:T" "$out"
check_absent   "no stale arg/callee_bc or corruption in OP_APPLY &rest build" \
  "APPLY-REST-GC:NIL\|not of type\|corrupted\|type 0\|too many\|Unbound" "$out"

# --- Case: user-defined metaclasses survive compaction ---------------------
# Bug class: %ENSURE-CLASS-VIA-METACLASS allocates the class metaobject as an
# instance of its metaclass and runs it through INITIALIZE-INSTANCE (which
# conses supers, merges :default-initargs, populates metaclass slots).  Every
# alloc can compact, so the class object, its supers, and the metaclass slot
# value must all survive relocation.  Covers serapeum ABSTRACT-CLASS /
# TOPMOST-OBJECT (specs/mop.md user-defined metaclasses).
cat > "$WORK/metaclass.lisp" <<'EOF'
(defclass gcs-abstract-mc (standard-class) ())
(defmethod allocate-instance ((a gcs-abstract-mc) &rest initargs)
  (declare (ignore initargs)) (error "abstract"))
(defclass gcs-topmost () ())
(defclass gcs-topmost-mc (standard-class)
  ((topc :initarg :topc :reader gcs-topc)))
(defmethod validate-superclass ((c1 gcs-topmost-mc) (c2 standard-class)) t)
(defun gcs-ins (sc list)
  (cond ((null list) (list sc))
        ((subtypep sc (first list)) (cons sc list))
        (t (cons (first list) (gcs-ins sc (rest list))))))
(defmethod initialize-instance :around
    ((class gcs-topmost-mc) &rest initargs &key direct-superclasses topc)
  (if (find topc direct-superclasses :test (lambda (a b) (subtypep b a)))
      (call-next-method)
      (apply #'call-next-method class
             :direct-superclasses (gcs-ins (find-class topc) direct-superclasses)
             initargs)))
(defclass gcs-meta (gcs-topmost-mc) () (:default-initargs :topc 'gcs-topmost))
(let ((ok t))
  (dotimes (i 40)
    (eval `(defclass ,(intern (format nil "GCS-ABS-~d" i)) ()
             () (:metaclass gcs-abstract-mc)))
    (let ((junk (make-list 20)))
      (declare (ignore junk))
      (unless (eq :sig
                  (handler-case
                      (progn (make-instance (intern (format nil "GCS-ABS-~d" i)))
                             :no)
                    (error () :sig)))
        (setf ok nil)))
    (eval `(defclass ,(intern (format nil "GCS-TOP-~d" i)) ()
             () (:metaclass gcs-meta)))
    (unless (typep (make-instance (intern (format nil "GCS-TOP-~d" i)))
                   'gcs-topmost)
      (setf ok nil)))
  (format t "METACLASS-GC:~a~%" ok))
EOF
out=$(run_stress "$WORK/metaclass.lisp")
check_contains "user metaclass abstract+topmost correct under GC stress" "METACLASS-GC:T" "$out"
check_absent   "no corruption in metaclass class creation under GC stress" \
  "METACLASS-GC:NIL\|not of type\|corrupted\|type 0\|Unbound\|No applicable" "$out"

# --- Case: specialized-element-type arrays keep their code under compaction -
# Bug class: make-array records a general-vector element-type code (elt_type)
# so (VECTOR FIXNUM) is distinct from (VECTOR T).  The code lives in the
# vector header; compaction must relocate the header (and its code) intact
# while the array also holds live CL_Obj elements.  Covers serapeum VECT-TYPE.
cat > "$WORK/elttype.lisp" <<'EOF'
(let ((ok t))
  (dotimes (i 80)
    (let ((fa (make-array 4 :element-type 'fixnum :adjustable t :fill-pointer 0))
          (junk (make-list 15)))
      (declare (ignore junk))
      (vector-push-extend (* i 1) fa)
      (vector-push-extend (* i 2) fa)
      (unless (and (eq 'fixnum (array-element-type fa))
                   (not (typep fa '(vector t)))
                   (typep fa '(vector fixnum))
                   (= (aref fa 0) (* i 1))
                   (= (aref fa 1) (* i 2)))
        (setf ok nil)))
    (let ((ta (make-array 3 :initial-element i)))
      (unless (and (typep ta '(vector t))
                   (eq 't (array-element-type ta)))
        (setf ok nil))))
  (format t "ELTTYPE-GC:~a~%" ok))
;; Deftype alias for FIXNUM: classify_array_elt_type calls cl_vm_apply for the
;; expander, which triggers a compacting GC under stress.  element_type (bi_make_array)
;; and typespec (bi_upgraded_array_element_type) are C locals that must be
;; re-read from the GC-rooted args[] slot after classify returns.
(deftype gcs-myfixnum () 'fixnum)
(deftype gcs-msingle () 'single-float)
(let ((ok2 t))
  (dotimes (i 40)
    (let ((va (make-array 4 :element-type 'gcs-myfixnum))
          (junk (make-list 10)))
      (declare (ignore junk))
      (setf (aref va 0) (* i 3))
      (setf (aref va 1) (+ i 1))
      (unless (and (eq 'fixnum (array-element-type va))
                   (typep va '(vector fixnum))
                   (not (typep va '(vector t)))
                   (= (aref va 0) (* i 3))
                   (= (aref va 1) (+ i 1))
                   ;; upgraded-array-element-type must also re-read typespec after classify
                   (eq 'fixnum (upgraded-array-element-type 'gcs-myfixnum))
                   (eq 'single-float (upgraded-array-element-type 'gcs-msingle)))
        (setf ok2 nil))))
  (format t "ELTTYPE-DEF:~a~%" ok2))
EOF
out=$(run_stress "$WORK/elttype.lisp")
check_contains "specialized element-type arrays correct under GC stress" "ELTTYPE-GC:T" "$out"
check_absent   "no corruption in element-type-coded arrays under GC stress" \
  "ELTTYPE-GC:NIL\|not of type\|corrupted\|type 0\|Unbound" "$out"
check_contains "make-array+upgraded-array-elt-type with deftype alias under GC stress" "ELTTYPE-DEF:T" "$out"
check_absent   "no stale element_type/typespec from deftype alias classify compaction" \
  "ELTTYPE-DEF:NIL\|not of type\|corrupted\|type 0\|Unbound" "$out"

# --- Case: condition report with :format-arguments under GC stress --------
# Bug: format_condition_report read the format-control string and every
# format argument out of c->slots into un-rooted C locals BEFORE
# cl_make_string_output_stream / cl_format_to_stream allocated (and
# compacted) — the report then formatted relocated garbage ("CAR:
# corrupted pointer") on every (error "..." args) whose report is printed.
cat > "$WORK/condreport.lisp" <<'EOF'
(let ((ok t))
  (dotimes (i 30)
    (let ((rpt (handler-case
                   (error "gcstress ~a ~s ~d end" (list i (+ i 1)) "str" i)
                 (error (c) (format nil "~a" c)))))
      (unless (string= rpt (format nil "gcstress (~d ~d) \"str\" ~d end"
                                   i (+ i 1) i))
        (format t "BAD-REPORT:~s~%" rpt)
        (setf ok nil))))
  (format t "CONDREPORT:~a~%" ok))
EOF
out=$(run_stress "$WORK/condreport.lisp")
check_contains "condition report formats :format-arguments under GC stress" "CONDREPORT:T" "$out"
check_absent   "no corrupted pointer in condition report under GC stress" \
  "corrupted\|not of type\|BAD-REPORT" "$out"

# --- Case: SORT of a general vector with an allocating predicate/:key -----
# Bug: vector_insertion_sort captured cl_vector_data(v) ONCE and kept
# reading/writing through it across every user predicate/:key call; a
# compaction inside the predicate relocated the vector and the sort
# read/wrote the vector's OLD arena location.  The discriminator below
# makes the predicate drop a large live reference on its first call, so
# the next stress-compaction inside the sort slides the vector down over
# the freed space: the old code then "sorts" the stale copy and returns
# the vector UNSORTED (silent corruption); the fix re-derives the data
# pointer from the rooted vector after every user call.
# (Kept as flat top-level forms: a (let (...) (dotimes ...)) wrapper
# trips an unrelated, pre-existing compile-time GC-stress crash in
# compile_dotimes/compile_tagbody — see the tier-2 compiler findings.)
cat > "$WORK/sortvec.lisp" <<'EOF'
(defvar *junk* (make-list 2000))
(defvar *v* (coerce (loop for i from 30 downto 1 collect (list i)) 'vector))
(defvar *pred*
  (lambda (a b)
    (setq *junk* nil)  ; dies mid-sort -> next compaction slides *v*
    (< (first (list (car a))) (car b))))
(sort *v* *pred*)
(defvar *keys* (map 'list #'car *v*))
(format t "SORTVEC:~a~%"
        (if (equal *keys* (loop for i from 1 to 30 collect i)) t *keys*))
;; :key variant — allocating :key on a general vector of heap objects
(defvar *kv* (vector "dd" "b" "ccc" "a"))
(sort *kv* #'< :key (lambda (s) (first (list (length s)))))
;; stable sort by length: "b" and "a" (both length 1) keep order
(format t "SORTKEY:~a~%"
        (if (equal (coerce *kv* 'list) '("b" "a" "dd" "ccc"))
            t (coerce *kv* 'list)))
EOF
out=$(run_stress "$WORK/sortvec.lisp")
check_contains "sort vector with allocating predicate under GC stress" "SORTVEC:T" "$out"
check_contains "sort vector with allocating :key under GC stress"      "SORTKEY:T" "$out"
check_absent   "no corruption sorting vector under GC stress" \
  "corrupted\|not of type\|type 0\|FATAL" "$out"

# --- Case: THROW multiple values through an allocating unwind-protect ------
# Bug: t->pending_mv_values[], the saved_pending_stack[] snapshots, and
# nlx_stack[i].mv_values were neither marked nor forwarded by the GC.  A
# throw carrying (values ...) with heap objects runs the unwind-protect
# cleanup (arbitrary allocating Lisp) between the throw and the catch
# landing; a sweep during the cleanup collected the secondary values and
# a compaction left them (and the saved pending_tag) as stale offsets —
# garbage values or "No catch for tag".
cat > "$WORK/throwmv.lisp" <<'EOF'
(let ((ok t))
  (dotimes (i 40)
    (multiple-value-bind (a b c)
        (catch 'tag
          (unwind-protect
              (throw 'tag (values i (list i (+ i 1)) (format nil "s~d" i)))
            ;; allocating cleanup -> compaction while values are parked
            (make-list 20)))
      (unless (and (eql a i)
                   (equal b (list i (+ i 1)))
                   (string= c (format nil "s~d" i)))
        (format t "BAD-THROW-MV:~s ~s ~s~%" a b c)
        (setf ok nil))))
  ;; nested unwind-protects: outer throw's snapshot lives on
  ;; saved_pending_stack while the inner cleanup allocates
  (dotimes (i 40)
    (multiple-value-bind (x y)
        (catch 'outer
          (unwind-protect
              (unwind-protect
                  (throw 'outer (values (list i) (list (* i 2))))
                (make-list 15))
            (make-list 15)))
      (unless (and (equal x (list i)) (equal y (list (* i 2))))
        (format t "BAD-NESTED-MV:~s ~s~%" x y)
        (setf ok nil))))
  (format t "THROWMV:~a~%" ok))
EOF
out=$(run_stress "$WORK/throwmv.lisp")
check_contains "throw values survive allocating unwind-protect cleanup" "THROWMV:T" "$out"
check_absent   "no stale throw values / lost catch tags under GC stress" \
  "BAD-THROW-MV\|BAD-NESTED-MV\|No catch for tag\|corrupted" "$out"

# --- Case: compiling LET-wrapped iteration forms under GC stress -----------
# Coverage for the compile_let -> compile_dotimes/do/do* -> compile_tagbody
# recursion with inner LET + closures + macro-expanding bodies (INCF), the
# shape of a compile-time SIGBUS observed 2026-07-03 on master@8ed36ee
# (backtrace ended in cl_alloc inside a stress compaction).  The original
# exact form was lost and the crash has not been reproduced since; these
# shapes keep the suspect compile paths exercised under forced compaction.
cat > "$WORK/looping.lisp" <<'EOF'
(let ((ok t))
  (dotimes (i 20)
    (let ((v (make-array 4 :initial-element i)))
      (unless (eql (aref v 2) i) (setf ok nil))))
  (dotimes (i 20)
    (let ((v (lambda () i)))
      (unless (eql (funcall v) i) (setf ok nil))))
  (let ((acc 0))
    (do ((i 0 (+ i 1)))
        ((>= i 20))
      (let ((v (lambda () (incf acc i))))
        (funcall v)))
    (unless (= acc 190) (setf ok nil)))
  (do* ((j 0 (+ j 1))
        (w (list j) (list j)))
       ((>= j 10))
    (let ((u (lambda () (first w))))
      (when (< (funcall u) 0) (setf ok nil))))
  (format t "LOOPCOMPILE:~a~%" ok))
EOF
out=$(run_stress "$WORK/looping.lisp")
check_contains "LET-wrapped dotimes/do/do* with closures compile+run under GC stress" \
  "LOOPCOMPILE:T" "$out"

# --- Case: wide-string copy from an arena-interior source ------------------
# Bug: cl_make_wide_string lacked cl_make_string's arena-interior source
# guard.  cl_string_copy / cl_string_substring pass ws->data (an interior
# pointer into the source wide string); the cl_alloc inside
# cl_make_wide_string compacts (stress: before every alloc) and MOVES the
# source, so the memcpy reads from the source's pre-move address.
# Repro shape matters: the dead space below the source must be SMALL (one
# cons) so the destination allocation lands overlapping the source's old
# tail — cl_alloc's memset then zeroes the stale bytes before the copy.
# (A large dead gap slides the source far down but leaves its old bytes
# intact, so the stale read still returns the right data and hides the bug.)
cat > "$WORK/widecopy.lisp" <<'EOF'
(let ((ok t))
  (dotimes (i 30)
    (let ((junk (cons 1 2))
          (src (concatenate 'string "αβγδεζ" "ηθικλμ")))
      (setf junk nil)
      (let ((s (subseq src 3 9)))
        (unless (string= s "δεζηθι")
          (format t "BAD-WIDE-COPY:~s~%" s)
          (setf ok nil)))
      (let ((c (copy-seq src)))
        (unless (string= c "αβγδεζηθικλμ")
          (format t "BAD-WIDE-COPY2:~s~%" c)
          (setf ok nil)))))
  (format t "WIDECOPY:~a~%" ok))
EOF
out=$(run_stress "$WORK/widecopy.lisp")
check_contains "wide-string subseq/copy-seq survive compaction of the source" "WIDECOPY:T" "$out"
check_absent   "no garbage code points in wide-string copies under GC stress" \
  "BAD-WIDE-COPY" "$out"

# --- Case: oversized allocation must not corrupt the arena -----------------
# Bug: cl_alloc's >8MB header-size guard ran AFTER alloc_from_bump had
# already advanced the bump pointer; the guard's longjmp left a
# headerless region inside the walked bump front, and every later arena
# walk (sweep/forwarding/slide) desynced there — live objects above the
# hole were overwritten.  (ash 1 100000000) requests ~6.25M bignum limbs
# in a single allocation; since the tier-4 M1 caps it is rejected by
# cl_make_bignum's element-count guard ("exceeds the maximum heap object
# size"), one layer BEFORE cl_alloc's byte-size guard ("Allocation too
# large for header") — both signal the same clean cl_storage_error.
# ((make-array 3000000) no longer reaches either: the
# ARRAY-DIMENSION-LIMIT check rejects the dimension first — asserted
# separately below.)
# Note: the oversized-allocation guard signals via cl_storage_error (a
# C-level error frame, deliberately allocation-free), which aborts the
# offending top-level form rather than unwinding into handler-case; the
# loader reports it and continues.  What matters here is (a) the clean
# error message and (b) a fully intact heap afterwards — the C-level
# regression (bump must not advance) is asserted by tests/test_alloc_guard.c.
cat > "$WORK/bigalloc.lisp" <<'EOF'
(ash 1 100000000)
(make-array 3000000)
;; The heap must be fully intact afterwards: allocate, force compaction,
;; and verify data survives.
(let ((keep '()))
  (dotimes (i 50) (push (list i (format nil "x~d" i)) keep))
  (ext:gc-compact)
  (let ((ok t))
    (dotimes (i 50)
      (let ((e (nth (- 49 i) keep)))
        (unless (and (eql (first e) i)
                     (string= (second e) (format nil "x~d" i)))
          (setf ok nil))))
    (format t "BIGALLOC-2:~a~%" ok)))
EOF
out=$(run_stress "$WORK/bigalloc.lisp")
check_contains "oversized bignum allocation signals a clean storage error" "exceeds the maximum heap object size" "$out"
check_contains "oversized make-array signals a clean dimension-limit error" "exceeds ARRAY-DIMENSION-LIMIT" "$out"
check_contains "heap fully intact after oversized-allocation error"   "BIGALLOC-2:T" "$out"
check_absent   "no arena-walk corruption after oversized allocation" \
  "corrupted\|not of type\|type 0\|Guru" "$out"

# --- Tier-2 audit: builtin cursors under compacting :test/:key --------------
# Every op below runs with an ALLOCATING :test/:key (conses per call), so
# under stress each element visit compacts the heap.  Pre-fix, the walk
# cursors / element snapshots / SeqArgs fields / raw object pointers held
# stale offsets after the first user call (audit 2026-07 tier 2: the
# FIND/POSITION/COUNT family, remove family, remove-duplicates, substitute,
# mismatch/search/merge, reduce, maphash, equalp, hashtable-equalp,
# copy-list/append/reverse/butlast/pairlis, make-array/adjust-array
# :initial-element, string comparators with char designators, concatenate).
# DISCRIMINATING: the pre-fix binary dies here with
# "CAR: corrupted pointer" inside the sequence walks.
cat > "$WORK/tier2-builtins.lisp" <<'LISPEOF'
(defun k (x) (car (list x)))                 ; allocating identity :key
(defun teq (a b) (eql (car (list a)) b))     ; allocating :test
(let ((ok t))
  (macrolet ((chk (name form want)
               `(let ((got ,form))
                  (unless (equalp got ,want)
                    (format t "T2-BAD ~a:~s~%" ,name got)
                    (setf ok nil)))))
    (dotimes (i 5)
      (chk "find"       (find 3 '(1 2 3 4) :test #'teq :key #'k) 3)
      (chk "find-fe"    (find 2 '(1 2 3 2 1) :test #'teq :from-end t) 2)
      (chk "find-if"    (find-if #'evenp '(1 3 4 5) :key #'k) 4)
      (chk "pos"        (position 3 #(1 2 3 4) :test #'teq :key #'k) 2)
      (chk "pos-fe"     (position 2 '(1 2 3 2 1) :test #'teq :from-end t) 3)
      (chk "count"      (count 2 '(2 1 2 1 2) :test #'teq :key #'k) 3)
      (chk "count-fe"   (count-if #'oddp '(1 2 3 4 5) :key #'k :from-end t) 3)
      (chk "count-if-not-fe" (count-if-not #'oddp '(1 2 3 4 5) :key #'k :from-end t) 2)
      (chk "remove"     (remove 2 '(1 2 3 2) :test #'teq :key #'k) '(1 3))
      (chk "remove-str" (remove-if (lambda (c) (char= (k c) #\b)) "abcb") "ac")
      (chk "remove-vec" (remove 2 #(1 2 3 2) :test #'teq) #(1 3))
      (chk "remove-bv"  (remove 0 #*0101 :test #'teq) #*11)
      (chk "delete"     (delete 9 (list 9 1 9 2) :test #'teq) '(1 2))
      (chk "remdup"     (remove-duplicates '(1 2 1 3 2) :test #'teq :key #'k) '(1 3 2))
      (chk "remdup-fe"  (remove-duplicates '(1 2 1 3 2) :test #'teq :from-end t) '(1 2 3))
      (chk "subst"      (substitute 9 2 '(1 2 3 2) :test #'teq :key #'k) '(1 9 3 9))
      (chk "mismatch"   (mismatch '(1 2 3) '(1 2 4) :test #'teq :key #'k) 2)
      (chk "mismatch-fe" (mismatch "abcd" "abed" :from-end t
                                   :test (lambda (a b) (char= (k a) b))) 3)
      (chk "search"     (search '(2 3) '(1 2 3 4) :test #'teq :key #'k) 1)
      (chk "search-fe"  (search "ab" "abab" :from-end t
                                :test (lambda (a b) (char= (k a) b))) 2)
      (chk "merge-l"    (merge 'list (list 1 3) (list 2 4) #'< :key #'k) '(1 2 3 4))
      (chk "merge-v"    (merge 'vector (vector 1 3) (vector 2 4) #'< :key #'k) #(1 2 3 4))
      (chk "merge-s"    (merge 'string "ac" "bd" #'char< :key #'k) "abcd")
      (chk "reduce"     (reduce #'+ '(1 2 3 4) :key #'k) 10)
      (chk "reduce-fe"  (reduce #'cons '(1 2 3) :from-end t :initial-value nil)
                        '(1 2 3))
      (chk "reduce-vec" (reduce #'+ #(1 2 3 4) :key #'k :from-end t) 10)
      (chk "cat-list"   (concatenate 'list '(1 2) #(3 4) "ab")
                        (list 1 2 3 4 #\a #\b))
      (chk "copy-list"  (copy-list '(1 2 3 . 4)) '(1 2 3 . 4))
      (chk "append"     (append '(1 2) '(3) '(4 5)) '(1 2 3 4 5))
      (chk "reverse"    (reverse '(1 2 3 4)) '(4 3 2 1))
      (chk "butlast"    (butlast '(1 2 3 4) 2) '(1 2))
      (chk "pairlis"    (pairlis '(a b) '(1 2) '((c . 3)))
                        '((a . 1) (b . 2) (c . 3)))
      ;; equalp recursion allocates via ratio compare — cons/vector paths
      (chk "equalp"     (equalp (list 1/3 (vector 2/7 "Ab"))
                                (list 1/3 (vector 2/7 "aB"))) t)
      ;; maphash with an allocating fn; hash-table equalp
      (let ((h1 (make-hash-table :test 'equal))
            (h2 (make-hash-table :test 'equal))
            (n 0))
        (dotimes (j 8)
          (setf (gethash (format nil "k~d" j) h1) (list j))
          (setf (gethash (format nil "k~d" j) h2) (list j)))
        (maphash (lambda (key val) (setf n (+ n (k (first val)) (length key)))) h1)
        (chk "maphash" n 44)
        (chk "ht-equalp" (equalp h1 h2) t))
      ;; make-array / adjust-array :initial-element is a heap object
      (let ((a (make-array 5 :initial-element (list i))))
        (chk "ma-ie" (aref a 4) (list i))
        (let ((b (adjust-array a 9 :initial-element (list 99))))
          (chk "adj-ie" (aref b 8) (list 99))))
      (let ((m (make-array '(2 3) :initial-element "x")))
        (chk "ma-md" (aref m 1 2) "x"))
      ;; string comparators with character designators (coercing b conses)
      (chk "str=chr"  (string= #\a "a") t)
      (chk "str<chr"  (and (string-lessp "A" #\b) t) t)))
  (format t "T2-BUILTINS:~a~%" ok))
LISPEOF
out=$(run_stress "$WORK/tier2-builtins.lisp")
check_contains "tier-2 builtin cursor fixes survive compacting :test/:key" "T2-BUILTINS:T" "$out"
check_absent   "no tier-2 builtin corruption under GC stress" "T2-BAD\|corrupted pointer\|not of type" "$out"

# --- Tier-2 audit: compiler / format / package / reader stale locals --------
# Loading this file from source compiles every form under stress, exercising
# the compile_* handlers' post-allocation re-reads (catch, unwind-protect,
# macrolet, progv, flet/labels, cond, the, setq/setf places, mvc, eval-when,
# defsetf, defmacro destructuring, nlx_scan), the format ~{~} iteration
# snapshots, package make/export/rename cursors, struct printing, and the
# reader #+/#- saved-package root.  Coverage (not layout-discriminating):
# these sites need a specific relocation to corrupt, but any regression that
# crashes or mis-compiles fails the assertions.
cat > "$WORK/tier2-forms.lisp" <<'LISPEOF'
;; Top-level definitions (must exist at compile time of the checks below)
(defvar *t2-g* nil)
(defun t2-acc (x) (car x))
(defun t2-upd (x v) (setf (car x) v) v)
(defsetf t2-acc t2-upd)
;; defmacro destructuring rewrite: list params + &environment not first
(defmacro t2-dm ((a b) &rest rest &environment env)
  (declare (ignore env))
  `(list ,a ,b ,@rest))
;; eval-when with defmacro (two-pass top-level body)
(eval-when (:compile-toplevel :load-toplevel :execute)
  (defmacro t2-ew () `(list :ew)))
(defstruct t2s a b)

(let ((ok t))
  (macrolet ((chk (name form want)
               `(let ((got ,form))
                  (unless (equalp got ,want)
                    (format t "T2F-BAD ~a:~s~%" ,name got)
                    (setf ok nil)))))
    ;; catch: body compiled after allocating tag expr
    (chk "catch" (catch (intern (format nil "T2-TAG"))
                   (list 1 2) 42) 42)
    ;; unwind-protect: cleanup list derived after protected form compiles
    (let ((fx nil))
      (chk "uwp" (unwind-protect (values 1 2) (push (list :c) fx)) 1)
      (chk "uwp-c" (length fx) 1))
    ;; macrolet: body re-derived after expander install compiles+evals
    (chk "macrolet" (macrolet ((m (x) `(list ,x ,x))) (m 7)) '(7 7))
    ;; progv: values-form and body re-derived after symbols-form compiles
    (chk "progv" (progv (list (intern "T2-PV")) (list 5)
                   (symbol-value (intern "T2-PV"))) 5)
    ;; flet/labels: lambda-list re-derived after block-form conses; body
    ;; re-derived after phase compiles; implicit block works
    (chk "flet" (flet ((f (a &optional (b (list a))) (list a b)))
                  (f 1)) '(1 (1)))
    (chk "labels" (labels ((e? (n) (if (zerop n) t (o? (- n 1))))
                           (o? (n) (if (zerop n) nil (e? (- n 1)))))
                    (list (e? 10) (o? 7))) '(t t))
    ;; return-from inside flet body (nlx_scan flet handler cursors)
    (chk "flet-rf" (block b (flet ((g () (return-from b 9))) (g) 1)) 9)
    ;; cond: body re-derived after test compiles
    (chk "cond" (cond ((consp (list 1)) (list :yes)) (t :no)) '(:yes))
    ;; the: type-spec re-derived after value-form compiles
    (chk "the" (the (integer 0 100) (+ 40 2)) 42)
    ;; setq: var re-derived after value compiles (global store path)
    (setq *t2-g* (list 1 2))
    (chk "setq" *t2-g* '(1 2))
    ;; setf place paths: values, getf, nth-accessor, the
    (let ((c (list 1 2)) (pl (list :a 1)))
      (setf (values (car c) (cadr c)) (values 10 20))
      (chk "setf-values" c '(10 20))
      (setf (getf pl :b) (list 3))
      (chk "setf-getf" (getf pl :b) '(3))
      (setf (second c) 99)
      (chk "setf-second" c '(10 99))
      (setf (the integer (car c)) 7)
      (chk "setf-the" (car c) 7))
    ;; multiple-value-call: cursor + interned syms across cons chains
    (chk "mvc" (multiple-value-call #'list (values 1 2) 3 (values 4)) '(1 2 3 4))
    (chk "eval-when" (t2-ew) '(:ew))
    (let ((c (list 0)))
      (setf (t2-acc c) 5)
      (chk "defsetf" (t2-acc c) 5))
    (chk "dm-destr" (t2-dm (1 2) 3 4) '(1 2 3 4))
    (chk "db-key" (destructuring-bind (&key t2-fresh-kw-one t2-fresh-kw-two)
                      '(:t2-fresh-kw-one 1 :t2-fresh-kw-two 2)
                    (list t2-fresh-kw-one t2-fresh-kw-two)) '(1 2))
    (chk "db-aux" (destructuring-bind (a &aux (b (list a a))) '(5)
                    (list a b)) '(5 (5 5)))
    (chk "db-nest" (destructuring-bind ((a &optional (b (+ 1 2))) c) '((7) 9)
                     (list a b c)) '(7 3 9))
    ;; format iteration: ~{~} / ~:{~} / ~@{~} snapshots across fmt_run
    (chk "fmt-iter"  (format nil "~{~a-~}" '(1 2 3)) "1-2-3-")
    (chk "fmt-citer" (format nil "~:{[~a ~a]~}" '((1 2) (3 4))) "[1 2][3 4]")
    (chk "fmt-aiter" (format nil "~@{~a+~}" 1 2 3) "1+2+3+")
    ;; struct printing under stress (slot loop re-derives st)
    (let ((str (format nil "~s" (make-t2s :a (list 1) :b "x"))))
      (chk "struct-print" (and (search "T2S" str) t) t))
    ;; reader feature conditionals (saved_pkg root)
    #+common-lisp (chk "feat+" (list :plus) '(:plus))
    #-(or) (chk "feat-" (list :minus) '(:minus))
    ;; package ops: nicknames/use/export/rename cursors
    (let ((p (make-package "T2-PKG-A" :nicknames '("T2-NICK-A")
                           :use '("COMMON-LISP"))))
      (let ((syms (list (intern "T2-X" p) (intern "T2-Y" p))))
        (export syms p)
        (chk "pkg-export"
             (list (nth-value 1 (find-symbol "T2-X" "T2-NICK-A"))
                   (nth-value 1 (find-symbol "T2-Y" "T2-PKG-A")))
             '(:external :external)))
      (rename-package p "T2-PKG-B" '("T2-NICK-B"))
      (chk "pkg-rename" (and (find-package "T2-NICK-B") t) t)))
  (format t "T2-FORMS:~a~%" ok))
LISPEOF
out=$(run_stress "$WORK/tier2-forms.lisp")
check_contains "tier-2 compiler/format/package/reader paths correct under GC stress" "T2-FORMS:T" "$out"
check_absent   "no tier-2 compile-time corruption under GC stress" \
  "T2F-BAD\|Unbound variable\|Undefined function\|corrupted pointer\|malformed" "$out"

# --- Dead-bytecode finalization (gc_finalize_dead TYPE_BYTECODE) -----------
# Ephemeral compiled closures: each iteration compiles a fresh lambda, calls
# it, then drops the only reference, so the bytecode object dies and is
# swept+finalized under the next compaction (forced every alloc under
# stress). Exercises the TYPE_BYTECODE case in gc_finalize_dead — a dead
# bytecode's native_code/native_relocs are freed there (NULL on host since
# JIT_M68K is Amiga-only, but the finalizer branch and NULLing still run for
# every swept bytecode, so a double-free/UAF regression would still corrupt
# the heap here).
cat > "$WORK/bytecode-fin.lisp" <<'LISPEOF'
(let ((ok t))
  (dotimes (i 200)
    (unless (= (funcall (eval (list 'lambda '(x) (list '+ 'x i))) 10) (+ 10 i))
      (setf ok nil)))
  (ext:gc-compact)
  (dotimes (i 50)
    (unless (= (1+ i) (+ i 1)) (setf ok nil)))
  (format t "BCFIN:~a~%" ok))
LISPEOF
out=$(run_stress "$WORK/bytecode-fin.lisp")
check_contains "ephemeral compiled closures die and finalize cleanly under GC stress" "BCFIN:T" "$out"
check_absent   "no corruption from dead-bytecode TYPE_BYTECODE finalizer under GC stress" \
  "corrupted pointer\|not of type\|Guru\|double free" "$out"

# --- Tier-3 audit: numeric tower with big operands under GC stress ----------
# Every operation below crosses at least one allocating call while holding a
# bignum/ratio/complex intermediate (audit 2026-07 tier 3, batch A): negate/
# abs raw-limb memcpy after cl_make_bignum, mod's divisor deref after
# bignum_from_limbs, ash right-shift floor check through a stale limb ptr,
# isqrt's protect-after-alloc, ratio +/- nested-mul temps, ratio negate/abs
# stale CL_Ratio*, complex sqrt/expt nested float temps, do_rounding's
# GC-invisible pair[] and q/r staleness, mod/rem ratio paths protecting
# already-stale values, signum/lcm/max/dpb/boole stale operand copies, and
# RANDOM's PRNG-state WRITE through a dangling pointer after cl_make_bignum.
# Expected values verified by hand against exact arithmetic.
cat > "$WORK/tier3-numeric.lisp" <<'LISPEOF'
(let ((ok t))
  (macrolet ((chk (name form want)
               `(let ((got ,form))
                  (unless (equalp got ,want)
                    (format t "T3N-BAD ~a:~s~%" ,name got)
                    (setf ok nil)))))
    (dotimes (i 3)
      (chk "neg-big"   (- (expt 10 30)) (* -1 (expt 10 30)))
      (chk "abs-big"   (abs (- (expt 10 30))) (expt 10 30))
      (chk "mod-big"   (mod (- (1+ (expt 10 30))) (expt 10 25))
                       (1- (expt 10 25)))
      (chk "ash-floor" (ash (- (1+ (expt 2 100))) -3) (- (1+ (expt 2 97))))
      (chk "isqrt-big" (isqrt (expt 10 30)) (expt 10 15))
      (chk "ratio-add" (+ 12345678901234567890/7 1/3)
                       37037036703703703677/21)
      (chk "ratio-sub" (- 12345678901234567890/7 1/3)
                       37037036703703703663/21)
      (chk "ratio-neg" (- 12345678901234567890/7) -12345678901234567890/7)
      (chk "ratio-abs" (abs -12345678901234567890/7) 12345678901234567890/7)
      (chk "sqrt-cx"   (sqrt #C(-4.0 0.0)) #C(0.0 2.0))
      (chk "sqrt-neg"  (sqrt -4.0) #C(0.0 2.0))
      (chk "expt-neg"  (< (abs (- (imagpart (expt -4.0 0.5)) 2.0)) 1.0e-5) t)
      (chk "floor-1r"  (multiple-value-list (floor 12345678901234567890/7))
                       (list 1763668414462081127 1/7))
      (chk "ceil-1r"   (multiple-value-list (ceiling -12345678901234567890/7))
                       (list -1763668414462081127 -1/7))
      (chk "round-2r"  (round 12345678901234567890/7 3) 587889471487360376)
      (chk "fround-2r" (multiple-value-bind (q r) (ffloor 12345678901234567890/7 3)
                         (and (floatp q) r))
                       15/7)
      (chk "mod-ratio" (mod 12345678901234567890/7 3/2) 9/14)
      (chk "rem-ratio" (rem -12345678901234567890/7 3/2) -9/14)
      (chk "signum"    (signum -3.5) -1.0)
      (chk "lcm-big"   (lcm (expt 2 70) 243) (* (expt 2 70) 243))
      (chk "gcd-big"   (gcd (expt 2 70) (expt 2 65)) (expt 2 65))
      (chk "max-mixed" (max 1.5 12345678901234567890) 12345678901234567890)
      (chk "min-mixed" (min 1.5 12345678901234567890) 1.5)
      (chk "dpb-64"    (dpb -1 (byte 64 0) 0) (1- (expt 2 64)))
      (chk "ldb-64"    (ldb (byte 64 0) (1- (expt 2 64))) (1- (expt 2 64)))
      (chk "maskf-64"  (mask-field (byte 64 4) (1- (expt 2 68)))
                       (- (1- (expt 2 68)) 15))
      (chk "depf-64"   (deposit-field 0 (byte 64 0) (1- (expt 2 68)))
                       (* 15 (expt 2 64)))
      (chk "andc1-big" (boole boole-andc1 (expt 2 70) (+ (expt 2 70) 5)) 5)
      (chk "andc2-big" (boole boole-andc2 (+ (expt 2 70) 5) (expt 2 70)) 5)
      (chk "orc1-big"  (boole boole-orc1 -1 (expt 2 70)) (expt 2 70))
      (chk "orc2-big"  (boole boole-orc2 (expt 2 70) -1) (expt 2 70))
      ;; RANDOM with a bignum limit: pre-fix, xorshift128_next WROTE the PRNG
      ;; state through a pointer dangling after cl_make_bignum — heap smash.
      (let ((r (random (expt 2 64))))
        (chk "random-big" (and (integerp r) (>= r 0) (< r (expt 2 64))) t))
      ;; make-random-state copies the seed through a re-derived source ptr
      (let ((s (make-random-state)))
        (chk "mrs" (random-state-p s) t)
        (let ((s2 (make-random-state s)))
          (chk "mrs-copy" (random-state-p s2) t)))))
  (format t "T3-NUMERIC:~a~%" ok))
LISPEOF
out=$(run_stress "$WORK/tier3-numeric.lisp")
check_contains "tier-3 numeric-tower fixes survive big operands under GC stress" "T3-NUMERIC:T" "$out"
check_absent   "no tier-3 numeric corruption under GC stress" \
  "T3N-BAD\|corrupted pointer\|not of type\|Guru" "$out"

# --- Tier-3 audit: typep/coerce/subtypep with allocating type walks ---------
# Every check crosses an allocating call while holding obj/type-spec locals
# (audit 2026-07 tier 3, batch B): deftype expander cl_vm_apply in
# typep_symbol/typep_check/bi_subtypep (stale obj/type2), OR/AND/MEMBER walk
# cursors across recursion, bignum range arithmetic in unsigned-byte/
# signed-byte/integer/mod specs, coerce sequence converters deriving source
# pointers before the result alloc, and the recursive complex-coerce calls
# whose C-array args were unrooted.
cat > "$WORK/tier3-types.lisp" <<'LISPEOF'
(deftype t3-small () '(integer 0 10))
(deftype t3-pair (a b) `(cons ,a ,b))
(let ((ok t))
  (macrolet ((chk (name form want)
               `(let ((got ,form))
                  (unless (equalp got ,want)
                    (format t "T3T-BAD ~a:~s~%" ,name got)
                    (setf ok nil)))))
    (dotimes (i 3)
      (chk "tp-dt"    (typep 5 't3-small) t)
      (chk "tp-param" (typep '(1 . a) '(t3-pair integer symbol)) t)
      (chk "tp-or"    (typep "x" '(or t3-small string)) t)
      (chk "tp-and"   (typep 5 '(and t3-small (integer 3 7))) t)
      (chk "tp-ratio" (typep 2/3 '(rational 0 1)) t)
      (chk "tp-ub"    (typep (1- (expt 2 64)) '(unsigned-byte 64)) t)
      (chk "tp-ub-no" (typep (expt 2 64) '(unsigned-byte 64)) nil)
      (chk "tp-sb"    (typep (- (expt 2 40)) '(signed-byte 64)) t)
      (chk "tp-int"   (typep (expt 2 70) (list 'integer 0 (expt 2 71))) t)
      (chk "tp-cx"    (typep #C(1 2) '(complex t3-small)) t)
      (chk "tp-cons"  (typep '(1 . 2) '(cons t3-small *)) t)
      (chk "co-str"   (coerce '(#\a #\b #\c) 'string) "abc")
      (chk "co-vstr"  (coerce #(#\a #\b) 'string) "ab")
      (chk "co-bv"    (coerce '(1 0 1) 'bit-vector) #*101)
      (chk "co-list"  (coerce #(1 2 3) 'list) '(1 2 3))
      (chk "co-lbv"   (coerce #*101 'list) '(1 0 1))
      (chk "co-lstr"  (coerce "abc" 'list) '(#\a #\b #\c))
      (chk "co-vec"   (coerce "ab" 'vector) #(#\a #\b))
      (chk "co-vbv"   (coerce #*10 'vector) #(1 0))
      (chk "co-vlist" (coerce '(1 2) 'vector) #(1 2))
      (chk "co-fn"    (funcall (coerce '(lambda (x) (* x 2)) 'function) 21) 42)
      (chk "co-cx"    (coerce 2 '(complex single-float)) #C(2.0 0.0))
      (chk "st-dt"    (multiple-value-list (subtypep 't3-small 'integer))
                      '(t t))
      (chk "st-dt2"   (subtypep 't3-small '(vector t)) nil)
      (chk "st-or"    (subtypep '(or t3-small string) '(or string integer)) t)
      (chk "st-and"   (subtypep '(and integer (not real)) nil) t)
      (chk "st-mem"   (subtypep '(member 1 2) 't3-small) t)
      (chk "st-not"   (subtypep 'null '(not null)) nil)
      ;; equalp on complex went through the ordered comparator (always NIL
      ;; for floats) — must use numeric = per CLHS
      (chk "eqp-cx"   (equalp (sqrt -4.0) #C(0.0 2.0)) t)
      (chk "eqp-cxr"  (equalp #C(1/3 2/7) #C(1/3 2/7)) t)))
  (format t "T3-TYPES:~a~%" ok))
LISPEOF
out=$(run_stress "$WORK/tier3-types.lisp")
check_contains "tier-3 typep/coerce/subtypep fixes survive GC stress" "T3-TYPES:T" "$out"
check_absent   "no tier-3 type-system corruption under GC stress" \
  "T3T-BAD\|corrupted pointer\|not of type\|Guru" "$out"

# --- Tier-3 audit: package mutation / bit-vector ops / plist under stress ---
# (audit 2026-07 tier 3, batch C): bitvec_binop/bit_not read source data
# through pointers derived BEFORE resolve_result's allocation; package.c
# import/export/use-package/add-package-local-nickname stored new conses
# through pre-compaction CL_Package*/table pointers; (setf get) nested
# conses left the indicator stale; the REPL history fix (+/++ after eval
# compaction) is exercised implicitly by every --eval below.
cat > "$WORK/tier3-pkgbits.lisp" <<'LISPEOF'
(defpackage :t3s-a (:use :cl))
(defpackage :t3s-b (:use :cl))
(let ((ok t))
  (macrolet ((chk (name form want)
               `(let ((got ,form))
                  (unless (equalp got ,want)
                    (format t "T3P-BAD ~a:~s~%" ,name got)
                    (setf ok nil)))))
    (dotimes (i 3)
      (chk "bit-and"  (bit-and #*10101100 #*11001010) #*10001000)
      (chk "bit-ior"  (bit-ior #*1010 #*1100) #*1110)
      (chk "bit-xor"  (bit-xor #*1010 #*1100) #*0110)
      (chk "bit-eqv"  (bit-eqv #*1010 #*1100) #*1001)
      (chk "bit-not"  (bit-not #*1010) #*0101)
      (chk "bit-t"    (let ((a (copy-seq #*1010))) (bit-and a #*1100 t) a)
                      #*1000)
      ;; import an inherited symbol then export it (import+export cons
      ;; stores through re-derived package pointers)
      (let ((s (intern (format nil "T3SYM~d" i) :t3s-a)))
        (export s :t3s-a)
        (chk "export" (nth-value 1 (find-symbol (symbol-name s) :t3s-a))
             :external)
        (import s :t3s-b)
        (chk "import" (eq (find-symbol (symbol-name s) :t3s-b) s) t))
      ;; use-package prepends to the use-list through a re-derived pkg
      (let ((pu (make-package (format nil "T3USE~d" i) :use nil)))
        (use-package :t3s-a pu)
        (chk "use-pkg" (and (member (find-package :t3s-a)
                                    (package-use-list pu)) t) t)
        (clamiga::add-package-local-nickname (format nil "N~d" i) :t3s-a pu)
        (delete-package pu))
      (shadow (format nil "T3SHADOW~d" i) :t3s-b)
      ;; (setf get) with a NEW indicator prepends two conses to the plist
      (let ((sym (intern (format nil "T3PL~d" i) :t3s-b)))
        (setf (get sym 'k1) (list i))
        (setf (get sym 'k2) (list (* i 2)))
        (chk "setf-get" (list (get sym 'k1) (get sym 'k2))
             (list (list i) (list (* i 2)))))))
  (format t "T3-PKGBITS:~a~%" ok))
LISPEOF
out=$(run_stress "$WORK/tier3-pkgbits.lisp")
check_contains "tier-3 package/bit-vector fixes survive GC stress" "T3-PKGBITS:T" "$out"
check_absent   "no tier-3 package/bit-vector corruption under GC stress" \
  "T3P-BAD\|corrupted pointer\|not of type\|Guru" "$out"

# --- Tier-3 audit: condition creation/display + describe/inspect ------------
# (audit 2026-07 tier 3, batch D): make-condition/coerce_to_condition held a
# stale type symbol across the initarg cons loop + initform applies and baked
# it into the condition; the bi_error family handed a stale cond to the
# debugger after a declining handler allocated; bi_warn's muffle path had a
# LIFO root bug (lazy muffle_handler protect) AND never restored the handler
# active mask on the muffle longjmp (functional: only the first of several
# warns under one handler-bind was caught); slot-add stored through a
# pre-compaction condition pointer; describe read struct/condition fields
# through pointers staled by PRINT-OBJECT hook prints.
cat > "$WORK/tier3-conds.lisp" <<'LISPEOF'
(define-condition t3d-cond (error)
  ((a :initarg :a :reader t3d-a :initform (list 41))
   (b :initarg :b :reader t3d-b))
  (:default-initargs :b 7)
  (:report (lambda (c s) (format s "t3d a=~a" (t3d-a c)))))
(defstruct t3d-pt x y)
(let ((ok t))
  (macrolet ((chk (name form want)
               `(let ((got ,form))
                  (unless (equalp got ,want)
                    (format t "T3D-BAD ~a:~s~%" ,name got)
                    (setf ok nil)))))
    (dotimes (i 3)
      (chk "mkcond"    (t3d-a (make-condition 't3d-cond)) '(41))
      (chk "mkcond-di" (t3d-b (make-condition 't3d-cond)) 7)
      (chk "err-sym"   (handler-case (error 't3d-cond :a (list i))
                         (t3d-cond (c) (list (t3d-a c) (t3d-b c))))
                       (list (list i) 7))
      (chk "err-decl"  (handler-case
                           (handler-bind ((error (lambda (c)
                                                   (declare (ignore c)) nil)))
                             (error "boom ~a" i))
                         (simple-error (c)
                           (and (search (format nil "boom ~a" i)
                                        (format nil "~a" c)) t)))
                       t)
      ;; three warns under ONE handler-bind — pre-fix only the first was
      ;; caught (active mask not restored across the muffle longjmp)
      (chk "warn-x3"   (let ((n 0))
                         (handler-bind ((warning (lambda (c)
                                                   (declare (ignore c))
                                                   (incf n)
                                                   (muffle-warning))))
                           (warn "w1") (warn "w2") (warn "w3"))
                         n)
                       3)
      (chk "slot-add"  (let ((c (make-condition 't3d-cond :a 1)))
                         (setf (slot-value c 'fresh-slot) (list i))
                         (slot-value c 'fresh-slot))
                       (list i))
      (chk "iri"       (restart-case
                           (invoke-restart-interactively
                            (find-restart 'use-these))
                         (use-these (&rest vals)
                           :interactive (lambda () (list 1 2 3))
                           vals))
                       '(1 2 3))
      (chk "desc-st"   (and (with-output-to-string (s)
                              (describe (make-t3d-pt :x (list i) :y "why") s))
                            t) t)
      (chk "desc-cond" (and (with-output-to-string (s)
                              (describe (make-condition 't3d-cond) s))
                            t) t)
      (chk "desc-vec"  (and (with-output-to-string (s)
                              (describe (vector 1 "two" 3.0) s))
                            t) t)
      (chk "desc-sym"  (and (with-output-to-string (s) (describe 'car s)) t) t)
      (chk "desc-path" (and (with-output-to-string (s)
                              (describe #P"/tmp/x.lisp" s)) t) t)))
  (format t "T3-CONDS:~a~%" ok))
LISPEOF
out=$(run_stress "$WORK/tier3-conds.lisp")
check_contains "tier-3 condition/describe fixes survive GC stress" "T3-CONDS:T" "$out"
check_absent   "no tier-3 condition corruption under GC stress" \
  "T3D-BAD\|corrupted pointer\|not of type\|Guru" "$out"

# --- Tier-4 audit batch 1: compiler stale-local sites -----------------------
# (audit 2026-07 tier 4, batch 1) A cluster of compiler GC bugs where a form
# component (clauses / defaults / cursors) was read before an allocating
# compile_expr/scan and used after, or a struct slot was published to the
# compiler GC walkers before being initialized:
#   C1  compile_tagbody set tb->id via cl_cons while the reused slot still held
#       the previous tagbody's dead cons offset -> the mark/update walkers
#       traced a dangling offset during that alloc's compaction (SIGBUS in
#       cl_alloc; THE dotimes-with-closure crash).  Fixed by tb->id=CL_NIL first.
#   C2/C3 compile_case/compile_typecase protected `clauses` only AFTER
#       compile_expr(keyform) (which macroexpands/allocates) -> stale clause list.
#   C4  compile_lambda &key default restore wrote pre-compaction key-param
#       symbols back into env->locals from a raw C save array -> later key params
#       "Unbound variable".  Now restores from GC-forwarded ll.key_names.
#   C6  %struct-set inline hook read val_form before compile_expr(obj_form).
#   C7  scan_body_for_boxing SETQ/SETF handler walked pairs/pforms + re-used
#       `val` across allocating setf-expander applies unprotected.
#   C8  compile_multiple_value_prog1 staled rest_forms across compile first-form.
#   C10 compile_nth_value dynamic-index staled values_form across compile n.
#   C9  parse_lambda_list &key intern window: key_*[n] stored before n_keys bump
#       (invisible to walkers during cl_intern_keyword) + unprotected cursor.
#   C11/C12 scan_qq_for_boxing / nlx_scan_qq raw vector data + cursor across
#       allocating recursion.
cat > "$WORK/tier4-compiler.lisp" <<'EOF'
(in-package :cl-user)
;; C1: two NLX tagbodies (dotimes bodies that capture a closure) in one form.
(defun t4-c1 ()
  (let ((l '(1 2 3)) (ok t))
    (dotimes (i 4) (mapc (lambda (x) x) l))
    (dotimes (i 20) (let ((v i)) (lambda () v)))
    ok))
(format t "T4-C1:~a~%" (t4-c1))
;; C2/C3: case/typecase whose keyform is a macro call (macroexpands+allocates).
(defmacro t4-key () '(car (list 2)))
(defun t4-c2 ()
  (case (t4-key) (1 :one) (2 :two) (3 :three) (t :other)))
(defun t4-c3 ()
  (typecase (t4-key) (string :str) (integer :int) (t :other)))
(format t "T4-C2:~a~%" (t4-c2))
(format t "T4-C3:~a~%" (t4-c3))
;; C4: &key default forms that macroexpand/allocate; later key must resolve.
(defmacro t4-def () '(list 1 2 3))
(defun t4-c4 (&key (a (t4-def)) b (c (length (t4-def))))
  (list a b c))
(format t "T4-C4:~a~%" (t4-c4 :b 9))
;; C6: (setf (struct-accessor obj) val) where the object expr allocates.
(defstruct t4-pt x y)
(defun t4-c6 ()
  (let ((ps (list (make-t4-pt :x 0 :y 0))))
    (setf (t4-pt-x (car (progn (make-list 8) ps))) 42)
    (t4-pt-x (car ps))))
(format t "T4-C6:~a~%" (t4-c6))
;; C7: setf of an outer var hidden behind an allocating expansion, inside a
;; closure (boxing pre-scan walks the setf pair + re-reads val).
(defun t4-c7 ()
  (let ((acc 0))
    (flet ((bump () (setf acc (+ acc (length (make-list 3))))))
      (bump) (bump) acc)))
(format t "T4-C7:~a~%" (t4-c7))
;; C8: multiple-value-prog1 with an allocating first form and trailing forms.
(defun t4-c8 ()
  (multiple-value-list
    (multiple-value-prog1 (values 1 2 3)
      (make-list 5) (make-list 5))))
(format t "T4-C8:~a~%" (t4-c8))
;; C10: nth-value with a dynamic (non-constant) index.
(defun t4-c10 (n)
  (nth-value n (values :a :b :c)))
(format t "T4-C10:~a~%" (t4-c10 (car (list 2))))
;; C9: &key whose keyword has (almost certainly) never been interned before.
(defun t4-c9 (&key t4-never-before-interned-kw)
  t4-never-before-interned-kw)
(format t "T4-C9:~a~%" (t4-c9 :t4-never-before-interned-kw :ok))
;; C11/C12: quasiquote with a vector template + splices, driven by a macro.
(defmacro t4-qq (&rest xs) `(vector 0 ,@xs (+ ,@xs)))
(defun t4-c11 () (t4-qq 3 4 5))
(format t "T4-C11:~a~%" (coerce (t4-c11) 'list))
EOF
out=$(run_stress "$WORK/tier4-compiler.lisp")
check_contains "C1 nested NLX tagbody + closure compiles (no SIGBUS)"  "T4-C1:T"          "$out"
check_contains "C2 case with macro keyform under GC stress"            "T4-C2:TWO"        "$out"
check_contains "C3 typecase with macro keyform under GC stress"        "T4-C3:INT"        "$out"
check_contains "C4 &key allocating defaults, later key resolves"       "T4-C4:((1 2 3) 9 3)" "$out"
check_contains "C6 struct-setf with allocating object expr"            "T4-C6:42"         "$out"
check_contains "C7 setf outer var behind alloc in closure (boxing)"    "T4-C7:6"          "$out"
check_contains "C8 multiple-value-prog1 keeps all values"              "T4-C8:(1 2 3)"    "$out"
check_contains "C10 nth-value dynamic index under GC stress"           "T4-C10:C"         "$out"
check_contains "C9 fresh &key keyword interns cleanly under stress"    "T4-C9:OK"         "$out"
check_contains "C11 quasiquote vector template + splice under stress"  "T4-C11:(0 3 4 5 12)" "$out"
check_absent   "no unbound/undefined/CDR-type-error in tier-4 compiler cases" \
  "Unbound variable\|Undefined\|not of type\|corrupted\|Guru" "$out"

# --- Tier-4 audit batch 2: format stack smash + printer element loops -------
# (audit 2026-07 tier 4, batch 2)
#   FS1 fmt_padded_integer comma loop had no bounds check: 101 digits with
#       comma-interval 1 wrote 201 bytes into char with_commas[192] (SMASH).
#   FS2 fmt_recursive ~? string-control snapshot sub_args[64] was not rooted
#       across fmt_run (sibling of the fixed ~{~} bug).
#   P1  printer TYPE_STRUCT: cl_struct_slot_names ALLOCATES but ran before the
#       protect; type name + n_slots then read through the stale struct ptr.
#   P2/P3 vector/#nA element loops held a raw cl_vector_data pointer across
#       recursive print_obj (struct elements always allocate slot names).
#   P4  try_pprint_dispatch: cl_typep can run a deftype expander (allocates);
#       cursor/best_fn/caller obj went stale.  Also: in buffer mode the
#       dispatch fn was handed a NIL stream and its output silently dropped.
#   P5  TYPE_RESTART report-function path never re-derived r/obj across three
#       allocating calls; wide report text degraded to #<RESTART>.
#   P6  saved printer_stream / *print-escape* restores were unprotected C
#       locals across the print (stale offsets written back on restore).
#   FS4 render_integer stale-restored *print-base*/*print-radix*.
#   P7  TYPE_COMPLEX/TYPE_RATIO read components through a raw ptr mid-print.
#   P10 longjmp out of a print hook / pprint-dispatch fn leaked pr_depth,
#       pr_inprog_top, pprint_dispatch_active, circle_active (dispatch
#       permanently disabled, "#<...>" markers).  Now restored via
#       CL_ErrorFrame/CL_NLXFrame printer_mark.
cat > "$WORK/tier4-printer.lisp" <<'EOF'
(in-package :cl-user)
;; P1/P2: vector of structs — every element print allocates (slot names).
(defstruct t4b-pt x y)
(format t "T4B-P2:~a~%"
        (format nil "~S" (vector (make-t4b-pt :x 1 :y 2) (make-t4b-pt :x 3 :y 4))))
;; P3: multi-dimensional array of structs (print_array_slice re-derive).
(format t "T4B-P3:~a~%"
        (format nil "~S" (make-array '(2 2)
                          :initial-contents (list (list (make-t4b-pt :x 1 :y 2) 5)
                                                  (list 6 (make-t4b-pt :x 3 :y 4))))))
;; P1: struct nested inside a struct (slot_names window + slot loop).
(defstruct t4b-box inner tag)
(format t "T4B-P1:~a~%"
        (format nil "~S" (make-t4b-box :inner (make-t4b-pt :x 7 :y 8) :tag :k)))
;; P4: pprint dispatch where cl_typep runs a deftype expander per entry.
(deftype t4b-small () '(integer 0 9))
(set-pprint-dispatch 't4b-small (lambda (s obj) (format s "<small ~D>" obj)))
(format t "T4B-P4:~a~%" (write-to-string 5 :pretty t))
;; P5: restart whose report FUNCTION conses while printing (princ of restart).
(format t "T4B-P5:~a~%"
        (restart-case
            (let ((r (find-restart 't4b-resume)))
              (format nil "~A" r))
          (t4b-resume () :report
                       (lambda (s) (format s "resume-~{~A~}" (list 1 2 3)))
                       nil)))
;; P5 conformance: report emitting non-ASCII (wide string) must still splice.
(format t "T4B-P5W:~a~%"
        (restart-case
            (let ((r (find-restart 't4b-wide)))
              (format nil "~A" r))
          (t4b-wide () :report
                     (lambda (s) (format s "<r~aport>" "ë"))
                     nil)))
;; P6: print-object method that re-enters the printer via (format nil ...).
(defstruct t4b-inner v)
(defmethod print-object ((o t4b-inner) s)
  (format s "[inner ~A]" (format nil "~A" (t4b-inner-v o))))
(format t "T4B-P6:~a~%" (format nil "~A" (make-t4b-inner :v (list 1 2))))
;; P7: ratio with bignum numerator + complex.
(format t "T4B-P7:~a~%" (format nil "~S ~S" (/ (expt 10 30) 3) (complex 1 -2)))
;; FS2: ~? string control with heap args and a trailing parent directive.
(format t "T4B-FS2:~a~%" (format nil "~? ~A" "<~A+~A>" (list (list 1) "x") (list 9)))
;; FS1: comma grouping of a 101-digit bignum (old stack smash).
(format t "T4B-FS1:~a~%" (length (format nil "~,,,1:D" (expt 10 100))))
;; FS4: *print-base*/*print-radix* restore across render_integer.
(format t "T4B-FS4:~a~%" (let ((*print-radix* t) (*print-base* 2))
                           (format nil "~X" 255)
                           (write-to-string 3)))
;; P10: an aborted print must not leak printer state.
(defparameter *t4b-n* 0)
(defstruct t4b-once v)
(defmethod print-object ((o t4b-once) s)
  (incf *t4b-n*)
  (if (= *t4b-n* 1) (error "first print boom") (format s "[ok ~A]" (t4b-once-v o))))
(defparameter *t4b-obj* (make-t4b-once :v 7))
(format t "T4B-P10A:~a~%" (handler-case (format nil "~A" *t4b-obj*) (error () :caught)))
;; same object again: a leaked pr_inprog entry would print #<...> instead.
(format t "T4B-P10B:~a~%" (format nil "~A" *t4b-obj*))
;; a pprint-dispatch fn that errors must not disable dispatch forever.
;; (LET-bind *print-pretty* rather than write-to-string :pretty — the
;; keyword path's global set/restore is skipped on the abort; that restore
;; class is the batch-3 FS12/IO2 item.)
(deftype t4b-neg () '(integer -9 -1))
(set-pprint-dispatch 't4b-neg (lambda (s o) (declare (ignore s o)) (error "dispatch boom")))
(format t "T4B-P10C:~a~%" (handler-case (let ((*print-pretty* t)) (prin1-to-string -5))
                            (error () :caught)))
(format t "T4B-P10D:~a~%" (let ((*print-pretty* t)) (prin1-to-string 5)))
;; IO5 (pulled forward from batch 3): set-pprint-dispatch removal-path rebuild
;; conses under stress; the surviving entry must still dispatch.
(set-pprint-dispatch 't4b-neg nil)
(format t "T4B-IO5:~a ~a~%"
        (let ((*print-pretty* t)) (prin1-to-string -5))
        (let ((*print-pretty* t)) (prin1-to-string 5)))
EOF
out=$(run_stress "$WORK/tier4-printer.lisp")
check_contains "P2 vector of structs prints under GC stress" \
  "T4B-P2:#(#S(T4B-PT :X 1 :Y 2) #S(T4B-PT :X 3 :Y 4))" "$out"
check_contains "P3 #2A array of structs prints under GC stress" \
  "T4B-P3:#2A((#S(T4B-PT :X 1 :Y 2) 5) (6 #S(T4B-PT :X 3 :Y 4)))" "$out"
check_contains "P1 nested struct slot names print under GC stress" \
  "T4B-P1:#S(T4B-BOX :INNER #S(T4B-PT :X 7 :Y 8) :TAG :K)" "$out"
check_contains "P4 pprint dispatch via deftype + buffer-mode capture" \
  "T4B-P4:<small 5>" "$out"
check_contains "P5 restart report function output under GC stress" \
  "T4B-P5:resume-123" "$out"
check_contains "P5W wide restart report splices" \
  "T4B-P5W:<r" "$out"
check_absent   "P5W not degraded to #<RESTART>" \
  "T4B-P5W:#<RESTART" "$out"
check_contains "P6 nested print via print-object method" \
  "T4B-P6:\[inner (1 2)\]" "$out"
check_contains "P7 bignum ratio + complex print under GC stress" \
  "T4B-P7:1000000000000000000000000000000/3 #C(1 -2)" "$out"
check_contains "FS2 ~? string-control args survive fmt_run compaction" \
  "T4B-FS2:<(1)+x> (9)" "$out"
check_contains "FS1 101-digit comma grouping (no stack smash)" \
  "T4B-FS1:201" "$out"
check_contains "FS4 print-base/radix restored across render_integer" \
  "T4B-FS4:#b11" "$out"
check_contains "P10 aborted print caught" "T4B-P10A:CAUGHT" "$out"
check_contains "P10 no leaked pr_inprog marker on reprint" \
  "T4B-P10B:\[ok 7\]" "$out"
check_contains "P10 erroring dispatch fn caught" "T4B-P10C:CAUGHT" "$out"
check_contains "P10 pprint dispatch still enabled after aborted dispatch" \
  "T4B-P10D:<small 5>" "$out"
check_contains "IO5 set-pprint-dispatch removal rebuild under GC stress" \
  "T4B-IO5:-5 <small 5>" "$out"
check_absent   "no corruption in tier-4 printer/format cases" \
  "T4B-BAD\|corrupted pointer\|not of type\|Guru\|SIGSEGV" "$out"

# --- Tier-4 audit batch 3: IO / strings / reader ----------------------------
# (audit 2026-07 tier 4, batch 3)
#   IO2/IO3/FS12 write/pprint/write-to-string saved the *PRINT-* controls in
#       unrooted C locals across the (compacting) print — the restore wrote
#       stale offsets back into the control symbols, corrupting *PRINT-BASE*
#       etc. for the rest of the session.  Now a shared rooted-snapshot helper
#       (cl_print_controls_save/restore).  bi_write also returned a stale obj.
#   IO4  read-delimited-list held `stream` unprotected across nested reads.
#   IO6/IO7 copy-pprint-dispatch / pprint-dispatch walked cursors across
#       allocating cl_cons / cl_typep (deftype expander) calls.
#   IO8  require walked the pathnames list + a C-local load_args slot across
#       bi_load (which compacts massively and re-reads args[0]).
#   IO9  macroexpand held form/env across expander applies; macroexpand-1's
#       expanded-p EQ test compared against a stale form copy.
#   IO10 disassemble read through a stale CL_Bytecode* while the output
#       stream (string/Gray) allocated.
#   IO12 bi_load/compile-file-pathname passed an unrooted C-local merge_args
#       slot to allocating bi_merge_pathnames.
#   FS10 string-trim family read the char-bag from args[0] BEFORE the coerce
#       of a char designator allocated.
#   FS11 concatenate compound result-type staled `rest` across the deftype-
#       expanding element-type checks.
#   R1   #nA reader flattened nested lists with unprotected work[]/cursor.
#   R2/R3/R4 nested-read save/restore staled saved_stream/saved_uninterned.
#   R5   feature keyword roots registered only after all three interns.
#   R6   (conformance) quote/backquote/#'/dotted-cdr/#nA embedded the 0x06
#       CL_READER_SKIP sentinel when a false #+/#- preceded the sub-form.
mkdir -p "$WORK"
cat > "$WORK/t4io-mod.lisp" <<'EOF'
(if (boundp 'cl-user::*t4io-mod-loads*)
    (incf cl-user::*t4io-mod-loads*)
    (defparameter cl-user::*t4io-mod-loads* 1))
(provide "t4io-mod")
EOF
cat > "$WORK/tier4-io.lisp" <<'EOF'
(in-package :cl-user)
;; IO2: write keyword overrides restore *print-base* and return the object.
(format t "T4C-IO2:~a ~a ~a~%"
        (let ((s (make-string-output-stream)))
          (write 255 :stream s :base 16)
          (get-output-stream-string s))
        (write-to-string 255)
        (let ((s (make-string-output-stream))) (write 7 :stream s)))
;; FS12: write-to-string keyword overrides restore.
(format t "T4C-FS12:~a ~a~%" (write-to-string 255 :base 16) (write-to-string 255))
;; IO3: pprint restores the caller's *print-escape*/*print-pretty* bindings.
(format t "T4C-IO3:~a~%"
        (let ((*print-escape* nil))
          (let ((s (make-string-output-stream))) (pprint '(1 2) s))
          (if *print-escape* :leaked :restored)))
;; IO4: read-delimited-list conses per element under stress.
(format t "T4C-IO4:~s~%"
        (with-input-from-string (in "1 2 3 4 5 6 7 8 9 10 ]")
          (read-delimited-list #\] in)))
;; IO7: pprint-dispatch builtin walks entries across deftype-expanding typep.
(deftype t4c-small () '(integer 0 9))
(deftype t4c-big () '(integer 100 999))
(set-pprint-dispatch 't4c-small (lambda (s o) (format s "[sm ~d]" o)))
(set-pprint-dispatch 't4c-big (lambda (s o) (format s "[bg ~d]" o)))
(format t "T4C-IO7:~a~%"
        (let ((fn (pprint-dispatch 5)))
          (with-output-to-string (s) (funcall fn s 5))))
;; IO6: copy-pprint-dispatch rebuild loop; copy must still dispatch.
(format t "T4C-IO6:~a ~a~%"
        (length (copy-pprint-dispatch))
        (if (nth-value 1 (pprint-dispatch 5 (copy-pprint-dispatch))) :hit :miss))
;; IO9: macroexpand chains + correct expanded-p secondary values.
(defmacro t4c-m1 (x) (list 't4c-m2 x))
(defmacro t4c-m2 (x) (list 'list x x))
(format t "T4C-IO9:~s ~s ~s~%"
        (multiple-value-list (macroexpand '(t4c-m1 3)))
        (nth-value 1 (macroexpand-1 '(quote z)))
        (nth-value 1 (macroexpand-1 '(t4c-m2 1))))
;; IO10: disassemble to a string stream (allocating output path).
(defun t4c-d (x) (+ x 1))
(format t "T4C-IO10:~a~%"
        (let ((out (with-output-to-string (*standard-output*)
                     (disassemble 't4c-d))))
          (if (and (search "Disassembly" out) (search "Constants" out)) :ok :fail)))
;; IO12: compile-file-pathname merges through the rooted args slot.
(format t "T4C-IO12:~a~%" (pathname-type (compile-file-pathname "t4c-x.lisp")))
;; IO8: require with a LIST of pathnames (two loads of the same module file).
(require "t4io-mod" (list (merge-pathnames "t4io-mod.lisp" *load-pathname*)
                          (merge-pathnames "t4io-mod.lisp" *load-pathname*)))
(format t "T4C-IO8:~a ~a~%" *t4io-mod-loads*
        (if (member "t4io-mod" *modules* :test 'equal) :provided :missing))
;; FS10: char designator char-bag allocates in the coerce.
(format t "T4C-FS10:~a|~a|~a~%"
        (string-trim "x" #\a)
        (string-left-trim '(#\a) "aabca")
        (string-right-trim "A" 'xa))
;; FS11: concatenate with a deftype-compound result type.
(deftype t4c-oct () '(unsigned-byte 8))
(format t "T4C-FS11:~s ~s~%"
        (concatenate '(vector t4c-oct) #(1 2) #(3))
        (concatenate '(vector character) "ab" "c"))
;; R1: #3A flatten conses per element under stress.
(defparameter *t4c-arr*
  (read-from-string "#3A(((1 2) (3 4)) ((5 6) (7 8)))"))
(format t "T4C-R1:~a ~a ~a~%"
        (aref *t4c-arr* 0 0 0) (aref *t4c-arr* 1 0 1) (aref *t4c-arr* 1 1 1))
;; R2/R3: nested read (read-from-string inside #. during an outer read).
(format t "T4C-R2:~s~%"
        (read-from-string "(a #.(cl:read-from-string \"(x y)\") b)"))
;; R5: first cons feature-expressions of the session — the operator-keyword
;; interns inside ensure_feature_keywords can compact while expr is live.
(format t "T4C-R5:~s ~s ~s~%"
        (read-from-string "#+(and) 7")
        (read-from-string "#-(or) 8")
        (read-from-string "#+(and common-lisp (not t4c-no-such-ft)) 9"))
;; R6: false #+/#- must skip cleanly inside quote/function/dotted/array forms.
(format t "T4C-R6:~s ~s ~s ~s~%"
        (read-from-string "'#+t4c-no-such-feature foo bar")
        (read-from-string "#'#+t4c-no-such-feature foo bar")
        (read-from-string "(a . #+t4c-no-such-feature b c)")
        (read-from-string "#2A#+t4c-no-such-feature x ((1 2) (3 4))"))
;; R6b: false #+/#- must also skip cleanly inside #. read-time eval.
(format t "T4C-R6B:~s~%"
        (read-from-string "#.#+t4c-no-such-feature x 5"))
(format t "T4C-DONE~%")
EOF
out=$(run_stress "$WORK/tier4-io.lisp")
check_contains "IO2 write :base override restored + returns object" \
  "T4C-IO2:FF 255 7" "$out"
check_contains "FS12 write-to-string :base override restored" \
  "T4C-FS12:FF 255" "$out"
check_contains "IO3 pprint restores caller print-control bindings" \
  "T4C-IO3:RESTORED" "$out"
check_contains "IO4 read-delimited-list under GC stress" \
  "T4C-IO4:(1 2 3 4 5 6 7 8 9 10)" "$out"
check_contains "IO7 pprint-dispatch walk across deftype typep" \
  "T4C-IO7:\[sm 5\]" "$out"
check_contains "IO6 copy-pprint-dispatch rebuild + copy dispatches" \
  "T4C-IO6:2 HIT" "$out"
check_contains "IO9 macroexpand chain + expanded-p values" \
  "T4C-IO9:((LIST 3 3) T) NIL T" "$out"
check_contains "IO10 disassemble to allocating string stream" \
  "T4C-IO10:OK" "$out"
check_contains "IO12 compile-file-pathname through rooted merge slot" \
  "T4C-IO12:fasl" "$out"
check_contains "IO8 require list-of-pathnames loads all + provides" \
  "T4C-IO8:2 PROVIDED" "$out"
check_contains "FS10 string-trim family char designators" \
  "T4C-FS10:a|bca|X" "$out"
check_contains "FS11 concatenate deftype compound result types" \
  "T4C-FS11:#(1 2 3) \"abc\"" "$out"
check_contains "R1 #3A reader flatten under GC stress" \
  "T4C-R1:1 6 8" "$out"
check_contains "R2 nested read-from-string restores reader state" \
  "T4C-R2:(A (X Y) B)" "$out"
check_contains "R5 first-use cons feature expressions" \
  "T4C-R5:7 8 9" "$out"
check_contains "R6 skip sentinel filtered in quote/fn/dotted/#nA" \
  "T4C-R6:(QUOTE BAR) (FUNCTION BAR) (A . C) #2A((1 2) (3 4))" "$out"
check_contains "R6b skip sentinel filtered in #. read-time eval" \
  "T4C-R6B:5" "$out"
check_contains "tier-4 io/strings/reader cases run to completion" \
  "T4C-DONE" "$out"
check_absent   "no corruption in tier-4 io/strings/reader cases" \
  "corrupted pointer\|not of type\|Guru\|SIGSEGV\|badmark" "$out"

# ============================================================================
# Tier-4 batch 4: sequences / lists / hashtable / VM opcodes under GC stress.
# Each case targets one audited stale-local / unrooted-cursor site; the
# allocating lambdas ((car (list x)) etc.) force a compaction inside every
# user-callback window so pre-fix binaries corrupt deterministically.
# ============================================================================
cat > "$WORK/tier4-seq.lisp" <<'EOF'
(in-package :cl-user)
;; S4: list merge sort roots pred/key across the recursive sorts.
(format t "T4S-S4:~s ~s~%"
        (sort (list 5 3 8 1 9 2 7 6 4)
              (lambda (a b) (< (car (list a)) (car (list b)))))
        (sort (list 3 1 2) #'< :key (lambda (x) (car (list x)))))
;; S6: string/bit-vector insertion sort roots kval across the inner loop
;; (:key returns a fresh heap string here).
(format t "T4S-S6:~a ~s~%"
        (sort (copy-seq "dcba") #'string< :key #'string)
        (sort (copy-seq #*0110)
              (lambda (a b) (< (car (list a)) (car (list b))))))
;; S5: map to string/vector re-reads the seq cursors after the result alloc.
(format t "T4S-S5:~a ~s~%"
        (map 'string #'char-upcase (coerce "hello" 'list))
        (map 'vector (lambda (x) (* x 2)) (list 1 2 3 4 5)))
;; S7: map result-type via deftype expansion + (or ...) branch resolution.
(deftype t4s-str () 'string)
(format t "T4S-S7:~a ~s~%"
        (map 't4s-str #'char-upcase "abc")
        (map '(or symbol (vector t 3)) #'1+ (list 1 2 3)))
;; S8: merge classifies a deftype result-type BEFORE reading operands.
(deftype t4s-lst () 'list)
(format t "T4S-S8:~s ~s~%"
        (merge 't4s-lst (list 1 3 5) (list 2 4 6) #'<)
        (merge 'vector (vector 1 4) (list 2 3) #'<
               :key (lambda (x) (car (list x)))))
;; S1: remove family re-reads elem after the allocating match test.
(format t "T4S-S1:~s ~s~%"
        (remove 3 (list 1 2 3 4 3 5)
                :test (lambda (a b) (eql a (car (list b)))))
        (remove-if (lambda (x) (oddp (car (list x))))
                   (list 1 2 3 4 5 6) :from-end t :count 2))
;; S2/S3: reduce seeds its list cursor from the rooted arg slot.
(format t "T4S-S2:~s ~s ~s~%"
        (reduce #'cons (list 1 2 3 4) :from-end t :initial-value 'end)
        (reduce (lambda (a e) (cons e a)) (list 1 2 3 4) :initial-value nil)
        (reduce #'+ (list 1 2 3) :from-end t
                :key (lambda (x) (car (list x)))))
;; L1: nsubst stores through the per-frame rooted tree after user :test.
(format t "T4S-L1:~s~%"
        (nsubst 'x 'b (list 'a (list 'b 'c) 'b)
                :test (lambda (a b) (eq a (car (list b))))))
;; L2: reverse of vector/bit-vector re-derives from the protected seq.
(format t "T4S-L2:~s ~s~%" (reverse (vector 1 2 3 4 5)) (reverse #*10010))
;; L3: make-list re-reads the heap :initial-element on every cons.
(let ((l (make-list 12 :initial-element (list 'x))))
  (format t "T4S-L3:~a ~s~%"
          (count-if (lambda (e) (equal e '(x))) l) (first l)))
;; AH1: hash iteration (LOOP + maphash, backed by %hash-table-pairs) conses
;; per entry while walking bucket chains.
(let ((h (make-hash-table)))
  (dotimes (i 10) (setf (gethash i h) (* i i)))
  (format t "T4S-AH1:~a ~a~%"
          (loop for v being the hash-values of h sum v)
          (let ((n 0))
            (maphash (lambda (k v) (declare (ignore k v)) (incf n)) h)
            n)))
;; AH2: make-array materializes keywords from rooted args[] slots after the
;; deftype classify (keyword ORDER matters: values before :element-type).
(deftype t4s-oct () '(unsigned-byte 8))
(deftype t4s-any () 't)
(format t "T4S-AH2:~s ~s~%"
        (make-array 4 :initial-contents (list 1 2 3 4)
                      :element-type 't4s-oct)
        (let ((a (make-array (list 2 2) :initial-element (list 7)
                             :element-type 't4s-any)))
          (aref a 1 1)))
;; V2/V3/V4: def* opcodes push the (forwarded) constants-pool entry, not the
;; pre-registrar stale local.
(format t "T4S-V2:~a~%" (defmacro t4s-dm (x) (list 'list x)))
(format t "T4S-V3:~a~%" (deftype t4s-ty () 'integer))
(defun t4s-get (x) (car x))
(defun t4s-set (x v) (setf (car x) v))
(format t "T4S-V4:~a~%" (defsetf t4s-get t4s-set))
;; V1: OP_ASSERT_TYPE re-reads val/type-spec across the deftype-expanding
;; typep and between the condition-slot conses.
(deftype t4s-small () '(integer 0 9))
(format t "T4S-V1:~a ~s~%"
        (the t4s-small 5)
        (handler-case (let ((v 99)) (the t4s-small v))
          (type-error (c) (list (type-error-datum c)
                                (type-error-expected-type c)))))
;; V5/V6: traced user fn (incl. tail-recursive self-call) — the trace prints
;; allocate; func_obj/callee_bc must be re-derived after them.
(defun t4s-tr (n) (if (zerop n) (list 'done) (t4s-tr (1- n))))
(trace t4s-tr)
(format t "T4S-V5:~s~%" (t4s-tr 3))
(untrace t4s-tr)
;; V5b: traced builtin via OP_CALL (f re-derived, result rooted).
(trace char-upcase)
(format t "T4S-V5B:~s~%" (char-upcase #\a))
(untrace char-upcase)
;; V7: traced builtin via OP_APPLY (apply_func rooted, result rooted).
(trace +)
(format t "T4S-V7:~a~%" (apply #'+ 1 2 (list 3 4)))
(untrace +)
(format t "T4S-DONE~%")
EOF
out=$(run_stress "$WORK/tier4-seq.lisp")
check_contains "S4 sort with allocating pred/key" \
  "T4S-S4:(1 2 3 4 5 6 7 8 9) (1 2 3)" "$out"
check_contains "S6 string/bit-vector sort with heap kval" \
  "T4S-S6:abcd #\*0011" "$out"
check_contains "S5 map string/vector cursors re-read after result alloc" \
  "T4S-S5:HELLO #(2 4 6 8 10)" "$out"
check_contains "S7 map deftype + (or ...) result-types" \
  "T4S-S7:ABC #(2 3 4)" "$out"
check_contains "S8 merge deftype result-type classified first" \
  "T4S-S8:(1 2 3 4 5 6) #(1 2 3 4)" "$out"
check_contains "S1 remove family re-reads elem after match test" \
  "T4S-S1:(1 2 4 5) (1 2 4 6)" "$out"
check_contains "S2/S3 reduce cursors from rooted arg slot" \
  "T4S-S2:(1 2 3 4 . END) (4 3 2 1) 6" "$out"
check_contains "L1 nsubst destructive stores through rooted tree" \
  "T4S-L1:(A (X C) X)" "$out"
check_contains "L2 reverse vector/bit-vector re-derives protected seq" \
  "T4S-L2:#(5 4 3 2 1) #\*01001" "$out"
check_contains "L3 make-list heap initial-element rooted" \
  "T4S-L3:12 (X)" "$out"
check_contains "AH1 hash iteration chain cursor rooted" \
  "T4S-AH1:285 10" "$out"
check_contains "AH2 make-array keywords from rooted slots after classify" \
  "T4S-AH2:#(1 2 3 4) (7)" "$out"
check_contains "V2 defmacro pushes forwarded pool entry" \
  "T4S-V2:T4S-DM" "$out"
check_contains "V3 deftype pushes forwarded pool entry" \
  "T4S-V3:T4S-TY" "$out"
check_contains "V4 defsetf pushes forwarded pool entry" \
  "T4S-V4:T4S-GET" "$out"
check_contains "V1 assert-type re-reads across deftype typep" \
  "T4S-V1:5 (99 T4S-SMALL)" "$out"
check_contains "V5 traced tail-recursive fn under stress" \
  "T4S-V5:(DONE)" "$out"
check_contains "V5b traced builtin call under stress" \
  "T4S-V5B:#\\\\A" "$out"
check_contains "V7 traced builtin apply under stress" \
  "T4S-V7:10" "$out"
check_contains "tier-4 sequence/list/hash/vm cases run to completion" \
  "T4S-DONE" "$out"
check_absent   "no corruption in tier-4 sequence/list/hash/vm cases" \
  "corrupted pointer\|not of type\|Guru\|SIGSEGV\|badmark" "$out"

# ---------------------------------------------------------------------------
# Tier-4 batch 7a: fixed C buffers replaced with GC staging (VM stack /
# GC vectors / GC bit-vectors).  Every replacement introduces an allocating
# call into a path that previously performed none — exercise them under
# per-alloc compaction so a missed protect/re-derive corrupts deterministically.
# ---------------------------------------------------------------------------
echo ""
echo "--- tier-4 batch 7a: uncapped apply/format/string staging ---"
cat > "$WORK/tier4-b7a.lisp" <<'EOF'
;; bi_apply VM-stack spread (>64 args through the C APPLY path).
(format t "T4B7-A1:~a~%"
        (funcall #'apply #'+ (make-list 100 :initial-element 1)))
;; remove_from_string keep_vec staging + width-preserving result build,
;; with an allocating :test crossing every element.
(format t "T4B7-S9:~a~%"
        (length (remove #\a (make-string 1200 :initial-element #\b)
                        :test (lambda (a b) (list a b) (char= a b)))))
;; concatenate string staging vector (>4096 result).
(format t "T4B7-S13:~a~%"
        (length (concatenate 'string
                             (make-string 3000 :initial-element #\x)
                             (make-string 2000 :initial-element #\y))))
;; remove-duplicates KEEP bit-vector with allocating :test.
(format t "T4B7-RD:~s~%"
        (remove-duplicates (list 1 2 1 3 2 4)
                           :test (lambda (a b) (list a b) (eql a b))))
;; remove_from_vector / bitvector KEEP bit-vector with allocating :key.
(format t "T4B7-RV:~s ~s~%"
        (remove 2 (vector 1 2 3 2 4) :key (lambda (x) (list x) x))
        (remove 0 (make-array 5 :element-type 'bit
                                :initial-contents '(0 1 0 1 1))
                :key (lambda (b) (list b) b)))
;; REPLACE snapshot GC vector (self-overlap correctness preserved).
(format t "T4B7-RP:~s~%"
        (let ((v (vector 1 2 3 4 5)))
          (replace v v :start1 1 :start2 0 :end2 4)))
;; string sort SORT_TMP GC-vector staging with allocating :key.
(format t "T4B7-SS:~a~%"
        (sort (copy-seq "dcba") #'char< :key (lambda (c) (list c) c)))
;; format ~? / ~:{ / FORMATTER staging on the VM stack past 64 slots.
(let ((ctrl (with-output-to-string (s)
              (dotimes (i 80) (write-string "~A" s)))))
  (format t "T4B7-F1:~a~%"
          (length (format nil "~?" ctrl (make-list 80 :initial-element 7))))
  (format t "T4B7-F2:~a~%"
          (length (with-output-to-string (s)
                    (apply (eval (list 'formatter ctrl)) s
                           (make-list 80 :initial-element 3))))))
(format t "T4B7-F3:~a~%"
        (length (format nil "~:{~@{~A~}~}"
                        (list (make-list 90 :initial-element 1)))))
;; coerce list/vector -> string width preservation (wide builds).
(format t "T4B7-CO:~a~%"
        (char-code (char (coerce (list #\a (code-char 20013)) 'string) 1)))
(format t "T4B7-DONE~%")
EOF
out=$(run_stress "$WORK/tier4-b7a.lisp")
check_contains "B7a apply VM-stack spread past 64 args" "T4B7-A1:100" "$out"
check_contains "B7a remove string keep_vec staging uncapped" "T4B7-S9:1200" "$out"
check_contains "B7a concatenate string staging uncapped" "T4B7-S13:5000" "$out"
check_contains "B7a remove-duplicates KEEP bit-vector" "T4B7-RD:(1 3 2 4)" "$out"
check_contains "B7a remove vector/bitvector KEEP bit-vector" \
  "T4B7-RV:#(1 3 4) #\*111" "$out"
check_contains "B7a replace snapshot GC vector" "T4B7-RP:#(1 1 2 3 4)" "$out"
check_contains "B7a string sort GC-vector staging" "T4B7-SS:abcd" "$out"
check_contains "B7a format ~? staging past 64 args" "T4B7-F1:80" "$out"
check_contains "B7a formatter-inner staging past 64 args" "T4B7-F2:80" "$out"
check_contains "B7a iteration sublist staging past 64 elements" "T4B7-F3:90" "$out"
check_contains "B7a coerce wide char preserved" "T4B7-CO:20013" "$out"
check_contains "tier-4 batch 7a cases run to completion" "T4B7-DONE" "$out"
check_absent   "no corruption in tier-4 batch 7a cases" \
  "corrupted pointer\|not of type\|Guru\|SIGSEGV\|badmark" "$out"

# ---------------------------------------------------------------------------
# Tier-4 batch 7b: hashtable content hashes (wide strings, bit-vectors,
# EQUALP vectors).  The new hash paths run inside gc_rehash_table after every
# compaction — exercise insert/lookup with allocation churn between them so a
# hash/equality mismatch (or a rehash through the new code) misses
# deterministically.
# ---------------------------------------------------------------------------
echo ""
echo "--- tier-4 batch 7b: hashtable content-hash contract ---"
cat > "$WORK/tier4-b7b.lisp" <<'EOF'
(let ((ht (make-hash-table :test 'equal)) (ok t))
  (dotimes (i 40)
    (setf (gethash (format nil "w~A~A" (code-char 20013) i) ht) i))
  (dotimes (i 40)
    (list i i)   ; churn between lookups
    (unless (eql (gethash (format nil "w~A~A" (code-char 20013) i) ht) i)
      (setf ok nil)))
  (format t "T4B7B-W:~a ~a~%" ok (hash-table-count ht)))
(let ((ht (make-hash-table :test 'equal)) (ok t))
  (dotimes (i 20)
    (let ((bv (make-array 8 :element-type 'bit :initial-element 0)))
      (dotimes (j 8) (setf (aref bv j) (if (logbitp j i) 1 0)))
      (setf (gethash bv ht) i)))
  (dotimes (i 20)
    (let ((bv (make-array 8 :element-type 'bit :initial-element 0)))
      (dotimes (j 8) (setf (aref bv j) (if (logbitp j i) 1 0)))
      (unless (eql (gethash bv ht) i) (setf ok nil))))
  (format t "T4B7B-BV:~a~%" ok))
(let ((ht (make-hash-table :test 'equalp)))
  (setf (gethash (vector 1 (list 2) "x") ht) :deep)
  (format t "T4B7B-EQV:~a~%" (gethash (vector 1 (list 2) "x") ht)))
(format t "T4B7B-DONE~%")
EOF
out=$(run_stress "$WORK/tier4-b7b.lisp")
check_contains "B7b wide-string EQUAL keys content-hashed" "T4B7B-W:T 40" "$out"
check_contains "B7b bit-vector EQUAL keys content-hashed" "T4B7B-BV:T" "$out"
check_contains "B7b EQUALP vector keys content-hashed" "T4B7B-EQV:DEEP" "$out"
check_contains "tier-4 batch 7b cases run to completion" "T4B7B-DONE" "$out"
check_absent   "no corruption in tier-4 batch 7b cases" \
  "corrupted pointer\|not of type\|Guru\|SIGSEGV\|badmark" "$out"

echo ""
echo "$passed passed, $failed failed, $total total"
[ "$failed" -eq 0 ]
