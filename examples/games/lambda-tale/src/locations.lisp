;;; Lambda's Tale — locations: shops and other enterable places.
;;;
;;; A location is map data: the special op
;;;     (location TITLE KIND ARG...)
;;; attached to a cell (a building's door square in a city, a hut in a
;;; dungeon).  Stepping onto the cell enters the location: the game
;;; gains a modal LOCATION state (like combat), the front-ends switch to
;;; its menu.  KIND is an open set; the engine ships mechanics for
;;; :SHOP (ARG... is :stock (ITEM-NAME...); stock is unlimited, Bard's
;;; Tale style).  Unknown kinds still enter/leave cleanly — campaigns
;;; script them via the :ENTER-LOCATION event.
;;;
;;; The interaction itself is modeled here too, platform-free: a
;;; SHOP-VIEW holds the UI state (which hero is shopping, buy or sell
;;; page), SHOP-LINES renders the menu as text lines and SHOP-ACT maps
;;; a key press to mechanics — both front-ends drive the same model, so
;;; the whole shop flow is testable on the host without a UI.

(in-package :tale)

(defstruct (location (:constructor %make-location))
  title               ; display string, e.g. "Garth's Equipment Shoppe"
  kind                ; keyword: :shop, ...
  args)               ; remaining plist, e.g. (:stock (short-sword ...))

(defun location-arg (location key)
  (getf (location-args location) key))

(defun enter-location (game spec)
  "Enter the location described by SPEC = (TITLE KIND ARG...) — the
LOCATION special op calls this.  Sets the game's modal location state
and emits :ENTER-LOCATION."
  (destructuring-bind (title kind &rest args) spec
    (unless (stringp title)
      (error "location: title ~S must be a string" title))
    (unless (keywordp kind)
      (error "location ~S: kind ~S must be a keyword (:shop, ...)"
             title kind))
    (when (game-location game)
      (error "enter-location: already inside ~A"
             (location-title (game-location game))))
    (let ((loc (%make-location :title title :kind kind :args args)))
      (when (eq kind :shop)
        (dolist (name (location-arg loc :stock))
          (find-item-type name)))   ; catch bad stock at entry, loudly
      (setf (game-location game) loc)
      (say game "The party enters ~A." title)
      (emit game :enter-location loc)
      loc)))

(defun leave-location (game)
  "Leave the current location.  Emits :LEAVE-LOCATION."
  (let ((loc (game-location game)))
    (when loc
      (setf (game-location game) nil)
      (say game "You leave ~A." (location-title loc))
      (emit game :leave-location loc))
    loc))

;;; ---------------------------------------------------------------------
;;; Shop mechanics

(defun item-price (name)
  (item-type-price (find-item-type name)))

(defun item-sell-price (name)
  "Shops buy back at half price."
  (floor (item-price name) 2))

(defun shop-stock (location)
  (location-arg location :stock))

(defun buy-item (game hero name)
  "HERO buys item NAME: checks gold and pack space, then pays and takes
it.  Freshly bought equipment is auto-equipped when the hero has nothing
of that kind equipped and can use it.  Returns T on success."
  (let ((type (find-item-type name)))
    (cond ((< (hero-gold hero) (item-type-price type))
           (say game "~A cannot afford ~A." (hero-name hero)
                (item-type-title type))
           nil)
          ((>= (length (hero-items hero)) +inventory-limit+)
           (say game "~A's pack is full." (hero-name hero))
           nil)
          (t
           (decf (hero-gold hero) (item-type-price type))
           (give-item game hero name)
           (say game "~A buys ~A for ~D gold." (hero-name hero)
                (item-type-title type) (item-type-price type))
           (when (and (member (item-type-kind type)
                              '(:weapon :armor :shield))
                      (not (equipped-of-kind hero (item-type-kind type)))
                      (item-usable-p hero name))
             (equip-item game hero name))
           t))))

(defun sell-item (game hero name)
  "HERO sells item NAME back to the shop at half price.  Returns T, or
says why and returns NIL when the hero does not carry it."
  (if (not (hero-carrying-p hero name))
      (progn
        (say game "~A does not carry ~A." (hero-name hero) (item-title name))
        nil)
      (let ((price (item-sell-price name)))
        (drop-item game hero name)
        (incf (hero-gold hero) price)
        (say game "~A sells ~A for ~D gold." (hero-name hero)
             (item-title name) price)
        t)))

;;; ---------------------------------------------------------------------
;;; The shop interaction model (shared by both front-ends).

(defstruct (shop-view (:constructor %make-shop-view))
  hero                ; the shopping HERO, or NIL while picking one
  (mode :buy))        ; :buy or :sell page

(defun make-shop-view ()
  (%make-shop-view))

(defun shop-lines (game view)
  "The current shop menu as a list of text lines — the front-ends draw
these verbatim (the same pattern as HERO-SUMMARY-LINES)."
  (let* ((loc (game-location game))
         (hero (shop-view-hero view)))
    (append
     (list (format nil "*** ~A ***" (location-title loc)) "")
     (cond
       ((null hero)
        (append
         (list "Who is shopping?" "")
         (let ((i 0))
           (mapcar (lambda (h)
                     (incf i)
                     (format nil "~D) ~A  (~D gp)"
                             i (hero-name h) (hero-gold h)))
                   (game-party game)))
         (list "" "[1-7] choose  [Esc] leave")))
       ((eq (shop-view-mode view) :buy)
        (append
         (list (format nil "~A buys.  Gold: ~D gp"
                       (hero-name hero) (hero-gold hero))
               "")
         (let ((i 0))
           (mapcar (lambda (name)
                     (incf i)
                     (format nil "~D) ~A  ~D gp"
                             i (item-title name) (item-price name)))
                   (shop-stock loc)))
         (list "" "[1-9] buy  [s] sell  [Esc] back")))
       (t
        (append
         (list (format nil "~A sells.  Gold: ~D gp"
                       (hero-name hero) (hero-gold hero))
               "")
         (let ((i 0))
           (mapcar (lambda (name)
                     (incf i)
                     (format nil "~D) ~A~:[~;*~]  ~D gp"
                             i (item-title name)
                             (member name (hero-equipped hero))
                             (item-sell-price name)))
                   (hero-items hero)))
         (list "" "[1-9] sell  [b] buy  [Esc] back")))))))

(defun shop-act (game view char)
  "Apply key CHAR to the shop interaction.  Mutates VIEW and the game
state; returns :LEFT when the party leaves the location (the front-end
drops its view then), else NIL."
  (let ((loc (game-location game))
        (hero (shop-view-hero view))
        (digit (digit-char-p char)))
    (cond
      ((null hero)
       (cond ((and digit (<= 1 digit (length (game-party game))))
              (setf (shop-view-hero view)
                    (nth (1- digit) (game-party game)))
              nil)
             ((member char '(#\Escape #\l #\L #\q #\Q))
              (leave-location game)
              :left)
             (t nil)))
      ((member char '(#\Escape))
       (setf (shop-view-hero view) nil
             (shop-view-mode view) :buy)
       nil)
      ((eq (shop-view-mode view) :buy)
       (cond ((and digit (<= 1 digit (length (shop-stock loc))))
              (buy-item game hero (nth (1- digit) (shop-stock loc)))
              nil)
             ((member char '(#\s #\S))
              (setf (shop-view-mode view) :sell)
              nil)
             (t nil)))
      (t
       (cond ((and digit (<= 1 digit (length (hero-items hero))))
              (sell-item game hero (nth (1- digit) (hero-items hero)))
              nil)
             ((member char '(#\b #\B))
              (setf (shop-view-mode view) :buy)
              nil)
             (t nil))))))

(defun location-lines (game view)
  "Menu lines for the current location: the shop model for :SHOP,
a plain notice for kinds the engine has no mechanics for."
  (let ((loc (game-location game)))
    (if (eq (location-kind loc) :shop)
        (shop-lines game view)
        (list (format nil "*** ~A ***" (location-title loc)) ""
              "There is nothing to do here."
              "" "[Esc] leave"))))

(defun location-act (game view char)
  "Apply key CHAR inside the current location (see SHOP-ACT)."
  (let ((loc (game-location game)))
    (if (eq (location-kind loc) :shop)
        (shop-act game view char)
        (when (member char '(#\Escape #\l #\L #\q #\Q))
          (leave-location game)
          :left))))
