;;; Lambda's Tale — items, inventory and equipment.
;;;
;;; Item types are campaign data, not engine facts: the campaign
;;; registers them with DEFINE-ITEM in its campaign.lisp and maps
;;; refer to them by name in shop stock lists.  The engine only knows
;;; the mechanics: a hero carries up to +INVENTORY-LIMIT+ items and
;;; can equip one item of each equipment kind (*ITEM-KINDS* less
;;; :MISC — weapon, armor, shield, helmet, gloves, bow, arrow,
;;; instrument, ring, wand, figurine) at a time.  Armor class is
;;; descending, so an item's :AC bonus *lowers* the effective AC.
;;;
;;; An item may also be USABLE (:USE) — a torch, a potion, a wand:
;;; using it applies an effect from the same vocabulary spells speak —
;;; instant keys that need no battle ((:heal DICE), (:summon NAME)),
;;; or timed keys through APPLY-EFFECT-SPEC ((:light t :duration MIN),
;;; (:buff-ac N :duration MIN)), but not both at once.  The battle
;;; instants (the damage family) may not ride on an item directly:
;;; a spell-triggering item says :USE (:CAST SPELL) instead and casts
;;; that registered spell for free — no spell points, no spellbook,
;;; the item is the magic (Bard's Tale's Wizhelm).  A :CONSUMED item
;;; leaves the pack on use, and :IMAGE names the effects-band icon of
;;; the installed effect.  The use interaction (USE-VIEW / USE-LINES /
;;; USE-ACT, the SHOP-VIEW pattern) lives here too, driven by both
;;; front-ends, and so does the gear page (EQUIP-VIEW — 'e' on the
;;; character sheet): a digit toggles a pack item on/off, class-unfit
;;; items are marked.

(in-package :tale)

(defconstant +inventory-limit+ 8
  "Maximum items a hero can carry (Bard's Tale pack size).")

(defparameter *item-kinds*
  '(:weapon :armor :shield :helmet :gloves :bow :arrow
    :instrument :ring :wand :figurine :misc)
  "The item kinds (Bard's Tale's equipment categories).  Every kind
but :MISC is equipment: a hero wears one item of each kind at a time,
and every equipped item's :AC bonus counts.")

(defstruct (item-type (:constructor %make-item-type))
  name                ; symbol, e.g. SHORT-SWORD
  title               ; display string, e.g. "Short Sword"
  (kind :misc)        ; one of *ITEM-KINDS*
  (price 0)           ; shop price in gold
  damage              ; attack dice (weapons), or NIL
  (ac 0)              ; armor bonus: subtracted from descending AC
  classes             ; hero classes allowed to use it; NIL = anyone
  use                 ; effect on use: a non-battle instant spec
                      ; (:heal DICE), a timed spec (:light t :duration
                      ; MIN), or (:cast SPELL); NIL = not usable
  consumed            ; T: one use, the item leaves the pack
  image)              ; effects-band icon for the timed :use, or NIL

(defvar *item-types* (make-hash-table :test 'eq))

(defun define-item (name &key title (kind :misc) (price 0) damage (ac 0)
                              classes use consumed image)
  "Register item type NAME (a symbol).  Campaign data calls this.
TITLE defaults to the capitalized name (SHORT-SWORD -> \"Short Sword\").
:USE makes the item usable, one of three shapes: instant keys of the
shared vocabulary that need no battle (e.g. (:heal DICE),
\(:summon NAME) — the damage family is refused); a timed spec
\(*TIMED-EFFECT-KEYS* in game.lisp, e.g. (:light t :duration 30))
that installs its effect; or (:cast SPELL) — using the item casts the
already-registered spell for free, so register the spell first.
:CONSUMED spends the item on use and :IMAGE names the installed
effect's band icon."
  (unless (member kind *item-kinds*)
    (error "define-item ~S: kind ~S is not one of ~{~S~^ ~}"
           name kind *item-kinds*))
  (when use
    (unless (consp use)
      (error "define-item ~S: :use ~S must be an effect plist -- ~
              (:heal DICE), a timed spec like (:light t :duration 30), ~
              or (:cast SPELL)"
             name use))
    (if (eq (first use) :cast)
        (progn
          (unless (and (= (length use) 2)
                       (symbolp (second use)) (second use))
            (error "define-item ~S: a casting :use is (:cast SPELL) ~
                    alone (got ~S)" name use))
          ;; a clear error now beats a broken item later: the spell
          ;; must already be registered (DEFINE-SPELL comes first)
          (find-spell-type (second use)))
        (multiple-value-bind (timed instant)
            (check-effect-spec "define-item" name use)
          (when (and timed instant)
            (error "define-item ~S: :use ~S mixes timed and instant ~
                    effects -- an item speaks one or the other"
                   name use))
          (when (effect-spec-combat-only-p use)
            (error "define-item ~S: :use ~S is a battle effect -- ~
                    register a spell and give the item :use ~
                    (:cast SPELL) instead"
                   name use)))))
  (when (and consumed (not use))
    (error "define-item ~S: :consumed without a :use" name))
  (setf (gethash name *item-types*)
        (%make-item-type
         :name name
         :title (or title
                    (string-capitalize (substitute #\Space #\- (string name))))
         :kind kind :price price :damage damage :ac ac :classes classes
         :use use :consumed consumed :image image))
  name)

(defun find-item-type (name)
  (or (gethash name *item-types*)
      (error "Unknown item ~S (register it with DEFINE-ITEM)" name)))

(defun item-title (name)
  (item-type-title (find-item-type name)))

(defun item-usable-p (hero name)
  "Can HERO's class use item NAME?  (NIL :classes means anyone.)"
  (let ((classes (item-type-classes (find-item-type name))))
    (or (null classes)
        (and (member (hero-class hero) classes) t))))

(defun item-fit-marker (hero name)
  "\" (unfit)\" when HERO's class cannot use item NAME, else \"\" —
the sheet, gear and shop pages append it to the item's row so a
class mismatch shows before the player tries (or buys)."
  (if (item-usable-p hero name) "" " (unfit)"))

;;; ---------------------------------------------------------------------
;;; Inventory

(defun hero-carrying-p (hero name)
  (member name (hero-items hero)))

(defun give-item (game hero name)
  "Put item NAME into HERO's pack.  Returns T, or says the pack is full
and returns NIL (like JOIN-PARTY, a full pack is a game situation, not
a bug)."
  (find-item-type name)
  (if (>= (length (hero-items hero)) +inventory-limit+)
      (progn
        (say game "~A's pack is full." (hero-name hero))
        nil)
      (progn
        (setf (hero-items hero) (append (hero-items hero) (list name)))
        t)))

(defun drop-item (game hero name)
  "Remove one item NAME from HERO's pack (unequipping it first).
Returns T, or NIL when the hero does not carry it."
  (declare (ignore game))
  (when (hero-carrying-p hero name)
    (setf (hero-equipped hero) (remove name (hero-equipped hero) :count 1))
    (setf (hero-items hero) (remove name (hero-items hero) :count 1))
    t))

;;; ---------------------------------------------------------------------
;;; Equipment

(defun equipped-of-kind (hero kind)
  "The equipped item of KIND, or NIL."
  (find kind (hero-equipped hero)
        :key (lambda (name) (item-type-kind (find-item-type name)))))

(defun equip-item (game hero name)
  "Equip item NAME from HERO's pack, replacing any equipped item of the
same kind.  Returns T; says why and returns NIL when the hero does not
carry it, the item is not equipment, or the class cannot use it."
  (cond ((not (hero-carrying-p hero name))
         (say game "~A does not carry ~A." (hero-name hero) (item-title name))
         nil)
        ((eq (item-type-kind (find-item-type name)) :misc)
         (say game "~A cannot be equipped." (item-title name))
         nil)
        ((not (item-usable-p hero name))
         (say game "~A cannot use ~A." (hero-name hero) (item-title name))
         nil)
        (t
         (let ((old (equipped-of-kind
                     hero (item-type-kind (find-item-type name)))))
           (when old
             (setf (hero-equipped hero)
                   (remove old (hero-equipped hero) :count 1))))
         (setf (hero-equipped hero)
               (append (hero-equipped hero) (list name)))
         (say game "~A equips ~A." (hero-name hero) (item-title name))
         t)))

(defun unequip-item (game hero name)
  "Unequip item NAME (it stays in the pack).  Returns T, or NIL when it
was not equipped."
  (declare (ignore game))
  (when (member name (hero-equipped hero))
    (setf (hero-equipped hero) (remove name (hero-equipped hero) :count 1))
    t))

(defun toggle-equip (game hero name)
  "Equip pack item NAME, or take it off when it is worn — the gear
page's one-key toggle.  Returns T on a change; says why and returns
NIL when the item cannot go on (see EQUIP-ITEM)."
  (if (member name (hero-equipped hero))
      (progn
        (unequip-item game hero name)
        (say game "~A removes ~A." (hero-name hero) (item-title name))
        t)
      (equip-item game hero name)))

(defun hero-attack-dice (hero)
  "The dice HERO attacks with: the equipped weapon's damage, else the
hero's bare (class) damage."
  (let ((weapon (equipped-of-kind hero :weapon)))
    (if weapon
        (or (item-type-damage (find-item-type weapon)) (hero-damage hero))
        (hero-damage hero))))

(defun hero-effective-ac (hero &optional game)
  "HERO's armor class with equipment: descending AC minus the AC bonus
of every equipped item — and, when GAME is given, minus the party-wide
:AC effect bonuses (a spell shield lowers it further)."
  (let ((ac (hero-ac hero)))
    (dolist (name (hero-equipped hero))
      (decf ac (item-type-ac (find-item-type name))))
    (when game
      (decf ac (effects-ac-bonus game)))
    ac))

;;; ---------------------------------------------------------------------
;;; The gear page (opened from the character sheet with 'e' — the
;;; SHOP-VIEW pattern): the hero's pack as a numbered list, a digit
;;; toggles that item on/off, unfit items carry the (unfit) marker.

(defstruct (equip-view (:constructor %make-equip-view))
  hero                ; the hero whose gear page this is
  (top 0))            ; scroll offset into the pack list

(defun make-equip-view (hero)
  (%make-equip-view :hero hero))

(defun equip-lines (game view)
  "The gear page as a list of menu lines — the front-ends draw these
verbatim (the SHOP-LINES pattern).  Equipped items are starred, items
the hero's class cannot use are marked (unfit); the AC/attack header
shows the effect of every toggle."
  (let ((hero (equip-view-hero view)))
    (append
     (list (format nil "*** ~A's Gear ***" (hero-name hero))
           ""
           (format nil "AC ~D   Attack ~A"
                   (hero-effective-ac hero game) (hero-attack-dice hero))
           "")
     (if (hero-items hero)
         (menu-scrolled-lines
          (hero-items hero) (equip-view-top view)
          (lambda (i name)
            (menu-numbered
             i (format nil "~D) ~A~:[~;*~]~A" i (item-title name)
                       (member name (hero-equipped hero))
                       (item-fit-marker hero name)))))
         (list "The pack is empty."))
     (list "" "[1-9] equip/remove  [Esc] back"))))

(defun equip-act (game view char)
  "Apply key CHAR to the gear page: a digit toggles that pack item —
worn comes off, equipment goes on (TOGGLE-EQUIP says why when it
cannot) — u/d scroll a long pack, Esc closes the page.  Returns
:CANCELLED on Esc, else NIL."
  (let ((hero (equip-view-hero view))
        (digit (digit-char-p char)))
    (cond (digit
           (let ((name (menu-window-pick (hero-items hero)
                                         (equip-view-top view) digit)))
             (when name
               (toggle-equip game hero name)))
           nil)
          ((eql char #\Escape) :cancelled)
          (t
           (let ((top (menu-scroll (equip-view-top view) char
                                   (length (hero-items hero)))))
             (when top (setf (equip-view-top view) top)))
           nil))))

;;; ---------------------------------------------------------------------
;;; Using items

(defun usable-items (hero)
  "The :USE-carrying items in HERO's pack the hero's class may use —
duplicates kept: two torches are two uses."
  (remove-if-not (lambda (name)
                   (and (item-type-use (find-item-type name))
                        (item-usable-p hero name)))
                 (hero-items hero)))

(defun %use-instant-p (use)
  "True when the validated item :USE spec speaks instant keys (else it
is a timed spec; a spec never mixes the two — DEFINE-ITEM refuses)."
  (loop for tail on use by #'cddr
        thereis (and (assoc (first tail) *instant-effect-keys*) t)))

(defun item-target-kind (name)
  "What using item NAME needs aimed at: :HERO when its effect — or the
spell a (:cast SPELL) use triggers — mends one chosen hero; else
:NONE."
  (let ((use (item-type-use (find-item-type name))))
    (cond ((null use) :none)
          ((eq (first use) :cast) (spell-target-kind (second use)))
          (t (effect-spec-target-kind use)))))

(defun use-item (game hero name &optional target)
  "HERO uses item NAME (on TARGET, a hero, when the use mends one
chosen hero — defaults to the user).  Says why and returns NIL when
the hero does not carry it, the class cannot use it, it has no use,
or a (:cast SPELL) trigger is a battle spell with nothing to strike —
the item is not spent.  Otherwise applies the :USE — non-battle
instant keys through the spell machinery, a timed spec through
APPLY-EFFECT-SPEC, a (:cast SPELL) trigger as a free cast (no spell
points, no spellbook: the item is the magic) — spends a :CONSUMED
item, emits :ITEM-USED and returns T."
  (let* ((type (find-item-type name))
         (use (item-type-use type)))
    (cond
      ((not (hero-carrying-p hero name))
       (say game "~A does not carry ~A." (hero-name hero)
            (item-type-title type))
       nil)
      ((not (item-usable-p hero name))
       (say game "~A cannot use ~A." (hero-name hero)
            (item-type-title type))
       nil)
      ((null use)
       (say game "Nothing happens.")
       nil)
      (t
       (let ((spell (and (eq (first use) :cast) (second use))))
         (cond
           ((and spell
                 (%spell-strike-blocked-p game (find-spell-type spell)))
            nil)                        ; it said why; the item keeps
           (t
            (say game "~A uses ~A." (hero-name hero)
                 (item-type-title type))
            (cond
              (spell
               (say game "The ~A casts ~A!"
                    (item-type-title type) (spell-title spell))
               (%resolve-spell-cast game hero (find-spell-type spell)
                                    target))
              ((%use-instant-p use)
               (%apply-instant-effects game hero use target))
              (t
               (apply-effect-spec game (item-type-title type) use
                                  :image (item-type-image type))))
            (when (item-type-consumed type)
              (drop-item game hero name))
            (emit game :item-used hero name)
            t)))))))

;;; ---------------------------------------------------------------------
;;; The use interaction model (shared by both front-ends — the
;;; CAST-VIEW pattern: pick the user, the item, and — for a healing
;;; item — the target).

(defstruct (use-view (:constructor %make-use-view))
  hero                ; the chosen user, or NIL while picking
  item                ; the chosen item name, or NIL while picking
  in-combat           ; T: committing fights one COMBAT-ROUND;
                      ; :ORDERS: committing returns the pick as a
                      ; round action (the combat-orders flow)
  (top 0))            ; scroll offset into the item list

(defun make-use-view (&key in-combat hero)
  "HERO presets the user (the combat-orders flow asks for one hero's
pick); NIL starts at the who-uses page."
  (%make-use-view :in-combat in-combat :hero hero))

(defun %use-commit (game view target)
  "Resolve the completed pick: use directly; in combat fight one round
where the user uses the item and everyone else attacks; in :ORDERS
mode hand the pick back as (:ACTION (:USE ITEM [TARGET]))."
  (let ((hero (use-view-hero view))
        (item (use-view-item view)))
    (cond
      ((eq (use-view-in-combat view) :orders)
       (list :action (if target
                         (list :use item target)
                         (list :use item))))
      ((use-view-in-combat view)
       (combat-round game
                     (mapcar (lambda (h)
                               (if (eq h hero)
                                   (list :use item target)
                                   :attack))
                             (alive-heroes game)))
       :done)
      (t (use-item game hero item target)
         :done))))

(defun use-lines (game view)
  "The current use menu as a list of menu lines — the front-ends draw
these verbatim (the SHOP-LINES pattern); option rows carry their pick
key (see MENU-NUMBERED)."
  (let ((hero (use-view-hero view))
        (item (use-view-item view)))
    (append
     (list "*** Use an Item ***" "")
     (cond
       ((null hero)
        (append
         (list "Who uses?" "")
         (let ((i 0))
           (mapcar (lambda (h)
                     (incf i)
                     (menu-numbered
                      i (format nil "~D) ~A  (~D usable)"
                                i (hero-name h)
                                (length (usable-items h)))))
                   (game-party game)))
         (list "" "[1-7] choose  [Esc] cancel")))
       ((null item)
        (append
         (list (format nil "~A uses." (hero-name hero)) "")
         (menu-scrolled-lines
          (usable-items hero) (use-view-top view)
          (lambda (i name)
            (menu-numbered
             i (format nil "~D) ~A" i (item-title name)))))
         (list "" "[1-9] use  [Esc] back")))
       (t                              ; a mending item picks its target
        (append
         (list (format nil "~A on whom?" (item-title item)) "")
         (let ((i 0))
           (mapcar (lambda (h)
                     (incf i)
                     (menu-numbered
                      i (format nil "~D) ~A  (HP ~D/~D)"
                                i (hero-name h)
                                (hero-hp h) (hero-max-hp h))))
                   (game-party game)))
         (list "" "[1-7] choose  [Esc] back")))))))

(defun use-act (game view char)
  "Apply key CHAR to the use menu.  Returns :DONE when a use resolved
(the front-end drops the view) — in :ORDERS mode
\(:ACTION (:USE ...)) instead — :CANCELLED on Esc at the top level,
else NIL."
  (let ((hero (use-view-hero view))
        (item (use-view-item view))
        (digit (digit-char-p char)))
    (cond
      ;; picking the user
      ((null hero)
       (cond ((and digit (<= 1 digit (length (game-party game))))
              (let ((h (nth (1- digit) (game-party game))))
                (when (and (hero-alive-p h) (usable-items h))
                  (setf (use-view-hero view) h)))
              nil)
             ((eql char #\Escape) :cancelled)
             (t nil)))
      ;; picking the item
      ((null item)
       (cond (digit
              (let ((name (menu-window-pick (usable-items hero)
                                            (use-view-top view) digit)))
                (when name
                  (setf (use-view-item view) name)
                  (if (eq (item-target-kind name) :hero)
                      nil               ; a mender picks its target next
                      (%use-commit game view nil)))))
             ((eql char #\Escape)
              (setf (use-view-hero view) nil
                    (use-view-top view) 0)
              nil)
             (t
              (let ((top (menu-scroll (use-view-top view) char
                                      (length (usable-items hero)))))
                (when top (setf (use-view-top view) top)))
              nil)))
      ;; picking the heal target
      (t
       (cond ((and digit (<= 1 digit (length (game-party game))))
              (%use-commit game view (nth (1- digit) (game-party game))))
             ((eql char #\Escape)
              (setf (use-view-item view) nil)
              nil)
             (t nil))))))
