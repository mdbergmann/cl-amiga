;; Test loading fset on Amiga — all from source, no ASDF compile-file
;; Requires: quicklisp dists available (S:quicklisp/ on Amiga, ~/quicklisp/ on host)
;; Run: clamiga --heap 48M --non-interactive --load tests/amiga/test-fset.lisp

(format t "~%; === FSet source loading test ===~%")

;; Determine base path for quicklisp software
(defvar *ql-software*
  (let ((amiga-path "S:quicklisp/dists/quicklisp/software/"))
    (if (probe-file amiga-path)
        amiga-path
        ;; Host: try ~/quicklisp/
        (let ((host-path (concatenate 'string
                           (namestring (user-homedir-pathname))
                           "quicklisp/dists/quicklisp/software/")))
          (if (probe-file host-path)
              host-path
              (error "Cannot find quicklisp software directory"))))))

(format t "; Using quicklisp software at: ~A~%" *ql-software*)

;; Load named-readtables stub
(format t "~%; Loading named-readtables stub~%")
(load "lib/named-readtables-stub.lisp")

;; Load misc-extensions from source
(let ((base (concatenate 'string *ql-software* "misc-extensions-20260101-git/src/")))
  (dolist (f '("defs" "new-let" "fn" "gmap" "rev-fun-bind" "define-class" "contexts"))
    (format t "~%; Loading misc-extensions/~A.lisp~%" f)
    (handler-case
      (load (concatenate 'string base f ".lisp"))
      (error (c) (format t "~%ERROR loading misc-extensions/~A: ~A~%" f c)))))

;; Load mt19937 from source
(format t "~%; Loading mt19937.lisp~%")
(handler-case
  (load (concatenate 'string *ql-software* "mt19937-1.1.1/mt19937.lisp"))
  (error (c) (format t "~%ERROR loading mt19937: ~A~%" c)))

;; Create FSET package via defs.lisp
(format t "~%; Loading fset/defs.lisp~%")
(load (concatenate 'string *ql-software* "fset-v2.2.0/Code/defs.lisp"))

;; Load compat stubs now that FSET package exists
(format t "~%; Loading fset-compat.lisp~%")
(load "lib/fset-compat.lisp")

;; Load fset source files directly
(let ((base (concatenate 'string *ql-software* "fset-v2.2.0/Code/")))
  (dolist (f '("port" "macros" "order" "hash" "wb-trees" "champ"
               "reader" "fset" "replay" "tuples" "interval"
               "relations" "complement-sets" "bounded-sets" "post"))
    (format t "~%; Loading fset/~A.lisp~%" f)
    (handler-case
      (load (concatenate 'string base f ".lisp"))
      (error (c)
        (format t "~%ERROR loading ~A: ~A~%" f c)))))

;; Test basic fset operations
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
(format t "~%FSet test complete.~%")
