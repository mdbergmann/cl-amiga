; ARexx development port tests -- AmigaOS / MorphOS
;
; Loaded by tests/amiga/run-tests.lisp, which defines CHECK, *PASS-COUNT*
; and *FAIL-COUNT*.  It is a separate file because the tests name symbols in
; the AMIGA.AREXX package: the reader resolves those as it reads each form,
; so the REQUIRE that creates the package has to have run in an earlier
; read/eval cycle -- which a nested LOAD gives us and a form in the same
; file cannot.
;
; End-to-end over the real host protocol: AMIGA.AREXX:SEND builds an actual
; RXCOMM message with an argstring in ARG0 and waits for the reply, exactly
; as the ARexx interpreter does -- so this exercises the port without needing
; RexxMast or a second process.  The port name is deliberately NOT "CLAMIGA":
; a developer may have a real dev port open on the same machine.

(require "amiga/arexx")

(handler-case
    (progn
      (with-open-file (s "T:clamiga-arexx-test.lisp"
                         :direction :output :if-exists :supersede)
        (format s "(defvar *arexx-test-one* t)~%")
        (format s "(error \"arexx test error one\")~%")
        (format s "(arexx-test-undefined-function)~%")
        (format s "(defvar *arexx-test-end* t)~%"))
      (let ((port (amiga.arexx:start :name "CLAMIGATEST")))
        (check "arexx start returns the port name" "CLAMIGATEST" port)
        (check "arexx port-name" "CLAMIGATEST" (amiga.arexx:port-name))
        (check "arexx running-p" t (amiga.arexx:running-p))

        ; PING: the minimal round trip -- message out, reply in, rc 0 with a
        ; RESULT argstring (rc 0 is the only case ARexx transmits one).
        (multiple-value-bind (rc text) (amiga.arexx:send port "PING")
          (check "arexx ping rc" 0 rc)
          (check "arexx ping result" "PONG" text))

        ; Regression: AMIGA:AREXX-SEND used to leak the port-name buffer when
        ; the command argument failed its type check -- the type error itself
        ; longjmp'd past the cleanup.  Must error cleanly and leave the port
        ; serving.
        (check "arexx send rejects a non-string command" t
               (handler-case (progn (amiga.arexx:send port 42) nil)
                 (error () t)))
        (multiple-value-bind (rc text) (amiga.arexx:send port "PING")
          (check "arexx still alive after a bad command" 0 rc)
          (check "arexx still answers after a bad command" "PONG" text))

        (multiple-value-bind (rc text) (amiga.arexx:send port "VERSION")
          (check "arexx version rc" 0 rc)
          (check "arexx version mentions CL-Amiga" t
                 (and (search "CL-Amiga" text) t)))

        ; A form evaluated in the running image, from the editor's side.
        (multiple-value-bind (rc text) (amiga.arexx:send port "EVAL (+ 40 2)")
          (check "arexx eval rc" 0 rc)
          (check "arexx eval result" t (and (search "42" text) t)))

        ; An unknown verb is fatal (rc 20) and, per the protocol, carries no
        ; RESULT -- the text is only reachable through LASTRESULT.
        (multiple-value-bind (rc text) (amiga.arexx:send port "NOSUCHVERB")
          (check "arexx unknown command rc" 20 rc)
          (check "arexx unknown command drops RESULT" "" text))
        (multiple-value-bind (rc text) (amiga.arexx:send port "LASTRESULT")
          (check "arexx lastresult rc" 0 rc)
          (check "arexx lastresult recovers the text" t
                 (and (search "unknown command" text) t)))

        ; The headline case: a file with two bad top-level forms comes back
        ; with a diagnostic for EACH, and the good forms after them still ran.
        (multiple-value-bind (rc text)
            (amiga.arexx:send port "LOAD T:clamiga-arexx-test.lisp")
          (check "arexx load rc is error" 10 rc)
          (check "arexx load error drops RESULT" "" text))
        (multiple-value-bind (rc text) (amiga.arexx:send port "LASTRESULT")
          (check "arexx load diagnostics rc" 0 rc)
          (check "arexx load reports both errors" t
                 (and (search "2 error(s)" text) t))
          (check "arexx load diagnostic has file:line" t
                 (and (search "clamiga-arexx-test.lisp:2" text) t)))
        (check "arexx load ran the forms after the errors" t
               (and (boundp '*arexx-test-end*) t))

        ; Introspection -- the editor's phase-2 commands, over the port.
        ; The definitions come from a file so SOURCE-LOCATION has a line to
        ; report and the docstring went through the compiler's load-time
        ; record.
        (with-open-file (s "T:clamiga-arexx-intro.lisp"
                           :direction :output :if-exists :supersede)
          (format s "(defun arexx-intro-fn (a &optional (b 2))~%")
          (format s "  \"Intro doc.\"~%")
          (format s "  (list a b))~%")
          (format s "(defmacro arexx-intro-mac (x &body body) `(progn ,x ,@body))~%"))
        (multiple-value-bind (rc text)
            (amiga.arexx:send port "LOAD T:clamiga-arexx-intro.lisp")
          (check "arexx intro file loads" 0 rc)
          (check "arexx intro file loads clean" t
                 (and (search "0 error(s)" text) t)))
        (multiple-value-bind (rc text) (amiga.arexx:send port "ARGLIST arexx-intro-fn")
          (check "arexx ARGLIST rc" 0 rc)
          (check "arexx ARGLIST is the written lambda list" "(a &optional (b 2))" text))
        (multiple-value-bind (rc text) (amiga.arexx:send port "ARGLIST arexx-intro-mac")
          (check "arexx ARGLIST of a macro rc" 0 rc)
          (check "arexx ARGLIST of a macro" "(x &body body)" text))
        (multiple-value-bind (rc text) (amiga.arexx:send port "ARGLIST no-such-fn-here")
          (declare (ignore text))
          (check "arexx ARGLIST of an unknown name is rc 10" 10 rc))
        (multiple-value-bind (rc text) (amiga.arexx:send port "COMPLETE arexx-intro-")
          (check "arexx COMPLETE rc" 0 rc)
          (check "arexx COMPLETE lists both, sorted" t
                 (and (search "arexx-intro-fn" text)
                      (search "arexx-intro-mac" text)
                      (< (search "arexx-intro-fn" text) (search "arexx-intro-mac" text))
                      t)))
        (multiple-value-bind (rc text) (amiga.arexx:send port "DESCRIBE arexx-intro-fn")
          (check "arexx DESCRIBE rc" 0 rc)
          (check "arexx DESCRIBE shows the lambda list" t
                 (and (search "Lambda-list: (A &OPTIONAL (B 2))" text) t))
          (check "arexx DESCRIBE shows the docstring" t
                 (and (search "Documentation: Intro doc." text) t)))
        (multiple-value-bind (rc text) (amiga.arexx:send port "APROPOS arexx-intro")
          (check "arexx APROPOS rc" 0 rc)
          (check "arexx APROPOS tags the kinds" t
                 (and (search "arexx-intro-fn function" text)
                      (search "arexx-intro-mac macro" text)
                      t)))
        (multiple-value-bind (rc text) (amiga.arexx:send port "SOURCE-LOCATION arexx-intro-fn")
          (check "arexx SOURCE-LOCATION rc" 0 rc)
          (check "arexx SOURCE-LOCATION is file:line" t
                 (and (search "clamiga-arexx-intro.lisp:1" text) t)))
        (multiple-value-bind (rc text)
            (amiga.arexx:send port "MACROEXPAND-1 (arexx-intro-mac 1 2 3)")
          (check "arexx MACROEXPAND-1 rc" 0 rc)
          (check "arexx MACROEXPAND-1 expansion" "(progn 1 2 3)" text))
        (delete-file "T:clamiga-arexx-intro.lisp")

        ; The REPL (lib/dev-repl.lisp): a thread of its own that talks back
        ; to the editor's port.  There is one ARexx port per process, so the
        ; editor's port here is our own: the REPL thread's OUTPUT / READLINE
        ; / RESULT commands travel over ARexx as EVALs that record them, and
        ; READLINE is answered with a REPL-INPUT round trip -- the real
        ; protocol in both directions, with the editor played by this file.
        (defvar cl-user::*arexx-repl-sent* '())
        (setf ext.dev:*repl-send*
              (lambda (to command)
                (amiga.arexx:send
                 to (format nil "EVAL (push ~s cl-user::*arexx-repl-sent*)" command))
                (when (string= command "READLINE")
                  (amiga.arexx:send to "REPL-INPUT typed on the amiga"))
                (values 0 "")))
        (flet ((wait-result ()
                 (loop repeat 500
                       until (find-if (lambda (s)
                                        (and (>= (length s) 6)
                                             (string= "RESULT" s :end2 6)))
                                      cl-user::*arexx-repl-sent*)
                       do (sleep 0.02))
                 (prog1 (reverse cl-user::*arexx-repl-sent*)
                   (setf cl-user::*arexx-repl-sent* '())))
               (index-of (prefix sent)
                 (position-if (lambda (s) (and (>= (length s) (length prefix))
                                               (string= prefix s :end2 (length prefix))))
                              sent)))
          (multiple-value-bind (rc text) (amiga.arexx:send port "REPL-ATTACH CLAMIGATEST")
            (check "arexx REPL-ATTACH rc" 0 rc)
            (check "arexx REPL-ATTACH answers the package" "CL-USER" text))
          (multiple-value-bind (rc text)
              (amiga.arexx:send
               port "REPL-EVAL (progn (princ \"repl says hi\") (terpri) (read-line))")
            (check "arexx REPL-EVAL replies at once" 0 rc)
            (check "arexx REPL-EVAL reply carries no values" "" text))
          (let ((sent (wait-result)))
            (check "arexx REPL streams the output" t
                   (and (index-of "OUTPUT repl says hi" sent) t))
            (check "arexx REPL asks the editor for a line" t
                   (and (index-of "READLINE" sent) t))
            (check "arexx REPL RESULT comes last" t
                   (and (index-of "RESULT 0 CL-USER" sent)
                        (= (index-of "RESULT 0 CL-USER" sent) (1- (length sent)))))
            (check "arexx REPL RESULT carries the line read" t
                   (and (search "\"typed on the amiga\"" (car (last sent))) t))
            (check "arexx REPL output precedes the read" t
                   (and (index-of "OUTPUT repl says hi" sent)
                        (index-of "READLINE" sent)
                        (< (index-of "OUTPUT repl says hi" sent)
                           (index-of "READLINE" sent)))))
          ; A running form keeps the port free, and can be aborted.
          (multiple-value-bind (rc text) (amiga.arexx:send port "REPL-EVAL (loop)")
            (declare (ignore text))
            (check "arexx REPL-EVAL of an endless loop replies" 0 rc))
          (sleep 0.2)
          (multiple-value-bind (rc text) (amiga.arexx:send port "PING")
            (check "arexx port answers while the REPL runs" 0 rc)
            (check "arexx port answers PONG while the REPL runs" "PONG" text))
          (multiple-value-bind (rc text) (amiga.arexx:send port "REPL-EVAL (+ 1 1)")
            (declare (ignore text))
            (check "arexx REPL-EVAL while busy is rc 10" 10 rc))
          (multiple-value-bind (rc text) (amiga.arexx:send port "REPL-INTERRUPT")
            (declare (ignore text))
            (check "arexx REPL-INTERRUPT rc" 0 rc))
          (let ((sent (wait-result)))
            (check "arexx REPL-INTERRUPT aborts the form" t
                   (and (index-of "RESULT 10 CL-USER" sent)
                        (search "Interrupted" (car (last sent)))
                        t)))
          (multiple-value-bind (rc text) (amiga.arexx:send port "REPL-EVAL (+ 40 2)")
            (declare (ignore text))
            (check "arexx REPL-EVAL after the interrupt rc" 0 rc))
          (let ((sent (wait-result)))
            (check "arexx REPL works after the interrupt" t
                   (and (index-of "RESULT 0 CL-USER" sent)
                        (search "42" (car (last sent)))
                        t)))
          (multiple-value-bind (rc text) (amiga.arexx:send port "REPL-DETACH")
            (declare (ignore text))
            (check "arexx REPL-DETACH rc" 0 rc))
          (multiple-value-bind (rc text) (amiga.arexx:send port "REPL-EVAL (+ 1 1)")
            (declare (ignore text))
            (check "arexx REPL-EVAL after detach is rc 10" 10 rc)))
        (setf ext.dev:*repl-send* #'amiga.arexx:send)

        ; The port keeps serving after all of that.
        (multiple-value-bind (rc text) (amiga.arexx:send port "PING")
          (check "arexx still alive after failures" 0 rc)
          (check "arexx still answers" "PONG" text))

        (amiga.arexx:stop)
        (check "arexx stop clears running-p" nil (amiga.arexx:running-p))
        (check "arexx stop clears port-name" nil (amiga.arexx:port-name))
        ; Sending to a port that is gone must fail cleanly, not hang.
        (check "arexx send to a closed port errors" t
               (handler-case (progn (amiga.arexx:send port "PING") nil)
                 (error () t)))
        (delete-file "T:clamiga-arexx-test.lisp")))
  (error (e)
    (setq *fail-count* (+ *fail-count* 1))
    (format t "FAIL: ARexx port tests signaled: ~A~%" e)))
