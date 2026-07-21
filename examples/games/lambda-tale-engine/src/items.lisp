;;; Lambda's Tale — items, inventory and equipment.
;;;
;;; Item types are campaign data, not engine facts: the campaign
;;; registers them with DEFINE-ITEM in its campaign.lisp and maps
;;; refer to them by name in shop stock lists.  The engine only knows
;;; the mechanics: a hero carries up to +INVENTORY-LIMIT+ items and can
;;; equip one weapon, one armor and one shield at a time.  Armor class
;;; is descending, so an item's :AC bonus *lowers* the effective AC.
;;;
;;; An item may also be USABLE (:USE) — a torch, a potion: using it
;;; applies an effect from the same vocabulary spells speak, either
;;; instant (:heal DICE) or timed through APPLY-EFFECT-SPEC
;;; ((:light t :duration MIN), (:buff-ac N :duration MIN),
;;; (:compass t :duration MIN)); a :CONSUMED item leaves the pack on
;;; use, and :IMAGE names the effects-band icon of the installed
;;; effect.  The use interaction (USE-VIEW / USE-LINES / USE-ACT, the
;;; SHOP-VIEW pattern) lives here too, driven by both front-ends, and
;;; so does the gear page (EQUIP-VIEW — 'e' on the character sheet):
;;; a digit toggles a pack item on/off, class-unfit items are marked.

(in-package :tale)

(defconstant +inventory-limit+ 8
  "Maximum items a hero can carry (Bard's Tale pack size).")

(defstruct (item-type (:constructor %make-item-type))
  name                ; symbol, e.g. SHORT-SWORD
  title               ; display string, e.g. "Short Sword"
  (kind :misc)        ; :weapon, :armor, :shield or :misc
  (price 0)           ; shop price in gold
  damage              ; attack dice (weapons), or NIL
  (ac 0)              ; armor bonus: subtracted from descending AC
  classes             ; hero classes allowed to use it; NIL = anyone
  use                 ; effect on use: (:heal DICE) or a timed spec
                      ; (:light t :duration MIN) etc.; NIL = not usable
  consumed            ; T: one use, the item leaves the pack
  image)              ; effects-band icon for the timed :use, or NIL

(defvar *item-types* (make-hash-table :test 'eq))

(defun define-item (name &key title (kind :misc) (price 0) damage (ac 0)
                              classes use consumed image)
  "Register item type NAME (a symbol).  Campaign data calls this.
TITLE defaults to the capitalized name (SHORT-SWORD -> \"Short Sword\").
:USE makes the item usable — (:heal DICE) heals a chosen hero, the
timed kinds ((:light t :duration MIN), (:buff-ac N :duration MIN),
(:compass t :duration MIN)) install the effect; :CONSUMED spends the
item on use and :IMAGE names the installed effect's band icon."
  (unless (member kind '(:weapon :armor :shield :misc))
    (error "define-item ~S: kind ~S is not one of :weapon :armor :shield :misc"
           name kind))
  (when use
    (unless (and (consp use)
                 (member (first use) '(:heal :buff-ac :light :compass)))
      (error "define-item ~S: :use ~S must be (:heal DICE) or a timed ~
              (:buff-ac ...), (:light ...) or (:compass ...) spec"
             name use))
    (unless (eq (first use) :heal)
      (let ((duration (getf use :duration)))
        (unless (and (integerp duration) (plusp duration))
          (error "define-item ~S: a timed :use needs a positive integer ~
                  :duration (got ~S)" name duration)))))
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

(defun use-item (game hero name &optional target)
  "HERO uses item NAME (on TARGET, a hero, when the item heals —
defaults to the user).  Says why and returns NIL when the hero does
not carry it, the class cannot use it, or it has no use; otherwise
applies the :USE effect — instant :HEAL, or a timed effect through
APPLY-EFFECT-SPEC — spends a :CONSUMED item, emits :ITEM-USED and
returns T."
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
       (say game "~A uses ~A." (hero-name hero) (item-type-title type))
       (if (getf use :heal)
           (heal-hero game (or target hero)
                      (max 0 (roll-dice (getf use :heal))))
           (apply-effect-spec game (item-type-title type) use
                              :image (item-type-image type)))
       (when (item-type-consumed type)
         (drop-item game hero name))
       (emit game :item-used hero name)
       t))))

;;; ---------------------------------------------------------------------
;;; The use interaction model (shared by both front-ends — the
;;; CAST-VIEW pattern: pick the user, the item, and — for a healing
;;; item — the target).

(defstruct (use-view (:constructor %make-use-view))
  hero                ; the chosen user, or NIL while picking
  item                ; the chosen item name, or NIL while picking
  (top 0))            ; scroll offset into the item list

(defun make-use-view ()
  (%make-use-view))

(defun %use-commit (game view target)
  (use-item game (use-view-hero view) (use-view-item view) target)
  :done)

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
       (t                              ; a healing item picks its target
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
(the front-end drops the view), :CANCELLED on Esc at the top level,
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
                  (if (getf (item-type-use (find-item-type name)) :heal)
                      nil               ; a heal picks its target next
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
