;;; Lambda's Tale — AmigaOS front-end (Intuition window or custom screen,
;;; wireframe view).  Layout per specs/ui-and-engine.md, the Bard's
;;; Tale two-column split, sized by the active display profile:
;;;
;;;   +--------------------+----------------------+
;;;   | first-person view  | message log          |
;;;   |                    | (newest at the       |
;;;   |                    |  bottom, older       |
;;;   +--------------------+  scrolling up)       |
;;;   | location plaque    +----------------------+
;;;   |                    | effects     | compass|
;;;   +--------------------+----------------------+
;;;   | status line                               |
;;;   | party roster (7 rows)                     |
;;;   +-------------------------------------------+
;;;
;;; The automap lives in a full-screen map mode under the 'm' key.
;;;
;;; Loaded only on AmigaOS (see src/load.lisp); the requires below must run
;;; before the rest of this file is read so AMIGA.INTUITION / AMIGA.GFX /
;;; AMIGA.GADTOOLS symbols resolve.
;;;
;;; Pens: 0 background, 1 wireframe/text, 3 doors and the party marker;
;;; on the custom screen pens 4 up to the profile's depth carry the
;;; tile pack's colors (see %APPLY-PACK-PALETTE).
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

;;; Screen and window geometry, viewport size and tile pack all come
;;; from the active DISPLAY-PROFILE (src/profiles.lisp) — PLAY-AMIGA's
;;; :PROFILE argument selects one.  Window mode uses the profile's
;;; window size at 0,0 so both displays lay out identically;
;;; BEST-MODE-ID promotes the screen mode on Picasso96/CyberGraphX/
;;; MorphOS RTG systems.

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
  bx by            ; content top-left (inside the chrome ring)
  right bottom     ; content right/bottom edges
  lh base          ; text line height / baseline (rastport font metrics)
  cw               ; character cell width (rastport font metrics)
  fp-w fp-h        ; first-person view size
  log-x log-w      ; message log column
  col-h            ; height of the log/page column
  band-y band-h    ; effects + compass band at the log column's foot
  plaque-y         ; location plaque top (under the view)
  status-y         ; status pane top
  hdr-y            ; party roster header row top
  party-y          ; party roster rows top
  ring-p)          ; draw the ornate border ring (full-screen backdrop)

(defun %amiga-layout (win rp)
  ;; The Bard's Tale chrome (border ring, framed view, plaque, roster
  ;; header) needs the full 256-line backdrop; a bordered Workbench
  ;; window skips the ring so the framed view still fits.
  (let* ((p *display-profile*)
         (ring-p (zerop (amiga.intuition:window-border-top win)))
         (pad-x (if ring-p (display-profile-pad-x p) 4))
         (pad-y (if ring-p (display-profile-pad-y p) 4))
         (bx (+ (amiga.intuition:window-border-left win) pad-x))
         (by (+ (amiga.intuition:window-border-top win) pad-y))
         (right (- (amiga.intuition:window-width win)
                   (amiga.intuition:window-border-right win) pad-x))
         (bottom (- (amiga.intuition:window-height win)
                    (amiga.intuition:window-border-bottom win) pad-y))
         (lh (+ (amiga.gfx:rastport-tx-height rp) 2))
         (base (amiga.gfx:rastport-tx-baseline rp))
         (cw (max 1 (amiga.gfx:text-length rp "M")))
         (party-y (- bottom (* lh +party-limit+)))
         (hdr-y (- party-y lh))
         (status-y (- hdr-y lh 1))
         (fp-h (min *fp-view-height* (- status-y 1 (+ lh 3) by)))
         (plaque-y (+ by fp-h 1))
         (col-h (- (+ plaque-y lh 1) by))
         (log-x (+ bx *fp-view-width* (display-profile-view-gap p)))
         ;; the band sits at the foot of the log column, its bottom
         ;; flush with the page interior (one above the page outline)
         (band-h (min (display-profile-band-height p) (- col-h 2)))
         (band-y (- (+ by col-h -1) band-h)))
    (%make-ui-layout :bx bx :by by :right right :bottom bottom
                     :lh lh :base base :cw cw
                     :fp-w *fp-view-width* :fp-h fp-h
                     :log-x log-x :log-w (- right log-x)
                     :col-h col-h
                     :band-y band-y :band-h band-h
                     :plaque-y plaque-y
                     :status-y status-y :hdr-y hdr-y :party-y party-y
                     :ring-p ring-p)))

;;; ---------------------------------------------------------------------
;;; Chrome: the Bard's Tale presentation — grey screen, riveted border
;;; ring, the dungeon view as a framed picture with a drop shadow and a
;;; location plaque, the message log as a white page.  All procedural
;;; (pens 0/1/2), so it needs no art assets.

(defun %chrome-rect (rp x0 y0 x1 y1)
  "Rectangle outline with the current pen."
  (amiga.gfx:draw-line rp x0 y0 x1 y0)
  (amiga.gfx:draw-line rp x0 y1 x1 y1)
  (amiga.gfx:draw-line rp x0 y0 x0 y1)
  (amiga.gfx:draw-line rp x1 y0 x1 y1))

(defun %chrome-bg (rp win l)
  "Grey background over the whole inner window, plus the riveted
double-outline border ring on the full-screen backdrop."
  (let* ((el (amiga.intuition:window-border-left win))
         (et (amiga.intuition:window-border-top win))
         (er (- (amiga.intuition:window-width win)
                (amiga.intuition:window-border-right win) 1))
         (eb (- (amiga.intuition:window-height win)
                (amiga.intuition:window-border-bottom win) 1)))
    (amiga.gfx:set-a-pen rp 2)
    (amiga.gfx:rect-fill rp el et er eb)
    (when (ui-layout-ring-p l)
      (amiga.gfx:set-a-pen rp 0)
      (%chrome-rect rp (+ el 1) (+ et 1) (- er 1) (- eb 1))
      (%chrome-rect rp (+ el 6) (+ et 6) (- er 6) (- eb 6))
      ;; rivets along the band between the outlines
      (loop for x from (+ el 8) to (- er 10) by 16
            do (amiga.gfx:rect-fill rp x (+ et 3) (+ x 2) (+ et 4))
               (amiga.gfx:rect-fill rp x (- eb 4) (+ x 2) (- eb 3)))
      (loop for y from (+ et 8) to (- eb 10) by 16
            do (amiga.gfx:rect-fill rp (+ el 3) y (+ el 4) (+ y 2))
               (amiga.gfx:rect-fill rp (- er 4) y (- er 3) (+ y 2))))
    (amiga.gfx:set-a-pen rp 1)))

(defun %chrome-frames (rp game l)
  "The picture frame + drop shadow around the view, the location
plaque under it, and the white message page shell."
  (let* ((bx (ui-layout-bx l))
         (by (ui-layout-by l))
         (w (ui-layout-fp-w l))
         (h (ui-layout-fp-h l))
         (lh (ui-layout-lh l))
         (py (ui-layout-plaque-y l))
         (pb (+ py lh))                 ; plaque bottom
         (lx (ui-layout-log-x l))
         (r (ui-layout-right l)))
    ;; view + plaque drop shadow (down-right, BT2 style)
    (amiga.gfx:set-a-pen rp 0)
    (amiga.gfx:rect-fill rp (+ bx w 1) (+ by 1) (+ bx w 3) (+ pb 3))
    (amiga.gfx:rect-fill rp (+ bx 1) (1+ pb) (+ bx w 3) (+ pb 3))
    ;; white picture frame around the view
    (amiga.gfx:set-a-pen rp 1)
    (%chrome-rect rp (1- bx) (1- by) (+ bx w) (+ by h))
    ;; location plaque: black block, white border, centered map name
    (amiga.gfx:set-a-pen rp 0)
    (amiga.gfx:rect-fill rp (1- bx) (1+ (+ by h)) (+ bx w) pb)
    (amiga.gfx:set-a-pen rp 1)
    (%chrome-rect rp (1- bx) (1+ (+ by h)) (+ bx w) pb)
    (let* ((name (string-capitalize (map-title (game-map game))))
           (tw (amiga.gfx:text-length rp name)))
      (amiga.gfx:move-to rp (+ bx (max 0 (floor (- w tw) 2)))
                         (+ py (ui-layout-base l)))
      (amiga.gfx:gfx-text rp name))
    ;; message page: white sheet with a black outline and shadow
    (amiga.gfx:set-a-pen rp 0)
    (amiga.gfx:rect-fill rp (+ (- lx 4) 2) (+ by 1) (+ r 2) (+ pb 2))
    (amiga.gfx:set-a-pen rp 1)
    (amiga.gfx:rect-fill rp (- lx 4) (1- by) r pb)
    (amiga.gfx:set-a-pen rp 0)
    (%chrome-rect rp (- lx 4) (1- by) r pb)
    (amiga.gfx:set-a-pen rp 1)))

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
the viewport has its full asset size, the wireframe otherwise.  The
blitted view starts from the ceiling/floor backdrop (black where the
pack has none); the walls carve the perspective on top of it."
  (let ((slices (compute-view (game-map game) (game-x game) (game-y game)
                              (game-facing game) (game-view-depth game)))
        (planes (view-planes w h)))
    (if (and walls (= w *fp-view-width*) (= h *fp-view-height*))
        (progn
          ;; ceiling above the horizon, floor below (opaque backdrop),
          ;; then the walls cookie-cut on top so the corners they don't
          ;; cover let the backdrop show through
          (loop for key in '((:ceiling) (:floor))
                for (x y pw ph) in (backdrop-rects planes)
                do (let ((entry (gethash key walls)))
                     (if entry
                         (amiga.gfx:blt-bitmap-rastport (car entry) 0 0 rp
                                                        (+ ox x) (+ oy y)
                                                        pw ph)
                         (progn
                           (amiga.gfx:set-a-pen rp 0)
                           (amiga.gfx:rect-fill rp (+ ox x) (+ oy y)
                                                (+ ox x pw -1)
                                                (+ oy y ph -1))))))
          ;; The wall of night: when darkness truncated the view at an
          ;; open front, the backdrop must not show a lit corridor
          ;; receding beyond it — black out the plane past the last
          ;; visible cell before the walls go on top.
          (let ((s (car (last slices))))
            (when (and s
                       (eq (view-slice-front s) :open)
                       (game-dark-p game))
              (destructuring-bind (qx0 qy0 qx1 qy1)
                  (aref planes (1+ (view-slice-depth s)))
                (amiga.gfx:set-a-pen rp 0)
                (amiga.gfx:rect-fill rp (+ ox qx0) (+ oy qy0)
                                     (+ ox qx1) (+ oy qy1)))))
          (amiga.gfx:set-a-pen rp 1)
          (dolist (rec (view-blit-list slices planes))
            (destructuring-bind (piece x y pw ph) rec
              (let ((entry (gethash piece walls)))
                (when entry
                  (destructuring-bind (bm . mask) entry
                    (if mask
                        (amiga.gfx:blt-mask-bitmap-rastport
                         bm 0 0 rp (+ ox x) (+ oy y) pw ph mask)
                        (amiga.gfx:blt-bitmap-rastport
                         bm 0 0 rp (+ ox x) (+ oy y) pw ph))))))))
        (progn
          (amiga.gfx:set-a-pen rp 0)
          (amiga.gfx:rect-fill rp ox oy (+ ox w -1) (+ oy h -1))
          (amiga.gfx:set-a-pen rp 1)
          (dolist (prim (view-display-list slices planes))
            (ecase (first prim)
              (:line (destructuring-bind (x0 y0 x1 y1) (rest prim)
                       (amiga.gfx:draw-line rp (+ ox x0) (+ oy y0)
                                            (+ ox x1) (+ oy y1))))
              (:door (destructuring-bind (cx cy hw hh) (rest prim)
                       (%amiga-door-rect rp (+ ox cx) (+ oy cy)
                                         hw hh)))))))))

;;; ---------------------------------------------------------------------
;;; Wall-piece assets (M3): the data/gfx ILBMs loaded into offscreen
;;; bitmaps once per session.  RTG-safe: the window's own bitmap is the
;;; AllocBitMap friend and sets the depth, so the pieces live in the
;;; display's native format and every blit copies all its planes;
;;; pixels go in as chunky bytes (WRITE-CHUNKY), never as planes.

;;; The active tile pack directory *GFX-DIR* lives in view.lisp (it is
;;; platform-independent: the manifest and the asset generator use it
;;; on the host too); PLAY-AMIGA's :GFX-DIR argument rebinds it.

(defun %window-bitmap (rp)
  "The BitMap a window rastport renders into (rp_BitMap)."
  (ffi:make-foreign-pointer (ffi:peek-u32 rp 4)))

(defun %load-wall-assets (rp log)
  "Load the active tile pack (*GFX-DIR*): every wall piece into an
offscreen bitmap, plus the optional floor.iff / ceiling.iff backdrops
under the keys (:FLOOR) / (:CEILING).  Returns (VALUES WALLS PALETTE):
the piece key -> (BITMAP . MASK) hash and the pack's CMAP palette
(palette.iff's when present, else the first wall piece's), or
(VALUES NIL NIL) — falling back to the wireframe view — when the pack
is missing, unreadable, or mis-sized.  MASK is a chip-RAM cookie-cut
plane for a piece that uses pen 0 (transparent), else NIL (a plain
opaque blit); the backdrops are always opaque."
  (let ((walls (make-hash-table :test #'equal))
        (palette nil)
        (friend (%window-bitmap rp))
        (depth (max 2 (amiga.gfx:get-bitmap-attr (%window-bitmap rp)
                                                 amiga.gfx:+bma-depth+)))
        (planes (view-planes *fp-view-width* *fp-view-height*)))
    (labels ((build-mask (img)
               ;; A piece that uses pen 0 (the transparent key) gets a
               ;; cookie-cut mask in chip RAM so the backdrop shows
               ;; through; a fully-painted piece needs none.
               (when (image-transparent-p img)
                 (let* ((bytes (mask-bytes (image-width img)
                                           (image-height img)
                                           (image-pixels img)))
                        (chip (amiga:alloc-chip (length bytes))))
                   (dotimes (i (length bytes) chip)
                     (ffi:poke-u8 chip (aref bytes i) i)))))
             (load-piece (key file w h &optional maskable)
               ;; Blits copy W x H from the bitmap wherever the piece's
               ;; slot sits, so a mis-sized pack file would read past
               ;; the bitmap's edges — reject it here, loudly.
               (let ((img (read-ilbm file)))
                 (unless (and (= (image-width img) w)
                              (= (image-height img) h))
                   (error "~A is ~Dx~D, its slot needs ~Dx~D (see ~
PRINT-TILE-MANIFEST)"
                          file (image-width img) (image-height img) w h))
                 (let ((bm (amiga.gfx:alloc-bitmap w h depth
                                                   :friend friend))
                       (mask (when maskable (build-mask img))))
                   (setf (gethash key walls) (cons bm mask))
                   (unless palette
                     (setf palette (image-palette img)))
                   (amiga.gfx:with-bitmap-rastport (brp bm)
                     (amiga.gfx:write-chunky brp 0 0 w h
                                             (image-pixels img)))))))
      (handler-case
          (progn
            (dolist (piece (wall-piece-names))
              (let ((file (concatenate 'string *gfx-dir*
                                       (wall-piece-file piece))))
                (unless (probe-file file)
                  (error "missing wall asset ~A" file))
                (destructuring-bind (x y w h) (wall-piece-rect planes piece)
                  (declare (ignore x y))
                  (load-piece piece file w h t))))
            ;; The backdrops are optional and always opaque: a pack
            ;; without them keeps the black ceiling/floor.
            (destructuring-bind (ceiling floor) (backdrop-rects planes)
              (dolist (entry (list (list '(:ceiling) "ceiling.iff" ceiling)
                                   (list '(:floor) "floor.iff" floor)))
                (destructuring-bind (key name (x y w h)) entry
                  (declare (ignore x y))
                  (let ((file (concatenate 'string *gfx-dir* name)))
                    (when (probe-file file)
                      (load-piece key file w h))))))
            ;; palette.iff (any size) overrides the pack palette.
            (let ((file (concatenate 'string *gfx-dir* "palette.iff")))
              (when (probe-file file)
                (setf palette (image-palette (read-ilbm file)))))
            (values walls palette))
        (error (e)
          (%free-wall-assets walls)
          (when log
            (log-message log (format nil "No wall graphics (~A); ~
wireframe view." e)))
          (values nil nil))))))

;;; The layout reads its text metrics (line height, character cell)
;;; from the rastport font, but the profiles' region budgets are tuned
;;; for topaz 8 (10px lines, 8px cells).  RTG Workbenches often default
;;; to a bigger system font, which would shrink the viewport below the
;;; wall assets' size — so the game selects topaz 8 (a ROM font) on its
;;; rastport explicitly.

(defun %with-game-font (fn)
  "Call FN with the topaz 8 TextFont (NIL when unavailable — the
layout then uses whatever the rastport carries); closes the font after
FN returns."
  (let ((font (amiga.gfx:open-font "topaz.font" 8)))
    (unwind-protect
        (funcall fn font)
      (when font (amiga.gfx:close-font font)))))

(defun %game-rastport (win font)
  "The window's rastport with the game font selected and JAM1 drawing —
text paints glyphs only, so it sits on the grey chrome and the white
page without a background-color box around every character."
  (let ((rp (amiga.intuition:window-rastport win)))
    (when font (amiga.gfx:set-font rp font))
    (amiga.gfx:set-drmd rp amiga.gfx:+jam1+)
    rp))

(defun %free-wall-assets (walls)
  "Free the piece bitmaps and their cookie-cut masks; safe with NIL."
  (when walls
    (maphash (lambda (piece entry)
               (declare (ignore piece))
               (amiga.gfx:free-bitmap (car entry))
               (when (cdr entry)
                 (amiga:free-chip (cdr entry))))
             walls))
  nil)

(defun %amiga-draw-map-region (rp game ox oy cell x0 y0 vw vh full
                               &optional (cw 8))
  "Draw automap cells [X0,X0+VW) x [Y0,Y0+VH) at (OX,OY), CELL pixels
per cell.  FULL non-NIL draws everything (debug); otherwise only what
the party's knowledge holds.  CW is the font's character cell width —
feature glyphs draw only where a character fits.  Used for both the
minimap viewport and the full map mode."
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
          (when (>= cell cw)
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

(defun %amiga-draw-band (rp game l)
  "The band at the foot of the message-log column: active effects
(shield, lamp, ...) as lines at the left, the compass rose — the four
cardinal letters around a diamond, the needle pointing at the party's
facing — in the BAND-H square at the right.  Black on the white page,
separated from the log above by a thin rule."
  (let* ((ox (ui-layout-log-x l))
         (w (ui-layout-log-w l))
         (right (ui-layout-right l))
         (band-y (ui-layout-band-y l))
         (band-h (ui-layout-band-h l))
         (bottom (+ band-y band-h -1))
         (lh (ui-layout-lh l))
         (cw (ui-layout-cw l))
         (rose-w band-h)                ; the rose's square at the right
         (cx (- right 1 (floor rose-w 2)))
         (cy (+ band-y (floor band-h 2)))
         (r (max 6 (- (floor band-h 2) 6)))
         (max-chars (max 4 (floor (- w rose-w 4) cw))))
    ;; band interior on the white page, plus the rule under the log
    (amiga.gfx:set-a-pen rp 1)
    (amiga.gfx:rect-fill rp (- ox 3) band-y (- right 1) bottom)
    (amiga.gfx:set-a-pen rp 0)
    (amiga.gfx:draw-line rp (- ox 3) band-y (- right 1) band-y)
    ;; active effects, one line each
    (let ((y (+ band-y 2 (ui-layout-base l))))
      (dolist (e (game-effects game))
        (when (< y bottom)
          (let ((text (effect-label e)))
            (amiga.gfx:move-to rp ox y)
            (amiga.gfx:gfx-text rp (if (> (length text) max-chars)
                                       (subseq text 0 max-chars)
                                       text))))
        (incf y lh)))
    ;; compass rose
    (destructuring-bind (needle letters)
        (compass-points (game-facing game) cx cy r)
      ;; the rose: a diamond through the needle's reach
      (let ((ri (max 2 (- r 8))))
        (amiga.gfx:set-a-pen rp 0)
        (amiga.gfx:draw-line rp cx (- cy ri) (+ cx ri) cy)
        (amiga.gfx:draw-line rp (+ cx ri) cy cx (+ cy ri))
        (amiga.gfx:draw-line rp cx (+ cy ri) (- cx ri) cy)
        (amiga.gfx:draw-line rp (- cx ri) cy cx (- cy ri)))
      (destructuring-bind (x0 y0 x1 y1) needle
        (amiga.gfx:set-a-pen rp 3)
        (amiga.gfx:draw-line rp x0 y0 x1 y1))
      (dolist (p letters)
        (destructuring-bind (ch x y facing-p) p
          (amiga.gfx:set-a-pen rp (if facing-p 3 0))
          (amiga.gfx:move-to rp (- x (floor cw 2)) (+ y 3))
          (amiga.gfx:gfx-text rp (string ch)))))
    (amiga.gfx:set-a-pen rp 1)))

(defun %amiga-draw-log (rp log l)
  "Message log: trailing lines, newest at the bottom, black text on
the white page (the shell — outline and shadow — is %CHROME-FRAMES's;
the column's foot below belongs to %AMIGA-DRAW-BAND)."
  (let* ((ox (ui-layout-log-x l))
         (oy (ui-layout-by l))
         (w (ui-layout-log-w l))
         (h (- (ui-layout-band-y l) oy))
         (lh (ui-layout-lh l))
         (n (max 1 (floor (- h 2) lh)))
         (max-chars (max 4 (floor (- w 4) (ui-layout-cw l))))
         ;; Each message starts with "> "; long ones wrap onto indented
         ;; continuation lines.  Keep the trailing N display lines so
         ;; the newest stays at the bottom.
         (wrapped (mapcan (lambda (m) (wrap-message m max-chars))
                          (log-recent log n)))
         (lines (last wrapped n)))
    ;; page interior (inside the black outline)
    (amiga.gfx:set-a-pen rp 1)
    (amiga.gfx:rect-fill rp (- ox 3) oy (+ ox w -1) (+ oy h -1))
    (amiga.gfx:set-a-pen rp 0)
    (let ((y (+ oy (- h (* (length lines) lh)) (ui-layout-base l) -2)))
      (dolist (m lines)
        (amiga.gfx:move-to rp ox y)
        (amiga.gfx:gfx-text rp m)
        (incf y lh)))
    (amiga.gfx:set-a-pen rp 1)))

(defun %amiga-hero-row (rp l y hero index)
  "One roster table row at baseline Y: number, name, level and gold in
black; the hit points picked out in white; a downed hero in amber.
Columns come from the profile's ROSTER-COLS character cells."
  (let* ((ox (ui-layout-bx l))
         (cw (ui-layout-cw l))
         (cols (display-profile-roster-cols *display-profile*))
         ;; the name column runs up to one cell short of the level column
         (name-w (- (getf cols :lv) 1 (getf cols :name))))
    (labels ((col (cell pen text)
               (amiga.gfx:set-a-pen rp pen)
               (amiga.gfx:move-to rp (+ ox (* cw cell)) y)
               (amiga.gfx:gfx-text rp text)))
      (col (getf cols :no) 0 (format nil "~D" (1+ index)))
      (col (getf cols :name) 0
           (let ((name (hero-name hero)))
             (if (> (length name) name-w) (subseq name 0 name-w) name)))
      (col (getf cols :lv) 0 (format nil "~D" (hero-level hero)))
      (col (getf cols :hits) 1
           (format nil "~D/~D" (hero-hp hero) (hero-max-hp hero)))
      (col (getf cols :gold) 0 (format nil "~D" (hero-gold hero)))
      (unless (hero-alive-p hero)
        (col (getf cols :down) 3 "DOWN")))
    (amiga.gfx:set-a-pen rp 1)))

(defun %amiga-status (rp game l text)
  "Status pane: position/facing plus contextual key help at the left,
the game clock at the right, black on the grey chrome."
  (let ((ox (ui-layout-bx l))
        (oy (ui-layout-status-y l))
        (right (ui-layout-right l)))
    (amiga.gfx:set-a-pen rp 2)
    (amiga.gfx:rect-fill rp ox oy right (+ oy (ui-layout-lh l) -1))
    (amiga.gfx:set-a-pen rp 0)
    (let* ((clock (clock-line game))
           (clock-w (* (ui-layout-cw l) (length clock)))
           (left (format nil "(~D,~D) ~A  ~A"
                         (game-x game) (game-y game)
                         (dir-keyword (game-facing game))
                         text))
           (left-max (max 0 (floor (- right ox clock-w
                                      (ui-layout-cw l))
                                   (ui-layout-cw l)))))
      (amiga.gfx:move-to rp ox (+ oy (ui-layout-base l)))
      (amiga.gfx:gfx-text rp (if (> (length left) left-max)
                                 (subseq left 0 left-max)
                                 left))
      (amiga.gfx:move-to rp (- right clock-w) (+ oy (ui-layout-base l)))
      (amiga.gfx:gfx-text rp clock))
    (amiga.gfx:set-a-pen rp 1)))

(defun %amiga-party (rp game l)
  "Party roster table, Bard's Tale style: a header row and one numbered
row per hero, black on the grey chrome with the hit points in white.
The row number is the key that opens that hero's character sheet."
  (let* ((ox (ui-layout-bx l))
         (oy (ui-layout-hdr-y l))
         (lh (ui-layout-lh l)))
    (amiga.gfx:set-a-pen rp 2)
    (amiga.gfx:rect-fill rp ox oy
                         (ui-layout-right l)
                         (+ (ui-layout-party-y l) (* lh +party-limit+) -1))
    (amiga.gfx:set-a-pen rp 0)
    (let ((y (+ oy (ui-layout-base l)))
          (cw (ui-layout-cw l))
          (cols (display-profile-roster-cols *display-profile*)))
      (labels ((col (cell text)
                 (amiga.gfx:move-to rp (+ ox (* cw cell)) y)
                 (amiga.gfx:gfx-text rp text)))
        (col (getf cols :name) "CHARACTER")
        (col (getf cols :lv)   "LV")
        (col (getf cols :hits) "HITS")
        (col (getf cols :gold) "GOLD")))
    (let ((y (+ (ui-layout-party-y l) (ui-layout-base l)))
          (i 0))
      (dolist (h (game-party game))
        (%amiga-hero-row rp l y h i)
        (incf y lh)
        (incf i)))
    (amiga.gfx:set-a-pen rp 1)))

(defun %amiga-draw-sheet (rp game index l)
  "Character-sheet page ('1'-'7' from play): the INDEXth party member's
stat block on a white parchment page over the grey chrome."
  (let* ((ox (ui-layout-bx l))
         (oy (ui-layout-by l))
         (lh (ui-layout-lh l))
         (cw (ui-layout-cw l))
         (hero (nth index (game-party game)))
         (px (+ ox (* 2 cw)))           ; the page
         (py (+ oy 8))
         (pw (min (* 40 cw) (- (ui-layout-right l) px (* 2 cw))))
         (ph (+ (* lh 10) 12)))
    (amiga.gfx:set-a-pen rp 2)
    (amiga.gfx:rect-fill rp ox oy (ui-layout-right l) (ui-layout-bottom l))
    ;; page shadow, sheet, outline
    (amiga.gfx:set-a-pen rp 0)
    (amiga.gfx:rect-fill rp (+ px 2) (+ py 2) (+ px pw 2) (+ py ph 2))
    (amiga.gfx:set-a-pen rp 1)
    (amiga.gfx:rect-fill rp px py (+ px pw) (+ py ph))
    (amiga.gfx:set-a-pen rp 0)
    (%chrome-rect rp px py (+ px pw) (+ py ph))
    (let ((y (+ py 4 (ui-layout-base l)))
          (max-chars (floor (- pw 16) cw)))
      (labels ((line (text)
                 (amiga.gfx:move-to rp (+ px 8) y)
                 (amiga.gfx:gfx-text rp (if (> (length text) max-chars)
                                            (subseq text 0 max-chars)
                                            text))
                 (incf y lh)))
        (line (format nil "Character ~D of ~D" (1+ index)
                      (length (game-party game))))
        (incf y (floor lh 2))
        (when hero
          (dolist (text (hero-summary-lines hero))
            (line text)))
        (incf y (floor lh 2))
        (line "1-7 view another   Esc back")))
    (amiga.gfx:set-a-pen rp 1)))

(defun %amiga-draw-location (rp game view l)
  "Location page (shop menu): the location's menu lines on a white
page over the view column.  The log, status and roster panes stay
live around it — gold and messages update as the party shops."
  (let* ((ox (ui-layout-bx l))
         (oy (ui-layout-by l))
         (lh (ui-layout-lh l))
         (cw (ui-layout-cw l))
         (px (+ ox 4))
         (py (+ oy 4))
         (pw (ui-layout-fp-w l))
         (ph (- (ui-layout-status-y l) 4 py))
         (max-lines (floor (- ph 8) lh))
         (max-chars (floor (- pw 16) cw))
         (lines (mapcan (lambda (text) (wrap-text text max-chars))
                        (location-lines game view))))
    ;; page shadow, sheet, outline — same look as the character sheet
    (amiga.gfx:set-a-pen rp 0)
    (amiga.gfx:rect-fill rp (+ px 2) (+ py 2) (+ px pw 2) (+ py ph 2))
    (amiga.gfx:set-a-pen rp 1)
    (amiga.gfx:rect-fill rp px py (+ px pw) (+ py ph))
    (amiga.gfx:set-a-pen rp 0)
    (%chrome-rect rp px py (+ px pw) (+ py ph))
    (let ((y (+ py 4 (ui-layout-base l)))
          (n 0))
      (dolist (text lines)
        (when (< n max-lines)
          (amiga.gfx:move-to rp (+ px 8) y)
          (amiga.gfx:gfx-text rp (if (> (length text) max-chars)
                                     (subseq text 0 max-chars)
                                     text))
          (incf y lh)
          (incf n))))
    (amiga.gfx:set-a-pen rp 1)))

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
      (%amiga-draw-map-region rp game bx by cell x0 y0 w h full
                              (ui-layout-cw l))
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
  "UI palette for the custom screen: black, white, the chrome grey and
amber in pens 0-3 — fixed, tile packs may not change them.  Pens 4-15
start black until %APPLY-PACK-PALETTE fills them with the pack's
colors."
  (let ((vp (amiga.intuition:screen-viewport scr)))
    (amiga.gfx:set-rgb4 vp 0 0 0 0)
    (amiga.gfx:set-rgb4 vp 1 15 15 15)
    (amiga.gfx:set-rgb4 vp 2 10 10 10)
    (amiga.gfx:set-rgb4 vp 3 15 10 3)
    (loop for pen from 4 below (ash 1 (display-profile-screen-depth
                                       *display-profile*))
          do (amiga.gfx:set-rgb4 vp pen 0 0 0))))

(defun %apply-pack-palette (scr palette)
  "Load the tile pack's colors into pens 4-15 of the custom screen.
PALETTE is a CMAP vector of (R G B) lists, 0-255 components (see
READ-ILBM); entries 0-3 are ignored — those pens are the fixed UI
colors — and the components scale to SET-RGB4 nibbles."
  (when palette
    (let ((vp (amiga.intuition:screen-viewport scr)))
      (loop for pen from 4 below (min (ash 1 (display-profile-screen-depth
                                              *display-profile*))
                                      (length palette))
            for rgb = (aref palette pen)
            when rgb
              do (amiga.gfx:set-rgb4 vp pen
                                     (floor (first rgb) 17)
                                     (floor (second rgb) 17)
                                     (floor (third rgb) 17))))))

(defun %call-with-game-window (display fn)
  "Open DISPLAY per the active *DISPLAY-PROFILE* and call FN with the
screen and window.  :WINDOW — a window on the public (Workbench)
screen, the development default.  :SCREEN — an own custom screen (the
profile's nominal PAL geometry, chosen RTG-aware through
AMIGA.GFX:BEST-MODE-ID) covered by a borderless backdrop window."
  (let ((p *display-profile*))
    (ecase display
      (:window
       (amiga.intuition:with-pub-screen (scr)
         (amiga.intuition:with-window
             (win :title "Lambda's Tale"
                  :left 0 :top 0
                  :width (display-profile-win-width p)
                  :height (display-profile-win-height p)
                  :idcmp +game-idcmp+)
           (funcall fn scr win))))
      (:screen
       (amiga.intuition:with-screen
           (scr :width (display-profile-screen-width p)
                :height (display-profile-screen-height p)
                :depth (display-profile-screen-depth p)
                :title "Lambda's Tale"
                :mode-id (amiga.gfx:best-mode-id
                          :width (display-profile-screen-width p)
                          :height (display-profile-screen-height p)
                          :depth (display-profile-screen-depth p)))
         (%game-screen-palette scr)
         ;; the game owns the whole display: put the OS screen bar
         ;; behind the backdrop window (Bard's Tale has no title bar)
         (amiga.intuition:show-title scr nil)
         ;; no window title: on a backdrop window WA_Title still costs
         ;; a title bar (border-top), and the screen already carries one
         (amiga.intuition:with-window
             (win :left 0 :top 0
                  :width (amiga.intuition:screen-width scr)
                  :height (amiga.intuition:screen-height scr)
                  :screen scr
                  :flags (logior amiga.intuition:+wflg-borderless+
                                 amiga.intuition:+wflg-backdrop+
                                 amiga.intuition:+wflg-activate+)
                  :idcmp +game-idcmp+)
           (funcall fn scr win)))))))

;;; ---------------------------------------------------------------------
;;; The game proper

(defun play-amiga (&optional (map-file "worlds/closure/town.map")
                   &key (display :screen) (profile *display-profile*)
                     gfx-dir)
  "Interactive walkabout.  Loads the campaign.lisp next to the map
file (classes, monsters, items, party) when present — a designer's own
world directory brings its own campaign.
DISPLAY is :screen (the default: an own custom screen, RTG-aware) or
:window (a development view on the Workbench screen).  PROFILE is a
DISPLAY-PROFILE or its name — :lores or :hires — and sets the screen
geometry, viewport and default tile pack.  A zone may declare its own
pack with (ZONE :GFX DIR) — see ZONE-GFX-DIR — swapped in when travel
enters it; GFX-DIR here overrides both (precedence: GFX-DIR, then the
zone's :GFX, then the profile's pack).  A pack is a directory of
wall-piece ILBMs plus the optional floor.iff / ceiling.iff /
palette.iff (see PRINT-TILE-MANIFEST for the contract, which depends
on the profile); the pack's colors show on the custom screen — a
Workbench window keeps the Workbench palette.
Keys: W forward, S back-step, A/D turn, M map mode (M/Esc leaves it,
F toggles the debug full view there), 1-7 open a party member's
character sheet (1-7 switch heroes there, Esc leaves), Q/Esc quit;
in combat A attack, D defend, F flee; in a location (shop) 1-9
choose, S/B switch sell/buy, Esc back/leave.  Save/Load/Quit sit in
the menu strip (right mouse button)."
  (load-campaign map-file)
  (with-display-profile (profile)
   (let* ((*gfx-dir* (or gfx-dir *gfx-dir*))
         (map (load-map-file map-file))
         (game nil)
         (log nil)
         (mode :play)       ; :play, :map (full map view) or :sheet
         (full nil)         ; omniscient map (debug), map mode only
         (sheet-hero 0)     ; party index shown in :sheet mode
         (shopv nil)        ; SHOP-VIEW while inside a location
         (zone-dirty nil)   ; party traveled: the chrome needs a repaint
         (over nil))
    (labels ((wire (g)
               (setf log (attach-message-log g))
               (setf shopv (when (game-location g) (make-shop-view)))
               (on-event g :enter-location
                         (lambda (gm loc) (declare (ignore gm loc))
                           (setf shopv (make-shop-view))))
               (on-event g :leave-location
                         (lambda (gm loc) (declare (ignore gm loc))
                           (setf shopv nil)))
               (on-event g :enter-zone
                         (lambda (gm map) (declare (ignore gm map))
                           (setf zone-dirty t)))
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
                       (walls nil)      ; loaded piece bitmaps
                       (walls-dir nil)) ; the pack they came from
               (labels ((effective-gfx-dir ()
                          ;; precedence: the explicit :GFX-DIR argument,
                          ;; then the zone's (ZONE :GFX ...), then the
                          ;; profile's pack (bound into *GFX-DIR*)
                          (or gfx-dir (zone-gfx-dir game) *gfx-dir*))
                        (ensure-walls ()
                          ;; (re)load the wall bitmaps when the wanted
                          ;; pack changed — first draw, zone travel
                          ;; (:GFX zones), load-game.  Pack colors only
                          ;; on our own screen — a Workbench window has
                          ;; no say over the Workbench palette.
                          (let ((want (effective-gfx-dir)))
                            (unless (equal want walls-dir)
                              (setf walls (%free-wall-assets walls)
                                    walls-dir want)
                              (let ((*gfx-dir* want))
                                (multiple-value-bind (w pal)
                                    (%load-wall-assets rp log)
                                  (setf walls w)
                                  (when (eq display :screen)
                                    (%apply-pack-palette scr pal)))))))
                        (status-text ()
                          (cond ((eq over :won) "You win!  Press Q.")
                                ((eq over :lost) "Game over.  Press Q.")
                                ((game-combat game)
                                 "COMBAT!  A attack  D defend  F flee")
                                ((game-location game)
                                 (location-title (game-location game)))
                                (t "W/S move  A/D turn  M map  1-7 hero")))
                        (clear-inner ()
                          ;; Grey-wipe the content area (a bit beyond
                          ;; it, to catch the frames and shadows) when
                          ;; the map/sheet page was underneath.
                          (amiga.gfx:set-a-pen rp 2)
                          (amiga.gfx:rect-fill rp
                                               (- (ui-layout-bx l) 2)
                                               (- (ui-layout-by l) 2)
                                               (+ (ui-layout-right l) 2)
                                               (ui-layout-bottom l))
                          (amiga.gfx:set-a-pen rp 1))
                        (fresh-play ()
                          ;; back to the play page: chrome + panes
                          (setf mode :play)
                          (clear-inner)
                          (%chrome-frames rp game l)
                          (redraw))
                        (leave-map ()
                          (fresh-play))
                        (open-sheet (i)
                          ;; '1'-'7' from play: show that roster slot if
                          ;; it holds a hero, else stay put
                          (when (nth i (game-party game))
                            (setf sheet-hero i
                                  mode :sheet)
                            (redraw)))
                        (leave-sheet ()
                          (fresh-play))
                        (redraw ()
                          ;; travel switched zones: swap in the zone's
                          ;; tile pack and repaint the chrome first
                          ;; (the plaque carries the zone title)
                          (when zone-dirty
                            (setf zone-dirty nil)
                            (ensure-walls)
                            (clear-inner)
                            (%chrome-frames rp game l))
                          (cond
                            ((eq mode :map)
                             (%amiga-draw-map-page rp game l full))
                            ((eq mode :sheet)
                             (%amiga-draw-sheet rp game sheet-hero l))
                            (t
                             (if (game-location game)
                                 (%amiga-draw-location
                                  rp game
                                  (or shopv (setf shopv (make-shop-view)))
                                  l)
                                 (progn
                                   (%amiga-draw-fp rp game
                                                   (ui-layout-bx l)
                                                   (ui-layout-by l)
                                                   (ui-layout-fp-w l)
                                                   (ui-layout-fp-h l)
                                                   walls)
                                   (%amiga-draw-band rp game l)))
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
                                      mode :play
                                      zone-dirty nil)
                                ;; loading may leave map mode (menu
                                ;; item) or land in a zone with its own
                                ;; tile pack — swap packs and repaint
                                ;; the chrome (plaque carries the name)
                                (ensure-walls)
                                (clear-inner)
                                (%chrome-frames rp game l)
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
                                  ((eq mode :sheet)
                                   (cond ((eql lc #\q) :quit)
                                         ((eql c :esc) (leave-sheet) nil)
                                         ((and (characterp c)
                                               (digit-char-p c)
                                               (<= 1 (digit-char-p c)
                                                   +party-limit+))
                                          (open-sheet (1- (digit-char-p c)))
                                          nil)))
                                  ((game-location game)
                                   ;; inside a shop: the shared model
                                   ;; handles the keys (Esc backs out /
                                   ;; leaves; Q still quits the game)
                                   (if (eql lc #\q)
                                       :quit
                                       (let ((key (if (eq c :esc)
                                                      #\Escape
                                                      c)))
                                         (when (characterp key)
                                           (location-act
                                            game
                                            (or shopv
                                                (setf shopv
                                                      (make-shop-view)))
                                            key))
                                         (if (game-location game)
                                             (redraw)
                                             (fresh-play))
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
                                  ((and (characterp c) (digit-char-p c)
                                        (<= 1 (digit-char-p c) +party-limit+))
                                   (open-sheet (1- (digit-char-p c)))
                                   nil)
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
                       (ensure-walls)
                       (%chrome-bg rp win l)
                       (%chrome-frames rp game l)
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
                   (setf walls (%free-wall-assets walls)))))))))))))))
