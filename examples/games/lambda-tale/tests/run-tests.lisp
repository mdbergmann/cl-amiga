;;; Lambda's Tale — test suite (M0: map model, movement, knowledge, renderer).
;;; Run from the project root (examples/games/lambda-tale):  make test

(load "src/load.lisp")

(in-package :tale)

;;; ---------------------------------------------------------------------
;;; Tiny test harness

(defvar *checks* 0)
(defvar *failures* 0)

(defun check (label expected actual)
  (incf *checks*)
  (unless (equal expected actual)
    (incf *failures*)
    (format t "FAIL ~A~%  expected: ~S~%  actual:   ~S~%" label expected actual)))

(defun check-true (label value)
  (check label t (not (not value))))

(defmacro check-error (label &body body)
  "Pass when BODY signals an error."
  `(check-true ,label
               (handler-case (progn ,@body nil)
                 (error () t))))

;;; ---------------------------------------------------------------------
;;; Test maps

;; 3x2 map: doors, features, fully walled border.
;;   (0,0) start, east open; (1,0)/(1,1) connected by a door;
;;   '<' feature (stairs up) at (2,1).
(defparameter *art*
"+-+-+-+
|@  | |
+ +D+ +
| |  <|
+-+-+-+")

;; 2x1 wrapping map with open borders (short second line pads to :open).
(defparameter *wrap-art*
"+ + +
|
+ + +")

;;; ---------------------------------------------------------------------
;;; Direction helpers

(check "dir-index keyword" 2 (dir-index :south))
(check "dir-index index passthrough" 3 (dir-index 3))
(check "dir-keyword" :west (dir-keyword 3))
(check "dir-opposite" +south+ (dir-opposite :north))
(check "turn right from west wraps to north" +north+ (turn-dir :west 1))
(check "turn left from north wraps to west" +west+ (turn-dir :north -1))
(check-error "dir-index rejects garbage" (dir-index :up))

;;; ---------------------------------------------------------------------
;;; Map parsing

(let ((m (parse-map *art* :name "test")))
  (check "width" 3 (dungeon-map-width m))
  (check "height" 2 (dungeon-map-height m))
  (check "start-x" 0 (dungeon-map-start-x m))
  (check "start-y" 0 (dungeon-map-start-y m))
  (check "start-facing" :north (dungeon-map-start-facing m))
  (check "no wrap by default" nil (dungeon-map-wrap m))

  (check "north border is wall" :wall (cell-wall m 0 0 :north))
  (check "west border is wall" :wall (cell-wall m 0 0 :west))
  (check "open east edge" :open (cell-wall m 0 0 :east))
  (check "open south edge" :open (cell-wall m 0 0 :south))
  (check "door south of (1,0)" :door (cell-wall m 1 0 :south))
  (check "same door north of (1,1)" :door (cell-wall m 1 1 :north))
  (check "interior wall east of (1,0)" :wall (cell-wall m 1 0 :east))
  (check "interior wall west of (2,0)" :wall (cell-wall m 2 0 :west))

  (check "feature at (2,1)" #\< (cell-feature m 2 1))
  (check "no feature at (0,0)" nil (cell-feature m 0 0))
  (check "start glyph not stored as feature" nil (cell-feature m 1 0))

  (check "wall not passable" nil (wall-passable-p :wall))
  (check-true "door passable" (wall-passable-p :door))
  (check-true "open passable" (wall-passable-p :open))

  (multiple-value-bind (nx ny) (neighbor m 1 0 :south)
    (check "neighbor south" '(1 1) (list nx ny)))
  (check "neighbor off-map is nil (no wrap)" nil (neighbor m 0 0 :west)))

;; :start-facing overrides the default facing; '>' in a cell is a plain
;; feature (stairs down), not a start glyph.
(let ((m (parse-map "+-+
|>|
+-+" :start-facing :east)))
  (check "start-facing argument" :east (dungeon-map-start-facing m))
  (check "'>' is a feature, not a start glyph" #\> (cell-feature m 0 0))
  (check "default start position" 0 (dungeon-map-start-x m)))

(check-error "invalid wall char rejected"
  (parse-map "+-+
|@x
+-+"))
(check-error "even-sized art rejected"
  (parse-map "++
++"))

;;; ---------------------------------------------------------------------
;;; Movement

(let* ((m (parse-map *art* :name "test"))
       (g (new-game m)))
  (check "game starts at start pos" '(0 0) (list (game-x g) (game-y g)))
  (check "game starts facing north" +north+ (game-facing g))

  (check "forward into wall blocked" :blocked (move-party g :forward))
  (check "blocked move keeps position" '(0 0) (list (game-x g) (game-y g)))

  (check "turn right" :east (turn-right g))
  (check "forward through open edge" :moved (move-party g :forward))
  (check "moved east" '(1 0) (list (game-x g) (game-y g)))

  (check "turn right again" :south (turn-right g))
  (check "forward through door reports :door" :door (move-party g :forward))
  (check "moved through door" '(1 1) (list (game-x g) (game-y g)))

  ;; Back-step keeps the facing.
  (check "back-step through door" :door (move-party g :back))
  (check "back-step position" '(1 0) (list (game-x g) (game-y g)))
  (check "back-step kept facing" +south+ (game-facing g))

  (check "turn left" :east (turn-left g))
  (check "turn around" :west (turn-around g)))

;;; ---------------------------------------------------------------------
;;; Wrapping maps

(let* ((m (parse-map *wrap-art* :wrap t))
       (g (new-game m)))
  (check "wrap map width" 2 (dungeon-map-width m))
  (check "wrap map height" 1 (dungeon-map-height m))
  (check-true "wrap flag" (dungeon-map-wrap m))

  ;; North edge open, height 1: wraps back to the same cell.
  (check "wrap north" :moved (move-party g :forward))
  (check "wrap north lands on same cell" '(0 0) (list (game-x g) (game-y g)))

  (check "west wall blocks despite wrap" :blocked
         (progn (turn-left g) (move-party g :forward)))

  (turn-around g)                       ; face east
  (check "move east" :moved (move-party g :forward))
  (check "at (1,0)" '(1 0) (list (game-x g) (game-y g)))
  ;; East edge of (1,0) is open; wrapping enters (0,0) even though (0,0)'s
  ;; west side is a wall — movement uses the current cell's wall only
  ;; (one-way walls are a feature, not a bug).
  (check "wrap east through one-way boundary" :moved (move-party g :forward))
  (check "wrapped to (0,0)" '(0 0) (list (game-x g) (game-y g))))

;; Same map without :wrap — the open border edge leads off-map: blocked.
(let* ((m (parse-map *wrap-art*))
       (g (new-game m)))
  (check "open border edge blocked without wrap" :blocked
         (move-party g :forward)))

;;; ---------------------------------------------------------------------
;;; First-person view geometry

(let* ((m (parse-map *art* :name "test"))
       (v (compute-view m 0 0 :north)))
  (check "view blocked at depth 0" 1 (length v))
  (let ((s (first v)))
    (check "slice front" :wall (view-slice-front s))
    (check "slice left" :wall (view-slice-left s))
    (check "slice right" :open (view-slice-right s))
    (check "right side cell" '(1 0)
           (list (view-slice-rx s) (view-slice-ry s)))
    (check "right side front wall" :wall (view-slice-right-front s))
    (check "no left side cell behind wall" nil (view-slice-lx s))))

(let* ((m (parse-map *art* :name "test"))
       (v (compute-view m 0 0 :east)))
  (check "corridor view depth" 2 (length v))
  (let ((s0 (first v))
        (s1 (second v)))
    (check "near slice front open" :open (view-slice-front s0))
    (check "near slice right cell" '(0 1)
           (list (view-slice-rx s0) (view-slice-ry s0)))
    (check "far slice center cell" '(1 0)
           (list (view-slice-cx s1) (view-slice-cy s1)))
    (check "far slice front wall" :wall (view-slice-front s1))
    (check "far slice right door" :door (view-slice-right s1))
    (check "closed side door not seen through" nil (view-slice-rx s1))))

;; A door straight ahead blocks the view like a wall.
(let* ((m (parse-map *art* :name "test"))
       (v (compute-view m 1 0 :south)))
  (check "door blocks view" 1 (length v))
  (check "front door in slice" :door (view-slice-front (first v))))

;; An endless (wrapping) corridor is capped at +view-depth+ slices.
(let ((m (parse-map *wrap-art* :wrap t)))
  (check "wrap corridor capped at view depth"
         +view-depth+ (length (compute-view m 0 0 :north))))

(check "view-planes plane 1" '(6 3 26 13) (aref (view-planes 33 17) 1))
(check "view-planes plane 0 is viewport" '(0 0 32 16)
       (aref (view-planes 33 17) 0))

;;; ---------------------------------------------------------------------
;;; Knowledge

(let* ((m (parse-map *art* :name "test"))
       (g (new-game m))
       (k (game-knowledge g)))
  (check-true "start cell explored" (cell-explored-p k 0 0))
  (check "unvisited cell not explored" nil (cell-explored-p k 1 0))
  (check-true "start cell walls known" (wall-known-p k 0 0 :north))
  ;; The east side of the start cell is open, so the neighbor's front wall
  ;; is visible from the start — and recorded.
  (check-true "side cell front wall seen through opening"
              (wall-known-p k 1 0 :north))
  (check "out-of-view walls unknown" nil (wall-known-p k 1 1 :north))

  (turn-right g)
  (move-party g :forward)
  (check-true "explored after moving" (cell-explored-p k 1 0))
  (check-true "walls known after moving" (wall-known-p k 1 0 :south)))

;;; ---------------------------------------------------------------------
;;; Rendering

;; Omniscient render round-trips the source art (start glyph is not a
;; feature, so its cell renders blank).
(let ((m (parse-map *art* :name "test")))
  (check "omniscient render round-trips art"
         "+-+-+-+
|   | |
+ +D+ +
| |  <|
+-+-+-+"
         (render-dungeon m)))

;; Party arrow in the full view.
(let* ((m (parse-map *art* :name "test"))
       (g (new-game m)))
  (turn-right g)
  (move-party g :forward)
  (check "full render with party arrow"
         "+-+-+-+
|  >| |
+ +D+ +
| |  <|
+-+-+-+"
         (render-game g :full t)))

;; Knowledge-filtered render of a fresh game: the start cell's walls plus
;; the neighbor's front wall (seen through the open east side) and the
;; party arrow.
(let* ((m (parse-map *art* :name "test"))
       (g (new-game m)))
  (check "knowledge render shows explored cell and seen walls"
         "+-+-+
|^
+

"
         (render-game g)))

;; After walking through the door the door and new walls appear; the
;; unexplored feature cell (2,1) stays hidden.
(let* ((m (parse-map *art* :name "test"))
       (g (new-game m)))
  (turn-right g)
  (move-party g :forward)
  (turn-right g)
  (move-party g :forward)
  (let ((view (render-game g)))
    (check-true "door visible after passing it" (search "D" view))
    (check "hidden feature not rendered" nil (search "<" view))))

;;; ---------------------------------------------------------------------
;;; First-person ASCII renderer

;; Facing north at the start of *art*: solid left wall (trapezoid), open
;; right side showing the neighbor's front wall, solid front at plane 1.
(let* ((m (parse-map *art* :name "test"))
       (g (new-game m))
       (lines (%split-lines (render-first-person g))))
  (check "fp viewport height" 17 (length lines))
  (check "fp front wall and right opening line"
         "|     +-------------------+-----+"
         (nth 3 lines))
  (check-true "fp left wall receding edge" (find #\\ (nth 1 lines))))

;; A door straight ahead renders a 'D' marker centered on the front wall.
(let* ((m (parse-map *art* :name "test"))
       (g (new-game m)))
  (turn-right g)
  (move-party g :forward)
  (turn-right g)                        ; at (1,0) facing the door south
  (let ((lines (%split-lines (render-first-person g))))
    (check "fp door marker centered" #\D (char (nth 8 lines) 16))))

;;; ---------------------------------------------------------------------
;;; Sample map loads

(let ((m (load-map-file "data/cellar.map")))
  (check "cellar width" 6 (dungeon-map-width m))
  (check "cellar height" 5 (dungeon-map-height m))
  (check "cellar stairs down" #\> (cell-feature m 3 2))
  (check "cellar stairs up" #\< (cell-feature m 5 4)))

;;; ---------------------------------------------------------------------
;;; Amiga front-end smoke test (real Intuition window + graphics.library
;;; calls; only runs when this suite is loaded under AmigaOS clamiga).

#+amigaos
(let* ((m (parse-map *art* :name "test"))
       (g (new-game m)))
  (check "amiga-ui draws into a real window without error" t
         (amiga.intuition:with-window
             (win :title "Lambda's Tale Test"
                  :left 20 :top 20
                  :width *amiga-win-width* :height *amiga-win-height*
                  :idcmp amiga.intuition:+idcmp-closewindow+)
           (let* ((rp (amiga.intuition:window-rastport win))
                  (bx (+ (amiga.intuition:window-border-left win) 6))
                  (by (+ (amiga.intuition:window-border-top win) 6))
                  (map-x (+ bx *amiga-fp-width* 16))
                  (map-w (- *amiga-win-width* map-x 10))
                  (status-y (+ by *amiga-fp-height* 18)))
             (%amiga-draw-fp rp g bx by *amiga-fp-width* *amiga-fp-height*)
             (%amiga-draw-map rp g map-x by map-w *amiga-fp-height* nil)
             (%amiga-status rp g bx status-y
                            (- *amiga-win-width* bx 10) "Smoke test")
             t))))

;;; ---------------------------------------------------------------------
;;; Summary

(format t "~%Lambda's Tale tests: ~D checks, ~D failures.~%" *checks* *failures*)
(cl-user::quit (if (zerop *failures*) 0 1))
