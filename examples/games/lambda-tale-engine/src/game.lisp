;;; Lambda's Tale — game state and movement.

(in-package :tale)

(defstruct (game (:constructor %make-game))
  map                 ; dungeon-map — the zone the party is in
  knowledge           ; map-knowledge for the current zone
  (x 0)
  (y 0)
  (facing +north+)    ; direction index 0..3
  (time *new-game-minutes*) ; game minutes since campaign start (time.lisp)
  party               ; list of HERO (NIL for a bare walkabout)
  (flags (make-hash-table :test 'equal)) ; story flags (see events.lisp)
  handlers            ; event subscriptions: alist topic -> handler list
  combat              ; active COMBAT or NIL
  effects             ; active EFFECT records (shield, light, ...), see below
  location            ; active LOCATION (shop, ...) or NIL, see locations.lisp
  ;; The world: every zone the party has visited this session, keyed by
  ;; map file path -> (MAP . KNOWLEDGE).  TRAVEL-PARTY switches zones,
  ;; keeping each zone's map and automap knowledge alive.
  (zones (make-hash-table :test 'equal))
  ;; Automap knowledge of zones restored from a save but not yet
  ;; revisited: alist path -> knowledge row lists (see save.lisp).
  zone-knowledge)

(defun observe (game)
  "Record what the party can see from its position into the automap:
the standing cell fully, and for each cell in the view cone its front and
side walls plus the front walls seen through open sides.

The cone is GAME-VIEW-DEPTH — what the light allows — NOT the drawn
RENDER-VIEW-DEPTH: the draw-distance knob is a speed setting, and a
player who lowers it must still map what the party could see."
  (let ((k (game-knowledge game))
        (f (game-facing game)))
    (know-cell k (game-x game) (game-y game))
    (dolist (s (compute-view (game-map game) (game-x game) (game-y game) f
                             (game-view-depth game)))
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
    (setf (gethash (dungeon-map-name map) (game-zones g))
          (cons map (game-knowledge g)))
    (observe g)
    g))

;;; ---------------------------------------------------------------------
;;; The world: travel between zones (cities, dungeons — all just maps).

(defun %resolve-map-path (base file)
  "Resolve FILE relative to the directory of BASE (the current map's
file path).  FILE stays as-is when it is absolute (leading '/' or an
Amiga volume ':')."
  (if (or (and (> (length file) 0) (char= (char file 0) #\/))
          (find #\: file))
      file
      (let* ((slash (position #\/ base :from-end t))
             (colon (position #\: base :from-end t))
             (cut (cond ((and slash colon) (max slash colon))
                        (slash slash)
                        (colon colon))))
        (if cut
            (concatenate 'string (subseq base 0 (1+ cut)) file)
            file))))

(defun load-campaign (map-file)
  "Load the campaign that belongs to MAP-FILE: the campaign.lisp in
the same directory as the map (hero classes, monsters, items, the
starting party — see the Closure game's worlds/closure/campaign.lisp
for a worked example).  A world is a directory of map files plus its
campaign.lisp; the front-ends call this so a designer's own world
brings its own definitions.  Returns the loaded path, or NIL when
there is none."
  (let ((path (%resolve-map-path map-file "campaign.lisp")))
    (when (probe-file path)
      (dlog-timed ("campaign ~A" path)
        (load path))
      path)))

(defun zone-gfx-dir (game)
  "The current zone's declared tile pack, or NIL: the map's
(ZONE :GFX DIR), resolved in two steps so worlds stay portable —
relative to the map file's directory when the pack lives there (a
self-contained world directory), else relative to the game directory
(a pack the game ships beside its worlds).  The probe is the front-0.iff
every pack must hold; a wrong directory still surfaces through the
wall loader's loud wireframe fallback."
  (let* ((map (game-map game))
         (gfx (dungeon-map-gfx map)))
    (when gfx
      (let ((local (%resolve-map-path (dungeon-map-name map) gfx)))
        (if (probe-file (concatenate 'string local "front-0.iff"))
            local
            gfx)))))

(defun travel-party (game file &optional x y facing)
  "Move the party to another zone: the map at FILE, resolved relative
to the current map's directory.  The party arrives at cell (X,Y) facing
FACING when given, else at the target map's start.  A zone already
visited keeps its map and automap knowledge; a new one is loaded from
disk.  Emits :ENTER-ZONE and :ENTER-CELL and triggers the arrival
cell's special."
  (when (game-combat game)
    (error "travel-party: the party is in combat"))
  (let* ((path (%resolve-map-path (dungeon-map-name (game-map game)) file))
         (zone (gethash path (game-zones game)))
         (map (car zone))
         (knowledge (cdr zone)))
    (unless zone
      (setf map (load-map-file path)
            knowledge (make-map-knowledge map))
      ;; A save may carry this zone's automap knowledge from an earlier
      ;; visit — restore it on first (re)entry.
      (let ((pending (assoc path (game-zone-knowledge game) :test #'equal)))
        (when pending
          (%rows->knowledge knowledge (cdr pending))
          (setf (game-zone-knowledge game)
                (remove pending (game-zone-knowledge game)))))
      (setf (gethash path (game-zones game)) (cons map knowledge)))
    (let ((tx (or x (dungeon-map-start-x map)))
          (ty (or y (dungeon-map-start-y map))))
      (unless (and (integerp tx) (< -1 tx (dungeon-map-width map))
                   (integerp ty) (< -1 ty (dungeon-map-height map)))
        (error "Travel target (~S,~S) is outside the ~Dx~D map ~A"
               tx ty (dungeon-map-width map) (dungeon-map-height map) path))
      (setf (game-map game) map
            (game-knowledge game) knowledge
            (game-x game) tx
            (game-y game) ty
            (game-facing game)
            (dir-index (or facing (dungeon-map-start-facing map))))
      (observe game)
      (emit game :enter-zone map)
      (say game "You enter ~A." (map-title map))
      (emit game :enter-cell tx ty)
      (trigger-special game))))

;;; Active effects — the UI's spell strip (shield, light, ...).
;;; An effect is a record: a display name, an optional expiry on the
;;; game clock (ADVANCE-TIME announces and drops it, see time.lisp),
;;; a payload plist of engine facts the mechanics read:
;;;   (:ac N)      party armor class bonus (see HERO-EFFECTIVE-AC)
;;;   (:light t)   the party carries light (see GAME-DARK-P)
;;;   (:compass t) the party knows its facing (see COMPASS-ACTIVE-P)
;;; and an optional icon image — a file name the front-end resolves
;;; and draws in the effects band (NIL = the text label alone).
;;; Effects live in save games (see save.lisp).

(defstruct (effect (:constructor %make-effect))
  name          ; display key (string or symbol); EQUAL identity
  expires-at    ; game minute the effect ends, or NIL (until removed)
  payload       ; readable plist of engine facts: (:ac N), (:light t)
  image)        ; icon file name for the effects band, or NIL

(defun add-effect (game name &key duration payload image)
  "Add active effect NAME (a string or symbol).  DURATION minutes from
now sets the expiry (NIL = until removed); PAYLOAD is a readable plist
of engine facts; IMAGE names the effect's icon file (NIL = text only).
Re-adding NAME refreshes its expiry, payload and image in place — a
recast spell burns anew, keeping its spot in the strip.
Returns the effect list."
  (let ((expires (when duration (+ (game-time game) duration)))
        (existing (find-effect game name)))
    (if existing
        (setf (effect-expires-at existing) expires
              (effect-payload existing) payload
              (effect-image existing) image)
        (setf (game-effects game)
              (append (game-effects game)
                      (list (%make-effect :name name
                                          :expires-at expires
                                          :payload payload
                                          :image image))))))
  (game-effects game))

(defun remove-effect (game name)
  "Remove active effect NAME.  Returns the remaining effect list."
  (setf (game-effects game)
        (remove name (game-effects game)
                :key #'effect-name :test #'equal)))

(defun find-effect (game name)
  "The active EFFECT named NAME, or NIL."
  (find name (game-effects game) :key #'effect-name :test #'equal))

(defun effect-label (effect)
  "EFFECT's display string for the UI's effects strip."
  (string-downcase (princ-to-string (effect-name effect))))

(defun effect-image-path (game effect)
  "EFFECT's icon file resolved like a zone tile pack — relative to the
current map file's directory, so a self-contained world directory
carries its own icons — or NIL when the effect has none."
  (let ((image (effect-image effect)))
    (when image
      (%resolve-map-path (dungeon-map-name (game-map game)) image))))

(defun effects-ac-bonus (game)
  "The summed :AC bonuses of the active effects (a party-wide shield)."
  (let ((n 0))
    (dolist (e (game-effects game) n)
      (incf n (or (getf (effect-payload e) :ac) 0)))))

(defun light-active-p (game)
  "True when any active effect carries light (a :LIGHT payload)."
  (and (some (lambda (e) (getf (effect-payload e) :light))
             (game-effects game))
       t))

(defun compass-active-p (game)
  "True when any active effect orients the party (a :COMPASS payload) —
only then do the front-ends show the compass rose and the facing."
  (and (some (lambda (e) (getf (effect-payload e) :compass))
             (game-effects game))
       t))

(defun apply-effect-spec (game name spec &key image extra-payload)
  "Install SPEC — (:buff-ac N :duration M), (:light t :duration M) or
\(:compass t :duration M), the shared timed-effect vocabulary spells,
usable items and songs speak — as active effect NAME with icon IMAGE.
EXTRA-PAYLOAD is appended to the payload (a song's :SONG marker).
Returns the effect list; rejects a spec naming no known effect."
  (let ((payload
          (cond ((getf spec :buff-ac) (list :ac (getf spec :buff-ac)))
                ((getf spec :light) '(:light t))
                ((getf spec :compass) '(:compass t))
                (t (error "apply-effect-spec ~S: ~S names no timed ~
                           effect (:buff-ac, :light or :compass)"
                          name spec)))))
    (add-effect game name :duration (getf spec :duration)
                          :payload (append payload extra-payload)
                          :image image)))

(defun turn-left (game)
  (setf (game-facing game) (turn-dir (game-facing game) -1))
  (advance-time game)
  (observe game)
  (dir-keyword (game-facing game)))

(defun turn-right (game)
  (setf (game-facing game) (turn-dir (game-facing game) 1))
  (advance-time game)
  (observe game)
  (dir-keyword (game-facing game)))

(defun turn-around (game)
  (setf (game-facing game) (dir-opposite (game-facing game)))
  (advance-time game)
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
  (when (game-location game)
    (error "move-party: the party is inside ~A (LEAVE-LOCATION first)"
           (location-title (game-location game))))
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
                ;; The step costs time before the party looks around:
                ;; a light that gutters out right now shrinks what this
                ;; very step maps, and an AT-NIGHT special on the target
                ;; cell must see the post-step clock.
                (advance-time game)
                (observe game)
                (emit game :enter-cell nx ny)
                (trigger-special game)
                (if (eq wall :door) :door :moved)))))))
