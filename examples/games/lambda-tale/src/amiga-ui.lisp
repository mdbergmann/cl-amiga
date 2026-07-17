;;; Lambda's Tale — AmigaOS front-end (Intuition window or custom screen,
;;; wireframe view).  Layout per specs/ui-and-engine.md:
;;;
;;;   +--------------------+------+------------------+
;;;   | first-person view  |spells| message log      |
;;;   |                    |shield| (newest at the   |
;;;   |                    |lamp  |  bottom, older   |
;;;   +--------------------+ ...  |  scrolling up)   |
;;;   | status line        |      |                  |
;;;   | party roster (7 rows)                        |
;;;   +----------------------------------------------+
;;;
;;; The automap lives in a full-screen map mode under the 'm' key.
;;;
;;; Loaded only on AmigaOS (see src/load.lisp); the requires below must run
;;; before the rest of this file is read so AMIGA.INTUITION / AMIGA.GFX /
;;; AMIGA.GADTOOLS symbols resolve.
;;;
;;; Pens: 0 background, 1 wireframe/text, 3 doors and the party marker.
;;;
;;; Save/Load/Quit live in an Intuition menu strip (right mouse button),
;;; built with gadtools.library — the Amiga-native place for them.

(require "amiga/intuition")
(require "amiga/graphics")
(require "amiga/gadtools")

(in-package :tale)

(defparameter *save-file* "tale.sav")

(defvar *autoplay* nil
  "Testing hook: a list of key characters fed to the game one per
Intuition tick (~10/s), driving a full unattended session.  :ESC quits.")

;;; Window mode uses the same PAL-screen geometry as :display :screen,
;;; so the window fits (and fills) a 640x256 PAL Workbench and the two
;;; displays lay out identically.  Opened at 0,0 — a PAL screen has no
;;; room to spare for an offset.  The first-person viewport size lives
;;; in view.lisp (*FP-VIEW-WIDTH* x *FP-VIEW-HEIGHT*) — the wall-piece
;;; assets are drawn for exactly those planes.
(defparameter *amiga-win-width* 640)
(defparameter *amiga-win-height* 256)

;;; :display :screen geometry — nominal PAL hires; BEST-MODE-ID promotes
;;; it to a suitable RTG mode on Picasso96/CyberGraphX/MorphOS.
(defparameter *amiga-screen-width* 640)
(defparameter *amiga-screen-height* 256)
(defparameter *amiga-screen-depth* 2)

(defparameter *amiga-spells-width* 64
  "Width of the active-spells strip between the view and the text column.")

(defconstant +game-idcmp+
  (logior amiga.intuition:+idcmp-closewindow+
          amiga.intuition:+idcmp-vanillakey+
          amiga.intuition:+idcmp-menupick+
          ;; the *autoplay* heartbeat
          amiga.intuition:+idcmp-intuiticks+))

;;; ---------------------------------------------------------------------
;;; Layout: computed from the window's actual inner size, so the same
;;; code serves the Workbench window, a 640x256 custom screen and
;;; whatever an RTG driver promotes the mode to.

(defstruct (ui-layout (:constructor %make-ui-layout))
  bx by            ; inner top-left
  right bottom     ; inner right/bottom edges
  lh base          ; text line height / baseline (rastport font metrics)
  fp-w fp-h        ; first-person view size
  spells-x spells-w ; active-spells strip
  log-x log-w      ; message log column
  col-h            ; height of the strip and the log column
  status-y         ; status pane top
  party-y)         ; party roster pane top

(defun %amiga-layout (win rp)
  (let* ((bx (+ (amiga.intuition:window-border-left win) 6))
         (by (+ (amiga.intuition:window-border-top win) 6))
         (right (- (amiga.intuition:window-width win)
                   (amiga.intuition:window-border-right win) 6))
         (bottom (- (amiga.intuition:window-height win)
                    (amiga.intuition:window-border-bottom win) 6))
         (lh (+ (amiga.gfx:rastport-tx-height rp) 2))
         (base (amiga.gfx:rastport-tx-baseline rp))
         (party-y (- bottom (* lh +party-limit+)))
         (status-y (- party-y lh 4))
         (content-bottom (- status-y 6))
         (fp-h (min *fp-view-height* (- content-bottom by)))
         (spells-x (+ bx *fp-view-width* 12))
         (log-x (+ spells-x *amiga-spells-width* 12)))
    (%make-ui-layout :bx bx :by by :right right :bottom bottom
                     :lh lh :base base
                     :fp-w *fp-view-width* :fp-h fp-h
                     :spells-x spells-x :spells-w *amiga-spells-width*
                     :log-x log-x :log-w (- right log-x)
                     :col-h (- content-bottom by)
                     :status-y status-y :party-y party-y)))

;;; ---------------------------------------------------------------------
;;; Drawing

(defun %amiga-door-rect (rp cx cy hw hh)
  (amiga.gfx:set-a-pen rp 3)
  (amiga.gfx:draw-line rp (- cx hw) (- cy hh) (+ cx hw) (- cy hh))
  (amiga.gfx:draw-line rp (- cx hw) (+ cy hh) (+ cx hw) (+ cy hh))
  (amiga.gfx:draw-line rp (- cx hw) (- cy hh) (- cx hw) (+ cy hh))
  (amiga.gfx:draw-line rp (+ cx hw) (- cy hh) (+ cx hw) (+ cy hh))
  (amiga.gfx:set-a-pen rp 1))

(defun %amiga-draw-fp (rp game ox oy w h &optional walls)
  "Draw the first-person view into the rastport at (OX,OY): blitted
wall graphics when WALLS (the loaded piece bitmaps) is available and
the viewport has its full asset size, the wireframe otherwise."
  (amiga.gfx:set-a-pen rp 0)
  (amiga.gfx:rect-fill rp ox oy (+ ox w -1) (+ oy h -1))
  (amiga.gfx:set-a-pen rp 1)
  (let ((slices (compute-view (game-map game) (game-x game) (game-y game)
                              (game-facing game)))
        (planes (view-planes w h)))
    (if (and walls (= w *fp-view-width*) (= h *fp-view-height*))
        (dolist (rec (view-blit-list slices planes))
          (destructuring-bind (piece x y pw ph) rec
            (let ((bm (gethash piece walls)))
              (when bm
                (amiga.gfx:blt-bitmap-rastport bm 0 0 rp
                                               (+ ox x) (+ oy y) pw ph)))))
        (dolist (prim (view-display-list slices planes))
          (ecase (first prim)
            (:line (destructuring-bind (x0 y0 x1 y1) (rest prim)
                     (amiga.gfx:draw-line rp (+ ox x0) (+ oy y0)
                                          (+ ox x1) (+ oy y1))))
            (:door (destructuring-bind (cx cy hw hh) (rest prim)
                     (%amiga-door-rect rp (+ ox cx) (+ oy cy) hw hh))))))))

;;; ---------------------------------------------------------------------
;;; Wall-piece assets (M3): the data/gfx ILBMs loaded into offscreen
;;; bitmaps once per session.  RTG-safe: the window's own bitmap is the
;;; AllocBitMap friend and sets the depth, so the pieces live in the
;;; display's native format and every blit copies all its planes;
;;; pixels go in as chunky bytes (WRITE-CHUNKY), never as planes.

(defparameter *gfx-dir* "data/gfx/"
  "Where the wall-piece ILBMs live, relative to the game directory.")

(defun %window-bitmap (rp)
  "The BitMap a window rastport renders into (rp_BitMap)."
  (ffi:make-foreign-pointer (ffi:peek-u32 rp 4)))

(defun %load-wall-assets (rp log)
  "Load every wall piece from *GFX-DIR* into an offscreen bitmap;
returns a hash of piece key -> bitmap, or NIL (falling back to the
wireframe view) when the assets are missing or unreadable."
  (let ((walls (make-hash-table :test #'equal))
        (friend (%window-bitmap rp))
        (depth (max 2 (amiga.gfx:get-bitmap-attr (%window-bitmap rp)
                                                 amiga.gfx:+bma-depth+))))
    (handler-case
        (dolist (piece (wall-piece-names) walls)
          (let ((file (concatenate 'string *gfx-dir* (wall-piece-file piece))))
            (unless (probe-file file)
              (error "missing wall asset ~A" file))
            (let* ((img (read-ilbm file))
                   (bm (amiga.gfx:alloc-bitmap (image-width img)
                                               (image-height img)
                                               depth
                                               :friend friend)))
              (setf (gethash piece walls) bm)
              (amiga.gfx:with-bitmap-rastport (brp bm)
                (amiga.gfx:write-chunky brp 0 0
                                        (image-width img)
                                        (image-height img)
                                        (image-pixels img))))))
      (error (e)
        (%free-wall-assets walls)
        (when log
          (log-message log (format nil "No wall graphics (~A); wireframe view."
                                   e)))
        nil))))

;;; The layout is designed around topaz 8 (10px line height): the fixed
;;; pixel columns in the party pane, the glyph budgets in the strips
;;; and the full 240x130 asset viewport all assume it.  RTG Workbenches
;;; often default to a bigger system font, which would blow the layout
;;; up and shrink the viewport below the wall assets' size — so the
;;; game selects topaz 8 (a ROM font) on its rastport explicitly.

(defun %with-game-font (fn)
  "Call FN with the topaz 8 TextFont (NIL when unavailable — the
layout then uses whatever the rastport carries); closes the font after
FN returns."
  (let ((font (amiga.gfx:open-font "topaz.font" 8)))
    (unwind-protect
        (funcall fn font)
      (when font (amiga.gfx:close-font font)))))

(defun %game-rastport (win font)
  "The window's rastport with the game font selected."
  (let ((rp (amiga.intuition:window-rastport win)))
    (when font (amiga.gfx:set-font rp font))
    rp))

(defun %free-wall-assets (walls)
  "Free the piece bitmaps; safe to call with NIL."
  (when walls
    (maphash (lambda (piece bm)
               (declare (ignore piece))
               (amiga.gfx:free-bitmap bm))
             walls))
  nil)

(defun %amiga-draw-map-region (rp game ox oy cell x0 y0 vw vh full)
  "Draw automap cells [X0,X0+VW) x [Y0,Y0+VH) at (OX,OY), CELL pixels
per cell.  FULL non-NIL draws everything (debug); otherwise only what
the party's knowledge holds.  Used for both the minimap viewport and
the full map mode."
  (amiga.gfx:set-a-pen rp 0)
  (amiga.gfx:rect-fill rp ox oy (+ ox (* cell vw)) (+ oy (* cell vh)))
  (amiga.gfx:set-a-pen rp 1)
  (let ((map (game-map game))
        (k (game-knowledge game)))
    (dotimes (ry vh)
      (dotimes (rx vw)
        (let* ((x (+ rx x0))
               (y (+ ry y0))
               (px (+ ox (* rx cell)))
               (py (+ oy (* ry cell))))
          ;; walls
          (dotimes (d 4)
            (let ((wall (cell-wall map x y d)))
              (when (and (not (eq wall :open))
                         (or full (wall-known-p k x y d)))
                (amiga.gfx:set-a-pen rp (if (eq wall :door) 3 1))
                (ecase d
                  (0 (amiga.gfx:draw-line rp px py (+ px cell) py))
                  (2 (amiga.gfx:draw-line rp px (+ py cell)
                                          (+ px cell) (+ py cell)))
                  (3 (amiga.gfx:draw-line rp px py px (+ py cell)))
                  (1 (amiga.gfx:draw-line rp (+ px cell) py
                                          (+ px cell) (+ py cell)))))))
          ;; feature glyph (needs room for a character)
          (when (>= cell 8)
            (let ((f (cell-feature map x y)))
              (when (and f (or full (cell-explored-p k x y)))
                (amiga.gfx:set-a-pen rp 1)
                (amiga.gfx:move-to rp (+ px 2) (+ py cell -2))
                (amiga.gfx:gfx-text rp (string f))))))))
    ;; party marker: filled block + facing tick, when inside the region
    (let ((gx (game-x game))
          (gy (game-y game)))
      (when (and (<= x0 gx) (< gx (+ x0 vw))
                 (<= y0 gy) (< gy (+ y0 vh)))
        (let* ((px (+ ox (* (- gx x0) cell)))
               (py (+ oy (* (- gy y0) cell)))
               (cx (+ px (floor cell 2)))
               (cy (+ py (floor cell 2)))
               (r (max 1 (floor cell 4)))
               (f (game-facing game)))
          (amiga.gfx:set-a-pen rp 3)
          (amiga.gfx:rect-fill rp (- cx r) (- cy r) (+ cx r) (+ cy r))
          (amiga.gfx:draw-line rp cx cy
                               (+ cx (* (aref *dir-dx* f)
                                        (- (floor cell 2) 1)))
                               (+ cy (* (aref *dir-dy* f)
                                        (- (floor cell 2) 1))))
          (amiga.gfx:set-a-pen rp 1))))))

(defparameter *amiga-compass-height* 56
  "Vertical space at the foot of the middle column reserved for the
compass rose.")

(defun %amiga-draw-effects (rp game l)
  "The active-spells strip between the view and the text column:
one line per active effect (shield, lamp, ...).  The bottom
*AMIGA-COMPASS-HEIGHT* pixels belong to the compass rose."
  (let* ((ox (ui-layout-spells-x l))
         (oy (ui-layout-by l))
         (w (ui-layout-spells-w l))
         (h (max 0 (- (ui-layout-col-h l) *amiga-compass-height*)))
         (lh (ui-layout-lh l))
         (max-chars (max 4 (floor w 8))))
    (amiga.gfx:set-a-pen rp 0)
    (amiga.gfx:rect-fill rp ox oy (+ ox w -1) (+ oy h -1))
    (amiga.gfx:set-a-pen rp 1)
    (let ((y (+ oy (ui-layout-base l))))
      (dolist (e (game-effects game))
        (when (< y (+ oy h))
          (let ((text (string-downcase (princ-to-string e))))
            (amiga.gfx:move-to rp ox y)
            (amiga.gfx:gfx-text rp (if (> (length text) max-chars)
                                       (subseq text 0 max-chars)
                                       text))))
        (incf y lh)))))

(defun %amiga-draw-compass (rp game l)
  "Compass rose at the foot of the middle column: the four cardinal
letters around a diamond, the needle pointing at the party's facing."
  (let* ((ox (ui-layout-spells-x l))
         (w (ui-layout-spells-w l))
         (bottom (+ (ui-layout-by l) (ui-layout-col-h l)))
         (h (min *amiga-compass-height* (ui-layout-col-h l)))
         (cx (+ ox (floor w 2)))
         (cy (- bottom (floor h 2)))
         (r (max 6 (min (floor (- w 20) 2) (- (floor h 2) 6)))))
    (amiga.gfx:set-a-pen rp 0)
    (amiga.gfx:rect-fill rp ox (- bottom h) (+ ox w -1) (- bottom 1))
    (destructuring-bind (needle letters)
        (compass-points (game-facing game) cx cy r)
      ;; the rose: a diamond through the needle's reach
      (let ((ri (max 2 (- r 8))))
        (amiga.gfx:set-a-pen rp 1)
        (amiga.gfx:draw-line rp cx (- cy ri) (+ cx ri) cy)
        (amiga.gfx:draw-line rp (+ cx ri) cy cx (+ cy ri))
        (amiga.gfx:draw-line rp cx (+ cy ri) (- cx ri) cy)
        (amiga.gfx:draw-line rp (- cx ri) cy cx (- cy ri)))
      (destructuring-bind (x0 y0 x1 y1) needle
        (amiga.gfx:set-a-pen rp 3)
        (amiga.gfx:draw-line rp x0 y0 x1 y1))
      (dolist (p letters)
        (destructuring-bind (ch x y facing-p) p
          (amiga.gfx:set-a-pen rp (if facing-p 3 1))
          (amiga.gfx:move-to rp (- x 4) (+ y 3))
          (amiga.gfx:gfx-text rp (string ch)))))
    (amiga.gfx:set-a-pen rp 1)))

(defun %amiga-draw-log (rp log l)
  "Message log column: trailing lines, newest at the bottom (spec: the
Bard's Tale text column)."
  (let* ((ox (ui-layout-log-x l))
         (oy (ui-layout-by l))
         (w (ui-layout-log-w l))
         (h (ui-layout-col-h l))
         (lh (ui-layout-lh l))
         (n (max 1 (floor h lh)))
         (max-chars (max 4 (floor w 8)))
         ;; Each message starts with "> "; long ones wrap onto indented
         ;; continuation lines.  Keep the trailing N display lines so
         ;; the newest stays at the bottom.
         (wrapped (mapcan (lambda (m) (wrap-message m max-chars))
                          (log-recent log n)))
         (lines (last wrapped n)))
    (amiga.gfx:set-a-pen rp 0)
    (amiga.gfx:rect-fill rp ox oy (+ ox w -1) (+ oy h -1))
    (amiga.gfx:set-a-pen rp 1)
    (let ((y (+ oy (- h (* (length lines) lh)) (ui-layout-base l))))
      (dolist (m lines)
        (amiga.gfx:move-to rp ox y)
        (amiga.gfx:gfx-text rp m)
        (incf y lh)))))

(defun %amiga-status (rp game l text)
  "Status pane: position/facing plus contextual key help."
  (let ((ox (ui-layout-bx l))
        (oy (ui-layout-status-y l)))
    (amiga.gfx:set-a-pen rp 0)
    (amiga.gfx:rect-fill rp ox oy
                         (ui-layout-right l) (+ oy (ui-layout-lh l) -1))
    (amiga.gfx:set-a-pen rp 1)
    (amiga.gfx:move-to rp ox (+ oy (ui-layout-base l)))
    (amiga.gfx:gfx-text rp (format nil "(~D,~D) ~A  ~A"
                                   (game-x game) (game-y game)
                                   (dir-keyword (game-facing game))
                                   text))))

(defun %amiga-party (rp game l)
  "Party roster: one line per hero at fixed pixel columns; the pane
reserves +PARTY-LIMIT+ (7) rows."
  (let* ((ox (ui-layout-bx l))
         (oy (ui-layout-party-y l))
         (lh (ui-layout-lh l)))
    (amiga.gfx:set-a-pen rp 0)
    (amiga.gfx:rect-fill rp ox oy
                         (ui-layout-right l)
                         (+ oy (* lh +party-limit+) -1))
    (amiga.gfx:set-a-pen rp 1)
    (let ((y (+ oy (ui-layout-base l))))
      (dolist (h (game-party game))
        (labels ((col (x text)
                   (amiga.gfx:move-to rp (+ ox x) y)
                   (amiga.gfx:gfx-text rp text)))
          (col 0   (hero-name h))
          (col 130 (format nil "Lv~D" (hero-level h)))
          (col 185 (format nil "HP ~D/~D" (hero-hp h) (hero-max-hp h)))
          (col 290 (format nil "~D gp" (hero-gold h)))
          (unless (hero-alive-p h)
            (col 360 "(down)")))
        (incf y lh)))))

(defun %amiga-draw-map-page (rp game l full)
  "Full map mode ('m'): the automap over the whole inner area, party
centered and clamped to what fits at a readable cell size."
  (let* ((bx (ui-layout-bx l))
         (by (ui-layout-by l))
         (map (game-map game))
         (mw (dungeon-map-width map))
         (mh (dungeon-map-height map))
         (avail-w (- (ui-layout-right l) bx))
         (avail-h (- (ui-layout-bottom l) by (ui-layout-lh l) 4))
         (cell (max 4 (min 16
                           (floor avail-w (max mw 1))
                           (floor avail-h (max mh 1)))))
         (vw (min mw (floor avail-w cell)))
         (vh (min mh (floor avail-h cell))))
    ;; clear the whole inner area (the play panes underneath)
    (amiga.gfx:set-a-pen rp 0)
    (amiga.gfx:rect-fill rp bx by (ui-layout-right l) (ui-layout-bottom l))
    (multiple-value-bind (x0 y0 w h)
        (map-viewport map (game-x game) (game-y game) vw vh)
      (%amiga-draw-map-region rp game bx by cell x0 y0 w h full)
      (amiga.gfx:set-a-pen rp 1)
      (amiga.gfx:move-to rp bx (- (ui-layout-bottom l)
                                  (- (ui-layout-lh l)
                                     (ui-layout-base l))))
      (amiga.gfx:gfx-text
       rp (format nil "Map ~D,~D..~D,~D of ~Dx~D~@[ FULL~]  ~
                       M/Esc back  F full  Q quit"
                  x0 y0 (+ x0 w -1) (+ y0 h -1) mw mh full)))))

;;; The Game menu.  Item numbers (the bar counts as an item) are decoded
;;; from the MENUPICK code below: Save 0, Load 1, Quit 3.
(defparameter *menu-entries*
  (list (list amiga.gadtools:+nm-title+ "Game")
        (list amiga.gadtools:+nm-item+ "Save" :commkey "S")
        (list amiga.gadtools:+nm-item+ "Load" :commkey "L")
        :bar
        (list amiga.gadtools:+nm-item+ "Quit" :commkey "Q")))

(defconstant +menu-null+ #xFFFF)

(defun %menu-item-number (code)
  "The item number packed into a MENUPICK code (FULLMENUNUM layout:
menu bits 0-4, item bits 5-10, sub-item bits 11-15)."
  (logand (ash code -5) #x3F))

;;; ---------------------------------------------------------------------
;;; Display: Workbench window or own custom screen

(defun %game-screen-palette (scr)
  "Dungeon palette for the custom screen: black background, white
wireframe, grey spare, amber doors/party marker."
  (let ((vp (amiga.intuition:screen-viewport scr)))
    (amiga.gfx:set-rgb4 vp 0 0 0 0)
    (amiga.gfx:set-rgb4 vp 1 15 15 15)
    (amiga.gfx:set-rgb4 vp 2 8 8 8)
    (amiga.gfx:set-rgb4 vp 3 15 10 3)))

(defun %call-with-game-window (display fn)
  "Open DISPLAY and call FN with the screen and window.
:WINDOW — a window on the public (Workbench) screen, the development
default.  :SCREEN — an own custom screen (nominal PAL 640x256 hires,
chosen RTG-aware through AMIGA.GFX:BEST-MODE-ID) covered by a
borderless backdrop window."
  (ecase display
    (:window
     (amiga.intuition:with-pub-screen (scr)
       (amiga.intuition:with-window
           (win :title "Lambda's Tale"
                :left 0 :top 0
                :width *amiga-win-width* :height *amiga-win-height*
                :idcmp +game-idcmp+)
         (funcall fn scr win))))
    (:screen
     (amiga.intuition:with-screen
         (scr :width *amiga-screen-width*
              :height *amiga-screen-height*
              :depth *amiga-screen-depth*
              :title "Lambda's Tale"
              :mode-id (amiga.gfx:best-mode-id
                        :width *amiga-screen-width*
                        :height *amiga-screen-height*
                        :depth *amiga-screen-depth*))
       (%game-screen-palette scr)
       ;; no window title: on a backdrop window WA_Title still costs a
       ;; title bar (border-top), and the screen already carries one
       (amiga.intuition:with-window
           (win :left 0 :top 0
                :width (amiga.intuition:screen-width scr)
                :height (amiga.intuition:screen-height scr)
                :screen scr
                :flags (logior amiga.intuition:+wflg-borderless+
                               amiga.intuition:+wflg-backdrop+
                               amiga.intuition:+wflg-activate+)
                :idcmp +game-idcmp+)
         (funcall fn scr win))))))

;;; ---------------------------------------------------------------------
;;; The game proper

(defun play-amiga (&optional (map-file "data/cellar.map")
                   &key (display :window))
  "Interactive walkabout.  Loads data/campaign.lisp (classes, monsters,
party) when present.  DISPLAY is :window (on the Workbench screen) or
:screen (own PAL-ish custom screen, RTG-aware).
Keys: W forward, S back-step, A/D turn, M map mode (M/Esc leaves it,
F toggles the debug full view there), Q/Esc quit; in combat A attack,
D defend, F flee.  Save/Load/Quit sit in the menu strip (right mouse
button)."
  (when (probe-file "data/campaign.lisp")
    (load "data/campaign.lisp"))
  (let* ((map (load-map-file map-file))
         (game nil)
         (log nil)
         (mode :play)       ; :play or :map (the full map view)
         (full nil)         ; omniscient map (debug), map mode only
         (over nil))
    (labels ((wire (g)
               (setf log (attach-message-log g))
               (on-event g :game-won
                         (lambda (gm) (declare (ignore gm))
                           (setf over :won)))
               (on-event g :party-defeated
                         (lambda (gm) (declare (ignore gm))
                           (setf over :lost)))
               g))
      (setf game (wire (new-game map
                                 :party (when (fboundp 'default-party)
                                          (funcall 'default-party)))))
      (trigger-special game)
      (%with-game-font
       (lambda (font)
         (%call-with-game-window
          display
          (lambda (scr win)
            (amiga.gadtools:with-visual-info (vi scr)
              (amiga.gadtools:with-menus (menu *menu-entries* vi win)
                (let* ((rp (%game-rastport win font))
                       (l (%amiga-layout win rp))
                       (walls (%load-wall-assets rp log)))
               (labels ((status-text ()
                          (cond ((eq over :won) "You win!  Press Q.")
                                ((eq over :lost) "Game over.  Press Q.")
                                ((game-combat game)
                                 "COMBAT!  A attack  D defend  F flee")
                                (t "W/S move  A/D turn  M map")))
                        (clear-inner ()
                          ;; The play page only repaints its own panes;
                          ;; wipe the whole inner area when the full-map
                          ;; page was underneath.
                          (amiga.gfx:set-a-pen rp 0)
                          (amiga.gfx:rect-fill rp
                                               (ui-layout-bx l)
                                               (ui-layout-by l)
                                               (ui-layout-right l)
                                               (ui-layout-bottom l)))
                        (leave-map ()
                          (setf mode :play)
                          (clear-inner)
                          (redraw))
                        (redraw ()
                          (if (eq mode :map)
                              (%amiga-draw-map-page rp game l full)
                              (progn
                                (%amiga-draw-fp rp game
                                                (ui-layout-bx l)
                                                (ui-layout-by l)
                                                (ui-layout-fp-w l)
                                                (ui-layout-fp-h l)
                                                walls)
                                (%amiga-draw-effects rp game l)
                                (%amiga-draw-compass rp game l)
                                (%amiga-draw-log rp log l)
                                (%amiga-status rp game l (status-text))
                                (%amiga-party rp game l))))
                        (%step (relative)
                          ;; Log the notable step results; plain steps
                          ;; stay quiet so the log tracks events, not
                          ;; every footfall.
                          (case (move-party game relative)
                            (:door (say game "You pass through a door."))
                            (:blocked (say game "You bump into a wall."))))
                        (do-save ()
                          (if (game-combat game)
                              (log-message log "No saving during combat.")
                              (progn
                                (save-game game *save-file*)
                                (log-message log "Game saved."))))
                        (do-load ()
                          (if (probe-file *save-file*)
                              (progn
                                (setf game (wire (load-game *save-file*)))
                                (setf over nil
                                      mode :play)
                                ;; loading may leave map mode (menu item)
                                (clear-inner)
                                (log-message log "Game loaded."))
                              (log-message log "No saved game found.")))
                        (act (c)
                          "Handle key C; :quit means leave the event loop."
                          (let ((lc (if (characterp c) (char-downcase c) c)))
                            (cond ((eq mode :map)
                                   (cond ((eql lc #\q) :quit)
                                         ((or (eql lc #\m) (eql c :esc))
                                          (leave-map)
                                          nil)
                                         ((eql lc #\f)
                                          (setf full (not full))
                                          (redraw)
                                          nil)))
                                  ((or (eql lc #\q) (eql c :esc)) :quit)
                                  (over nil) ; game ended: only Q/Esc react
                                  ((game-combat game)
                                   (case lc
                                     (#\a (combat-round game) (redraw))
                                     (#\d (combat-round game
                                                        (mapcar
                                                         (lambda (h)
                                                           (declare (ignore h))
                                                           :defend)
                                                         (alive-heroes game)))
                                          (redraw))
                                     (#\f (attempt-flee game) (redraw)))
                                   nil)
                                  ((eql c #\S) (do-save) (redraw) nil)
                                  ((eql c #\L) (do-load) (redraw) nil)
                                  (t
                                   (case lc
                                     (#\w (%step :forward) (redraw))
                                     (#\s (%step :back) (redraw))
                                     (#\a (turn-left game) (redraw))
                                     (#\d (turn-right game) (redraw))
                                     (#\m (setf mode :map)
                                          (redraw)))
                                   nil)))))
                 (unwind-protect
                     (progn
                       (redraw)
                       (amiga.intuition:event-loop win
                         (amiga.intuition:+idcmp-closewindow+ (msg)
                           (return))
                         (amiga.intuition:+idcmp-menupick+ (msg)
                           (let ((code (amiga.intuition:msg-code msg)))
                             (unless (= code +menu-null+)
                               (case (%menu-item-number code)
                                 (0 (do-save) (redraw))
                                 (1 (do-load) (redraw))
                                 (3 (return))))))
                         (amiga.intuition:+idcmp-vanillakey+ (msg)
                           (let* ((code (amiga.intuition:msg-code msg))
                                  (c (if (= code 27) :esc (code-char code))))
                             (when (eq (act c) :quit)
                               (return))))
                         (amiga.intuition:+idcmp-intuiticks+ (msg)
                           (when *autoplay*
                             (when (eq (act (pop *autoplay*)) :quit)
                               (return))))))
                   (setf walls (%free-wall-assets walls))))))))))))))
