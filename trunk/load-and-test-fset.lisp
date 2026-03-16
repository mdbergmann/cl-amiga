;; Load and test FSet via quicklisp
;; Works on both host and Amiga
;;
;; Usage:
;;   ./build/host/clamiga --heap 24M --load trunk/load-and-test-fset.lisp
;;   (on Amiga: clamiga --heap 48M --load trunk/load-and-test-fset.lisp)

(require "asdf")

;; Host: ~/quicklisp/setup.lisp, Amiga: local quicklisp/setup.lisp
#+amigaos (load "quicklisp/setup.lisp")
#-amigaos (load (merge-pathnames "quicklisp/setup.lisp" (user-homedir-pathname)))

#+amigaos (load "lib/quicklisp-compat.lisp")

(format t "~%--- Loading FSet via quicklisp ---~%")
(ql:quickload :fset)

(in-package :fset)

(format t "~%--- Running FSet tests ---~%")

(defvar *fset-pass* 0)
(defvar *fset-fail* 0)

(defmacro run-fset-test (name &body body)
  `(handler-case
     (progn ,@body
            (incf *fset-pass*)
            (format t "~A: PASS~%" ,name))
     (error (e)
       (incf *fset-fail*)
       (format t "~A: FAIL ~A~%" ,name e))))

(run-fset-test "Test-GMap" (Test-GMap))
(run-fset-test "Test-Equivalent-Sets(10,5)" (Test-Equivalent-Sets 10 5))
(run-fset-test "Test-Misc-1" (Test-Misc-1))
(run-fset-test "Test-Misc-2" (Test-Misc-2))
(run-fset-test "Test-Misc-3" (Test-Misc-3))
(run-fset-test "Test-Misc-4" (Test-Misc-4))
(run-fset-test "Test-Reader" (Test-Reader))
(run-fset-test "Test-Compare-Lex" (Test-Compare-Lexicographically))
(run-fset-test "Test-Deep-Update" (Test-Functional-Deep-Update))
(run-fset-test "Test-Bounded-Sets" (Test-Bounded-Sets))
(run-fset-test "Test-Complement-Sets" (Test-Complement-Sets))
(run-fset-test "Test-2-Relations" (Test-2-Relations))
(run-fset-test "Test-List-Relations" (Test-List-Relations))
(run-fset-test "Test-FSet2" (Test-FSet2))

(run-fset-test "Random-Ops (2 iters)"
  (let ((*random-state* (make-random-state nil)))
    (dotimes (i 2)
      (Test-Set-Operations i)
      (Test-Map-Operations i (Test-Set-Operations i))
      (Test-Bag-Operations i)
      (Test-Seq-Operations i)
      (Test-Tuple-Operations i))))

(run-fset-test "Champ-Sets" (test-champ-sets 2))
(run-fset-test "Champ-Maps" (test-champ-maps 2))

(in-package :cl-user)
(format t "~%=== FSet Results: ~D passed, ~D failed, ~D total ===~%"
        fset::*fset-pass* fset::*fset-fail*
        (+ fset::*fset-pass* fset::*fset-fail*))
