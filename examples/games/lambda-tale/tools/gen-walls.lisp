;;; Lambda's Tale — procedural wall-art generator (M3).
;;;
;;; Draws every wall piece named by WALL-PIECE-NAMES as a 4-color image
;;; (the dungeon palette: black, white, grey, amber) sized to its
;;; fixed screen slot for the *FP-VIEW-WIDTH* x *FP-VIEW-HEIGHT*
;;; perspective planes, and writes them as ILBM files into data/gfx/.
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
;;; look over, amber doors.

(in-package :tale)

;;; The dungeon palette, 8-bit components (SET-RGB4 nibbles x 17):
;;; pen 0 black, pen 1 white, pen 2 grey, pen 3 amber.
(defparameter *wall-palette*
  #((0 0 0) (255 255 255) (136 136 136) (255 170 51)))

(defconstant +pen-bg+ 0)
(defconstant +pen-edge+ 1)
(defconstant +pen-brick+ 2)
(defconstant +pen-door+ 3)

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

(defun %draw-front-wall (w h &key door)
  "A flat brick wall filling W x H (front and flank pieces share the
wall height at a given depth, so their brick courses line up).  DOOR
non-NIL puts an amber door in the middle."
  (let ((img (make-image w h 2 :palette *wall-palette*))
        (course (max 3 (round h *brick-courses*)))
        (brick (max 6 (round w 5))))
    (%img-fill img 0 0 (1- w) (1- h) +pen-brick+)
    ;; mortar: courses and running-bond joints
    (loop for y from course below h by course
          for row from 1
          do (%img-fill img 0 y (1- w) y +pen-bg+)
             (loop for x from (if (evenp row) 0 (floor brick 2))
                     below w by brick
                   do (%img-fill img x (- y course) x (1- y) +pen-bg+)))
    (loop for x from (floor brick 2) below w by brick
          do (%img-fill img x (* course (floor (1- h) course)) x (1- h)
                        +pen-bg+))
    ;; white edge highlight all around (the wireframe look)
    (%img-fill img 0 0 (1- w) 0 +pen-edge+)
    (%img-fill img 0 (1- h) (1- w) (1- h) +pen-edge+)
    (%img-fill img 0 0 0 (1- h) +pen-edge+)
    (%img-fill img (1- w) 0 (1- w) (1- h) +pen-edge+)
    (when door
      (let* ((dw (max 3 (floor w 3)))
             (dh (max 4 (floor (* 3 h) 4)))
             (dx (floor (- w dw) 2))
             (dy (- h 1 dh)))
        (%img-fill img dx dy (+ dx dw -1) (- h 2) +pen-door+)
        ;; black frame + knob
        (%img-fill img dx dy (+ dx dw -1) dy +pen-bg+)
        (%img-fill img dx dy dx (- h 2) +pen-bg+)
        (%img-fill img (+ dx dw -1) dy (+ dx dw -1) (- h 2) +pen-bg+)
        (when (> dh 8)
          (let ((ky (+ dy (floor dh 2))))
            (%img-fill img (+ dx dw -3) ky (+ dx dw -3) (1+ ky) +pen-bg+)))))
    img))

;;; ---------------------------------------------------------------------
;;; Receding side walls (:side — trapezoids with baked ceiling/floor)

(defun %draw-side-wall (w h top-far bot-far &key door)
  "A left-hand receding wall in a W x H band: the near edge (x = 0)
spans the full height, the far edge (x = W-1) spans TOP-FAR..BOT-FAR.
Ceiling and floor corners stay background.  DOOR puts a
perspective-skewed amber door on the wall.  Right-hand pieces are the
mirror image (see %IMG-MIRROR-X)."
  (let ((img (make-image w h 2 :palette *wall-palette*)))
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
                   (setf (pixel-ref img x y) +pen-bg+))))
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
                                  do (setf (pixel-ref img x y) +pen-bg+))))))
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
                                        +pen-bg+
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
             (%draw-front-wall w h :door (eq kind :flank-door)))
            ((:side :side-door)
             (let ((img (%draw-side-wall w h
                                         (- qy0 py0) (- qy1 py0)
                                         :door (eq kind :side-door))))
               (if (eq side :r) (%img-mirror-x img) img)))))))))

(defun generate-wall-assets (&key (dir "data/gfx/"))
  "Draw all wall pieces and write them as ILBM files into DIR.
Returns the number of files written."
  (let ((planes (view-planes *fp-view-width* *fp-view-height*))
        (n 0))
    (ensure-directories-exist dir)
    (dolist (piece (wall-piece-names) n)
      (write-ilbm (draw-wall-piece piece planes)
                  (concatenate 'string dir (wall-piece-file piece)))
      (incf n))))
