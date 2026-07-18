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
  defenders)          ; heroes defending during the current round

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

(defun %hero-attack (game hero monster)
  (let ((type (monster-kind monster)))
    (if (%attack-hits-p (+ (hero-level hero) (stat-bonus (hero-str hero)))
                        (monster-type-ac type))
        (let ((dmg (max 1 (+ (roll-dice (hero-attack-dice hero))
                             (stat-bonus (hero-str hero))))))
          (decf (monster-hp monster) dmg)
          (if (monster-alive-p monster)
              (say game "~A hits the ~A for ~D damage."
                   (hero-name hero) (monster-type-name type) dmg)
              (say game "~A slays the ~A!"
                   (hero-name hero) (monster-type-name type))))
        (say game "~A misses the ~A."
             (hero-name hero) (monster-type-name type)))))

(defun %monster-attack (game combat monster)
  (let* ((targets (front-ranks game))
         (hero (nth (roll (length targets)) targets))
         (type (monster-kind monster))
         (ac (- (hero-effective-ac hero)
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
;;; Rounds

(defun combat-round (game &optional actions)
  "Fight one round.  ACTIONS lists an action per living hero in party
order — :attack (the default) or :defend.  Heroes strike the first
living monster; then the surviving monsters strike back.  Returns
:victory, :defeat or :ongoing."
  (let ((combat (game-combat game)))
    (unless combat
      (error "combat-round: no combat is in progress"))
    (let ((pairs (mapcar (lambda (h) (cons h (or (pop actions) :attack)))
                         (alive-heroes game))))
      (setf (combat-defenders combat)
            (let ((d '()))
              (dolist (p pairs (nreverse d))
                (when (eq (cdr p) :defend)
                  (push (car p) d)))))
      (dolist (p pairs)
        (when (eq (cdr p) :attack)
          (let ((target (first (alive-monsters combat))))
            (when target
              (%hero-attack game (car p) target))))))
    (%monsters-act game combat)
    (%combat-outcome game combat)))

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
