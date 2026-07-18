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
  rx ry right-front)  ; right side cell + its front wall (when right is :open)

(defun compute-view (map x y facing)
  "List of VIEW-SLICEs visible from (X,Y) looking FACING, nearest first.
Stops at a solid or door front wall, an off-map edge, or +VIEW-DEPTH+."
  (let* ((f (dir-index facing))
         (ldir (turn-dir f -1))
         (rdir (turn-dir f 1))
         (slices '())
         (cx x)
         (cy y))
    (dotimes (d +view-depth+)
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
        (push (%make-view-slice :depth d :cx cx :cy cy
                                :front front :left left :right right
                                :lx lx :ly ly :left-front lf
                                :rx rx :ry ry :right-front rf)
              slices)
        ;; Advance only through an open front (doors block the view).
        (unless (eq front :open)
          (return))
        (multiple-value-bind (nx ny) (neighbor map cx cy f)
          (if nx
              (setf cx nx cy ny)
              (return)))))
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
                        (when (view-slice-left-front s)
                          (rect px0 qy0 qx0 qy1)
                          (when (eq (view-slice-left-front s) :door)
                            (door-mark px0 qy0 qx0 qy1)))))
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
                        (when (view-slice-right-front s)
                          (rect qx1 qy0 px1 qy1)
                          (when (eq (view-slice-right-front s) :door)
                            (door-mark qx1 qy0 px1 qy1)))))
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
  "Screen slot (X Y W H) of PIECE for the plane set PLANES."
  (destructuring-bind (kind depth &optional side) piece
    (destructuring-bind (px0 py0 px1 py1) (aref planes depth)
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
           (if (eq side :l)
               (list px0 qy0 (1+ (- qx0 px0)) (1+ (- qy1 qy0)))
               (list qx1 qy0 (1+ (- px1 qx1)) (1+ (- qy1 qy0))))))))))

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

(defun view-blit-list (slices planes)
  "Flatten SLICES into blit records (PIECE X Y W H), back to front.
The bitmap twin of VIEW-DISPLAY-LIST — same wall logic, but each wall
becomes one fixed-slot piece blit instead of wireframe lines."
  (let ((blits '()))
    (labels ((blit (kind depth &optional side)
               (let ((piece (if side
                                (list kind depth side)
                                (list kind depth))))
                 (push (cons piece (wall-piece-rect planes piece)) blits)))
             (draw-slice (s)
               (let ((d (view-slice-depth s)))
                 ;; sides first, then the front wall on top of their seams
                 (dolist (side '(:l :r))
                   (let ((wall (if (eq side :l)
                                   (view-slice-left s)
                                   (view-slice-right s)))
                         (beyond (if (eq side :l)
                                     (view-slice-left-front s)
                                     (view-slice-right-front s))))
                     (case wall
                       (:wall (blit :side d side))
                       (:door (blit :side-door d side))
                       (:open
                        (case beyond
                          (:wall (blit :flank d side))
                          (:door (blit :flank-door d side)))))))
                 (case (view-slice-front s)
                   (:wall (blit :front d))
                   (:door (blit :front-door d))))))
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
    (format stream "Palette: pens 0-3 are fixed UI colors (black, white, ~
grey, amber);~%pens 4-~D belong to the pack — taken from palette.iff's ~
CMAP when present,~%else from front-0.iff's (custom screen only; a ~
Workbench window keeps~%the Workbench palette).~%"
            (1- (ash 1 (display-profile-screen-depth *display-profile*))))
    (format stream "Transparency: in a WALL piece pen 0 is transparent — ~
the ceiling/~%floor backdrop shows through it — so paint solid black ~
with pen 4, not~%pen 0.  The ceiling/floor backdrops are opaque; pen 0 ~
there is plain black.~%")
    n))

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
