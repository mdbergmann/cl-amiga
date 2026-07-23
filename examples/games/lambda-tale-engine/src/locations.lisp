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

(defun location-image (location)
  "LOCATION's picture file name — the :IMAGE arg of the location op —
or NIL.  The Amiga front-end shows it in the view column while the
location's menu takes over the message area.  The street face is the
:FACADE arg, shown before the party steps in — see
FACING-LOCATION-IMAGE-PATH."
  (location-arg location :image))

(defun location-image-path (game)
  "The current location's picture file resolved like an effect icon —
relative to the current map file's directory, so a self-contained
world directory carries its own art — or NIL: no location, or it
names no :IMAGE."
  (let* ((loc (game-location game))
         (image (and loc (location-image loc))))
    (when image
      (%resolve-map-path (dungeon-map-name (game-map game)) image))))

(defun cell-location-op (map x y)
  "The (TITLE KIND ARG...) tail of the LOCATION op attached to cell
\(X,Y), or NIL.  Only a top-level op counts — a location hidden behind
ONCE or a flag test is not knowable map data."
  (dolist (op (cell-special map x y))
    (when (and (consp op) (symbolp (first op))
               (string-equal (symbol-name (first op)) "LOCATION"))
      (return (rest op)))))

(defun facing-location-image-path (game)
  "The facade the party stands before: when the wall straight ahead is
a door and the cell beyond holds a LOCATION with a picture, that
picture's path resolved like LOCATION-IMAGE-PATH — else NIL.  :FACADE
names the street face and wins over :IMAGE (the picture shown inside,
see LOCATION-IMAGE-PATH); a location with only an :IMAGE shows that
from the street too.  The Amiga front-end shows it in the view
column, so a city street reads as houses with faces, not one long
grey wall (the Bard's Tale building-front look).  The wall directly
ahead is visible even in the dark (GAME-VIEW-DEPTH is never less than
one cell)."
  (let* ((map (game-map game))
         (x (game-x game))
         (y (game-y game))
         (f (game-facing game)))
    (when (eq (cell-wall map x y f) :door)
      (multiple-value-bind (nx ny) (neighbor map x y f)
        (when nx
          (let* ((loc (cell-location-op map nx ny))
                 (args (cddr loc))
                 (image (and loc (or (getf args :facade)
                                     (getf args :image)))))
            (when image
              (%resolve-map-path (dungeon-map-name map) image))))))))

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
  (mode :buy)         ; :buy or :sell page
  (top 0))            ; scroll offset into the stock/pack list

(defun make-shop-view ()
  (%make-shop-view))

(defun shop-lines (game view)
  "The current shop menu as a list of menu lines — the front-ends draw
these verbatim (the same pattern as HERO-SUMMARY-LINES); option rows
carry their pick key (see MENU-NUMBERED)."
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
                     (menu-numbered i (format nil "~D) ~A  (~D gp)"
                                              i (hero-name h)
                                              (hero-gold h))))
                   (game-party game)))
         (list "" "[1-7] choose  [Esc] leave")))
       ((eq (shop-view-mode view) :buy)
        (append
         (list (format nil "~A buys.  Gold: ~D gp"
                       (hero-name hero) (hero-gold hero))
               "")
         (menu-scrolled-lines
          (shop-stock loc) (shop-view-top view)
          (lambda (i name)
            (menu-numbered i (format nil "~D) ~A~A  ~D gp"
                                     i (item-title name)
                                     (item-fit-marker hero name)
                                     (item-price name)))))
         (list "" "[1-9] buy  [s] sell  [g] pool gold  [Esc] back")))
       (t
        (append
         (list (format nil "~A sells.  Gold: ~D gp"
                       (hero-name hero) (hero-gold hero))
               "")
         (menu-scrolled-lines
          (hero-items hero) (shop-view-top view)
          (lambda (i name)
            (menu-numbered i (format nil "~D) ~A~:[~;*~]~A  ~D gp"
                                     i (item-title name)
                                     (member name (hero-equipped hero))
                                     (item-fit-marker hero name)
                                     (item-sell-price name)))))
         (list "" "[1-9] sell  [b] buy  [g] pool gold  [Esc] back")))))))

(defun shop-act (game view char)
  "Apply key CHAR to the shop interaction.  Digits pick within the
visible stock/pack window, u/d scroll it (see MENU-WINDOW), g pools
the party's gold onto the shopper (see POOL-GOLD).  Mutates VIEW and
the game state; returns :LEFT when the party leaves the location (the
front-end drops its view then), else NIL."
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
             (shop-view-mode view) :buy
             (shop-view-top view) 0)
       nil)
      ((eq (shop-view-mode view) :buy)
       (cond (digit
              (let ((name (menu-window-pick (shop-stock loc)
                                            (shop-view-top view) digit)))
                (when name
                  (buy-item game hero name)))
              nil)
             ((member char '(#\s #\S))
              (setf (shop-view-mode view) :sell
                    (shop-view-top view) 0)
              nil)
             ((member char '(#\g #\G))
              (pool-gold game hero)
              nil)
             (t
              (let ((top (menu-scroll (shop-view-top view) char
                                      (length (shop-stock loc)))))
                (when top (setf (shop-view-top view) top)))
              nil)))
      (t
       (cond (digit
              (let ((name (menu-window-pick (hero-items hero)
                                            (shop-view-top view) digit)))
                (when name
                  (sell-item game hero name)))
              nil)
             ((member char '(#\b #\B))
              (setf (shop-view-mode view) :buy
                    (shop-view-top view) 0)
              nil)
             ((member char '(#\g #\G))
              (pool-gold game hero)
              nil)
             (t
              (let ((top (menu-scroll (shop-view-top view) char
                                      (length (hero-items hero)))))
                (when top (setf (shop-view-top view) top)))
              nil))))))

;;; ---------------------------------------------------------------------
;;; Tavern mechanics: the singers' watering hole.  A :TAVERN location
;;; sells drinks — a drink refills a singer's tunes (see songs.lisp) —
;;; and may hold the way down: (location TITLE :tavern :price N
;;; :down FILE) travels to FILE through the trapdoor, Bard's Tale
;;; cellar style.

(defun tavern-price (location)
  "The price of a drink at LOCATION (:PRICE, default 3 gold)."
  (or (location-arg location :price) 3))

(defun buy-drink (game hero)
  "HERO buys a round at the current tavern: pays the price, and — for
a singer — the tunes come flooding back (TUNES refilled to the
maximum).  Returns T, or says why not and returns NIL."
  (let ((price (tavern-price (game-location game))))
    (cond ((< (hero-gold hero) price)
           (say game "~A cannot afford a drink." (hero-name hero))
           nil)
          (t
           (decf (hero-gold hero) price)
           (if (hero-singer-p hero)
               (progn
                 (setf (hero-tunes hero) (hero-max-tunes hero))
                 (say game "~A drinks; the tunes come flooding back."
                      (hero-name hero)))
               (say game "~A enjoys a fine ale." (hero-name hero)))
           (emit game :drink hero)
           t))))

(defun tavern-lines (game)
  "The tavern menu as text lines: one numbered row per party member,
the drink price, and the trapdoor when the tavern has one."
  (let ((loc (game-location game)))
    (append
     (list (format nil "*** ~A ***" (location-title loc)) ""
           (format nil "A drink is ~D gold." (tavern-price loc)) "")
     (let ((i 0))
       (mapcar (lambda (h)
                 (incf i)
                 (menu-numbered
                  i (format nil "~D) ~A  (~D gp~@[, Tunes ~A~])"
                            i (hero-name h) (hero-gold h)
                            (when (hero-singer-p h)
                              (format nil "~D/~D" (hero-tunes h)
                                      (hero-max-tunes h))))))
               (game-party game)))
     (list ""
           (format nil "[1-7] drink~@[  [d] down the trapdoor~]  ~
                        [Esc] leave"
                   (location-arg loc :down))))))

(defun tavern-act (game char)
  "Apply key CHAR to the tavern menu: a digit buys that hero a drink,
'd' takes the trapdoor down when the tavern has one, Esc leaves.
Returns :LEFT when the party leaves the location, else NIL."
  (let* ((loc (game-location game))
         (down (location-arg loc :down))
         (digit (digit-char-p char)))
    (cond ((and digit (<= 1 digit (length (game-party game))))
           (let ((h (nth (1- digit) (game-party game))))
             (when (hero-alive-p h)
               (buy-drink game h)))
           nil)
          ((and down (member char '(#\d #\D)))
           (leave-location game)
           (say game "Down through the trapdoor.")
           (travel-party game down)
           :left)
          ((member char '(#\Escape #\l #\L #\q #\Q))
           (leave-location game)
           :left)
          (t nil))))

(defun location-lines (game view)
  "Menu lines for the current location: the shop model for :SHOP, the
tavern menu for :TAVERN, a plain notice for kinds the engine has no
mechanics for."
  (let ((loc (game-location game)))
    (case (location-kind loc)
      (:shop (shop-lines game view))
      (:tavern (tavern-lines game))
      (t (list (format nil "*** ~A ***" (location-title loc)) ""
               "There is nothing to do here."
               "" "[Esc] leave")))))

(defun location-act (game view char)
  "Apply key CHAR inside the current location (see SHOP-ACT and
TAVERN-ACT)."
  (let ((loc (game-location game)))
    (case (location-kind loc)
      (:shop (shop-act game view char))
      (:tavern (tavern-act game char))
      (t (when (member char '(#\Escape #\l #\L #\q #\Q))
           (leave-location game)
           :left)))))
