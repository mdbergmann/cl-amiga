;;; test_amiga_boopsi.lisp — host-side checks of lib/amiga/boopsi.lisp
;;; (AMIGA.BOOPSI, the toolkit-neutral BOOPSI helpers), driven by
;;; tests/test_amiga_boopsi.sh.
;;;
;;; The host has no BOOPSI objects (no AmigaOS at all): what can be
;;; checked here is the portable half of the module — the ULONG coercion,
;;; the foreign pool, NEW-LIST's exec list header, WITH-TAGS' TagItem
;;; layout, DO-METHOD's message layout is exercised on the Amiga — plus
;;; the layering itself: the module loads with no toolkit package in
;;; sight, and AMIGA.REACTION re-exports these very symbols instead of
;;; homonyms.  tests/amiga/test-boopsi.lisp drives real objects of the
;;; built-in intuition classes on the Amiga.

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

;;; --- layering: BOOPSI stands alone ------------------------------------

(check "boopsi-not-loaded-yet" nil (find-package "AMIGA.BOOPSI"))

(require "amiga/boopsi")

(check "boopsi-loads-without-any-toolkit" '(t nil nil)
  (list (and (find-package "AMIGA.BOOPSI") t)
        (find-package "AMIGA.REACTION")
        (find-package "AMIGA.MUI")))

(check "boopsi-exports" '("DO-METHOD" "FREE-LIST-NODES" "GET-ATTR" "GET-ATTR-POINTER"
                          "NEW-LIST" "OBJECT-CLASS" "POOL-ALLOC" "POOL-FINALIZER"
                          "POOL-HOOK" "POOL-STRING"
                          "SET-ATTRS" "WITH-FOREIGN-POOL" "WITH-TAGS")
  (let ((names '()))
    (do-external-symbols (s "AMIGA.BOOPSI") (push (symbol-name s) names))
    (sort names #'string<)))

;;; --- ULONG coercion ----------------------------------------------------

(check "boopsi-ulong-coercions" '(0 1 5 #xFFFFFFFF #x80000000)
  (list (amiga.boopsi::%ulong nil) (amiga.boopsi::%ulong t)
        (amiga.boopsi::%ulong 5) (amiga.boopsi::%ulong -1)
        (amiga.boopsi::%ulong #x80000000)))

(check "boopsi-ulong-pointer-is-its-address" t
  (let ((p (ffi:alloc-foreign 4)))
    (prog1 (= (amiga.boopsi::%ulong p) (ffi:foreign-pointer-address p))
      (ffi:free-foreign p))))

(check "boopsi-ulong-rejects-strings" t
  (handler-case (progn (amiga.boopsi::%ulong "no") nil)
    (error (e) (and (search "AMIGA.BOOPSI" (format nil "~A" e)) t))))

;;; --- the foreign pool --------------------------------------------------

(check "boopsi-pool-required" t
  (handler-case (progn (amiga.boopsi:pool-string "x") nil)
    (error (e) (let ((m (format nil "~A" e)))
                 (and (search "WITH-FOREIGN-POOL" m) (search "AMIGA.BOOPSI" m) t)))))

(check "boopsi-pool-alloc-required" t
  (handler-case (progn (amiga.boopsi:pool-alloc 8) nil)
    (error (e) (and (search "WITH-FOREIGN-POOL" (format nil "~A" e)) t))))

(check "boopsi-pool-string-roundtrip" "BOOPSI"
  (amiga.boopsi:with-foreign-pool ()
    (ffi:foreign-to-string (amiga.boopsi:pool-string "BOOPSI"))))

(check "boopsi-pool-alloc-zeroed" 0
  (amiga.boopsi:with-foreign-pool ()
    (let ((p (amiga.boopsi:pool-alloc 16)))
      (logior (ffi:peek-u32 p 0) (ffi:peek-u32 p 12)))))

;; the pool frees on a non-local exit too: the pointers are released
;; (FFI:FREE-FOREIGN of a freed pointer would error) — we only assert
;; the unwind reaches the body's THROW
(check "boopsi-pool-unwinds" :thrown
  (catch 'out
    (amiga.boopsi:with-foreign-pool ()
      (amiga.boopsi:pool-string "gone")
      (throw 'out :thrown))))

;; pools nest: the inner pool's allocations are gone when it exits, the
;; outer one's survive it
(check "boopsi-pools-nest" '(2 1)
  (amiga.boopsi:with-foreign-pool ()
    (amiga.boopsi:pool-string "outer")
    (let ((inner-count
            (amiga.boopsi:with-foreign-pool ()
              (amiga.boopsi:pool-string "inner-1")
              (amiga.boopsi:pool-string "inner-2")
              (length (car amiga.boopsi::*foreign-pool*)))))
      (list inner-count (length (car amiga.boopsi::*foreign-pool*))))))

;;; --- hooks in the pool ---------------------------------------------------
;;; On the host a MAKE-HOOK hook's entry is a plain C function of
;;; (hook, object, message) -- libffi's callback -- so it can be driven
;;; through FFI:CALL-FOREIGN exactly as CallHookPkt drives it on the Amiga
;;; (tests/amiga/test-raw-bindings.lisp does that half).

(defun call-hook (hook object message)
  (ffi:call-foreign (amiga.ffi:hook-entry hook) :uint32 '(:pointer :pointer :pointer)
                    (list hook object message)))

;; finalizers run at pool exit, most recent first, after the pointers
;; registered after them and before those registered before
(check "boopsi-pool-finalizers-run-in-reverse-order" '(:second :first)
  (let ((order '()))
    (amiga.boopsi:with-foreign-pool ()
      (amiga.boopsi:pool-finalizer (lambda () (push :first order)))
      (amiga.boopsi:pool-string "between")
      (amiga.boopsi:pool-finalizer (lambda () (push :second order))))
    (reverse order)))

(check "boopsi-pool-finalizer-runs-on-unwind" :ran
  (let ((state :not-run))
    (catch 'out
      (amiga.boopsi:with-foreign-pool ()
        (amiga.boopsi:pool-finalizer (lambda () (setf state :ran)))
        (throw 'out nil)))
    state))

(check "boopsi-pool-finalizer-rejects-non-functions" t
  (handler-case (progn (amiga.boopsi:with-foreign-pool () (amiga.boopsi:pool-finalizer 5)) nil)
    (error (e) (and (search "POOL-FINALIZER" (format nil "~A" e)) t))))

;; POOL-HOOK: the hook receives hook / object / message (a0 / a2 / a1 on
;; the Amiga, the three C arguments here), its return value reaches the
;; caller as a ULONG, h_Data reads back
(check "boopsi-pool-hook-round-trip" '(4711 t #x1234 #xCAFE 99 7)
  (amiga.boopsi:with-foreign-pool ()
    (let* ((seen nil)
           (message (amiga.boopsi:pool-alloc 4))
           (hook (amiga.boopsi:pool-hook
                  (lambda (h o m)
                    (setf seen (list (ffi:foreign-pointer-address h)
                                     (ffi:foreign-pointer-address o)
                                     (ffi:peek-u32 m 0)))
                    4711)
                  :data 99)))
      (ffi:poke-u32 message #xCAFE 0)
      (let ((result (call-hook hook (ffi:make-foreign-pointer #x1234) message)))
        (list result
              (= (first seen) (ffi:foreign-pointer-address hook))   ; a0 = the hook itself
              (second seen)                                        ; a2 = the object
              (third seen)                                         ; a1 = the message
              (amiga.ffi:hook-data hook)
              (progn (setf (amiga.ffi:hook-data hook) 7)
                     (amiga.ffi:hook-data hook)))))))

(check "boopsi-pool-hook-needs-a-pool" t
  (handler-case (progn (amiga.boopsi:pool-hook (lambda (h o m) h o m 0)) nil)
    (error (e) (and (search "WITH-FOREIGN-POOL" (format nil "~A" e)) t))))

;; the hook is freed by the pool: its entry is gone afterwards
(check "boopsi-pool-hook-freed-at-pool-exit" t
  (let ((hook (amiga.boopsi:with-foreign-pool ()
                (amiga.boopsi:pool-hook (lambda (h o m) h o m 1)))))
    (handler-case (progn (amiga.ffi:hook-entry hook) nil)
      (error (e) (and (search "not a live MAKE-HOOK hook" (format nil "~A" e)) t)))))

;; the foreign-callback boundary through a hook: an error in the hook
;; function does not unwind through the C caller -- the hook returns 0 and
;; the condition is re-signaled once the call returns, where a
;; HANDLER-CASE around it catches it; a bad return value is reported the
;; same way, naming the value
(check "boopsi-pool-hook-error-is-deferred" '("hook boom" t)
  (amiga.boopsi:with-foreign-pool ()
    (let ((bad (amiga.boopsi:pool-hook (lambda (h o m) h o m (error "hook boom"))))
          (bad-value (amiga.boopsi:pool-hook (lambda (h o m) h o m "not a ulong"))))
      (list (handler-case (progn (call-hook bad nil nil) :no-error)
              (error (e) (format nil "~A" e)))
            (handler-case (progn (call-hook bad-value nil nil) nil)
              (error (e) (let ((msg (format nil "~A" e)))
                           (and (search "\"not a ulong\"" msg) (search "ULONG" msg) t))))))))

(check "boopsi-pool-hook-throw-across-boundary-is-refused" t
  (amiga.boopsi:with-foreign-pool ()
    (let ((hook (amiga.boopsi:pool-hook (lambda (h o m) h o m (throw 'outside 1)))))
      (handler-case (catch 'outside (call-hook hook nil nil) nil)
        (error (e) (and (search "foreign callback" (format nil "~A" e)) t))))))

;; (the host's foreign addresses are 64-bit and the exec list holds
;; longwords, so compare modulo 2^32 — on the Amiga that is the identity)
(check "boopsi-new-list-initialised" '(4 0 0)
  (amiga.boopsi:with-foreign-pool ()
    (let* ((l (amiga.boopsi:new-list))
           (addr (ffi:foreign-pointer-address l)))
      (list (logand (- (ffi:peek-u32 l 0) addr) #xFFFFFFFF)
            (ffi:peek-u32 l 4)
            (logand (- (ffi:peek-u32 l 8) addr) #xFFFFFFFF)))))

;;; --- tag lists ---------------------------------------------------------

;; a string value becomes a (pooled) foreign string's address; on the
;; host that address is truncated to the TagItem's longword, so only its
;; presence is checked here — the Amiga test reads the string back
(check "boopsi-with-tags-layout" '(#x80000001 7 #x80000002 t #x80000003 #xFFFFFFFF 0)
  (amiga.boopsi:with-foreign-pool ()
    (amiga.boopsi:with-tags (tags #x80000001 7 #x80000002 "abc" #x80000003 -1)
      (list (ffi:peek-u32 tags 0) (ffi:peek-u32 tags 4)
            (ffi:peek-u32 tags 8)
            (not (zerop (ffi:peek-u32 tags 12)))
            (ffi:peek-u32 tags 16) (ffi:peek-u32 tags 20)
            (ffi:peek-u32 tags 24)))))

(check "boopsi-with-tags-t-nil-pointer-values" '(1 0 t)
  (amiga.boopsi:with-foreign-pool ()
    (let ((p (amiga.boopsi:pool-alloc 4)))
      (amiga.boopsi:with-tags (tags 1 t 2 nil 3 p)
        (list (ffi:peek-u32 tags 4) (ffi:peek-u32 tags 12)
              (= (ffi:peek-u32 tags 20)
                 (logand (ffi:foreign-pointer-address p) #xFFFFFFFF)))))))

;; an empty tag list is just TAG_DONE
(check "boopsi-with-tags-empty" '(0 0)
  (amiga.boopsi:with-tags (tags)
    (list (ffi:peek-u32 tags 0) (ffi:peek-u32 tags 4))))

;; the string copy is pooled: without a pool WITH-TAGS with a string
;; value errors, and the array is not leaked (its allocation is undone)
(check "boopsi-with-tags-string-needs-pool" t
  (handler-case (amiga.boopsi:with-tags (tags 1 "abc") (declare (ignore tags)) nil)
    (error (e) (and (search "WITH-FOREIGN-POOL" (format nil "~A" e)) t))))

(check "boopsi-with-tags-odd-length-is-error" t
  (handler-case (amiga.boopsi:with-tags (tags 1 2 3) (declare (ignore tags)) nil)
    (error () t)))

(check "boopsi-with-tags-non-integer-tag-is-error" t
  (handler-case (amiga.boopsi:with-tags (tags "tag" 2) (declare (ignore tags)) nil)
    (error (e) (let ((m (format nil "~A" e)))
                 (and (search "not an integer" m) (search "AMIGA.BOOPSI" m) t)))))

(check "boopsi-with-tags-bad-value-is-error" t
  (handler-case (amiga.boopsi:with-tags (tags 1 :keyword) (declare (ignore tags)) nil)
    (error (e) (and (search "ULONG" (format nil "~A" e)) t))))

;;; --- the layering, seen from AMIGA.REACTION -----------------------------

(require "amiga/reaction")

;; every re-exported name is the SAME symbol -- one implementation, not a
;; homonym that happens to behave alike
(check "reaction-reexports-boopsi-symbols" t
  (let ((ok t))
    (do-external-symbols (s "AMIGA.BOOPSI")
      (multiple-value-bind (r status) (find-symbol (symbol-name s) "AMIGA.REACTION")
        (unless (and (eq r s) (eq status :external))
          (format t "  ~A: ~S ~S~%" (symbol-name s) r status)
          (setf ok nil))))
    ok))

;; ... and the pool is one dynamic extent for both spellings
(check "reaction-pool-is-boopsi-pool" "shared"
  (amiga.reaction:with-foreign-pool ()
    (ffi:foreign-to-string (amiga.boopsi:pool-string "shared"))))

(format t "~%~D passed, ~D failed~%" *pass* *fail*)
(if (zerop *fail*)
    (format t "ALL BOOPSI HOST CHECKS PASSED~%")
    (format t "SOME BOOPSI HOST CHECKS FAILED~%"))
