;;; Lambda's Tale — heroes and the party.
;;;
;;; Hero classes are campaign data, not engine facts: the campaign
;;; registers them with DEFINE-HERO-CLASS (see data/campaign.lisp) and
;;; MAKE-HERO rolls a level-1 character of that class.  Armor class is
;;; Bard's Tale style descending: lower is better, unarmored is 10.

(in-package :tale)

(defstruct (hero (:constructor %make-hero))
  name
  class               ; keyword registered via DEFINE-HERO-CLASS
  (level 1)
  (xp 0)
  (max-hp 1)
  (hp 1)
  (str 10) (dex 10) (iq 10) (con 10) (lck 10)
  (ac 10)             ; descending: lower is better
  (damage "1d4")      ; the hero's attack dice
  (gold 0))

(defvar *hero-classes* (make-hash-table :test 'eq))

(defun define-hero-class (name &key (hp-dice "1d8") (damage "1d4") (ac 10))
  "Register hero class NAME (a keyword) with its hit dice, attack dice
and starting armor class.  Campaign data calls this."
  (setf (gethash name *hero-classes*)
        (list :hp-dice hp-dice :damage damage :ac ac))
  name)

(defun hero-class-property (class key)
  (let ((plist (gethash class *hero-classes*)))
    (unless plist
      (error "Unknown hero class ~S (register it with DEFINE-HERO-CLASS)"
             class))
    (getf plist key)))

(defun make-hero (name class)
  "Create a level-1 hero of CLASS: hp from the class hit dice, abilities
rolled 3d6 in the order str, dex, iq, con, lck."
  (let ((hp (max 1 (roll-dice (hero-class-property class :hp-dice)))))
    (%make-hero :name name :class class
                :max-hp hp :hp hp
                :str (roll-dice "3d6") :dex (roll-dice "3d6")
                :iq (roll-dice "3d6") :con (roll-dice "3d6")
                :lck (roll-dice "3d6")
                :ac (hero-class-property class :ac)
                :damage (hero-class-property class :damage))))

(defun stat-bonus (stat)
  "Bonus for an ability score: +1 per 2 points above 10, negative below."
  (floor (- stat 10) 2))

(defun hero-alive-p (hero)
  (> (hero-hp hero) 0))

(defun alive-heroes (game)
  "The living party members, in party order."
  (remove-if-not #'hero-alive-p (game-party game)))

(defun party-alive-p (game)
  (not (null (alive-heroes game))))

(defun front-ranks (game &optional (n 3))
  "The first N living heroes — the ones monsters can reach."
  (let ((alive (alive-heroes game)))
    (if (> (length alive) n) (subseq alive 0 n) alive)))

(defun damage-hero (game hero amount)
  "Deal AMOUNT damage to HERO.  Emits :HERO-DIED when this kills them
and :PARTY-DEFEATED when nobody is left standing.  Returns remaining hp."
  (setf (hero-hp hero) (max 0 (- (hero-hp hero) amount)))
  (when (zerop (hero-hp hero))
    (say game "~A falls!" (hero-name hero))
    (emit game :hero-died hero)
    (unless (party-alive-p game)
      (emit game :party-defeated)))
  (hero-hp hero))

(defun heal-hero (game hero amount)
  "Heal HERO by AMOUNT, capped at max hp.  Returns the hp gained."
  (let* ((new (min (hero-max-hp hero) (+ (hero-hp hero) amount)))
         (gained (- new (hero-hp hero))))
    (setf (hero-hp hero) new)
    (when (> gained 0)
      (say game "~A recovers ~D hit point~A." (hero-name hero) gained
           (if (> gained 1) "s" "")))
    gained))

;;; ---------------------------------------------------------------------
;;; Experience and levels

(defun xp-for-level (level)
  "Total experience required to reach LEVEL."
  (* 50 level (1- level)))

(defun level-up (game hero)
  (incf (hero-level hero))
  (let ((gain (max 1 (roll-dice (hero-class-property (hero-class hero)
                                                     :hp-dice)))))
    (incf (hero-max-hp hero) gain)
    (incf (hero-hp hero) gain))
  (say game "~A rises to level ~D!" (hero-name hero) (hero-level hero)))

(defun award-xp (game hero amount)
  "Grant HERO experience, leveling up as thresholds are crossed."
  (incf (hero-xp hero) amount)
  (loop while (>= (hero-xp hero) (xp-for-level (1+ (hero-level hero))))
        do (level-up game hero))
  (hero-xp hero))
