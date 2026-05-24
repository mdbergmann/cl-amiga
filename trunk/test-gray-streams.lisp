;; Regression test: (typep gray-stream 'stream) => T
;;
;; Verifies the fix for the bug where Gray stream instances (CLOS objects)
;; were not recognised as STREAMs by TYPEP, even though STREAMP and
;; OUTPUT-STREAM-P already returned T.
;;
;; Usage (host):
;;   ./build/host/clamiga --heap 24M --load trunk/test-gray-streams.lisp
;;
;; Usage (Amiga, from the Amiga shell):
;;   stack 65536
;;   clamiga --heap 24M --load trunk/test-gray-streams.lisp

(setq *pass-count* 0)
(setq *fail-count* 0)

(defmacro check (name expected actual)
  (let ((e (gensym "E")) (a (gensym "A")) (c (gensym "C")))
    `(handler-case
         (let ((,e ,expected) (,a ,actual))
           (if (equal ,e ,a)
               (progn (incf *pass-count*)
                      (format t "PASS: ~A~%" ,name))
               (progn (incf *fail-count*)
                      (format t "FAIL: ~A  expected ~S  got ~S~%" ,name ,e ,a))))
       (error (,c)
         (incf *fail-count*)
         (format t "FAIL: ~A  signaled: ~A~%" ,name ,c)))))

(format t "~%--- Loading gray-streams ---~%")
(load #-amigaos "lib/gray-streams.lisp"
       #+amigaos "lib/gray-streams.lisp")

(format t "~%--- Gray stream typep regression ---~%")

;; Define a minimal output-only Gray stream subclass.
(defclass test-gray-out-stream (gray:fundamental-output-stream) ())

;; GRAY::STREAM-WRITE-CHAR is the only mandatory output method; provide a
;; no-op so the class is complete enough for type-check tests.
(defmethod gray:stream-write-char ((s test-gray-out-stream) char)
  (declare (ignore s char))
  nil)

(let ((g (make-instance 'test-gray-out-stream)))

  ;; These two were already passing before the fix.
  (check "streamp on gray instance"         t (streamp g))
  (check "output-stream-p on gray instance" t (output-stream-p g))

  ;; This is the regression: was returning NIL before the fix.
  (check "typep gray-stream 'stream"        t (typep g 'stream))

  ;; Verify the negative cases are unaffected.
  (check "typep fixnum 'stream"   nil (typep 42 'stream))
  (check "typep string 'stream"   nil (typep "hi" 'stream))
  (check "typep nil 'stream"      nil (typep nil 'stream))

  ;; Gray output stream must not match built-in stream subtypes.
  (check "gray not file-stream"    nil (typep g 'file-stream))
  (check "gray not string-stream"  nil (typep g 'string-stream))
  (check "gray not synonym-stream" nil (typep g 'synonym-stream))

  ;; Native streams must still pass.
  (let ((ss (make-string-output-stream)))
    (check "typep native stream 'stream" t (typep ss 'stream))))

(format t "~%--- Gray fundamental-input-stream typep ---~%")

;; A minimal input-only Gray stream subclass.
(defclass test-gray-in-stream (gray:fundamental-input-stream) ())

(defmethod gray:stream-read-char ((s test-gray-in-stream))
  (declare (ignore s))
  :eof)

(let ((gi (make-instance 'test-gray-in-stream)))
  (check "streamp on gray input instance"           t   (streamp gi))
  (check "typep gray-input-stream 'stream"          t   (typep gi 'stream))
  (check "output-stream-p on gray input instance"   nil (output-stream-p gi)))

(format t "~%--- Printer functions route to Gray streams (item-2) ---~%")

;; A capturing Gray output stream that stores chars for later inspection.
(defclass test-gray-cap-stream (gray:fundamental-output-stream)
  ((buf :initform nil)))

(defmethod gray:stream-write-char ((s test-gray-cap-stream) c)
  (setf (slot-value s 'buf) (cons c (slot-value s 'buf))))

(defun gray-cap-flush (s)
  "Return accumulated string and reset the buffer."
  (let ((str (coerce (nreverse (slot-value s 'buf)) 'string)))
    (setf (slot-value s 'buf) nil)
    str))

(let ((g (make-instance 'test-gray-cap-stream)))

  ;; PRINC routes to gray stream.
  (check "princ to gray-stream"
         "hello"
         (progn (princ "hello" g) (gray-cap-flush g)))

  ;; PRIN1 routes to gray stream (strings get quotes).
  (check "prin1 to gray-stream"
         "\"hi\""
         (progn (prin1 "hi" g) (gray-cap-flush g)))

  ;; WRITE :stream routes to gray stream.
  (check "write to gray-stream"
         "\"hi\""
         (progn (write "hi" :stream g) (gray-cap-flush g)))

  ;; FORMAT with a stream destination routes to gray stream.
  (check "format to gray-stream"
         "42"
         (progn (format g "~A" 42) (gray-cap-flush g)))

  ;; FORMAT nil is unaffected — still returns a string.
  (check "format nil unaffected" "99" (format nil "~A" 99))

  ;; PRINT prepends newline and appends space.
  (check "print to gray-stream"
         (concatenate 'string (string #\Newline) "42 ")
         (progn (print 42 g) (gray-cap-flush g)))

  ;; PPRINT routes to gray stream (produces at least one char of output).
  (check "pprint to gray-stream (non-empty)"
         t
         (progn (pprint '(a b c) g) (> (length (gray-cap-flush g)) 0)))

  ;; The motivating SLDB case: PRINC on a condition must not leak to terminal.
  (handler-case
      (error "slynk-test-condition")
    (error (e)
      (princ e g)
      (let ((s (gray-cap-flush g)))
        (check "princ condition to gray-stream (non-empty)" t (> (length s) 0))))))

(format t "~%Passed: ~A  Failed: ~A~%" *pass-count* *fail-count*)
(if (= *fail-count* 0)
    (format t "ALL TESTS PASSED~%")
    (progn (format t "SOME TESTS FAILED~%")
           (ext:quit 1)))
