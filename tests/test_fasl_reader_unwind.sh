#!/bin/sh
# A non-local exit out of a FASL load must take the loader's reader (and, for
# a source load's auto-cache, its writer) with it.
#
# fasl_load keeps its CL_FaslReader — a C stack local — registered as a GC
# root for the whole load, so forward GENSYM_REF/OBJ_REF targets survive a
# compaction.  An error caught by a C error frame restores the registry, but a
# Lisp HANDLER-CASE, RETURN-FROM or THROW leaves through the VM's NLX landing
# and passes no error frame: the registry kept pointing into dead stack, and
# the next GC marked whatever lived there as a reader (SEGV in
# gc_mark_children at varying addresses) — plus 256 KB of off-heap table
# leaked per abandoned load.  The landing now calls cl_fasl_reader_unwind_to
# next to cl_compiler_unwind_to.
#
# bi_load's own auto-cache registers a second, heap-allocated CL_FaslWriter
# (fw) the same way, for the same reason (its gensym dedup table must survive
# compaction across the interleaved compile/eval/serialize of later forms).
# fw is platform_alloc'd rather than a stack local, so an abandoning longjmp
# does not dangle-pointer-crash it — but nothing else freed fw or its
# fasl_buf/unit_buf for that path either, so it (and both buffers) leaked
# forever and the writer stayed registered until the active-writer table
# filled.  The landing now calls cl_fasl_writer_unwind_to right alongside
# cl_fasl_reader_unwind_to (see the "writer" case below).
#
# A stale registry is made deterministic here through EXT:SAVE-IMAGE, which
# refuses to run while any reader OR writer is registered.  The leak half
# lives in tests/test_memleak_tracked.sh, the compaction half in
# tests/test_gc_stress_regression.sh.
#
# Run: sh tests/test_fasl_reader_unwind.sh [path-to-clamiga]

CLAMIGA="${1:-build/host/clamiga}"
case "$CLAMIGA" in
    /*|[A-Za-z]:/*) ;;
    *) CLAMIGA="$(cd "$(dirname "$CLAMIGA")" && pwd)/$(basename "$CLAMIGA")" ;;
esac

passed=0
failed=0
TMP="${TMPDIR:-/tmp}/clamiga-faslunwind-$$"
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT

check() {
    desc="$1"; cond="$2"
    if [ "$cond" = "yes" ]; then
        echo "  ok  $desc"; passed=$((passed + 1))
    else
        echo "  FAIL  $desc"; failed=$((failed + 1))
        [ -n "$out" ] && echo "$out" | sed 's/^/      | /'
    fi
}
has()  { echo "$out" | grep -q -- "$1" && echo yes || echo no; }
hasnt() { echo "$out" | grep -q -- "$1" && echo no || echo yes; }

# run DIR — load run.lisp from inside DIR; relative names keep the generated
# Lisp free of host paths.
run() {
    out=$(cd "$1" && CLAMIGA_FASL_CACHE_DIR="$1/cache" \
          "$CLAMIGA" --no-userinit --non-interactive --load run.lisp \
          </dev/null 2>&1)
}

# boom.lisp: defines something, then signals with the shared-object table live
# (one literal referenced twice), and never reaches its last form.
write_boom() {
    cat > "$1/boom.lisp" <<'EOF'
(defvar *boom-shared* '(#1=(1 2 3) #1# #2="shared" #2#))
(defun boom-before () :before)
(error "boom from inside a FASL unit")
(defun boom-after () :after)
EOF
}
# SAVE-IMAGE as the probe: :clean, or the refusal text.
PROBE='(defun probe (tag)
  (format t "~A ~A~%" tag
          (handler-case (progn (ext:save-image "probe.img") :clean)
            (error (e) (princ-to-string e)))))'

# --- 1. the three ways out through the landing -------------------------------
d="$TMP/exits"; mkdir -p "$d"; write_boom "$d"
cat > "$d/run.lisp" <<EOF
$PROBE
(compile-file "boom.lisp" :output-file "boom.fasl")
(format t "HC ~S~%" (handler-case (load "boom.fasl") (error (e) (princ-to-string e))))
(probe "AFTER-HC")
(format t "RF ~S~%"
        (block out
          (handler-bind ((error (lambda (e) (declare (ignore e)) (return-from out :returned))))
            (load "boom.fasl"))))
(probe "AFTER-RF")
(format t "TH ~S~%"
        (catch 'tag
          (handler-bind ((error (lambda (e) (declare (ignore e)) (throw 'tag :thrown))))
            (load "boom.fasl"))))
(probe "AFTER-TH")
(format t "UP ~S~%"
        (let ((cleaned nil))
          (list (handler-case (unwind-protect (load "boom.fasl") (setq cleaned t))
                  (error () :caught))
                cleaned)))
(probe "AFTER-UP")
(gc)
(format t "STATE ~S ~S ~S~%" (boom-before) (fboundp 'boom-after) (length *boom-shared*))
(format t "READERS ~S~%" (first (ext:%fasl-registry-stats)))
(format t "EXITS-DONE~%")
EOF
run "$d"
check "handler_case_sees_the_units_error" "$(has 'HC "boom from inside a FASL unit"')"
check "handler_case_drops_the_reader"     "$(has '^AFTER-HC CLEAN')"
check "return_from_lands"                 "$(has '^RF :RETURNED')"
check "return_from_drops_the_reader"      "$(has '^AFTER-RF CLEAN')"
check "throw_lands"                       "$(has '^TH :THROWN')"
check "throw_drops_the_reader"            "$(has '^AFTER-TH CLEAN')"
check "unwind_protect_cleanup_ran"        "$(has '^UP (:CAUGHT T)')"
check "unwind_protect_drops_the_reader"   "$(has '^AFTER-UP CLEAN')"
check "units_before_the_error_took_effect" "$(has '^STATE :BEFORE NIL 4')"
check "gc_after_abandoned_loads_survives" "$(has '^EXITS-DONE')"
check "no_reader_left_registered"         "$(hasnt 'still active')"
check "registry_stats_report_no_reader"   "$(has '^READERS 0')"

# --- 2. a landing INSIDE a live load keeps that load's reader ----------------
# outer.fasl's second unit catches inner's error itself: the landing is
# deeper than outer's fasl_load frame, so only inner's reader may go.  The
# units after it still resolve their shared objects through outer's reader.
d="$TMP/nested"; mkdir -p "$d"; write_boom "$d"
cat > "$d/outer.lisp" <<'EOF'
(defvar *outer-1* '(#1=(a b) #1#))
(defvar *outer-caught* (handler-case (load "boom.fasl") (error () :inner-caught)))
(gc)
(defvar *outer-2* '(#1=(tail) #1# #2=(x y) #2#))
(defun outer-ok () (list *outer-caught* (eq (first *outer-1*) (second *outer-1*))
                         (eq (first *outer-2*) (second *outer-2*))
                         (eq (third *outer-2*) (fourth *outer-2*))))
EOF
cat > "$d/run.lisp" <<EOF
$PROBE
(compile-file "boom.lisp" :output-file "boom.fasl")
(compile-file "outer.lisp" :output-file "outer.fasl")
(format t "LOADED ~S~%" (load "outer.fasl"))
(format t "OUTER ~S~%" (outer-ok))
(probe "AFTER-NESTED")
EOF
run "$d"
check "outer_load_completes"            "$(has '^LOADED T')"
check "outer_units_after_the_landing"   "$(has '^OUTER (:INNER-CAUGHT T T T)')"
check "nested_leaves_nothing_registered" "$(has '^AFTER-NESTED CLEAN')"

# --- 3. the C error frame path (LOAD's per-form recovery) still restores -----
d="$TMP/cframe"; mkdir -p "$d"; write_boom "$d"
cat > "$d/wrapper.lisp" <<'EOF'
(load "boom.fasl")
(format t "WRAPPER-CONTINUED~%")
EOF
cat > "$d/run.lisp" <<EOF
$PROBE
(compile-file "boom.lisp" :output-file "boom.fasl")
(load "wrapper.lisp")
(probe "AFTER-CFRAME")
EOF
run "$d"
check "unhandled_error_is_reported"     "$(has 'boom from inside a FASL unit')"
check "source_load_recovers"            "$(has '^WRAPPER-CONTINUED')"
check "error_frame_drops_the_reader"    "$(has '^AFTER-CFRAME CLEAN')"

# --- 4. a worker thread's abandoned load touches only its own entries --------
d="$TMP/mt"; mkdir -p "$d"; write_boom "$d"
cat > "$d/run.lisp" <<EOF
$PROBE
(compile-file "boom.lisp" :output-file "boom.fasl")
(let ((threads (loop repeat 3 collect
                 (mp:make-thread
                  (lambda ()
                    (loop repeat 5 collect
                      (handler-case (load "boom.fasl") (error () :caught))))))))
  (format t "MT ~S~%" (mapcar #'mp:join-thread threads)))
(gc)
(probe "AFTER-MT")
EOF
run "$d"
check "workers_all_caught" "$(has '^MT ((:CAUGHT :CAUGHT :CAUGHT :CAUGHT :CAUGHT) (:CAUGHT :CAUGHT :CAUGHT :CAUGHT :CAUGHT) (:CAUGHT :CAUGHT :CAUGHT :CAUGHT :CAUGHT))')"
check "workers_leave_nothing_registered" "$(has '^AFTER-MT CLEAN')"

# --- 5. the writer's auto-cache path (a source LOAD) also drops on NLX ------
# bi_load auto-caches a source LOAD by heap-allocating a CL_FaslWriter (fw)
# plus its fasl_buf/unit_buf and registering fw as a GC root for the whole
# load, same rationale as the reader above.  Unlike the reader, fw survives
# an abandoning longjmp (it's platform_alloc'd, not a stack local) — but
# without a landing-side unwind nothing ever freed it or its two buffers, and
# it stayed registered forever (an active-writer slot gone until the table
# filled and every subsequent cached LOAD/COMPILE-FILE started failing). The
# landing now calls cl_fasl_writer_unwind_to next to cl_fasl_reader_unwind_to.
# `write_boom` writes the same boom.lisp used above; here it is loaded
# directly (a SOURCE load) instead of compiled first, so the auto-cache
# writer path is exercised instead of the reader.
d="$TMP/writer"; mkdir -p "$d"; write_boom "$d"
cat > "$d/run.lisp" <<EOF
$PROBE
(format t "HC ~S~%" (handler-case (load "boom.lisp") (error (e) (princ-to-string e))))
(format t "WRITERS-AFTER-HC ~S~%" (second (ext:%fasl-registry-stats)))
(probe "AFTER-WRITER-HC")
(format t "RF ~S~%"
        (block out
          (handler-bind ((error (lambda (e) (declare (ignore e)) (return-from out :returned))))
            (load "boom.lisp"))))
(format t "WRITERS-AFTER-RF ~S~%" (second (ext:%fasl-registry-stats)))
(probe "AFTER-WRITER-RF")
(format t "TH ~S~%"
        (catch 'tag
          (handler-bind ((error (lambda (e) (declare (ignore e)) (throw 'tag :thrown))))
            (load "boom.lisp"))))
(format t "WRITERS-AFTER-TH ~S~%" (second (ext:%fasl-registry-stats)))
(probe "AFTER-WRITER-TH")
(gc)
(format t "WRITER-DONE~%")
EOF
run "$d"
check "writer_handler_case_sees_the_units_error" "$(has 'HC "boom from inside a FASL unit"')"
check "writer_handler_case_drops_the_writer"     "$(has '^WRITERS-AFTER-HC 0')"
check "writer_handler_case_probe_clean"          "$(has '^AFTER-WRITER-HC CLEAN')"
check "writer_return_from_lands"                 "$(has '^RF :RETURNED')"
check "writer_return_from_drops_the_writer"      "$(has '^WRITERS-AFTER-RF 0')"
check "writer_return_from_probe_clean"           "$(has '^AFTER-WRITER-RF CLEAN')"
check "writer_throw_lands"                       "$(has '^TH :THROWN')"
check "writer_throw_drops_the_writer"            "$(has '^WRITERS-AFTER-TH 0')"
check "writer_throw_probe_clean"                 "$(has '^AFTER-WRITER-TH CLEAN')"
check "gc_after_abandoned_writer_loads_survives" "$(has '^WRITER-DONE')"
check "no_writer_left_registered"                "$(hasnt 'still active')"

echo ""
echo "test_fasl_reader_unwind: $passed passed, $failed failed"
[ "$failed" -eq 0 ]
