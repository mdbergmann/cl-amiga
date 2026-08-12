#!/bin/sh
# Executable specification for EXT.DEV:HANDLE-COMMAND -- the command layer
# behind the ARexx development port (lib/dev-commands.lisp).
#
# The whole reason that layer is a separate, platform-neutral file is so it
# can be tested here, on the host, where there is no ARexx at all.  The
# Amiga side (lib/amiga/arexx.lisp) only moves these strings across a
# message port, and is covered by the arexx block in
# tests/amiga/run-tests.lisp.
#
# What matters most: a LOAD of a file with SEVERAL bad top-level forms must
# come back with a diagnostic for EACH of them, each carrying file:line.
# That is the editor's whole workflow, and it depends on two runtime fixes
# guarded by tests/test_unwind_load.c.
#
# Run: sh tests/test_dev_commands.sh build/host/clamiga

CLAMIGA="${1:-build/host/clamiga}"
passed=0
failed=0
total=0
TMPD=$(mktemp -d "${TMPDIR:-/tmp}/test_dev_commands_XXXXXX") || exit 1
trap 'rm -rf "$TMPD"' EXIT INT TERM

# Each case runs in a fresh image: no FASL cache carry-over between checks.
run_cmd() {
    # $1 = command string (already Lisp-escaped)
    cat <<EOF | "$CLAMIGA" --no-userinit --batch 2>&1
(require "dev-commands")
(multiple-value-bind (rc text) (ext.dev:handle-command "$1")
  (format t "~&<<RC=~d>>~%~a~%<<END>>~%" rc text))
EOF
}

check() {
    desc="$1"; pattern="$2"; out="$3"
    total=$((total + 1))
    if echo "$out" | grep -q "$pattern"; then
        echo "  ok  $desc"
        passed=$((passed + 1))
    else
        echo "  FAIL  $desc (missing: $pattern)"
        echo "    output: $(echo "$out" | head -12)"
        failed=$((failed + 1))
    fi
}

# Run a Lisp script FILE in a fresh image.  Scripts go through a file rather
# than an inline heredoc inside $( ): a heredoc nested in a command
# substitution re-parses quotes, and an apostrophe in Lisp source (quote) is
# enough to break the shell parse.
run_script() {
    "$CLAMIGA" --no-userinit --batch < "$1" 2>&1
}

check_not() {
    desc="$1"; pattern="$2"; out="$3"
    total=$((total + 1))
    if echo "$out" | grep -q "$pattern"; then
        echo "  FAIL  $desc (unexpected: $pattern)"
        echo "    output: $(echo "$out" | head -12)"
        failed=$((failed + 1))
    else
        echo "  ok  $desc"
        passed=$((passed + 1))
    fi
}

# --- fixture files ---------------------------------------------------------

# Three bad forms among good ones: the multi-diagnostic case.
cat > "$TMPD/three-errors.lisp" <<'EOF'
(defun dev-ok-1 () 1)
(error "first bad form")
(defun dev-ok-2 () 2)
(undefined-function-one)
(defun dev-ok-3 () 3)
(undefined-function-two)
(defvar *dev-reached-end* t)
EOF

cat > "$TMPD/clean.lisp" <<'EOF'
(defvar *dev-clean-loaded* t)
EOF

# Unbalanced parens: the half-saved-buffer case.  The reader error takes the
# rest of the file with it, which the reply has to say out loud.
cat > "$TMPD/unbalanced.lisp" <<'EOF'
(defvar *dev-unbalanced-first* t)
(defun broken (
EOF

# --- PING / VERSION --------------------------------------------------------

out=$(run_cmd 'PING')
check "PING returns rc 0"            '<<RC=0>>' "$out"
check "PING answers PONG"            'PONG'     "$out"

out=$(run_cmd 'ping')
check "verbs are case-insensitive"   'PONG'     "$out"

out=$(run_cmd 'VERSION')
check "VERSION returns rc 0"         '<<RC=0>>' "$out"
check "VERSION names the implementation" 'CL-Amiga' "$out"

# --- unknown command -------------------------------------------------------

out=$(run_cmd 'FLURB some args')
check "unknown command is fatal"     '<<RC=20>>'            "$out"
check "unknown command names itself" 'unknown command: FLURB' "$out"
check "unknown command lists verbs"  'Known commands:.*LOAD' "$out"

# --- EVAL ------------------------------------------------------------------

out=$(run_cmd 'EVAL (+ 1 2)')
check "EVAL returns rc 0"            '<<RC=0>>' "$out"
check "EVAL prints the value"        '^3$'      "$out"

out=$(run_cmd '(list 1 2 3)')
check "bare form is evaluated"       '(1 2 3)'  "$out"

out=$(run_cmd 'EVAL (values 1 2)')
check "EVAL shows multiple values"   '1 ; 2'    "$out"

out=$(run_cmd 'EVAL (/ 1 0)')
check "EVAL error gives rc 10"       '<<RC=10>>'      "$out"
check "EVAL error is reported"       'ERROR: division by zero' "$out"
# A form typed into a command string has no source file; inventing one from
# whatever the compiler last stamped would send the editor to a random file.
check_not "EVAL error has no bogus location" 'lisp:[0-9]*: ERROR: division' "$out"

# --- LOAD ------------------------------------------------------------------

out=$(run_cmd "LOAD $TMPD/clean.lisp")
check "clean LOAD returns rc 0"      '<<RC=0>>'              "$out"
check "clean LOAD reports no errors" '0 error(s), 0 warning(s)' "$out"

out=$(run_cmd "LOAD $TMPD/three-errors.lisp")
check "failing LOAD returns rc 10"   '<<RC=10>>'             "$out"
check "LOAD reports ALL three errors" '3 error(s), 0 warning(s)' "$out"
check "first error has file:line"    'three-errors.lisp:2: ERROR: first bad form' "$out"
check "second error has file:line"   'three-errors.lisp:4: ERROR: Undefined function: UNDEFINED-FUNCTION-ONE' "$out"
check "third error has file:line"    'three-errors.lisp:6: ERROR: Undefined function: UNDEFINED-FUNCTION-TWO' "$out"

# Recovery is not just cosmetic: the forms after each bad one must have run.
cat > "$TMPD/reached-end.lisp" <<EOF
(require "dev-commands")
(ext.dev:handle-command "LOAD $TMPD/three-errors.lisp")
(format t "~&<<REACHED-END=~a>>~%" (and (boundp (quote cl-user::*dev-reached-end*)) t))
EOF
out=$(run_script "$TMPD/reached-end.lisp")
check "LOAD kept going to the last form" '<<REACHED-END=T>>' "$out"

out=$(run_cmd "LOAD $TMPD/unbalanced.lisp")
check "reader error returns rc 10"   '<<RC=10>>'  "$out"
check "reader error says it aborted" 'aborted'    "$out"

out=$(run_cmd "LOAD $TMPD/no-such-file-here.lisp")
check "missing file returns rc 10"   '<<RC=10>>'         "$out"
check "missing file is reported"     'does not exist'    "$out"

out=$(run_cmd 'LOAD')
check "LOAD with no argument is fatal" '<<RC=20>>'            "$out"
check "LOAD with no argument explains" 'requires a file name' "$out"

# Editors quote paths that contain spaces.
mkdir -p "$TMPD/dir with space"
cp "$TMPD/clean.lisp" "$TMPD/dir with space/quoted.lisp"
out=$(run_cmd "LOAD \\\"$TMPD/dir with space/quoted.lisp\\\"")
check "quoted path with spaces loads" '0 error(s), 0 warning(s)' "$out"

# --- the port survives a bad command ---------------------------------------
# The handler loop keeps serving after anything a command can do to it; if a
# failing LOAD could take the image down, the editor would lose the session.
cat > "$TMPD/survives.lisp" <<EOF
(require "dev-commands")
(ext.dev:handle-command "LOAD $TMPD/unbalanced.lisp")
(ext.dev:handle-command "EVAL (/ 1 0)")
(ext.dev:handle-command "LOAD $TMPD/no-such-file-here.lisp")
(multiple-value-bind (rc text) (ext.dev:handle-command "PING")
  (format t "~&<<STILL-ALIVE rc=~d ~a>>~%" rc text))
EOF
out=$(run_script "$TMPD/survives.lisp")
check "handler survives a run of bad commands" '<<STILL-ALIVE rc=0 PONG>>' "$out"

# --- LASTRESULT ------------------------------------------------------------
# ARexx only transmits RESULT when rc is 0, so a failing command's text is
# unreachable without this.
cat > "$TMPD/lastresult.lisp" <<EOF
(require "dev-commands")
(ext.dev:handle-command "LOAD $TMPD/three-errors.lisp")
(multiple-value-bind (rc text) (ext.dev:handle-command "LASTRESULT")
  (format t "~&<<RC=~d>>~%~a~%<<END>>~%" rc text))
EOF
out=$(run_script "$TMPD/lastresult.lisp")
check "LASTRESULT returns rc 0"       '<<RC=0>>'  "$out"
check "LASTRESULT replays diagnostics" '3 error(s), 0 warning(s)' "$out"

# --- IN-PACKAGE ------------------------------------------------------------

cat > "$TMPD/in-package.lisp" <<'EOF'
(require "dev-commands")
(defpackage "DEV-CMD-TEST" (:use "CL"))
(ext.dev:handle-command "IN-PACKAGE DEV-CMD-TEST")
(multiple-value-bind (rc text) (ext.dev:handle-command "EVAL (package-name *package*)")
  (format t "~&<<RC=~d>>~%~a~%<<END>>~%" rc text))
EOF
out=$(run_script "$TMPD/in-package.lisp")
check "IN-PACKAGE switches the command package" 'DEV-CMD-TEST' "$out"

out=$(run_cmd 'IN-PACKAGE NO-SUCH-PACKAGE-HERE')
check "IN-PACKAGE rejects unknown packages" 'no such package' "$out"

# --- result cap ------------------------------------------------------------
# An unbounded compiler log cannot be shipped as an ARexx argstring.
cat > "$TMPD/truncate.lisp" <<'EOF'
(require "dev-commands")
(let ((ext.dev:*max-result-length* 200))
  (multiple-value-bind (rc text)
      (ext.dev:handle-command "EVAL (dotimes (i 200) (format t \"filler line ~d~%\" i))")
    (format t "~&<<RC=~d LEN=~d>>~%~a~%" rc (length text) text)))
EOF
out=$(run_script "$TMPD/truncate.lisp")
check "over-long reply is truncated"   'truncated at 200 characters' "$out"

echo ""
echo "test_dev_commands: $passed passed, $failed failed, $total total"
[ "$failed" -eq 0 ]
