;;; Lambda's Tale — test suite: map model, movement, knowledge, renderers
;;; (M0/M1); dice, events, specials, party, combat, save games (M2).
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

(defmacro with-rng ((&rest values) &body body)
  "Run BODY with *RNG* scripted: successive (roll N) calls return the
next of VALUES (mod N), then 0 forever."
  (let ((vals (gensym "VALS")))
    `(let* ((,vals (list ,@values))
            (*rng* (lambda (n) (mod (if ,vals (pop ,vals) 0) n))))
       ,@body)))

(defun watch-messages (game)
  "Subscribe to GAME's :MESSAGE events; returns a closure yielding the
messages so far (oldest first)."
  (let ((msgs '()))
    (on-event game :message
              (lambda (g text) (declare (ignore g)) (push text msgs)))
    (lambda () (reverse msgs))))

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
;;; Large maps and the minimap viewport (specs/ui-and-engine.md)

(defun %big-map-art (w h)
  "Art for a fully-walled WxH map with all interior edges open."
  (with-output-to-string (s)
    (dotimes (row (1+ (* 2 h)))
      (dotimes (col (1+ (* 2 w)))
        (write-char
         (cond ((and (evenp row) (evenp col)) #\+)
               ((evenp row)
                (if (or (= row 0) (= row (* 2 h))) #\- #\Space))
               ((evenp col)
                (if (or (= col 0) (= col (* 2 w))) #\| #\Space))
               (t #\Space))
         s))
      (write-char #\Newline s))))

;; 30x30 — the Bard's Tale I level size, the spec's minimum.
(let* ((m (parse-map (%big-map-art 30 30) :name "big30"))
       (g (new-game m)))
  (check "30x30 parses" '(30 30)
         (list (dungeon-map-width m) (dungeon-map-height m)))
  ;; viewport clamping: top-left corner, center, bottom-right corner
  (multiple-value-bind (x0 y0 w h) (map-viewport m 0 0 6 6)
    (check "viewport clamps at origin" '(0 0 6 6) (list x0 y0 w h)))
  (multiple-value-bind (x0 y0 w h) (map-viewport m 15 15 6 6)
    (check "viewport centers on the party" '(12 12 6 6) (list x0 y0 w h)))
  (multiple-value-bind (x0 y0 w h) (map-viewport m 29 29 6 6)
    (check "viewport clamps at the far corner" '(24 24 6 6)
           (list x0 y0 w h)))
  (multiple-value-bind (x0 y0 w h) (map-viewport m 29 0 6 6)
    (check "viewport clamps mixed edges" '(24 0 6 6) (list x0 y0 w h)))
  ;; walk the top corridor east and check movement + knowledge scale
  (turn-right g)
  (dotimes (i 10) (move-party g :forward))
  (check "movement across a big map" '(10 0) (list (game-x g) (game-y g)))
  (check-true "knowledge recorded far from origin"
              (cell-explored-p (game-knowledge g) 10 0)))

;; A viewport request larger than the map yields the whole map.
(let ((m (parse-map *art* :name "small")))
  (multiple-value-bind (x0 y0 w h) (map-viewport m 1 0 6 6)
    (check "viewport of a small map is the whole map" '(0 0 3 2)
           (list x0 y0 w h))))

;; Region rendering (the full map view's clamped window): a 6x6 region
;; is 13 art lines high, and the party arrow sits inside it.
(let* ((m (parse-map (%big-map-art 30 30) :name "big30"))
       (g (new-game m))
       (lines (%split-lines
               (multiple-value-bind (x0 y0 w h)
                   (map-viewport m (game-x g) (game-y g) 6 6)
                 (render-dungeon m :px (game-x g) :py (game-y g)
                                   :facing (game-facing g)
                                   :x0 x0 :y0 y0 :w w :h h)))))
  (check "region render is 6 cells high" 13 (length lines))
  (check "region render top border" "+-+-+-+-+-+-+" (first lines))
  (check "region render party arrow" "|^" (second lines)))

;; The region window follows the party: at the far corner the arrow
;; renders at the region's bottom-right cell, and cells outside the
;; region are not drawn.
(let* ((m (parse-map (%big-map-art 30 30) :name "big30"))
       (g (new-game m)))
  (setf (game-x g) 29 (game-y g) 29)
  (observe g)
  (let ((lines (%split-lines
                (multiple-value-bind (x0 y0 w h)
                    (map-viewport m 29 29 6 6)
                  (render-dungeon m :px 29 :py 29
                                    :facing (game-facing g)
                                    :x0 x0 :y0 y0 :w w :h h)))))
    (check "region render height at far corner" 13 (length lines))
    (check "region arrow at far corner" #\^
           (char (nth 11 lines) 11))))

;; 64x64: full save/load round-trip at flexible-map scale.
(let* ((m (parse-map (%big-map-art 64 64) :name "big64"))
       (g (new-game m)))
  (check "64x64 parses" '(64 64)
         (list (dungeon-map-width m) (dungeon-map-height m)))
  (turn-right g)
  (dotimes (i 5) (move-party g :forward))
  ;; save needs a map FILE to reference: write the art out
  (with-open-file (s "tests/tmp-big.map"
                     :direction :output :if-exists :supersede)
    (write-string (%big-map-art 64 64) s))
  (let ((g2 (new-game (load-map-file "tests/tmp-big.map"))))
    (turn-right g2)
    (dotimes (i 5) (move-party g2 :forward))
    (save-game g2 "tests/tmp-big.sav")
    (let ((g3 (load-game "tests/tmp-big.sav")))
      (check "64x64 save round-trips position" '(5 0)
             (list (game-x g3) (game-y g3)))
      (check-true "64x64 save round-trips knowledge"
                  (cell-explored-p (game-knowledge g3) 3 0))
      (check "64x64 unvisited cells stay unknown" nil
             (cell-explored-p (game-knowledge g3) 3 5))))
  (delete-file "tests/tmp-big.map")
  (delete-file "tests/tmp-big.sav"))

;; 128x128: the spec's upper flexibility bound — parse, move, viewport.
(let* ((m (parse-map (%big-map-art 128 128) :name "big128"))
       (g (new-game m)))
  (check "128x128 parses" '(128 128)
         (list (dungeon-map-width m) (dungeon-map-height m)))
  (setf (game-x g) 100 (game-y g) 64)
  (observe g)
  (multiple-value-bind (x0 y0 w h) (map-viewport m 100 64 6 6)
    (check "128x128 viewport" '(97 61 6 6) (list x0 y0 w h)))
  (move-party g :forward)
  (check "128x128 movement" '(100 63) (list (game-x g) (game-y g))))

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
;;; Wall-piece slots and the blit list (M3)

(check "wall-piece-names covers all kinds and depths"
       (* +view-depth+ 10) (length (wall-piece-names)))

(let ((planes (view-planes 240 130)))
  ;; the fixed slots at the game's FP viewport size
  (check "front slot at depth 0" '(48 26 144 78)
         (wall-piece-rect planes '(:front 0)))
  (check "front-door shares the front slot"
         (wall-piece-rect planes '(:front 0))
         (wall-piece-rect planes '(:front-door 0)))
  (check "left side slot spans the full column" '(0 0 49 130)
         (wall-piece-rect planes '(:side 0 :l)))
  (check "left flank slot is the side band at wall height" '(0 26 49 78)
         (wall-piece-rect planes '(:flank 0 :l)))
  ;; left/right slots mirror around the viewport center
  (let ((l (wall-piece-rect planes '(:side 1 :l)))
        (r (wall-piece-rect planes '(:side 1 :r))))
    (check "side slots mirror"
           (list (first l) (third l))
           (list (- 239 (first r) (1- (third r))) (third r))))
  ;; every piece the view can ask for fits inside the viewport
  (check "all piece slots lie inside the viewport" nil
         (remove-if (lambda (piece)
                      (destructuring-bind (x y w h)
                          (wall-piece-rect planes piece)
                        (and (<= 0 x) (<= 0 y) (< 0 w) (< 0 h)
                             (<= (+ x w) 240) (<= (+ y h) 130))))
                    (wall-piece-names))))

;; The blit list mirrors the display-list wall logic: same map spots as
;; the display-list tests above.
(let* ((m (parse-map *art* :name "test"))
       (planes (view-planes 240 130)))
  (check "blit list: walled dead end"
         '((:side 0 :l) (:flank 0 :r) (:front 0))
         (mapcar #'first (view-blit-list (compute-view m 0 0 :north) planes)))
  (check "blit list: corridor with side door, far to near"
         '((:side 1 :l) (:side-door 1 :r) (:front 1)
           (:side 0 :l) (:flank 0 :r))
         (mapcar #'first (view-blit-list (compute-view m 0 0 :east) planes)))
  ;; (1,0) facing south: east wall left, the open start cell right
  ;; (open beyond -> no piece), the door dead ahead
  (check "blit list: front door blocks the view"
         '((:side 0 :l) (:front-door 0))
         (mapcar #'first (view-blit-list (compute-view m 1 0 :south) planes)))
  ;; each record carries its slot rect
  (check "blit records carry their slot rects" nil
         (remove-if (lambda (rec)
                      (equal (rest rec) (wall-piece-rect planes (first rec))))
                    (view-blit-list (compute-view m 0 0 :east) planes))))

;;; ---------------------------------------------------------------------
;;; Backdrop slots (ceiling/floor) and the tile-pack manifest

(destructuring-bind (ceiling floor) (backdrop-rects (view-planes 240 130))
  (check "ceiling backdrop slot" '(0 0 240 65) ceiling)
  (check "floor backdrop slot" '(0 65 240 65) floor))

;; the two slots tile any viewport exactly, split at the horizon
(destructuring-bind (ceiling floor) (backdrop-rects (view-planes 33 17))
  (destructuring-bind (cx cy cw ch) ceiling
    (destructuring-bind (fx fy fw fh) floor
      (check "small-viewport backdrops start at the top-left" '(0 0)
             (list cx cy))
      (check "small-viewport backdrops span the width" '(33 33)
             (list cw fw))
      (check "floor starts where the ceiling ends" (+ cy ch) fy)
      (check "backdrops tile the viewport height" 17 (+ ch fh)))))

(check "gfx-dir defaults to the demo pack" "data/gfx/" *gfx-dir*)

(let ((manifest (with-output-to-string (s) (print-tile-manifest s))))
  (check "manifest lists every pack file"
         (+ 2 (length (wall-piece-names)))
         (print-tile-manifest (make-broadcast-stream)))
  (check-true "manifest names the wall pieces"
              (search "side-door-2-l.iff" manifest))
  (check-true "manifest names the backdrops"
              (and (search "ceiling.iff" manifest)
                   (search "floor.iff" manifest)))
  (check-true "manifest states the palette contract"
              (search "pens 4-15" manifest)))

;;; ---------------------------------------------------------------------
;;; Cookie-cut mask bytes (the Amiga transparent-blit source): a 1 bit
;;; per opaque pixel, MSB first, rows padded to a 16-pixel word.

(multiple-value-bind (m bpr)
    (mask-bytes 10 2 (let ((p (make-array 20 :element-type '(unsigned-byte 8)
                                          :initial-element 0)))
                       (setf (aref p 2) 1 (aref p 3) 1 (aref p 8) 1)
                       p))
  (check "mask row is word-aligned" 2 bpr)
  (check "mask covers every row" 4 (length m))
  (check "mask row0 byte0 marks pixels 2,3" #x30 (aref m 0))
  (check "mask row0 byte1 marks pixel 8" #x80 (aref m 1))
  (check "mask clears an all-transparent row" 0 (+ (aref m 2) (aref m 3))))

;; the transparent key need not be pen 0 (pen 3 here) — and pen 0 is not
;; special: it counts as opaque when it isn't the key
(check "mask honors a non-zero transparent key" #xEF
       (aref (mask-bytes 8 1 #(0 1 2 3 4 5 6 7) 3) 0))

(check-true "image-transparent-p spots the key"
            (image-transparent-p (make-image 2 2 2)))       ; all pen 0
(check-true "image-transparent-p is nil when fully painted"
            (not (image-transparent-p
                  (let ((img (make-image 2 2 2)))
                    (dotimes (y 2 img) (dotimes (x 2)
                                         (setf (pixel-ref img x y) 1)))))))

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
;;; Dice

(multiple-value-bind (c s b) (parse-dice "2d6+1")
  (check "parse-dice 2d6+1" '(2 6 1) (list c s b)))
(multiple-value-bind (c s b) (parse-dice "1d8")
  (check "parse-dice 1d8" '(1 8 0) (list c s b)))
(multiple-value-bind (c s b) (parse-dice "3d4-1")
  (check "parse-dice 3d4-1" '(3 4 -1) (list c s b)))
(multiple-value-bind (c s b) (parse-dice 5)
  (check "parse-dice integer constant" '(0 0 5) (list c s b)))
(check-error "parse-dice rejects garbage" (parse-dice "banana"))
(check-error "parse-dice rejects zero dice" (parse-dice "0d6"))
(check-error "parse-dice rejects zero sides" (parse-dice "1d0"))

(with-rng (3 4)
  (check "roll-dice 2d6+1 scripted" 10 (roll-dice "2d6+1")))
(with-rng ()
  (check "roll-dice exhausted script rolls ones" 1 (roll-dice "1d8")))
(check "roll-dice integer is constant" 7 (roll-dice 7))
(with-rng (13)
  (check "scripted roll wraps mod n" 3 (roll 10)))

;;; ---------------------------------------------------------------------
;;; Events and story flags

(let* ((m (parse-map *art* :name "test"))
       (g (new-game m))
       (order '()))
  (on-event g :ping (lambda (game x) (declare (ignore game))
                      (push (list :a x) order)))
  (on-event g :ping (lambda (game x) (declare (ignore game))
                      (push (list :b x) order)))
  (emit g :ping 7)
  (check "handlers run in subscription order" '((:a 7) (:b 7))
         (nreverse order))
  (check "emit without subscribers is quiet" nil
         (handler-case (progn (emit g :nobody-listens) nil)
           (error () :boom))))

(let* ((m (parse-map *art* :name "test"))
       (g (new-game m))
       (msgs (watch-messages g)))
  (say g "hello ~D" 5)
  (check "say formats into a :message event" '("hello 5") (funcall msgs)))

(let* ((m (parse-map *art* :name "test"))
       (g (new-game m)))
  (check "unset flag is nil" nil (flag g :quest))
  (set-flag g :quest)
  (check "set-flag defaults to t" t (flag g :quest))
  (set-flag g :quest 42)
  (check "set-flag with value" 42 (flag g :quest))
  (set-flag g '(:door "cellar" 1) :open)
  (check "flags use equal keys" :open (flag g '(:door "cellar" 1)))
  (clear-flag g :quest)
  (check "clear-flag" nil (flag g :quest)))

;; The message log: the Bard's Tale text column's backing store.
(let* ((m (parse-map *art* :name "test"))
       (g (new-game m))
       (log (attach-message-log g :limit 3)))
  (check "fresh log is empty" '() (log-recent log 10))
  (say g "one")
  (say g "two")
  (check "log-recent returns oldest first" '("one" "two")
         (log-recent log 10))
  (check "log-recent trailing lines only" '("two") (log-recent log 1))
  (say g "three")
  (say g "four")
  (check "log ring drops the oldest beyond the limit"
         '("two" "three" "four") (log-recent log 10))
  (log-message log "five")
  (check "log-message appends directly" '("three" "four" "five")
         (log-recent log 10)))

;; Word wrap for the text column (the Amiga log wraps long messages).
(check "wrap: short text passes through" '("short") (wrap-text "short" 10))
(check "wrap: exact width does not wrap" '("12345") (wrap-text "12345" 5))
(check "wrap: empty string yields one empty line" '("") (wrap-text "" 10))
(check "wrap: breaks at word boundaries"
       '("El Cid hits the" "giant rat for 2" "damage.")
       (wrap-text "El Cid hits the giant rat for 2 damage." 16))
(check "wrap: space at the boundary"
       '("one two" "three") (wrap-text "one two three" 7))
(check "wrap: long word breaks hard"
       '("aaaa" "aaaa" "aa") (wrap-text "aaaaaaaaaa" 4))
(check "wrap: width floor of 1" '("a" "b") (wrap-text "ab" 0))
(check-true "wrap: every line fits the width"
            (every (lambda (line) (<= (length line) 12))
                   (wrap-text "the quick brown fox jumps over the lazy dog" 12)))

;; wrap-message: "> " marks where a message starts, continuations indent.
(check "wrap-message: single line gets the marker"
       '("> short") (wrap-message "short" 10))
(check "wrap-message: continuation lines indent"
       '("> one two" "  three") (wrap-message "one two three" 9))
(check-true "wrap-message: every line fits the width"
            (every (lambda (line) (<= (length line) 16))
                   (wrap-message "El Cid hits the giant rat for 2 damage." 16)))
(check "wrap-message: narrow width floor"
       '("> a" "  b") (wrap-message "ab" 0))

;; Compass-rose geometry (the UI's facing indicator).
(destructuring-bind (needle letters) (compass-points +north+ 100 50 20)
  (check "compass needle points north" '(100 50 100 38) needle)
  (check "compass north letter" '(#\N 100 30 t) (first letters))
  (check "compass east letter" '(#\E 120 50 nil) (second letters))
  (check "compass south letter" '(#\S 100 70 nil) (third letters))
  (check "compass west letter" '(#\W 80 50 nil) (fourth letters)))
(destructuring-bind (needle letters) (compass-points +west+ 0 0 10)
  (check "compass needle points west" '(0 0 -2 0) needle)
  (check "only the facing letter is highlighted"
         '(nil nil nil t) (mapcar #'fourth letters)))
(destructuring-bind (needle letters) (compass-points +east+ 10 10 4)
  (declare (ignore letters))
  (check "compass needle keeps a minimum length" '(10 10 12 10) needle))

;; Active spell effects (the UI's spell strip).
(let* ((m (parse-map *art* :name "test"))
       (g (new-game m)))
  (check "fresh game has no active effects" '() (game-effects g))
  (add-effect g "shield")
  (add-effect g "lamp")
  (check "effects accumulate in order" '("shield" "lamp")
         (game-effects g))
  (add-effect g "shield")
  (check "add-effect ignores duplicates" '("shield" "lamp")
         (game-effects g))
  (remove-effect g "shield")
  (check "remove-effect" '("lamp") (game-effects g))
  (remove-effect g "not-there")
  (check "removing an absent effect is quiet" '("lamp")
         (game-effects g)))

;; Movement emits :enter-cell and :blocked.
(let* ((m (parse-map *art* :name "test"))
       (g (new-game m))
       (entered '())
       (blocked '()))
  (on-event g :enter-cell (lambda (game x y) (declare (ignore game))
                            (push (list x y) entered)))
  (on-event g :blocked (lambda (game dir) (declare (ignore game))
                         (push dir blocked)))
  (move-party g :forward)               ; north wall
  (turn-right g)
  (move-party g :forward)               ; east to (1,0)
  (check "blocked event carries direction" '(:north) blocked)
  (check "enter-cell event carries coordinates" '((1 0)) (nreverse entered)))

;;; ---------------------------------------------------------------------
;;; Cell specials

;; message + set-flag on entry.
(let* ((m (parse-map *art* :name "test"))
       (g (new-game m))
       (msgs (watch-messages g)))
  (setf (cell-special m 1 0) '((message "It is dark here.")
                               (set-flag :dark)))
  (turn-right g)
  (move-party g :forward)
  (check "special message on entry" '("It is dark here.") (funcall msgs))
  (check "special set a flag" t (flag g :dark)))

;; trigger-special fires the start cell by hand (after wiring handlers).
(let* ((m (parse-map *art* :name "test"))
       (g (new-game m))
       (msgs (watch-messages g)))
  (setf (cell-special m 0 0) '((message "You are at the start.")))
  (trigger-special g)
  (check "trigger-special runs the standing cell"
         '("You are at the start.") (funcall msgs)))

;; once runs only the first time, ever.
(let* ((m (parse-map *art* :name "test"))
       (g (new-game m))
       (msgs (watch-messages g)))
  (setf (cell-special m 1 0) '((once (message "first time"))))
  (turn-right g)
  (move-party g :forward)
  (move-party g :back)
  (move-party g :forward)
  (check "once fires a single time" '("first time") (funcall msgs)))

;; when-flag / unless-flag branch on story flags.
(let* ((m (parse-map *art* :name "test"))
       (g (new-game m))
       (msgs (watch-messages g)))
  (setf (cell-special m 1 0) '((when-flag :key (message "yes"))
                               (unless-flag :key (message "no"))))
  (turn-right g)
  (move-party g :forward)
  (check "unless-flag branch without flag" '("no") (funcall msgs))
  (set-flag g :key)
  (move-party g :back)
  (move-party g :forward)
  (check "when-flag branch with flag" '("no" "yes") (funcall msgs)))

;; teleport relocates, faces, records knowledge and chains the target's
;; special.
(let* ((m (parse-map *art* :name "test"))
       (g (new-game m))
       (msgs (watch-messages g)))
  (setf (cell-special m 1 0) '((teleport 2 1 :south)))
  (setf (cell-special m 2 1) '((message "arrived")))
  (turn-right g)
  (move-party g :forward)
  (check "teleport position" '(2 1) (list (game-x g) (game-y g)))
  (check "teleport facing" +south+ (game-facing g))
  (check "teleport chains target special" '("arrived") (funcall msgs))
  (check-true "teleport target explored"
              (cell-explored-p (game-knowledge g) 2 1)))

;; a teleport loop trips the recursion guard.
(let* ((m (parse-map *art* :name "test"))
       (g (new-game m)))
  (setf (cell-special m 0 0) '((teleport 1 0)))
  (setf (cell-special m 1 0) '((teleport 0 0)))
  (check-error "teleport loop is caught" (trigger-special g)))

;; teleport off the map is rejected.
(let* ((m (parse-map *art* :name "test"))
       (g (new-game m)))
  (setf (cell-special m 0 0) '((teleport 9 9)))
  (check-error "teleport off-map is rejected" (trigger-special g)))

;; spin turns the party to a random facing, silently.
(let* ((m (parse-map *art* :name "test"))
       (g (new-game m))
       (msgs (watch-messages g)))
  (setf (cell-special m 1 0) '((spin)))
  (turn-right g)
  (with-rng (2)
    (move-party g :forward))
  (check "spinner facing" +south+ (game-facing g))
  (check "spinner is silent" '() (funcall msgs)))

;; unknown ops and malformed ops are loud.
(let* ((m (parse-map *art* :name "test"))
       (g (new-game m)))
  (setf (cell-special m 0 0) '((frobnicate 1 2)))
  (check-error "unknown special op is an error" (trigger-special g))
  (setf (cell-special m 0 0) '(42))
  (check-error "non-list special op is an error" (trigger-special g)))

;;; ---------------------------------------------------------------------
;;; Heroes and the party

(define-hero-class :tester :hp-dice "1d8+2" :damage "1d6" :ac 8)

(let ((h (with-rng (5) (make-hero "Bob" :tester))))
  (check "hero name" "Bob" (hero-name h))
  (check "hero class" :tester (hero-class h))
  (check "hero hp from class hit dice" 8 (hero-max-hp h))
  (check "hero starts at full hp" 8 (hero-hp h))
  (check "hero str rolled 3d6" 3 (hero-str h))
  (check "hero ac from class" 8 (hero-ac h))
  (check "hero damage from class" "1d6" (hero-damage h))
  (check "hero level 1" 1 (hero-level h))
  (check-true "fresh hero is alive" (hero-alive-p h)))

(check-error "make-hero rejects unknown class" (make-hero "X" :nonesuch))

;; Character sheet (the party-UI stat block): pure text, rendered by
;; the Amiga :sheet page and tested here from the same source.
(define-hero-class :war-mage :hp-dice "1d6" :damage "1d4" :ac 8)
(check "hero-class-title spaces and capitalizes" "War Mage"
       (hero-class-title (%make-hero :name "z" :class :war-mage)))
(let* ((h (%make-hero :name "El Cid" :class :war-mage :level 3 :xp 1200
                      :max-hp 11 :hp 9 :str 15 :dex 12 :iq 9 :con 14
                      :lck 10 :ac 8 :gold 250))
       (lines (hero-summary-lines h)))
  (check "sheet has six lines" 6 (length lines))
  (check "sheet name/class line" "El Cid the War Mage" (first lines))
  (check "sheet level/xp line" "Level 3    XP 1200" (second lines))
  (check "sheet hp/ac line" "HP 9/11    AC 8" (third lines))
  (check "sheet primary stats line" "STR 15  DEX 12  IQ 9" (fourth lines))
  (check "sheet secondary stats line" "CON 14  LCK 10" (fifth lines))
  (check "sheet gold line, standing" "Gold 250 gp" (sixth lines)))
;; a downed hero is flagged on the gold line
(check "sheet marks a downed hero" "Gold 0 gp   (down)"
       (sixth (hero-summary-lines
               (%make-hero :name "x" :class :war-mage :hp 0))))

(check "stat-bonus 10" 0 (stat-bonus 10))
(check "stat-bonus 12" 1 (stat-bonus 12))
(check "stat-bonus 15" 2 (stat-bonus 15))
(check "stat-bonus 9" -1 (stat-bonus 9))
(check "stat-bonus 3" -4 (stat-bonus 3))

(check "xp-for-level 2" 100 (xp-for-level 2))
(check "xp-for-level 3" 300 (xp-for-level 3))

;; Party queries, damage and healing.
(let* ((m (parse-map *art* :name "test"))
       (heroes (with-rng () (list (make-hero "A" :tester)
                                  (make-hero "B" :tester)
                                  (make-hero "C" :tester)
                                  (make-hero "D" :tester))))
       (g (new-game m :party heroes))
       (msgs (watch-messages g))
       (died '())
       (wiped nil))
  (on-event g :hero-died (lambda (game h) (declare (ignore game))
                           (push (hero-name h) died)))
  (on-event g :party-defeated (lambda (game) (declare (ignore game))
                                (setf wiped t)))
  (check "party of four alive" 4 (length (alive-heroes g)))
  (check "front ranks are the first three" '("A" "B" "C")
         (mapcar #'hero-name (front-ranks g)))
  (damage-hero g (first heroes) 999)
  (check "damage clamps hp at zero" 0 (hero-hp (first heroes)))
  (check "hero-died event" '("A") died)
  (check "front ranks skip the fallen" '("B" "C" "D")
         (mapcar #'hero-name (front-ranks g)))
  (check "three heroes standing" 3 (length (alive-heroes g)))
  (check "party not wiped yet" nil wiped)
  (dolist (h (rest heroes))
    (damage-hero g h 999))
  (check-true "party-defeated event after the last hero" wiped)
  (check "party-alive-p when wiped" nil (party-alive-p g))
  (check-true "fall message emitted" (search "falls" (first (funcall msgs)))))

;; The roster holds up to +party-limit+ (7) members: 6 heroes + 1 guest.
(check "party limit is seven" 7 +party-limit+)
(let* ((m (parse-map *art* :name "test"))
       (g (new-game m :party (with-rng () (list (make-hero "A" :tester)))))
       (msgs (watch-messages g))
       (joined '()))
  (on-event g :party-joined (lambda (game h) (declare (ignore game))
                              (push (hero-name h) joined)))
  (check "party starts below the limit" nil (party-full-p g))
  (dotimes (i 6)
    (check-true (format nil "join-party accepts member ~D" (+ 2 i))
                (with-rng () (join-party g (make-hero (format nil "H~D" i)
                                                      :tester)))))
  (check "party at the limit" 7 (length (game-party g)))
  (check-true "party-full-p at the limit" (party-full-p g))
  (check ":party-joined emitted per join" 6 (length joined))
  (check "join-party refuses the 8th" nil
         (with-rng () (join-party g (make-hero "Late" :tester))))
  (check "refused hero not added" 7 (length (game-party g)))
  (check-true "join message emitted"
              (find-if (lambda (s) (search "joins the party" s))
                       (funcall msgs)))
  (check-true "full message emitted"
              (find-if (lambda (s) (search "party is full" s))
                       (funcall msgs)))
  ;; combat still works with a full roster: front ranks stay three
  (check "front ranks with a full party" 3 (length (front-ranks g))))

(let* ((m (parse-map *art* :name "test"))
       (h (with-rng () (make-hero "A" :tester)))  ; 3 max hp
       (g (new-game m :party (list h))))
  (damage-hero g h 2)
  (check "heal-hero returns hp gained" 1 (with-rng (0) (heal-hero g h 1)))
  (check "heal-hero caps at max" 3 (progn (heal-hero g h 99) (hero-hp h))))

;; Leveling: crossing a threshold rolls the class hit dice again.
(let* ((m (parse-map *art* :name "test"))
       (h (with-rng (5) (make-hero "A" :tester)))  ; 8 max hp
       (g (new-game m :party (list h)))
       (msgs (watch-messages g)))
  (with-rng (4) (award-xp g h 100))
  (check "level after 100 xp" 2 (hero-level h))
  (check "level-up adds rolled hp" 15 (hero-max-hp h))
  (check-true "level-up message" (search "level 2" (first (funcall msgs))))
  (with-rng (4 4) (award-xp g h 500))   ; 600 xp: levels 3 and 4
  (check "multiple level-ups in one award" 4 (hero-level h)))

;;; ---------------------------------------------------------------------
;;; Combat

(define-monster "test rat"
  :level 1 :hp-dice 3 :ac 10 :damage "1d2" :xp 10 :gold 6)

(check-error "unknown monster type" (find-monster-type "grue"))

(defun %combat-hero ()
  "A deterministic level-1 :tester hero: 8 hp, str 10 (no bonus)."
  (let ((h (with-rng (5) (make-hero "Alva" :tester))))
    (setf (hero-str h) 10)
    h))

;; start-combat: dice group counts, banner, event, exclusivity.
(let* ((m (parse-map *art* :name "test"))
       (g (new-game m :party (list (%combat-hero))))
       (msgs (watch-messages g))
       (started '()))
  (on-event g :combat-start (lambda (game monsters) (declare (ignore game))
                              (setf started (length monsters))))
  (with-rng (2)
    (start-combat g '(("test rat" "1d3+1"))))
  (check "dice group count spawns monsters" 4 started)
  (check "combat groups count the living" '(("test rat" . 4))
         (mapcar (lambda (grp) (cons (monster-type-name (car grp)) (cdr grp)))
                 (combat-groups (game-combat g))))
  (check "combat banner" '("You face 4 test rats!") (funcall msgs))
  (check-error "no moving during combat" (move-party g :forward))
  (check-error "no nested combat" (start-combat g '(("test rat" 1)))))

;; A clean kill: hero hits, monster dies, rewards are handed out.
(let* ((m (parse-map *art* :name "test"))
       (h (%combat-hero))
       (g (new-game m :party (list h)))
       (msgs (watch-messages g))
       (ended '()))
  (on-event g :combat-end (lambda (game result) (declare (ignore game))
                            (push result ended)))
  (start-combat g '(("test rat" 1)))
  (check "single-monster banner" '("You face 1 test rat!") (funcall msgs))
  (check "victory round" :victory (with-rng (10 2) (combat-round g)))
  (check "combat cleared after victory" nil (game-combat g))
  (check "combat-end event" '(:victory) ended)
  (check "xp awarded" 10 (hero-xp h))
  (check "gold awarded" 6 (hero-gold h))
  (check-true "slain message"
              (find-if (lambda (s) (search "slays" s)) (funcall msgs)))
  (check-true "victory message"
              (find-if (lambda (s) (search "Victory" s)) (funcall msgs))))

;; Miss, get hit back; defending raises AC for the round.
(let* ((m (parse-map *art* :name "test"))
       (h (%combat-hero))
       (g (new-game m :party (list h)))
       (msgs (watch-messages g)))
  (start-combat g '(("test rat" 1)))
  ;; hero d20=1 misses; rat targets hero, d20=12 hits ac 8, 1d2 -> 2.
  (check "ongoing round" :ongoing (with-rng (0 0 11 1) (combat-round g)))
  (check "monster damage landed" 6 (hero-hp h))
  ;; defending: ac 8-4=4 needs 16+; rat d20=14 now misses.
  (check "defend round ongoing" :ongoing
         (with-rng (0 13) (combat-round g '(:defend))))
  (check "defender untouched" 6 (hero-hp h))
  (check-true "monster miss message"
              (find-if (lambda (s) (search "misses Alva" s)) (funcall msgs)))
  ;; without defending the same monster roll (d20=14 vs ac 8) hits.
  (check "same roll hits when not defending" :ongoing
         (with-rng (0 0 13 0) (combat-round g)))
  (check "hit for 1d2 minimum" 5 (hero-hp h)))

;; Defeat: the last hero falls to a monster.
(let* ((m (parse-map *art* :name "test"))
       (h (%combat-hero))
       (g (new-game m :party (list h)))
       (wiped nil))
  (on-event g :party-defeated (lambda (game) (declare (ignore game))
                                (setf wiped t)))
  (start-combat g '(("test rat" 1)))
  (setf (hero-hp h) 1)
  (check "defeat round" :defeat (with-rng (0 0 11 1) (combat-round g)))
  (check "combat cleared after defeat" nil (game-combat g))
  (check-true "party-defeated emitted in combat" wiped))

;; Fleeing: even odds; failure hands the monsters a free round.
(let* ((m (parse-map *art* :name "test"))
       (g (new-game m :party (list (%combat-hero)))))
  (start-combat g '(("test rat" 1)))
  (check "flee success" :fled (with-rng (10) (attempt-flee g)))
  (check "combat cleared after fleeing" nil (game-combat g)))
(let* ((m (parse-map *art* :name "test"))
       (h (%combat-hero))
       (g (new-game m :party (list h)))
       (msgs (watch-messages g)))
  (start-combat g '(("test rat" 1)))
  (check "flee failure is ongoing" :ongoing
         (with-rng (60 0 11 1) (attempt-flee g)))
  (check "free round hurt the party" 6 (hero-hp h))
  (check-true "no escape message"
              (find-if (lambda (s) (search "No escape" s)) (funcall msgs))))

(check-error "combat-round without combat"
  (combat-round (new-game (parse-map *art*))))
(check-error "attempt-flee without combat"
  (attempt-flee (new-game (parse-map *art*))))

;; The encounter special starts combat and skips the remaining ops.
(let* ((m (parse-map *art* :name "test"))
       (g (new-game m :party (list (%combat-hero))))
       (msgs (watch-messages g)))
  (setf (cell-special m 1 0) '((encounter ("test rat" 1))
                               (message "never shown")))
  (turn-right g)
  (check "moving onto an encounter still reports the step" :moved
         (move-party g :forward))
  (check-true "encounter started combat" (game-combat g))
  (check "ops after encounter are skipped" '("You face 1 test rat!")
         (funcall msgs)))

;;; ---------------------------------------------------------------------
;;; Map files with a story layer

(with-open-file (s "tests/tmp.map" :direction :output :if-exists :supersede)
  (write-string "+-+-+
|@  |
+-+-+

;; the story layer
(special (1 0) (message \"hello\") (set-flag :was-here))
" s))
(let ((m (load-map-file "tests/tmp.map")))
  (check "map file art still parses" 2 (dungeon-map-width m))
  (check "special read from map file"
         '((message "hello") (set-flag :was-here))
         (cell-special m 1 0))
  (check "cells without specials" nil (cell-special m 0 0)))

(with-open-file (s "tests/tmp.map" :direction :output :if-exists :supersede)
  (write-string "+-+
|@|
+-+
(special (5 5) (message \"nope\"))
" s))
(check-error "out-of-range special coordinates rejected"
  (load-map-file "tests/tmp.map"))

(with-open-file (s "tests/tmp.map" :direction :output :if-exists :supersede)
  (write-string "+-+
|@|
+-+
(frobnicate 1)
" s))
(check-error "unknown map form rejected" (load-map-file "tests/tmp.map"))
(delete-file "tests/tmp.map")

;; The shipped cellar map carries its story layer.
(let ((m (load-map-file "data/cellar.map")))
  (check-true "cellar start special" (cell-special m 0 0))
  (check-true "cellar stairs-down special" (cell-special m 3 2)))

;;; ---------------------------------------------------------------------
;;; Save games

(with-open-file (s "tests/tmp.map" :direction :output :if-exists :supersede)
  (write-string "+-+-+-+
|@  | |
+ +D+ +
| |  <|
+-+-+-+

(special (1 0) (message \"dusty\"))
" s))

(let* ((m (load-map-file "tests/tmp.map"))
       (a (with-rng (5) (make-hero "Alva" :tester)))
       (b (with-rng (5) (make-hero "Berk" :tester)))
       (g (new-game m :party (list a b))))
  (turn-right g)
  (move-party g :forward)               ; to (1,0)
  (set-flag g :quest 42)
  (set-flag g '(:seen "door") t)
  (damage-hero g a 3)
  (setf (hero-xp b) 60)
  (incf (hero-gold b) 17)
  (save-game g "tests/tmp-save.lisp")
  (let ((g2 (load-game "tests/tmp-save.lisp")))
    (check "loaded position" '(1 0) (list (game-x g2) (game-y g2)))
    (check "loaded facing" +east+ (game-facing g2))
    (check "loaded flag value" 42 (flag g2 :quest))
    (check "loaded equal-key flag" t (flag g2 '(:seen "door")))
    (check "loaded party size" 2 (length (game-party g2)))
    (let ((a2 (first (game-party g2)))
          (b2 (second (game-party g2))))
      (check "loaded hero name" "Alva" (hero-name a2))
      (check "loaded hero class" :tester (hero-class a2))
      (check "loaded hero damage taken" 5 (hero-hp a2))
      (check "loaded hero max hp" 8 (hero-max-hp a2))
      (check "loaded hero xp" 60 (hero-xp b2))
      (check "loaded hero gold" 17 (hero-gold b2)))
    (check-true "loaded knowledge: visited cell explored"
                (cell-explored-p (game-knowledge g2) 1 0))
    (check "loaded knowledge: unseen cell unexplored" nil
           (cell-explored-p (game-knowledge g2) 1 1))
    (check-true "loaded knowledge: seen wall known"
                (wall-known-p (game-knowledge g2) 1 0 :north))
    (check "loaded map keeps its story layer"
           '((message "dusty")) (cell-special (game-map g2) 1 0))))

;; Saving mid-combat is refused; junk files are rejected on load.
(let* ((m (parse-map *art* :name "test"))
       (g (new-game m :party (list (%combat-hero)))))
  (start-combat g '(("test rat" 1)))
  (check-error "no saving during combat" (save-game g "tests/tmp-save.lisp")))
(check-error "load-game rejects non-save files"
  (load-game "tests/tmp.map"))
(delete-file "tests/tmp.map")
(delete-file "tests/tmp-save.lisp")

;;; ---------------------------------------------------------------------
;;; ILBM images (M3): the image model, ByteRun1, reader/writer round trips.

(check-error "make-image rejects zero width" (make-image 0 5 2))
(check-error "make-image rejects depth 9" (make-image 4 4 9))

(let ((img (make-image 7 5 3)))
  (check "fresh image is pen 0" 0 (pixel-ref img 6 4))
  (setf (pixel-ref img 6 4) 5)
  (check "pixel-ref reads back" 5 (pixel-ref img 6 4))
  (check "row-major neighbors untouched" 0 (pixel-ref img 5 4)))

;; ByteRun1 pack/unpack, straight on byte rows: repeats, literals,
;; run-length caps at 128, runs of exactly 2 (stay literal) and 3.
(labels ((rt (bytes label)
           (let* ((row (coerce bytes '(vector (unsigned-byte 8))))
                  (packed (coerce (%pack-byte-run1 row)
                                  '(vector (unsigned-byte 8))))
                  (out (make-array (length row)
                                   :element-type '(unsigned-byte 8))))
             (%unpack-byte-run1 packed 0 (length packed) out (length row)
                                "test")
             (check label (coerce row 'list) (coerce out 'list))
             packed)))
  (let ((packed (rt (make-list 300 :initial-element 7)
                    "ByteRun1 round-trips a 300-byte repeat")))
    (check-true "long repeat splits into 128-byte runs" (<= (length packed) 6)))
  (rt (loop for i below 200 collect (mod i 251))
      "ByteRun1 round-trips a 200-byte literal row")
  (rt (loop for i below 40 collect (if (evenp i) 1 2))
      "ByteRun1 round-trips alternating bytes")
  (rt '(9 9 3 3 3 4 4 5 5 5 5) "runs of 2 stay literal, 3+ compress")
  (rt '(1) "single-byte row")
  (check-true "truncated ByteRun1 input signals"
              (handler-case
                  (let ((out (make-array 8 :element-type '(unsigned-byte 8))))
                    (%unpack-byte-run1
                     (coerce '(200) '(vector (unsigned-byte 8)))
                     0 1 out 8 "test")
                    nil)
                (error () t))))

;; Reader/writer round trips: both compressions, pad-boundary widths,
;; depth 1 and depth 8, palette preserved.
(labels ((checker (w h depth)
           (let ((img (make-image w h depth)))
             (dotimes (y h img)
               (dotimes (x w)
                 (setf (pixel-ref img x y)
                       (mod (+ x (* 3 y)) (ash 1 depth)))))))
         (same-image (label a b)
           (check (format nil "~A: dimensions" label)
                  (list (image-width a) (image-height a) (image-depth a))
                  (list (image-width b) (image-height b) (image-depth b)))
           (check-true (format nil "~A: pixels" label)
                       (equalp (image-pixels a) (image-pixels b)))))
  (dolist (compression '(1 0))
    (dolist (dims '((7 5 2) (16 4 3) (17 3 4) (24 2 1) (33 2 8)))
      (destructuring-bind (w h depth) dims
        (let ((img (checker w h depth))
              (path "tests/tmp-img.iff"))
          (write-ilbm img path :compression compression)
          (same-image (format nil "round trip ~Dx~Dx~D cmp ~D"
                              w h depth compression)
                      img (read-ilbm path))))))
  ;; palette round trip (partial CMAP: only set entries are written)
  (let ((img (checker 8 4 2)))
    (setf (aref (image-palette img) 0) '(0 0 0)
          (aref (image-palette img) 1) '(255 255 255)
          (aref (image-palette img) 2) '(136 136 136)
          (aref (image-palette img) 3) '(255 170 51))
    (write-ilbm img "tests/tmp-img.iff")
    (check "palette survives the round trip"
           '(255 170 51)
           (aref (image-palette (read-ilbm "tests/tmp-img.iff")) 3))))

;; Unknown chunks are skipped (with odd-length padding): splice an ANNO
;; chunk between BMHD and BODY by byte surgery and re-read.
(let ((img (make-image 8 3 2)))
  (setf (pixel-ref img 2 1) 3)
  (write-ilbm img "tests/tmp-img.iff")
  (let* ((bytes (with-open-file (s "tests/tmp-img.iff"
                                   :element-type '(unsigned-byte 8))
                  (let ((v (make-array (file-length s)
                                       :element-type '(unsigned-byte 8))))
                    (read-sequence v s)
                    v)))
         ;; ANNO chunk, 5 data bytes -> padded to 6 on disk
         (anno (coerce (append (map 'list #'char-code "ANNO")
                               '(0 0 0 5)
                               (map 'list #'char-code "prop!")
                               '(0))
                       '(vector (unsigned-byte 8))))
         (cut (+ 12 8 20))                 ; after FORM hdr + BMHD chunk
         (spliced (concatenate '(vector (unsigned-byte 8))
                               (subseq bytes 0 cut) anno
                               (subseq bytes cut))))
    ;; fix the FORM length
    (let ((len (- (length spliced) 8)))
      (setf (aref spliced 4) (ldb (byte 8 24) len)
            (aref spliced 5) (ldb (byte 8 16) len)
            (aref spliced 6) (ldb (byte 8 8) len)
            (aref spliced 7) (ldb (byte 8 0) len)))
    (with-open-file (s "tests/tmp-img.iff" :direction :output
                       :element-type '(unsigned-byte 8)
                       :if-exists :supersede)
      (write-sequence spliced s))
    (let ((back (read-ilbm "tests/tmp-img.iff")))
      (check "reader skips unknown ANNO chunk" 3 (pixel-ref back 2 1)))))

;; Not-an-ILBM and truncated BODY both signal clear errors.
(with-open-file (s "tests/tmp-img.iff" :direction :output
                   :element-type '(unsigned-byte 8)
                   :if-exists :supersede)
  (map nil (lambda (c) (write-byte (char-code c) s)) "just some text"))
(check-error "read-ilbm rejects a non-IFF file"
  (read-ilbm "tests/tmp-img.iff"))
(let ((img (make-image 32 8 4)))
  (write-ilbm img "tests/tmp-img.iff" :compression 0)
  (let* ((bytes (with-open-file (s "tests/tmp-img.iff"
                                   :element-type '(unsigned-byte 8))
                  (let ((v (make-array (file-length s)
                                       :element-type '(unsigned-byte 8))))
                    (read-sequence v s)
                    v))))
    (with-open-file (s "tests/tmp-img.iff" :direction :output
                       :element-type '(unsigned-byte 8)
                       :if-exists :supersede)
      (write-sequence (subseq bytes 0 (- (length bytes) 10)) s))
    (check-error "read-ilbm rejects a truncated BODY"
      (read-ilbm "tests/tmp-img.iff"))))
(delete-file "tests/tmp-img.iff")

;;; ---------------------------------------------------------------------
;;; Wall-art assets (M3): the checked-in data/gfx ILBMs must match what
;;; the generator draws today — pixel for pixel — so art and code can
;;; never drift apart.  (Regenerate with `make assets` after changing
;;; tools/gen-walls.lisp.)

(load "tools/gen-walls.lisp")

(check "wall-piece-file name" "side-door-2-l.iff"
       (wall-piece-file '(:side-door 2 :l)))

(let ((planes (view-planes *fp-view-width* *fp-view-height*))
      (stale '()))
  (dolist (piece (wall-piece-names))
    (let ((file (concatenate 'string "data/gfx/" (wall-piece-file piece))))
      (if (not (probe-file file))
          (push (list piece :missing) stale)
          (let ((disk (read-ilbm file))
                (drawn (draw-wall-piece piece planes)))
            (destructuring-bind (x y w h) (wall-piece-rect planes piece)
              (declare (ignore x y))
              (unless (and (= (image-width disk) w)
                           (= (image-height disk) h))
                (push (list piece :wrong-size) stale)))
            (unless (equalp (image-pixels disk) (image-pixels drawn))
              (push (list piece :differs) stale))))))
  (check "all 40 data/gfx assets exist and match the generator" nil stale)
  ;; the demo ceiling/floor backdrops are pinned the same way
  (let ((stale '()))
    (loop for key in '(:ceiling :floor)
          for name in '("ceiling.iff" "floor.iff")
          for rect in (backdrop-rects planes)
          do (let ((file (concatenate 'string "data/gfx/" name)))
               (if (not (probe-file file))
                   (push (list key :missing) stale)
                   (let ((disk (read-ilbm file))
                         (drawn (draw-backdrop-piece key planes)))
                     (unless (and (= (image-width disk) (third rect))
                                  (= (image-height disk) (fourth rect)))
                       (push (list key :wrong-size) stale))
                     (unless (equalp (image-pixels disk)
                                     (image-pixels drawn))
                       (push (list key :differs) stale))))))
    (check "the backdrop assets exist and match the generator" nil stale))
  ;; the reader restores the dungeon palette from the CMAP
  (check "asset palette carries the dungeon colors"
         (coerce *wall-palette* 'list)
         (coerce (image-palette
                  (read-ilbm (concatenate 'string "data/gfx/"
                                          (wall-piece-file '(:front 0)))))
                 'list)))

;; Transparency contract: receding side pieces keep pen-0 corners so the
;; backdrop shows through the cookie-cut blit; front/flank pieces fill
;; their whole rect (opaque), drawing black as the mortar pen, not pen 0.
(let ((planes (view-planes *fp-view-width* *fp-view-height*)))
  (check-true "wall pieces are depth 3"
              (= 3 (image-depth (draw-wall-piece '(:front 0) planes))))
  (check-true "a side piece leaves transparent corners"
              (image-transparent-p (draw-wall-piece '(:side 0 :l) planes)))
  (check "a side piece's far top corner is transparent" +pen-bg+
         (let ((img (draw-wall-piece '(:side 0 :l) planes)))
           (pixel-ref img (1- (image-width img)) 0)))
  (check-true "a front piece is fully opaque"
              (not (image-transparent-p (draw-wall-piece '(:front 0) planes))))
  (check-true "a flank piece is fully opaque"
              (not (image-transparent-p
                    (draw-wall-piece '(:flank 0 :l) planes))))
  (check-true "opaque black is the mortar pen (4), never pen 0"
              (and (find +pen-mortar+
                         (image-pixels (draw-wall-piece '(:front 0) planes)))
                   (not (image-transparent-p
                         (draw-wall-piece '(:front 0) planes))))))

;;; ---------------------------------------------------------------------
;;; Amiga front-end smoke test (real Intuition window + graphics.library
;;; calls; only runs when this suite is loaded under AmigaOS clamiga).

#+amigaos
(let* ((m (parse-map *art* :name "test"))
       (g (new-game m))
       (log (attach-message-log g)))
  (say g "Smoke test line one.")
  (say g "Smoke test line two.")
  (add-effect g "shield")
  (add-effect g "lamp")
  (check "amiga-ui draws the play layout into a real window" t
         (amiga.intuition:with-window
             (win :title "Lambda's Tale Test"
                  :left 0 :top 0
                  :width *amiga-win-width* :height *amiga-win-height*
                  :idcmp amiga.intuition:+idcmp-closewindow+)
           (let* ((rp (amiga.intuition:window-rastport win))
                  (l (%amiga-layout win rp)))
             (%amiga-draw-fp rp g (ui-layout-bx l) (ui-layout-by l)
                             (ui-layout-fp-w l) (ui-layout-fp-h l))
             (%amiga-draw-effects rp g l)
             (%amiga-draw-log rp log l)
             (%amiga-status rp g l "Smoke test")
             ;; the full map mode over the same window
             (%amiga-draw-map-page rp g l nil)
             (%amiga-draw-map-page rp g l t)
             t))))

;; The full map view must cope with a map bigger than the window —
;; the layout the spec is actually about.
#+amigaos
(let* ((m (parse-map (%big-map-art 30 30) :name "big30"))
       (g (new-game m))
       (log (attach-message-log g)))
  (setf (game-x g) 15 (game-y g) 15)
  (observe g)
  (check "amiga-ui map page on a 30x30 map" t
         (amiga.intuition:with-window
             (win :title "Lambda's Tale Test"
                  :left 0 :top 0
                  :width *amiga-win-width* :height *amiga-win-height*
                  :idcmp amiga.intuition:+idcmp-closewindow+)
           (let* ((rp (amiga.intuition:window-rastport win))
                  (l (%amiga-layout win rp)))
             (%amiga-draw-effects rp g l)
             (%amiga-draw-log rp log l)
             (%amiga-draw-map-page rp g l nil)
             t))))

;; GadTools menu strip (creation/layout via WITH-VISUAL-INFO/WITH-MENUS)
;; and the party roster pane — with a full seven-member roster.
#+amigaos
(let* ((m (parse-map *art* :name "test"))
       (g (new-game m :party
                    (with-rng ()
                      (loop for i from 1 to +party-limit+
                            collect (make-hero (format nil "Hero~D" i)
                                               :tester))))))
  (check "amiga-ui menu strip and 7-row party roster draw without error" t
         (amiga.intuition:with-pub-screen (scr)
           (amiga.gadtools:with-visual-info (vi scr)
             (amiga.intuition:with-window
                 (win :title "Lambda's Tale Test"
                      :left 0 :top 0
                      :width *amiga-win-width* :height *amiga-win-height*
                      :idcmp amiga.intuition:+idcmp-closewindow+)
               (amiga.gadtools:with-menus (menu *menu-entries* vi win)
                 (let* ((rp (amiga.intuition:window-rastport win))
                        (l (%amiga-layout win rp)))
                   (%amiga-party rp g l)
                   ;; the numbered roster's character-sheet page draws too
                   (%amiga-draw-sheet rp g 0 l)
                   t)))))))

;; Custom screen support: open an own screen (RTG-aware mode pick),
;; set the palette, draw into a backdrop window, close it all again.
#+amigaos
(let* ((m (parse-map *art* :name "test"))
       (g (new-game m))
       (log (attach-message-log g)))
  (say g "Custom screen smoke test.")
  (check "amiga-ui draws on an own custom screen" t
         (amiga.intuition:with-screen
             (scr :width *amiga-screen-width*
                  :height *amiga-screen-height*
                  :depth *amiga-screen-depth*
                  :title "Lambda's Tale Test"
                  :mode-id (amiga.gfx:best-mode-id
                            :width *amiga-screen-width*
                            :height *amiga-screen-height*
                            :depth *amiga-screen-depth*))
           (%game-screen-palette scr)
           (check "custom screen reports its width" *amiga-screen-width*
                  (amiga.intuition:screen-width scr))
           (amiga.intuition:with-window
               (win :title "Lambda's Tale Test"
                    :left 0 :top 0
                    :width (amiga.intuition:screen-width scr)
                    :height (amiga.intuition:screen-height scr)
                    :screen scr
                    :flags (logior amiga.intuition:+wflg-borderless+
                                   amiga.intuition:+wflg-backdrop+
                                   amiga.intuition:+wflg-activate+)
                    :idcmp amiga.intuition:+idcmp-closewindow+)
             (let* ((rp (amiga.intuition:window-rastport win))
                    (l (%amiga-layout win rp)))
               (%amiga-draw-fp rp g (ui-layout-bx l) (ui-layout-by l)
                               (ui-layout-fp-w l) (ui-layout-fp-h l))
               (%amiga-draw-effects rp g l)
               (%amiga-draw-log rp log l)
               (%amiga-status rp g l "Screen smoke test")
               (%amiga-party rp g l)
               t)))))

;; Wall-piece assets (M3): the data/gfx ILBMs load into offscreen
;; bitmaps and the first-person view composits them with real OS blits.
;; Runs on the game's own custom screen — the nominal PAL 640x256
;; geometry guarantees the layout keeps the full 240x130 asset-size
;; viewport (the FS-UAE Workbench can be shorter than 256 lines, where
;; the view correctly falls back to the wireframe).
#+amigaos
(let* ((m (parse-map *art* :name "test"))
       (g (new-game m)))
  (%with-game-font
   (lambda (font)
     (amiga.intuition:with-screen
         (scr :width *amiga-screen-width*
              :height *amiga-screen-height*
              :depth *amiga-screen-depth*
              :title "Walls Test"
              :mode-id (amiga.gfx:best-mode-id
                        :width *amiga-screen-width*
                        :height *amiga-screen-height*
                        :depth *amiga-screen-depth*))
       (%game-screen-palette scr)
       ;; untitled: a backdrop window with a WA_Title still gets a
       ;; title bar, which would push the layout below the asset size
       (amiga.intuition:with-window
           (win :title nil :left 0 :top 0
                :width (amiga.intuition:screen-width scr)
                :height (amiga.intuition:screen-height scr)
                :screen scr
                :flags (logior amiga.intuition:+wflg-borderless+
                               amiga.intuition:+wflg-backdrop+
                               amiga.intuition:+wflg-activate+)
                :idcmp amiga.intuition:+idcmp-closewindow+)
         (check "borderless untitled backdrop window has no top border" 0
                (amiga.intuition:window-border-top win))
         (let* ((rp (%game-rastport win font))
                (l (%amiga-layout win rp)))
          (multiple-value-bind (walls pack-palette)
              (%load-wall-assets rp nil)
           (check "game font gives the designed line height" 10
                  (ui-layout-lh l))
           (check-true "wall assets load into bitmaps" walls)
           (when walls
             (check "every pack piece got a bitmap (walls + backdrops)"
                    (+ 2 (length (wall-piece-names)))
                    (hash-table-count walls))
             (check-true "the pack palette is the demo CMAP"
                         (equalp pack-palette *wall-palette*))
             ;; transparency wiring: receding side pieces carry a
             ;; cookie-cut mask; opaque front pieces and backdrops don't
             (check-true "a side piece got a cookie-cut mask"
                         (cdr (gethash '(:side 0 :l) walls)))
             (check-true "a front piece is an opaque blit (no mask)"
                         (not (cdr (gethash '(:front 0) walls))))
             (check-true "the ceiling backdrop is opaque (no mask)"
                         (not (cdr (gethash '(:ceiling) walls))))
             (check-true "custom screen leaves the full asset-size viewport"
                         (= (ui-layout-fp-h l) *fp-view-height*))
             (%amiga-draw-fp rp g (ui-layout-bx l) (ui-layout-by l)
                             (ui-layout-fp-w l) (ui-layout-fp-h l) walls)
             ;; dead end at (0,0) facing north: the front wall piece
             ;; lands on the plane-1 rect; its top edge row is the
             ;; white highlight
             (check "blitted front wall edge pixel" 1
                    (amiga.gfx:read-pixel rp (+ (ui-layout-bx l) 100)
                                          (+ (ui-layout-by l) 26)))
             ;; above the front wall the ceiling backdrop shows: (100,25)
             ;; is a speckle pixel ((7x+13y) mod 41 = 0 -> grey)
             (check "blitted ceiling speckle pixel" 2
                    (amiga.gfx:read-pixel rp (+ (ui-layout-bx l) 100)
                                          (+ (ui-layout-by l) 25)))
             ;; below the front wall the floor backdrop shows: (90,110)
             ;; is flagstone fill (local floor row 45, between joints)
             (check "blitted floor stone pixel" 2
                    (amiga.gfx:read-pixel rp (+ (ui-layout-bx l) 90)
                                          (+ (ui-layout-by l) 110))))
           (%free-wall-assets walls))))))))

;; *autoplay* drives a full unattended PLAY-AMIGA session: scripted keys
;; are fed one per INTUITICK (~10/s), ending in #\q so the event loop
;; exits on its own.  Verifies the whole real event path — window, menu
;; strip, redraws, key dispatch — with no user at the keyboard.  The
;; script also enters map mode (m), toggles the debug full view (f)
;; twice and leaves map mode (m) before quitting.
;; The scripted keys also open a character sheet (1), switch to another
;; roster slot (2) and leave it (:esc) — exercising the whole :sheet
;; mode through the real event loop.
#+amigaos
(check "amiga-ui autoplay plays a scripted session and quits" :done
       (let ((*autoplay* (list #\w #\d #\1 #\2 :esc #\w #\a
                               #\m #\f #\f #\m #\s #\q)))
         ;; :window is the development view — :screen (the default)
         ;; gets its own autoplay below
         (play-amiga "data/cellar.map" :display :window)
         :done))

;; The same unattended session on an own custom screen (:display :screen)
;; — the whole open-screen/backdrop-window/menus/event-loop path, with
;; the tile pack named explicitly (:gfx-dir): the depth-4 screen, the
;; pack palette and the ceiling/floor backdrop all draw for real.
#+amigaos
(check "amiga-ui autoplay on an own custom screen" :done
       (let ((*autoplay* (list #\w #\d #\m #\m #\q)))
         (play-amiga "data/cellar.map" :display :screen
                                       :gfx-dir "data/gfx/")
         :done))

;;; ---------------------------------------------------------------------
;;; Summary

(format t "~%Lambda's Tale tests: ~D checks, ~D failures.~%" *checks* *failures*)
(cl-user::quit (if (zerop *failures*) 0 1))
