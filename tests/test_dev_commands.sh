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

# ===========================================================================
# Introspection: ARGLIST / COMPLETE / DESCRIBE / APROPOS / SOURCE-LOCATION /
# MACROEXPAND -- the editor's phase-2 commands.  Every case that names a
# symbol resolves it against the command package without interning, so an
# unknown name is rc 10 with a reason.
# ===========================================================================

# A file of definitions, loaded from its FASL: SOURCE-LOCATION and the
# docstrings must survive COMPILE-FILE + LOAD in a fresh session, which is
# the load-time-call design of specs/documentation-introspection.md.
cat > "$TMPD/intro.lisp" <<'EOF'
(in-package :cl-user)

(defun intro-fn (a b &key (c 3))
  "Intro doc."
  (list a b c))
(defmacro intro-mac (x &body body)
  "Intro macro doc."
  `(let ((y ,x)) ,@body))
(defgeneric intro-gf (a) (:documentation "Intro gf doc."))
(defmethod intro-gf ((a string)) a)
(defvar *intro-var* 1 "Intro var doc.")
(defun intro-other () 2)
EOF
cat > "$TMPD/intro-compile.lisp" <<EOF
(compile-file "$TMPD/intro.lisp" :output-file "$TMPD/intro.fasl")
EOF
run_script "$TMPD/intro-compile.lisp" >/dev/null 2>&1

# Runs COMMAND after loading the FASL (a fresh image each time).
run_intro() {
    cat <<EOF | "$CLAMIGA" --no-userinit --batch 2>&1
(require "dev-commands")
(load "$TMPD/intro.fasl")
(multiple-value-bind (rc text) (ext.dev:handle-command "$1")
  (format t "~&<<RC=~d>>~%~a~%<<END>>~%" rc text))
EOF
}

# --- ARGLIST ---------------------------------------------------------------

out=$(run_intro 'ARGLIST intro-fn')
check "ARGLIST returns rc 0"                 '<<RC=0>>' "$out"
check "ARGLIST is the lambda list as written, lower case" '^(a b &key (c 3))$' "$out"

out=$(run_intro 'ARGLIST intro-mac')
check "ARGLIST of a macro is its written lambda list" '^(x &body body)$' "$out"

out=$(run_intro 'ARGLIST intro-gf')
check "ARGLIST of a generic function"        '^(a)$' "$out"

out=$(run_intro 'ARGLIST cl:mapcar')
check "ARGLIST of a builtin has bare placeholders" '^(arg0 arg1 &rest arg2)$' "$out"

out=$(run_intro 'ARGLIST if')
check "ARGLIST of a special operator comes from the table" '^(test then &optional else)$' "$out"

out=$(run_intro 'ARGLIST no-such-symbol-here')
check "ARGLIST of an unknown name is rc 10"  '<<RC=10>>' "$out"
check "ARGLIST names the missing symbol"     'no such symbol: no-such-symbol-here' "$out"

out=$(run_intro 'ARGLIST nopkg:foo')
check "ARGLIST with an unknown package is rc 10" 'no such package: nopkg' "$out"

out=$(run_intro 'ARGLIST *intro-var*')
check "ARGLIST of a variable is rc 10"       '<<RC=10>>' "$out"

out=$(run_intro 'ARGLIST')
check "ARGLIST without a name is fatal"      '<<RC=20>>' "$out"

# --- COMPLETE --------------------------------------------------------------

out=$(run_intro 'COMPLETE intro-')
check "COMPLETE lists every candidate"       '^intro-fn$' "$out"
check "COMPLETE lists the macro too"         '^intro-mac$' "$out"
check "COMPLETE candidates are sorted"       'intro-fn.*intro-gf.*intro-mac.*intro-other' "$(echo "$out" | tr '\n' ' ')"

out=$(run_intro 'COMPLETE multiple-value-b')
check "COMPLETE sees symbols inherited from CL" '^multiple-value-bind$' "$out"

out=$(run_intro 'COMPLETE cl:mapc')
check "COMPLETE keeps the package prefix as typed" '^cl:mapcar$' "$out"

out=$(run_intro 'COMPLETE :for')
check "COMPLETE handles keywords"            '^:format-control$' "$out"

out=$(run_intro 'COMPLETE handle ext.dev')
check "COMPLETE takes an explicit package"   '^handle-command$' "$out"

out=$(run_intro 'COMPLETE zzz-nothing')
check "COMPLETE with no candidate is rc 0"   '<<RC=0>>' "$out"

out=$(run_intro 'COMPLETE x nopkg')
check "COMPLETE with an unknown package is rc 10" 'no such package: nopkg' "$out"

# Exported symbols come first: MAPCAR (external in CL) before a same-prefix
# internal of the command package.
cat > "$TMPD/complete-order.lisp" <<EOF
(require "dev-commands")
(load "$TMPD/intro.fasl")
(defun mapcar-intro-internal () 1)
(multiple-value-bind (rc text) (ext.dev:handle-command "COMPLETE mapcar")
  (format t "~&<<RC=~d>>~%~a~%<<END>>~%" rc (substitute #\\Space #\\Newline text)))
(let ((ext.dev:*max-completions* 2))
  (multiple-value-bind (rc text) (ext.dev:handle-command "COMPLETE m")
    (format t "~&<<CAP rc=~d lines=~d>>~%" rc (1+ (count #\\Newline text)))))
EOF
out=$(run_script "$TMPD/complete-order.lisp")
check "COMPLETE puts exported candidates first" 'mapcar mapcar-intro-internal' "$out"
check "COMPLETE caps the candidate list"     '<<CAP rc=0 lines=2>>' "$out"

# --- DESCRIBE --------------------------------------------------------------

out=$(run_intro 'DESCRIBE intro-fn')
check "DESCRIBE returns rc 0"                '<<RC=0>>' "$out"
check "DESCRIBE shows the lambda list"       'Lambda-list: (A B &KEY (C 3))' "$out"
check "DESCRIBE shows the docstring loaded from the FASL" 'Documentation: Intro doc.' "$out"
check "DESCRIBE shows the source location"   'Source: .*intro.lisp:3' "$out"

out=$(run_intro 'DESCRIBE intro-mac')
check "DESCRIBE of a macro names it a macro" 'Macro: ' "$out"
check "DESCRIBE of a macro shows its docstring" 'Documentation: Intro macro doc.' "$out"

out=$(run_intro 'DESCRIBE *intro-var*')
check "DESCRIBE shows a variable docstring"  'Variable documentation: Intro var doc.' "$out"

out=$(run_intro 'DESCRIBE no-such-symbol-here')
check "DESCRIBE of an unknown name is rc 10" '<<RC=10>>' "$out"

# --- APROPOS ---------------------------------------------------------------

out=$(run_intro 'APROPOS intro-')
check "APROPOS finds the user's own definitions" '^intro-fn function$' "$out"
check "APROPOS tags a macro"                 '^intro-mac macro$' "$out"
check "APROPOS tags a variable"              '^\*intro-var\* variable$' "$out"

out=$(run_intro 'APROPOS make-hash cl')
check "APROPOS takes a package"              '^make-hash-table function$' "$out"

out=$(run_intro 'APROPOS x nopkg')
check "APROPOS with an unknown package is rc 10" 'no such package: nopkg' "$out"

out=$(run_intro 'APROPOS')
check "APROPOS without a string is fatal"    '<<RC=20>>' "$out"

# --- SOURCE-LOCATION -------------------------------------------------------

out=$(run_intro 'SOURCE-LOCATION intro-fn')
check "SOURCE-LOCATION returns rc 0"         '<<RC=0>>' "$out"
check "SOURCE-LOCATION is file:line of the DEFUN" 'intro.lisp:3$' "$out"

out=$(run_intro 'SOURCE-LOCATION intro-mac')
check "SOURCE-LOCATION of a macro"           'intro.lisp:6$' "$out"

# A generic function has no body of its own: the first method with a
# recorded location stands in.  From the source, since a method body
# loaded from a FASL is compiled at load time and carries no line -- and
# then the honest answer is rc 10, not "file:0".
cat > "$TMPD/gf-loc.lisp" <<EOF
(require "dev-commands")
(load "$TMPD/intro.lisp")
(multiple-value-bind (rc text) (ext.dev:handle-command "SOURCE-LOCATION intro-gf")
  (format t "~&<<SRC rc=~d>>~%~a~%<<END>>~%" rc text))
EOF
out=$(run_script "$TMPD/gf-loc.lisp")
check "SOURCE-LOCATION of a generic function is its first method" 'intro.lisp:10$' "$out"
out=$(run_intro 'SOURCE-LOCATION intro-gf')
check "SOURCE-LOCATION without a line is rc 10, not file:0" '<<RC=10>>' "$out"

out=$(run_intro 'SOURCE-LOCATION cl:mapcar')
check "SOURCE-LOCATION of a builtin is rc 10" '<<RC=10>>' "$out"
check "SOURCE-LOCATION explains"             'no source location recorded for mapcar' "$out"

# --- MACROEXPAND -----------------------------------------------------------

out=$(run_intro 'MACROEXPAND-1 (intro-mac (+ 1 2) (print y))')
check "MACROEXPAND-1 returns rc 0"           '<<RC=0>>' "$out"
check "MACROEXPAND-1 expands once, lower case" '^(let ((y (+ 1 2))) (print y))$' "$out"

# A long expansion is laid out like code: body forms indented by two under
# WITH-OPEN-FILE's LET, not filled to the margin.
cat > "$TMPD/mx.lisp" <<'EOF'
(require "dev-commands")
(multiple-value-bind (rc text)
    (ext.dev:handle-command "MACROEXPAND (with-open-file (s \"foo\" :direction :output :if-exists :supersede) (format s \"hello ~a\" 1) (format s \"world\"))")
  (format t "~&<<RC=~d>>~%~a~%<<END>>~%" rc text))
EOF
out=$(run_script "$TMPD/mx.lisp")
check "MACROEXPAND lays the expansion out"   '^(let ((s (open "foo" :direction :output :if-exists :supersede)))$' "$out"
check "MACROEXPAND indents the body by two"  '^  (unwind-protect' "$out"

out=$(run_intro 'MACROEXPAND (intro-fn 1 2)')
check "MACROEXPAND of a non-macro form returns it" '^(intro-fn 1 2)$' "$out"

out=$(run_intro 'MACROEXPAND (')
check "MACROEXPAND of an unreadable form is rc 10" '<<RC=10>>' "$out"

out=$(run_intro 'MACROEXPAND')
check "MACROEXPAND without a form is fatal"  '<<RC=20>>' "$out"

echo ""
echo "test_dev_commands: $passed passed, $failed failed, $total total"
[ "$failed" -eq 0 ]
