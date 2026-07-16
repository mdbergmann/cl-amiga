;;; Lambda's Tale — game state and movement.

(in-package :tale)

(defstruct (game (:constructor %make-game))
  map                 ; dungeon-map
  knowledge           ; map-knowledge
  (x 0)
  (y 0)
  (facing +north+)    ; direction index 0..3
  party               ; list of HERO (NIL for a bare walkabout)
  (flags (make-hash-table :test 'equal)) ; story flags (see events.lisp)
  handlers            ; event subscriptions: alist topic -> handler list
  combat              ; active COMBAT or NIL
  effects)            ; active spell effects (shield, lamp, ...), see below

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

(defun new-game (map &key party)
  "Start a fresh game on MAP at its start position, with PARTY (a list
of heroes; NIL for a bare walkabout).  The start cell's special is NOT
triggered here — subscribe your event handlers first, then call
TRIGGER-SPECIAL once."
  (let ((g (%make-game :map map
                       :knowledge (make-map-knowledge map)
                       :x (dungeon-map-start-x map)
                       :y (dungeon-map-start-y map)
                       :facing (dir-index (dungeon-map-start-facing map))
                       :party party)))
    (observe g)
    g))

;;; Active spell effects — the UI's spell strip (shield, lamp, ...).
;;; A placeholder for the coming spell system: story/test code manages
;;; the list, the front-ends render it.  Effects are transient — they
;;; do not live in save games yet (durations belong to the real spell
;;; system, see specs/ui-and-engine.md).

(defun add-effect (game name)
  "Add active effect NAME (a string or symbol); duplicates are ignored.
Returns the effect list."
  (setf (game-effects game)
        (append (game-effects game)
                (unless (member name (game-effects game) :test #'equal)
                  (list name))))
  (game-effects game))

(defun remove-effect (game name)
  "Remove active effect NAME.  Returns the remaining effect list."
  (setf (game-effects game)
        (remove name (game-effects game) :test #'equal)))

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
:moved, :door (stepped through a door) or :blocked.  Entering a cell
emits :ENTER-CELL and triggers the cell's special; bumping a wall
emits :BLOCKED.  Signals an error during combat — there is no walking
away from a fight (see ATTEMPT-FLEE)."
  (when (game-combat game)
    (error "move-party: the party is in combat (attack or flee first)"))
  (let* ((dir (ecase relative
                (:forward (game-facing game))
                (:back (dir-opposite (game-facing game)))))
         (wall (cell-wall (game-map game) (game-x game) (game-y game) dir)))
    (if (not (wall-passable-p wall))
        (progn
          (emit game :blocked (dir-keyword dir))
          :blocked)
        (multiple-value-bind (nx ny)
            (neighbor (game-map game) (game-x game) (game-y game) dir)
          (if (null nx)
              (progn
                (emit game :blocked (dir-keyword dir))
                :blocked)
              (progn
                (setf (game-x game) nx
                      (game-y game) ny)
                (observe game)
                (emit game :enter-cell nx ny)
                (trigger-special game)
                (if (eq wall :door) :door :moved)))))))
