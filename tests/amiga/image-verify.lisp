; Heap-image restore leg: runs in a `clamiga --image clamiga-test.img`
; session (see tests/amiga/image-save.lisp for the state this checks).
; Prints IMAGE-VERIFY-PASSED only if every check holds — the marker
; Makefile.cross's verify-amiga requires.

(setq *img-verify-fails* 0)
(defmacro img-check (name expected actual)
  `(handler-case
       (let ((e ,expected) (a ,actual))
         (if (equal e a)
             (format t "PASS: image ~a~%" ,name)
             (progn (setq *img-verify-fails* (+ *img-verify-fails* 1))
                    (format t "FAIL: image ~a - expected ~s got ~s~%"
                            ,name e a))))
     (error (c)
       (setq *img-verify-fails* (+ *img-verify-fails* 1))
       (format t "FAIL: image ~a - signaled: ~a~%" ,name c))))

(img-check "restored-p" t ext:*image-restored-p*)
(img-check "function" 144 (img-fib 12))
(img-check "macro" 42 (img-twice 21))
(img-check "clos dispatch" "Rex speaks" (img-speak *img-pet*))
(img-check "new instance" "Fido speaks"
           (img-speak (make-instance 'img-animal :name "Fido")))
(img-check "struct" 3 (img-point-x *img-pt*))
(img-check "eq hashtable identity" 'img-hit (gethash *img-key* *img-ht*))
(img-check "closure over cell" 42 (funcall *img-close* 11))
(img-check "readtable macro" 4242 (read-from-string "!"))
(img-check "string-output-stream buffer" "pre-save;post"
           (progn (write-string "post" *img-sos*)
                  (get-output-stream-string *img-sos*)))
(img-check "restored lock" t
           (progn (mp:acquire-lock *img-lock*)
                  (mp:release-lock *img-lock*)
                  t))
(img-check "restored condvar usable" t
           (progn (mp:condition-notify *img-cv*) t))
(img-check "dead thread not alive" nil (mp:thread-alive-p *img-dead-thr*))
(img-check "dead thread join signals" t
           (handler-case (progn (mp:join-thread *img-dead-thr*) nil)
             (error () t)))
; The restored heap must survive GC + compaction (offsets, JIT relink,
; blob-attached bytecode all get exercised by the collector).
(img-check "gc after restore" 55
           (progn (dotimes (i 20000) (cons i i))
                  (ext:gc)
                  (img-fib 10)))

(if (= *img-verify-fails* 0)
    (format t "~%IMAGE-VERIFY-PASSED~%")
    (format t "~%IMAGE-VERIFY-FAILED: ~a check(s)~%" *img-verify-fails*))
