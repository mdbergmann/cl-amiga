;;; Lambda's Tale — procedural wall-art generator (M3).
;;;
;;; Draws every wall piece named by WALL-PIECE-NAMES (depth-3 images:
;;; pen 0 transparent, plus white/grey/amber/black), plus the demo
;;; ceiling/floor backdrops (BACKDROP-RECTS, opaque depth-3), sized to
;;; their fixed screen slots for the *FP-VIEW-WIDTH* x *FP-VIEW-HEIGHT*
;;; perspective planes, and writes them as ILBM files into data/gfx/.
;;; The receding side pieces leave their ceiling/floor corners at pen 0
;;; so the backdrop shows through them (cookie-cut blit on the Amiga).
;;;
;;; Definitions only — load src/load.lisp first, then this file, then
;;; call (generate-wall-assets).  tools/make-assets.lisp is the script
;;; form (`make assets`); the test suite loads this file and compares
;;; the checked-in assets pixel-for-pixel against freshly drawn pieces,
;;; so the files in data/gfx/ can never silently drift from this code.
;;;
;;; Style: grey brick walls with black mortar in perspective (courses
;;; and joints shrink with distance, side-wall courses converge along
;;; the receding edges), white edge highlights carrying the wireframe
;;; look over, amber doors.  The floor is one solid grey (no pattern,
;;; no distance shading); the ceiling is solid distance bands darkening
;;; toward the horizon, split at the perspective-plane rows so each
;;; band lines up with the corridor cell at its depth.

(in-package :tale)

;;; The dungeon palette, 8-bit components (SET-RGB4 nibbles x 17):
;;; pen 0 background, pen 1 white, pen 2 grey, pen 3 amber, pen 4 black,
;;; pen 5 the solid floor grey, pens 6-7 the ceiling's distance bands.
;;; Pen 0 is the TRANSPARENT key in wall pieces (the ceiling/floor
;;; backdrop shows through it), so opaque black inside a wall — mortar,
;;; joints, door frames — is drawn with pen 4, not pen 0.  The backdrops
;;; (ceiling/floor) are opaque and keep pen 0 as plain black.
(defparameter *wall-palette*
  #((0 0 0) (255 255 255) (136 136 136) (255 170 51)
    (0 0 0) (85 85 85) (51 51 51) (34 34 34)))

(defconstant +pen-bg+ 0)      ; transparent key in wall pieces
(defconstant +pen-edge+ 1)
(defconstant +pen-brick+ 2)
(defconstant +pen-door+ 3)
(defconstant +pen-mortar+ 4)  ; opaque black: mortar, joints, door frames
(defconstant +pen-mid+ 5)     ; mid grey: the solid floor
(defconstant +pen-dim+ 6)     ; dim grey: near ceiling band
(defconstant +pen-dark+ 7)    ; dark grey: mid ceiling band — aliases
                               ; +pen-plaster+ below; see the WARNING
                               ; above DRAW-CITY-WALL-PIECE

(defparameter *brick-courses* 6
  "Brick courses over a full wall height.")

;;; ---------------------------------------------------------------------
;;; Small image helpers

(defun %img-fill (img x0 y0 x1 y1 pen)
  "Fill the inclusive rect, clipped to IMG."
  (loop for y from (max 0 y0) to (min y1 (1- (image-height img)))
        do (loop for x from (max 0 x0) to (min x1 (1- (image-width img)))
                 do (setf (pixel-ref img x y) pen))))

(defun %img-mirror-x (img)
  "New image: IMG flipped horizontally (for the :r wall pieces)."
  (let* ((w (image-width img))
         (out (make-image w (image-height img) (image-depth img)
                          :palette (image-palette img))))
    (dotimes (y (image-height img) out)
      (dotimes (x w)
        (setf (pixel-ref out (- w 1 x) y) (pixel-ref img x y))))))

;;; ---------------------------------------------------------------------
;;; Front-facing walls (:front, :flank — flat rectangles)

(defun %draw-front-wall (w h &key door (pattern-w w) (pattern-x0 0))
  "A flat brick wall filling W x H (front and flank pieces share the
wall height at a given depth, so their brick courses line up).  The
brick length comes from PATTERN-W and the piece's horizontal placement
within the wall's bond pattern from PATTERN-X0: a front piece is its
own pattern (defaults), a flank piece passes the front slot's width
and its offset from the front's left edge, so its bricks continue the
front wall's coursing across the seam at the same scale.  (Flanks
used to size bricks to their own narrow slot — the same flat wall at
the same depth showed bricks three times smaller beside the front
piece.)  DOOR non-NIL puts an amber door in the middle."
  ;; Front and flank pieces fill their whole rectangle (no pen 0 left),
  ;; so they are fully opaque — the backdrop never shows through them.
  (let ((img (make-image w h 3 :palette *wall-palette*))
        (course (max 3 (round h *brick-courses*)))
        (brick (max 6 (round pattern-w 5))))
    (labels ((joints (row-offset y0 y1)
               ;; one course row's vertical joints: the global pattern
               ;; columns congruent to ROW-OFFSET mod BRICK, clipped to
               ;; this piece's [PATTERN-X0, PATTERN-X0+W) window
               (loop for gx from (+ pattern-x0
                                    (mod (- row-offset pattern-x0) brick))
                       below (+ pattern-x0 w) by brick
                     do (%img-fill img (- gx pattern-x0) y0
                                   (- gx pattern-x0) y1 +pen-mortar+))))
      (%img-fill img 0 0 (1- w) (1- h) +pen-brick+)
      ;; mortar: courses and running-bond joints
      (loop for y from course below h by course
            for row from 1
            do (%img-fill img 0 y (1- w) y +pen-mortar+)
               (joints (if (evenp row) 0 (floor brick 2))
                       (- y course) (1- y)))
      (joints (floor brick 2)
              (* course (floor (1- h) course)) (1- h)))
    ;; white edge highlight all around (the wireframe look)
    (%img-fill img 0 0 (1- w) 0 +pen-edge+)
    (%img-fill img 0 (1- h) (1- w) (1- h) +pen-edge+)
    (%img-fill img 0 0 0 (1- h) +pen-edge+)
    (%img-fill img (1- w) 0 (1- w) (1- h) +pen-edge+)
    (when door
      (%draw-front-door img w h))
    img))

(defun %front-door-rect (w h)
  "The front-piece door's placement (VALUES DX DY DW DH) — shared so
window layouts can keep clear of the door."
  (let* ((dw (max 3 (floor w 3)))
         (dh (max 4 (floor (* 3 h) 4)))
         (dx (floor (- w dw) 2))
         (dy (- h 1 dh)))
    (values dx dy dw dh)))

(defun %draw-front-door (img w h)
  "The amber door with black frame and knob in the middle of a front
piece — shared by the dungeon brick and the city house fronts."
  (multiple-value-bind (dx dy dw dh) (%front-door-rect w h)
    (%img-fill img dx dy (+ dx dw -1) (- h 2) +pen-door+)
    ;; black frame + knob
    (%img-fill img dx dy (+ dx dw -1) dy +pen-mortar+)
    (%img-fill img dx dy dx (- h 2) +pen-mortar+)
    (%img-fill img (+ dx dw -1) dy (+ dx dw -1) (- h 2) +pen-mortar+)
    (when (> dh 8)
      (let ((ky (+ dy (floor dh 2))))
        (%img-fill img (+ dx dw -3) ky (+ dx dw -3) (1+ ky)
                   +pen-mortar+)))))

;;; ---------------------------------------------------------------------
;;; Receding side walls (:side — trapezoids with baked ceiling/floor)

(defun %draw-side-wall (w h top-far bot-far &key door)
  "A left-hand receding wall in a W x H band: the near edge (x = 0)
spans the full height, the far edge (x = W-1) spans TOP-FAR..BOT-FAR.
The ceiling and floor corners stay pen 0 (transparent) — the backdrop
shows through them via the cookie-cut blit.  DOOR puts a
perspective-skewed amber door on the wall.  Right-hand pieces are the
mirror image (see %IMG-MIRROR-X)."
  ;; Depth 3: the wall itself uses the opaque black mortar pen (4); only
  ;; the untouched corners keep pen 0, which is what makes them transparent.
  (let ((img (make-image w h 3 :palette *wall-palette*)))
    (labels ((top-at (x) (round (* top-far x) (max 1 (1- w))))
             (bot-at (x) (- (1- h) (round (* (- (1- h) bot-far) x)
                                          (max 1 (1- w))))))
      ;; wall fill
      (dotimes (x w)
        (loop for y from (top-at x) to (bot-at x)
              do (setf (pixel-ref img x y) +pen-brick+)))
      ;; mortar courses converge along the receding edges
      (loop for k from 1 below *brick-courses*
            do (dotimes (x w)
                 (let* ((top (top-at x))
                        (bot (bot-at x))
                        (y (+ top (round (* k (- bot top)) *brick-courses*))))
                   (setf (pixel-ref img x y) +pen-mortar+))))
      ;; vertical joints, running bond per course
      (let ((brick (max 6 (round w 3))))
        (loop for k below *brick-courses*
              do (loop for x from (if (evenp k) (floor brick 2) brick)
                         below w by brick
                       do (let* ((top (top-at x))
                                 (bot (bot-at x))
                                 (y0 (+ top (round (* k (- bot top))
                                                   *brick-courses*)))
                                 (y1 (+ top (round (* (1+ k) (- bot top))
                                                   *brick-courses*))))
                            (loop for y from y0 to (min y1 (bot-at x))
                                  do (setf (pixel-ref img x y)
                                           +pen-mortar+))))))
      ;; door: skewed onto the wall, standing on the floor edge
      (when door
        (let ((dx0 (floor w 4))
              (dx1 (floor (* 3 w) 4)))
          (loop for x from dx0 to dx1
                do (let* ((top (top-at x))
                          (bot (bot-at x))
                          (dtop (- bot (floor (* 3 (- bot top)) 4))))
                     (loop for y from dtop below bot
                           do (setf (pixel-ref img x y)
                                    (if (or (= x dx0) (= x dx1) (= y dtop))
                                        +pen-mortar+
                                        +pen-door+)))))))
      ;; white edge highlights: receding ceiling/floor edges + verticals
      (dotimes (x w)
        (setf (pixel-ref img x (top-at x)) +pen-edge+)
        (setf (pixel-ref img x (bot-at x)) +pen-edge+))
      (loop for y from (top-at 0) to (bot-at 0)
            do (setf (pixel-ref img 0 y) +pen-edge+))
      (loop for y from (top-at (1- w)) to (bot-at (1- w))
            do (setf (pixel-ref img (1- w) y) +pen-edge+)))
    img))

;;; ---------------------------------------------------------------------
;;; City house walls: the Bard's Tale street look — every wall piece a
;;; timber-framed house (thatch roof band, plaster with dark timber
;;; framing, lit amber windows, stone foundation) instead of dungeon
;;; brick.  Same slot geometry, transparency rules and pen contract as
;;; the brick pieces (pen 0 transparent in side corners, pen 4 the
;;; opaque black frame); the house's own colors live in pens 7-9 —
;;; a city pack merges *HOUSE-COLORS* into its palette.
;;;
;;; WARNING — pen 7 aliases +PEN-DARK+ (the dungeon ceiling's mid
;;; band, see above): a pack's palette is one shared Amiga CMAP, so a
;;; city pack that draws its ceiling/floor with DRAW-BACKDROP-PIECE
;;; (which paints pens +PEN-DIM+/+PEN-DARK+, i.e. 6/7) while also
;;; using DRAW-CITY-WALL-PIECE (which paints +PEN-PLASTER+ at the same
;;; pen 7) will silently render house walls in the ceiling's dark grey
;;; instead of plaster, or vice versa.  A city pack MUST draw its own
;;; ceiling/floor backdrop instead of calling DRAW-BACKDROP-PIECE — see
;;; the hand-drawn flat night-sky/street backdrop in make-pack.lisp
;;; (Closure's worlds/closure/gfx/), which exists for this reason as
;;; well as the stylistic ones documented there.

(defconstant +pen-plaster+ 7)   ; cream plaster (city house pieces)
(defconstant +pen-timber+ 8)    ; dark timber framing
(defconstant +pen-roof+ 9)      ; thatch roof band

(defparameter *house-colors*
  '((7 (238 221 187)) (8 (119 85 51)) (9 (187 153 85)))
  "(PEN (R G B)) entries for the house wall pieces — a city pack
copies these into pens 7-9 of its palette (see make-pack.lisp in
Closure's worlds/closure/gfx/).  Pen 7 aliases +PEN-DARK+ (the
dungeon ceiling band, see the WARNING above DRAW-CITY-WALL-PIECE) —
a city pack must not also call DRAW-BACKDROP-PIECE.")

(defun %house-bands (h)
  "The house front's horizontal bands for a piece H tall:
\(VALUES ROOF-BOTTOM FOUNDATION-TOP) — rows 0..ROOF-BOTTOM are thatch,
FOUNDATION-TOP..H-1 stone."
  (values (max 1 (floor h 6))
          (- (1- h) (max 1 (floor h 10)))))

(defun %draw-house-front (w h palette &key door (pattern-w w)
                                           (pattern-x0 0))
  "A timber-framed house front filling W x H: roof band, plaster with
timber posts and rails, lit windows, stone foundation.  PATTERN-W and
PATTERN-X0 place the piece inside the front slot's post rhythm the
same way the brick bond works — a flank continues the front wall's
framing across the seam.  DOOR puts the shared amber door in the
middle; windows keep clear of it."
  (let ((img (make-image w h 4 :palette palette)))
    (multiple-value-bind (roof found) (%house-bands h)
      (let* ((post (max 8 (round pattern-w 4)))
             (wall-top (1+ roof))
             (mid (+ wall-top (floor (- found wall-top) 2))))
        ;; plaster body, thatch band, stone foundation
        (%img-fill img 0 0 (1- w) (1- h) +pen-plaster+)
        (%img-fill img 0 0 (1- w) roof +pen-roof+)
        (%img-fill img 0 found (1- w) (1- h) +pen-brick+)
        ;; eave and sill lines
        (%img-fill img 0 roof (1- w) roof +pen-mortar+)
        (%img-fill img 0 found (1- w) found +pen-mortar+)
        ;; timber posts on the global pattern grid (2px), plus a
        ;; mid-rail when the wall band has the room
        (loop for gx from (+ pattern-x0 (mod (- pattern-x0) post))
                below (+ pattern-x0 w) by post
              do (%img-fill img (- gx pattern-x0) wall-top
                            (1+ (- gx pattern-x0)) (1- found)
                            +pen-timber+))
        (when (> (- found wall-top) 12)
          (%img-fill img 0 mid (1- w) mid +pen-timber+))
        ;; lit windows: one per full post interval, in the band above
        ;; the mid-rail, skipping any interval the door occupies
        (multiple-value-bind (dx dy dw dh) (%front-door-rect w h)
          (declare (ignore dy dh))
          (when (and (>= post 10) (> (- mid wall-top) 6))
            (loop for gx from (+ pattern-x0 (mod (- pattern-x0) post))
                    below (+ pattern-x0 w -1) by post
                  do (let* ((x0 (+ (- gx pattern-x0) 4))
                            (x1 (- (+ (- gx pattern-x0) post) 4))
                            (y0 (+ wall-top 3))
                            (y1 (- mid 3)))
                       (when (and (< x1 w) (>= x0 0) (> (- x1 x0) 2)
                                  (not (and door
                                            (<= x0 (+ dx dw))
                                            (<= (1- dx) x1))))
                         (%img-fill img x0 y0 x1 y1 +pen-door+)
                         (%img-fill img x0 y0 x1 y0 +pen-mortar+)
                         (%img-fill img x0 y1 x1 y1 +pen-mortar+)
                         (%img-fill img x0 y0 x0 y1 +pen-mortar+)
                         (%img-fill img x1 y0 x1 y1 +pen-mortar+))))))
        ;; white edge highlight all around (the engine's piece look)
        (%img-fill img 0 0 (1- w) 0 +pen-edge+)
        (%img-fill img 0 (1- h) (1- w) (1- h) +pen-edge+)
        (%img-fill img 0 0 0 (1- h) +pen-edge+)
        (%img-fill img (1- w) 0 (1- w) (1- h) +pen-edge+)
        (when door
          (%draw-front-door img w h))))
    img))

(defun %draw-house-side (w h top-far bot-far palette &key door)
  "A left-hand receding house wall in a W x H band — the trapezoid of
%DRAW-SIDE-WALL styled as a house: thatch and foundation bands follow
the receding edges, timber posts span between them, a lit window sits
in each clear post interval.  Ceiling/floor corners stay pen 0
\(transparent).  Right-hand pieces mirror (see %IMG-MIRROR-X)."
  (let ((img (make-image w h 4 :palette palette)))
    (labels ((top-at (x) (round (* top-far x) (max 1 (1- w))))
             (bot-at (x) (- (1- h) (round (* (- (1- h) bot-far) x)
                                          (max 1 (1- w)))))
             (roof-at (x) (+ (top-at x)
                             (max 1 (floor (- (bot-at x) (top-at x)) 6))))
             (found-at (x) (- (bot-at x)
                              (max 1 (floor (- (bot-at x) (top-at x))
                                            10)))))
      ;; fill: thatch above the eave, stone below the sill, plaster
      ;; between; eave/sill in the black frame pen
      (dotimes (x w)
        (loop for y from (top-at x) to (bot-at x)
              do (setf (pixel-ref img x y)
                       (cond ((< y (roof-at x)) +pen-roof+)
                             ((= y (roof-at x)) +pen-mortar+)
                             ((> y (found-at x)) +pen-brick+)
                             ((= y (found-at x)) +pen-mortar+)
                             (t +pen-plaster+)))))
      ;; timber posts between eave and sill — house-sized bays, not a
      ;; fence (the near side slot is only 24px wide at lores)
      (let ((post (max 10 (round w 2))))
        (loop for x from (floor post 2) below w by post
              do (loop for y from (1+ (roof-at x)) below (found-at x)
                       do (setf (pixel-ref img x y) +pen-timber+))
                 (when (< (1+ x) w)
                   (loop for y from (1+ (roof-at (1+ x)))
                           below (found-at (1+ x))
                         do (setf (pixel-ref img (1+ x) y)
                                  +pen-timber+))))
        ;; a lit window in each bay wide enough, upper wall band
        (when (and (not door) (>= post 10))
          (loop for x0 from (+ (floor post 2) 3) below (- w 3) by post
                do (let ((x1 (min (- w 4) (- (+ x0 post) 4))))
                     (when (> (- x1 x0) 2)
                       (loop for x from x0 to x1
                             do (let* ((top (roof-at x))
                                       (bot (found-at x))
                                       (y0 (+ top 2
                                              (floor (- bot top) 8)))
                                       (y1 (+ top
                                              (floor (* 3 (- bot top))
                                                     7))))
                                  (loop for y from y0 to y1
                                        do (setf (pixel-ref img x y)
                                                 (if (or (= x x0) (= x x1)
                                                         (= y y0) (= y y1))
                                                     +pen-mortar+
                                                     +pen-door+))))))))))
      ;; door: skewed onto the wall, standing on the floor edge (the
      ;; brick side's geometry)
      (when door
        (let ((dx0 (floor w 4))
              (dx1 (floor (* 3 w) 4)))
          (loop for x from dx0 to dx1
                do (let* ((top (top-at x))
                          (bot (bot-at x))
                          (dtop (- bot (floor (* 3 (- bot top)) 4))))
                     (loop for y from dtop below bot
                           do (setf (pixel-ref img x y)
                                    (if (or (= x dx0) (= x dx1) (= y dtop))
                                        +pen-mortar+
                                        +pen-door+)))))))
      ;; white edge highlights, as on every piece
      (dotimes (x w)
        (setf (pixel-ref img x (top-at x)) +pen-edge+)
        (setf (pixel-ref img x (bot-at x)) +pen-edge+))
      (loop for y from (top-at 0) to (bot-at 0)
            do (setf (pixel-ref img 0 y) +pen-edge+))
      (loop for y from (top-at (1- w)) to (bot-at (1- w))
            do (setf (pixel-ref img (1- w) y) +pen-edge+)))
    img))

(defun draw-city-wall-piece (piece planes palette)
  "Draw PIECE at its slot size for PLANES in the city house style —
the twin of DRAW-WALL-PIECE for city tile packs.  PALETTE must carry
*HOUSE-COLORS* in pens 7-9 (pens 0-4 fixed as everywhere)."
  (destructuring-bind (kind depth &optional side) piece
    (destructuring-bind (px0 py0 px1 py1) (aref planes depth)
      (declare (ignore px0 px1 py1))
      (destructuring-bind (qx0 qy0 qx1 qy1) (aref planes (1+ depth))
        (declare (ignore qx0 qx1))
        (destructuring-bind (x y w h) (wall-piece-rect planes piece)
          (declare (ignore x y))
          (ecase kind
            ((:front :front-door)
             (%draw-house-front w h palette
                                :door (eq kind :front-door)))
            ((:flank :flank-door)
             (destructuring-bind (fx fy fw fh)
                 (wall-piece-rect planes (list :front depth))
               (declare (ignore fx fy fh))
               (%draw-house-front w h palette
                                  :door (eq kind :flank-door)
                                  :pattern-w fw
                                  :pattern-x0 (if (eq side :l) (- w) fw))))
            ((:side :side-door)
             (let ((img (%draw-house-side w h (- qy0 py0) (- qy1 py0)
                                          palette
                                          :door (eq kind :side-door))))
               (if (eq side :r) (%img-mirror-x img) img)))))))))

;;; ---------------------------------------------------------------------
;;; Backdrops: the ceiling and floor filling the two BACKDROP-RECTS
;;; slots above and below the horizon.  The ceiling is solid distance
;;; bands painted in screen space at the perspective-plane rows: the
;;; wall pieces blit on top, so each band lines up with the corridor
;;; cell at its depth (and shows through open sides at the same rows,
;;; which is perspective-consistent for the corridor beyond).  The
;;; floor is one flat color — a single-pen "band" filling its slot.

(defun %draw-backdrop (w h planes oy top-p pens &key (depth 3)
                                                     (palette *wall-palette*))
  "One backdrop slot as solid distance bands: a W x H image whose band
K spans the rows between perspective planes K and K+1 (screen rows,
offset by the slot's top OY), filled with (AREF PENS K); the last band
runs on to the horizon edge.  TOP-P selects the ceiling side of the
planes (their top rows) or the floor side (bottom rows)."
  (let ((img (make-image w h depth :palette palette))
        (n (length pens)))
    (dotimes (k n img)
      (let* ((near (aref planes k))
             (far (unless (= k (1- n)) (aref planes (1+ k))))
             (y0 (if top-p
                     (- (second near) oy)
                     (if far (- (1+ (fourth far)) oy) 0)))
             (y1 (if top-p
                     (if far (- (1- (second far)) oy) (1- h))
                     (- (fourth near) oy))))
        (%img-fill img 0 y0 (1- w) y1 (aref pens k))))))

(defun draw-backdrop-piece (key planes)
  "Draw the :CEILING or :FLOOR demo backdrop at its slot size for
PLANES: the ceiling as three solid grey bands darkening toward the
horizon (ending in plain black), keeping the dark dungeon mood; the
floor as one flat mid grey (no distance shading)."
  (destructuring-bind (ceiling floor) (backdrop-rects planes)
    (ecase key
      (:ceiling (%draw-backdrop (third ceiling) (fourth ceiling) planes
                                (second ceiling) t
                                (vector +pen-dim+ +pen-dark+ +pen-bg+)))
      (:floor (%draw-backdrop (third floor) (fourth floor) planes
                              (second floor) nil
                              (vector +pen-mid+))))))

;;; ---------------------------------------------------------------------
;;; Piece dispatch

(defun draw-wall-piece (piece planes)
  "Draw PIECE (a WALL-PIECE-NAMES key) at its slot size for PLANES."
  (destructuring-bind (kind depth &optional side) piece
    (destructuring-bind (px0 py0 px1 py1) (aref planes depth)
      (declare (ignore px0 px1 py1))
      (destructuring-bind (qx0 qy0 qx1 qy1) (aref planes (1+ depth))
        (declare (ignore qx0 qx1))
        (destructuring-bind (x y w h) (wall-piece-rect planes piece)
          (declare (ignore x y))
          (ecase kind
            ((:front :front-door)
             (%draw-front-wall w h :door (eq kind :front-door)))
            ((:flank :flank-door)
             ;; A flank is the same flat wall as the front piece at this
             ;; depth, continued through the open side — its bricks must
             ;; carry the front slot's scale and bond across the seam,
             ;; not shrink to the flank's narrow strip.
             (destructuring-bind (fx fy fw fh)
                 (wall-piece-rect planes (list :front depth))
               (declare (ignore fx fy fh))
               (%draw-front-wall w h :door (eq kind :flank-door)
                                 :pattern-w fw
                                 :pattern-x0 (if (eq side :l) (- w) fw))))
            ((:side :side-door)
             (let ((img (%draw-side-wall w h
                                         (- qy0 py0) (- qy1 py0)
                                         :door (eq kind :side-door))))
               (if (eq side :r) (%img-mirror-x img) img)))))))))

;;; ---------------------------------------------------------------------
;;; Effects-band icons: small procedural placeholders a world can ship
;;; for its timed effects (define-spell/define-item :image).  Pen 0 is
;;; the transparent key (the band's white page shows through), pen 4
;;; the opaque black outline — the wall-piece color contract.

(defparameter *effect-icon-size* 16
  "Effects-band icons are square, this many pixels a side.")

(defparameter *effect-icon-palette*
  #((0 0 0) (255 255 255) (136 136 136) (255 170 51)
    (0 0 0) (0 0 0) (0 0 0) (0 0 0))
  "CMAP for the icon files: the UI pens 0-3 plus opaque black at 4.")

(defun draw-effect-icon (kind)
  "A 16x16 effects-band icon image for KIND — :compass (the rose in
miniature: black diamond, amber north needle), :flame (an amber
teardrop) or :shield (a grey kite with an amber boss)."
  (let ((img (make-image *effect-icon-size* *effect-icon-size* 3
                         :palette *effect-icon-palette*)))
    (ecase kind
      (:compass
       (dotimes (y 16)
         (dotimes (x 16)
           (when (= (+ (abs (- x 7)) (abs (- y 7))) 6)
             (setf (pixel-ref img x y) 4))))
       (loop for y from 2 to 7
             do (setf (pixel-ref img 7 y) 3)))
      (:flame
       (loop for y from 2 to 13
             do (let ((hw (min 4 (floor (- y 1) 3))))
                  (loop for x from (- 7 hw) to (+ 7 hw)
                        do (setf (pixel-ref img x y)
                                 (if (= (abs (- x 7)) hw) 4 3))))))
      (:shield
       (loop for y from 2 to 13
             do (let ((hw (if (<= y 8) 5 (- 13 y))))
                  (loop for x from (- 7 hw) to (+ 7 hw)
                        do (setf (pixel-ref img x y)
                                 (if (or (= (abs (- x 7)) hw)
                                         (= y 2))
                                     4 2)))))
       (loop for y from 5 to 7
             do (setf (pixel-ref img 7 y) 3))))
    img))

;;; ---------------------------------------------------------------------
;;; Location pictures and character portraits: procedural placeholders
;;; a world can ship for its location :image and hero-class :image —
;;; the art the view column shows while the location menu or the
;;; character sheet takes over the message area.  Pictures blit opaque
;;; over a black backdrop, so pen 0 is plain black here (no
;;; transparency contract).  They draw in the fixed UI pens only —
;;; black, white, grey, amber — so they read correctly on any screen
;;; whatever tile pack is active (packs may only recolor pens 4+).

(defparameter *picture-palette*
  #((0 0 0) (255 255 255) (136 136 136) (255 170 51))
  "CMAP for pictures and portraits: the fixed UI pens.")

(defun %img-ellipse (img cx cy rx ry fill &optional edge)
  "Filled ellipse at (CX,CY), radii RX/RY, pen FILL; EDGE (when given)
outlines the rim one pixel thick."
  (let ((rx2 (* rx rx))
        (ry2 (* ry ry)))
    (loop for y from (- cy ry) to (+ cy ry)
          do (loop for x from (- cx rx) to (+ cx rx)
                   do (let* ((dx (- x cx))
                             (dy (- y cy))
                             (d (+ (* dx dx ry2) (* dy dy rx2))))
                        (when (and (<= d (* rx2 ry2))
                                   (<= 0 x (1- (image-width img)))
                                   (<= 0 y (1- (image-height img))))
                          (setf (pixel-ref img x y)
                                (if (and edge
                                         (> d (* (1- rx) (1- rx)
                                                 (1- ry) (1- ry))))
                                    edge
                                    fill))))))))

(defun %chrome-scene-rect (img x0 y0 x1 y1 pen)
  "Rectangle outline inside a picture."
  (%img-fill img x0 y0 x1 y0 pen)
  (%img-fill img x0 y1 x1 y1 pen)
  (%img-fill img x0 y0 x0 y1 pen)
  (%img-fill img x1 y0 x1 y1 pen))

(defun draw-location-scene (kind w h)
  "A W x H location picture for the view column — :SHOP (stocked
shelves over a counter), :TAVERN (a table with a foaming mug beside a
barrel), anything else (a plain doorway)."
  (let ((img (make-image w h 2 :palette *picture-palette*)))
    (ecase kind
      (:shop
       ;; grey floor, a counter, two stocked shelves
       (%img-fill img 0 (floor (* 7 h) 8) (1- w) (1- h) 2)
       (%img-fill img (floor w 8) (floor (* 5 h) 8)
                  (floor (* 7 w) 8) (floor (* 7 h) 8) 2)
       (%img-fill img (floor w 8) (floor (* 5 h) 8)
                  (floor (* 7 w) 8) (floor (* 5 h) 8) 1)
       (dolist (sy (list (floor h 4) (floor (* 7 h) 16)))
         (%img-fill img (floor w 8) sy (floor (* 7 w) 8) sy 1)
         ;; the goods: amber wares spaced along each shelf
         (loop for x from (floor w 6) below (floor (* 4 w) 5)
                 by (max 6 (floor w 10))
               do (%img-fill img x (- sy (max 3 (floor h 16)))
                             (+ x (max 3 (floor w 40))) (1- sy) 3)))
       ;; the hanging sign
       (%img-fill img (floor (* 2 w) 5) (floor h 16)
                  (floor (* 3 w) 5) (floor h 8) 3)
       (%chrome-scene-rect img (floor (* 2 w) 5) (floor h 16)
                           (floor (* 3 w) 5) (floor h 8) 1))
      (:tavern
       ;; grey floor, a table with a foaming mug, a barrel to the right
       (%img-fill img 0 (floor (* 7 h) 8) (1- w) (1- h) 2)
       (%img-fill img (floor w 8) (floor (* 5 h) 8)
                  (floor (* 5 w) 8) (floor (* 11 h) 16) 2)
       (%img-fill img (floor w 8) (floor (* 5 h) 8)
                  (floor (* 5 w) 8) (floor (* 5 h) 8) 1)
       (%img-fill img (floor (* 3 w) 16) (floor (* 11 h) 16)
                  (floor (* 3 w) 16) (floor (* 7 h) 8) 2)
       (%img-fill img (floor (* 9 w) 16) (floor (* 11 h) 16)
                  (floor (* 9 w) 16) (floor (* 7 h) 8) 2)
       ;; the mug: amber body, white foam, a handle
       (let ((mx (floor (* 3 w) 8))
             (mt (floor (* 15 h) 32)))
         (%img-fill img mx mt (+ mx (floor w 16)) (floor (* 5 h) 8) 3)
         (%img-fill img mx (- mt (max 1 (floor h 32)))
                    (+ mx (floor w 16)) mt 1)
         (%img-fill img (+ mx (floor w 16) 1) (+ mt (floor h 32))
                    (+ mx (floor w 16) 2) (floor (* 9 h) 16) 1))
       ;; the barrel: grey ellipse with black hoops
       (let ((bx (floor (* 3 w) 4))
             (by (floor (* 11 h) 16)))
         (%img-ellipse img bx by (floor w 10) (floor h 5) 2 1)
         (%img-fill img (- bx (floor w 11)) (- by (floor h 16))
                    (+ bx (floor w 11)) (- by (floor h 16)) 0)
         (%img-fill img (- bx (floor w 11)) (+ by (floor h 16))
                    (+ bx (floor w 11)) (+ by (floor h 16)) 0)))
      (t
       ;; a plain amber doorway
       (let ((dx0 (floor (* 3 w) 8))
             (dx1 (floor (* 5 w) 8))
             (dy0 (floor h 4))
             (dy1 (floor (* 7 h) 8)))
         (%img-fill img 0 dy1 (1- w) (1- h) 2)
         (%img-fill img dx0 dy0 dx1 dy1 3)
         (%chrome-scene-rect img dx0 dy0 dx1 dy1 1))))
    img))

(defparameter *portrait-size* 64
  "Class portraits are square, this many pixels a side.")

(defun draw-portrait (style &optional (w *portrait-size*) (h *portrait-size*))
  "A W x H bust portrait placeholder for a hero class.  STYLE picks
the headgear: :HELM (a grey helmet with a nose guard), :CREST (the
helmet with an amber plume), :HOOD (a grey hood), :CAP (a flat cap
with an amber feather), :HAT (a tall pointed hat with an amber band)
or :PLAIN (a bare head)."
  (let* ((img (make-image w h 2 :palette *picture-palette*))
         (cx (floor w 2))
         (cy (floor (* 2 h) 5))
         (rx (floor w 6))
         (ry (floor h 5)))
    ;; shoulders: a trapezoid rising to the neck
    (loop for y from (floor (* 7 h) 10) below h
          do (let ((hw (min (floor (* 2 w) 5)
                            (+ (floor w 6)
                               (floor (* (- y (floor (* 7 h) 10)) w)
                                      h)))))
               (%img-fill img (- cx hw) y (+ cx hw) y 2)))
    (%img-fill img (- cx (floor rx 2)) (+ cy ry)
               (+ cx (floor rx 2)) (floor (* 7 h) 10) 2)   ; the neck
    ;; the head, then the face
    (%img-ellipse img cx cy rx ry 2 1)
    (let ((ex (floor rx 2))
          (ey (floor ry 4)))
      (%img-fill img (- cx ex) (- cy ey) (1+ (- cx ex)) (- cy ey) 0)
      (%img-fill img (+ cx ex -1) (- cy ey) (+ cx ex) (- cy ey) 0)
      (%img-fill img (- cx (floor ex 2)) (+ cy (* 2 ey))
                 (+ cx (floor ex 2)) (+ cy (* 2 ey)) 0))
    ;; the headgear
    (ecase style
      ((:helm :crest)
       (%img-fill img (- cx rx) (- cy ry) (+ cx rx)
                  (- cy (floor ry 3)) 2)
       (%img-fill img (- cx rx) (- cy (floor ry 3)) (+ cx rx)
                  (- cy (floor ry 3)) 1)
       (%img-fill img cx (- cy (floor ry 3)) cx (- cy (floor ry 8)) 2)
       (when (eq style :crest)
         (%img-fill img (- cx 1) (- cy ry (floor h 10))
                    (1+ cx) (- cy ry) 3)))
      (:hood
       (%img-ellipse img cx (- cy (floor ry 2)) (+ rx 3)
                     (+ (floor ry 2) 3) 2 1)
       (%img-fill img (- cx rx -2) (- cy (floor ry 2))
                 (+ cx rx -2) (- cy (floor ry 3)) 0))
      (:cap
       (%img-fill img (- cx rx 2) (- cy ry 2) (+ cx rx 2) (- cy ry -2) 2)
       (%img-fill img (- cx rx 2) (- cy ry 2) (+ cx rx 2) (- cy ry 2) 1)
       (%img-fill img (+ cx rx) (- cy ry (floor h 12))
                  (+ cx rx 2) (- cy ry) 3))
      (:hat
       ;; the point at the top, widening down to the brim
       (loop for k from 1 to (floor h 5)
             do (let ((hw (max 1 (round (* rx k) (floor h 5)))))
                  (%img-fill img (- cx hw) (- (- cy ry) (- (floor h 5) k))
                             (+ cx hw) (- (- cy ry) (- (floor h 5) k)) 2)))
       (%img-fill img (- cx rx) (- cy ry) (+ cx rx) (- cy ry -1) 3))
      (:plain
       (%img-fill img (- cx rx) (- cy ry) (+ cx rx)
                  (- cy (floor (* 2 ry) 3)) 2)))
    img))

(defun generate-wall-assets (&key (profile *display-profile*) dir)
  "Draw all wall pieces plus the demo ceiling/floor backdrops for
PROFILE's viewport and write them as ILBM files into DIR (default: the
profile's tile-pack directory).  Returns the number of files written."
  (with-display-profile (profile)
    (let ((dir (or dir *gfx-dir*))
          (planes (view-planes *fp-view-width* *fp-view-height*))
          (n 0))
      (ensure-directories-exist dir)
      (dolist (piece (wall-piece-names))
        (write-ilbm (draw-wall-piece piece planes)
                    (concatenate 'string dir (wall-piece-file piece)))
        (incf n))
      (dolist (key '(:ceiling :floor) n)
        (write-ilbm (draw-backdrop-piece key planes)
                    (concatenate 'string dir
                                 (string-downcase (symbol-name key))
                                 ".iff"))
        (incf n)))))
