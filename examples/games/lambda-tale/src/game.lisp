;;; Lambda's Tale — game state and movement.

(in-package :tale)

(defstruct (game (:constructor %make-game))
  map                 ; dungeon-map
  knowledge           ; map-knowledge
  (x 0)
  (y 0)
  (facing +north+))   ; direction index 0..3

(defun observe (game)
  "Record what the party can see from its position into the automap:
the standing cell fully, and for each cell in the view cone its front and
side walls plus the front walls seen through open sides."
  (let ((k (game-knowledge game))
        (f (game-facing game)))
    (know-cell k (game-x game) (game-y game))
    (dolist (s (compute-view (game-map game) (game-x game) (game-y game) f))
      (know-wall k (view-slice-cx s) (view-slice-cy s) f)
      (know-wall k (view-slice-cx s) (view-slice-cy s) (turn-dir f -1))
      (know-wall k (view-slice-cx s) (view-slice-cy s) (turn-dir f 1))
      (when (view-slice-lx s)
        (know-wall k (view-slice-lx s) (view-slice-ly s) f))
      (when (view-slice-rx s)
        (know-wall k (view-slice-rx s) (view-slice-ry s) f)))))

(defun new-game (map)
  "Start a fresh game on MAP at its start position."
  (let ((g (%make-game :map map
                       :knowledge (make-map-knowledge map)
                       :x (dungeon-map-start-x map)
                       :y (dungeon-map-start-y map)
                       :facing (dir-index (dungeon-map-start-facing map)))))
    (observe g)
    g))

(defun turn-left (game)
  (setf (game-facing game) (turn-dir (game-facing game) -1))
  (observe game)
  (dir-keyword (game-facing game)))

(defun turn-right (game)
  (setf (game-facing game) (turn-dir (game-facing game) 1))
  (observe game)
  (dir-keyword (game-facing game)))

(defun turn-around (game)
  (setf (game-facing game) (dir-opposite (game-facing game)))
  (observe game)
  (dir-keyword (game-facing game)))

(defun move-party (game &optional (relative :forward))
  "Attempt to step the party one cell.  RELATIVE is :forward or :back
\(a Bard's Tale back-step keeps the current facing).  Returns
:moved, :door (stepped through a door) or :blocked."
  (let* ((dir (ecase relative
                (:forward (game-facing game))
                (:back (dir-opposite (game-facing game)))))
         (wall (cell-wall (game-map game) (game-x game) (game-y game) dir)))
    (if (not (wall-passable-p wall))
        :blocked
        (multiple-value-bind (nx ny)
            (neighbor (game-map game) (game-x game) (game-y game) dir)
          (if (null nx)
              :blocked
              (progn
                (setf (game-x game) nx
                      (game-y game) ny)
                (observe game)
                (if (eq wall :door) :door :moved)))))))
