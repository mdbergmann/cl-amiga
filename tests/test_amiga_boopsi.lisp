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
                          "NEW-LIST" "OBJECT-CLASS" "POOL-ALLOC" "POOL-STRING"
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
