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

;; the same slots at the lores profile's 160x112 viewport
(let ((planes (view-planes 160 112)))
  (check "lores view-planes plane 1" '(32 22 127 89) (aref planes 1))
  (check "lores front slot at depth 0" '(32 22 96 68)
         (wall-piece-rect planes '(:front 0)))
  (check "lores left side slot spans the full column" '(0 0 33 112)
         (wall-piece-rect planes '(:side 0 :l)))
  (check "lores left flank slot is the side band at wall height"
         '(0 22 33 68)
         (wall-piece-rect planes '(:flank 0 :l)))
  (check "lores piece slots lie inside the viewport" nil
         (remove-if (lambda (piece)
                      (destructuring-bind (x y w h)
                          (wall-piece-rect planes piece)
                        (and (<= 0 x) (<= 0 y) (< 0 w) (< 0 h)
                             (<= (+ x w) 160) (<= (+ y h) 112))))
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

(destructuring-bind (ceiling floor) (backdrop-rects (view-planes 160 112))
  (check "lores ceiling backdrop slot" '(0 0 160 56) ceiling)
  (check "lores floor backdrop slot" '(0 56 160 56) floor))

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
              (search "pens 4-31" manifest)))

;;; ---------------------------------------------------------------------
;;; Display profiles (src/profiles.lisp): the per-target bundles of
;;; screen geometry, viewport, tile pack and layout tuning.

(check "find-display-profile resolves :lores" *lores-profile*
       (find-display-profile :lores))
(check "find-display-profile resolves :hires" *hires-profile*
       (find-display-profile :hires))
(check "find-display-profile passes a profile through" *lores-profile*
       (find-display-profile *lores-profile*))
(check-true "find-display-profile rejects an unknown name"
            (handler-case (progn (find-display-profile :vga) nil)
              (error () t)))

(check "the default profile's pack is the default gfx-dir"
       (display-profile-gfx-dir *display-profile*) *gfx-dir*)
(check "the default profile's viewport is the default viewport"
       (list (display-profile-fp-width *display-profile*)
             (display-profile-fp-height *display-profile*))
       (list *fp-view-width* *fp-view-height*))

(let ((outer-w *fp-view-width*)
      (outer-dir *gfx-dir*))
  (with-display-profile (:hires)
    (check "with-display-profile binds the profile" :hires
           (display-profile-name *display-profile*))
    (check "with-display-profile binds the viewport" '(240 130)
           (list *fp-view-width* *fp-view-height*))
    (check "with-display-profile binds the pack dir"
           (display-profile-gfx-dir *hires-profile*) *gfx-dir*))
  (check "with-display-profile restores the viewport"
         outer-w *fp-view-width*)
  (check "with-display-profile restores the pack dir"
         outer-dir *gfx-dir*))

(with-display-profile (:hires)
  (let ((manifest (with-output-to-string (s) (print-tile-manifest s))))
    (check-true "hires manifest names its viewport"
                (search "240x130 viewport" manifest))
    (check-true "hires manifest states the 16-color palette contract"
                (search "pens 4-15" manifest))))

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

(let ((m (load-map-file "worlds/closure/cellar.map")))
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

;; Active effects (the UI's spell strip): records with an optional
;; expiry on the game clock and a payload plist of engine facts.
(let* ((m (parse-map *art* :name "test"))
       (g (new-game m)))
  (check "fresh game has no active effects" '() (game-effects g))
  (add-effect g "shield")
  (add-effect g "lamp")
  (check "effects accumulate in order" '("shield" "lamp")
         (mapcar #'effect-name (game-effects g)))
  (add-effect g "shield")
  (check "re-adding keeps one entry in place" '("shield" "lamp")
         (mapcar #'effect-name (game-effects g)))
  (check "an undated effect has no expiry" nil
         (effect-expires-at (find-effect g "shield")))
  (remove-effect g "shield")
  (check "remove-effect" '("lamp") (mapcar #'effect-name (game-effects g)))
  (remove-effect g "not-there")
  (check "removing an absent effect is quiet" '("lamp")
         (mapcar #'effect-name (game-effects g)))
  ;; durations and payloads
  (add-effect g "mage flame" :duration 60 :payload '(:light t))
  (check "duration sets the expiry on the clock"
         (+ (game-time g) 60)
         (effect-expires-at (find-effect g "mage flame")))
  (check-true "payload is stored"
              (getf (effect-payload (find-effect g "mage flame")) :light))
  (check-true "a :light payload lights the party" (light-active-p g))
  (add-effect g "mage flame" :duration 10 :payload '(:light t))
  (check "re-adding refreshes the expiry"
         (+ (game-time g) 10)
         (effect-expires-at (find-effect g "mage flame")))
  (check "effect-label downcases for the strip" "mage flame"
         (effect-label (find-effect g "mage flame")))
  (add-effect g "stone skin" :duration 30 :payload '(:ac 2))
  (add-effect g "blessing" :payload '(:ac 1))
  (check ":ac payloads sum into the party bonus" 3 (effects-ac-bonus g))
  (check "lamp carries no :ac payload" nil
         (getf (effect-payload (find-effect g "lamp")) :ac)))

;;; ---------------------------------------------------------------------
;;; Game time: the clock, day and night, darkness

;; The clock: action costs and the display line.
(let* ((m (parse-map *art* :name "test"))
       (g (new-game m)))
  (check "a fresh game starts at *new-game-minutes*"
         *new-game-minutes* (game-time g))
  (check "fresh clock line" "Day 1, 08:00" (clock-line g))
  (turn-right g)                       ; face east
  (check "a turn costs one minute" (+ *new-game-minutes* 1) (game-time g))
  (check "step east" :moved (move-party g :forward))
  (check "a step costs one minute" (+ *new-game-minutes* 2) (game-time g))
  (turn-left g)                        ; face north into the border wall
  (let ((before (game-time g)))
    (check "a blocked step is blocked" :blocked (move-party g :forward))
    (check "a blocked step costs nothing" before (game-time g)))
  (setf (game-time g) (+ +minutes-per-day+ (* 13 60) 5))
  (check "clock line formats day and zero-padded time"
         "Day 2, 13:05" (clock-line g)))

;; Daylight boundaries: [06:00, 20:00).
(check-true "05:59 is night" (not (daylight-p 359)))
(check-true "06:00 is day" (daylight-p 360))
(check-true "19:59 is day" (daylight-p 1199))
(check-true "20:00 is night" (not (daylight-p 1200)))
(check-true "daylight wraps across days"
            (daylight-p (+ +minutes-per-day+ 360)))

;; advance-time: boundary events and effect expiry.
(let* ((m (parse-map *art* :name "test"))
       (g (new-game m))
       (msgs (watch-messages g))
       (events '()))
  (on-event g :sunset (lambda (game) (declare (ignore game))
                        (push :sunset events)))
  (on-event g :sunrise (lambda (game) (declare (ignore game))
                         (push :sunrise events)))
  (setf (game-time g) 1199)
  (advance-time g)
  (check "crossing 20:00 emits :sunset" '(:sunset) events)
  (check-true "night falls in the log"
              (member "Night falls." (funcall msgs) :test #'equal))
  (setf (game-time g) (+ +minutes-per-day+ 359))
  (advance-time g)
  (check "crossing 06:00 emits :sunrise" '(:sunrise :sunset) events)
  (check-true "the sun rises in the log"
              (member "The sun rises." (funcall msgs) :test #'equal))
  ;; a timed effect expires with a message and an event
  (let ((expired '()))
    (on-event g :effect-expired
              (lambda (game name) (declare (ignore game))
                (push name expired)))
    (add-effect g "mage flame" :duration 5 :payload '(:light t))
    (advance-time g 3)
    (check "an unexpired effect stays" '("mage flame")
           (mapcar #'effect-name (game-effects g)))
    (advance-time g 2)
    (check "the effect expires on time" '() (game-effects g))
    (check "expiry emits :effect-expired" '("mage flame") expired)
    (check-true "expiry is announced"
                (member "Mage flame wears off." (funcall msgs)
                        :test #'equal))))

;; Darkness: night and :dark zones shrink the view (and the automap) to
;; one cell; a light effect restores it.
(defparameter *corridor-art*
"+-+-+-+-+
|@      |
+-+-+-+-+")

(let* ((m (parse-map *corridor-art* :name "dark-test" :start-facing :east))
       (g (new-game m)))
  (check "daylight: full view depth" +view-depth+ (game-view-depth g))
  (check-true "daylight outdoors is not dark" (not (game-dark-p g)))
  (setf (game-time g) 1200)            ; 20:00 — night
  (check-true "night outdoors is dark" (game-dark-p g))
  (check "night: view depth one" 1 (game-view-depth g))
  (check "darkness truncates compute-view" 1
         (length (compute-view (game-map g) (game-x g) (game-y g)
                               (game-facing g) (game-view-depth g))))
  (add-effect g "torchlight" :payload '(:light t))
  (check-true "a light effect defeats the night" (not (game-dark-p g)))
  (check "lit night: full view depth" +view-depth+ (game-view-depth g))
  (remove-effect g "torchlight")
  (check "light gone: dark again" 1 (game-view-depth g)))

;; The automap honors darkness: what the party cannot see it cannot map.
;; The game must be born at night — NEW-GAME's first OBSERVE already maps.
(let* ((m (parse-map *corridor-art* :name "dark-map" :start-facing :east))
       (g (let ((*new-game-minutes* 1200))
            (new-game m))))
  ;; the corridor runs east from (0,0); at night the party sees only its
  ;; own cell — the far wall of (2,0) stays unknown
  (check-true "night automap: standing cell is known"
              (cell-explored-p (game-knowledge g) 0 0))
  (check-true "night automap: two cells ahead is unknown"
              (not (wall-known-p (game-knowledge g) 2 0 +east+)))
  (add-effect g "torchlight" :payload '(:light t))
  (observe g)
  (check-true "lit automap: two cells ahead is known"
              (wall-known-p (game-knowledge g) 2 0 +east+)))

;; A (zone :dark t) zone is dark at any hour.
(let ((path "tests/tmp-dark.map"))
  (with-open-file (s path :direction :output :if-exists :supersede)
    (write-string "+-+-+
|@  |
+-+-+
(zone :kind :dungeon :title \"the crypt\" :dark t)
" s))
  (let* ((m (load-map-file path))
         (g (new-game m)))
    (check-true "zone :dark parses" (dungeon-map-dark m))
    (check-true "a :dark zone is dark at noon"
                (progn (setf (game-time g) 720) (game-dark-p g)))
    (add-effect g "mage flame" :payload '(:light t))
    (check-true "light works underground too" (not (game-dark-p g))))
  (delete-file path))

;; at-night / at-day specials: pure clock tests.
(let* ((m (parse-map *art* :name "test"))
       (g (new-game m))
       (msgs (watch-messages g)))
  (setf (cell-special m 1 0)
        '((at-night (message "Eyes glitter in the dark."))
          (at-day (message "The lane lies quiet."))))
  (turn-right g)                       ; face east
  (setf (game-time g) 719)             ; the step lands at noon
  (move-party g :forward)
  (check "at-day runs by day" '("The lane lies quiet.") (funcall msgs))
  (move-party g :back)                 ; back-step keeps facing east
  (setf (game-time g) 1249)            ; the step lands well into night
  (move-party g :forward)
  (check-true "at-night runs by night"
              (member "Eyes glitter in the dark." (funcall msgs)
                      :test #'equal))
  (check-true "at-day stays quiet by night"
              (= 1 (count "The lane lies quiet." (funcall msgs)
                          :test #'equal))))

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
  (check "sheet has seven lines" 7 (length lines))
  (check "sheet name/class line" "El Cid the War Mage" (first lines))
  (check "sheet level/xp line" "Level 3    XP 1200" (second lines))
  (check "sheet hp/ac line" "HP 9/11    AC 8" (third lines))
  (check "sheet primary stats line" "STR 15  DEX 12  IQ 9" (fourth lines))
  (check "sheet secondary stats line" "CON 14  LCK 10" (fifth lines))
  (check "sheet gold line, standing" "Gold 250 gp" (sixth lines))
  (check "sheet pack line, empty pack" "Pack: nothing" (seventh lines)))
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
;;; Spells: casters, spell points, DEFINE-SPELL, casting, the cast menu

(define-hero-class :t-mage :hp-dice "1d6+3" :damage "1d4" :ac 10 :caster t)

(defun %combat-mage (&optional (name "Zzgo"))
  "A deterministic level-1 :t-mage: 7 hp, iq 18 (+4 bonus, 6 sp)."
  (let ((h (with-rng (3  0 0 0  0 0 0  5 5 5) (make-hero name :t-mage))))
    (setf (hero-str h) 10)
    h))

;; Casters and spell points.
(let ((mage (%combat-mage))
      (grunt (%combat-hero)))
  (check-true "a :caster class hero is a caster" (hero-caster-p mage))
  (check "caster sp: 2 per level + iq bonus" 6 (hero-max-sp mage))
  (check "caster starts at full sp" 6 (hero-sp mage))
  (check-true "a plain class hero is no caster" (not (hero-caster-p grunt)))
  (check "non-caster has no sp" 0 (hero-max-sp grunt))
  (check-true "caster sheet shows sp"
              (search "SP 6/6" (third (hero-summary-lines mage))))
  (check-true "non-caster sheet stays sp-free"
              (not (search "SP" (third (hero-summary-lines grunt))))))

;; Leveling grows sp like hp: the new maximum arrives ready to burn.
(let* ((m (parse-map *art* :name "test"))
       (h (%combat-mage))
       (g (new-game m :party (list h))))
  (setf (hero-sp h) 1)
  (with-rng (2) (award-xp g h 100))     ; level 2: max-sp 2*2+4 = 8
  (check "level-up raises max sp" 8 (hero-max-sp h))
  (check "level-up adds the growth to current sp" 3 (hero-sp h)))

;; DEFINE-SPELL validation and the registry.
(define-spell 'test-bolt  :cost 2 :level 1 :classes '(:t-mage)
  :damage "1d4")
(define-spell 'test-mend  :cost 2 :level 1 :classes '(:t-mage)
  :heal "1d8")
(define-spell 'test-shield :cost 3 :level 1 :classes '(:t-mage)
  :buff-ac 2 :duration 30)
(define-spell 'test-flame :cost 1 :level 1 :classes '(:t-mage)
  :light t :duration 60)
(define-spell 'test-lore  :cost 1 :level 3 :classes '(:t-mage)
  :heal "1d4")

(check "spell-title downcases the name" "test bolt" (spell-title 'test-bolt))
(check-error "unknown spell rejected" (find-spell-type 'test-nonesuch))
(check-error "define-spell needs exactly one effect"
  (define-spell 'test-bogus :damage "1d4" :heal "1d4"))
(check-error "define-spell needs an effect"
  (define-spell 'test-bogus :cost 1))
(check-error "a timed spell needs a duration"
  (define-spell 'test-bogus :buff-ac 1))
(check-error "duration must be a positive integer"
  (define-spell 'test-bogus :light t :duration -5))

;; Knowledge gates: class, level, caster-ness.
(let ((mage (%combat-mage))
      (grunt (%combat-hero)))
  (check-true "mage knows a class spell" (spell-known-p mage 'test-bolt))
  (check-true "non-caster knows nothing" (not (spell-known-p grunt 'test-bolt)))
  (check-true "level gate holds" (not (spell-known-p mage 'test-lore)))
  (setf (hero-level mage) 3)
  (check-true "level gate opens" (spell-known-p mage 'test-lore))
  (check "known spells in registration order"
         '(test-bolt test-mend test-shield test-flame test-lore)
         (spells-for-hero mage)))

;; Cast refusals: each says why, costs nothing, returns NIL.
(let* ((m (parse-map *art* :name "test"))
       (mage (%combat-mage))
       (g (new-game m :party (list mage)))
       (msgs (watch-messages g)))
  (check "damage spell out of combat refused" nil
         (cast-spell g mage 'test-bolt))
  (check-true "out-of-combat refusal says why"
              (find-if (lambda (s) (search "nothing to strike" s))
                       (funcall msgs)))
  (setf (hero-sp mage) 1)
  (check "unaffordable spell refused" nil (cast-spell g mage 'test-mend))
  (check-true "no-sp refusal says why"
              (find-if (lambda (s) (search "lacks the spell points" s))
                       (funcall msgs)))
  (check "refusals cost no sp" 1 (hero-sp mage))
  (check "unknown-to-the-hero spell refused" nil
         (cast-spell g (%combat-hero) 'test-bolt)))

;; Damage cast in combat: auto-hits the first living monster.
(let* ((m (parse-map *art* :name "test"))
       (mage (%combat-mage))
       (g (new-game m :party (list mage)))
       (msgs (watch-messages g))
       (cast '()))
  (on-event g :spell-cast (lambda (game h name) (declare (ignore game))
                            (push (list (hero-name h) name) cast)))
  (start-combat g '(("test rat" 1)))    ; 3 hp
  (check-true "bolt slays the rat"
              (with-rng (2) (cast-spell g mage 'test-bolt)))  ; 1d4 -> 3
  (check "cast paid its sp" 4 (hero-sp mage))
  (check ":spell-cast emitted" '(("Zzgo" test-bolt)) cast)
  (check-true "cast announced"
              (find-if (lambda (s) (search "casts test bolt" s))
                       (funcall msgs)))
  (check-true "spell kill reads like a kill"
              (find-if (lambda (s) (search "slays the test rat" s))
                       (funcall msgs))))

;; Heal targets a chosen hero; buffs and light become timed effects.
(let* ((m (parse-map *art* :name "test"))
       (mage (%combat-mage))
       (grunt (%combat-hero))
       (g (new-game m :party (list grunt mage))))
  (damage-hero g grunt 5)
  (check-true "mend heals the chosen hero"
              (with-rng (3) (cast-spell g mage 'test-mend grunt)))
  (check "heal landed on the target" 7 (hero-hp grunt))
  (check-true "shield casts" (cast-spell g mage 'test-shield))
  (check "shield lowers the party's effective ac" 6
         (hero-effective-ac grunt g))
  (check "plain ac untouched without game context" 8
         (hero-effective-ac grunt))
  (check "shield is a timed effect"
         (+ (game-time g) 30)
         (effect-expires-at (find-effect g "test shield")))
  (check-true "flame casts" (cast-spell g mage 'test-flame))
  (check-true "flame lights the party" (light-active-p g))
  (advance-time g 30)
  (check "expired shield stops shielding" 8 (hero-effective-ac grunt g))
  (check-true "flame still burns" (light-active-p g))
  (check "recasting an active effect pays and succeeds" 0
         (progn (setf (hero-sp mage) 1)
                (cast-spell g mage 'test-flame)
                (hero-sp mage)))
  (check "recast refreshed the flame's expiry"
         (+ (game-time g) 60)
         (effect-expires-at (find-effect g "test flame"))))

;; combat-round accepts (:cast SPELL [TARGET]) beside :attack/:defend.
(let* ((m (parse-map *art* :name "test"))
       (grunt (%combat-hero))
       (mage (%combat-mage))
       (g (new-game m :party (list grunt mage))))
  (start-combat g '(("test rat" 2)))  ; two rats, 3 hp each
  ;; grunt attacks (d20=11 hits ac 10, 1d6=3 slays rat #1); the mage's
  ;; bolt (1d4=3) slays rat #2; nobody is left to strike back.
  (check "mixed action round wins" :victory
         (with-rng (10 2 2)
           (combat-round g (list :attack '(:cast test-bolt)))))
  (check "the cast in the round paid sp" 4 (hero-sp mage)))

;; SP regen: daylight, outdoors, out of combat — 1 sp per 4 minutes.
(let* ((m (parse-map *corridor-art* :name "regen" :start-facing :east))
       (mage (%combat-mage))
       (g (new-game m :party (list mage))))
  (setf (hero-sp mage) 0)
  (setf (game-time g) 480)              ; day 1, 08:00 — daylight
  (dotimes (i 8) (turn-right g))        ; 8 minutes pass outdoors
  (check "eight daylight minutes regain two sp" 2 (hero-sp mage))
  (setf (game-time g) 1240)             ; night
  (dotimes (i 8) (turn-right g))
  (check "night regains nothing" 2 (hero-sp mage))
  (setf (hero-sp mage) (hero-max-sp mage))
  (setf (game-time g) 480)
  (dotimes (i 8) (turn-right g))
  (check "regen caps at max sp" (hero-max-sp mage) (hero-sp mage)))

;; No regen underground: a :dark zone shuts the sky out.
(let ((path "tests/tmp-dark-regen.map"))
  (with-open-file (s path :direction :output :if-exists :supersede)
    (write-string "+-+-+
|@  |
+-+-+
(zone :dark t)
" s))
  (let* ((m (load-map-file path))
         (mage (%combat-mage))
         (g (new-game m :party (list mage))))
    (setf (hero-sp mage) 0)
    (setf (game-time g) 480)
    (dotimes (i 8) (turn-right g))
    (check "no regen in a :dark zone" 0 (hero-sp mage)))
  (delete-file path))

;; The cast menu model: the full key walk both frontends drive.
(let* ((m (parse-map *art* :name "test"))
       (grunt (%combat-hero))
       (mage (%combat-mage))
       (g (new-game m :party (list grunt mage)))
       (view (make-cast-view)))
  (check-true "caster page lists only casters"
              (let ((lines (cast-lines g view)))
                (and (find-if (lambda (s) (search "2) Zzgo" s)) lines)
                     (not (find-if (lambda (s) (search "Alva" s)) lines)))))
  (check "a non-caster digit is ignored" nil
         (progn (cast-act g view #\1) (cast-view-hero view)))
  (cast-act g view #\2)
  (check "digit picks the caster by party slot" "Zzgo"
         (hero-name (cast-view-hero view)))
  (check-true "spell page shows the book"
              (find-if (lambda (s) (search "test bolt" s))
                       (cast-lines g view)))
  ;; a damage spell out of combat: refused in place, menu stays open
  (check "uncastable pick refuses and stays" nil (cast-act g view #\1))
  (check-true "menu still open on the spell page"
              (and (cast-view-hero view) (null (cast-view-spell view))))
  ;; a heal walks on to the target page and commits
  (damage-hero g grunt 3)
  (cast-act g view #\2)                 ; test-mend -> target page
  (check-true "heal pick opens the target page"
              (find-if (lambda (s) (search "on whom?" s))
                       (cast-lines g view)))
  (check "target digit commits the cast" :done
         (with-rng (7) (cast-act g view #\1)))
  (check "menu cast healed the grunt" 8 (hero-hp grunt))
  (check "menu cast paid the sp" 4 (hero-sp mage)))

;; Esc unwinds the menu one page at a time, then cancels.
(let* ((m (parse-map *art* :name "test"))
       (mage (%combat-mage))
       (g (new-game m :party (list mage)))
       (view (make-cast-view)))
  (cast-act g view #\1)
  (cast-act g view #\2)                 ; test-mend -> target page
  (check "esc leaves the target page" nil (cast-act g view #\Escape))
  (check "back on the spell page" nil (cast-view-spell view))
  (check "esc leaves the spell page" nil (cast-act g view #\Escape))
  (check "back on the caster page" nil (cast-view-hero view))
  (check "esc at the top cancels" :cancelled (cast-act g view #\Escape)))

;; In combat the menu composes a full round: the caster casts, every
;; other living hero attacks.
(let* ((m (parse-map *art* :name "test"))
       (grunt (%combat-hero))
       (mage (%combat-mage))
       (g (new-game m :party (list grunt mage)))
       (view (make-cast-view :in-combat t)))
  (start-combat g '(("test rat" 2)))
  (cast-act g view #\2)                 ; Zzgo
  ;; grunt attack: d20=11 hits, 1d6=3 slays; bolt 1d4=3 slays the other.
  (check "combat commit fights the round" :done
         (with-rng (10 2 2) (cast-act g view #\1)))
  (check "the round is won" nil (game-combat g)))

;;; ---------------------------------------------------------------------
;;; Items, inventory and equipment (M4)

(define-item 't-torch   :price 2)
(define-item 't-sword   :kind :weapon :price 10 :damage "1d6+2")
(define-item 't-axe     :kind :weapon :price 14 :damage "1d8")
(define-item 't-mail    :kind :armor  :price 20 :ac 4 :classes '(:tester))
(define-item 't-buckler :kind :shield :price 6  :ac 1)

(check "item-title capitalizes the name" "T Sword" (item-title 't-sword))
(check "item-title override" "Fancy Lamp"
       (item-title (define-item 't-lamp :title "Fancy Lamp" :price 1)))
(check-error "unknown item rejected" (find-item-type 't-nonesuch))
(check-error "define-item rejects a bad kind"
  (define-item 't-bogus :kind :hat))

(check "inventory limit is eight" 8 +inventory-limit+)

;; Pack: give up to the limit, refuse the ninth, drop.
(let* ((m (parse-map *art* :name "test"))
       (h (%combat-hero))
       (g (new-game m :party (list h)))
       (msgs (watch-messages g)))
  (dotimes (i 8)
    (check-true (format nil "give-item accepts item ~D" (1+ i))
                (give-item g h 't-torch)))
  (check "pack holds eight" 8 (length (hero-items h)))
  (check "give-item refuses the ninth" nil (give-item g h 't-sword))
  (check-true "pack-full message"
              (find-if (lambda (s) (search "pack is full" s))
                       (funcall msgs)))
  (check-true "drop-item removes one" (drop-item g h 't-torch))
  (check "pack down to seven" 7 (length (hero-items h)))
  (check "drop-item without the item" nil (drop-item g h 't-sword))
  (check-error "give-item checks the item exists" (give-item g h 't-nada)))

;; Equipment: one per kind, class restrictions, misc not equippable.
(let* ((m (parse-map *art* :name "test"))
       (h (%combat-hero))
       (g (new-game m :party (list h)))
       (msgs (watch-messages g)))
  (check "bare attack dice are the class dice" "1d6" (hero-attack-dice h))
  (check "bare effective ac is the class ac" 8 (hero-effective-ac h))
  (check "equip-item needs the item in the pack" nil
         (equip-item g h 't-sword))
  (give-item g h 't-sword)
  (give-item g h 't-axe)
  (give-item g h 't-mail)
  (give-item g h 't-buckler)
  (give-item g h 't-torch)
  (check-true "equip a weapon" (equip-item g h 't-sword))
  (check "equipped weapon found" 't-sword (equipped-of-kind h :weapon))
  (check "weapon changes the attack dice" "1d6+2" (hero-attack-dice h))
  (check-true "equip message"
              (find-if (lambda (s) (search "equips T Sword" s))
                       (funcall msgs)))
  (check-true "equipping a second weapon swaps it" (equip-item g h 't-axe))
  (check "swapped weapon" 't-axe (equipped-of-kind h :weapon))
  (check "only one weapon equipped" 1 (length (hero-equipped h)))
  (check "the swapped-out sword stays in the pack" 5
         (length (hero-items h)))
  (check-true "equip armor (class allowed)" (equip-item g h 't-mail))
  (check-true "equip a shield" (equip-item g h 't-buckler))
  (check "armor and shield lower the descending ac" 3
         (hero-effective-ac h))
  (check "misc items cannot be equipped" nil (equip-item g h 't-torch))
  (check-true "unequip returns t" (unequip-item g h 't-mail))
  (check "unequip keeps the item in the pack" t
         (not (not (hero-carrying-p h 't-mail))))
  (check "ac back without the armor" 7 (hero-effective-ac h))
  (check "unequip when not equipped" nil (unequip-item g h 't-mail))
  ;; the character sheet's pack line marks equipped items with *
  (check "sheet pack line marks equipped"
         "Pack: T Sword, T Axe*, T Mail, T Buckler*, T Torch"
         (seventh (hero-summary-lines h))))

;; Class restrictions: a hero whose class the item excludes.
(define-hero-class :t-wizard :hp-dice "1d4" :damage "1d3" :ac 10)
(let* ((m (parse-map *art* :name "test"))
       (h (with-rng () (make-hero "Wiz" :t-wizard)))
       (g (new-game m :party (list h)))
       (msgs (watch-messages g)))
  (give-item g h 't-mail)
  (check "class-restricted item refused" nil (equip-item g h 't-mail))
  (check-true "cannot-use message"
              (find-if (lambda (s) (search "cannot use" s))
                       (funcall msgs)))
  (check "item-usable-p for the wrong class" nil (item-usable-p h 't-mail))
  (check-true "item-usable-p unrestricted" (item-usable-p h 't-torch)))

;; Combat uses the equipment: weapon dice on the attack, effective AC
;; against the monsters.
(let* ((m (parse-map *art* :name "test"))
       (h (%combat-hero))
       (g (new-game m :party (list h))))
  (give-item g h 't-sword)
  (equip-item g h 't-sword)
  (start-combat g '(("test rat" 1)))
  ;; hero d20=11 hits ac 10; weapon 1d6+2 rolls 2 -> 5 damage, str 10
  ;; adds nothing: the 3 hp rat dies in one blow.
  (check "weapon dice carry the round" :victory
         (with-rng (10 1) (combat-round g))))
(let* ((m (parse-map *art* :name "test"))
       (h (%combat-hero))
       (g (new-game m :party (list h))))
  (give-item g h 't-mail)
  (equip-item g h 't-mail)
  (give-item g h 't-buckler)
  (equip-item g h 't-buckler)
  (start-combat g '(("test rat" 1)))
  ;; effective ac 3: the rat needs d20 >= 16 (1 + roll + level 1).
  ;; hero misses (d20=1); rat d20=15 -> 17 < 20-3: a miss.
  (check "armor turns the blow" :ongoing
         (with-rng (0 0 14) (combat-round g)))
  (check "no damage through the armor" 8 (hero-hp h))
  ;; same roll against the bare hero would have hit (17 >= 12): take
  ;; the armor off and let it land.
  (unequip-item g h 't-mail)
  (unequip-item g h 't-buckler)
  (check "bare round" :ongoing (with-rng (0 0 14 0) (combat-round g)))
  (check "the same swing hits without armor" 7 (hero-hp h)))

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
(let ((m (load-map-file "worlds/closure/cellar.map")))
  (check "cellar is a dungeon zone" :dungeon (dungeon-map-kind m))
  (check-true "cellar start special" (cell-special m 0 0))
  (check-true "cellar stairs-down special" (cell-special m 3 2))
  (check-true "cellar ladder leads back to town"
              (find-if (lambda (op) (string-equal (first op) "TRAVEL"))
                       (cell-special m 5 4))))

;; The shipped town: a city zone whose buildings and gates are data.
(let ((m (load-map-file "worlds/closure/town.map")))
  (check "town is a city zone" :city (dungeon-map-kind m))
  (check "town title" "Closure" (map-title m))
  (check-true "town shoppe location"
              (find-if (lambda (op) (string-equal (first op) "LOCATION"))
                       (cell-special m 2 1)))
  (check-true "town tavern leads to the cellar"
              (find-if (lambda (op) (string-equal (first op) "TRAVEL"))
                       (cell-special m 6 1)))
  (check "town declares the city tile pack" "gfx-city-demo/"
         (dungeon-map-gfx m)))

;; Zone tile packs: (zone :gfx DIR) names the zone's pack, and
;; ZONE-GFX-DIR resolves it in two steps — relative to the map file's
;; directory when the pack lives there (self-contained world), else
;; relative to the game directory (a shipped pack).
(check-error "zone :gfx must be a directory string"
  (%apply-map-form (parse-map *art*) '(zone :gfx 42) "x.map"))
(let* ((m (parse-map *art* :name "data/x.map"))
       (g (new-game m)))
  (%apply-map-form m '(zone :gfx "gfx/") "data/x.map")
  (check "zone :gfx parses" "gfx/" (dungeon-map-gfx m))
  (check "zone pack resolves map-relative when it lives there"
         "data/gfx/" (zone-gfx-dir g)))
(let* ((m (parse-map *art* :name "worlds/closure/x.map"))
       (g (new-game m)))
  (%apply-map-form m '(zone :gfx "gfx-city-demo/") "x.map")
  (check "zone pack falls back to the game directory"
         "gfx-city-demo/" (zone-gfx-dir g)))
(check "a zone without :gfx has no pack" nil
       (zone-gfx-dir (new-game (parse-map *art*))))

;; A world is a directory: the campaign.lisp NEXT TO the map file is
;; the one that loads — a designer's own world brings its own classes,
;; monsters and items, never the demo's.
(with-open-file (s "tests/campaign.lisp" :direction :output
                   :if-exists :supersede)
  (write-string "(in-package :tale)
(define-item 't-camp-ration :price 7)
" s))
(check "load-campaign finds the campaign next to the map"
       "tests/campaign.lisp" (load-campaign "tests/anything.map"))
(check "its definitions are live" 7 (item-price 't-camp-ration))
(delete-file "tests/campaign.lisp")
(check "no campaign next to the map" nil
       (load-campaign "data/gfx/anything.map"))

;; Walk the shipped world end-to-end: gate -> shoppe -> tavern -> cellar
;; and back up — the demo campaign's whole overworld loop on real data.
;; The shoppe's stock names campaign items, so the campaign loads first
;; (exactly what PLAY/PLAY-AMIGA do).
(load-campaign "worlds/closure/town.map")
(let* ((m (load-map-file "worlds/closure/town.map"))
       (g (new-game m)))
  (trigger-special g)
  (check "the town's zone pack is the shipped city pack" "gfx-city-demo/"
         (zone-gfx-dir g))
  ;; to the shoppe: N, around the well block, through the door
  (move-party g)                          ; (4,4)
  (turn-left g)
  (move-party g) (move-party g)           ; (2,4)
  (turn-right g)
  (move-party g) (move-party g)           ; (2,2)
  (check "shoppe door opens" :door (move-party g))
  (check "the shoppe is a shop location" :shop
         (location-kind (game-location g)))
  (leave-location g)
  ;; over to the tavern: back out, east along the street, up the door
  (move-party g :back)                    ; (2,2)
  (turn-right g)
  (move-party g) (move-party g) (move-party g) (move-party g) ; (6,2)
  (turn-left g)
  (check "tavern trapdoor drops into the cellar" :door (move-party g))
  (check "tavern travel landed in the cellar" "the cellar"
         (map-title (game-map g)))
  (check "the cellar zone has no pack (profile default)" nil
         (zone-gfx-dir g))
  (check "cellar arrival at its start" '(0 0)
         (list (game-x g) (game-y g)))
  ;; the ladder back up: teleport to the cellar's < cell and step on it
  (teleport-party g 5 4)
  (check "ladder returns to the town" "Closure"
         (map-title (game-map g)))
  (check "ladder lands on the tavern doorstep" '(6 2)
         (list (game-x g) (game-y g))))

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

(define-hero-class :t-caster :hp-dice "1d4" :damage "1d3" :ac 10 :caster t)
(let* ((m (load-map-file "tests/tmp.map"))
       (a (with-rng (5) (make-hero "Alva" :tester)))
       (b (with-rng (5) (make-hero "Berk" :tester)))
       (c (with-rng (5) (make-hero "Cael" :t-caster)))
       (g (new-game m :party (list a b c))))
  (turn-right g)
  (move-party g :forward)               ; to (1,0)
  (set-flag g :quest 42)
  (set-flag g '(:seen "door") t)
  (damage-hero g a 3)
  (setf (hero-xp b) 60)
  (incf (hero-gold b) 17)
  (decf (hero-sp c))
  (setf (game-time g) 700)
  (add-effect g "mage flame" :duration 60 :payload '(:light t))
  (add-effect g "blessing" :payload '(:ac 1))
  (save-game g "tests/tmp-save.lisp")
  (let ((g2 (load-game "tests/tmp-save.lisp")))
    (check "loaded position" '(1 0) (list (game-x g2) (game-y g2)))
    (check "loaded facing" +east+ (game-facing g2))
    (check "loaded clock" 700 (game-time g2))
    (check "loaded effects in order" '("mage flame" "blessing")
           (mapcar #'effect-name (game-effects g2)))
    (check "loaded effect keeps its expiry" 760
           (effect-expires-at (find-effect g2 "mage flame")))
    (check "loaded effect keeps its payload" '(:light t)
           (effect-payload (find-effect g2 "mage flame")))
    (check "loaded undated effect stays undated" nil
           (effect-expires-at (find-effect g2 "blessing")))
    (check "loaded :ac payload feeds the party bonus" 1
           (effects-ac-bonus g2))
    (check "loaded flag value" 42 (flag g2 :quest))
    (check "loaded equal-key flag" t (flag g2 '(:seen "door")))
    (check "loaded party size" 3 (length (game-party g2)))
    (let ((a2 (first (game-party g2)))
          (b2 (second (game-party g2)))
          (c2 (third (game-party g2))))
      (check "loaded hero name" "Alva" (hero-name a2))
      (check "loaded hero class" :tester (hero-class a2))
      (check "loaded hero damage taken" 5 (hero-hp a2))
      (check "loaded hero max hp" 8 (hero-max-hp a2))
      (check "loaded hero xp" 60 (hero-xp b2))
      (check "loaded hero gold" 17 (hero-gold b2))
      (check-true "loaded caster hero is still a caster" (hero-caster-p c2))
      (check "loaded caster max-sp" (hero-max-sp c) (hero-max-sp c2))
      (check "loaded caster sp" (hero-sp c) (hero-sp c2)))
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
;;; The world: zones and travel (M4).  Cities and dungeons are both
;;; first-class: ordinary maps that self-describe through their ZONE
;;; form, linked by TRAVEL specials.

;; Relative map paths resolve against the current map's directory.
(check "resolve sibling path" "worlds/closure/town.map"
       (%resolve-map-path "worlds/closure/cellar.map" "town.map"))
(check "resolve flat path" "town.map"
       (%resolve-map-path "cellar.map" "town.map"))
(check "resolve amiga volume base" "dh0:games/b.map"
       (%resolve-map-path "dh0:games/a.map" "b.map"))
(check "resolve volume-only base" "dh0:b.map"
       (%resolve-map-path "dh0:a.map" "b.map"))
(check "absolute posix target stays" "/maps/b.map"
       (%resolve-map-path "data/a.map" "/maps/b.map"))
(check "absolute amiga target stays" "vol:b.map"
       (%resolve-map-path "data/a.map" "vol:b.map"))

;; The two test zones: a city with a shop and a stairs-down cell, and a
;; dungeon whose (1,0) leads back up.
(with-open-file (s "tests/tmp-town.map" :direction :output
                   :if-exists :supersede)
  (write-string "+-+-+-+
|@D  <|
+-+-+-+

(zone :kind :city :title \"Testville\")
(special (1 0)
  (location \"The Test Shoppe\" :shop :stock (t-sword t-mail t-torch)))
(special (2 0) (message \"Down you go.\") (travel \"tmp-dung.map\"))
" s))
(with-open-file (s "tests/tmp-dung.map" :direction :output
                   :if-exists :supersede)
  (write-string "+-+-+
|@  |
+-+-+

(zone :kind :dungeon :title \"Testpit\" :start-facing :east)
(special (1 0)
  (message \"A ladder leads up.\")
  (travel \"tmp-town.map\" 0 0 :north)
  (set-flag :after-travel))
" s))

;; Zone metadata comes from the file; plain maps stay dungeons.
(let ((m (load-map-file "tests/tmp-town.map")))
  (check "zone kind read" :city (dungeon-map-kind m))
  (check "zone title read" "Testville" (dungeon-map-title m))
  (check "map-title prefers the zone title" "Testville" (map-title m)))
(let ((m (parse-map *art* :name "plain")))
  (check "default zone kind" :dungeon (dungeon-map-kind m))
  (check "map-title falls back to the name" "plain" (map-title m)))
(let ((m (load-map-file "tests/tmp-dung.map")))
  (check "zone start-facing applies" :east (dungeon-map-start-facing m)))
(with-open-file (s "tests/tmp.map" :direction :output :if-exists :supersede)
  (write-string "+-+
|@|
+-+
(zone :wrap t :start-facing :south)
" s))
(let ((m (load-map-file "tests/tmp.map")))
  (check-true "zone :wrap applies" (dungeon-map-wrap m))
  (check "zone :start-facing keyword form" :south
         (dungeon-map-start-facing m)))
(with-open-file (s "tests/tmp.map" :direction :output :if-exists :supersede)
  (write-string "+-+
|@|
+-+
(zone :kind \"city\")
" s))
(check-error "zone kind must be a keyword" (load-map-file "tests/tmp.map"))
(delete-file "tests/tmp.map")

;; Travel: switch zones, keep each zone's map and knowledge alive.
(let* ((m (load-map-file "tests/tmp-town.map"))
       (g (new-game m))
       (msgs (watch-messages g))
       (zones '()))
  (on-event g :enter-zone
            (lambda (game map) (declare (ignore game))
              (push (map-title map) zones)))
  (check-true "town start explored"
              (cell-explored-p (game-knowledge g) 0 0))
  (travel-party g "tmp-dung.map")
  (check "travel lands in the dungeon" "Testpit" (map-title (game-map g)))
  (check "travel resolved the sibling path" "tests/tmp-dung.map"
         (dungeon-map-name (game-map g)))
  (check ":enter-zone emitted" '("Testpit") zones)
  (check-true "enter-zone message"
              (find-if (lambda (s) (search "You enter Testpit" s))
                       (funcall msgs)))
  (check "arrival at the target start" '(0 0)
         (list (game-x g) (game-y g)))
  (check "arrival facing the zone's start-facing" +east+ (game-facing g))
  (check-true "dungeon knowledge is fresh"
              (not (cell-explored-p (game-knowledge g) 1 0)))
  ;; step east onto the ladder cell: its special travels back up, and
  ;; the op AFTER the travel must not run (it belongs to the old cell)
  (let ((dung-map (game-map g))
        (dung-knowledge (game-knowledge g)))
    (move-party g :forward)
    (check "ladder special travels back to town" "Testville"
           (map-title (game-map g)))
    (check "explicit travel target position" '(0 0)
           (list (game-x g) (game-y g)))
    (check "explicit travel facing" +north+ (game-facing g))
    (check "ops after travel are skipped" nil (flag g :after-travel))
    (check-true "ladder message ran before the travel"
                (find-if (lambda (s) (search "ladder" s)) (funcall msgs)))
    ;; back down: the dungeon zone is reused, not reloaded
    (travel-party g "tmp-dung.map")
    (check-true "revisited zone keeps its map object"
                (eq dung-map (game-map g)))
    (check-true "revisited zone keeps its knowledge"
                (eq dung-knowledge (game-knowledge g)))
    (check-true "knowledge remembers the ladder cell"
                (cell-explored-p (game-knowledge g) 1 0))))

;; Travel guards: bad targets fail loudly.
(let* ((m (load-map-file "tests/tmp-town.map"))
       (g (new-game m)))
  (check-error "travel to a missing map file"
    (travel-party g "tmp-nonesuch.map"))
  (check-error "travel target outside the map"
    (travel-party g "tmp-dung.map" 9 9))
  (start-combat g '(("test rat" 1)))
  (check-error "no traveling during combat"
    (travel-party g "tmp-dung.map")))

;; A travel loop in map data hits the recursion cap instead of hanging.
(with-open-file (s "tests/tmp-loop-a.map" :direction :output
                   :if-exists :supersede)
  (write-string "+-+
|@|
+-+
(special (0 0) (travel \"tmp-loop-b.map\"))
" s))
(with-open-file (s "tests/tmp-loop-b.map" :direction :output
                   :if-exists :supersede)
  (write-string "+-+
|@|
+-+
(special (0 0) (travel \"tmp-loop-a.map\"))
" s))
(check-error "travel loop capped"
  (let ((g (new-game (load-map-file "tests/tmp-loop-a.map"))))
    (trigger-special g)))
(delete-file "tests/tmp-loop-a.map")
(delete-file "tests/tmp-loop-b.map")

;;; ---------------------------------------------------------------------
;;; Locations and shops (M4)

;; Stepping onto the shop cell enters the location; the game becomes
;; modal like combat.
(let* ((m (load-map-file "tests/tmp-town.map"))
       (h (%combat-hero))
       (g (new-game m :party (list h)))
       (msgs (watch-messages g))
       (entered '())
       (left '()))
  (on-event g :enter-location
            (lambda (game loc) (declare (ignore game))
              (push (location-title loc) entered)))
  (on-event g :leave-location
            (lambda (game loc) (declare (ignore game))
              (push (location-title loc) left)))
  (turn-right g)
  (check "stepping into the shop passes the door" :door
         (move-party g :forward))
  (check-true "location is set" (game-location g))
  (check "location title" "The Test Shoppe"
         (location-title (game-location g)))
  (check "location kind" :shop (location-kind (game-location g)))
  (check "shop stock from map data" '(t-sword t-mail t-torch)
         (shop-stock (game-location g)))
  (check ":enter-location emitted" '("The Test Shoppe") entered)
  (check-true "entry message"
              (find-if (lambda (s) (search "enters The Test Shoppe" s))
                       (funcall msgs)))
  (check-error "no walking inside a location" (move-party g :forward))
  (check-error "no nested locations"
    (enter-location g '("Another" :shop)))
  (check-true "leave-location returns the location" (leave-location g))
  (check "location cleared" nil (game-location g))
  (check ":leave-location emitted" '("The Test Shoppe") left)
  (check "leave-location when outside" nil (leave-location g))
  (check "movement works again" :moved (move-party g :forward)))

;; Location specs are validated loudly.
(let ((g (new-game (parse-map *art* :name "test"))))
  (check-error "location title must be a string"
    (enter-location g '(nope :shop)))
  (check-error "location kind must be a keyword"
    (enter-location g '("X" shop)))
  (check-error "shop stock items must exist"
    (enter-location g '("X" :shop :stock (t-nada)))))

;; Buying and selling.
(let* ((m (load-map-file "tests/tmp-town.map"))
       (h (%combat-hero))
       (g (new-game m :party (list h)))
       (msgs (watch-messages g)))
  (setf (hero-gold h) 30)
  (check "item price" 10 (item-price 't-sword))
  (check "sell price is half" 5 (item-sell-price 't-sword))
  (check-true "buy a sword" (buy-item g h 't-sword))
  (check "gold deducted" 20 (hero-gold h))
  (check-true "bought item in the pack" (hero-carrying-p h 't-sword))
  (check "fresh equipment auto-equips" 't-sword
         (equipped-of-kind h :weapon))
  (check-true "buy message"
              (find-if (lambda (s) (search "buys T Sword for 10 gold" s))
                       (funcall msgs)))
  (check-true "buy a second sword" (buy-item g h 't-sword))
  (check "no re-equip with a weapon in hand" 1 (length (hero-equipped h)))
  (setf (hero-gold h) 1)
  (check "cannot afford" nil (buy-item g h 't-torch))
  (check "gold untouched on refusal" 1 (hero-gold h))
  (check-true "afford message"
              (find-if (lambda (s) (search "cannot afford" s))
                       (funcall msgs)))
  ;; fill the pack: the shop refuses when there is no room
  (loop while (< (length (hero-items h)) +inventory-limit+)
        do (give-item g h 't-torch))
  (setf (hero-gold h) 50)
  (check "full pack refuses the purchase" nil (buy-item g h 't-torch))
  (check "gold untouched on a full pack" 50 (hero-gold h))
  ;; selling: half price back, equipped items are unequipped
  (check-true "sell the equipped sword" (sell-item g h 't-sword))
  (check "sell pays half price" 55 (hero-gold h))
  (check "selling unequips" nil (equipped-of-kind h :weapon))
  (check-true "the second sword is still packed"
              (hero-carrying-p h 't-sword))
  (check "sell without the item" nil (sell-item g h 't-mail)))

;; A class the armor excludes buys it without auto-equip.
(let* ((m (load-map-file "tests/tmp-town.map"))
       (h (with-rng () (make-hero "Wiz" :t-wizard)))
       (g (new-game m :party (list h))))
  (setf (hero-gold h) 30)
  (check-true "wizard buys the mail anyway" (buy-item g h 't-mail))
  (check "but does not auto-equip it" nil (equipped-of-kind h :armor)))

;; The shared shop interaction model: both front-ends feed keys into
;; SHOP-ACT and draw SHOP-LINES, so the whole flow tests here.
(let* ((m (load-map-file "tests/tmp-town.map"))
       (h (%combat-hero))
       (g (new-game m :party (list h)))
       (view (make-shop-view)))
  (setf (hero-gold h) 30)
  (turn-right g)
  (move-party g :forward)               ; into the shop
  (check-true "pick page shows the shop name"
              (search "The Test Shoppe" (first (shop-lines g view))))
  (check-true "pick page asks who shops"
              (find-if (lambda (s) (search "Who is shopping?" s))
                       (shop-lines g view)))
  (check "digit picks the hero" nil (shop-act g view #\1))
  (check-true "hero selected" (eq h (shop-view-hero view)))
  (check-true "buy page lists the stock priced"
              (find-if (lambda (s) (search "1) T Sword  10 gp" s))
                       (shop-lines g view)))
  (shop-act g view #\1)                 ; buy the sword
  (check "shop-act buys" 20 (hero-gold h))
  (check "s flips to the sell page" nil (shop-act g view #\s))
  (check "sell mode" :sell (shop-view-mode view))
  (check-true "sell page lists the pack with sell prices"
              (find-if (lambda (s) (search "1) T Sword*  5 gp" s))
                       (shop-lines g view)))
  (shop-act g view #\1)                 ; sell it again
  (check "shop-act sells" 25 (hero-gold h))
  (check "escape backs out to the pick page" nil
         (shop-act g view #\Escape))
  (check "hero deselected" nil (shop-view-hero view))
  (check "escape from the pick page leaves" :left
         (shop-act g view #\Escape))
  (check "location closed by the model" nil (game-location g)))

;; Kinds the engine has no mechanics for still enter and leave cleanly.
(let* ((g (new-game (parse-map *art* :name "test")))
       (view (make-shop-view)))
  (enter-location g '("Empty Hut" :hut))
  (check-true "unknown kind gets the plain notice"
              (find-if (lambda (s) (search "nothing to do" s))
                       (location-lines g view)))
  (check "escape leaves the unknown kind" :left
         (location-act g view #\Escape))
  (check "unknown kind left" nil (game-location g)))

;;; ---------------------------------------------------------------------
;;; Save games: the whole world round-trips — every visited zone's
;;; knowledge, the party's packs and equipment.

(let* ((m (load-map-file "tests/tmp-town.map"))
       (h (%combat-hero))
       (g (new-game m :party (list h))))
  (give-item g h 't-sword)
  (give-item g h 't-torch)
  (equip-item g h 't-sword)
  (travel-party g "tmp-dung.map")       ; explore the dungeon start
  (travel-party g "tmp-town.map" 0 0 :north)
  (save-game g "tests/tmp-save.lisp")
  (let ((g2 (load-game "tests/tmp-save.lisp")))
    (check "world load restores the current zone" "Testville"
           (map-title (game-map g2)))
    (check-true "world town knowledge restored"
                (cell-explored-p (game-knowledge g2) 0 0))
    (let ((h2 (first (game-party g2))))
      (check "world pack restored" '(t-sword t-torch) (hero-items h2))
      (check "world equipment restored" '(t-sword) (hero-equipped h2))
      (check "world attack dice from restored gear" "1d6+2"
             (hero-attack-dice h2)))
    (check-true "unvisited zone knowledge kept pending"
                (assoc "tests/tmp-dung.map" (game-zone-knowledge g2)
                       :test #'equal))
    ;; traveling back restores the pending knowledge
    (travel-party g2 "tmp-dung.map")
    (check-true "world dungeon knowledge restored on travel"
                (cell-explored-p (game-knowledge g2) 0 0))
    (check "pending knowledge consumed" nil
           (assoc "tests/tmp-dung.map" (game-zone-knowledge g2)
                  :test #'equal))))
(delete-file "tests/tmp-save.lisp")

;; Old save versions are rejected with a clear error.
(with-open-file (s "tests/tmp-save.lisp" :direction :output
                   :if-exists :supersede)
  (write-string "(:lambda-tale-save 1 :map-file \"tests/tmp-town.map\")" s))
(check-error "v1 saves are rejected" (load-game "tests/tmp-save.lisp"))
(with-open-file (s "tests/tmp-save.lisp" :direction :output
                   :if-exists :supersede)
  (write-string "(:lambda-tale-save 2 :map-file \"tests/tmp-town.map\" :x 0 :y 0 :facing 0)" s))
(check-error "v2 saves are rejected" (load-game "tests/tmp-save.lisp"))
(delete-file "tests/tmp-save.lisp")

(delete-file "tests/tmp-town.map")
(delete-file "tests/tmp-dung.map")

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
;;; Wall-art assets (M3): the checked-in tile packs — one per display
;;; profile — must match what the generator draws today, pixel for
;;; pixel, so art and code can never drift apart.  (Regenerate with
;;; `make assets` after changing tools/gen-walls.lisp.)

(load "tools/gen-walls.lisp")

(check "wall-piece-file name" "side-door-2-l.iff"
       (wall-piece-file '(:side-door 2 :l)))

;; one checked-in pack per display profile, each pinned to the
;; generator at that profile's viewport
(dolist (profile *display-profiles*)
  (with-display-profile (profile)
    (let ((planes (view-planes *fp-view-width* *fp-view-height*))
          (dir *gfx-dir*)
          (stale '()))
      (dolist (piece (wall-piece-names))
        (let ((file (concatenate 'string dir (wall-piece-file piece))))
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
      (check (format nil "all 40 ~A assets exist and match the generator"
                     dir)
             nil stale)
      ;; the demo ceiling/floor backdrops are pinned the same way
      (let ((stale '()))
        (loop for key in '(:ceiling :floor)
              for name in '("ceiling.iff" "floor.iff")
              for rect in (backdrop-rects planes)
              do (let ((file (concatenate 'string dir name)))
                   (if (not (probe-file file))
                       (push (list key :missing) stale)
                       (let ((disk (read-ilbm file))
                             (drawn (draw-backdrop-piece key planes)))
                         (unless (and (= (image-width disk) (third rect))
                                      (= (image-height disk)
                                         (fourth rect)))
                           (push (list key :wrong-size) stale))
                         (unless (equalp (image-pixels disk)
                                         (image-pixels drawn))
                           (push (list key :differs) stale))))))
        (check (format nil "the ~A backdrop assets match the generator"
                       dir)
               nil stale))
      ;; the reader restores the dungeon palette from the CMAP
      (check (format nil "~A palette carries the dungeon colors" dir)
             (coerce *wall-palette* 'list)
             (coerce (image-palette
                      (read-ilbm (concatenate 'string dir
                                              (wall-piece-file
                                               '(:front 0)))))
                     'list)))))

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
                  :width (display-profile-win-width *display-profile*)
                  :height (display-profile-win-height *display-profile*)
                  :idcmp amiga.intuition:+idcmp-closewindow+)
           (let* ((rp (amiga.intuition:window-rastport win))
                  (l (%amiga-layout win rp)))
             (%amiga-draw-fp rp g (ui-layout-bx l) (ui-layout-by l)
                             (ui-layout-fp-w l) (ui-layout-fp-h l))
             (%amiga-draw-band rp g l)
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
                  :width (display-profile-win-width *display-profile*)
                  :height (display-profile-win-height *display-profile*)
                  :idcmp amiga.intuition:+idcmp-closewindow+)
           (let* ((rp (amiga.intuition:window-rastport win))
                  (l (%amiga-layout win rp)))
             (%amiga-draw-band rp g l)
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
                      :width (display-profile-win-width *display-profile*)
                  :height (display-profile-win-height *display-profile*)
                      :idcmp amiga.intuition:+idcmp-closewindow+)
               (amiga.gadtools:with-menus (menu *menu-entries* vi win)
                 (let* ((rp (amiga.intuition:window-rastport win))
                        (l (%amiga-layout win rp)))
                   (%amiga-party rp g l)
                   ;; the numbered roster's character-sheet page draws too
                   (%amiga-draw-sheet rp g 0 l)
                   t)))))))

;; The location (shop) page draws over the play view — enter a shop
;; location for real and render its menu pages.
#+amigaos
(let* ((m (parse-map *art* :name "test"))
       (g (new-game m :party (with-rng () (list (make-hero "A" :tester)))))
       (log (attach-message-log g))
       (view (make-shop-view)))
  (enter-location g '("Smoke Shoppe" :shop :stock (t-sword t-torch)))
  (check "amiga-ui draws the location page" t
         (amiga.intuition:with-window
             (win :title "Lambda's Tale Test"
                  :left 0 :top 0
                  :width (display-profile-win-width *display-profile*)
                  :height (display-profile-win-height *display-profile*)
                  :idcmp amiga.intuition:+idcmp-closewindow+)
           (let* ((rp (amiga.intuition:window-rastport win))
                  (l (%amiga-layout win rp)))
             (%amiga-draw-page rp (location-lines g view) l)   ; pick-hero page
             (shop-act g view #\1)
             (%amiga-draw-page rp (location-lines g view) l)   ; buy page
             (shop-act g view #\s)
             (%amiga-draw-page rp (location-lines g view) l)   ; sell page
             (%amiga-draw-log rp log l)
             (%amiga-status rp g l "Shop smoke test")
             t)))
  (leave-location g))

;; Custom screen support: open an own screen (RTG-aware mode pick),
;; set the palette, draw into a backdrop window, close it all again.
#+amigaos
(let* ((m (parse-map *art* :name "test"))
       (g (new-game m))
       (log (attach-message-log g)))
  (say g "Custom screen smoke test.")
  (check "amiga-ui draws on an own custom screen" t
         (amiga.intuition:with-screen
             (scr :width (display-profile-screen-width *display-profile*)
                  :height (display-profile-screen-height *display-profile*)
                  :depth (display-profile-screen-depth *display-profile*)
                  :title "Lambda's Tale Test"
                  :mode-id (amiga.gfx:best-mode-id
                            :width (display-profile-screen-width
                                    *display-profile*)
                            :height (display-profile-screen-height
                                     *display-profile*)
                            :depth (display-profile-screen-depth
                                    *display-profile*)))
           (%game-screen-palette scr)
           (check "custom screen reports its width"
                  (display-profile-screen-width *display-profile*)
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
               (%amiga-draw-band rp g l)
               (%amiga-draw-log rp log l)
               (%amiga-status rp g l "Screen smoke test")
               (%amiga-party rp g l)
               t)))))

;; Wall-piece assets (M3): each profile's pack ILBMs load into
;; offscreen bitmaps and the first-person view composits them with
;; real OS blits.  Runs on the game's own custom screen — the
;; profile's nominal PAL geometry guarantees the layout keeps the full
;; asset-size viewport (the FS-UAE Workbench can be shorter than 256
;; lines, where the view correctly falls back to the wireframe).
;; Read-back probes, dead end at (0,0) facing north: the front wall
;; piece's top row is the white edge highlight; above it the ceiling
;; backdrop shows a speckle ((7x+13y) mod 41 = 0 -> grey); below it
;; the floor backdrop shows flagstone fill between the joints.
#+amigaos
(dolist (spec '((:lores (80 22) (43 21) (70 96))
                (:hires (100 26) (100 25) (90 110))))
 (destructuring-bind (pname front-xy ceiling-xy floor-xy) spec
  (with-display-profile (pname)
   (let* ((m (parse-map *art* :name "test"))
          (g (new-game m)))
    (%with-game-font
     (lambda (font)
      (amiga.intuition:with-screen
          (scr :width (display-profile-screen-width *display-profile*)
               :height (display-profile-screen-height *display-profile*)
               :depth (display-profile-screen-depth *display-profile*)
               :title "Walls Test"
               :mode-id (amiga.gfx:best-mode-id
                         :width (display-profile-screen-width
                                 *display-profile*)
                         :height (display-profile-screen-height
                                  *display-profile*)
                         :depth (display-profile-screen-depth
                                 *display-profile*)))
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
          (check (format nil "~A: backdrop window has no top border" pname)
                 0 (amiga.intuition:window-border-top win))
          (let* ((rp (%game-rastport win font))
                 (l (%amiga-layout win rp)))
           (multiple-value-bind (walls pack-palette)
               (%load-wall-assets rp nil)
            (check (format nil "~A: game font gives the designed ~
line height" pname)
                   10 (ui-layout-lh l))
            (check-true (format nil "~A: wall assets load into bitmaps"
                                pname)
                        walls)
            (when walls
              (check (format nil "~A: every pack piece got a bitmap ~
(walls + backdrops)" pname)
                     (+ 2 (length (wall-piece-names)))
                     (hash-table-count walls))
              (check-true (format nil "~A: the pack palette is the ~
demo CMAP" pname)
                          (equalp pack-palette *wall-palette*))
              ;; transparency wiring: receding side pieces carry a
              ;; cookie-cut mask; opaque front pieces and backdrops don't
              (check-true (format nil "~A: a side piece got a ~
cookie-cut mask" pname)
                          (cdr (gethash '(:side 0 :l) walls)))
              (check-true (format nil "~A: a front piece is an opaque ~
blit (no mask)" pname)
                          (not (cdr (gethash '(:front 0) walls))))
              (check-true (format nil "~A: the ceiling backdrop is ~
opaque (no mask)" pname)
                          (not (cdr (gethash '(:ceiling) walls))))
              (check-true (format nil "~A: custom screen leaves the ~
full asset-size viewport" pname)
                          (= (ui-layout-fp-h l) *fp-view-height*))
              (%amiga-draw-fp rp g (ui-layout-bx l) (ui-layout-by l)
                              (ui-layout-fp-w l) (ui-layout-fp-h l)
                              walls)
              (destructuring-bind (fx fy) front-xy
                (check (format nil "~A: blitted front wall edge pixel"
                               pname)
                       1
                       (amiga.gfx:read-pixel rp (+ (ui-layout-bx l) fx)
                                             (+ (ui-layout-by l) fy))))
              (destructuring-bind (cx cy) ceiling-xy
                (check (format nil "~A: blitted ceiling speckle pixel"
                               pname)
                       2
                       (amiga.gfx:read-pixel rp (+ (ui-layout-bx l) cx)
                                             (+ (ui-layout-by l) cy))))
              (destructuring-bind (sx sy) floor-xy
                (check (format nil "~A: blitted floor stone pixel"
                               pname)
                       2
                       (amiga.gfx:read-pixel rp (+ (ui-layout-bx l) sx)
                                             (+ (ui-layout-by l) sy)))))
            (%free-wall-assets walls)))))))))))

;; *autoplay* drives a full unattended PLAY-AMIGA session: scripted keys
;; are fed one per INTUITICK (~10/s), ending in #\q so the event loop
;; exits on its own.  Verifies the whole real event path — window, menu
;; strip, redraws, key dispatch — with no user at the keyboard.  The
;; script also enters map mode (m), toggles the debug full view (f)
;; twice and leaves map mode (m) before quitting.
;; The scripted keys also open a character sheet (1), switch to another
;; roster slot (2) and leave it (:esc) — exercising the whole :sheet
;; mode through the real event loop.  The cellar is a :DARK zone, so
;; the whole session renders at the one-cell darkness view depth (the
;; :screen sessions below exercise the blit path's wall of night).
#+amigaos
(check "amiga-ui autoplay plays a scripted session and quits" :done
       (let ((*autoplay* (list #\w #\d #\1 #\2 :esc #\w #\a
                               #\m #\f #\f #\m #\s #\q)))
         ;; :window is the development view — :screen (the default)
         ;; gets its own autoplay below
         (play-amiga "worlds/closure/cellar.map" :display :window)
         :done))

;; The town: an unattended session first casts through the real event
;; loop — open the cast menu (c), pick Zzgo the conjurer (4), cast
;; mage flame (1) — then walks from the gate to Wolfgar's shoppe (a
;; LOCATION special), shops for real — pick a hero (1), buy a torch
;; (1), flip to the sell page (s), sell it again (1), back out
;; (Esc Esc) — steps back into the street, walks east to the tavern
;; and drops through the trapdoor into the cellar: the town's
;; (ZONE :GFX ...) city pack swaps for the cellar's default pack on
;; the way (the wall-bitmap reload path), and the cellar is a :DARK
;; zone — Zzgo's flame is what keeps the view lit — then quits.
#+amigaos
(check "amiga-ui autoplay casts, shops and drops to the dark cellar"
       :done
       (let ((*autoplay* (list #\c #\4 #\1
                               #\w #\a #\w #\w #\d #\w #\w #\w
                               #\1 #\1 #\s #\1 :esc :esc
                               #\s #\d #\w #\w #\w #\w #\a #\w #\q)))
         (play-amiga "worlds/closure/town.map" :display :window)
         :done))

;; The same unattended session on an own custom screen (:display :screen)
;; — the whole open-screen/backdrop-window/menus/event-loop path, with
;; the tile pack named explicitly (:gfx-dir): the lores depth-5 screen,
;; the pack palette and the ceiling/floor backdrop all draw for real.
#+amigaos
(check "amiga-ui autoplay on an own custom screen" :done
       (let ((*autoplay* (list #\w #\d #\m #\m #\q)))
         (play-amiga "worlds/closure/cellar.map" :display :screen
                                       :gfx-dir "data/gfx/")
         :done))

;; The hires profile end to end: its 640x256 depth-4 screen, the
;; 240x130 viewport and the data/gfx-hires pack, through the same
;; scripted event loop.
#+amigaos
(check "amiga-ui autoplay on the hires profile" :done
       (let ((*autoplay* (list #\w #\d #\m #\m #\q)))
         (play-amiga "worlds/closure/cellar.map" :display :screen
                                       :profile :hires)
         :done))

;;; ---------------------------------------------------------------------
;;; Summary

(format t "~%Lambda's Tale tests: ~D checks, ~D failures.~%" *checks* *failures*)
(cl-user::quit (if (zerop *failures*) 0 1))
