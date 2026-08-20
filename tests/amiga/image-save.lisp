; Heap-image save leg (driven by verify/realamiga/call-on-ustartup):
; build one instance of every state class the image format must carry,
; then save and quit.  tests/amiga/image-verify.lisp checks the restored
; session; Makefile.cross's verify-amiga requires its IMAGE-VERIFY-PASSED
; marker in the results log.

(defun img-fib (n) (if (< n 2) n (+ (img-fib (- n 1)) (img-fib (- n 2)))))
(defmacro img-twice (x) `(* 2 ,x))

(defclass img-animal () ((name :initarg :name :accessor img-name)))
(defmethod img-speak ((a img-animal)) (format nil "~a speaks" (img-name a)))
(defvar *img-pet* (make-instance 'img-animal :name "Rex"))

(defstruct img-point x y)
(defvar *img-pt* (make-img-point :x 3 :y 4))

(defvar *img-ht* (make-hash-table :test 'eq))
(defvar *img-key* (cons 'k 'v))
(setf (gethash *img-key* *img-ht*) 'img-hit)

(defvar *img-close* (let ((n 31)) (lambda (x) (+ x n))))
(set-macro-character #\! (lambda (s c) (declare (ignore s c)) 4242))

(defvar *img-sos* (make-string-output-stream))
(write-string "pre-save;" *img-sos*)

(defvar *img-lock* (mp:make-lock "img-lock"))
(defvar *img-cv* (mp:make-condition-variable "img-cv"))
(defvar *img-dead-thr* (mp:make-thread (lambda () 99)))
(mp:join-thread *img-dead-thr*)

(push (lambda () (format t "IMAGE-RESTORE-HOOK-RAN~%")) ext:*restore-hooks*)

(format t "IMAGE-STATE-BUILT fib10=~a~%" (img-fib 10))
(ext:save-image "build/amiga/clamiga-test.img" :quit t)
