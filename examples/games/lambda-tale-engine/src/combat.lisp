;;; Lambda's Tale — combat.
;;;
;;; Monster types are campaign data (DEFINE-MONSTER); the engine only
;;; knows the mechanics.  Combat is Bard's Tale round-based: the party
;;; declares actions, heroes strike first, then every surviving monster
;;; swings at a random front-rank hero.  The whole transcript travels as
;;; :MESSAGE events; :COMBAT-START and :COMBAT-END frame the fight.
;;;
;;; To-hit: d20 + bonus hits when it reaches 20 - AC (descending AC,
;;; unarmored 10 = hit on 10+).  Defending is +4 AC for the round.

(in-package :tale)

(defstruct (monster-type (:constructor %make-monster-type))
  name                ; string, e.g. "giant rat"
  (level 1)           ; to-hit bonus
  (hp-dice "1d8")
  (ac 10)
  (damage "1d4")
  (xp 10)
  (gold-dice 0))

(defvar *monster-types* (make-hash-table :test 'equalp))

(defun define-monster (name &key (level 1) (hp-dice "1d8") (ac 10)
                                 (damage "1d4") (xp 10) (gold 0))
  "Register monster type NAME (a string).  Campaign data calls this."
  (setf (gethash name *monster-types*)
        (%make-monster-type :name name :level level :hp-dice hp-dice
                            :ac ac :damage damage :xp xp :gold-dice gold))
  name)

(defun find-monster-type (name)
  (or (gethash name *monster-types*)
      (error "Unknown monster ~S (register it with DEFINE-MONSTER)" name)))

(defstruct (monster (:constructor %make-monster))
  kind                ; MONSTER-TYPE
  hp)

(defun monster-alive-p (monster)
  (> (monster-hp monster) 0))

(defstruct (combat (:constructor %make-combat))
  monsters            ; list of MONSTER (the fallen stay, filtered below)
  defenders           ; heroes defending during the current round
  (round-no 0))       ; completed rounds; COMBAT-ROUND counts them up

(defun alive-monsters (combat)
  (remove-if-not #'monster-alive-p (combat-monsters combat)))

(defun combat-groups (combat)
  "Alist of (MONSTER-TYPE . live count) over the living monsters,
in encounter order."
  (let ((groups '()))
    (dolist (m (alive-monsters combat) (nreverse groups))
      (let ((entry (assoc (monster-kind m) groups)))
        (if entry
            (incf (cdr entry))
            (push (cons (monster-kind m) 1) groups))))))

(defun combat-banner (combat)
  (with-output-to-string (s)
    (write-string "You face " s)
    (let ((first t))
      (dolist (group (combat-groups combat))
        (unless first (write-string " and " s))
        (setf first nil)
        (format s "~D ~A~A" (cdr group) (monster-type-name (car group))
                (if (> (cdr group) 1) "s" ""))))
    (write-char #\! s)))

(defun %spawn-monsters (name count)
  (let ((type (find-monster-type name))
        (monsters '()))
    (dotimes (i count (nreverse monsters))
      (push (%make-monster
             :kind type
             :hp (max 1 (roll-dice (monster-type-hp-dice type))))
            monsters))))

(defun start-combat (game spec)
  "Begin combat.  SPEC is a list of (MONSTER-NAME COUNT) groups; COUNT
may be dice (see PARSE-DICE).  Returns the new COMBAT."
  (when (game-combat game)
    (error "start-combat: combat is already in progress"))
  (let ((monsters '()))
    (dolist (group spec)
      (setf monsters
            (append monsters
                    (%spawn-monsters (first group)
                                     (max 1 (roll-dice (second group)))))))
    (let ((combat (%make-combat :monsters monsters)))
      (setf (game-combat game) combat)
      (emit game :combat-start monsters)
      (say game "~A" (combat-banner combat))
      combat)))

;;; ---------------------------------------------------------------------
;;; Attack resolution

(defun %attack-hits-p (bonus ac)
  "Roll d20 + BONUS against descending AC: hit on 20 - AC or better."
  (>= (+ 1 (roll 20) bonus) (- 20 ac)))

(defun %strike-monster (game attacker-name monster dmg)
  "Apply DMG to MONSTER with the hit/slay transcript — shared by melee
and damage spells so the log reads the same either way."
  (let ((type (monster-kind monster)))
    (decf (monster-hp monster) dmg)
    (if (monster-alive-p monster)
        (say game "~A hits the ~A for ~D damage."
             attacker-name (monster-type-name type) dmg)
        (say game "~A slays the ~A!"
             attacker-name (monster-type-name type)))))

(defun %hero-attack (game hero monster)
  (let ((type (monster-kind monster)))
    (if (%attack-hits-p (+ (hero-level hero) (stat-bonus (hero-str hero)))
                        (monster-type-ac type))
        (%strike-monster game (hero-name hero) monster
                         (max 1 (+ (roll-dice (hero-attack-dice hero))
                                   (stat-bonus (hero-str hero)))))
        (say game "~A misses the ~A."
             (hero-name hero) (monster-type-name type)))))

(defun %monster-attack (game combat monster)
  (let* ((targets (front-ranks game))
         (hero (nth (roll (length targets)) targets))
         (type (monster-kind monster))
         (ac (- (hero-effective-ac hero game)
                (if (member hero (combat-defenders combat)) 4 0))))
    (if (%attack-hits-p (monster-type-level type) ac)
        (let ((dmg (max 1 (roll-dice (monster-type-damage type)))))
          (say game "The ~A hits ~A for ~D damage."
               (monster-type-name type) (hero-name hero) dmg)
          (damage-hero game hero dmg))
        (say game "The ~A misses ~A."
             (monster-type-name type) (hero-name hero)))))

(defun %monsters-act (game combat)
  (dolist (m (alive-monsters combat))
    (when (party-alive-p game)
      (%monster-attack game combat m))))

(defun %award-victory (game combat)
  (let ((xp 0)
        (gold 0))
    (dolist (m (combat-monsters combat))
      (incf xp (monster-type-xp (monster-kind m)))
      (incf gold (roll-dice (monster-type-gold-dice (monster-kind m)))))
    (say game "Victory!  The party wins ~D experience and ~D gold." xp gold)
    (let ((survivors (alive-heroes game)))
      (when survivors
        (incf (hero-gold (first survivors)) gold)
        (let ((share (floor xp (length survivors))))
          (dolist (h survivors)
            (award-xp game h share)))))))

(defun %combat-outcome (game combat)
  (cond ((not (party-alive-p game))
         (setf (game-combat game) nil)
         (say game "The party has been defeated...")
         (emit game :combat-end :defeat)
         :defeat)
        ((null (alive-monsters combat))
         (%award-victory game combat)
         (setf (game-combat game) nil)
         (emit game :combat-end :victory)
         :victory)
        (t :ongoing)))

;;; ---------------------------------------------------------------------
;;; Combat transcript speed.  The engine only keeps the setting; the
;;; front-ends linger COMBAT-MESSAGE-DELAY seconds on each :MESSAGE
;;; while a round plays out, so the transcript reads like a fight
;;; instead of arriving as one block.

(defconstant +combat-speed-max+ 5
  "The fastest combat transcript speed: no lingering at all.")

(defparameter *combat-speed* 3
  "Combat transcript speed, 1 (slow) .. +COMBAT-SPEED-MAX+ (instant).
The +/- keys during the round orders adjust it.")

(defun combat-message-delay ()
  "Seconds the front-ends linger on each combat message: 0.25s per
speed step below the maximum (1.0s at speed 1, 0.0 at speed 5)."
  (/ (- +combat-speed-max+
        (max 1 (min +combat-speed-max+ *combat-speed*)))
     4.0))

(defun adjust-combat-speed (game delta)
  "Bump *COMBAT-SPEED* by DELTA, clamped to 1..+COMBAT-SPEED-MAX+, and
say where it landed.  Returns the new speed."
  (setf *combat-speed*
        (max 1 (min +combat-speed-max+ (+ *combat-speed* delta))))
  (say game "Combat speed ~D of ~D~:[~; (instant)~]."
       *combat-speed* +combat-speed-max+
       (= *combat-speed* +combat-speed-max+))
  *combat-speed*)

;;; ---------------------------------------------------------------------
;;; Rounds

(defun combat-round (game &optional actions)
  "Fight one round.  ACTIONS lists an action per living hero in party
order — :attack (the default), :defend, (:cast SPELL [TARGET]) to
cast a spell (see CAST-SPELL; a failed cast wastes the round), or
\(:sing SONG) to strike up a song (see SING-SONG; likewise).  Heroes
strike the first living monster; then the surviving monsters strike
back.  The round costs one clock tick.  Returns :victory, :defeat or
:ongoing."
  (let ((combat (game-combat game)))
    (unless combat
      (error "combat-round: no combat is in progress"))
    (advance-time game)
    (say game "-- Round ~D --" (incf (combat-round-no combat)))
    (let ((pairs (mapcar (lambda (h) (cons h (or (pop actions) :attack)))
                         (alive-heroes game))))
      (setf (combat-defenders combat)
            (let ((d '()))
              (dolist (p pairs (nreverse d))
                (when (eq (cdr p) :defend)
                  (push (car p) d)))))
      (dolist (p pairs)
        (let ((a (cdr p)))
          (cond ((eq a :attack)
                 (let ((target (first (alive-monsters combat))))
                   (when target
                     (%hero-attack game (car p) target))))
                ((and (consp a) (eq (first a) :cast))
                 (cast-spell game (car p) (second a) (third a)))
                ((and (consp a) (eq (first a) :sing))
                 (sing-song game (car p) (second a)))))))
    (%monsters-act game combat)
    (%combat-outcome game combat)))

;;; ---------------------------------------------------------------------
;;; The round-orders interaction model (shared by both front-ends).
;;;
;;; Bard's Tale style: every living hero picks an action for the round
;;; — attack, defend, cast, play — and only then does the round run.
;;; The model collects (HERO . ACTION) pairs in party order; C and P
;;; open the cast/sing pickers for the hero at hand in their :ORDERS
;;; mode, which hands the pick back as a round action instead of
;;; fighting a round itself (see %CAST-COMMIT / %SING-COMMIT).  F is
;;; party-level flight, Esc undoes the previous hero's pick, +/- set
;;; the transcript speed.  When the last hero has picked,
;;; COMBAT-ORDERS-ACT returns (:FIGHT ACTIONS) and the front-end
;;; fights the round with them.

(defstruct (combat-orders (:constructor %make-combat-orders))
  chosen              ; (HERO . ACTION) pairs picked so far, party order
  sub)                ; CAST-VIEW/SING-VIEW picking for the hero at hand

(defun make-combat-orders ()
  (%make-combat-orders))

(defun combat-orders-hero (game view)
  "The hero the orders view is asking about, or NIL once every living
hero has an action."
  (nth (length (combat-orders-chosen view)) (alive-heroes game)))

(defun %orders-action-label (action)
  "A short display label for a round action."
  (cond ((eq action :attack) "attack")
        ((eq action :defend) "defend")
        ((and (consp action) (eq (first action) :cast))
         (format nil "cast ~A~@[ on ~A~]"
                 (spell-title (second action))
                 (let ((target (third action)))
                   (and target (hero-name target)))))
        ((and (consp action) (eq (first action) :sing))
         (format nil "play ~A" (song-title (second action))))
        (t (string-downcase (princ-to-string action)))))

(defun combat-orders-lines (game view)
  "The round-orders page as menu lines (the SHOP-LINES pattern) —
the upcoming round, the enemy groups, one row per living hero with
the picked action ('?' marks the hero at hand) and the key footer.
While a cast/sing pick is open, its page shows instead."
  (let ((sub (combat-orders-sub view)))
    (cond
      ((cast-view-p sub) (cast-lines game sub))
      ((sing-view-p sub) (sing-lines game sub))
      (t
       (let ((combat (game-combat game))
             (current (combat-orders-hero game view)))
         (append
          (list (format nil "*** Combat -- Round ~D ***"
                        (1+ (combat-round-no combat)))
                "")
          (mapcar (lambda (group)
                    (format nil "  ~D ~A~A" (cdr group)
                            (monster-type-name (car group))
                            (if (> (cdr group) 1) "s" "")))
                  (combat-groups combat))
          (list "")
          (mapcar (lambda (h)
                    (let ((pair (assoc h (combat-orders-chosen view))))
                      (format nil "~:[ ~;>~] ~12A ~A"
                              (eq h current) (hero-name h)
                              (cond (pair (%orders-action-label (cdr pair)))
                                    ((eq h current) "?")
                                    (t "")))))
                  (alive-heroes game))
          (list ""
                "[a]ttack [d]efend [c]ast [p]lay"
                (format nil "[f]lee  [Esc] undo  +/- speed ~D"
                        *combat-speed*))))))))

(defun %orders-record (game view action)
  "Record ACTION for the hero at hand.  When that completes the list,
return (:FIGHT ACTIONS), the actions in party order; else NIL."
  (setf (combat-orders-chosen view)
        (append (combat-orders-chosen view)
                (list (cons (combat-orders-hero game view) action))))
  (if (combat-orders-hero game view)
      nil
      (list :fight (mapcar #'cdr (combat-orders-chosen view)))))

(defun %orders-sub-act (game view sub char)
  "Forward CHAR to the open cast/sing picker.  A completed pick lands
as the hero-at-hand's action; Esc backs out to the action keys."
  (let ((result (if (cast-view-p sub)
                    (cast-act game sub char)
                    (sing-act game sub char))))
    (cond ((and (consp result) (eq (first result) :action))
           (setf (combat-orders-sub view) nil)
           (%orders-record game view (second result)))
          ((or (eq result :cancelled)
               ;; Esc on the picker's first page clears its preset
               ;; hero — that is the whole picker backing out
               (null (if (cast-view-p sub)
                         (cast-view-hero sub)
                         (sing-view-hero sub))))
           (setf (combat-orders-sub view) nil)
           nil)
          (t nil))))

(defun combat-orders-act (game view char)
  "Apply key CHAR to the round-orders page.  Returns (:FIGHT ACTIONS)
when the last living hero picks — the front-end fights the round with
them (COMBAT-ROUND) — :FLEE when the party runs (ATTEMPT-FLEE), else
NIL."
  (let ((sub (combat-orders-sub view))
        (hero (combat-orders-hero game view)))
    (cond
      (sub (%orders-sub-act game view sub char))
      ((null hero) nil)                 ; complete — awaiting the round
      (t
       (case (char-downcase char)
         (#\a (%orders-record game view :attack))
         (#\d (%orders-record game view :defend))
         (#\c (if (and (hero-caster-p hero) (spells-for-hero hero))
                  (setf (combat-orders-sub view)
                        (make-cast-view :in-combat :orders :hero hero))
                  (say game "~A cannot cast." (hero-name hero)))
              nil)
         (#\p (if (and (hero-singer-p hero) (songs-for-hero hero))
                  (setf (combat-orders-sub view)
                        (make-sing-view :in-combat :orders :hero hero))
                  (say game "~A cannot play." (hero-name hero)))
              nil)
         (#\f :flee)
         (#\+ (adjust-combat-speed game 1) nil)
         (#\- (adjust-combat-speed game -1) nil)
         (#\Escape
          (let ((chosen (combat-orders-chosen view)))
            (when chosen
              (setf (combat-orders-chosen view) (butlast chosen))))
          nil)
         (t nil))))))

(defun attempt-flee (game)
  "Try to run from combat.  Success (even odds) ends the fight; failure
gives the monsters a free round.  Returns :fled, :defeat or :ongoing."
  (let ((combat (game-combat game)))
    (unless combat
      (error "attempt-flee: no combat is in progress"))
    (setf (combat-defenders combat) '())
    (if (< (roll 100) 50)
        (progn
          (setf (game-combat game) nil)
          (say game "The party flees!")
          (emit game :combat-end :fled)
          :fled)
        (progn
          (say game "No escape!")
          (%monsters-act game combat)
          (%combat-outcome game combat)))))
