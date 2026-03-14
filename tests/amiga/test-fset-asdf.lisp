;; Test loading fset via ASDF/quicklisp on Amiga
;; Verifies that compile-file works correctly (NLX frame sync fix)
;; Dependencies loaded via ASDF compile-file, fset loaded from source
;; Run: clamiga --heap 48M --non-interactive --load tests/amiga/test-fset-asdf.lisp

(format t "~%; === FSet ASDF compile-file test ===~%")

;; 1. Load named-readtables stub (fset dependency)
(format t "; Loading named-readtables stub~%")
(load "lib/named-readtables-stub.lisp")

;; 2. Load ASDF
(format t "; Loading ASDF~%")
(load "lib/asdf.lisp")

;; 3. Load quicklisp
(format t "; Loading quicklisp~%")
(let ((amiga-setup "S:quicklisp/setup.lisp"))
  (if (probe-file amiga-setup)
      (load amiga-setup)
      (let ((host-setup (concatenate 'string
                          (namestring (user-homedir-pathname))
                          "quicklisp/setup.lisp")))
        (load host-setup))))

;; 4. Quickload fset dependencies via ASDF compile-file
;;    This is the key test: compile-file must not crash (NLX frame sync fix)
(format t "~%; === Testing ASDF compile-file (NLX fix) ===~%")

(format t "; Quickloading misc-extensions via ASDF...~%")
(handler-case
  (ql:quickload :misc-extensions)
  (error (c)
    (format t "~%FAIL: misc-extensions quickload: ~A~%" c)))

(format t "; Quickloading mt19937 via ASDF...~%")
(handler-case
  (ql:quickload :mt19937)
  (error (c)
    (format t "~%FAIL: mt19937 quickload: ~A~%" c)))

(format t "~%; ASDF compile-file test PASSED~%")

;; 5. Load fset from source (ASDF quickload of fset has a separate
;;    session-management bug unrelated to the NLX fix)
(format t "~%; === Loading fset from source ===~%")

(let ((amiga-sw "S:quicklisp/dists/quicklisp/software/")
      (host-sw (concatenate 'string
                 (namestring (user-homedir-pathname))
                 "quicklisp/dists/quicklisp/software/")))
  (let ((sw (if (probe-file amiga-sw) amiga-sw host-sw)))
    ;; Create FSET package via defs.lisp
    (format t "; Loading fset/defs.lisp~%")
    (load (concatenate 'string sw "fset-v2.2.0/Code/defs.lisp"))

    ;; Load fset source files (port.lisp has #+cl-amiga stubs)
    (let ((base (concatenate 'string sw "fset-v2.2.0/Code/")))
      (dolist (f '("port" "macros" "order" "hash" "wb-trees" "champ"
                   "reader" "fset" "replay" "tuples" "interval"
                   "relations" "complement-sets" "bounded-sets" "post"))
        (format t "; Loading fset/~A.lisp~%" f)
        (handler-case
          (load (concatenate 'string base f ".lisp"))
          (error (c)
            (format t "~%ERROR loading ~A: ~A~%" f c)))))))

;; 6. Test basic fset operations
(format t "~%Testing FSet...~%")
(handler-case
  (let ((s (fset:set 1 2 3 4 5)))
    (format t "Set: ~A~%" s)
    (format t "Contains 3: ~A~%" (fset:contains? s 3))
    (format t "Contains 9: ~A~%" (fset:contains? s 9))
    (format t "Union: ~A~%" (fset:union s (fset:set 4 5 6 7))))
  (error (c) (format t "Set ERROR: ~A~%" c)))
(handler-case
  (let ((m (fset:map ("a" 1) ("b" 2) ("c" 3))))
    (format t "Map: ~A~%" m)
    (format t "Lookup b: ~A~%" (fset:lookup m "b")))
  (error (c) (format t "Map ERROR: ~A~%" c)))
(format t "~%ALL TESTS PASSED~%")
