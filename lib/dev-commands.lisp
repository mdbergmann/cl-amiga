;;; dev-commands.lisp -- the editor-facing command language for CL-Amiga
;;;
;;; Loaded via (require "dev-commands").
;;;
;;; A command is one string in, one reply string plus a return code out:
;;;
;;;   (ext.dev:handle-command "LOAD Work:src/foo.lisp")
;;;   => 10, "Work:src/foo.lisp:3: ERROR: Too many arguments to FOO ...
;;;           1 error(s), 0 warning(s)"
;;;
;;; That signature is the whole point of keeping this file separate from
;;; lib/amiga/arexx.lisp: the ARexx port is only a transport for these
;;; strings, so everything that decides what a command MEANS -- parsing,
;;; error capture, diagnostic rendering -- is plain portable Lisp that runs
;;; and is tested on the host, where there is no ARexx at all.
;;;
;;; See tests/test_dev_commands.sh for the executable specification.

(defpackage "EXT.DEV"
  (:use "CL")
  (:export
   ;; Entry point
   "HANDLE-COMMAND"
   ;; Return codes (the ARexx severity ladder -- see +RC-WARN+)
   "+RC-OK+" "+RC-WARN+" "+RC-ERROR+" "+RC-FATAL+"
   ;; Session state
   "*COMMAND-PACKAGE*" "*MAX-RESULT-LENGTH*" "*LAST-RESULT*"
   ;; Introspection / extension
   "*COMMANDS*" "DEFINE-COMMAND"))

(in-package "EXT.DEV")

;;; ================================================================
;;; Return codes
;;;
;;; These are ARexx severity levels, and the values are not arbitrary:
;;; ARexx aborts a macro when a command's return code reaches the FAILAT
;;; threshold, which defaults to 10.  Warnings must therefore stay BELOW
;;; 10 (a style warning should not kill the editor macro) and errors must
;;; reach it (a failed load should).
;;; ================================================================

(defconstant +rc-ok+     0)
(defconstant +rc-warn+   5)
(defconstant +rc-error+ 10)
(defconstant +rc-fatal+ 20)

(defvar *command-package* (find-package "CL-USER")
  "Package that EVAL and READ use for commands.  Set by IN-PACKAGE.
Per-connection state: the REPL's *PACKAGE* is left alone.")

(defvar *max-result-length* 8192
  "Cap on the reply string.  A reply longer than this is truncated on a
line boundary and a marker line is appended.  ARexx result strings are
argstrings and every editor holds the whole thing in memory, so an
unbounded compiler log is not a reasonable thing to send back.")

(defvar *last-result* ""
  "Reply text of the most recent command.  Kept because ARexx only
transmits RESULT when the return code is 0 -- LASTRESULT is how a macro
retrieves the diagnostics that came with a failing rc.")

;;; ================================================================
;;; Diagnostics
;;; ================================================================

(defstruct (diag (:constructor make-diag (severity file line text)))
  severity   ; :ERROR or :WARNING
  file       ; namestring or NIL
  line       ; integer or NIL
  text)      ; the condition report

(defun %condition-text (condition)
  "Report string for CONDITION, never signalling in the process -- a
PRINT-OBJECT method that itself errors must not take down the port."
  (handler-case (princ-to-string condition)
    (error () (format nil "<unprintable ~a condition>" (type-of condition)))))

(defun %own-source-file ()
  "The source file this very file was compiled from, as EXT:BACKTRACE
spells it.  Frame 0 inside a function IS that function, so asking for
one frame here names our own file whether we were loaded from source or
from a FASL."
  (third (first (ext:backtrace 1))))

(defvar *this-file* (%own-source-file)
  "Used to skip our own frames when locating a diagnostic -- see
%ERROR-LOCATION.")

(defmacro %error-location (locate)
  "Source location of the form that is signalling, as (values FILE LINE),
or (values NIL NIL) when LOCATE is false or no location is credible.

EXT:BACKTRACE returns (INDEX NAME FILE LINE) innermost-first.  Two cases
have to be handled differently, which is why this skips rather than just
taking frame 0:

  - Errors raised inside the runtime (undefined function, bad arity) are
    captured at signal time, so frame 0 is already the offending form.
  - Conditions signalled by CL:ERROR run their handlers on top of the
    live stack, so frame 0 is OUR handler, in THIS file.

Skipping leading frames from *THIS-FILE* gives the offending user form in
both cases.  LOCATE is false for commands like EVAL where the form came
from a command string and never had a source location to begin with --
reporting the stale file the compiler last stamped would be worse than
reporting none.

A MACRO, not a function, and that is load-bearing twice over: a function
call would push yet another frame, and the skip depends on the handler
frame being in this file."
  `(if ,locate
       (let ((found nil))
         (dolist (fr (ext:backtrace 8))
           (unless (equal (third fr) *this-file*)
             (setf found fr)
             (return)))
         (if (and found (third found))
             (values (third found) (fourth found))
             (values nil nil)))
       (values nil nil)))

(defun %call-guarded (thunk)
  "Call THUNK, returning (values RESULT ESCAPED-P).  If a condition
unwinds out of THUNK, control lands here instead of continuing outward,
and ESCAPED-P is true.

Deliberately built from CATCH/UNWIND-PROTECT/THROW rather than
HANDLER-CASE: a handler for ERROR anywhere in the dynamic extent wins
the handler search ahead of LOAD's own per-top-level-form recovery, so
the first bad form would abort the file and the editor would see one
diagnostic where there are five.  CATCH establishes no handler, so the
condition system never sees this guard; the cleanup only fires once the
error is already unwinding, and throwing from it abandons that unwind
here.  See tests/test_dev_commands.sh."
  (let ((escaped t) (result nil))
    (catch '%escape
      (unwind-protect
           (progn (setf result (funcall thunk))
                  (setf escaped nil))
        (when escaped (throw '%escape nil))))
    (values result escaped)))

(defun %render-diag (d)
  (let ((file (diag-file d))
        (line (diag-line d)))
    (if (and file line)
        (format nil "~a:~d: ~a: ~a" file line (diag-severity d) (diag-text d))
        (format nil "~a: ~a" (diag-severity d) (diag-text d)))))

(defun %summary (diags)
  (let ((errors 0) (warnings 0))
    (dolist (d diags)
      (if (eq (diag-severity d) :error) (incf errors) (incf warnings)))
    (values errors warnings
            (format nil "~d error(s), ~d warning(s)" errors warnings))))

(defun call-with-diagnostics (thunk &key locate)
  "Call THUNK, returning (values LOG DIAGS ESCAPED-P): everything it
printed, a DIAG struct for every ERROR and WARNING signalled inside it,
and whether a condition unwound out of THUNK entirely.

The ERROR handler DECLINES (returns normally) on purpose.  LOAD catches
per top-level form and moves on to the next one, so declining is what
turns `load a file with three broken forms' into three diagnostics
instead of one -- a handler that transferred control would stop at the
first.  WARNING is muffled after recording, since it has already been
turned into a diagnostic line.

Note this relies on LOAD's per-form recovery surviving the UNWIND-PROTECT
that WITH-OUTPUT-TO-STRING expands into; see tests/test_unwind_load.c for
the regression that guards it."
  (let ((diags '()) (escaped nil))
    (let ((log (with-output-to-string (stream)
                 (let ((*standard-output* stream)
                       (*error-output* stream)
                       (*trace-output* stream))
                   (setf escaped
                         (nth-value
                          1
                          (%call-guarded
                           (lambda ()
                             (handler-bind
                                 ((warning
                                    (lambda (c)
                                      (multiple-value-bind (f l) (%error-location locate)
                                        (push (make-diag :warning f l (%condition-text c))
                                              diags))
                                      (muffle-warning c)))
                                  (error
                                    (lambda (c)
                                      (multiple-value-bind (f l) (%error-location locate)
                                        (push (make-diag :error f l (%condition-text c))
                                              diags)))))
                               (funcall thunk))))))))))
      (values log (nreverse diags) escaped))))

(defun %reply (diags log escaped &optional prefix)
  "Assemble the reply text and its return code from recorded DIAGS and
captured LOG.  Diagnostics come first so that the machine-readable part
survives truncation; the raw log follows for a human to read."
  (multiple-value-bind (errors warnings summary) (%summary diags)
    (let ((text (with-output-to-string (s)
                  (when prefix
                    (write-string prefix s)
                    (terpri s))
                  (dolist (d diags)
                    (write-string (%render-diag d) s)
                    (terpri s))
                  (write-string summary s)
                  (when escaped
                    ;; A reader error (an unbalanced paren in a half-saved
                    ;; buffer, say) takes the rest of the file with it -- say
                    ;; so, rather than letting the count imply the file was
                    ;; fully processed.
                    (format s "~%; aborted -- the remaining forms were not processed"))
                  (when (plusp (length log))
                    (format s "~%--- log ---~%~a" log)))))
      (values (cond ((or escaped (plusp errors)) +rc-error+)
                    ((plusp warnings) +rc-warn+)
                    (t +rc-ok+))
              text))))

(defun %truncate (text)
  (if (<= (length text) *max-result-length*)
      text
      (let* ((cut (subseq text 0 *max-result-length*))
             (nl (position #\Newline cut :from-end t)))
        (concatenate 'string
                     (subseq cut 0 (or nl (length cut)))
                     (format nil "~%[truncated at ~d characters]"
                             *max-result-length*)))))

;;; ================================================================
;;; Command table
;;; ================================================================

(defvar *commands* '()
  "Alist of (VERB-STRING . HANDLER).  HANDLER takes the argument string
and returns (values RC TEXT).")

(defmacro define-command (verb (arg) &body body)
  `(let ((entry (assoc ,verb *commands* :test #'string=)))
     (if entry
         (setf (cdr entry) (lambda (,arg) ,@body))
         (push (cons ,verb (lambda (,arg) ,@body)) *commands*))
     ,verb))

(defun %split-verb (string)
  "Split STRING into (values VERB REST).  VERB is upcased; REST keeps its
internal spacing but is trimmed at both ends."
  (let* ((s (string-trim '(#\Space #\Tab #\Newline #\Return) string))
         (end (or (position-if (lambda (c) (member c '(#\Space #\Tab))) s)
                  (length s))))
    (values (string-upcase (subseq s 0 end))
            (string-trim '(#\Space #\Tab #\Newline #\Return) (subseq s end)))))

(defun %unquote (string)
  "Strip one layer of surrounding double or single quotes -- editors quote
paths that contain spaces (LOAD \"Ram Disk:foo.lisp\")."
  (let ((s (string-trim '(#\Space #\Tab) string)))
    (if (and (>= (length s) 2)
             (or (and (char= (char s 0) #\") (char= (char s (1- (length s))) #\"))
                 (and (char= (char s 0) #\') (char= (char s (1- (length s))) #\'))))
        (subseq s 1 (1- (length s)))
        s)))

;;; ================================================================
;;; The commands
;;; ================================================================

(define-command "PING" (arg)
  (declare (ignore arg))
  (values +rc-ok+ "PONG"))

(define-command "VERSION" (arg)
  (declare (ignore arg))
  (values +rc-ok+
          (format nil "~a ~a on ~a/~a"
                  (lisp-implementation-type)
                  (lisp-implementation-version)
                  (software-type)
                  (machine-type))))

(define-command "LASTRESULT" (arg)
  (declare (ignore arg))
  ;; Deliberately rc 0: this command exists precisely so a macro that just
  ;; got a non-zero rc (and therefore no RESULT) can still read the text.
  (values +rc-ok+ *last-result*))

(define-command "IN-PACKAGE" (arg)
  (let* ((name (string-upcase (%unquote arg)))
         (pkg (find-package name)))
    (if pkg
        (progn (setf *command-package* pkg)
               (values +rc-ok+ (format nil "Package is now ~a" (package-name pkg))))
        (values +rc-error+ (format nil "ERROR: no such package: ~a" name)))))

(define-command "LOAD" (arg)
  (let ((path (%unquote arg)))
    (if (zerop (length path))
        (values +rc-fatal+ "ERROR: LOAD requires a file name")
        (multiple-value-bind (log diags escaped)
            (call-with-diagnostics
             (lambda () (let ((*package* *command-package*)) (load path)))
             :locate t)
          (%reply diags log escaped (format nil "; loading ~a" path))))))

(define-command "COMPILE-FILE" (arg)
  (let ((path (%unquote arg)))
    (if (zerop (length path))
        (values +rc-fatal+ "ERROR: COMPILE-FILE requires a file name")
        (let ((output nil) (failure nil) (warned nil))
          (multiple-value-bind (log diags escaped)
              (call-with-diagnostics
               (lambda ()
                 (let ((*package* *command-package*))
                   (multiple-value-setq (output warned failure)
                     (compile-file path))))
               :locate t)
            (multiple-value-bind (rc text)
                (%reply diags log escaped
                        (format nil "; compiling ~a~@[~%; wrote ~a~]"
                                path (and output (namestring output))))
              ;; COMPILE-FILE reports failure through its own return values
              ;; as well as through signalled conditions (CLHS 3.2.5); a
              ;; failure-p of T with nothing recorded still has to fail.
              (values (cond (failure (max rc +rc-error+))
                            (warned  (max rc +rc-warn+))
                            (t rc))
                      text)))))))

(define-command "EVAL" (arg)
  (if (zerop (length arg))
      (values +rc-fatal+ "ERROR: EVAL requires a form")
      (let ((values-text nil))
        (multiple-value-bind (log diags escaped)
            (call-with-diagnostics
             (lambda ()
               (let ((*package* *command-package*)
                     (eof (list :eof)))
                 (setf values-text
                       (with-output-to-string (out)
                         (with-input-from-string (in arg)
                           (loop for form = (read in nil eof)
                                 until (eq form eof)
                                 do (let ((results (multiple-value-list (eval form))))
                                      (if results
                                          (format out "~{~a~^ ; ~}~%"
                                                  (mapcar #'prin1-to-string results))
                                          (format out "; no values~%"))))))))))
          (multiple-value-bind (rc text) (%reply diags log escaped)
            (values rc
                    (if (and (= rc +rc-ok+) values-text (plusp (length values-text)))
                        (concatenate 'string
                                     (string-right-trim '(#\Newline) values-text)
                                     (string #\Newline) text)
                        text)))))))

;;; ================================================================
;;; Dispatch
;;; ================================================================

(defun handle-command (command)
  "Execute COMMAND, a command string from an editor.  Returns
(values RC TEXT); TEXT is also stashed in *LAST-RESULT*.

A command starting with #\\( is a Lisp form and goes straight to EVAL, so
both `EVAL (room)' and `(room)' work from a macro."
  (let ((rc +rc-fatal+)
        (text ""))
    ;; %CALL-GUARDED, not HANDLER-CASE: handle-command is called from the
    ;; port's handler loop and must always produce a reply, but a handler
    ;; for ERROR established here would sit in the dynamic extent of every
    ;; LOAD below it and pre-empt LOAD's per-form recovery (see
    ;; %call-guarded).  The guard catches escapes without ever entering the
    ;; handler search.
    (let ((escaped
            (nth-value
             1
             (%call-guarded
              (lambda ()
                (let ((trimmed (string-trim '(#\Space #\Tab #\Newline #\Return) command)))
                  (cond
                    ((zerop (length trimmed))
                     (setf rc +rc-ok+ text ""))
                    ((char= (char trimmed 0) #\()
                     (multiple-value-setq (rc text)
                       (funcall (cdr (assoc "EVAL" *commands* :test #'string=)) trimmed)))
                    (t
                     (multiple-value-bind (verb rest) (%split-verb trimmed)
                       (let ((entry (assoc verb *commands* :test #'string=)))
                         (if entry
                             (multiple-value-setq (rc text) (funcall (cdr entry) rest))
                             (setf rc +rc-fatal+
                                   text (format nil "ERROR: unknown command: ~a~%Known commands: ~{~a~^ ~}"
                                                verb (sort (mapcar #'car *commands*)
                                                           #'string<))))))))))))))
      (when escaped
        (setf rc +rc-fatal+
              text "ERROR: the command handler was aborted by an unhandled condition")))
    (setf text (%truncate (or text "")))
    (setf *last-result* text)
    (values rc text)))

(provide "dev-commands")
