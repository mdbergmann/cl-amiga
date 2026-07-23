;;; Lambda's Tale — races.
;;;
;;; Like hero classes, spells and songs, races are campaign data, not
;;; engine facts: the campaign registers them with DEFINE-RACE in its
;;; campaign.lisp; the engine only knows what a race IS.  A race carries
;;; ability-score modifiers that MAKE-HERO adds to the freshly rolled
;;; 3d6 abilities, and a list of the hero classes the race may take
;;; (MAKE-HERO rejects an illegal race/class pairing).  See the Closure
;;; game's worlds/closure/campaign.lisp for a worked example.

(in-package :tale)

(defstruct (race (:constructor %make-race))
  name                ; keyword, e.g. :DWARF
  (str 0) (dex 0) (iq 0) (con 0) (lck 0)  ; ability-score modifiers
  classes             ; hero classes the race may take; NIL = any class
  description         ; one-line flavour string, or NIL
  image)              ; portrait file (map-relative, like a class), or NIL

(defvar *races* (make-hash-table :test 'eq))
(defvar *race-names* '()
  "Race names in registration order — the stable order of the menus.")

(defun define-race (name &key (str 0) (dex 0) (iq 0) (con 0) (lck 0)
                              classes description image)
  "Register race NAME (a keyword) with its ability-score modifiers
\(STR DEX IQ CON LCK, added to the 3d6 rolls in MAKE-HERO), the hero
CLASSES it may take (a list of class keywords; NIL = any registered
class), a one-line DESCRIPTION and an optional portrait IMAGE.  A
re-registration keeps the race's spot in the menu order.  Campaign
data calls this."
  (setf (gethash name *races*)
        (%make-race :name name :str str :dex dex :iq iq :con con :lck lck
                    :classes classes :description description :image image))
  (unless (member name *race-names*)
    (setf *race-names* (append *race-names* (list name))))
  name)

(defun find-race (name)
  (or (gethash name *races*)
      (error "Unknown race ~S (register it with DEFINE-RACE)" name)))

(defun races ()
  "The registered races, in registration order."
  (copy-list *race-names*))

(defun race-title (name)
  "Race NAME as a display string, hyphenation kept: :HALF-ELF ->
\"Half-Elf\", :DWARF -> \"Dwarf\"."
  (string-capitalize (symbol-name name)))

(defun race-allows-class-p (name class)
  "True when race NAME may take hero CLASS — the race lists it, or the
race restricts nothing (a NIL :classes means any class)."
  (let ((race (find-race name)))
    (or (null (race-classes race))
        (and (member class (race-classes race)) t))))

(defconstant +stat-min+ 1
  "Floor an ability score cannot drop below after a racial modifier.")
(defconstant +stat-max+ 18
  "Ceiling a starting ability score cannot rise above after a racial
modifier (3d6 tops out at 18; gameplay may lift it further later).")

(defun clamp-stat (n)
  "Keep ability score N in the playable +STAT-MIN+..+STAT-MAX+ range
after a racial modifier."
  (max +stat-min+ (min +stat-max+ n)))
