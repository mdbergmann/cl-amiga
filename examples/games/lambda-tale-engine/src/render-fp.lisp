;;; Lambda's Tale — first-person ASCII renderer (host development view).
;;;
;;; Rasterizes the backend-independent display list from view.lisp into a
;;; character grid: '-' '|' for straight edges, '/' '\' for receding wall
;;; edges, '+' at straight-line endpoints (corners), 'D' for doors.

(in-package :tale)

(defun %raster-line (grid x0 y0 x1 y1)
  (let ((dx (- x1 x0))
        (dy (- y1 y0)))
    (cond ((and (zerop dx) (zerop dy))
           (%grid-put grid x0 y0 #\+))
          ((zerop dy)                   ; horizontal
           (let ((step (if (plusp dx) 1 -1)))
             (do ((x x0 (+ x step)))
                 ((= x x1))
               (%grid-put grid x y0 #\-)))
           (%grid-put grid x0 y0 #\+)
           (%grid-put grid x1 y1 #\+))
          ((zerop dx)                   ; vertical
           (let ((step (if (plusp dy) 1 -1)))
             (do ((y y0 (+ y step)))
                 ((= y y1))
               (%grid-put grid x0 y #\|)))
           (%grid-put grid x0 y0 #\+)
           (%grid-put grid x1 y1 #\+))
          (t                            ; diagonal: endpoints left to the
           (let* ((n (max (abs dx) (abs dy)))   ; straight lines that join it
                  (ch (if (eq (plusp dx) (plusp dy)) #\\ #\/)))
             (do ((i 1 (1+ i)))
                 ((>= i n))
               (%grid-put grid
                          (+ x0 (round (* i dx) n))
                          (+ y0 (round (* i dy) n))
                          ch)))))))

(defun render-first-person (game &key (width 33) (height 17))
  "Wireframe first-person view of GAME as a multi-line string."
  (let ((grid (%make-grid width height))
        (slices (compute-view (game-map game) (game-x game) (game-y game)
                              (game-facing game) (render-view-depth game)))
        (planes (view-planes width height)))
    (dolist (prim (view-display-list slices planes))
      (ecase (first prim)
        (:line (destructuring-bind (x0 y0 x1 y1) (rest prim)
                 (%raster-line grid x0 y0 x1 y1)))
        (:door (destructuring-bind (cx cy hw hh) (rest prim)
                 (declare (ignore hw hh))
                 (%grid-put grid cx cy #\D)))))
    (%grid->string grid)))
