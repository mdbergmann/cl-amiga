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
; :SHAKE-BINDINGS (spec Phase 3) — the image was saved with its binding
; table shed.  Names referenced before the save are ordinary symbols; the
; rest do not exist, and the reader says why instead of implying a typo.
(img-check "shed table keeps its marker" t
           (getf (clamiga::%binding-table-info "IMG-BT") :shed))
(img-check "shed table reports no entries" 0
           (getf (clamiga::%binding-table-info "IMG-BT") :entries))
(img-check "touched constant survives the shed" 11 (symbol-value *img-bt-kept*))
(img-check "touched name keeps its identity" t
           (eq *img-bt-kept* (find-symbol "+KEPT+" "IMG-BT")))
(img-check "touched libcall stub survives" -30
           (getf (ffi::%ffi-stub-info (symbol-function (find-symbol "IMG-CALL" "IMG-BT"))) :lvo))
(img-check "untouched name is gone" '(nil nil)
           (multiple-value-list (find-symbol "+GONE+" "IMG-BT")))
(img-check "untouched field accessor is gone" '(nil nil)
           (multiple-value-list (find-symbol "IMG-NODE-X" "IMG-BT")))
(img-check "reader explains a shed miss" t
           (handler-case (progn (read-from-string "img-bt:+gone+") nil)
             (error (c) (and (search "shed by SAVE-IMAGE" (format nil "~a" c)) t))))
(img-check "eager flip on a shed package is a no-op" t
           (progn (clamiga::%binding-table-materialize-all "IMG-BT")
                  (getf (clamiga::%binding-table-info "IMG-BT") :shed)))

; The restored heap must survive GC + compaction (offsets, JIT relink,
; blob-attached bytecode all get exercised by the collector).
(img-check "gc after restore" 55
           (progn (dotimes (i 20000) (cons i i))
                  (ext:gc)
                  (img-fib 10)))

(if (= *img-verify-fails* 0)
    (format t "~%IMAGE-VERIFY-PASSED~%")
    (format t "~%IMAGE-VERIFY-FAILED: ~a check(s)~%" *img-verify-fails*))
