;;; Lambda's Tale — first-person view geometry.
;;;
;;; COMPUTE-VIEW turns (map, position, facing) into a list of VIEW-SLICEs:
;;; one per visible cell straight ahead, nearest first, in facing-relative
;;; terms (front/left/right).  The Bard's Tale view never looks around
;;; corners — from each slice you see its side walls and, through an open
;;; side, the front wall of the adjacent cell.
;;;
;;; VIEW-DISPLAY-LIST flattens slices onto perspective plane rectangles as
;;; backend-independent primitives:
;;;   (:line x0 y0 x1 y1)    wireframe segment
;;;   (:door cx cy hw hh)    door marker (center + half extents)
;;; ordered back to front.  The ASCII renderer rasterizes these into
;;; characters; the Amiga renderer feeds them to graphics.library.

(in-package :tale)

(defconstant +view-depth+ 4)    ; cells visible ahead, including standing cell

;;; The first-person viewport size shared by the Amiga front-end, the
;;; wall-art generator and the tests: the wall-piece assets are drawn
;;; for exactly these planes, so the three must agree.  Initialized
;;; from the default display profile; WITH-DISPLAY-PROFILE (and thus
;;; PLAY-AMIGA's :PROFILE argument) rebinds them per target.
(defparameter *fp-view-width* (display-profile-fp-width *display-profile*))
(defparameter *fp-view-height* (display-profile-fp-height *display-profile*))

(defstruct (view-slice (:constructor %make-view-slice))
  (depth 0)
  cx cy               ; center cell coordinates
  front left right    ; wall values of the center cell, facing-relative
  lx ly left-front    ; left side cell + its front wall (when left is :open)
  rx ry right-front   ; right side cell + its front wall (when right is :open)
  front-style         ; style of the building behind each wall (or NIL
  left-style          ; when that surface is no wall) — see %WALL-STYLE;
  right-style         ; the blitted view picks tile-pack piece variants
  left-front-style    ; with these, so a street reads as a row of
  right-front-style)  ; different houses

;;; Buildings: the mass behind a wall.
;;;
;;; A city block is many cells of solid mass, but it is ONE building —
;;; its street face must wear one look, not a different piece variant
;;; per cell (which showed a house whose wall changed from stone to
;;; plaster halfway along its own front).  A cell belongs to a building
;;; when the party can never stand in it: every one of its four walls
;;; is a wall or a door.  The building is the 4-connected region of
;;; such cells, and its style is the :STYLE a LOCATION op anywhere in
;;; it pins (the campaign matching a house's street look to its facade
;;; picture), else a hash of the region's first cell.

(defun %coord-style (x y)
  "The fallback style of a building with no LOCATION op: a coordinate
hash.  The range 12 divides evenly by 1-4, so (MOD STYLE
VARIANT-COUNT) stays uniform over the usual variant counts."
  (mod (+ (* 31 x) (* 17 y)) 12))

(defun %location-style (map x y)
  "The :STYLE integer of a LOCATION op on cell (X,Y), or NIL."
  (let* ((loc (cell-location-op map x y))
         (style (and loc (getf (cddr loc) :style))))
    (when (integerp style) style)))

(defun %cell-solid-p (map x y)
  "Is (X,Y) part of a building mass — walled in on all four sides?"
  (dotimes (d 4 t)
    (when (eq (cell-wall map x y d) :open)
      (return nil))))

(defun %building-styles (map)
  "Vector of W*H style values: for every solid cell the style of the
building it belongs to, NIL for a cell the party can walk in.  Flood-
filled once per map and cached in *BUILDING-STYLES* (see
%WALL-STYLE) — every wall of one mass then answers with the same
style, so a block-long house wears one look."
  (if (eq (car *building-styles*) map)
      (cdr *building-styles*)
      (let* ((w (dungeon-map-width map))
             (h (dungeon-map-height map))
             (styles (make-array (* w h) :initial-element nil)))
        (dotimes (sy h)
          (dotimes (sx w)
            (when (and (null (aref styles (+ sx (* sy w))))
                       (%cell-solid-p map sx sy))
              ;; flood this mass, picking up the :STYLE any LOCATION op
              ;; inside it pins; (SX,SY) — scan order, so deterministic
              ;; — anchors the fallback hash
              (let ((stack (list (cons sx sy)))
                    (cells '())
                    (pinned nil))
                (setf (aref styles (+ sx (* sy w))) :seen)
                (loop while stack
                      do (let* ((cell (pop stack))
                                (cx (car cell))
                                (cy (cdr cell)))
                           (push cell cells)
                           (unless pinned
                             (setf pinned (%location-style map cx cy)))
                           (dotimes (d 4)
                             (multiple-value-bind (nx ny)
                                 (neighbor map cx cy d)
                               (when (and nx
                                          (null (aref styles
                                                      (+ nx (* ny w))))
                                          (%cell-solid-p map nx ny))
                                 (setf (aref styles (+ nx (* ny w))) :seen)
                                 (push (cons nx ny) stack))))))
                (let ((style (or pinned (%coord-style sx sy))))
                  (dolist (cell cells)
                    (setf (aref styles (+ (car cell) (* (cdr cell) w)))
                          style)))))))
        (setf *building-styles* (cons map styles))
        styles)))

(defun %wall-style (map x y)
  "Deterministic style index of the building whose mass fills cell
\(X,Y): the style of the whole building the cell belongs to (see
%BUILDING-STYLES), the :STYLE integer of a LOCATION op on a walkable
cell, else a coordinate hash.  The blitted view picks among a tile
pack's piece variants with it (see VIEW-BLIT-LIST and the -vN files in
%LOAD-WALL-ASSETS) so a street reads as a row of different houses; a
pack without variants renders every style the same."
  (or (aref (%building-styles map)
            (+ x (* y (dungeon-map-width map))))
      (%location-style map x y)
      (%coord-style x y)))

(defun compute-view (map x y facing &optional (depth +view-depth+))
  "List of VIEW-SLICEs visible from (X,Y) looking FACING, nearest first.
Stops at a solid or door front wall, an off-map edge, or DEPTH cells
\(default +VIEW-DEPTH+; darkness passes 1 — see GAME-VIEW-DEPTH)."
  (let* ((f (dir-index facing))
         (ldir (turn-dir f -1))
         (rdir (turn-dir f 1))
         (slices '())
         (cx x)
         (cy y))
    (flet ((style-behind (x y dir)
             ;; the building is the cell on the far side of the wall;
             ;; a map-edge wall (no far cell) styles from the near one
             (multiple-value-bind (nx ny) (neighbor map x y dir)
               (if nx
                   (%wall-style map nx ny)
                   (%wall-style map x y)))))
      (dotimes (d depth)
        (let ((front (cell-wall map cx cy f))
              (left (cell-wall map cx cy ldir))
              (right (cell-wall map cx cy rdir))
              (lx nil) (ly nil) (lf nil)
              (rx nil) (ry nil) (rf nil))
          (when (eq left :open)
            (multiple-value-bind (nx ny) (neighbor map cx cy ldir)
              (when nx
                (setf lx nx ly ny lf (cell-wall map nx ny f)))))
          (when (eq right :open)
            (multiple-value-bind (nx ny) (neighbor map cx cy rdir)
              (when nx
                (setf rx nx ry ny rf (cell-wall map nx ny f)))))
          (push (%make-view-slice
                 :depth d :cx cx :cy cy
                 :front front :left left :right right
                 :lx lx :ly ly :left-front lf
                 :rx rx :ry ry :right-front rf
                 :front-style (when (member front '(:wall :door))
                                (style-behind cx cy f))
                 :left-style (when (member left '(:wall :door))
                               (style-behind cx cy ldir))
                 :right-style (when (member right '(:wall :door))
                                (style-behind cx cy rdir))
                 :left-front-style (when (member lf '(:wall :door))
                                     (style-behind lx ly f))
                 :right-front-style (when (member rf '(:wall :door))
                                      (style-behind rx ry f)))
                slices)
          ;; Advance only through an open front (doors block the view).
          (unless (eq front :open)
            (return))
          (multiple-value-bind (nx ny) (neighbor map cx cy f)
            (if nx
                (setf cx nx cy ny)
                (return))))))
    (nreverse slices)))

;;; ---------------------------------------------------------------------
;;; Perspective planes

;; Inset fraction of the viewport per depth boundary; +view-depth+ cells
;; need (1+ +view-depth+) planes.
(defparameter *plane-fractions* #(0 1/5 33/100 42/100 47/100))

(defun view-planes (width height)
  "Vector of plane rectangles (x0 y0 x1 y1) for a WIDTH x HEIGHT viewport,
one per depth boundary, plane 0 being the viewport itself."
  (let* ((n (length *plane-fractions*))
         (planes (make-array n)))
    (dotimes (k n planes)
      (let ((x0 (round (* (1- width) (aref *plane-fractions* k))))
            (y0 (round (* (1- height) (aref *plane-fractions* k)))))
        (setf (aref planes k)
              (list x0 y0 (- (1- width) x0) (- (1- height) y0)))))))

;;; ---------------------------------------------------------------------
;;; Display list

(defun view-display-list (slices planes)
  "Flatten SLICES onto PLANES as (:line x0 y0 x1 y1) / (:door cx cy hw hh)
primitives, ordered back to front."
  (let ((prims '()))
    (labels ((line (x0 y0 x1 y1)
               (push (list :line x0 y0 x1 y1) prims))
             (rect (x0 y0 x1 y1)
               ;; diagonal-free box; verticals last so their endpoints
               ;; win the corners in the ASCII rasterizer
               (line x0 y0 x1 y0)
               (line x0 y1 x1 y1)
               (line x0 y0 x0 y1)
               (line x1 y0 x1 y1))
             (door-mark (x0 y0 x1 y1)
               (push (list :door
                           (floor (+ x0 x1) 2)
                           (floor (+ y0 y1) 2)
                           (max 1 (floor (- x1 x0) 6))
                           (max 1 (floor (- y1 y0) 4)))
                     prims))
             (flank (s side kind)
               ;; the neighbor's front wall through an open side, at
               ;; the perspective width the blitted view gives it
               (multiple-value-bind (x y w h sx)
                   (visible-flank-rect slices planes
                                       (list kind (view-slice-depth s) side))
                 (declare (ignore sx))
                 (rect x y (+ x w -1) (+ y h -1))
                 (when (eq kind :flank-door)
                   (door-mark x y (+ x w -1) (+ y h -1)))))
             (draw-slice (s)
               (let ((p (aref planes (view-slice-depth s)))
                     (q (aref planes (1+ (view-slice-depth s)))))
                 (destructuring-bind (px0 py0 px1 py1) p
                   (destructuring-bind (qx0 qy0 qx1 qy1) q
                     ;; left side
                     (case (view-slice-left s)
                       ((:wall :door)
                        (line px0 py0 qx0 qy0)    ; receding ceiling edge
                        (line px0 py1 qx0 qy1)    ; receding floor edge
                        (line px0 py0 px0 py1)
                        (line qx0 qy0 qx0 qy1)
                        (when (eq (view-slice-left s) :door)
                          (door-mark px0 qy0 qx0 qy1)))
                       (:open
                        (case (view-slice-left-front s)
                          (:wall (flank s :l :flank))
                          (:door (flank s :l :flank-door)))))
                     ;; right side (mirrored)
                     (case (view-slice-right s)
                       ((:wall :door)
                        (line px1 py0 qx1 qy0)
                        (line px1 py1 qx1 qy1)
                        (line px1 py0 px1 py1)
                        (line qx1 qy0 qx1 qy1)
                        (when (eq (view-slice-right s) :door)
                          (door-mark qx1 qy0 px1 qy1)))
                       (:open
                        (case (view-slice-right-front s)
                          (:wall (flank s :r :flank))
                          (:door (flank s :r :flank-door)))))
                     ;; front wall
                     (when (member (view-slice-front s) '(:wall :door))
                       (rect qx0 qy0 qx1 qy1)
                       (when (eq (view-slice-front s) :door)
                         (door-mark qx0 qy0 qx1 qy1))))))))
      (dolist (s (reverse slices))
        (draw-slice s))
      (nreverse prims))))

;;; ---------------------------------------------------------------------
;;; Wall pieces (M3): the Bard's Tale fixed-slot geometry for the blitted
;;; view.  Every wall the view can show falls into one of a fixed set of
;;; screen slots derived from the perspective planes; each slot is filled
;;; by one pre-rendered bitmap piece.  The piece keys name both the
;;; bitmaps in the asset cache and the files the art generator emits:
;;;
;;;   (:front d)             front wall across the corridor at depth d
;;;   (:front-door d)        the same slot with a door in the wall
;;;   (:side d :l/:r)        receding side wall (the trapezoid, with the
;;;                          ceiling/floor corners baked in)
;;;   (:side-door d :l/:r)   side wall with a door
;;;   (:flank d :l/:r)       front wall of the neighbor cell seen through
;;;                          an open side
;;;   (:flank-door d :l/:r)  the same with a door
;;;
;;; VIEW-BLIT-LIST is the bitmap twin of VIEW-DISPLAY-LIST: same slice
;;; walk, but flattened to (piece x y w h) blit records, back to front,
;;; so the near pieces overdraw the far ones (rectangular blits with the
;;; corners painted in the background color make that correct).

(defun wall-piece-rect (planes piece)
  "Screen slot (X Y W H) of PIECE for the plane set PLANES.

A flank — the neighbor cell's front wall, seen through an open side —
stands one cell to the side at the SAME distance as the front wall at
its depth, so it gets that wall's full perspective width (the corridor
width at plane DEPTH+1), clipped to the viewport, not the narrow strip
between the near and far planes.  Drawn narrow, a house across an open
side came out roughly half as wide as perspective demands.  How much
of that slot is actually visible depends on the walls in front of it —
VIEW-BLIT-LIST clips it and blits the visible part."
  (destructuring-bind (kind depth &optional side) piece
    (destructuring-bind (px0 py0 px1 py1) (aref planes depth)
      (declare (ignorable px0 px1))
      (destructuring-bind (qx0 qy0 qx1 qy1) (aref planes (1+ depth))
        (declare (ignorable qx1))
        (ecase kind
          ((:front :front-door)
           (list qx0 qy0 (1+ (- qx1 qx0)) (1+ (- qy1 qy0))))
          ((:side :side-door)
           (if (eq side :l)
               (list px0 py0 (1+ (- qx0 px0)) (1+ (- py1 py0)))
               (list qx1 py0 (1+ (- px1 qx1)) (1+ (- py1 py0)))))
          ((:flank :flank-door)
           (destructuring-bind (vx0 vy0 vx1 vy1) (aref planes 0)
             (declare (ignore vy0 vy1))
             (let ((cell (- qx1 qx0)))  ; one cell wide at that distance
               (if (eq side :l)
                   (let ((x0 (max vx0 (- qx0 cell))))
                     (list x0 qy0 (1+ (- qx0 x0)) (1+ (- qy1 qy0))))
                   (let ((x1 (min vx1 (+ qx1 cell))))
                     (list qx1 qy0 (1+ (- x1 qx1))
                           (1+ (- qy1 qy0)))))))))))))

(defun %plane-edge (planes k side)
  "Plane K's left (:L) or right (:R) screen edge."
  (destructuring-bind (x0 y0 x1 y1) (aref planes k)
    (declare (ignore y0 y1))
    (if (eq side :l) x0 x1)))

(defun flank-visible-x (slices planes depth side)
  "The screen x a flank at DEPTH on SIDE is visible up to: the far edge
of the nearest wall standing between the party and it, else the
viewport edge.  A side wall at depth K covers the band from plane K to
plane K+1, so anything deeper is hidden up to plane K+1's edge; an
open side whose neighbor shows a front wall (a flank of its own)
blocks exactly the same band."
  (loop for k from (1- depth) downto 0
        for s = (nth k slices)
        for wall = (if (eq side :l) (view-slice-left s) (view-slice-right s))
        for beyond = (if (eq side :l)
                         (view-slice-left-front s)
                         (view-slice-right-front s))
        when (or (member wall '(:wall :door))
                 (member beyond '(:wall :door)))
          do (return (%plane-edge planes (1+ k) side))
        finally (return (%plane-edge planes 0 side))))

(defun visible-flank-rect (slices planes piece)
  "PIECE's slot cropped to what the walls in front of it leave visible:
\(VALUES X Y W H SX), SX the x offset into the piece bitmap X starts
at.  Both renderers place a flank with this, so the wireframe and the
blitted view draw the same wall."
  (destructuring-bind (kind depth &optional side) piece
    (declare (ignore kind))
    (destructuring-bind (x y w h) (wall-piece-rect planes piece)
      (let ((edge (flank-visible-x slices planes depth side)))
        (if (eq side :l)
            (let ((nx (max x (min edge (+ x w -1)))))
              (values nx y (- (+ x w) nx) h (- nx x)))
            (values x y (1+ (- (max x (min edge (+ x w -1))) x)) h 0))))))

(defun wall-piece-names ()
  "All piece keys the view can ever ask for (the asset set)."
  (loop for d below +view-depth+
        append (list* (list :front d) (list :front-door d)
                      (loop for kind in '(:side :side-door :flank :flank-door)
                            append (list (list kind d :l)
                                         (list kind d :r))))))

(defun wall-piece-file (piece)
  "Asset file name (under data/gfx/) for PIECE, e.g. \"side-door-2-l.iff\"."
  (format nil "~{~A~^-~}.iff"
          (mapcar (lambda (part) (string-downcase (princ-to-string part)))
                  piece)))

(defun wall-piece-variant-file (piece n)
  "File name of PIECE's Nth style variant, e.g. \"front-0-v1.iff\" —
the optional per-house looks a pack may ship beside the base piece
(see %WALL-STYLE and the loader contract in the README)."
  (let ((base (wall-piece-file piece)))
    (format nil "~A-v~D.iff" (subseq base 0 (- (length base) 4)) n)))

(defun view-blit-list (slices planes)
  "Flatten SLICES into blit records (PIECE STYLE X Y W H SX), back to
front.  The bitmap twin of VIEW-DISPLAY-LIST — same wall logic, but
each wall becomes one fixed-slot piece blit instead of wireframe
lines.  STYLE is the wall's building style (see %WALL-STYLE); the
renderer picks the piece's variant with (MOD STYLE VARIANT-COUNT), so
it is 0 whenever the pack ships no variants.  SX is the x offset into
the piece bitmap the blit starts at: 0 for every piece drawn whole,
non-zero only for a left flank the walls in front of it hide part of
(see WALL-PIECE-RECT and FLANK-VISIBLE-X) — X/W are that visible part,
so the piece is never stretched, only cropped."
  (let ((blits '()))
    (labels ((blit (kind depth style &optional side)
               (let ((piece (if side
                                (list kind depth side)
                                (list kind depth))))
                 (if (member kind '(:flank :flank-door))
                     ;; crop to what the walls in front leave visible
                     (multiple-value-bind (x y w h sx)
                         (visible-flank-rect slices planes piece)
                       (push (list piece (or style 0) x y w h sx) blits))
                     (destructuring-bind (x y w h)
                         (wall-piece-rect planes piece)
                       (push (list piece (or style 0) x y w h 0) blits)))))
             (draw-slice (s)
               (let ((d (view-slice-depth s)))
                 ;; sides first, then the front wall on top of their seams
                 (dolist (side '(:l :r))
                   (let ((wall (if (eq side :l)
                                   (view-slice-left s)
                                   (view-slice-right s)))
                         (wall-style (if (eq side :l)
                                         (view-slice-left-style s)
                                         (view-slice-right-style s)))
                         (beyond (if (eq side :l)
                                     (view-slice-left-front s)
                                     (view-slice-right-front s)))
                         (beyond-style (if (eq side :l)
                                           (view-slice-left-front-style s)
                                           (view-slice-right-front-style s))))
                     (case wall
                       (:wall (blit :side d wall-style side))
                       (:door (blit :side-door d wall-style side))
                       (:open
                        (case beyond
                          (:wall (blit :flank d beyond-style side))
                          (:door (blit :flank-door d beyond-style side)))))))
                 (case (view-slice-front s)
                   (:wall (blit :front d (view-slice-front-style s)))
                   (:door (blit :front-door d
                                (view-slice-front-style s)))))))
      (dolist (s (reverse slices))
        (draw-slice s))
      (nreverse blits))))

;;; ---------------------------------------------------------------------
;;; Backdrop (floor and ceiling): the Bard's Tale split-region look.
;;; One ceiling image fills the viewport above the horizon, one floor
;;; image below it; the walls blit on top and carve the perspective
;;; (their bases and tops bound the visible floor/sky trapezoid).

(defun backdrop-rects (planes)
  "The two backdrop slots ((X Y W H) (X Y W H)) — ceiling then floor —
for the plane set PLANES.  Together they tile the viewport exactly,
split at the horizon (the vertical center of the innermost plane)."
  (destructuring-bind (px0 py0 px1 py1) (aref planes 0)
    (destructuring-bind (qx0 qy0 qx1 qy1) (aref planes (1- (length planes)))
      (declare (ignore qx0 qx1))
      (let ((w (1+ (- px1 px0)))
            (horizon (floor (+ qy0 qy1) 2)))
        (list (list px0 py0 w (1+ (- horizon py0)))
              (list px0 (1+ horizon) w (- py1 horizon)))))))

;;; ---------------------------------------------------------------------
;;; The tile-pack manifest: the contract a custom tile pack must meet.

(defparameter *gfx-dir* (display-profile-gfx-dir *display-profile*)
  "The active tile pack: the directory holding the wall-piece ILBMs
(and the optional floor.iff / ceiling.iff / palette.iff), relative to
the game directory.  Defaults to the active display profile's pack;
PLAY-AMIGA's :GFX-DIR argument and WITH-DISPLAY-PROFILE rebind it.")

(defparameter *gfx-cache-packs* :auto
  "How many *inactive* tile packs the Amiga front end keeps loaded.

Loading a pack decodes a directory of ILBMs into offscreen bitmaps —
seconds on a 68020 — so travelling between zones with different packs
\(the town and its cellar, say) pays for the swap every time.  Caching
trades memory for that time: a lores pack is roughly 40K of bitmaps
plus 8K of chip-RAM masks.

  0      never cache — reload on every swap (the smallest footprint)
  N      keep up to N inactive packs, least-recently-used evicted first
  :auto  keep one, but drop the cache whenever free memory falls below
         *GFX-CACHE-MIN-FREE* (the default: fast on a big machine,
         self-limiting on a small one)

Bound per session, so a game can set it in its campaign.")

(defparameter *gfx-cache-min-free* (* 1024 1024)
  "Free-memory floor for (SETF *GFX-CACHE-PACKS* :AUTO), in bytes.
After a pack load the front end asks exec for free RAM; below this it
frees the cached packs rather than hold them.  Measured with
AvailMem(MEMF_ANY), which does not see an RTG board's own memory — on
an RTG machine the piece bitmaps may live there and this floor is
merely conservative.")

;;; The cache itself: a list of INACTIVE packs, most recently used
;;; first, each entry (DIR WALLS . PALETTE).  The active pack is not in
;;; it — the front end holds that separately.  WALLS is opaque here
;;; (on the Amiga it is the piece-bitmap hash); freeing is the caller's
;;; job, passed in as FREE-FN, which keeps this policy platform-free
;;; and testable on the host.

(defun %pack-cache-limit ()
  "How many inactive packs *GFX-CACHE-PACKS* allows us to hold."
  (let ((v *gfx-cache-packs*))
    (cond ((eq v :auto) 1)
          ((and (integerp v) (>= v 0)) v)
          (t (error "*GFX-CACHE-PACKS* is ~S: expected a non-negative ~
integer or :AUTO" v)))))

(defun %pack-cache-take (cache dir)
  "Look DIR up in CACHE.  Returns (VALUES WALLS PALETTE REST) with the
entry removed, or (VALUES NIL NIL CACHE) on a miss."
  (let ((hit (assoc dir cache :test #'equal)))
    (if hit
        (values (second hit) (cddr hit) (remove hit cache))
        (values nil nil cache))))

(defun %pack-cache-put (cache dir walls palette)
  "CACHE with DIR's pack at the front (most recently used).  A pack
that failed to load (WALLS is NIL — the wireframe fallback) is not
worth caching: re-trying the load is what surfaces a fixed pack.  DIR
replaces any older entry for the same directory rather than doubling
it — two entries would leak the older one's bitmaps."
  (if walls
      (cons (list* dir walls palette)
            (remove dir cache :key #'first :test #'equal))
      cache))

(defun %pack-cache-drop (cache free-fn)
  "Call FREE-FN on every pack in CACHE; returns NIL."
  (dolist (entry cache nil)
    (funcall free-fn (second entry))))

(defun %pack-cache-trim (cache free-fn &optional free-memory)
  "Drop packs beyond the *GFX-CACHE-PACKS* budget, least-recently-used
first, freeing each with FREE-FN.  FREE-MEMORY is the machine's free
RAM in bytes (or NIL when it could not be measured): under :AUTO the
whole cache goes when that has fallen below *GFX-CACHE-MIN-FREE*, so a
small machine reloads instead of running itself out of memory."
  (let* ((limit (%pack-cache-limit))
         (limit (if (and free-memory (< free-memory *gfx-cache-min-free*))
                    (progn
                      (dlog "pack cache: ~D bytes free is under the ~D ~
floor — dropping the cache" free-memory *gfx-cache-min-free*)
                      0)
                    limit)))
    (if (<= (length cache) limit)
        cache
        (let ((keep (subseq cache 0 limit))
              (drop (nthcdr limit cache)))
          (dolist (entry drop)
            (dlog "pack cache: evicting ~A" (first entry)))
          (%pack-cache-drop drop free-fn)
          keep))))

(defun print-tile-manifest (&optional (stream *standard-output*))
  "Print the tile-pack contract: every asset file a pack directory may
hold, with its exact pixel size for the canonical *FP-VIEW-WIDTH* x
*FP-VIEW-HEIGHT* viewport, and the palette rules.  Returns the number
of files listed."
  (let* ((planes (view-planes *fp-view-width* *fp-view-height*))
         (n 0))
    (format stream "Tile-pack manifest (~Dx~D viewport, ~A profile, ~
IFF ILBM files):~%"
            *fp-view-width* *fp-view-height*
            (string-downcase
             (princ-to-string (display-profile-name *display-profile*))))
    (dolist (piece (wall-piece-names))
      (destructuring-bind (x y w h) (wall-piece-rect planes piece)
        (declare (ignore x y))
        (format stream "  ~24A ~3Dx~D~%" (wall-piece-file piece) w h)
        (incf n)))
    (destructuring-bind (ceiling floor) (backdrop-rects planes)
      (format stream "  ~24A ~3Dx~D  (optional backdrop above the horizon)~%"
              "ceiling.iff" (third ceiling) (fourth ceiling))
      (format stream "  ~24A ~3Dx~D  (optional backdrop below the horizon)~%"
              "floor.iff" (third floor) (fourth floor))
      (incf n 2))
    (let* ((depth (display-profile-screen-depth *display-profile*))
           (figures (figure-pens depth)))
      (format stream "Palette: these pens belong to THIS PACK — sky, ~
ground and art:~%  ~{~D~^ ~}~%They are taken from palette.iff's CMAP ~
when present, else from~%front-0.iff's (custom screen only; a Workbench ~
window keeps the~%Workbench palette).~%"
              (pack-pens depth))
      (format stream "Every other pen is the engine's and holds the same ~
color in every~%zone: 0 transparent, 1-3 UI (white, grey, amber), 4 ~
opaque black~{,~%pens ~D-~D the mouse pointer~}~{, pens ~D-~D the shared ~
figure core~}.~%A pack's CMAP may carry them but cannot change them.~%"
              (let ((p (pointer-pens depth)))
                (when p (list (first p) (car (last p)))))
              (when figures
                (list (first figures) (car (last figures)))))
      (format stream "Travelling art — monster sprites, hero portraits, ~
effect icons —~%is cached by path across zone changes, so it may use ~
ONLY pen 0~%(transparent) and these:~%  ~{~D~^ ~}~%Build it with ~
GENERATE-FIGURE, which enforces exactly that.~%"
              (figure-palette-pens depth)))
    (format stream "Transparency: in a WALL piece pen 0 is transparent — ~
the ceiling/~%floor backdrop shows through it — so paint solid black ~
with pen 4, not~%pen 0.  The ceiling/floor backdrops are opaque; pen 0 ~
there is plain black.~%")
    (format stream "Variants: any wall piece may ship per-building ~
style variants beside~%it (front-0-v1.iff, front-0-v2.iff, ... — same ~
size and transparency~%rules), probed in order until one is missing; ~
the view deals them out~%per building cell, a location op's :style ~
pins one.~%")
    n))

;;; ---------------------------------------------------------------------
;;; Title fitting: the location plaque under the view is only as wide
;;; as the profile's view column, which is a tuning knob — so a zone
;;; title can be wider than the plaque and must lose trailing
;;; characters instead of overrunning the border into the log column.

(defun fit-title (name measure max-w)
  "NAME shortened by dropping trailing characters until (FUNCALL
MEASURE NAME) is at most MAX-W pixels or one character remains.
MEASURE is the front-end's text-width function (proportional fonts
keep working); NAME is returned unchanged when it already fits."
  (loop while (and (> (length name) 1)
                   (> (funcall measure name) max-w))
        do (setf name (subseq name 0 (1- (length name)))))
  name)

;;; ---------------------------------------------------------------------
;;; Compass rose: display geometry for the UI's facing indicator.

(defun compass-points (facing cx cy r)
  "Compass-rose geometry around hub (CX,CY) with letter radius R:
returns (NEEDLE LETTERS) where NEEDLE is the line (X0 Y0 X1 Y1) from
the hub toward FACING and LETTERS is a list of (CHAR X Y FACING-P)
anchor points for N/E/S/W, FACING-P marking the faced direction."
  (let ((needle-r (max 2 (- r 8))))
    (list (list cx cy
                (+ cx (* (aref *dir-dx* facing) needle-r))
                (+ cy (* (aref *dir-dy* facing) needle-r)))
          (loop for d below 4
                collect (list (char "NESW" d)
                              (+ cx (* (aref *dir-dx* d) r))
                              (+ cy (* (aref *dir-dy* d) r))
                              (= d facing))))))
