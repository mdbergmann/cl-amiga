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

  ;; Native streams must still pass.
  (let ((ss (make-string-output-stream)))
    (check "typep native stream 'stream" t (typep ss 'stream))))

(format t "~%Passed: ~A  Failed: ~A~%" *pass-count* *fail-count*)
(if (= *fail-count* 0)
    (format t "ALL TESTS PASSED~%")
    (progn (format t "SOME TESTS FAILED~%")
           (ext:quit 1)))
