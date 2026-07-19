;;; Closure — loader: brings in the Lambda's Tale engine from the
;;; sibling directory (examples/games/lambda-tale-engine).  Located
;;; through *LOAD-TRUENAME* — two parents up from this file, then into
;;; the engine — because a portable relative parent path does not
;;; exist: POSIX ".." is not AmigaDOS, and clamiga parses a leading
;;; "/" as volume-absolute, not as the AmigaDOS parent dir.  The
;;; engine is itself self-locating, so the working directory stays the
;;; game's: worlds/ and saves/ resolve here.

(let ((here (directory-namestring *load-truename*)))   ; .../closure/src/
  (flet ((parent (dir)
           (let ((cut (position #\/ dir :from-end t
                                :end (max 0 (1- (length dir))))))
             (if cut (subseq dir 0 (1+ cut)) dir))))
    (load (concatenate 'string (parent (parent here))  ; .../games/
                       "lambda-tale-engine/src/load.lisp"))))
