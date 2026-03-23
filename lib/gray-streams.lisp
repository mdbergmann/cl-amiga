;;; gray-streams.lisp — Gray Streams implementation for CL-Amiga
;;;
;;; Provides the fundamental stream classes and generic functions required
;;; by trivial-gray-streams and flexi-streams.
;;;
;;; Package is named GRAY (same as ECL/CLISP) with "..." as nickname
;;; so trivial-gray-streams' fallback for unknown implementations finds it.
;;;
;;; Load BEFORE trivial-gray-streams (i.e., before quickloading libraries
;;; that depend on it).

(in-package "COMMON-LISP-USER")

(defpackage "GRAY"
  (:use "COMMON-LISP")
  (:nicknames "...")
  (:export
   ;; Classes
   #:fundamental-stream
   #:fundamental-input-stream
   #:fundamental-output-stream
   #:fundamental-character-stream
   #:fundamental-binary-stream
   #:fundamental-character-input-stream
   #:fundamental-character-output-stream
   #:fundamental-binary-input-stream
   #:fundamental-binary-output-stream
   ;; Generic functions
   #:stream-read-char
   #:stream-unread-char
   #:stream-read-char-no-hang
   #:stream-peek-char
   #:stream-listen
   #:stream-read-line
   #:stream-clear-input
   #:stream-write-char
   #:stream-line-column
   #:stream-start-line-p
   #:stream-write-string
   #:stream-terpri
   #:stream-fresh-line
   #:stream-finish-output
   #:stream-force-output
   #:stream-clear-output
   #:stream-advance-to-column
   #:stream-read-byte
   #:stream-write-byte))

(in-package "GRAY")

;;; ====================================================================
;;; Fundamental stream class hierarchy
;;; ====================================================================

(defclass fundamental-stream (standard-object)
  ((open-p :initform t :accessor stream-open-p))
  (:documentation "Base class for Gray streams."))

(defclass fundamental-input-stream (fundamental-stream)
  ()
  (:documentation "Base class for Gray input streams."))

(defclass fundamental-output-stream (fundamental-stream)
  ()
  (:documentation "Base class for Gray output streams."))

(defclass fundamental-character-stream (fundamental-stream)
  ()
  (:documentation "Base class for Gray character streams."))

(defclass fundamental-binary-stream (fundamental-stream)
  ()
  (:documentation "Base class for Gray binary streams."))

(defclass fundamental-character-input-stream
    (fundamental-input-stream fundamental-character-stream)
  ()
  (:documentation "Base class for Gray character input streams."))

(defclass fundamental-character-output-stream
    (fundamental-output-stream fundamental-character-stream)
  ()
  (:documentation "Base class for Gray character output streams."))

(defclass fundamental-binary-input-stream
    (fundamental-input-stream fundamental-binary-stream)
  ()
  (:documentation "Base class for Gray binary input streams."))

(defclass fundamental-binary-output-stream
    (fundamental-output-stream fundamental-binary-stream)
  ()
  (:documentation "Base class for Gray binary output streams."))

;;; ====================================================================
;;; Generic functions — character input
;;; ====================================================================

(defgeneric stream-read-char (stream)
  (:documentation "Read one character from STREAM. Return character or :EOF."))

(defgeneric stream-unread-char (stream character)
  (:documentation "Un-read CHARACTER on STREAM."))

(defgeneric stream-read-char-no-hang (stream)
  (:documentation "Read one character from STREAM if available. Return character, NIL, or :EOF."))

(defmethod stream-read-char-no-hang ((stream fundamental-character-input-stream))
  (stream-read-char stream))

(defgeneric stream-peek-char (stream)
  (:documentation "Peek at the next character from STREAM without consuming it."))

(defmethod stream-peek-char ((stream fundamental-character-input-stream))
  (let ((ch (stream-read-char stream)))
    (unless (eq ch :eof)
      (stream-unread-char stream ch))
    ch))

(defgeneric stream-listen (stream)
  (:documentation "Return true if a character is available on STREAM."))

(defmethod stream-listen ((stream fundamental-character-input-stream))
  (let ((ch (stream-read-char-no-hang stream)))
    (when (and ch (not (eq ch :eof)))
      (stream-unread-char stream ch)
      t)))

(defgeneric stream-read-line (stream)
  (:documentation "Read a line from STREAM. Return (values string missing-newline-p)."))

(defmethod stream-read-line ((stream fundamental-character-input-stream))
  (let ((result (make-string 0))
        (ch nil))
    (loop
      (setq ch (stream-read-char stream))
      (when (eq ch :eof)
        (return (values result t)))
      (when (char= ch #\Newline)
        (return (values result nil)))
      (setq result (concatenate 'string result (string ch))))))

(defgeneric stream-clear-input (stream)
  (:documentation "Clear buffered input on STREAM."))

(defmethod stream-clear-input ((stream fundamental-input-stream))
  nil)

;;; ====================================================================
;;; Generic functions — character output
;;; ====================================================================

(defgeneric stream-write-char (stream character)
  (:documentation "Write CHARACTER to STREAM."))

(defgeneric stream-line-column (stream)
  (:documentation "Return the column number of STREAM, or NIL if unknown."))

(defmethod stream-line-column ((stream fundamental-character-output-stream))
  nil)

(defgeneric stream-start-line-p (stream)
  (:documentation "Return true if STREAM is at the start of a line."))

(defmethod stream-start-line-p ((stream fundamental-character-output-stream))
  (let ((col (stream-line-column stream)))
    (and col (zerop col))))

(defgeneric stream-write-string (stream string &optional start end)
  (:documentation "Write STRING (or substring) to STREAM."))

(defmethod stream-write-string ((stream fundamental-character-output-stream)
                                string &optional (start 0) end)
  (let ((e (or end (length string))))
    (do ((i start (1+ i)))
        ((>= i e) string)
      (stream-write-char stream (char string i)))))

(defgeneric stream-terpri (stream)
  (:documentation "Write a newline to STREAM."))

(defmethod stream-terpri ((stream fundamental-character-output-stream))
  (stream-write-char stream #\Newline)
  nil)

(defgeneric stream-fresh-line (stream)
  (:documentation "Write a newline if STREAM is not at the start of a line."))

(defmethod stream-fresh-line ((stream fundamental-character-output-stream))
  (unless (stream-start-line-p stream)
    (stream-terpri stream)
    t))

(defgeneric stream-finish-output (stream)
  (:documentation "Ensure all output to STREAM has been sent."))

(defmethod stream-finish-output ((stream fundamental-output-stream))
  nil)

(defgeneric stream-force-output (stream)
  (:documentation "Initiate sending any buffered output on STREAM."))

(defmethod stream-force-output ((stream fundamental-output-stream))
  nil)

(defgeneric stream-clear-output (stream)
  (:documentation "Clear any buffered output on STREAM."))

(defmethod stream-clear-output ((stream fundamental-output-stream))
  nil)

(defgeneric stream-advance-to-column (stream column)
  (:documentation "Advance STREAM to COLUMN, writing spaces if necessary."))

(defmethod stream-advance-to-column ((stream fundamental-character-output-stream) column)
  (let ((cur (stream-line-column stream)))
    (when cur
      (dotimes (i (- column cur))
        (stream-write-char stream #\Space)))
    (not (null cur))))

;;; ====================================================================
;;; Generic functions — binary I/O
;;; ====================================================================

(defgeneric stream-read-byte (stream)
  (:documentation "Read one byte from STREAM. Return byte or :EOF."))

(defgeneric stream-write-byte (stream byte)
  (:documentation "Write BYTE to STREAM."))

;;; ====================================================================
;;; Integration with CL I/O functions
;;; ====================================================================
;;; Make CL:READ-CHAR, CL:WRITE-CHAR, etc. dispatch to gray stream
;;; generic functions when given a CLOS stream instance.

(in-package "COMMON-LISP")

;; Save original C builtins
(let ((orig-read-char (symbol-function 'read-char))
      (orig-write-char (symbol-function 'write-char))
      (orig-read-byte (symbol-function 'read-byte))
      (orig-write-byte (symbol-function 'write-byte))
      (orig-peek-char (symbol-function 'peek-char))
      (orig-unread-char (symbol-function 'unread-char))
      (orig-read-line (symbol-function 'read-line))
      (orig-write-string (symbol-function 'write-string))
      (orig-write-line (symbol-function 'write-line))
      (orig-terpri (symbol-function 'terpri))
      (orig-fresh-line (symbol-function 'fresh-line))
      (orig-finish-output (symbol-function 'finish-output))
      (orig-force-output (symbol-function 'force-output))
      (orig-clear-output (symbol-function 'clear-output))
      (orig-streamp (symbol-function 'streamp))
      (orig-input-stream-p (symbol-function 'input-stream-p))
      (orig-output-stream-p (symbol-function 'output-stream-p))
      (orig-close (symbol-function 'close))
      (orig-open-stream-p (symbol-function 'open-stream-p))
      (orig-stream-element-type (if (fboundp 'stream-element-type)
                                    (symbol-function 'stream-element-type)
                                    nil)))

  (defun %gray-stream-p (x)
    "Return T if X is a Gray stream (CLOS instance of fundamental-stream)."
    (and (typep x 'standard-object)
         (typep x 'gray:fundamental-stream)))

  (defun %resolve-input-stream (stream)
    (cond ((null stream) *standard-input*)
          ((eq stream t) *terminal-io*)
          (t stream)))

  (defun %resolve-output-stream (stream)
    (cond ((null stream) *standard-output*)
          ((eq stream t) *terminal-io*)
          (t stream)))

  ;; STREAMP — also true for gray streams
  (defun streamp (x)
    (or (funcall orig-streamp x)
        (%gray-stream-p x)))

  ;; INPUT-STREAM-P — GF so libraries can add methods
  (defgeneric input-stream-p (stream))
  (defmethod input-stream-p ((stream gray:fundamental-input-stream))
    t)
  (defmethod input-stream-p ((stream t))
    (funcall orig-input-stream-p stream))

  ;; OUTPUT-STREAM-P — GF so libraries can add methods
  (defgeneric output-stream-p (stream))
  (defmethod output-stream-p ((stream gray:fundamental-output-stream))
    t)
  (defmethod output-stream-p ((stream t))
    (funcall orig-output-stream-p stream))

  ;; CLOSE — define as GF so libraries can add methods (e.g. flexi-streams)
  ;; while built-in streams keep working via the default (T) method.
  (defgeneric close (stream &key abort))
  (defmethod close ((stream gray:fundamental-stream) &key abort)
    (declare (ignore abort))
    (setf (slot-value stream 'open-p) nil)
    t)
  (defmethod close ((stream t) &key abort)
    (declare (ignore abort))
    (funcall orig-close stream))

  ;; OPEN-STREAM-P — GF so libraries can add methods
  (defgeneric open-stream-p (stream))
  (defmethod open-stream-p ((stream gray:fundamental-stream))
    (slot-value stream 'open-p))
  (defmethod open-stream-p ((stream t))
    (funcall orig-open-stream-p stream))

  ;; STREAM-ELEMENT-TYPE — GF so libraries can add methods
  (defgeneric stream-element-type (stream))
  (defmethod stream-element-type ((stream t))
    (if orig-stream-element-type
        (funcall orig-stream-element-type stream)
        'character))

  ;; READ-CHAR
  (defun read-char (&optional stream (eof-error-p t) eof-value recursive-p)
    (declare (ignore recursive-p))
    (let ((s (%resolve-input-stream stream)))
      (if (%gray-stream-p s)
          (let ((result (gray:stream-read-char s)))
            (if (eq result :eof)
                (if eof-error-p
                    (error "READ-CHAR: end of file on ~A" s)
                    eof-value)
                result))
          (funcall orig-read-char s eof-error-p eof-value))))

  ;; WRITE-CHAR
  (defun write-char (character &optional stream)
    (let ((s (%resolve-output-stream stream)))
      (if (%gray-stream-p s)
          (progn (gray:stream-write-char s character) character)
          (funcall orig-write-char character s))))

  ;; READ-BYTE
  (defun read-byte (stream &optional (eof-error-p t) eof-value)
    (let ((s (%resolve-input-stream stream)))
      (if (%gray-stream-p s)
          (let ((result (gray:stream-read-byte s)))
            (if (eq result :eof)
                (if eof-error-p
                    (error "READ-BYTE: end of file on ~A" s)
                    eof-value)
                result))
          (funcall orig-read-byte s eof-error-p eof-value))))

  ;; WRITE-BYTE
  (defun write-byte (byte stream)
    (let ((s (%resolve-output-stream stream)))
      (if (%gray-stream-p s)
          (progn (gray:stream-write-byte s byte) byte)
          (funcall orig-write-byte byte s))))

  ;; PEEK-CHAR
  (defun peek-char (&optional peek-type stream (eof-error-p t) eof-value recursive-p)
    (declare (ignore recursive-p))
    (let ((s (%resolve-input-stream stream)))
      (if (%gray-stream-p s)
          (cond
            ((null peek-type)
             (let ((ch (gray:stream-peek-char s)))
               (if (eq ch :eof)
                   (if eof-error-p
                       (error "PEEK-CHAR: end of file")
                       eof-value)
                   ch)))
            ((eq peek-type t)
             ;; Skip whitespace
             (loop
               (let ((ch (gray:stream-read-char s)))
                 (cond ((eq ch :eof)
                        (return (if eof-error-p
                                    (error "PEEK-CHAR: end of file")
                                    eof-value)))
                       ((not (member ch '(#\Space #\Tab #\Newline #\Return #\Page)))
                        (gray:stream-unread-char s ch)
                        (return ch))))))
            ((characterp peek-type)
             ;; Skip until peek-type
             (loop
               (let ((ch (gray:stream-read-char s)))
                 (cond ((eq ch :eof)
                        (return (if eof-error-p
                                    (error "PEEK-CHAR: end of file")
                                    eof-value)))
                       ((char= ch peek-type)
                        (gray:stream-unread-char s ch)
                        (return ch)))))))
          (funcall orig-peek-char peek-type s eof-error-p eof-value))))

  ;; UNREAD-CHAR
  (defun unread-char (character &optional stream)
    (let ((s (%resolve-input-stream stream)))
      (if (%gray-stream-p s)
          (progn (gray:stream-unread-char s character) nil)
          (funcall orig-unread-char character s))))

  ;; READ-LINE
  (defun read-line (&optional stream (eof-error-p t) eof-value recursive-p)
    (declare (ignore recursive-p))
    (let ((s (%resolve-input-stream stream)))
      (if (%gray-stream-p s)
          (multiple-value-bind (line missing-newline-p)
              (gray:stream-read-line s)
            (if (and missing-newline-p (zerop (length line)))
                (if eof-error-p
                    (error "READ-LINE: end of file")
                    (values eof-value t))
                (values line missing-newline-p)))
          (funcall orig-read-line s eof-error-p eof-value))))

  ;; WRITE-STRING
  (defun write-string (string &optional stream &key (start 0) end)
    (let ((s (%resolve-output-stream stream)))
      (if (%gray-stream-p s)
          (progn (gray:stream-write-string s string start end) string)
          (funcall orig-write-string string s :start start :end end))))

  ;; WRITE-LINE
  (defun write-line (string &optional stream &key (start 0) end)
    (let ((s (%resolve-output-stream stream)))
      (if (%gray-stream-p s)
          (progn
            (gray:stream-write-string s string start end)
            (gray:stream-terpri s)
            string)
          (funcall orig-write-line string s :start start :end end))))

  ;; TERPRI
  (defun terpri (&optional stream)
    (let ((s (%resolve-output-stream stream)))
      (if (%gray-stream-p s)
          (progn (gray:stream-terpri s) nil)
          (funcall orig-terpri s))))

  ;; FRESH-LINE
  (defun fresh-line (&optional stream)
    (let ((s (%resolve-output-stream stream)))
      (if (%gray-stream-p s)
          (gray:stream-fresh-line s)
          (funcall orig-fresh-line s))))

  ;; FINISH-OUTPUT
  (defun finish-output (&optional stream)
    (let ((s (%resolve-output-stream stream)))
      (if (%gray-stream-p s)
          (gray:stream-finish-output s)
          (funcall orig-finish-output s))))

  ;; FORCE-OUTPUT
  (defun force-output (&optional stream)
    (let ((s (%resolve-output-stream stream)))
      (if (%gray-stream-p s)
          (gray:stream-force-output s)
          (funcall orig-force-output s))))

  ;; CLEAR-OUTPUT
  (defun clear-output (&optional stream)
    (let ((s (%resolve-output-stream stream)))
      (if (%gray-stream-p s)
          (gray:stream-clear-output s)
          (funcall orig-clear-output s))))

) ;; end let

;; CLEAR-INPUT — no C builtin, define directly
(defun clear-input (&optional stream)
  (let ((s (%resolve-input-stream stream)))
    (when (%gray-stream-p s)
      (gray:stream-clear-input s)))
  nil)
(export 'clear-input)

;; LISTEN — no C builtin, define directly
(defun listen (&optional stream)
  (let ((s (%resolve-input-stream stream)))
    (if (%gray-stream-p s)
        (gray:stream-listen s)
        nil)))
(export 'listen)

(in-package "COMMON-LISP-USER")
