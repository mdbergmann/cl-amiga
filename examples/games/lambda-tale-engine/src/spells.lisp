;;; Lambda's Tale — spells.
;;;
;;; Spell types are campaign data, not engine facts: the campaign
;;; registers them with DEFINE-SPELL in its campaign.lisp;
;;; the engine only knows the mechanics.  Casters (hero classes with
;;; :CASTER T) pay spell points; SP regenerate while walking outdoors
;;; in daylight (see ADVANCE-TIME in time.lisp).
;;;
;;; A spell's effect is a plist over the shared effect vocabulary
;;; (game.lisp): the INSTANT keys (*INSTANT-EFFECT-KEYS* — the damage
;;; family, :heal/:heal-party/:resurrect, :scry, :summon ...) resolve
;;; at cast time; the TIMED keys (*TIMED-EFFECT-KEYS* — :buff-ac,
;;; :light, :compass, :buff-damage, :foes-ac ...) merge into one
;;; effect record via APPLY-EFFECT-SPEC, the funnel shared with usable
;;; items and songs.  Keys combine freely — a restoration heals AND
;;; cures; a batchspell installs five enchantments in one record.
;;; :IMAGE names the icon the effects band draws while a timed effect
;;; burns.
;;;
;;; Beside the mechanics a spell carries display metadata straight
;;; from the old lore: :CODE (the four-letter incantation, \"MAFL\"),
;;; :RANGE and :DURATION-TEXT (the spellbook's own words).  The
;;; engine stores and exposes them (SPELL-CODE, SPELL-RANGE,
;;; SPELL-DURATION-TEXT); the mechanics never read them.
;;;
;;; The cast interaction is modeled here too, platform-free (the
;;; SHOP-VIEW pattern): a CAST-VIEW holds the menu state, CAST-LINES
;;; renders it as text lines and CAST-ACT maps a key press to
;;; mechanics — both front-ends drive the same model.

(in-package :tale)

(defstruct (spell-type (:constructor %make-spell-type))
  name                ; symbol, e.g. MAGE-FLAME
  title               ; display string, e.g. "mage flame"
  code                ; four-letter incantation ("MAFL"), or NIL
  range               ; the spellbook's range text ("1 foe (10')"), or NIL
  duration-text       ; the spellbook's duration text ("medium"), or NIL
  (cost 1)            ; spell points to cast
  (level 1)           ; minimum caster level
  classes             ; caster classes allowed; NIL = any caster
  effect              ; effect plist over the shared vocabulary, e.g.
                      ; (:damage "1d4"), (:heal "10d4" :cure (:poison)),
                      ; (:buff-ac 2 :light t :duration 30)
  image)              ; effects-band icon file for the timed kinds, or NIL

(defvar *spell-types* (make-hash-table :test 'eq))
(defvar *spell-names* '()
  "Spell names in registration order — the stable order of the menus.")

(defparameter %spell-meta-keys
  '(:title :code :range :duration-text :cost :level :classes :image)
  "DEFINE-SPELL's non-effect keywords; everything else in the argument
plist is the effect spec.")

(defun define-spell (name &rest args)
  "Register spell type NAME (a symbol).  Campaign data calls this.
ARGS is a plist: :TITLE, :CODE, :RANGE, :DURATION-TEXT (display
metadata), :COST (sp, default 1), :LEVEL (minimum caster level,
default 1), :CLASSES (allowed caster classes; NIL = any caster),
:IMAGE (the effects-band icon for the timed kinds) — and the effect
spec itself, one or more keys of the shared vocabulary (see
*INSTANT-EFFECT-KEYS* / *TIMED-EFFECT-KEYS* in game.lisp) plus
:DURATION (game minutes, or :INDEFINITE) when any timed key rides
along.  TITLE defaults to the downcased name (MAGE-FLAME ->
\"mage flame\")."
  (loop for tail on args by #'cddr
        unless (and (keywordp (first tail)) (cdr tail))
          do (error "define-spell ~S: malformed argument plist at ~S"
                    name tail))
  (let ((spec (loop for (key value) on args by #'cddr
                    unless (member key %spell-meta-keys)
                      append (list key value))))
    (check-effect-spec "define-spell" name spec)
    (setf (gethash name *spell-types*)
          (%make-spell-type
           :name name
           :title (or (getf args :title)
                      (string-downcase (substitute #\Space #\- (string name))))
           :code (getf args :code)
           :range (getf args :range)
           :duration-text (getf args :duration-text)
           :cost (or (getf args :cost) 1)
           :level (or (getf args :level) 1)
           :classes (getf args :classes)
           :effect spec
           :image (getf args :image))))
  ;; keep the registration order; a re-registration keeps its spot
  (unless (member name *spell-names*)
    (setf *spell-names* (append *spell-names* (list name))))
  name)

(defun find-spell-type (name)
  (or (gethash name *spell-types*)
      (error "Unknown spell ~S (register it with DEFINE-SPELL)" name)))

(defun spell-title (name)
  (spell-type-title (find-spell-type name)))

(defun spell-code (name)
  "The spell's four-letter incantation (\"MAFL\"), or NIL."
  (spell-type-code (find-spell-type name)))

(defun spell-range (name)
  "The spellbook's range text for the spell, or NIL."
  (spell-type-range (find-spell-type name)))

(defun spell-duration-text (name)
  "The spellbook's duration text for the spell, or NIL."
  (spell-type-duration-text (find-spell-type name)))

(defun spell-target-kind (name)
  "What the spell needs aimed at: :HERO when it heals, cures or raises
one chosen hero; else :NONE (damage strikes the melee target, buffs
and light cover the party, :heal-party needs no choosing)."
  (let ((effect (spell-type-effect (find-spell-type name))))
    (if (and (not (getf effect :heal-party))
             (or (getf effect :heal) (getf effect :resurrect)
                 (getf effect :cure)))
        :hero
        :none)))

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
spell that needs a fight (the damage family and the foe-handling
keys) — in combat."
  (let ((type (find-spell-type name)))
    (and (spell-known-p hero name)
         (>= (hero-sp hero) (spell-type-cost type))
         (or (not (effect-spec-combat-only-p (spell-type-effect type)))
             (and (game-combat game) t)))))

(defun %revive-hero (game hero)
  "Raise the fallen HERO to one hit point (the resurrection effect)."
  (setf (hero-hp hero) 1)
  (say game "~A rises again!" (hero-name hero))
  (emit game :hero-revived hero))

(defun %heal-amount (hero spec)
  "The hit points a :HEAL/:HEAL-PARTY value SPEC grants HERO — a dice
roll, or everything missing for :FULL."
  (if (eq spec :full)
      (- (hero-max-hp hero) (hero-hp hero))
      (max 0 (roll-dice spec))))

(defun %apply-instant-effects (game hero effect target)
  "Resolve EFFECT's instant keys for the caster HERO (TARGET is the
chosen hero for the single-target kinds, defaulting to the caster).
The combat-only kinds may assume a fight with living monsters — the
cast refusals guarantee it.  The keys whose subsystem is still to
come (:cure, :summon, :teleport, ...) speak their flavor line so the
canonical spell already casts."
  (let ((combat (game-combat game))
        (target (or target hero)))
    ;; the mending family first — a healer's round helps before it harms
    (when (getf effect :resurrect)
      (if (getf effect :heal-party)
          (dolist (h (game-party game))     ; raise every fallen hero
            (unless (hero-alive-p h) (%revive-hero game h)))
          (if (hero-alive-p target)
              (say game "~A has not fallen." (hero-name target))
              (%revive-hero game target))))
    (when (getf effect :heal)
      (heal-hero game target (%heal-amount target (getf effect :heal))))
    (when (getf effect :heal-party)
      (dolist (h (alive-heroes game))
        (heal-hero game h (%heal-amount h (getf effect :heal-party)))))
    (when (getf effect :cure)             ; ailments are a coming subsystem
      (if (or (getf effect :heal-party) (getf effect :resurrect))
          (say game "The party is cleansed.")
          (say game "~A is cleansed." (hero-name target))))
    (when (getf effect :scry)
      (say game "You stand at (~D,~D) in ~A, facing ~A."
           (game-x game) (game-y game) (map-title (game-map game))
           (string-capitalize (dir-keyword (game-facing game)))))
    (when (getf effect :disarm-traps)     ; traps are a coming subsystem
      (say game "The way ahead is made safe."))
    ;; the damage family — each strike re-aims at the front survivor
    (flet ((front () (first (alive-monsters combat)))
           (strike (monster dmg)
             (%strike-monster game (hero-name hero) monster dmg)))
      (when (and (getf effect :damage) (front))
        (strike (front) (max 1 (roll-dice (getf effect :damage)))))
      (when (and (getf effect :damage-per-level) (front))
        (strike (front)
                (max 1 (* (roll-dice (getf effect :damage-per-level))
                          (hero-level hero)))))
      (when (and (getf effect :damage-group) (front))
        (let ((kind (monster-kind (front))))
          (dolist (m (alive-monsters combat))
            (when (eq (monster-kind m) kind)
              (strike m (max 1 (roll-dice (getf effect :damage-group))))))))
      (when (getf effect :damage-all)
        (dolist (m (alive-monsters combat))
          (strike m (max 1 (roll-dice (getf effect :damage-all))))))
      (when (and (getf effect :slay) (front))
        (let ((monster (front)))
          (if (< (roll 100) (getf effect :slay))
              (strike monster (monster-hp monster))
              (say game "The ~A resists the spell!"
                   (monster-type-name (monster-kind monster)))))))
    ;; foe handling and the marvels — flavor until their subsystem lands
    (when (getf effect :push-foes)
      (say game "The foes are hurled away!"))
    (when (getf effect :halt-foes)
      (say game "The foes freeze mid-stride!"))
    (when (getf effect :calm)
      (say game "A strange calm settles over the foes."))
    (when (getf effect :summon)
      (say game "A ~A answers the call -- and fades away again."
           (getf effect :summon)))
    (when (getf effect :teleport)
      (say game "Space folds and shimmers, but the way stays shut."))))

(defun cast-spell (game hero name &optional target)
  "HERO casts spell NAME (on TARGET, a hero, when the spell mends one
chosen hero — defaults to the caster).  Says why and returns NIL when
the hero cannot cast it (unknown, no sp, a battle spell out of
combat, nothing to strike); otherwise pays the sp, applies the
instant keys and installs the timed keys as one effect record, emits
:SPELL-CAST and returns T."
  (let* ((type (find-spell-type name))
         (effect (spell-type-effect type))
         (combat-only (effect-spec-combat-only-p effect)))
    (cond
      ((not (spell-known-p hero name))
       (say game "~A does not know ~A." (hero-name hero)
            (spell-type-title type))
       nil)
      ((< (hero-sp hero) (spell-type-cost type))
       (say game "~A lacks the spell points for ~A." (hero-name hero)
            (spell-type-title type))
       nil)
      ((and combat-only (not (game-combat game)))
       (say game "There is nothing to strike ~A at." (spell-type-title type))
       nil)
      ((and combat-only
            (null (alive-monsters (game-combat game))))
       (say game "There is nothing left to strike.")
       nil)
      (t
       (decf (hero-sp hero) (spell-type-cost type))
       (say game "~A casts ~A!" (hero-name hero) (spell-type-title type))
       (%apply-instant-effects game hero effect target)
       (when (loop for entry in *timed-effect-keys*
                   thereis (getf effect (first entry)))
         (apply-effect-spec game (spell-type-title type) effect
                            :image (spell-type-image type)))
       (emit game :spell-cast hero name)
       t))))

;;; ---------------------------------------------------------------------
;;; The cast interaction model (shared by both front-ends).

(defstruct (cast-view (:constructor %make-cast-view))
  hero                ; the chosen caster, or NIL while picking
  spell               ; the chosen spell name, or NIL while picking
  in-combat           ; T: committing fights one COMBAT-ROUND;
                      ; :ORDERS: committing returns the pick as a
                      ; round action (the combat-orders flow)
  (top 0))            ; scroll offset into the spell list

(defun make-cast-view (&key in-combat hero)
  "HERO presets the caster (the combat-orders flow asks for one hero's
pick); NIL starts at the who-casts page."
  (%make-cast-view :in-combat in-combat :hero hero))

(defun %cast-commit (game view target)
  "Resolve the completed pick: cast directly; in combat fight one
round where the caster casts and everyone else attacks; in :ORDERS
mode hand the pick back as (:ACTION (:CAST SPELL [TARGET]))."
  (let ((hero (cast-view-hero view))
        (spell (cast-view-spell view)))
    (cond
      ((eq (cast-view-in-combat view) :orders)
       (list :action (if target
                         (list :cast spell target)
                         (list :cast spell))))
      ((cast-view-in-combat view)
       (combat-round game
                     (mapcar (lambda (h)
                               (if (eq h hero)
                                   (list :cast spell target)
                                   :attack))
                             (alive-heroes game)))
       :done)
      (t (cast-spell game hero spell target)
         :done))))

(defun cast-lines (game view)
  "The current cast menu as a list of menu lines — the front-ends draw
these verbatim (the SHOP-LINES pattern); option rows carry their pick
key (see MENU-NUMBERED)."
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
                       (list (menu-numbered
                              i (format nil "~D) ~A  (SP ~D/~D)"
                                        i (hero-name h)
                                        (hero-sp h) (hero-max-sp h))))))
                   (game-party game)))
         (list "" "[1-7] choose  [Esc] cancel")))
       ((null spell)
        (append
         (list (format nil "~A casts.  SP ~D/~D"
                       (hero-name hero) (hero-sp hero) (hero-max-sp hero))
               "")
         (menu-scrolled-lines
          (spells-for-hero hero) (cast-view-top view)
          (lambda (i name)
            (menu-numbered
             i (format nil "~D) ~A  ~D sp~:[  (no sp)~;~]"
                       i (spell-title name)
                       (spell-type-cost (find-spell-type name))
                       (>= (hero-sp hero)
                           (spell-type-cost
                            (find-spell-type name)))))))
         (list "" "[1-9] cast  [Esc] back")))
       (t                              ; a healing spell picks its target
        (append
         (list (format nil "~A on whom?" (spell-title spell)) "")
         (let ((i 0))
           (mapcar (lambda (h)
                     (incf i)
                     (menu-numbered
                      i (format nil "~D) ~A  (HP ~D/~D)"
                                i (hero-name h)
                                (hero-hp h) (hero-max-hp h))))
                   (game-party game)))
         (list "" "[1-7] choose  [Esc] back")))))))

(defun cast-act (game view char)
  "Apply key CHAR to the cast menu.  Returns :DONE when a cast
resolved (the front-end drops the view) — in :ORDERS mode
\(:ACTION (:CAST ...)) instead — :CANCELLED on Esc at the top level,
else NIL."
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
       (cond (digit
              (let ((name (menu-window-pick (spells-for-hero hero)
                                            (cast-view-top view) digit)))
                (when name
                  (if (spell-castable-p game hero name)
                      (if (eq (spell-target-kind name) :hero)
                          (progn (setf (cast-view-spell view) name) nil)
                          (progn (setf (cast-view-spell view) name)
                                 (%cast-commit game view nil)))
                      (progn
                        (say game "~A cannot cast ~A now."
                             (hero-name hero) (spell-title name))
                        nil)))))
             ((eql char #\Escape)
              (setf (cast-view-hero view) nil
                    (cast-view-top view) 0)
              nil)
             (t
              (let ((top (menu-scroll (cast-view-top view) char
                                      (length (spells-for-hero hero)))))
                (when top (setf (cast-view-top view) top)))
              nil)))
      ;; picking the heal target
      (t
       (cond ((and digit (<= 1 digit (length (game-party game))))
              (%cast-commit game view (nth (1- digit) (game-party game))))
             ((eql char #\Escape)
              (setf (cast-view-spell view) nil)
              nil)
             (t nil))))))
