;;; Lambda's Tale — items, inventory and equipment.
;;;
;;; Item types are campaign data, not engine facts: the campaign
;;; registers them with DEFINE-ITEM (see worlds/closure/campaign.lisp) and maps
;;; refer to them by name in shop stock lists.  The engine only knows
;;; the mechanics: a hero carries up to +INVENTORY-LIMIT+ items and can
;;; equip one weapon, one armor and one shield at a time.  Armor class
;;; is descending, so an item's :AC bonus *lowers* the effective AC.

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
  classes)            ; hero classes allowed to use it; NIL = anyone

(defvar *item-types* (make-hash-table :test 'eq))

(defun define-item (name &key title (kind :misc) (price 0) damage (ac 0)
                              classes)
  "Register item type NAME (a symbol).  Campaign data calls this.
TITLE defaults to the capitalized name (SHORT-SWORD -> \"Short Sword\")."
  (unless (member kind '(:weapon :armor :shield :misc))
    (error "define-item ~S: kind ~S is not one of :weapon :armor :shield :misc"
           name kind))
  (setf (gethash name *item-types*)
        (%make-item-type
         :name name
         :title (or title
                    (string-capitalize (substitute #\Space #\- (string name))))
         :kind kind :price price :damage damage :ac ac :classes classes))
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

(defun hero-attack-dice (hero)
  "The dice HERO attacks with: the equipped weapon's damage, else the
hero's bare (class) damage."
  (let ((weapon (equipped-of-kind hero :weapon)))
    (if weapon
        (or (item-type-damage (find-item-type weapon)) (hero-damage hero))
        (hero-damage hero))))

(defun hero-effective-ac (hero)
  "HERO's armor class with equipment: descending AC minus the AC bonus
of every equipped item."
  (let ((ac (hero-ac hero)))
    (dolist (name (hero-equipped hero) ac)
      (decf ac (item-type-ac (find-item-type name))))))
