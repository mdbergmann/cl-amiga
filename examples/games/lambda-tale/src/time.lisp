;;; Lambda's Tale — game time: the clock, day and night, darkness.
;;;
;;; Time is counted in game minutes since the campaign began.  Every
;;; step, turn and combat round costs *MINUTES-PER-ACTION*; bumping a
;;; wall costs nothing.  ADVANCE-TIME is the single clock entry point:
;;; it emits :SUNRISE/:SUNSET when the clock crosses the daylight
;;; boundaries and expires timed effects (see game.lisp).
;;;
;;; Darkness: the first-person view shrinks to one cell when the party
;;; cannot see — at night in any outdoor zone, or always in a zone
;;; declared (ZONE ... :DARK T) — unless a light effect burns.  The
;;; automap honors the same rule: what the party cannot see, it cannot
;;; map (OBSERVE draws through GAME-VIEW-DEPTH).

(in-package :tale)

(defconstant +minutes-per-day+ 1440)
(defconstant +sunrise-minute+ 360)      ; 06:00
(defconstant +sunset-minute+ 1200)      ; 20:00

(defparameter *minutes-per-action* 1
  "Clock cost in minutes of one step, turn or combat round.")

(defparameter *new-game-minutes* 480
  "The clock a fresh game starts at: day 1, 08:00.")

(defun daylight-p (minutes)
  "True when the clock MINUTES falls in the 06:00-20:00 daylight window."
  (let ((m (mod minutes +minutes-per-day+)))
    (and (>= m +sunrise-minute+) (< m +sunset-minute+))))

(defun clock-line (game)
  "The clock as a display string, e.g. \"Day 2, 13:05\"."
  (multiple-value-bind (day m) (floor (game-time game) +minutes-per-day+)
    (multiple-value-bind (h mi) (floor m 60)
      (format nil "Day ~D, ~2,'0D:~2,'0D" (1+ day) h mi))))

(defun game-dark-p (game)
  "True when the party stands in darkness: the zone declares :DARK, or
it is night in any other (outdoor) zone — unless a light effect burns."
  (and (not (light-active-p game))
       (or (dungeon-map-dark (game-map game))
           (not (daylight-p (game-time game))))))

(defun game-view-depth (game)
  "How many cells ahead the party can see: +VIEW-DEPTH+, or one cell
in darkness (see GAME-DARK-P)."
  (if (game-dark-p game) 1 +view-depth+))

(defun advance-time (game &optional (minutes *minutes-per-action*))
  "Advance the clock by MINUTES (default *MINUTES-PER-ACTION*): emit
:SUNRISE/:SUNSET on a daylight boundary crossing and expire timed
effects.  The boundary check compares only the endpoints — exact for
small steps; a future long jump (a rest op) crossing more than one
boundary would need a per-segment walk."
  (let ((old (game-time game)))
    (incf (game-time game) minutes)
    (let ((new (game-time game)))
      (cond ((and (daylight-p new) (not (daylight-p old)))
             (say game "The sun rises.")
             (emit game :sunrise))
            ((and (daylight-p old) (not (daylight-p new)))
             (say game "Night falls.")
             (emit game :sunset)))
      (%expire-effects game new)))
  (game-time game))

(defun %expire-effects (game now)
  "Drop every timed effect whose expiry has passed, announcing each."
  (dolist (e (game-effects game))
    (let ((at (effect-expires-at e)))
      (when (and at (<= at now))
        (setf (game-effects game) (remove e (game-effects game)))
        (let ((label (copy-seq (effect-label e))))
          (when (plusp (length label))
            (setf (char label 0) (char-upcase (char label 0))))
          (say game "~A wears off." label))
        (emit game :effect-expired (effect-name e))))))
