;;; test_amiga_reaction.lisp — host-side checks of lib/amiga/reaction.lisp
;;; (AMIGA.REACTION), driven by tests/test_amiga_reaction.sh.
;;;
;;; The host has no ReAction (no AmigaOS at all): what can be checked here
;;; is the portable half of the module — the ULONG coercion, the foreign
;;; pool, NEW-LIST's exec list header, WITH-TAGS' TagItem layout, the
;;; error diagnostics — plus that every example under
;;; examples/amiga/reaction/ loads (all of its code compiles) and bows
;;; out with its "not available" line.  tests/amiga/test-reaction.lisp
;;; runs the same portable checks on the Amiga and adds the class half.

(defvar *pass* 0)
(defvar *fail* 0)

(defmacro check (name expected form)
  `(handler-case
       (let ((e ,expected) (a ,form))
         (if (equal e a)
             (progn (incf *pass*) (format t "PASS: ~A~%" ,name))
             (progn (incf *fail*) (format t "FAIL: ~A - expected ~S got ~S~%" ,name e a))))
     (error (c)
       (incf *fail*)
       (format t "FAIL: ~A - signaled error: ~A~%" ,name c))))

(require "amiga/reaction")

(check "host-reaction-not-available" nil (amiga.reaction:available-p))

(check "reaction-ulong-coercions" '(0 1 5 #xFFFFFFFF #x80000000)
  (list (amiga.reaction::%ulong nil) (amiga.reaction::%ulong t)
        (amiga.reaction::%ulong 5) (amiga.reaction::%ulong -1)
        (amiga.reaction::%ulong #x80000000)))

(check "reaction-ulong-pointer-is-its-address" t
  (let ((p (ffi:alloc-foreign 4)))
    (prog1 (= (amiga.reaction::%ulong p) (ffi:foreign-pointer-address p))
      (ffi:free-foreign p))))

(check "reaction-ulong-rejects-strings" t
  (handler-case (progn (amiga.reaction::%ulong "no") nil)
    (error () t)))

(check "reaction-pool-required" t
  (handler-case (progn (amiga.reaction:pool-string "x") nil)
    (error (e) (and (search "WITH-FOREIGN-POOL" (format nil "~A" e)) t))))

(check "reaction-pool-string-roundtrip" "ReAction"
  (amiga.reaction:with-foreign-pool ()
    (ffi:foreign-to-string (amiga.reaction:pool-string "ReAction"))))

(check "reaction-pool-alloc-zeroed" 0
  (amiga.reaction:with-foreign-pool ()
    (let ((p (amiga.reaction:pool-alloc 16)))
      (logior (ffi:peek-u32 p 0) (ffi:peek-u32 p 12)))))

;; the pool frees on a non-local exit too: the pointers are released
;; (FFI:FREE-FOREIGN of a freed pointer would error) — we only assert
;; the unwind reaches the body's THROW
(check "reaction-pool-unwinds" :thrown
  (catch 'out
    (amiga.reaction:with-foreign-pool ()
      (amiga.reaction:pool-string "gone")
      (throw 'out :thrown))))

;; (the host's foreign addresses are 64-bit and the exec list holds
;; longwords, so compare modulo 2^32 — on the Amiga that is the identity)
(check "reaction-new-list-initialised" '(4 0 0)
  (amiga.reaction:with-foreign-pool ()
    (let* ((l (amiga.reaction:new-list))
           (addr (ffi:foreign-pointer-address l)))
      (list (logand (- (ffi:peek-u32 l 0) addr) #xFFFFFFFF)
            (ffi:peek-u32 l 4)
            (logand (- (ffi:peek-u32 l 8) addr) #xFFFFFFFF)))))

;; a string value becomes a (pooled) foreign string's address; on the
;; host that address is truncated to the TagItem's longword, so only its
;; presence is checked here — the Amiga test reads the string back
(check "reaction-with-tags-layout" '(#x80000001 7 #x80000002 t #x80000003 #xFFFFFFFF 0)
  (amiga.reaction:with-foreign-pool ()
    (amiga.reaction:with-tags (tags #x80000001 7 #x80000002 "abc" #x80000003 -1)
      (list (ffi:peek-u32 tags 0) (ffi:peek-u32 tags 4)
            (ffi:peek-u32 tags 8)
            (not (zerop (ffi:peek-u32 tags 12)))
            (ffi:peek-u32 tags 16) (ffi:peek-u32 tags 20)
            (ffi:peek-u32 tags 24)))))

(check "reaction-with-tags-t-nil-pointer-values" '(1 0 t)
  (amiga.reaction:with-foreign-pool ()
    (let ((p (amiga.reaction:pool-alloc 4)))
      (amiga.reaction:with-tags (tags 1 t 2 nil 3 p)
        (list (ffi:peek-u32 tags 4) (ffi:peek-u32 tags 12)
              (= (ffi:peek-u32 tags 20)
                 (logand (ffi:foreign-pointer-address p) #xFFFFFFFF)))))))

(check "reaction-with-tags-odd-length-is-error" t
  (handler-case (amiga.reaction:with-tags (tags 1 2 3) (declare (ignore tags)) nil)
    (error () t)))

(check "reaction-with-tags-non-integer-tag-is-error" t
  (handler-case (amiga.reaction:with-tags (tags "tag" 2) (declare (ignore tags)) nil)
    (error (e) (and (search "not an integer" (format nil "~A" e)) t))))

(check "reaction-new-object-rejects-nil-class" t
  (handler-case (progn (amiga.reaction:new-object nil 1 2) nil)
    (error (e) (and (search "class pointer" (format nil "~A" e)) t))))

(check "reaction-do-window-events-is-a-block" :done
  ;; the macro must expand even though it cannot run here
  (progn (macroexpand-1 '(amiga.reaction:do-window-events ((r c) obj) (return)))
         :done))

(check "reaction-event-loop-timeout-default-nil" nil amiga.reaction:*event-loop-timeout*)

;;; --- the examples load on the host and bow out ------------------------

(defparameter *examples*
  '("buttons" "checkbox" "chooser" "clicktab" "fuelgauge" "integer"
    "listbrowser" "requester"))

(dolist (name *examples*)
  (check (format nil "example-~A-loads-and-bows-out" name) t
    (let ((output (with-output-to-string (*standard-output*)
                    (load (format nil "examples/amiga/reaction/~A.lisp" name)))))
      (and (search "not available" output) t))))

(format t "~%~D passed, ~D failed~%" *pass* *fail*)
(if (zerop *fail*)
    (format t "ALL REACTION HOST CHECKS PASSED~%")
    (format t "SOME REACTION HOST CHECKS FAILED~%"))
