;;; Lambda's Tale — spells.
;;;
;;; Spell types are campaign data, not engine facts: the campaign
;;; registers them with DEFINE-SPELL in its campaign.lisp;
;;; the engine only knows the mechanics.  Casters (hero classes with
;;; :CASTER T) pay spell points; SP regenerate while walking outdoors
;;; in daylight (see ADVANCE-TIME in time.lisp).
;;;
;;; The effect vocabulary is deliberately small (one per spell):
;;;   :damage DICE            combat only — strikes the first living
;;;                           monster (the melee target rule)
;;;   :heal DICE              heals one chosen hero
;;;   :buff-ac N + :duration  party-wide armor bonus for N minutes
;;;                           (an :AC effect record, see game.lisp)
;;;   :light T + :duration    the party carries light for N minutes
;;;                           (a :LIGHT effect — defeats darkness)
;;;
;;; The cast interaction is modeled here too, platform-free (the
;;; SHOP-VIEW pattern): a CAST-VIEW holds the menu state, CAST-LINES
;;; renders it as text lines and CAST-ACT maps a key press to
;;; mechanics — both front-ends drive the same model.

(in-package :tale)

(defstruct (spell-type (:constructor %make-spell-type))
  name                ; symbol, e.g. MAGE-FLAME
  title               ; display string, e.g. "mage flame"
  (cost 1)            ; spell points to cast
  (level 1)           ; minimum caster level
  classes             ; caster classes allowed; NIL = any caster
  effect)             ; one of (:damage DICE) (:heal DICE)
                      ; (:buff-ac N :duration MIN) (:light t :duration MIN)

(defvar *spell-types* (make-hash-table :test 'eq))
(defvar *spell-names* '()
  "Spell names in registration order — the stable order of the menus.")

(defun define-spell (name &key title (cost 1) (level 1) classes
                               damage heal buff-ac light duration)
  "Register spell type NAME (a symbol).  Campaign data calls this.
Exactly one of :DAMAGE DICE, :HEAL DICE, :BUFF-AC N or :LIGHT T names
the effect; :BUFF-AC and :LIGHT need a :DURATION in game minutes.
TITLE defaults to the downcased name (MAGE-FLAME -> \"mage flame\")."
  (let ((kinds (count-if #'identity (list damage heal buff-ac light))))
    (unless (= kinds 1)
      (error "define-spell ~S: exactly one of :damage :heal :buff-ac ~
              :light must be given (got ~D)" name kinds))
    (when (and (or buff-ac light) (not duration))
      (error "define-spell ~S: :buff-ac and :light need a :duration ~
              (game minutes)" name))
    (when (and duration (not (and (integerp duration) (plusp duration))))
      (error "define-spell ~S: :duration ~S must be a positive integer"
             name duration)))
  (setf (gethash name *spell-types*)
        (%make-spell-type
         :name name
         :title (or title
                    (string-downcase (substitute #\Space #\- (string name))))
         :cost cost :level level :classes classes
         :effect (cond (damage (list :damage damage))
                       (heal (list :heal heal))
                       (buff-ac (list :buff-ac buff-ac :duration duration))
                       (light (list :light t :duration duration)))))
  ;; keep the registration order; a re-registration keeps its spot
  (unless (member name *spell-names*)
    (setf *spell-names* (append *spell-names* (list name))))
  name)

(defun find-spell-type (name)
  (or (gethash name *spell-types*)
      (error "Unknown spell ~S (register it with DEFINE-SPELL)" name)))

(defun spell-title (name)
  (spell-type-title (find-spell-type name)))

(defun spell-target-kind (name)
  "What the spell needs aimed at: :HERO (heal), else :NONE (damage
strikes the melee target, buffs and light cover the party)."
  (if (getf (spell-type-effect (find-spell-type name)) :heal)
      :hero
      :none))

(defun spell-known-p (hero name)
  "Does HERO know spell NAME?  A caster of an allowed class (NIL
:classes = any caster) who has reached the spell's level."
  (let ((type (find-spell-type name)))
    (and (hero-caster-p hero)
         (or (null (spell-type-classes type))
             (member (hero-class hero) (spell-type-classes type)))
         (>= (hero-level hero) (spell-type-level type))
         t)))

(defun spells-for-hero (hero)
  "The spells HERO knows, in registration order."
  (remove-if-not (lambda (name) (spell-known-p hero name))
                 *spell-names*))

(defun spell-castable-p (game hero name)
  "Can HERO cast NAME right now?  Known, affordable, and — for a
damage spell — in combat."
  (let ((type (find-spell-type name)))
    (and (spell-known-p hero name)
         (>= (hero-sp hero) (spell-type-cost type))
         (or (not (getf (spell-type-effect type) :damage))
             (and (game-combat game) t)))))

(defun cast-spell (game hero name &optional target)
  "HERO casts spell NAME (on TARGET, a hero, when the spell heals —
defaults to the caster).  Says why and returns NIL when the hero
cannot cast it (unknown, no sp, a damage spell out of combat, nothing
to strike); otherwise pays the sp, applies the effect, emits
:SPELL-CAST and returns T."
  (let* ((type (find-spell-type name))
         (effect (spell-type-effect type)))
    (cond
      ((not (spell-known-p hero name))
       (say game "~A does not know ~A." (hero-name hero)
            (spell-type-title type))
       nil)
      ((< (hero-sp hero) (spell-type-cost type))
       (say game "~A lacks the spell points for ~A." (hero-name hero)
            (spell-type-title type))
       nil)
      ((and (getf effect :damage) (not (game-combat game)))
       (say game "There is nothing to strike ~A at." (spell-type-title type))
       nil)
      ((and (getf effect :damage)
            (null (alive-monsters (game-combat game))))
       (say game "There is nothing left to strike.")
       nil)
      (t
       (decf (hero-sp hero) (spell-type-cost type))
       (say game "~A casts ~A!" (hero-name hero) (spell-type-title type))
       (cond
         ((getf effect :damage)
          (let ((monster (first (alive-monsters (game-combat game))))
                (dmg (max 1 (roll-dice (getf effect :damage)))))
            (%strike-monster game (hero-name hero) monster dmg)))
         ((getf effect :heal)
          (heal-hero game (or target hero)
                     (max 0 (roll-dice (getf effect :heal)))))
         ((getf effect :buff-ac)
          (add-effect game (spell-type-title type)
                      :duration (getf effect :duration)
                      :payload (list :ac (getf effect :buff-ac))))
         ((getf effect :light)
          (add-effect game (spell-type-title type)
                      :duration (getf effect :duration)
                      :payload '(:light t))))
       (emit game :spell-cast hero name)
       t))))

;;; ---------------------------------------------------------------------
;;; The cast interaction model (shared by both front-ends).

(defstruct (cast-view (:constructor %make-cast-view))
  hero                ; the chosen caster, or NIL while picking
  spell               ; the chosen spell name, or NIL while picking
  in-combat)          ; T: committing fights one COMBAT-ROUND

(defun make-cast-view (&key in-combat)
  (%make-cast-view :in-combat in-combat))

(defun %cast-commit (game view target)
  "Resolve the completed pick: cast directly, or — in combat — fight
one round where the caster casts and everyone else attacks."
  (let ((hero (cast-view-hero view))
        (spell (cast-view-spell view)))
    (if (cast-view-in-combat view)
        (combat-round game
                      (mapcar (lambda (h)
                                (if (eq h hero)
                                    (list :cast spell target)
                                    :attack))
                              (alive-heroes game)))
        (cast-spell game hero spell target))
    :done))

(defun cast-lines (game view)
  "The current cast menu as a list of text lines — the front-ends draw
these verbatim (the SHOP-LINES pattern)."
  (let ((hero (cast-view-hero view))
        (spell (cast-view-spell view)))
    (append
     (list "*** Cast a Spell ***" "")
     (cond
       ((null hero)
        (append
         (list "Who casts?" "")
         (let ((i 0))
           (mapcan (lambda (h)
                     (incf i)
                     (when (hero-caster-p h)
                       (list (format nil "~D) ~A  (SP ~D/~D)"
                                     i (hero-name h)
                                     (hero-sp h) (hero-max-sp h)))))
                   (game-party game)))
         (list "" "[1-7] choose  [Esc] cancel")))
       ((null spell)
        (append
         (list (format nil "~A casts.  SP ~D/~D"
                       (hero-name hero) (hero-sp hero) (hero-max-sp hero))
               "")
         (let ((i 0))
           (mapcar (lambda (name)
                     (incf i)
                     (format nil "~D) ~A  ~D sp~:[  (no sp)~;~]"
                             i (spell-title name)
                             (spell-type-cost (find-spell-type name))
                             (>= (hero-sp hero)
                                 (spell-type-cost (find-spell-type name)))))
                   (spells-for-hero hero)))
         (list "" "[1-9] cast  [Esc] back")))
       (t                              ; a healing spell picks its target
        (append
         (list (format nil "~A on whom?" (spell-title spell)) "")
         (let ((i 0))
           (mapcar (lambda (h)
                     (incf i)
                     (format nil "~D) ~A  (HP ~D/~D)"
                             i (hero-name h)
                             (hero-hp h) (hero-max-hp h)))
                   (game-party game)))
         (list "" "[1-7] choose  [Esc] back")))))))

(defun cast-act (game view char)
  "Apply key CHAR to the cast menu.  Returns :DONE when a cast
resolved (the front-end drops the view), :CANCELLED on Esc at the top
level, else NIL."
  (let ((hero (cast-view-hero view))
        (spell (cast-view-spell view))
        (digit (digit-char-p char)))
    (cond
      ;; picking the caster
      ((null hero)
       (cond ((and digit (<= 1 digit (length (game-party game))))
              (let ((h (nth (1- digit) (game-party game))))
                (when (and (hero-caster-p h) (hero-alive-p h))
                  (setf (cast-view-hero view) h)))
              nil)
             ((eql char #\Escape) :cancelled)
             (t nil)))
      ;; picking the spell
      ((null spell)
       (cond ((and digit (<= 1 digit (length (spells-for-hero hero))))
              (let ((name (nth (1- digit) (spells-for-hero hero))))
                (if (spell-castable-p game hero name)
                    (if (eq (spell-target-kind name) :hero)
                        (progn (setf (cast-view-spell view) name) nil)
                        (progn (setf (cast-view-spell view) name)
                               (%cast-commit game view nil)))
                    (progn
                      (say game "~A cannot cast ~A now."
                           (hero-name hero) (spell-title name))
                      nil))))
             ((eql char #\Escape)
              (setf (cast-view-hero view) nil)
              nil)
             (t nil)))
      ;; picking the heal target
      (t
       (cond ((and digit (<= 1 digit (length (game-party game))))
              (%cast-commit game view (nth (1- digit) (game-party game))))
             ((eql char #\Escape)
              (setf (cast-view-spell view) nil)
              nil)
             (t nil))))))
