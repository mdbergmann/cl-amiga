;;; dev-repl.lisp -- the editor's REPL: a dedicated thread behind the port
;;;
;;; Loaded on demand by the REPL-ATTACH command in lib/dev-commands.lisp
;;; (it needs gray-streams, hence CLOS, which the rest of the command layer
;;; does not); (require "dev-repl") works too.
;;;
;;; The EVAL command answers with everything the form printed, after the
;;; fact.  A REPL wants the output as it happens, wants a READ-LINE to ask
;;; the editor, and must not tie up the port's handler thread while a form
;;; runs, or the editor's arglist and completion requests would queue
;;; behind it.  So the REPL is a second thread, and the conversation is
;;; two-way:
;;;
;;;   editor -> clamiga (commands, replied at once)
;;;     REPL-ATTACH <port>   start the REPL thread; PORT is the editor's
;;;                          ARexx port, where the traffic below goes
;;;     REPL-EVAL <forms>    queue the forms; rc 10 while one is running
;;;     REPL-INPUT <line>    answer an outstanding READLINE
;;;     REPL-INTERRUPT       abort the running form
;;;     REPL-DETACH          stop the thread
;;;
;;;   clamiga -> editor (sent from the REPL thread, through *REPL-SEND*)
;;;     OUTPUT <text>        a chunk of output: flushed on newline, on a
;;;                          size threshold and before RESULT
;;;     READLINE             the form is reading a line from standard
;;;                          input; the editor answers with REPL-INPUT
;;;     RESULT <rc> <pkg>    the form is done: rc 0 and the printed values
;;;       <values>           (one per line), or rc 10 and the error text;
;;;                          PKG is the current package for the prompt
;;;
;;; Why the editor never replies to READLINE directly: MUI answers an
;;; application's ARexx command the moment the command hook returns, so the
;;; editor cannot hold the reply until the user has typed the line.  The
;;; line comes back as a command of its own instead, and the REPL thread
;;; parks on a condition variable in between.  Same reason REPL-EVAL does
;;; not carry the values: they arrive with RESULT.
;;;
;;; See tests/test_dev_commands.sh (host, with a Lisp function standing in
;;; for the editor) and tests/amiga/arexx-tests.lisp (the real port).

(eval-when (:compile-toplevel :load-toplevel :execute)
  (require "dev-commands")
  (require "gray-streams"))

(in-package "EXT.DEV")

(defvar *repl-thread-stack-size* (* 256 1024)
  "C stack of the REPL thread: forms compile code, see AMIGA.AREXX:START.")

(defvar *repl-thread-vm-frames* 512)

(defvar *repl-output-chunk* 1024
  "Output is sent to the editor at every newline, or once this many
characters are buffered without one.")

(defvar *repl-stop-timeout-seconds* 5)

;;; Shared between the handler thread (the commands) and the REPL thread,
;;; all under *REPL-LOCK*.
(defvar *repl-lock* (mp:make-lock "repl"))
(defvar *repl-cv* (mp:make-condition-variable "repl"))
(defvar *repl-thread* nil)
(defvar *repl-port* nil "The editor's port name while attached.")
(defvar *repl-job* nil "Form text queued by REPL-EVAL, until the thread takes it.")
(defvar *repl-busy* nil "True from taking a job until RESULT has been sent.")
(defvar *repl-reading* nil "True while a READLINE is outstanding.")
(defvar *repl-input* nil "(LINE) once REPL-INPUT has answered, else NIL.")
(defvar *repl-stop* nil)

;;; Only the REPL thread touches this one: true while a form is being
;;; evaluated inside the handler that turns REPL-INTERRUPT into a RESULT.
;;; The interrupt closure clears it before it signals, so at most one
;;; interrupt reaches each form and none reaches the thread between forms.
(defvar *repl-interruptible* nil)

(define-condition repl-interrupt (serious-condition) ()
  (:report (lambda (c s) (declare (ignore c)) (write-string "Interrupted" s))))

(defun %prompt-package-name (package)
  "The shortest name PACKAGE answers to -- CL-USER, not COMMON-LISP-USER."
  (let ((best (package-name package)))
    (dolist (n (package-nicknames package) best)
      (when (< (length n) (length best)) (setf best n)))))

(defun %repl-send (command)
  "Send COMMAND to the editor's port.  A transport failure (the editor is
gone) stops the REPL rather than killing its thread: the form runs to its
end with its output dropped, and the thread exits at the next job."
  (unless *repl-stop*
    (handler-case (funcall *repl-send* *repl-port* command)
      (error (e)
        (declare (ignore e))
        (mp:with-lock-held (*repl-lock*)
          (setf *repl-stop* t)
          (mp:condition-broadcast *repl-cv*))))))

;;; ----------------------------------------------------------------
;;; The streams
;;; ----------------------------------------------------------------

(defclass repl-output-stream (gray:fundamental-character-output-stream)
  ((buffer :initform (make-string-output-stream))
   (count :initform 0)
   (column :initform 0)))

(defun %repl-flush (stream)
  (with-slots (buffer count) stream
    (when (plusp count)
      (let ((text (get-output-stream-string buffer)))
        (setf count 0)
        (%repl-send (concatenate 'string "OUTPUT " text))))))

(defmethod gray:stream-write-char ((stream repl-output-stream) char)
  (with-slots (buffer count column) stream
    (write-char char buffer)
    (incf count)
    (if (char= char #\Newline)
        (setf column 0)
        (incf column))
    (when (or (char= char #\Newline) (>= count *repl-output-chunk*))
      (%repl-flush stream)))
  char)

(defmethod gray:stream-write-string ((stream repl-output-stream) string
                                     &optional (start 0) end)
  (let ((end (or end (length string))))
    (if (> (- end start) *repl-output-chunk*)
        ;; One huge PRINC still reaches the editor in pieces it can insert
        ;; as they come.
        (loop for from = start then to
              for to = (min end (+ from *repl-output-chunk*))
              while (< from end)
              do (gray:stream-write-string stream string from to))
        (let ((nl (position #\Newline string :start start :end end :from-end t)))
          (when (< start end)
            (with-slots (buffer count column) stream
              (write-string string buffer :start start :end end)
              (incf count (- end start))
              (setf column (if nl (- end nl 1) (+ column (- end start))))
              (when (or nl (>= count *repl-output-chunk*))
                (%repl-flush stream)))))))
  string)

(defmethod gray:stream-line-column ((stream repl-output-stream))
  (slot-value stream 'column))

(defmethod gray:stream-force-output ((stream repl-output-stream))
  (%repl-flush stream)
  nil)

(defmethod gray:stream-finish-output ((stream repl-output-stream))
  (%repl-flush stream)
  nil)

(defclass repl-input-stream (gray:fundamental-character-input-stream)
  ((line :initform nil)      ; the line being served, without its newline
   (pos :initform 0)
   (pushback :initform nil)  ; an unread character
   (output :initarg :output)))

(defun %repl-request-line (stream)
  "Ask the editor for a line and wait for REPL-INPUT.  Returns the line,
or NIL when the REPL is being stopped."
  (%repl-flush (slot-value stream 'output))   ; the prompt goes out first
  (mp:with-lock-held (*repl-lock*)
    (setf *repl-input* nil
          *repl-reading* t))
  (%repl-send "READLINE")
  (mp:with-lock-held (*repl-lock*)
    (loop until (or *repl-input* *repl-stop*)
          do (mp:condition-wait *repl-cv* *repl-lock*))
    (setf *repl-reading* nil)
    (prog1 (car *repl-input*)
      (setf *repl-input* nil))))

(defmethod gray:stream-read-char ((stream repl-input-stream))
  (with-slots (line pos pushback) stream
    (cond (pushback
           (prog1 pushback (setf pushback nil)))
          ((and line (< pos (length line)))
           (prog1 (char line pos) (incf pos)))
          (line
           ;; The line's terminator; the next read asks for a new line.
           (setf line nil pos 0)
           #\Newline)
          (t
           (let ((got (%repl-request-line stream)))
             (if (null got)
                 :eof
                 (progn (setf line got pos 0)
                        (gray:stream-read-char stream))))))))

(defmethod gray:stream-unread-char ((stream repl-input-stream) char)
  (setf (slot-value stream 'pushback) char)
  nil)

(defmethod gray:stream-listen ((stream repl-input-stream))
  (with-slots (line pushback) stream
    (and (or pushback line) t)))

(defmethod gray:stream-clear-input ((stream repl-input-stream))
  (with-slots (line pos pushback) stream
    (setf line nil pos 0 pushback nil))
  nil)

;;; ----------------------------------------------------------------
;;; The thread
;;; ----------------------------------------------------------------

(defun %repl-values-string (values)
  (if values
      (format nil "~{~a~^~%~}" (mapcar #'prin1-to-string values))
      "; No values"))

(defun %repl-eval (form)
  "EVAL with the listener's history variables kept, as at the console."
  (setf - form)
  (let ((values (multiple-value-list (eval form))))
    (setf +++ ++ ++ + + form
          /// // // / / values
          *** ** ** * * (first values))
    values))

(defun %repl-run (text out in)
  "Evaluate the forms in TEXT, streaming their output, then send RESULT."
  (let ((rc +rc-ok+) (result nil) (eof (list :eof)))
    (handler-case
        (progn
          (setf *repl-interruptible* t)
          (with-input-from-string (src text)
            (loop for form = (read src nil eof)
                  until (eq form eof)
                  do (setf result (%repl-values-string (%repl-eval form)))))
          (setf *repl-interruptible* nil))
      (serious-condition (c)
        (setf *repl-interruptible* nil
              rc +rc-error+
              result (format nil "ERROR: ~a" (%condition-text c)))))
    (gray:stream-clear-input in)
    (%repl-flush out)
    (setf *command-package* *package*)
    (%repl-send (format nil "RESULT ~d ~a~%~a" rc (%prompt-package-name *package*)
                        (%truncate (or result "; No values"))))))

(defun %repl-take-job ()
  "Wait for REPL-EVAL's text, or NIL once the REPL is being stopped."
  (mp:with-lock-held (*repl-lock*)
    (loop until (or *repl-job* *repl-stop*)
          do (mp:condition-wait *repl-cv* *repl-lock*))
    (if *repl-stop*
        nil
        (prog1 *repl-job*
          (setf *repl-job* nil
                *repl-busy* t)))))

(defun %repl-finish-job ()
  ;; An interrupt may have cut a READLINE short: forget it, or the next
  ;; REPL-INPUT would be taken for an answer.
  (mp:with-lock-held (*repl-lock*)
    (setf *repl-busy* nil
          *repl-reading* nil
          *repl-input* nil)))

(defun %repl-loop ()
  (let* ((out (make-instance 'repl-output-stream))
         (in (make-instance 'repl-input-stream :output out))
         (*standard-output* out)
         (*error-output* out)
         (*trace-output* out)
         (*standard-input* in)
         (*package* *command-package*)
         ;; Private history for this REPL thread: without a per-thread
         ;; binding, %REPL-EVAL's SETFs would mutate the same global cells
         ;; the physical console's REPL uses (src/core/repl.c's
         ;; cl_repl_update_history), corrupting whichever session runs
         ;; concurrently with this one.
         (- nil) (+ nil) (++ nil) (+++ nil)
         (* nil) (** nil) (*** nil)
         (/ nil) (// nil) (/// nil))
    (unwind-protect
         (loop
           (let ((text (%repl-take-job)))
             (when (null text) (return))
             ;; IN-PACKAGE from the editor since the last form wins; a form's
             ;; own IN-PACKAGE is handed back through *COMMAND-PACKAGE*.
             (setf *package* *command-package*)
             (unwind-protect (%repl-run text out in)
               (%repl-finish-job))))
      (mp:with-lock-held (*repl-lock*)
        (setf *repl-busy* nil
              *repl-reading* nil
              *repl-port* nil)))))

(defun %repl-alive-p ()
  (and *repl-thread* (mp:thread-alive-p *repl-thread*) t))

(defun %repl-interrupt ()
  "Abort the running form, if any.  Safe when none runs: the closure
checks on the REPL thread itself."
  (when (%repl-alive-p)
    ;; IGNORE-ERRORS: the thread may exit between the check and the call.
    (ignore-errors
     (mp:interrupt-thread *repl-thread*
                          (lambda ()
                            (when *repl-interruptible*
                              (setf *repl-interruptible* nil)
                              (error 'repl-interrupt)))))))

(defun %repl-stop ()
  "Stop the REPL thread and wait for it.  Returns true when it is gone."
  (when (%repl-alive-p)
    (mp:with-lock-held (*repl-lock*)
      (setf *repl-stop* t)
      (mp:condition-broadcast *repl-cv*))
    (%repl-interrupt)
    (let ((waited 0))
      (loop while (and (mp:thread-alive-p *repl-thread*)
                       (< waited *repl-stop-timeout-seconds*))
            do (sleep 0.02)
               (incf waited 0.02))))
  (if (%repl-alive-p)
      nil
      (progn (setf *repl-thread* nil *repl-port* nil)
             t)))

;; The REPL thread must not outlive the process either (see the exit hook
;; in AMIGA.AREXX:START for what a leftover thread costs).  This file loads
;; after the port is up, so hooks running newest-first stop the REPL thread
;; before the port that fed it.
(ext:add-exit-hook '%repl-stop)

(defun %repl-start (port)
  (mp:with-lock-held (*repl-lock*)
    (setf *repl-port* port
          *repl-job* nil
          *repl-busy* nil
          *repl-reading* nil
          *repl-input* nil
          *repl-stop* nil))
  (setf *repl-interruptible* nil)
  (setf *repl-thread*
        (mp:make-thread #'%repl-loop
                        :name "repl"
                        :stack-size *repl-thread-stack-size*
                        :vm-frames *repl-thread-vm-frames*)))

;;; ----------------------------------------------------------------
;;; The commands
;;; ----------------------------------------------------------------

(define-command "REPL-ATTACH" (arg)
  (let ((port (%unquote arg)))
    (cond ((zerop (length port))
           (values +rc-fatal+ "ERROR: REPL-ATTACH requires the editor's port name"))
          ((null *repl-send*)
           (values +rc-fatal+ "ERROR: no ARexx transport in this image (is AMIGA.AREXX loaded?)"))
          ((not (%repl-stop))
           (values +rc-error+ "ERROR: the previous REPL thread did not stop"))
          (t
           (%repl-start port)
           (values +rc-ok+ (%prompt-package-name *command-package*))))))

(define-command "REPL-DETACH" (arg)
  (declare (ignore arg))
  (if (%repl-stop)
      (values +rc-ok+ "REPL detached")
      (values +rc-error+ "ERROR: the REPL thread did not stop")))

(define-command "REPL-EVAL" (arg)
  (cond ((zerop (length arg))
         (values +rc-fatal+ "ERROR: REPL-EVAL requires a form"))
        ((not (%repl-alive-p))
         (values +rc-error+ "ERROR: no REPL attached (send REPL-ATTACH <port> first)"))
        (t
         (mp:with-lock-held (*repl-lock*)
           (if (or *repl-job* *repl-busy*)
               (values +rc-error+ "ERROR: the REPL is busy (REPL-INTERRUPT aborts the running form)")
               (progn
                 (setf *repl-job* arg)
                 (mp:condition-broadcast *repl-cv*)
                 (values +rc-ok+ "")))))))

(define-command "REPL-INPUT" (arg)
  (if (not (%repl-alive-p))
      (values +rc-error+ "ERROR: no REPL attached")
      (mp:with-lock-held (*repl-lock*)
        (if *repl-reading*
            (progn
              (setf *repl-input* (list arg))
              (mp:condition-broadcast *repl-cv*)
              (values +rc-ok+ ""))
            (values +rc-error+ "ERROR: the REPL is not reading a line")))))

(define-command "REPL-INTERRUPT" (arg)
  (declare (ignore arg))
  (cond ((not (%repl-alive-p))
         (values +rc-error+ "ERROR: no REPL attached"))
        ((not (mp:with-lock-held (*repl-lock*) *repl-busy*))
         (values +rc-ok+ "the REPL is idle"))
        (t
         (%repl-interrupt)
         (values +rc-ok+ ""))))

(provide "dev-repl")
