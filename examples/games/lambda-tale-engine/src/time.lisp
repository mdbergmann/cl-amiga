;;; Lambda's Tale — game time: the clock, day and night, darkness.
;;;
;;; Time is counted in game minutes since the campaign began.  Every
;;; step, turn and combat round costs *MINUTES-PER-ACTION*; bumping a
;;; wall costs nothing.  ADVANCE-TIME is the single clock entry point:
;;; it emits :SUNRISE/:SUNSET when the clock crosses the daylight
;;; boundaries, expires timed effects (see game.lisp) and regenerates
;;; caster spell points — Bard's Tale style, walking under the open
;;; daytime sky restores the party's magic (see %REGEN-SP).
;;;
;;; Darkness: the first-person view shrinks when the party cannot see.
;;; A zone declared (ZONE ... :DARK T) — a dungeon or cellar — is
;;; completely dark, one cell, until a light effect burns; (ZONE :DARK N)
;;; grants N cells there.  Outdoors at night there is no sun but there is
;;; a moon: sight falls to *MOONLIGHT-DEPTH* (a few cells), not the blind
;;; one of the underground, and a light still restores the full depth.
;;; The automap honors the same rule: what the party cannot see, it
;;; cannot map (OBSERVE draws through GAME-VIEW-DEPTH).

(in-package :tale)

(defconstant +minutes-per-day+ 1440)
(defconstant +sunrise-minute+ 360)      ; 06:00
(defconstant +sunset-minute+ 1200)      ; 20:00

(defparameter *minutes-per-action* 1
  "Clock cost in minutes of one step, turn or combat round.")

(defparameter *new-game-minutes* 480
  "The clock a fresh game starts at: day 1, 08:00.")

(defparameter *sp-regen-minutes* 4
  "Casters regain one spell point per this many daylight minutes spent
outdoors (in a zone without :DARK) and out of combat.")

(defun daylight-p (minutes)
  "True when the clock MINUTES falls in the 06:00-20:00 daylight window."
  (let ((m (mod minutes +minutes-per-day+)))
    (and (>= m +sunrise-minute+) (< m +sunset-minute+))))

;;; ---------------------------------------------------------------------
;;; The five bands of the day.  They tile the clock and align exactly to
;;; the daylight window: MORNING+NOON+AFTERNOON+EVENING is [06:00,20:00)
;;; = (DAYLIGHT-P), NIGHT is the rest.  The first-person sky and the
;;; ground take a different colour in each band (see PALETTE.LISP's
;;; SKY-COLOR-FOR / GROUND-COLOR-FOR); ADVANCE-TIME emits :TIME-BAND when
;;; the band turns so the front-end can re-tint pens 5 and 6.

(defparameter *time-band-starts*
  '((:morning   . 360)     ; 06:00
    (:noon      . 600)     ; 10:00
    (:afternoon . 840)     ; 14:00
    (:evening   . 1080)    ; 18:00
    (:night     . 1200))   ; 20:00, wrapping midnight back to :morning
  "Each day-band's start minute-of-day, in clock order — a band runs
until the next one starts and :NIGHT wraps midnight.  The order here is
the canonical band order (see *TIME-BAND-NAMES*).")

(defparameter *time-band-names*
  '((:morning . "Morning") (:noon . "Noon") (:afternoon . "Afternoon")
    (:evening . "Evening") (:night . "Night"))
  "Display name of each day-band.")

(defparameter *time-band-messages*
  '((:morning   . "The sun rises.")
    (:noon      . "The sun climbs high.")
    (:afternoon . "The afternoon wears on.")
    (:evening   . "Dusk gathers.")
    (:night     . "Night falls."))
  "The line ADVANCE-TIME announces in the message log when the clock
turns into each day-band, so a party watching the sky cycle reads the
day passing.  :MORNING and :NIGHT coincide with sunrise and sunset.")

(defun time-of-day (minutes)
  "The day-band keyword for clock MINUTES — :MORNING :NOON :AFTERNOON
:EVENING or :NIGHT (see *TIME-BAND-STARTS*)."
  (let ((m (mod minutes +minutes-per-day+)))
    (cond ((< m 360) :night)          ; 00:00-05:59, before sunrise
          ((< m 600) :morning)
          ((< m 840) :noon)
          ((< m 1080) :afternoon)
          ((< m 1200) :evening)
          (t :night))))               ; 20:00-23:59, after sunset

(defun game-time-of-day (game)
  "The current day-band of GAME (see TIME-OF-DAY)."
  (time-of-day (game-time game)))

(defun time-of-day-name (band)
  "The display name of day-band BAND, e.g. \"Morning\"."
  (or (cdr (assoc band *time-band-names*)) "Day"))

(defun time-of-day-line (game)
  "The day-band as a display string, e.g. \"It's Morning.\" — shown on
the automap beside the clock."
  (format nil "It's ~A." (time-of-day-name (game-time-of-day game))))

;;; ---------------------------------------------------------------------
;;; The living-world clock: time passes while the party stands idle.
;;;
;;; Classic Bard's Tale moves the clock only on an action — a step, a
;;; turn, a combat round.  Standing still freezes time, so the sky never
;;; changes unless you walk.  The real-time front-end (the Amiga event
;;; loop) instead drips the clock forward on its idle heartbeat, at
;;; *IDLE-CLOCK-RATE* game-minutes per real second: the world breathes,
;;; the sky cycles, casters slowly regain magic outdoors and timed
;;; effects burn down whether or not the party moves.  It is the same
;;; ADVANCE-TIME, so every consequence (SP regen, effect expiry,
;;; :SUNRISE/:SUNSET, :TIME-BAND) fires exactly as it does for a step.
;;;
;;; The pace is a plain special variable so a campaign, a profile or the
;;; REPL can rebind or disable it live — the front-end reads it every
;;; tick.  The two helpers below are pure integer arithmetic (host
;;; testable) that turn elapsed real time into whole game-minutes and
;;; back, so the front-end can carry the sub-minute remainder forward
;;; and never lose or double-count time.

(defparameter *idle-clock-rate* 4
  "Game-minutes the clock advances per real second while the party
stands idle in free exploration (the real-time front-end's living-world
clock).  NIL disables idle progression — time then moves only on an
action, the classic Bard's Tale rule.  Read on every idle tick, so a
rebinding takes effect at once; suggested settings: 1 ambient (a day in
~24 real min), 4 brisk (~6 min), 20 demo (~72 sec).")

(defun idle-minutes-elapsed (elapsed-units)
  "Whole game-minutes that ELAPSED-UNITS of real idle time (measured in
INTERNAL-TIME-UNITS-PER-SECOND) buys at *IDLE-CLOCK-RATE*.  0 when idle
progression is off (*IDLE-CLOCK-RATE* NIL/non-positive) or under a
minute has passed."
  (let ((rate *idle-clock-rate*))
    (if (and rate (> rate 0) (> elapsed-units 0))
        (floor (* elapsed-units rate) internal-time-units-per-second)
        0)))

(defun idle-minutes-cost (minutes)
  "The real-time units MINUTES whole game-minutes cost at
*IDLE-CLOCK-RATE* — the front-end advances its idle base by this so the
leftover sub-minute real time carries into the next tick.  Never exceeds
the elapsed time IDLE-MINUTES-ELAPSED measured, so the base cannot run
ahead of the clock."
  (let ((rate *idle-clock-rate*))
    (if (and rate (> rate 0) (> minutes 0))
        (floor (* minutes internal-time-units-per-second) rate)
        0)))

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

(defparameter *moonlight-depth* 3
  "Cells the party sees ahead outdoors at night with no light burning:
moonlight, dimmer than the daytime +VIEW-DEPTH+ but not the blind one
cell of a lightless dungeon.  A light effect still restores the full
depth.  Capped at +VIEW-DEPTH+; set to 1 for pitch-black nights.
Underground and other (ZONE ... :DARK ...) zones ignore this — there is
no moon down there.")

(defun game-view-depth (game)
  "How many cells ahead the party can see: +VIEW-DEPTH+ in light.  In
darkness (see GAME-DARK-P): the zone's (ZONE :DARK N) cell count when it
declares one, one cell for a plain :DARK T zone (a lightless dungeon or
cellar — completely dark), and *MOONLIGHT-DEPTH* at night in an outdoor
zone (moonlight).  All capped at +VIEW-DEPTH+."
  (if (game-dark-p game)
      (let ((dark (dungeon-map-dark (game-map game))))
        (cond ((integerp dark) (min dark +view-depth+))  ; :DARK N
              (dark 1)                                    ; :DARK T, pitch black
              (t (min *moonlight-depth* +view-depth+))))  ; night outdoors
      +view-depth+))

(defun render-view-depth (game)
  "How many cells ahead the view DRAWS: GAME-VIEW-DEPTH capped by the
machine's draw distance (*DRAW-DEPTH*, see view.lisp).

The renderers call this; OBSERVE deliberately calls GAME-VIEW-DEPTH
instead, so lowering the draw distance for frames never shrinks what
the automap learns.  Darkness still wins when it is the tighter of the
two — a torchless dungeon shows one cell however fast the machine is."
  (min (game-view-depth game) (%draw-depth)))

(defun %time-band-crossings (old new)
  "The day-band keyword entered at every *TIME-BAND-STARTS* boundary
strictly after OLD and at or before NEW, oldest first — one entry per
band turn the clock crosses advancing from OLD to NEW, however many
bands or days that spans.  Walks whole days, not minutes, so a large
jump (the idle clock after a long real-time stall) costs one pass per
day spanned rather than one per game-minute."
  (let ((day-base (* +minutes-per-day+ (floor old +minutes-per-day+)))
        (crossings '()))
    (loop while (<= day-base new)
          do (dolist (entry *time-band-starts*)
               (let ((at (+ day-base (cdr entry))))
                 (when (and (> at old) (<= at new))
                   (push (cons at (car entry)) crossings))))
             (incf day-base +minutes-per-day+))
    (mapcar #'cdr (sort crossings #'< :key #'car))))

(defun advance-time (game &optional (minutes *minutes-per-action*))
  "Advance the clock by MINUTES (default *MINUTES-PER-ACTION*): announce
each band turn (see *TIME-BAND-MESSAGES*), emit :TIME-BAND on every band
turn crossed and :SUNRISE/:SUNSET on every daylight boundary — a large
MINUTES (the idle clock after a long stall) walks each boundary in
between via %TIME-BAND-CROSSINGS rather than only comparing the two
endpoints, so no band turn, message or sunrise/sunset in the middle of a
long jump is silently skipped.  Also expires timed effects and
regenerates caster spell points."
  (let ((old (game-time game)))
    (incf (game-time game) minutes)
    (let ((new (game-time game)))
      ;; the sky re-colours on every band turn, not only across the
      ;; daylight boundary — the front-end reloads pens 5/6 on :TIME-BAND.
      ;; :SUNRISE/:SUNSET coincide exactly with the turn into :MORNING/
      ;; :NIGHT (the daylight window is [+SUNRISE-MINUTE+,+SUNSET-MINUTE+)
      ;; which *TIME-BAND-STARTS* also uses as the morning/night starts).
      (dolist (band (%time-band-crossings old new))
        (emit game :time-band band)
        (let ((line (cdr (assoc band *time-band-messages*))))
          (when line (say game line)))
        (case band
          (:morning (emit game :sunrise))
          (:night (emit game :sunset))))
      (%expire-effects game new)
      (%regen-sp game old new)))
  (game-time game))

(defun %regen-sp (game old new)
  "Walking under the open daytime sky restores magic: on every clock
minute in (OLD, NEW] that is daylight, falls in a zone without :DARK,
is free of combat and hits a *SP-REGEN-MINUTES* boundary, every living
caster below full regains one spell point."
  (when (and (not (dungeon-map-dark (game-map game)))
             (not (game-combat game)))
    (let ((ticks 0))
      (loop for m from (1+ old) to new
            when (and (daylight-p m) (zerop (mod m *sp-regen-minutes*)))
              do (incf ticks))
      (when (plusp ticks)
        (dolist (h (alive-heroes game))
          (when (hero-caster-p h)
            (setf (hero-sp h)
                  (min (hero-max-sp h) (+ (hero-sp h) ticks)))))))))

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
