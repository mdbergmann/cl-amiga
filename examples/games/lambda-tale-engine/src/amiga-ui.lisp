;;; Lambda's Tale — AmigaOS front-end (Intuition window or custom screen,
;;; wireframe view).  Layout per specs/ui-and-engine.md, the Bard's
;;; Tale two-column split, sized by the active display profile:
;;;
;;;   +--------------------+----------------------+
;;;   | first-person view  | message log          |
;;;   |                    | (microfont, newest   |
;;;   |                    |  at the bottom)      |
;;;   +--------------------+----------------------+
;;;   | location plaque    | effect icons [+rose] |
;;;   +--------------------+----------------------+
;;;   | party roster (header + 7 rows)            |
;;;   +-------------------------------------------+
;;;
;;; There is no status line: the party roster sits right under the
;;; view, the key reference lives on the help page ('h' or '?'), and
;;; the position/clock show in the map mode's footer.  The message log
;;; renders in the engine's compact 5x7 microfont (microfont.lisp) so
;;; the narrow column holds more text.  Active effects show as icons
;;; only, laid out left to right in effect order on the 20px grey
;;; strip below the log page; an effect that grants a :COMPASS shows
;;; the live rose in its own slot (no fixed compass corner), and the
;;; facing also shows in the map footer while one burns
;;; (COMPASS-ACTIVE-P).
;;;
;;; The automap lives in a full-screen map mode under the 'm' key.
;;;
;;; Locations (shop, tavern, ...) and the character sheet TAKE OVER
;;; the message area (%AMIGA-DRAW-TAKEOVER): their menu lines render
;;; at the top of the log page, the trailing log lines keep scrolling
;;; below a rule, and the view column shows the location's :image /
;;; the sheet hero's class portrait when the campaign provides one
;;; (%AMIGA-DRAW-PICTURE; the live first-person view otherwise).  The
;;; cast/use/sing menus and the save picker keep their overlay page
;;; over the view column (%AMIGA-DRAW-PAGE) — the log stays readable
;;; beside them, which matters in combat.
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
;;; built with gadtools.library — the Amiga-native place for them; both
;;; items (and the S/L keys) open the shared save-slot picker
;;; (src/save-menu.lisp), saves/NAME.sav.

(require "amiga/intuition")
(require "amiga/graphics")
(require "amiga/gadtools")

(in-package :tale)

(defvar *autoplay* nil
  "Testing hook: a list of inputs fed to the game one per Intuition
tick (~10/s), driving a full unattended session — key characters,
:ESC, or (:CLICK X Y) entries resolved through the mouse hotspot map
exactly like a real left click.")

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
          ;; left-button clicks: everything play-able by key is
          ;; clickable too (see the hotspot list below)
          amiga.intuition:+idcmp-mousebuttons+
          ;; the *autoplay* heartbeat
          amiga.intuition:+idcmp-intuiticks+))

;;; ---------------------------------------------------------------------
;;; Mouse hotspots: the click-to-key map.  The game's whole input funnel
;;; is ACT (one key character per action), so mouse support synthesizes
;;; keys: every REDRAW rebuilds *HOTSPOTS* — the renderers register a
;;; rectangle per pickable thing (a menu option row, its footer's [s]
;;; hint, a roster row, a movement zone on the view) with the key it
;;; stands for — and a left click feeds the key under the pointer into
;;; ACT, which routes it exactly like the keyboard.  The list is
;;; rebuilt on every redraw, so it always matches what is on screen and
;;; which model currently eats the keys.

(defvar *hotspots* '()
  "Click targets of the current frame: entries (X0 Y0 X1 Y1 KEY),
newest first — later registrations win, so a mode's whole-page
catch-all goes in first and the specific targets after it.")

(defun %hotspot (key x0 y0 x1 y1)
  "Register the inclusive pixel rectangle as a click target for KEY (a
character or :ESC).  NIL keys register nothing."
  (when key
    (push (list x0 y0 x1 y1 key) *hotspots*)))

(defun %hotspot-at (x y)
  "The key registered under pixel (X,Y), or NIL — most recently
registered target first."
  (dolist (spot *hotspots*)
    (destructuring-bind (x0 y0 x1 y1 key) spot
      (when (and (<= x0 x x1) (<= y0 y y1))
        (return key)))))

(defun %register-line-hotspots (line x y advance line-h row-x0 row-x1)
  "Click targets for one drawn menu line: an option line (a key from
MENU-LINE-KEY) gets the whole row ROW-X0..ROW-X1, and a plain line's
bracket hints ([s] sell, [Esc] back — see MENU-KEY-SPANS) each get
their own span.  (X,Y) is the drawn text's top-left, ADVANCE the fixed
character cell width, LINE-H the row height."
  (let ((key (menu-line-key line))
        (y1 (+ y line-h -1)))
    (if key
        (%hotspot key row-x0 y row-x1 y1)
        (dolist (span (menu-key-spans (menu-line-text line)))
          (destructuring-bind (start end span-key) span
            ;; Esc goes through ACT as :ESC — the same shape a real
            ;; VANILLAKEY 27 arrives in, so every mode recognizes it
            (%hotspot (if (eql span-key #\Escape) :esc span-key)
                      (+ x (* start advance)) y
                      (+ x (* end advance) -1) y1))))))

(defun %register-move-zones (l)
  "Click-to-walk zones on the first-person view: the left and right
quarters turn (A/D), the middle walks forward (W) with a back-step
band (S) along its bottom third."
  (let* ((x0 (ui-layout-bx l))
         (y0 (ui-layout-by l))
         (w (ui-layout-fp-w l))
         (h (ui-layout-fp-h l))
         (x1 (+ x0 w -1))
         (y1 (+ y0 h -1))
         (q (floor w 4))
         (split (+ y0 (floor (* h 2) 3))))
    (%hotspot #\a x0 y0 (+ x0 q -1) y1)
    (%hotspot #\d (- x1 q -1) y0 x1 y1)
    (%hotspot #\w (+ x0 q) y0 (- x1 q) (1- split))
    (%hotspot #\s (+ x0 q) split (- x1 q) y1)))

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
  page-b           ; log page bottom edge (its outline row)
  band-y band-h    ; effect icons + compass strip below the log page
  plaque-y plaque-b ; location plaque top / bottom edges (under the view)
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
         ;; top-down: view, plaque, then the roster right below —
         ;; leftover space becomes bottom margin (no status line)
         (fp-h (min *fp-view-height*
                    (- bottom by
                       (+ lh 3)                 ; the plaque
                       (+ 5 lh)                 ; roster gap + header
                       (* lh +party-limit+))))  ; roster rows
         (plaque-y (+ by fp-h 1))
         (plaque-b (+ plaque-y lh 2))   ; 2px taller than a text line so
                                        ; the glyphs clear both borders
         (col-h (- (1+ plaque-b) by))
         (log-x (+ bx *fp-view-width* (display-profile-view-gap p)))
         ;; the effect strip sits at the foot of the log column, its
         ;; bottom flush with the plaque's; the white log page ends a
         ;; small gap above it
         (band-h (min (display-profile-band-height p) (- col-h 8)))
         (band-y (- (+ by col-h -1) band-h))
         (page-b (- band-y 4))
         (hdr-y (+ plaque-b 5))
         (party-y (+ hdr-y lh)))
    (%make-ui-layout :bx bx :by by :right right :bottom bottom
                     :lh lh :base base :cw cw
                     :fp-w *fp-view-width* :fp-h fp-h
                     :log-x log-x :log-w (- right log-x)
                     :col-h col-h
                     :page-b page-b
                     :band-y band-y :band-h band-h
                     :plaque-y plaque-y :plaque-b plaque-b
                     :hdr-y hdr-y :party-y party-y
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

(defun %plaque-name (rp name w)
  "NAME (already display-cased) clipped to the plaque's W-pixel
interior via FIT-TITLE (src/view.lisp), measured with the rastport's
real font."
  (fit-title name (lambda (s) (amiga.gfx:text-length rp s)) (- w 2)))

(defun %chrome-frames (rp game l)
  "The picture frame + drop shadow around the view, the location
plaque under it, and the white message page shell (the page ends a
small gap above the effect strip — see %AMIGA-DRAW-BAND)."
  (let* ((bx (ui-layout-bx l))
         (by (ui-layout-by l))
         (w (ui-layout-fp-w l))
         (h (ui-layout-fp-h l))
         (py (ui-layout-plaque-y l))
         (pb (ui-layout-plaque-b l))    ; plaque bottom
         (pg (ui-layout-page-b l))      ; log page bottom
         (lx (ui-layout-log-x l))
         (r (ui-layout-right l)))
    ;; view + plaque drop shadow (down-right, BT2 style)
    (amiga.gfx:set-a-pen rp 0)
    (amiga.gfx:rect-fill rp (+ bx w 1) (+ by 1) (+ bx w 3) (+ pb 3))
    (amiga.gfx:rect-fill rp (+ bx 1) (1+ pb) (+ bx w 3) (+ pb 3))
    ;; white picture frame around the view
    (amiga.gfx:set-a-pen rp 1)
    (%chrome-rect rp (1- bx) (1- by) (+ bx w) (+ by h))
    ;; location plaque: black block, white border, centered map name;
    ;; the block is two pixels taller than a text line so the glyphs
    ;; sit clear of both border rows
    (amiga.gfx:set-a-pen rp 0)
    (amiga.gfx:rect-fill rp (1- bx) py (+ bx w) pb)
    (amiga.gfx:set-a-pen rp 1)
    (%chrome-rect rp (1- bx) py (+ bx w) pb)
    (let ((name (%plaque-name rp (string-capitalize (map-title (game-map game))) w)))
      (let ((tw (amiga.gfx:text-length rp name)))
        (amiga.gfx:move-to rp (+ bx (max 0 (floor (- w tw) 2)))
                           (+ py 2 (ui-layout-base l)))
        (amiga.gfx:gfx-text rp name)))
    ;; message page: white sheet with a black outline and shadow
    (amiga.gfx:set-a-pen rp 0)
    (amiga.gfx:rect-fill rp (+ (- lx 4) 2) (+ by 1) (+ r 2) (+ pg 2))
    (amiga.gfx:set-a-pen rp 1)
    (amiga.gfx:rect-fill rp (- lx 4) (1- by) r pg)
    (amiga.gfx:set-a-pen rp 0)
    (%chrome-rect rp (- lx 4) (1- by) r pg)
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
      (dlog-timed ("wall pack ~A" *gfx-dir*)
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
          (dlog "wall pack ~A failed: ~A" *gfx-dir* e)
          (when log
            (log-message log (format nil "No wall graphics (~A); ~
wireframe view." e)))
          (values nil nil)))))))

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

;;; ---------------------------------------------------------------------
;;; The mouse pointer.  An own screen starts with unset sprite colors —
;;; the pointer would be invisible black on black — so the game always
;;; shows an explicit SetPointer sprite of its own: a pointing hand
;;; (the built-in *HAND-POINTER-ART*), overridable per campaign by a
;;; pointer.iff in the zone's tile pack (16 px wide at most, pens 1-3;
;;; its CMAP entries 1-3 become screen colors 17-19, the hot spot is
;;; the topmost-leftmost inked pixel).  The sprite is re-shown after
;;; every palette change: Picasso96/RTG latches the pointer image and
;;; its colors at SetPointer time, so setting colors 17-19 alone can
;;; leave an already-shown pointer black.  During the loads that take
;;; real seconds at 14MHz (tile packs on zone travel, a game load,
;;; first-sight ILBM images) the pointer switches to a busy hourglass:
;;; plane words (A B) per row, pixel value 1 (A) the sand, 2 (B) the
;;; frame.

(defvar *game-pointer* nil
  "The standard in-game pointer as (CHIP HEIGHT XOFF YOFF) — chip-RAM
sprite data currently shown on the game window — or NIL outside a
session (the system default pointer then applies).")

(defvar *pointer-window* nil
  "The game window while a session runs.  Draw code deep below the
event loop (the image cache) brackets its slow first-sight loads with
the busy pointer through this.")

(defun %pointer-chip (image)
  "IMAGE as chip-RAM sprite data plus SET-POINTER geometry: returns
\(CHIP HEIGHT XOFF YOFF); the caller frees CHIP (AMIGA:FREE-CHIP)."
  (let* ((rows (pointer-sprite-rows image))
         (chip (amiga:alloc-chip (* 2 (+ 2 (* 2 (length rows)) 2))))
         (off 0))
    (flet ((word (w) (ffi:poke-u16 chip w off) (incf off 2)))
      (word 0) (word 0)                 ; position control
      (dolist (row rows)
        (word (first row))
        (word (second row)))
      (word 0) (word 0))                ; sprite terminator
    (multiple-value-bind (hx hy) (pointer-hotspot image)
      (list chip (length rows) (- hx) (- hy)))))

(defun %apply-standard-pointer (win)
  "Show the session's standard pointer (the hand) on WIN, or the
system default when none is loaded."
  (if *game-pointer*
      (destructuring-bind (chip height xoff yoff) *game-pointer*
        (amiga.intuition:set-pointer win chip height 16 xoff yoff))
      (amiga.intuition:clear-pointer win)))

(defun %pointer-image ()
  "The standard pointer art: the tile pack's pointer.iff when the pack
ships one (campaign-configurable), else the built-in hand.  A
pointer.iff that will not load or breaks the sprite constraints logs
to the trace and falls back to the hand."
  (let ((file (and *gfx-dir*
                   (concatenate 'string *gfx-dir* "pointer.iff"))))
    (or (when (and file (probe-file file))
          (handler-case
              (let ((img (read-ilbm file)))
                (pointer-sprite-rows img) ; validate geometry and pens
                img)
            (error (e)
              (dlog "pointer.iff ~A rejected: ~A" file e)
              nil)))
        (hand-pointer-image))))

(defun %ensure-standard-pointer (scr win display)
  "(Re)build the standard pointer for the current tile pack and show
it: art from %POINTER-IMAGE, its palette entries 1-3 into the sprite
colors (screen colors 17-19) — on our own screen only; a Workbench
window keeps the Workbench pointer colors."
  (let ((img (%pointer-image)))
    (when (and scr (eq display :screen))
      (let ((vp (amiga.intuition:screen-viewport scr)))
        (loop for i from 1 to 3
              for rgb = (and (< i (length (image-palette img)))
                             (aref (image-palette img) i))
              when rgb
                do (amiga.gfx:set-rgb4 vp (+ 16 i)
                                       (floor (first rgb) 17)
                                       (floor (second rgb) 17)
                                       (floor (third rgb) 17)))))
    (let ((old *game-pointer*))
      (setf *game-pointer* (%pointer-chip img))
      (%apply-standard-pointer win)
      ;; the old sprite data is off the hardware only after the new
      ;; SetPointer above, so it frees last
      (when old (amiga:free-chip (first old))))))

(defun %free-standard-pointer (win)
  "Drop the standard pointer at session end: back to the system
default, sprite data freed."
  (when *game-pointer*
    (amiga.intuition:clear-pointer win)
    (amiga:free-chip (first *game-pointer*))
    (setf *game-pointer* nil)))

(defparameter *busy-pointer-image*
  '((#x0000 #xFFFF)
    (#x3FFC #x4002)
    (#x1FF8 #x2004)
    (#x0FF0 #x1008)
    (#x07E0 #x0810)
    (#x03C0 #x0420)
    (#x0180 #x0240)
    (#x0180 #x0240)
    (#x03C0 #x0420)
    (#x07E0 #x0810)
    (#x0FF0 #x1008)
    (#x1FF8 #x2004)
    (#x3FFC #x4002)
    (#x0000 #xFFFF)))

(defun %make-busy-pointer-chip ()
  "The busy-pointer image as chip-RAM sprite data ready for
SET-POINTER: posctl words, (A B) per row, trailing words.  The caller
frees it (AMIGA:FREE-CHIP) after the pointer moves off it."
  (let* ((rows (length *busy-pointer-image*))
         (chip (amiga:alloc-chip (* 2 (+ 2 (* 2 rows) 2))))
         (off 0))
    (flet ((word (w) (ffi:poke-u16 chip w off) (incf off 2)))
      (word 0) (word 0)                 ; position control
      (dolist (row *busy-pointer-image*)
        (word (first row))              ; low plane: the sand
        (word (second row)))            ; high plane: the frame
      (word 0) (word 0))                ; sprite terminator
    chip))

(defvar *busy-pointer-active* nil
  "Non-NIL while the busy pointer is up — nested loads (a game load
that then swaps tile packs) keep the outer pointer instead of flashing
it off halfway.")

(defun %call-with-busy-pointer (win fn)
  "Show the busy hourglass pointer on WIN while FN runs; puts the
standard pointer (the hand — or the system default outside a session)
back after and frees the sprite data.  Reentrant: a nested call runs
FN directly under the already-shown pointer."
  (if *busy-pointer-active*
      (funcall fn)
      (let ((chip (%make-busy-pointer-chip)))
        (unwind-protect
            (let ((*busy-pointer-active* t))
              (amiga.intuition:set-pointer
               win chip (length *busy-pointer-image*) 16 -7 -6)
              (funcall fn))
          (%apply-standard-pointer win)
          (amiga:free-chip chip)))))

;;; The image cache: effects-band icons (effect :image), location
;;; pictures (the location op's :image) and character portraits (hero
;;; class :image) — arbitrary ILBMs loaded lazily on first draw into a
;;; per-session cache keyed by the resolved path (map-relative, like
;;; zone tile packs), with the wall-piece bitmap recipe: window-depth
;;; friend bitmap, chunky upload, chip-RAM cookie-cut mask when the
;;; image uses pen 0.  A file that will not load logs once and its
;;; user falls back (text label, first-person view).

(defun %load-image (rp path)
  "Load the ILBM at PATH into an offscreen bitmap; returns the cache
entry (BITMAP WIDTH HEIGHT MASK), MASK NIL for an opaque image."
  (let* ((img (read-ilbm path))
         (w (image-width img))
         (h (image-height img))
         (friend (%window-bitmap rp))
         (depth (max 2 (amiga.gfx:get-bitmap-attr friend
                                                  amiga.gfx:+bma-depth+)))
         (bm (amiga.gfx:alloc-bitmap w h depth :friend friend))
         (mask (when (image-transparent-p img)
                 (let* ((bytes (mask-bytes w h (image-pixels img)))
                        (chip (amiga:alloc-chip (length bytes))))
                   (dotimes (i (length bytes) chip)
                     (ffi:poke-u8 chip (aref bytes i) i))))))
    (amiga.gfx:with-bitmap-rastport (brp bm)
      (amiga.gfx:write-chunky brp 0 0 w h (image-pixels img)))
    (list bm w h mask)))

(defun %cached-image (rp images path log)
  "The cached entry (BITMAP WIDTH HEIGHT MASK) for the ILBM at PATH,
loading it on first sight, or NIL — PATH is NIL, or the file would
not load (said once in the log).  A first-sight load reads an ILBM
from disk — seconds at 14MHz (a location picture on entering a shop),
so it runs under the busy pointer."
  (when path
    (let ((entry (gethash path images)))
      (flet ((load-it ()
               (handler-case
                   (setf (gethash path images) (%load-image rp path))
                 (error (e)
                   (when log
                     (log-message log (format nil "No image ~A (~A)."
                                              path e)))
                   (setf (gethash path images) :missing)
                   nil))))
        (cond ((eq entry :missing) nil)
              (entry entry)
              (*pointer-window*
               (%call-with-busy-pointer *pointer-window* #'load-it))
              (t (load-it)))))))

(defun %effect-icon (rp images game effect log)
  "EFFECT's cached icon entry (BITMAP WIDTH HEIGHT MASK), or NIL — no
:image, or the file would not load (the band falls back to nothing)."
  (%cached-image rp images (effect-image-path game effect) log))

(defun %free-images (images)
  "Free the cached image bitmaps and masks; safe with NIL."
  (when images
    (maphash (lambda (path entry)
               (declare (ignore path))
               (unless (eq entry :missing)
                 (amiga.gfx:free-bitmap (first entry))
                 (when (fourth entry)
                   (amiga:free-chip (fourth entry)))))
             images))
  nil)

(defun %amiga-draw-picture (rp images path l log)
  "Draw the ILBM at PATH in the view slot — black backdrop, the image
centered (center-cropped when it overhangs the viewport).  Returns T,
or NIL — no PATH, or the file would not load — so the caller can fall
back to the first-person view.  Pictures blit opaque: over the black
backdrop a pen-0 pixel and a masked-out pixel look the same."
  (let ((entry (%cached-image rp images path log)))
    (when entry
      (destructuring-bind (bm iw ih mask) entry
        (declare (ignore mask))
        (let* ((ox (ui-layout-bx l))
               (oy (ui-layout-by l))
               (w (ui-layout-fp-w l))
               (h (ui-layout-fp-h l))
               (bw (min iw w))
               (bh (min ih h))
               (sx (max 0 (floor (- iw w) 2)))
               (sy (max 0 (floor (- ih h) 2)))
               (dx (+ ox (max 0 (floor (- w iw) 2))))
               (dy (+ oy (max 0 (floor (- h ih) 2)))))
          (amiga.gfx:set-a-pen rp 0)
          (amiga.gfx:rect-fill rp ox oy (+ ox w -1) (+ oy h -1))
          (amiga.gfx:set-a-pen rp 1)
          (amiga.gfx:blt-bitmap-rastport bm sx sy rp dx dy bw bh)
          t)))))

;;; Message-log lines render in the engine's 5x7 microfont
;;; (microfont.lisp) — smaller than topaz 8, so the narrow column
;;; holds more text.  Each distinct display line is rasterized once
;;; (black on the white page) into an offscreen bitmap and blitted on
;;; every redraw — one OS call per line instead of a chunky upload,
;;; which matters at 14MHz.  The cache is per session, keyed by the
;;; (already truncated) line text.

(defconstant +log-line-cache-limit+ 128
  "Cache entries before the log-line bitmaps are flushed wholesale —
far above the distinct lines ever on screen at once.")

(defun %log-line-bitmap (rp lines text)
  "The cached (BITMAP . WIDTH) for the log display line TEXT, LINES
being the session's cache; renders and uploads it on first sight."
  (or (gethash text lines)
      (progn
        (when (>= (hash-table-count lines) +log-line-cache-limit+)
          (%free-log-lines lines)
          (clrhash lines))
        (multiple-value-bind (pens w h)
            (microfont-line text 0 1)   ; black glyphs on the white page
          (let* ((friend (%window-bitmap rp))
                 (depth (max 2 (amiga.gfx:get-bitmap-attr
                                friend amiga.gfx:+bma-depth+)))
                 (bm (amiga.gfx:alloc-bitmap w h depth :friend friend)))
            (amiga.gfx:with-bitmap-rastport (brp bm)
              (amiga.gfx:write-chunky brp 0 0 w h pens))
            (setf (gethash text lines) (cons bm w)))))))

(defun %free-log-lines (lines)
  "Free the cached log-line bitmaps; safe with NIL."
  (when lines
    (maphash (lambda (text entry)
               (declare (ignore text))
               (amiga.gfx:free-bitmap (car entry)))
             lines))
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

(defun %amiga-draw-band (rp game l &optional icons log)
  "The effect strip below the log page, on the grey chrome (a small
gap separates it from the page above; the profiles keep the strip 20px
— just clearing the 16px icons, so the log page above gets the room).
One slot per active effect, laid out left to right in effect order —
no labels; casting/expiry is announced in the log.  An effect's slot
shows its icon (ICONS is the session's icon cache, see %EFFECT-ICON),
except that an effect carrying a :COMPASS payload shows the live
compass rose instead: the diamond with the amber needle pointing at
the party's facing, the facing letter beside it.  The rose thus sits
wherever the granting spell/item/song sits in the strip, not at a
fixed spot.  An effect with neither icon nor :COMPASS shows nothing."
  (let* ((ox (ui-layout-log-x l))
         (right (ui-layout-right l))
         (band-y (ui-layout-band-y l))
         (band-h (ui-layout-band-h l))
         (bottom (+ band-y band-h -1))
         (cw (ui-layout-cw l))
         (x (- ox 4)))
    ;; the strip is bare chrome: grey-wipe it (covers icon churn)
    (amiga.gfx:set-a-pen rp 2)
    (amiga.gfx:rect-fill rp (- ox 4) band-y right bottom)
    (dolist (e (game-effects game))
      (cond
        ((getf (effect-payload e) :compass)
         ;; the live rose in this effect's slot
         (let* ((size (min band-h 16))
                (cx (+ x (floor size 2)))
                (cy (+ band-y (floor band-h 2)))
                (ri (max 3 (1- (floor size 2))))
                (f (game-facing game)))
           (when (<= (+ x size 2 cw -1) right)
             (amiga.gfx:set-a-pen rp 0)
             (amiga.gfx:draw-line rp cx (- cy ri) (+ cx ri) cy)
             (amiga.gfx:draw-line rp (+ cx ri) cy cx (+ cy ri))
             (amiga.gfx:draw-line rp cx (+ cy ri) (- cx ri) cy)
             (amiga.gfx:draw-line rp (- cx ri) cy cx (- cy ri))
             (amiga.gfx:set-a-pen rp 3)
             (amiga.gfx:draw-line rp cx cy
                                  (+ cx (* (aref *dir-dx* f) (1- ri)))
                                  (+ cy (* (aref *dir-dy* f) (1- ri))))
             ;; the facing letter, amber, beside the rose
             (amiga.gfx:move-to rp (+ x size 2) (+ cy 3))
             (amiga.gfx:gfx-text rp (string (char "NESW" f)))
             (incf x (+ size 2 cw 4)))))
        (t
         (let ((entry (and icons (%effect-icon rp icons game e log))))
           (when entry
             (destructuring-bind (bm iw ih mask) entry
               (when (and (<= (+ x iw -1) right) (<= ih band-h))
                 (let ((y (+ band-y (max 0 (floor (- band-h ih) 2)))))
                   (if mask
                       (amiga.gfx:blt-mask-bitmap-rastport
                        bm 0 0 rp x y iw ih mask)
                       (amiga.gfx:blt-bitmap-rastport
                        bm 0 0 rp x y iw ih)))
                 (incf x (+ iw 2)))))))))
    (amiga.gfx:set-a-pen rp 1)))

(defun %put-microfont-line (rp lines-cache text x y)
  "One microfont display line at (X,Y): the cached bitmap blit when
LINES-CACHE is given, a direct chunky upload otherwise (correct but
slower — good enough for tests)."
  (if lines-cache
      (destructuring-bind (bm . bw) (%log-line-bitmap rp lines-cache text)
        (amiga.gfx:blt-bitmap-rastport bm 0 0 rp x y
                                       bw +microfont-line-height+))
      (multiple-value-bind (pens pw ph) (microfont-line text 0 1)
        (amiga.gfx:write-chunky rp x y pw ph pens))))

(defun %amiga-draw-log (rp log l &optional lines-cache)
  "Message log: trailing lines, newest at the bottom, black microfont
text on the white page (the shell — outline and shadow — is
%CHROME-FRAMES's; the strip below the page belongs to
%AMIGA-DRAW-BAND).  LINES-CACHE is the session's rendered-line bitmap
cache (see %LOG-LINE-BITMAP)."
  (let* ((ox (ui-layout-log-x l))
         (oy (ui-layout-by l))
         (w (ui-layout-log-w l))
         (h (- (ui-layout-page-b l) oy))
         (lh +microfont-line-height+)
         (n (max 1 (floor (- h 2) lh)))
         (max-chars (max 4 (floor (- w 4) +microfont-advance+)))
         ;; Each message starts with "> "; long ones wrap onto indented
         ;; continuation lines.  Keep the trailing N display lines so
         ;; the newest stays at the bottom.
         (wrapped (mapcan (lambda (m) (wrap-message m max-chars))
                          (log-recent log n)))
         (lines (last wrapped n)))
    ;; page interior (inside the black outline)
    (amiga.gfx:set-a-pen rp 1)
    (amiga.gfx:rect-fill rp (- ox 3) oy (+ ox w -1) (+ oy h -1))
    (let ((y (+ oy (- h (* (length lines) lh)) -1)))
      (dolist (m lines)
        (%put-microfont-line rp lines-cache
                             (if (> (length m) max-chars)
                                 (subseq m 0 max-chars)
                                 m)
                             ox y)
        (incf y lh)))
    (amiga.gfx:set-a-pen rp 1)))

(defun %amiga-draw-takeover (rp lines log l &optional lines-cache)
  "An interaction taking over the message area — a location's menu or
the character sheet: LINES (microfont, wrapped) from the top of the
white page, then a rule, then the trailing log lines at the bottom, so
game feedback (a purchase, a drink) stays visible while the menu is
up.  The page interior repaints wholesale — the takeover's 'cls' — so
switching pages never leaves stale text.  LINES-CACHE as in
%AMIGA-DRAW-LOG."
  (let* ((ox (ui-layout-log-x l))
         (oy (ui-layout-by l))
         (w (ui-layout-log-w l))
         (h (- (ui-layout-page-b l) oy))
         (lh +microfont-line-height+)
         (rows (max 1 (floor (- h 2) lh)))
         (max-chars (max 4 (floor (- w 4) +microfont-advance+)))
         (menu (let ((all (mapcan (lambda (line)
                                    (wrap-menu-line line max-chars))
                                  lines)))
                 ;; a page that overflows gives up its blank spacer
                 ;; lines before it truncates content (the lores shop
                 ;; page is the tight case)
                 (if (> (length all) rows)
                     (delete-if (lambda (line)
                                  (equal (menu-line-text line) ""))
                                all)
                     all)))
         (n-menu (min (length menu) rows))
         ;; the rule and the log tail live in whatever rows the menu
         ;; leaves free (none is fine — the menu keeps the page)
         (tail-rows (max 0 (- rows n-menu 1)))
         (tail (when (plusp tail-rows)
                 (last (mapcan (lambda (m) (wrap-message m max-chars))
                               (log-recent log tail-rows))
                       tail-rows))))
    ;; page interior: the takeover's cls
    (amiga.gfx:set-a-pen rp 1)
    (amiga.gfx:rect-fill rp (- ox 3) oy (+ ox w -1) (+ oy h -1))
    (let ((y (1+ oy))
          (n 0))
      (dolist (line menu)
        (when (< n n-menu)
          (let ((text (menu-line-text line)))
            (%put-microfont-line rp lines-cache
                                 (if (> (length text) max-chars)
                                     (subseq text 0 max-chars)
                                     text)
                                 ox y)
            ;; a click on an option row / a footer hint is its key
            (%register-line-hotspots line ox y +microfont-advance+ lh
                                     (- ox 3) (+ ox w -1)))
          (incf y lh)
          (incf n))))
    ;; log tail, newest at the page bottom, under the rule
    (when tail
      (let ((y (+ oy (- h (* (length tail) lh)) -1)))
        (amiga.gfx:set-a-pen rp 0)
        (amiga.gfx:draw-line rp (- ox 2) (- y 3) (+ ox w -3) (- y 3))
        (dolist (m tail)
          (%put-microfont-line rp lines-cache
                               (if (> (length m) max-chars)
                                   (subseq m 0 max-chars)
                                   m)
                               ox y)
          (incf y lh))))
    (amiga.gfx:set-a-pen rp 1)))

(defun %amiga-hero-row (rp game l y hero index)
  "One roster table row at baseline Y, Bard's Tale columns: number,
name, armor class (with equipment and spell effects), then
max/current hit points and max/current spell points, and the class
code.  The current points are picked out in white; a downed hero's
name and hit points turn amber.  Columns come from the profile's
ROSTER-COLS character cells."
  (let* ((ox (ui-layout-bx l))
         (cw (ui-layout-cw l))
         (cols (display-profile-roster-cols *display-profile*))
         ;; the name column runs up to one cell short of the AC column
         (name-w (- (getf cols :ac) 1 (getf cols :name)))
         (down (not (hero-alive-p hero))))
    (labels ((col (cell pen text)
               (amiga.gfx:set-a-pen rp pen)
               (amiga.gfx:move-to rp (+ ox (* cw cell)) y)
               (amiga.gfx:gfx-text rp text)))
      (col (getf cols :no) 0 (format nil "~D" (1+ index)))
      (col (getf cols :name) (if down 3 0)
           (let ((name (hero-name hero)))
             (if (> (length name) name-w) (subseq name 0 name-w) name)))
      (col (getf cols :ac) 0
           (format nil "~D" (hero-effective-ac hero game)))
      (col (getf cols :hit) 0 (format nil "~D" (hero-max-hp hero)))
      (col (getf cols :hpts) (if down 3 1)
           (format nil "~D" (hero-hp hero)))
      (col (getf cols :spl) 0 (format nil "~D" (hero-max-sp hero)))
      (col (getf cols :spts) 1 (format nil "~D" (hero-sp hero)))
      (col (getf cols :cl) 0 (hero-class-abbrev hero)))
    (amiga.gfx:set-a-pen rp 1)))

(defun %amiga-party (rp game l &optional clickable)
  "Party roster table, Bard's Tale style: a header row and one numbered
row per hero, right under the location plaque (there is no status
line), black on the grey chrome with the current hit/spell points in
white.  The row number is the key that opens that hero's character
sheet; CLICKABLE non-NIL also registers each hero row as a click
target for its digit — passed only when digits currently mean the
roster (not while a menu model or a location eats them)."
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
        (col (getf cols :ac)   "AC")
        (col (getf cols :hit)  "HIT")
        (col (getf cols :hpts) "PTS")
        (col (getf cols :spl)  "SPL")
        (col (getf cols :spts) "PTS")
        (col (getf cols :cl)   "CL")))
    (let ((y (+ (ui-layout-party-y l) (ui-layout-base l)))
          (row-y (ui-layout-party-y l))
          (i 0))
      (dolist (h (game-party game))
        (%amiga-hero-row rp game l y h i)
        (when (and clickable (< i 9))
          (%hotspot (digit-char (1+ i))
                    ox row-y (ui-layout-right l) (+ row-y lh -1)))
        (incf y lh)
        (incf row-y lh)
        (incf i)))
    (amiga.gfx:set-a-pen rp 1)))

(defun %amiga-draw-help (rp l)
  "The help page ('h' or '?'): the key-mapping reference (HELP-LINES)
on a white parchment page over the grey chrome, like the character
sheet."
  (let* ((ox (ui-layout-bx l))
         (oy (ui-layout-by l))
         (lh (ui-layout-lh l))
         (cw (ui-layout-cw l))
         (lines (help-lines))
         (px (+ ox (* 2 cw)))           ; the page
         (py (+ oy 8))
         (pw (min (* 40 cw) (- (ui-layout-right l) px (* 2 cw))))
         (ph (min (- (ui-layout-bottom l) py 4)
                  (+ (* lh (length lines)) 12)))
         (max-lines (floor (- ph 8) lh))
         (max-chars (floor (- pw 16) cw)))
    (amiga.gfx:set-a-pen rp 2)
    (amiga.gfx:rect-fill rp ox oy (ui-layout-right l) (ui-layout-bottom l))
    ;; page shadow, sheet, outline — the character-sheet look
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

(defun %amiga-draw-sheet (rp game index l)
  "The INDEXth party member's stat block on a full white parchment
page over the grey chrome.  The play flow shows the sheet as a
message-area takeover instead (HERO-SHEET-LINES + the portrait in the
view column — see REDRAW); this full-page overlay variant stays for
front-ends that want it."
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

(defun %amiga-draw-page (rp menu-lines l)
  "An overlay menu page (cast, use, sing, save slots): MENU-LINES on
a white page over the view column.  The log and roster panes stay
live around it — hit/spell points and messages update as the party
acts.  Lines pack at one pixel of leading (tighter than the layout's
LH) so a full page fits above the roster on the lo-res screen.
Locations and the character sheet use the message-area takeover
instead (%AMIGA-DRAW-TAKEOVER)."
  (let* ((ox (ui-layout-bx l))
         (oy (ui-layout-by l))
         (lh (1+ (amiga.gfx:rastport-tx-height rp)))
         (cw (ui-layout-cw l))
         (px (+ ox 4))
         (py (+ oy 4))
         (pw (ui-layout-fp-w l))
         (ph (- (ui-layout-hdr-y l) 4 py))
         (max-lines (floor (- ph 8) lh))
         (max-chars (floor (- pw 16) cw))
         (lines (mapcan (lambda (line) (wrap-menu-line line max-chars))
                        menu-lines)))
    ;; page shadow, sheet, outline — same look as the character sheet
    (amiga.gfx:set-a-pen rp 0)
    (amiga.gfx:rect-fill rp (+ px 2) (+ py 2) (+ px pw 2) (+ py ph 2))
    (amiga.gfx:set-a-pen rp 1)
    (amiga.gfx:rect-fill rp px py (+ px pw) (+ py ph))
    (amiga.gfx:set-a-pen rp 0)
    (%chrome-rect rp px py (+ px pw) (+ py ph))
    (let ((y (+ py 4 (ui-layout-base l)))
          (n 0))
      (dolist (line lines)
        (when (< n max-lines)
          (let ((text (menu-line-text line)))
            (amiga.gfx:move-to rp (+ px 8) y)
            (amiga.gfx:gfx-text rp (if (> (length text) max-chars)
                                       (subseq text 0 max-chars)
                                       text))
            ;; a click on an option row / a footer hint is its key
            (%register-line-hotspots line (+ px 8)
                                     (- y (ui-layout-base l)) cw lh
                                     (1+ px) (+ px pw -1)))
          (incf y lh)
          (incf n))))
    (amiga.gfx:set-a-pen rp 1)))

(defun %amiga-draw-map-page (rp game l full)
  "Full map mode ('m'): the automap over the whole inner area, party
centered and clamped to what fits at a readable cell size.  The
two-line footer carries what the play page has no room for: the zone
title, the party position — plus the facing while a compass burns —
and the game clock (keys are on the help page)."
  (let* ((bx (ui-layout-bx l))
         (by (ui-layout-by l))
         (right (ui-layout-right l))
         (lh (ui-layout-lh l))
         (cw (ui-layout-cw l))
         (map (game-map game))
         (mw (dungeon-map-width map))
         (mh (dungeon-map-height map))
         (avail-w (- right bx))
         (avail-h (- (ui-layout-bottom l) by (* 2 lh) 6))
         (cell (max 4 (min 16
                           (floor avail-w (max mw 1))
                           (floor avail-h (max mh 1)))))
         (vw (min mw (floor avail-w cell)))
         (vh (min mh (floor avail-h cell))))
    ;; clear the whole inner area (the play panes underneath)
    (amiga.gfx:set-a-pen rp 0)
    (amiga.gfx:rect-fill rp bx by right (ui-layout-bottom l))
    (multiple-value-bind (x0 y0 w h)
        (map-viewport map (game-x game) (game-y game) vw vh)
      (%amiga-draw-map-region rp game bx by cell x0 y0 w h full cw))
    (amiga.gfx:set-a-pen rp 1)
    (let* ((base-off (- lh (ui-layout-base l)))
           (y1 (- (ui-layout-bottom l) lh base-off))  ; upper footer line
           (y2 (- (ui-layout-bottom l) base-off))     ; lower footer line
           (clock (clock-line game))
           (clock-w (* cw (length clock)))
           (place (format nil "~A  (~D,~D)~@[ ~A~]"
                          (string-capitalize (map-title map))
                          (game-x game) (game-y game)
                          (when (compass-active-p game)
                            (dir-keyword (game-facing game)))))
           (place-max (max 0 (floor (- right bx clock-w cw) cw))))
      (amiga.gfx:move-to rp bx y1)
      (amiga.gfx:gfx-text rp (if (> (length place) place-max)
                                 (subseq place 0 place-max)
                                 place))
      (amiga.gfx:move-to rp (- right clock-w) y1)
      (amiga.gfx:gfx-text rp clock)
      (amiga.gfx:move-to rp bx y2)
      (amiga.gfx:gfx-text rp (format nil "~Dx~D map~@[  FULL~]"
                                     mw mh full)))))

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
colors.  Colors 17-19 are the mouse pointer's (sprite 0): the classic
red pointer, dark outline, light gleam — unset they render the pointer
invisibly black."
  (let ((vp (amiga.intuition:screen-viewport scr)))
    (amiga.gfx:set-rgb4 vp 0 0 0 0)
    (amiga.gfx:set-rgb4 vp 1 15 15 15)
    (amiga.gfx:set-rgb4 vp 2 10 10 10)
    (amiga.gfx:set-rgb4 vp 3 15 10 3)
    (loop for pen from 4 below (ash 1 (display-profile-screen-depth
                                       *display-profile*))
          do (amiga.gfx:set-rgb4 vp pen 0 0 0))
    (amiga.gfx:set-rgb4 vp 17 14 4 4)
    (amiga.gfx:set-rgb4 vp 18 3 0 0)
    (amiga.gfx:set-rgb4 vp 19 14 14 12)))

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
         ;; :TITLE NIL, not merely omitted: on a backdrop window any
         ;; WA_Title — including OPEN-WINDOW's default — still costs a
         ;; title bar (border-top), and the screen already carries one
         (amiga.intuition:with-window
             (win :title nil
                  :left 0 :top 0
                  :width (amiga.intuition:screen-width scr)
                  :height (amiga.intuition:screen-height scr)
                  :screen scr
                  :flags (logior amiga.intuition:+wflg-borderless+
                                 amiga.intuition:+wflg-backdrop+
                                 amiga.intuition:+wflg-activate+)
                  :idcmp +game-idcmp+)
           ;; the game owns the whole display: put the OS screen bar
           ;; behind the backdrop window (Bard's Tale has no title
           ;; bar).  This must run AFTER the window is open — ShowTitle
           ;; rearranges the layers of the backdrop windows that exist
           ;; at call time, and on RTG-promoted screens (Picasso96
           ;; BestModeIDA modes) a backdrop window opened after the
           ;; call still sat behind the bar layer: the title bar stayed
           ;; visible in play even with SA_ShowTitle FALSE at
           ;; OpenScreen.
           (amiga.intuition:show-title scr nil)
           (funcall fn scr win)))))))

;;; ---------------------------------------------------------------------
;;; The game proper

(defun play-amiga (map-file
                   &key (display :screen) (profile *display-profile*)
                     gfx-dir)
  "Interactive walkabout on MAP-FILE.  Loads the campaign.lisp next to
the map file (classes, monsters, items, party) when present — a
designer's own world directory brings its own campaign; the engine has
no default world.
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
F toggles the debug full view there), H or ? the help page (the key
reference — H/Esc leaves), 1-7 open a party member's character sheet
(1-7 switch heroes there, Esc leaves), C cast a spell (pick
caster/spell/target by number, Esc backs out), Q/Esc quit; in combat
A attack, D defend, C cast, F flee; in a location (shop) 1-9 choose,
S/B switch sell/buy, Esc back/leave — the location menu and the
character sheet take over the message area, with the location's
:image / the hero's portrait in the view column when the campaign
ships one.  Shift-S / Shift-L (and the
menu strip's Save/Load, right mouse button) open the save-slot
picker: 1-9 pick a slot, N names a new save (saves/NAME.sav), Esc
cancels; Quit sits in the menu strip too.
Everything key-driven clicks too: the view walks (left/right quarters
turn, the middle steps forward, its bottom band back), a roster row
opens that character sheet, a menu's numbered rows pick and its
bracket hints ([s] sell, [Esc] back) act as their keys, and the
map/help/sheet pages close on a click outside a target — see
*HOTSPOTS*."
  (load-campaign map-file)
  (with-display-profile (profile)
   (dlog "play-amiga ~A display ~S profile ~S"
         map-file display (display-profile-name *display-profile*))
   (let* ((*gfx-dir* (or gfx-dir *gfx-dir*))
         (map (load-map-file map-file))
         (game nil)
         (log nil)
         (mode :play)       ; :play, :map (full map view), :sheet or :help
         (full nil)         ; omniscient map (debug), map mode only
         (sheet-hero 0)     ; party index shown in :sheet mode
         (help-prior-mode :play) ; mode to return to when help closes
         (shopv nil)        ; SHOP-VIEW while inside a location
         (castv nil)        ; CAST-VIEW while the cast menu is open
         (usev nil)         ; USE-VIEW while the use menu is open
         (singv nil)        ; SING-VIEW while the sing menu is open
         (savem nil)        ; SAVE-MENU while the save/load picker is open
         (saves-prior-mode :play) ; mode to return to when the picker closes
         (zone-dirty nil)   ; party traveled: the chrome needs a repaint
         (over nil))
    (labels ((wire (g)
               (setf log (attach-message-log g))
               (setf shopv (when (game-location g) (make-shop-view)))
               (setf castv nil)
               (setf usev nil)
               (setf singv nil)
               (setf savem nil)
               (on-event g :enter-location
                         (lambda (gm loc) (declare (ignore gm loc))
                           (setf shopv (make-shop-view))))
               (on-event g :leave-location
                         (lambda (gm loc) (declare (ignore gm loc))
                           (setf shopv nil)))
               (on-event g :enter-zone
                         (lambda (gm map) (declare (ignore gm map))
                           (setf zone-dirty t)))
               ;; the status line is gone, so the prompts it used to
               ;; carry go to the log instead
               (on-event g :combat-start
                         (lambda (gm monsters)
                           (declare (ignore gm monsters))
                           (log-message
                            log "A atk  D def  C cast  P play  F flee")))
               (on-event g :game-won
                         (lambda (gm) (declare (ignore gm))
                           (setf over :won)
                           (log-message log "You win!  Press Q.")))
               (on-event g :party-defeated
                         (lambda (gm) (declare (ignore gm))
                           (setf over :lost)
                           (log-message log "Game over.  Press Q.")))
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
                       (walls-dir nil)  ; the pack they came from
                       (icons (make-hash-table :test #'equal))
                                        ; image cache: effect icons,
                                        ; location pictures, portraits
                       (log-lines (make-hash-table :test #'equal)))
                                        ; rendered log-line bitmap cache
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
                          ;; no say over the Workbench palette.  The
                          ;; load reads a directory of ILBMs — seconds
                          ;; at 14MHz — so the busy pointer is up.
                          (let ((want (effective-gfx-dir)))
                            (unless (equal want walls-dir)
                              (%call-with-busy-pointer win
                               (lambda ()
                                 (setf walls (%free-wall-assets walls)
                                       walls-dir want)
                                 (let ((*gfx-dir* want))
                                   (multiple-value-bind (w pal)
                                       (%load-wall-assets rp log)
                                     (setf walls w)
                                     (when (eq display :screen)
                                       (%apply-pack-palette
                                        scr pal))
                                     ;; the pack may carry its own
                                     ;; pointer.iff; re-showing also
                                     ;; re-latches the sprite colors
                                     ;; after the palette change (RTG)
                                     (%ensure-standard-pointer
                                      scr win display))))))))
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
                        (fresh-play (&optional (target-mode :play))
                          ;; back to the play page (or, when a picker was
                          ;; opened over the map/sheet view via the menu
                          ;; strip, back to that view): chrome + panes
                          (setf mode target-mode)
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
                          ;; the sheet lives in the panes (takeover +
                          ;; portrait) — no chrome to repair
                          (setf mode :play)
                          (redraw))
                        (open-help ()
                          ;; 'h'/'?' from play or map mode: remember
                          ;; where to return
                          (setf help-prior-mode mode
                                mode :help)
                          (redraw))
                        (leave-help ()
                          (fresh-play help-prior-mode))
                        (menus-idle-p ()
                          ;; no menu model is eating the keys
                          (not (or savem castv usev singv
                                   (game-location game))))
                        (redraw ()
                          ;; travel switched zones: swap in the zone's
                          ;; tile pack and repaint the chrome first
                          ;; (the plaque carries the zone title)
                          (when zone-dirty
                            (setf zone-dirty nil)
                            (ensure-walls)
                            (clear-inner)
                            (%chrome-frames rp game l))
                          ;; the click targets are rebuilt with the
                          ;; frame: a full-page catch-all (leave) first
                          ;; where the mode has one, the renderers'
                          ;; specific targets on top of it
                          (setf *hotspots* '())
                          (when (member mode '(:map :help :sheet))
                            (%hotspot :esc
                                      (ui-layout-bx l) (ui-layout-by l)
                                      (ui-layout-right l)
                                      (ui-layout-bottom l)))
                          (cond
                            ((eq mode :map)
                             (%amiga-draw-map-page rp game l full))
                            ((eq mode :help)
                             (%amiga-draw-help rp l))
                            (t
                             ;; The view column: an overlay menu page
                             ;; (save picker, cast/use/sing), else the
                             ;; takeover's picture (the location's
                             ;; :image, the sheet hero's portrait) with
                             ;; the live first-person view as the
                             ;; fallback when there is no picture.
                             (cond (savem
                                    (%amiga-draw-page
                                     rp (save-menu-lines game savem) l))
                                   (castv
                                    (%amiga-draw-page
                                     rp (cast-lines game castv) l))
                                   (usev
                                    (%amiga-draw-page
                                     rp (use-lines game usev) l))
                                   (singv
                                    (%amiga-draw-page
                                     rp (sing-lines game singv) l))
                                   (t
                                    (let ((picture
                                            (cond ((eq mode :sheet)
                                                   (let ((hero (nth sheet-hero
                                                                    (game-party
                                                                     game))))
                                                     (when hero
                                                       (hero-image-path
                                                        game hero))))
                                                  ((game-location game)
                                                   (location-image-path
                                                    game)))))
                                      (unless (%amiga-draw-picture
                                               rp icons picture l log)
                                        (%amiga-draw-fp rp game
                                                        (ui-layout-bx l)
                                                        (ui-layout-by l)
                                                        (ui-layout-fp-w l)
                                                        (ui-layout-fp-h l)
                                                        walls)))
                                    (%amiga-draw-band rp game l icons log)))
                             ;; click-to-walk zones on the view — only
                             ;; while W/A/S/D actually walk (no menu or
                             ;; location eating keys, not in combat)
                             (when (and (eq mode :play) (menus-idle-p)
                                        (not (game-combat game))
                                        (not over))
                               (%register-move-zones l))
                             ;; The message area: taken over by the
                             ;; character sheet or the location's menu
                             ;; (log tail below the rule), else the log.
                             (cond ((eq mode :sheet)
                                    (%amiga-draw-takeover
                                     rp (hero-sheet-lines game sheet-hero)
                                     log l log-lines))
                                   ((game-location game)
                                    (%amiga-draw-takeover
                                     rp
                                     (location-lines
                                      game
                                      (or shopv
                                          (setf shopv (make-shop-view))))
                                     log l log-lines))
                                   (t
                                    (%amiga-draw-log rp log l log-lines)))
                             ;; roster rows click as their digits when
                             ;; digits mean the roster (sheet picks)
                             (%amiga-party rp game l
                                           (and (menus-idle-p)
                                                (not (game-combat game))
                                                (not over))))))
                        (%step (relative)
                          ;; Log the notable step results; plain steps
                          ;; stay quiet so the log tracks events, not
                          ;; every footfall.
                          (case (move-party game relative)
                            (:door (say game "You pass through a door."))
                            (:blocked (say game "You bump into a wall."))))
                        (open-cast (in-combat)
                          (if (some #'hero-caster-p (alive-heroes game))
                              (setf castv
                                    (make-cast-view :in-combat in-combat))
                              (log-message log "No one here can cast."))
                          (redraw))
                        (cast-menu-act (c)
                          (let ((key (if (eq c :esc) #\Escape c)))
                            (when (characterp key)
                              (case (cast-act game castv key)
                                ((:done :cancelled)
                                 (setf castv nil)
                                 (fresh-play)
                                 (return-from cast-menu-act nil)))))
                          (redraw)
                          nil)
                        (open-use ()
                          (if (some #'usable-items (alive-heroes game))
                              (setf usev (make-use-view))
                              (log-message log
                                           "No one carries anything to use."))
                          (redraw))
                        (use-menu-act (c)
                          (let ((key (if (eq c :esc) #\Escape c)))
                            (when (characterp key)
                              (case (use-act game usev key)
                                ((:done :cancelled)
                                 (setf usev nil)
                                 (fresh-play)
                                 (return-from use-menu-act nil)))))
                          (redraw)
                          nil)
                        (open-sing (in-combat)
                          (if (some #'hero-singer-p (alive-heroes game))
                              (setf singv
                                    (make-sing-view :in-combat in-combat))
                              (log-message log "No one here can play."))
                          (redraw))
                        (sing-menu-act (c)
                          (let ((key (if (eq c :esc) #\Escape c)))
                            (when (characterp key)
                              (case (sing-act game singv key)
                                ((:done :cancelled)
                                 (setf singv nil)
                                 (fresh-play)
                                 (return-from sing-menu-act nil)))))
                          (redraw)
                          nil)
                        (open-saves (menu-mode)
                          ;; S/L keys and the GadTools Save/Load items
                          ;; all land here; the picker draws over the
                          ;; view column like a shop page.  The menu
                          ;; strip can trigger this from :map/:sheet
                          ;; mode too, so remember where to return.
                          (setf saves-prior-mode mode)
                          (setf savem (make-save-menu menu-mode)
                                mode :play)
                          (clear-inner)
                          (%chrome-frames rp game l)
                          (redraw))
                        (saves-act (c)
                          (let* ((key (if (eq c :esc) #\Escape c))
                                 (r (when (characterp key)
                                      (save-menu-act game savem key))))
                            (cond ((eq r :closed)
                                   (setf savem nil)
                                   (fresh-play saves-prior-mode))
                                  ((and (consp r) (eq (first r) :save))
                                   (ensure-save-dir)
                                   (save-game game (second r))
                                   (log-message
                                    log
                                    (format nil "Saved ~A." (second r)))
                                   (setf savem nil)
                                   (fresh-play saves-prior-mode))
                                  ((and (consp r) (eq (first r) :load))
                                   (%call-with-busy-pointer win
                                    (lambda ()
                                      (setf game
                                            (wire (load-game (second r))))
                                      (setf over nil
                                            mode :play
                                            zone-dirty nil)
                                      ;; loading may land in a zone
                                      ;; with its own tile pack — swap
                                      ;; packs and repaint the chrome
                                      ;; (the plaque carries the zone
                                      ;; name)
                                      (ensure-walls)))
                                   (clear-inner)
                                   (%chrome-frames rp game l)
                                   (log-message log "Game loaded.")
                                   (redraw))
                                  (t (redraw))))
                          nil)
                        (act (c)
                          "Handle key C; :quit means leave the event loop."
                          (dlog "key ~S mode ~S" c mode)
                          (let ((lc (if (characterp c) (char-downcase c) c)))
                            (cond ((eq mode :map)
                                   (cond ((eql lc #\q) :quit)
                                         ((or (eql lc #\m) (eql c :esc))
                                          (leave-map)
                                          nil)
                                         ((eql lc #\f)
                                          (setf full (not full))
                                          (redraw)
                                          nil)
                                         ((or (eql lc #\h) (eql c #\?))
                                          (open-help)
                                          nil)))
                                  ((eq mode :help)
                                   (cond ((eql lc #\q) :quit)
                                         ((or (eql lc #\h) (eql c #\?)
                                              (eql c :esc))
                                          (leave-help)
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
                                  (savem
                                   ;; save/load picker: the shared model
                                   ;; eats every key — digits pick slots
                                   ;; and letters are name characters,
                                   ;; so S/L/q cannot leak through
                                   (saves-act c))
                                  (castv
                                   ;; cast menu: the shared model eats
                                   ;; every key (digits pick, Esc backs
                                   ;; out) — see spells.lisp
                                   (cast-menu-act c))
                                  (usev
                                   ;; use menu: same shape — see items.lisp
                                   (use-menu-act c))
                                  (singv
                                   ;; sing menu: same shape — see songs.lisp
                                   (sing-menu-act c))
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
                                         ;; the location lives in the
                                         ;; panes too — leaving needs
                                         ;; only a redraw (a trapdoor
                                         ;; travel sets zone-dirty and
                                         ;; redraw repaints the chrome)
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
                                     (#\c (open-cast t))
                                     (#\p (open-sing t))
                                     (#\f (attempt-flee game) (redraw)))
                                   nil)
                                  ((eql c #\S) (open-saves :save) nil)
                                  ((eql c #\L) (open-saves :load) nil)
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
                                     (#\c (open-cast nil))
                                     (#\u (open-use))
                                     (#\p (open-sing nil))
                                     (#\h (open-help))
                                     (#\? (open-help))
                                     (#\m (setf mode :map)
                                          (redraw)))
                                   nil)))))
                 (let ((*pointer-window* win))
                  (unwind-protect
                     (progn
                       ;; the hand pointer is up before the first
                       ;; (busy-bracketed) tile-pack load, so the busy
                       ;; pointer has something to restore to
                       (%ensure-standard-pointer scr win display)
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
                                 (0 (open-saves :save))
                                 (1 (open-saves :load))
                                 (3 (return))))))
                         (amiga.intuition:+idcmp-vanillakey+ (msg)
                           (let* ((code (amiga.intuition:msg-code msg))
                                  (c (if (= code 27) :esc (code-char code))))
                             (when (eq (act c) :quit)
                               (return))))
                         (amiga.intuition:+idcmp-mousebuttons+ (msg)
                           ;; a left click acts as the key registered
                           ;; under the pointer (see *HOTSPOTS*)
                           (when (= (amiga.intuition:msg-code msg)
                                    amiga.intuition:+selectdown+)
                             (let ((c (%hotspot-at
                                       (amiga.intuition:msg-mouse-x msg)
                                       (amiga.intuition:msg-mouse-y msg))))
                               (dlog "click ~Dx~D -> ~S"
                                     (amiga.intuition:msg-mouse-x msg)
                                     (amiga.intuition:msg-mouse-y msg)
                                     c)
                               (when (and c (eq (act c) :quit))
                                 (return)))))
                         (amiga.intuition:+idcmp-intuiticks+ (msg)
                           (when *autoplay*
                             ;; a scripted entry is a key, or a
                             ;; (:click X Y) resolved through the same
                             ;; hotspot map as a real button event
                             (let* ((entry (pop *autoplay*))
                                    (c (if (and (consp entry)
                                                (eq (first entry) :click))
                                           (%hotspot-at (second entry)
                                                        (third entry))
                                           entry)))
                               (when (and c (eq (act c) :quit))
                                 (return)))))))
                   (%free-standard-pointer win)
                   (setf walls (%free-wall-assets walls)
                         icons (%free-images icons)
                         log-lines (%free-log-lines log-lines))))))))))))))))
