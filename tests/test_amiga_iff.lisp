;;; test_amiga_iff.lisp — host-side checks of lib/amiga/iff.lisp
;;; (AMIGA.IFF, the iffparse.library module grown from the NDK 3.1 sift
;;; example), driven by tests/test_amiga_iff.sh.
;;;
;;; iffparse.library only exists on AmigaOS/MorphOS, so the module's
;;; real work runs on the Amiga (tests/amiga/test-iff.lisp is the
;;; functional specification, run by the FS-UAE suite).  What the host
;;; pins down: the module compiles and loads, the pure-Lisp helpers
;;; (ID-STRING / STRING-ID / ERROR-STRING) work everywhere, AVAILABLE-P
;;; answers NIL, every OS-facing entry point refuses cleanly instead of
;;; crashing into a NULL library base, and the example loads and bows
;;; out.

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

(require "amiga/iff")

(check "host-iff-not-available" nil (amiga.iff:available-p))

;; The pure-Lisp half works without iffparse.library.
(check "host-iff-id-string" "FORM" (amiga.iff:id-string #x464F524D))
(check "host-iff-string-id" #x494C424D (amiga.iff:string-id "ILBM"))
(check "host-iff-id-roundtrip" "8SVX"
  (amiga.iff:id-string (amiga.iff:string-id "8SVX")))
(check "host-iff-string-id-checks" '(t t)
  (list (handler-case (progn (amiga.iff:string-id "TOOLONG") nil)
          (error () t))
        (handler-case (progn (amiga.iff:string-id 42) nil)
          (error () t))))
(check "host-iff-error-strings"
       '("End of file (not an error)." "Not an IFF file.")
  (list (amiga.iff:error-string -1) (amiga.iff:error-string -10)))
(check "host-iff-error-string-unknown" t
  (and (search "Unknown" (amiga.iff:error-string -99)) t))

;; The OS-facing entry points refuse cleanly off-Amiga.
(check "host-iff-open-refuses" t
  (handler-case (progn (amiga.iff:open-iff "T:x.iff") nil)
    (error (e) (and (search "AmigaOS" (format nil "~A" e)) t))))

(check "host-iff-open-clipboard-refuses" t
  (handler-case (progn (amiga.iff:open-iff :clipboard) nil)
    (error (e) (and (search "AmigaOS" (format nil "~A" e)) t))))

(check "host-iff-sift-refuses" t
  (handler-case (progn (amiga.iff:sift "T:x.iff") nil)
    (error (e) (and (search "AmigaOS" (format nil "~A" e)) t))))

;; The accessors on a non-file complain about the type, not about NIL.
(check "host-iff-type-errors" '(t t)
  (list (handler-case (progn (amiga.iff:parse-step 42) nil)
          (error (e) (and (search "IFF-FILE" (format nil "~A" e)) t)))
        (handler-case (progn (amiga.iff:close-iff nil) nil)
          (error (e) (and (search "IFF-FILE" (format nil "~A" e)) t)))))

;; The example loads (all of its code compiles) and bows out.
(check "example-sift-loads-and-bows-out" t
  (let ((output (with-output-to-string (*standard-output*)
                  (load "examples/amiga/iff/sift.lisp"))))
    (and (search "not available" output) t)))

(format t "~%~D passed, ~D failed~%" *pass* *fail*)
(if (zerop *fail*)
    (format t "ALL IFF HOST CHECKS PASSED~%")
    (format t "SOME IFF HOST CHECKS FAILED~%"))
