;;; Lambda's Tale — heroes and the party.
;;;
;;; Hero classes are campaign data, not engine facts: the campaign
;;; registers them with DEFINE-HERO-CLASS in its campaign.lisp and
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
  (max-sp 0)          ; spell points; 0 = not a caster
  (sp 0)
  (str 10) (dex 10) (iq 10) (con 10) (lck 10)
  (ac 10)             ; descending: lower is better
  (damage "1d4")      ; the hero's bare attack dice (no weapon)
  (gold 0)
  (items '())         ; pack contents: item names, at most +inventory-limit+
  (equipped '()))     ; equipped subset: one :weapon, :armor, :shield each

(defvar *hero-classes* (make-hash-table :test 'eq))

(defun define-hero-class (name &key (hp-dice "1d8") (damage "1d4") (ac 10)
                                    caster)
  "Register hero class NAME (a keyword) with its hit dice, attack dice
and starting armor class; CASTER T marks a spell-casting class (spell
points from level and IQ, see %HERO-MAX-SP).  Campaign data calls this."
  (setf (gethash name *hero-classes*)
        (list :hp-dice hp-dice :damage damage :ac ac :caster caster))
  name)

(defun hero-class-property (class key)
  (let ((plist (gethash class *hero-classes*)))
    (unless plist
      (error "Unknown hero class ~S (register it with DEFINE-HERO-CLASS)"
             class))
    (getf plist key)))

(defun make-hero (name class &key (gold 0))
  "Create a level-1 hero of CLASS: hp from the class hit dice, abilities
rolled 3d6 in the order str, dex, iq, con, lck.  GOLD is the starting
purse (campaign data decides; dice strings welcome)."
  ;; Keep the roll order (hp, str, dex, iq, con, lck, gold) — the test
  ;; suite scripts heroes through *RNG* and depends on it.
  (let* ((hp (max 1 (roll-dice (hero-class-property class :hp-dice))))
         (str (roll-dice "3d6")) (dex (roll-dice "3d6"))
         (iq (roll-dice "3d6")) (con (roll-dice "3d6"))
         (lck (roll-dice "3d6"))
         (sp (%hero-max-sp class 1 iq)))
    (%make-hero :name name :class class
                :max-hp hp :hp hp
                :max-sp sp :sp sp
                :str str :dex dex :iq iq :con con :lck lck
                :ac (hero-class-property class :ac)
                :damage (hero-class-property class :damage)
                :gold (roll-dice gold))))

(defun stat-bonus (stat)
  "Bonus for an ability score: +1 per 2 points above 10, negative below."
  (floor (- stat 10) 2))

(defun %hero-max-sp (class level iq)
  "Spell points for a CLASS/LEVEL/IQ hero: 2 per level plus the IQ
bonus for casters (minimum 1); everyone else has none."
  (if (hero-class-property class :caster)
      (max 1 (+ (* 2 level) (stat-bonus iq)))
      0))

(defun hero-caster-p (hero)
  "True when HERO can cast spells (a caster class with spell points)."
  (> (hero-max-sp hero) 0))

(defun hero-alive-p (hero)
  (> (hero-hp hero) 0))

(defconstant +party-limit+ 7
  "Maximum roster size: six regular heroes plus one guest slot (a
summoned/charmed monster or story NPC, Bard's Tale tradition).")

(defun hero-class-title (hero)
  "The hero's class as a display string: :war-mage -> \"War Mage\"."
  (string-capitalize (substitute #\Space #\- (string (hero-class hero)))))

(defun hero-summary-lines (hero)
  "The character sheet as a list of text lines — the full stat block a
player sees when they open a roster slot.  Pure (no I/O), so both the
Amiga sheet view and the tests render from the same source."
  (list
   (format nil "~A the ~A" (hero-name hero) (hero-class-title hero))
   (format nil "Level ~D    XP ~D" (hero-level hero) (hero-xp hero))
   (if (hero-caster-p hero)
       (format nil "HP ~D/~D  SP ~D/~D  AC ~D"
               (hero-hp hero) (hero-max-hp hero)
               (hero-sp hero) (hero-max-sp hero) (hero-ac hero))
       (format nil "HP ~D/~D    AC ~D" (hero-hp hero) (hero-max-hp hero)
               (hero-ac hero)))
   (format nil "STR ~D  DEX ~D  IQ ~D"
           (hero-str hero) (hero-dex hero) (hero-iq hero))
   (format nil "CON ~D  LCK ~D" (hero-con hero) (hero-lck hero))
   (format nil "Gold ~D gp~@[   ~A~]" (hero-gold hero)
           (unless (hero-alive-p hero) "(down)"))
   (format nil "Pack: ~:[nothing~;~:*~{~A~^, ~}~]"
           (mapcar (lambda (name)
                     (format nil "~A~:[~;*~]" (item-title name)
                             (member name (hero-equipped hero))))
                   (hero-items hero)))))

(defun party-full-p (game)
  (>= (length (game-party game)) +party-limit+))

(defun join-party (game hero)
  "Append HERO to the party.  Returns T and emits :PARTY-JOINED on
success; when the roster already holds +PARTY-LIMIT+ members, says so
and returns NIL (no error — recruiting past a full party is a normal
game situation, not a bug)."
  (if (party-full-p game)
      (progn
        (say game "The party is full — ~A cannot join." (hero-name hero))
        nil)
      (progn
        (setf (game-party game)
              (append (game-party game) (list hero)))
        (say game "~A joins the party!" (hero-name hero))
        (emit game :party-joined hero)
        t)))

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
  ;; casters grow spell points like hit points: the new maximum arrives
  ;; as fresh, ready-to-burn sp
  (let ((new-sp (%hero-max-sp (hero-class hero) (hero-level hero)
                              (hero-iq hero))))
    (incf (hero-sp hero) (max 0 (- new-sp (hero-max-sp hero))))
    (setf (hero-max-sp hero) new-sp))
  (say game "~A rises to level ~D!" (hero-name hero) (hero-level hero)))

(defun award-xp (game hero amount)
  "Grant HERO experience, leveling up as thresholds are crossed."
  (incf (hero-xp hero) amount)
  (loop while (>= (hero-xp hero) (xp-for-level (1+ (hero-level hero))))
        do (level-up game hero))
  (hero-xp hero))
