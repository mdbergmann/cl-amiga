;;; Lambda's Tale — dice.
;;;
;;; All randomness in the engine goes through *RNG* so tests can script
;;; it deterministically.  Dice specs are either an integer (a constant)
;;; or a string in the classic "NdM", "NdM+K", "NdM-K" notation.

(in-package :tale)

(defvar *rng* (lambda (n) (random n))
  "Function of one positive integer N returning a uniform integer in
[0,N).  Rebind to a scripted function for deterministic tests.")

(defun roll (n)
  "A uniform random integer in [0,N)."
  (funcall *rng* n))

(defun parse-dice (spec)
  "Parse dice SPEC into (values COUNT SIDES BONUS).
SPEC is an integer (a constant: zero dice, the value as bonus) or a
string like \"2d6\", \"1d8+2\" or \"3d4-1\"."
  (etypecase spec
    (integer (values 0 0 spec))
    (string
     (let ((d (position #\d spec :test #'char-equal)))
       (unless d
         (error "Invalid dice spec ~S (want an integer or e.g. \"2d6+1\")"
                spec))
       (let* ((count (parse-integer spec :end d))
              (sign (position-if (lambda (c)
                                   (or (char= c #\+) (char= c #\-)))
                                 spec :start (1+ d)))
              (sides (parse-integer spec :start (1+ d) :end sign))
              (bonus (if sign (parse-integer spec :start sign) 0)))
         (unless (and (> count 0) (> sides 0))
           (error "Invalid dice spec ~S (count and sides must be positive)"
                  spec))
         (values count sides bonus))))))

(defun roll-dice (spec)
  "Roll dice SPEC (see PARSE-DICE) and return the total."
  (multiple-value-bind (count sides bonus) (parse-dice spec)
    (let ((total bonus))
      (dotimes (i count total)
        (incf total (1+ (roll sides)))))))
