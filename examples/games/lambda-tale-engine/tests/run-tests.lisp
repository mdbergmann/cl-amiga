;;; Lambda's Tale engine — test suite: map model, movement, knowledge,
;;; renderers (M0/M1); dice, events, specials, party, combat, save
;;; games (M2).  Story content lives in games (e.g. the Closure game
;;; next door, which has its own suite); the world these tests play is
;;; the minimal fixture under tests/world/.
;;; Run from the engine root (examples/games/lambda-tale-engine):  make test

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

;; the same slots at the lores profile's 120x112 viewport (2/5 of the
;; 320px screen's content span goes to the view, 3/5 to the log)
(let ((planes (view-planes 120 112)))
  (check "lores view-planes plane 1" '(24 22 95 89) (aref planes 1))
  (check "lores front slot at depth 0" '(24 22 72 68)
         (wall-piece-rect planes '(:front 0)))
  (check "lores left side slot spans the full column" '(0 0 25 112)
         (wall-piece-rect planes '(:side 0 :l)))
  (check "lores left flank slot is the side band at wall height"
         '(0 22 25 68)
         (wall-piece-rect planes '(:flank 0 :l)))
  (check "lores piece slots lie inside the viewport" nil
         (remove-if (lambda (piece)
                      (destructuring-bind (x y w h)
                          (wall-piece-rect planes piece)
                        (and (<= 0 x) (<= 0 y) (< 0 w) (< 0 h)
                             (<= (+ x w) 120) (<= (+ y h) 112))))
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

(destructuring-bind (ceiling floor) (backdrop-rects (view-planes 120 112))
  (check "lores ceiling backdrop slot" '(0 0 120 56) ceiling)
  (check "lores floor backdrop slot" '(0 56 120 56) floor))

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

;; FIT-TITLE: the plaque under the view clips a zone title wider than
;; the (profile-tunable, since 2026-07-19 narrower) view column instead
;; of overrunning into the log — regression for the 160->120 lores
;; shrink.  Measured with a topaz-8-like 8px/char ruler at the lores
;; plaque width.
(let ((px8 (lambda (s) (* 8 (length s)))))
  (check "fit-title passes a fitting name through unchanged"
         "The Cellar" (fit-title "The Cellar" px8 118))
  (check "fit-title drops trailing characters until the name fits"
         "A Very Long Lo"
         (fit-title "A Very Long Location Name That Overflows" px8 118))
  (check-true "fit-title results always fit the given width"
              (<= (funcall px8 (fit-title
                                "A Very Long Location Name That Overflows"
                                px8 118))
                  118))
  (check "fit-title never shrinks a name below one character"
         "W" (fit-title "W" px8 4)))

(check "gfx-dir defaults to the engine's lores pack"
       (engine-path "data/gfx/") *gfx-dir*)

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

;; A width whose row padding runs past the last pixel group (17 px: 3
;; groups of pixels, but 4 bytes per word-aligned row) must leave the
;; padding byte clear — the mask is blitted at full row width.
(multiple-value-bind (m bpr)
    (mask-bytes 17 1 (let ((p (make-array 17 :element-type '(unsigned-byte 8)
                                          :initial-element 0)))
                       (setf (aref p 16) 1) ; the lone pixel of group 2
                       p))
  (check "padded mask row is word-aligned" 4 bpr)
  (check "mask marks the last pixel before the padding" #x80 (aref m 2))
  (check "mask leaves the row padding clear" 0 (aref m 3)))

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
;;; Sample map loads (the committed fixture world, tests/world/)

(let ((m (load-map-file "tests/world/keep.map")))
  (check "keep width" 5 (dungeon-map-width m))
  (check "keep height" 1 (dungeon-map-height m))
  (check "keep stairs down" #\> (cell-feature m 4 0)))
(let ((m (load-map-file "tests/world/crypt.map")))
  (check "crypt width" 3 (dungeon-map-width m))
  (check "crypt height" 1 (dungeon-map-height m))
  (check "crypt ladder up" #\< (cell-feature m 2 0)))

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

;; Structured menu lines: an option line carries the key that picks it
;; (MENU-OPTION / MENU-NUMBERED), plain lines stay strings, and the
;; footer hints' bracket tokens locate as clickable spans
;; (MENU-KEY-SPANS) — the pointing front-end turns clicks on either
;; into key presses.
(check "menu-option pairs text with its key" '("3) row" . #\3)
       (menu-option #\3 "3) row"))
(check "menu-line-text unwraps an option line" "3) row"
       (menu-line-text (menu-option #\3 "3) row")))
(check "menu-line-text passes a plain line through" "plain"
       (menu-line-text "plain"))
(check "menu-line-key reads the option key" #\7
       (menu-line-key (menu-option #\7 "x")))
(check "menu-line-key of a plain line is NIL" nil (menu-line-key "x"))
(check "menu-numbered attaches the digit" '("2) b" . #\2)
       (menu-numbered 2 "2) b"))
(check "menu-numbered past nine stays plain" "10) c"
       (menu-numbered 10 "10) c"))
(check "menu-texts strips a mixed list" '("a" "1) b" "c")
       (menu-texts (list "a" (menu-numbered 1 "1) b") "c")))
(check "wrap-menu-line carries the key onto every row"
       '(("1) one two" . #\1) ("three" . #\1))
       (wrap-menu-line (menu-option #\1 "1) one two three") 10))
(check "wrap-menu-line leaves plain rows plain"
       '("one two" "three") (wrap-menu-line "one two three" 7))
;; menu-key-spans: the "[s] sell  [Esc] back" footer convention
(check "spans: single-char and Esc tokens, ranges skipped"
       '((11 19 #\s) (21 31 #\Escape))
       (menu-key-spans "[1-9] buy  [s] sell  [Esc] back"))
(check "spans: a span runs over its words to the line's end"
       '((0 21 #\d))
       (menu-key-spans "[d] down the trapdoor"))
(check "spans: Return commits, Esc cancels"
       '((0 13 #\Return) (15 27 #\Escape))
       (menu-key-spans "[Return] save  [Esc] cancel"))
(check "spans: no brackets, no spans" '() (menu-key-spans "plain text"))
(check "spans: an unmatched bracket yields nothing" '()
       (menu-key-spans "an [unclosed token"))
(check "spans: [n] new name parses" '((17 29 #\n))
       (menu-key-spans "[1-9] overwrite  [n] new name"))

;; Menu scrolling: a list longer than +MENU-PAGE-SIZE+ (7) windows to
;; 5 rows plus the more-above/more-below markers; the same MENU-WINDOW
;; math drives the renderers and the digit picks, so they cannot
;; disagree.
(multiple-value-bind (start end above below) (menu-window 7 0)
  (check "a page-sized list shows whole" '(0 7) (list start end))
  (check "a page-sized list has no markers" '(nil nil)
         (list above below)))
(multiple-value-bind (start end above below) (menu-window 3 9)
  (check "a short list ignores the offset" '(0 3) (list start end))
  (check "a short list has no markers" '(nil nil) (list above below)))
(multiple-value-bind (start end above below) (menu-window 12 0)
  (check "a deep list windows to page - 2 rows" '(0 5) (list start end))
  (check "only the below marker at the top" '(nil t)
         (list above (and below t))))
(multiple-value-bind (start end above below) (menu-window 12 3)
  (check "mid-list window" '(3 8) (list start end))
  (check "both markers mid-list" '(t t)
         (list (and above t) (and below t))))
(multiple-value-bind (start end above below) (menu-window 12 99)
  (check "the offset clamps to the tail" '(7 12) (list start end))
  (check "only the above marker at the bottom" '(t nil)
         (list (and above t) below)))
(multiple-value-bind (start end) (menu-window 12 -4)
  (check "a negative offset clamps to the head" '(0 5) (list start end)))
(multiple-value-bind (start end) (menu-window 10 0 8)
  (check "the page size is a parameter" '(0 6) (list start end)))
;; digits pick within the visible window
(let ((items '(a b c d e f g h i j k l)))
  (check "digit 1 picks the window's first row" 'f
         (menu-window-pick items 5 1))
  (check "digit 5 picks the window's last row" 'j
         (menu-window-pick items 5 5))
  (check "a digit past the window picks nothing" nil
         (menu-window-pick items 5 6))
  (check "the pick returns the absolute index" 7
         (nth-value 1 (menu-window-pick items 5 3)))
  (check "an unscrolled deep list picks from the head" 'e
         (menu-window-pick items 0 5)))
(let ((items '(a b)))
  (check "a short list picks directly" 'b (menu-window-pick items 0 2))
  (check "a digit past a short list picks nothing" nil
         (menu-window-pick items 0 3)))
;; u/d move the window; other keys and short lists say NIL
(check "d scrolls a window down" 5 (menu-scroll 0 #\d 12))
(check "d clamps at the tail" 7 (menu-scroll 5 #\d 12))
(check "d at the tail stays" 7 (menu-scroll 7 #\d 12))
(check "u scrolls a window up" 2 (menu-scroll 7 #\u 12))
(check "u clamps at the head" 0 (menu-scroll 2 #\u 12))
(check "U is u" 0 (menu-scroll 0 #\U 12))
(check "a non-scroll key is not a scroll" nil (menu-scroll 0 #\x 12))
(check "a non-character is not a scroll" nil (menu-scroll 0 :esc 12))
(check "a short list never scrolls" nil (menu-scroll 0 #\d 7))
;; the rendered window: marker rows carry the scroll keys
(let ((items '("A" "B" "C" "D" "E" "F" "G" "H" "I")))
  (let ((lines (menu-scrolled-lines
                items 0
                (lambda (i x) (menu-numbered i (format nil "~D) ~A" i x))))))
    (check "top window: rows then the below marker"
           '("1) A" "2) B" "3) C" "4) D" "5) E" "v more below [d]")
           (menu-texts lines))
    (check "the below marker carries the scroll key" #\d
           (menu-line-key (first (last lines))))
    (check "option rows renumber within the window" #\1
           (menu-line-key (first lines))))
  (let ((lines (menu-scrolled-lines
                items 4
                (lambda (i x) (menu-numbered i (format nil "~D) ~A" i x))))))
    (check "tail window: the above marker then the tail rows"
           '("^ more above [u]" "1) E" "2) F" "3) G" "4) H" "5) I")
           (menu-texts lines))
    (check "the above marker carries the scroll key" #\u
           (menu-line-key (first lines)))))
(check "a short list renders without markers" '("1) A" "2) B")
       (menu-texts (menu-scrolled-lines
                    '("A" "B") 0
                    (lambda (i x)
                      (menu-numbered i (format nil "~D) ~A" i x))))))

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
         (getf (effect-payload (find-effect g "lamp")) :ac))
  ;; icon images and the :compass payload
  (check "effects carry no image by default" nil
         (effect-image (find-effect g "blessing")))
  (check "without a :compass payload the party is lost" nil
         (compass-active-p g))
  (add-effect g "wayfinder" :duration 20 :payload '(:compass t)
                            :image "fx-rose.iff")
  (check "add-effect stores the icon image" "fx-rose.iff"
         (effect-image (find-effect g "wayfinder")))
  (check-true "a :compass payload orients the party" (compass-active-p g))
  (add-effect g "wayfinder" :duration 20 :payload '(:compass t)
                            :image "fx-rose2.iff")
  (check "re-adding refreshes the image" "fx-rose2.iff"
         (effect-image (find-effect g "wayfinder")))
  (remove-effect g "wayfinder")
  (check "a removed compass leaves the party lost again" nil
         (compass-active-p g)))

;; apply-effect-spec: the timed-effect vocabulary spells and usable
;; items speak, funneled into one applier.
(let* ((m (parse-map *art* :name "test"))
       (g (new-game m)))
  (apply-effect-spec g "wolf skin" '(:buff-ac 2 :duration 30))
  (check "a :buff-ac spec becomes an :ac effect" 2 (effects-ac-bonus g))
  (check "the spec's duration sets the expiry" (+ (game-time g) 30)
         (effect-expires-at (find-effect g "wolf skin")))
  (apply-effect-spec g "lantern" '(:light t :duration 10)
                     :image "fx-light.iff")
  (check-true "a :light spec lights the party" (light-active-p g))
  (check "the applier stores the image" "fx-light.iff"
         (effect-image (find-effect g "lantern")))
  (apply-effect-spec g "wayfinder" '(:compass t :duration 10))
  (check-true "a :compass spec orients the party" (compass-active-p g))
  (check-error "a spec naming no timed effect is rejected"
    (apply-effect-spec g "bogus" '(:frobnicate t :duration 5))))

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
    (check "a plain :dark t zone sees one cell" 1 (game-view-depth g))
    (add-effect g "mage flame" :payload '(:light t))
    (check-true "light works underground too" (not (game-dark-p g))))
  (delete-file path))

;; (zone :dark N) — dark at any hour, but with N cells of sight (the
;; Closure cellar plays with 3): a light effect still buys the full
;; view depth, and N is capped at +VIEW-DEPTH+.
(let* ((m (parse-map "+-+-+-+-+-+-+
|@          |
+-+-+-+-+-+-+"
                     :name "dim-test" :start-facing :east))
       (g (new-game m)))
  (setf (dungeon-map-dark m) 3)         ; as (zone :dark 3) stores it
  (setf (game-time g) 720)              ; noon — still dark underground
  (check-true "a :dark 3 zone is dark" (game-dark-p g))
  (check ":dark 3 sees three cells" 3 (game-view-depth g))
  (check ":dark 3 truncates compute-view to three" 3
         (length (compute-view (game-map g) (game-x g) (game-y g)
                               (game-facing g) (game-view-depth g))))
  (add-effect g "torchlight" :payload '(:light t))
  (check "a light buys the full view depth" +view-depth+
         (game-view-depth g))
  (remove-effect g "torchlight")
  (setf (dungeon-map-dark m) 99)
  (check ":dark above +view-depth+ is capped" +view-depth+
         (game-view-depth g)))

;; The :dark integer round-trips through the map file, and a bad value
;; is rejected with a message naming the map.
(let ((path "tests/tmp-dim.map"))
  (with-open-file (s path :direction :output :if-exists :supersede)
    (write-string "+-+-+
|@  |
+-+-+
(zone :kind :dungeon :title \"the cellar\" :dark 3)
" s))
  (check "zone :dark 3 parses as the integer" 3
         (dungeon-map-dark (load-map-file path)))
  (with-open-file (s path :direction :output :if-exists :supersede)
    (write-string "+-+-+
|@  |
+-+-+
(zone :dark :pitch-black)
" s))
  (check-error "zone :dark rejects a non-integer non-T value"
    (load-map-file path))
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

;; The character-sheet page (HERO-SHEET-LINES): header, the summary
;; block, the key hints — the front-ends (the Amiga message-area
;; takeover, the host :sheet mode) draw these verbatim.
(let* ((m (parse-map *art* :name "test"))
       (g (new-game m :party (with-rng ()
                               (list (make-hero "A" :tester)
                                     (make-hero "B" :tester)))))
       (lines (hero-sheet-lines g 1)))
  (check "sheet page header counts the roster" "*** Character 2 of 2 ***"
         (first lines))
  (check "sheet page embeds the summary block" "B the Tester"
         (third lines))
  (check "sheet page ends with the key hints"
         "[1-7] view another  [e] equip  [Esc] back"
         (first (last lines))))

;; Class portraits: DEFINE-HERO-CLASS :IMAGE resolves map-relative
;; (the effect-icon rule); a class without one has no portrait.
(define-hero-class :t-faced :image "gfx/face.iff")
(let* ((m (parse-map *art* :name "world/deep/test"))
       (g (new-game m :party (with-rng ()
                               (list (make-hero "F" :t-faced)
                                     (make-hero "A" :tester))))))
  (check "the portrait file is class data" "gfx/face.iff"
         (hero-image (first (game-party g))))
  (check "the portrait resolves beside the map" "world/deep/gfx/face.iff"
         (hero-image-path g (first (game-party g))))
  (check "a class without :image has no portrait" nil
         (hero-image-path g (second (game-party g)))))

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
(define-spell 'test-needle :cost 1 :level 1 :classes '(:t-mage)
  :compass t :duration 45 :image "fx-needle.iff")

(check "spell-title downcases the name" "test bolt" (spell-title 'test-bolt))
(check-error "unknown spell rejected" (find-spell-type 'test-nonesuch))
(check-error "define-spell needs exactly one effect"
  (define-spell 'test-bogus :damage "1d4" :heal "1d4"))
(check-error "define-spell needs an effect"
  (define-spell 'test-bogus :cost 1))
(check-error "a timed spell needs a duration"
  (define-spell 'test-bogus :buff-ac 1))
(check-error "a compass spell needs a duration too"
  (define-spell 'test-bogus :compass t))
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
         '(test-bolt test-mend test-shield test-flame test-lore test-needle)
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

;; A compass spell: the party knows its facing only while it burns.
(let* ((m (parse-map *art* :name "test"))
       (mage (%combat-mage))
       (g (new-game m :party (list mage))))
  (check "the party starts without a compass" nil (compass-active-p g))
  (check-true "needle casts" (cast-spell g mage 'test-needle))
  (check-true "the party knows its facing" (compass-active-p g))
  (check "the effect carries the spell's icon" "fx-needle.iff"
         (effect-image (find-effect g "test needle")))
  (check "the compass is a timed effect" (+ (game-time g) 45)
         (effect-expires-at (find-effect g "test needle")))
  (advance-time g 45)
  (check "an expired compass leaves the party lost" nil
         (compass-active-p g)))

;;; ---------------------------------------------------------------------
;;; Bard songs: singers and tunes, one song at a time, the tavern

(define-hero-class :t-bard :hp-dice "1d8" :damage "1d4" :ac 9 :singer t)
(define-song 'test-march :buff-ac 2 :duration 20)
(define-song 'test-gleam :light t :duration 20 :image "fx-gleam.iff")
(define-song 'test-dirge :level 3 :compass t :duration 20)

(check "song-title downcases the name" "test march" (song-title 'test-march))
(check-error "unknown song rejected" (find-song-type 'test-nonesuch))
(check-error "define-song needs exactly one effect"
  (define-song 'test-bogus :buff-ac 1 :light t :duration 5))
(check-error "define-song needs an effect"
  (define-song 'test-bogus :duration 5))
(check-error "a song needs a duration"
  (define-song 'test-bogus :light t))
(check-error "song duration must be a positive integer"
  (define-song 'test-bogus :light t :duration -5))

;; Singers and tunes: one charge per level, rested singers start full.
(let ((bard (with-rng () (make-hero "Mel" :t-bard)))
      (grunt (%combat-hero)))
  (check-true "the bard is a singer" (hero-singer-p bard))
  (check "a fresh bard holds one tune" 1 (hero-tunes bard))
  (check "max tunes follow the level" 1 (hero-max-tunes bard))
  (setf (hero-level bard) 3)
  (check "max tunes grow with the level" 3 (hero-max-tunes bard))
  (setf (hero-level bard) 1)
  (check-true "the grunt is no singer" (not (hero-singer-p grunt)))
  (check "non-singers hold no tunes" 0 (hero-max-tunes grunt))
  (check-true "the bard knows the level-1 songs"
              (song-known-p bard 'test-march))
  (check-true "the level gate holds" (not (song-known-p bard 'test-dirge)))
  (check-true "non-singers know nothing" (not (song-known-p grunt 'test-march)))
  (check "known songs in registration order" '(test-march test-gleam)
         (songs-for-hero bard))
  (check "the singer's sheet shows the tunes" "HP 1/1  Tunes 1/1  AC 9"
         (third (hero-summary-lines bard))))

;; SING-SONG: refusals say why; a song is a timed :SONG-marked effect
;; and a new song displaces the old (one tune at a time).
(let* ((m (parse-map *art* :name "test"))
       (bard (with-rng () (make-hero "Mel" :t-bard)))
       (grunt (%combat-hero))
       (g (new-game m :party (list grunt bard)))
       (msgs (watch-messages g))
       (sung '()))
  (on-event g :song-sung
            (lambda (game hero name) (declare (ignore game hero))
              (push name sung)))
  (check "a non-singer cannot sing" nil (sing-song g grunt 'test-march))
  (check-true "does-not-know message"
              (find-if (lambda (s) (search "does not know" s))
                       (funcall msgs)))
  (check "no song plays yet" nil (current-song g))
  (check-true "the bard strikes up the march"
              (sing-song g bard 'test-march))
  (check "the tune was spent" 0 (hero-tunes bard))
  (check "the march is the current song" "test march"
         (effect-name (current-song g)))
  (check-true "the song effect carries the :song marker"
              (getf (effect-payload (current-song g)) :song))
  (check "the march shields the party" 2 (effects-ac-bonus g))
  (check "the march is timed" (+ (game-time g) 20)
         (effect-expires-at (current-song g)))
  (check ":song-sung emitted" '(test-march) sung)
  ;; out of tunes: the tavern beckons
  (check "no tunes, no song" nil (sing-song g bard 'test-gleam))
  (check-true "no-tunes message names the tavern"
              (find-if (lambda (s) (search "tavern" s)) (funcall msgs)))
  (check-true "the march still plays" (current-song g))
  ;; a new song displaces the old
  (setf (hero-tunes bard) 1)
  (check-true "the bard strikes up the gleam"
              (sing-song g bard 'test-gleam))
  (check "the gleam displaced the march" "test gleam"
         (effect-name (current-song g)))
  (check "only one song plays" 1
         (count-if (lambda (e) (getf (effect-payload e) :song))
                   (game-effects g)))
  (check "the march's shield left with it" 0 (effects-ac-bonus g))
  (check-true "the gleam lights the party" (light-active-p g))
  (check "the song carries its icon" "fx-gleam.iff"
         (effect-image (current-song g)))
  ;; songs fade on the clock like any timed effect
  (advance-time g 20)
  (check "the faded song is gone" nil (current-song g))
  (check-true "the fade is announced"
              (find-if (lambda (s) (search "wears off" s))
                       (funcall msgs))))

;; combat-round accepts (:sing SONG) beside :attack and (:cast ...).
(let* ((m (parse-map *art* :name "test"))
       (grunt (%combat-hero))
       (bard (with-rng () (make-hero "Mel" :t-bard)))
       (g (new-game m :party (list grunt bard))))
  (start-combat g '(("test rat" 1)))
  ;; the grunt slays the rat (d20=11 hits, 1d6=3) while the bard sings
  (check "a sung round wins" :victory
         (with-rng (10 2)
           (combat-round g (list :attack '(:sing test-march)))))
  (check "the combat tune was spent" 0 (hero-tunes bard))
  (check-true "the march outlives the fight" (current-song g)))

;; The sing menu: pick the singer, then the song (the CAST-VIEW pattern).
(let* ((m (parse-map *art* :name "test"))
       (grunt (%combat-hero))
       (bard (with-rng () (make-hero "Mel" :t-bard)))
       (g (new-game m :party (list grunt bard))))
  (let ((v (make-sing-view)))
    (check "Esc at the top cancels" :cancelled (sing-act g v #\Escape)))
  (let ((v (make-sing-view)))
    (check-true "the menu opens on the singer pick"
                (member "Who plays?" (sing-lines g v) :test #'equal))
    (sing-act g v #\1)
    (check "a non-singer is not picked" nil (sing-view-hero v))
    (sing-act g v #\2)
    (check "the bard picked" bard (sing-view-hero v))
    (check-true "the song page lists the march"
                (find-if (lambda (s) (search "test march" s))
                         (menu-texts (sing-lines g v))))
    (check "the song row carries its pick key" #\1
           (menu-line-key
            (find-if (lambda (line)
                       (search "test march" (menu-line-text line)))
                     (sing-lines g v))))
    (sing-act g v #\Escape)
    (check "Esc backs out to the singer pick" nil (sing-view-hero v))
    (sing-act g v #\2)
    (check "picking the song resolves the menu" :done (sing-act g v #\1))
    (check "the march plays" "test march"
           (effect-name (current-song g)))))

;; The tavern: drinks refill a singer's tunes; :down holds the way
;; below (the Bard's Tale trapdoor).
(let* ((m (parse-map *art* :name "test"))
       (bard (with-rng () (make-hero "Mel" :t-bard)))
       (grunt (%combat-hero))
       (g (new-game m :party (list grunt bard)))
       (msgs (watch-messages g)))
  (setf (hero-gold grunt) 10
        (hero-gold bard) 2
        (hero-tunes bard) 0)
  (enter-location g '("The Rusty Flagon" :tavern :price 5))
  (check "the price is the location's" 5
         (tavern-price (game-location g)))
  (check-true "the menu shows the price"
              (find-if (lambda (s) (search "5 gold" s))
                       (menu-texts (tavern-lines g))))
  (check-true "no trapdoor line without :down"
              (not (find-if (lambda (s) (search "trapdoor" s))
                            (menu-texts (tavern-lines g)))))
  (check "a drink row carries its hero's digit" #\2
         (menu-line-key
          (find-if (lambda (line)
                     (search "2) " (menu-line-text line)))
                   (tavern-lines g))))
  (check "a poor hero is refused" nil (buy-drink g bard))
  (check-true "cannot-afford message"
              (find-if (lambda (s) (search "cannot afford" s))
                       (funcall msgs)))
  (check "refusals keep the gold" 2 (hero-gold bard))
  (check-true "the grunt drinks" (buy-drink g grunt))
  (check "the ale cost five gold" 5 (hero-gold grunt))
  (check "ale grants no tunes to a non-singer" 0 (hero-tunes grunt))
  (setf (hero-gold bard) 5)
  (check-true "the bard drinks through the menu"
              (progn (tavern-act g #\2) (zerop (hero-gold bard))))
  (check "the tunes came flooding back" 1 (hero-tunes bard))
  (check-true "the refill is announced"
              (find-if (lambda (s) (search "flooding back" s))
                       (funcall msgs)))
  (check "Esc leaves the tavern" :left (tavern-act g #\Escape))
  (check "the tavern is left behind" nil (game-location g)))

;; The default drink price, and the trapdoor down.
(with-open-file (s "tests/tmp-down.map" :direction :output
                   :if-exists :supersede)
  (write-string "+-+
|@|
+-+

(zone :kind :dungeon :title \"the snug\")
" s))
(let* ((m (parse-map *art* :name "test"))
       (g (new-game m :party (list (%combat-hero)))))
  (enter-location g '("Trapdoor Inn" :tavern :down "tests/tmp-down.map"))
  (check "a drink is three gold by default" 3
         (tavern-price (game-location g)))
  (check-true "the trapdoor is offered"
              (find-if (lambda (s) (search "down the trapdoor" s))
                       (menu-texts (tavern-lines g))))
  (check "the trapdoor drops through" :left (tavern-act g #\d))
  (check "the trapdoor landed below" "the snug" (map-title (game-map g)))
  (check "the location is left behind" nil (game-location g)))
(delete-file "tests/tmp-down.map")

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

;; Round structure: every round opens with its number.
(let* ((m (parse-map *art* :name "test"))
       (g (new-game m :party (list (%combat-hero))))
       (msgs (watch-messages g)))
  (start-combat g '(("test rat" 1)))
  (with-rng (0 13) (combat-round g '(:defend)))
  (check-true "round 1 header"
              (find "-- Round 1 --" (funcall msgs) :test #'equal))
  (check "the combat counts its rounds" 1
         (combat-round-no (game-combat g)))
  (with-rng (0 13) (combat-round g '(:defend)))
  (check-true "round 2 header"
              (find "-- Round 2 --" (funcall msgs) :test #'equal)))

;; The round-orders model: every living hero picks in turn, the last
;; pick hands the front-end the round's actions in party order.
(let* ((m (parse-map *art* :name "test"))
       (grunt (%combat-hero))
       (mage (%combat-mage))
       (g (new-game m :party (list grunt mage)))
       (view (make-combat-orders)))
  (start-combat g '(("test rat" 2)))
  (check "orders ask the first hero" grunt (combat-orders-hero g view))
  (check-true "orders page shows the coming round"
              (find-if (lambda (s) (search "Round 1" s))
                       (menu-texts (combat-orders-lines g view))))
  (check-true "orders page lists the enemy group"
              (find-if (lambda (s) (search "2 test rats" s))
                       (menu-texts (combat-orders-lines g view))))
  (check-true "orders page marks the hero at hand"
              (find-if (lambda (s) (and (search "> Alva" s)
                                        (search "?" s)))
                       (menu-texts (combat-orders-lines g view))))
  (check "the first pick advances" nil (combat-orders-act g view #\a))
  (check "orders ask the second hero" mage (combat-orders-hero g view))
  (check-true "a picked action shows on its row"
              (find-if (lambda (s) (and (search "Alva" s)
                                        (search "attack" s)))
                       (menu-texts (combat-orders-lines g view))))
  (check "esc undoes the previous pick" nil
         (combat-orders-act g view #\Escape))
  (check "back to the first hero" grunt (combat-orders-hero g view))
  (combat-orders-act g view #\a)
  (check "the last pick returns the fight"
         '(:fight (:attack :defend))
         (combat-orders-act g view #\d))
  (check "no round ran while picking" 0
         (combat-round-no (game-combat g))))

;; C during orders opens the spell pick for the hero at hand; the pick
;; lands as that hero's round action instead of fighting a round.
(let* ((m (parse-map *art* :name "test"))
       (grunt (%combat-hero))
       (mage (%combat-mage))
       (g (new-game m :party (list grunt mage)))
       (view (make-combat-orders)))
  (start-combat g '(("test rat" 2)))    ; 3 hp each
  (combat-orders-act g view #\a)        ; the grunt attacks
  (check "c opens the mage's spell pick" nil (combat-orders-act g view #\c))
  (check-true "the pick page is the mage's cast menu"
              (find-if (lambda (s) (search "Zzgo casts" s))
                       (menu-texts (combat-orders-lines g view))))
  ;; Esc backs out of the pick to the action keys, hero unchanged
  (check "esc leaves the spell pick" nil
         (combat-orders-act g view #\Escape))
  (check "still asking the mage" mage (combat-orders-hero g view))
  (combat-orders-act g view #\c)
  (let ((r (combat-orders-act g view #\1)))     ; test-bolt, no target
    (check "the spell pick completes the orders"
           '(:fight (:attack (:cast test-bolt))) r)
    (check "picking paid no sp yet" 6 (hero-sp mage))
    (check "picking ran no round" 0 (combat-round-no (game-combat g)))
    ;; the returned actions fight the round the mixed-action way
    (check "the ordered round wins" :victory
           (with-rng (10 2 2) (combat-round g (second r))))
    (check "the ordered cast paid sp" 4 (hero-sp mage))))

;; A heal pick during orders carries its chosen target along.
(let* ((m (parse-map *art* :name "test"))
       (grunt (%combat-hero))
       (mage (%combat-mage))
       (g (new-game m :party (list grunt mage)))
       (view (make-combat-orders)))
  (start-combat g '(("test rat" 1)))
  (combat-orders-act g view #\a)
  (combat-orders-act g view #\c)
  (combat-orders-act g view #\2)        ; test-mend: heal, pick a target
  (check "the heal pick completes with its target"
         (list :fight (list :attack (list :cast 'test-mend grunt)))
         (combat-orders-act g view #\1)))       ; on the grunt

;; P during orders opens the song pick the same way.
(let* ((m (parse-map *art* :name "test"))
       (grunt (%combat-hero))
       (bard (with-rng () (make-hero "Mel" :t-bard)))
       (g (new-game m :party (list grunt bard)))
       (view (make-combat-orders)))
  (start-combat g '(("test rat" 1)))
  (combat-orders-act g view #\a)
  (check "p opens the bard's song pick" nil (combat-orders-act g view #\p))
  (check "the song pick completes the orders"
         '(:fight (:attack (:sing test-march)))
         (combat-orders-act g view #\1))
  (check "picking spent no tune" 1 (hero-tunes bard)))

;; Refusals stay put; F flees party-level from any hero's turn.
(let* ((m (parse-map *art* :name "test"))
       (grunt (%combat-hero))
       (g (new-game m :party (list grunt)))
       (msgs (watch-messages g))
       (view (make-combat-orders)))
  (start-combat g '(("test rat" 1)))
  (check "c on a non-caster stays put" nil (combat-orders-act g view #\c))
  (check-true "and says who cannot cast"
              (find "Alva cannot cast." (funcall msgs) :test #'equal))
  (check "p on a non-singer stays put" nil (combat-orders-act g view #\p))
  (check-true "and says who cannot play"
              (find "Alva cannot play." (funcall msgs) :test #'equal))
  (check "still asking the same hero" grunt (combat-orders-hero g view))
  (check "f flees" :flee (combat-orders-act g view #\f)))

;; Combat transcript speed: +/- during orders, clamped both ways; the
;; front-ends linger COMBAT-MESSAGE-DELAY seconds on each message.
(let* ((m (parse-map *art* :name "test"))
       (g (new-game m :party (list (%combat-hero))))
       (msgs (watch-messages g))
       (view (make-combat-orders))
       (*combat-speed* 3))
  (start-combat g '(("test rat" 1)))
  (check "speed 3 lingers half a second" 0.5 (combat-message-delay))
  (check "+ is no round action" nil (combat-orders-act g view #\+))
  (check "+ raised the speed" 4 *combat-speed*)
  (check-true "and said so"
              (find "Combat speed 4 of 5." (funcall msgs) :test #'equal))
  (combat-orders-act g view #\+)
  (combat-orders-act g view #\+)
  (check "speed caps at the maximum" 5 *combat-speed*)
  (check "the cap is instant" 0.0 (combat-message-delay))
  (dotimes (i 6) (combat-orders-act g view #\-))
  (check "speed floors at 1" 1 *combat-speed*)
  (check "the floor lingers a second" 1.0 (combat-message-delay)))

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
              (let ((lines (menu-texts (cast-lines g view))))
                (and (find-if (lambda (s) (search "2) Zzgo" s)) lines)
                     (not (find-if (lambda (s) (search "Alva" s)) lines)))))
  (check "the caster row's key is its party slot" #\2
         (menu-line-key
          (find-if (lambda (line)
                     (search "Zzgo" (menu-line-text line)))
                   (cast-lines g view))))
  (check "a non-caster digit is ignored" nil
         (progn (cast-act g view #\1) (cast-view-hero view)))
  (cast-act g view #\2)
  (check "digit picks the caster by party slot" "Zzgo"
         (hero-name (cast-view-hero view)))
  (check-true "spell page shows the book"
              (find-if (lambda (s) (search "test bolt" s))
                       (menu-texts (cast-lines g view))))
  ;; a damage spell out of combat: refused in place, menu stays open
  (check "uncastable pick refuses and stays" nil (cast-act g view #\1))
  (check-true "menu still open on the spell page"
              (and (cast-view-hero view) (null (cast-view-spell view))))
  ;; a heal walks on to the target page and commits
  (damage-hero g grunt 3)
  (cast-act g view #\2)                 ; test-mend -> target page
  (check-true "heal pick opens the target page"
              (find-if (lambda (s) (search "on whom?" s))
                       (menu-texts (cast-lines g view))))
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
  (check-true "item-usable-p unrestricted" (item-usable-p h 't-torch))
  ;; the (unfit) marker: the sheet, gear and shop rows show a class
  ;; mismatch before the player tries
  (check "item-fit-marker for the wrong class" " (unfit)"
         (item-fit-marker h 't-mail))
  (check "item-fit-marker for a fitting item" ""
         (item-fit-marker h 't-torch))
  (check "sheet pack line marks the unfit item"
         "Pack: T Mail (unfit)"
         (seventh (hero-summary-lines h))))

;; The class registry lists the campaign's classes.
(let ((classes (hero-classes)))
  (check-true "hero-classes lists registered classes"
              (and (member :tester classes)
                   (member :t-wizard classes)))
  (check "hero-classes is sorted"
         (sort (copy-list classes) #'string< :key #'symbol-name)
         classes))

;; TOGGLE-EQUIP: the gear page's one-key toggle — on, off, and the
;; engine's refusals pass through.
(let* ((m (parse-map *art* :name "test"))
       (h (%combat-hero))
       (g (new-game m :party (list h)))
       (msgs (watch-messages g)))
  (give-item g h 't-sword)
  (give-item g h 't-torch)
  (check-true "toggle equips an unworn item" (toggle-equip g h 't-sword))
  (check "toggled on" 't-sword (equipped-of-kind h :weapon))
  (check-true "toggle removes a worn item" (toggle-equip g h 't-sword))
  (check "toggled off" nil (equipped-of-kind h :weapon))
  (check-true "removal message"
              (find-if (lambda (s) (search "removes T Sword" s))
                       (funcall msgs)))
  (check "toggle refuses misc items" nil (toggle-equip g h 't-torch)))

;; The gear page model (EQUIP-VIEW): both front-ends feed keys into
;; EQUIP-ACT and draw EQUIP-LINES — 'e' on the character sheet.
(let* ((m (parse-map *art* :name "test"))
       (h (with-rng () (make-hero "Wiz" :t-wizard)))
       (g (new-game m :party (list h)))
       (view (make-equip-view h)))
  (check-true "empty pack says so"
              (find-if (lambda (s) (search "The pack is empty" s))
                       (menu-texts (equip-lines g view))))
  (give-item g h 't-sword)
  (give-item g h 't-mail)                ; :classes (:tester) — unfit
  (check-true "gear page names the hero"
              (search "*** Wiz's Gear ***"
                      (first (equip-lines g view))))
  (check-true "gear page shows ac and attack"
              (find-if (lambda (s) (search "AC 10   Attack 1d3" s))
                       (menu-texts (equip-lines g view))))
  (check-true "gear page lists the pack numbered"
              (find-if (lambda (s) (search "1) T Sword" s))
                       (menu-texts (equip-lines g view))))
  (check "the item row carries its pick key" #\1
         (menu-line-key
          (find-if (lambda (line)
                     (search "T Sword" (menu-line-text line)))
                   (equip-lines g view))))
  (check-true "gear page marks the unfit item"
              (find-if (lambda (s) (search "2) T Mail (unfit)" s))
                       (menu-texts (equip-lines g view))))
  (check-true "gear page ends with the key hints"
              (search "[1-9] equip/remove"
                      (first (last (equip-lines g view)))))
  (check "a digit equips the item" nil (equip-act g view #\1))
  (check "equipped through the page" 't-sword (equipped-of-kind h :weapon))
  (check-true "the worn item is starred"
              (find-if (lambda (s) (search "1) T Sword*" s))
                       (menu-texts (equip-lines g view))))
  (check-true "the attack line follows the weapon"
              (find-if (lambda (s) (search "Attack 1d6+2" s))
                       (menu-texts (equip-lines g view))))
  (check "the same digit removes it again" nil (equip-act g view #\1))
  (check "removed through the page" nil (equipped-of-kind h :weapon))
  (check "the unfit item stays refused" nil
         (progn (equip-act g view #\2)
                (equipped-of-kind h :armor)))
  (check "a digit past the pack does nothing" nil (equip-act g view #\9))
  (check "escape closes the gear page" :cancelled
         (equip-act g view #\Escape)))

;; A deep pack scrolls on the gear page (the shop-stock pattern).
(dolist (name '(teq-1 teq-2 teq-3 teq-4 teq-5 teq-6 teq-7 teq-8))
  (define-item name :price 1))
(let* ((h (%combat-hero))
       (g (new-game (parse-map *art* :name "test") :party (list h)))
       (view (make-equip-view h)))
  (dolist (name '(teq-1 teq-2 teq-3 teq-4 teq-5 teq-6 teq-7 teq-8))
    (give-item g h name))
  (check-true "deep pack: the below marker shows"
              (member "v more below [d]" (menu-texts (equip-lines g view))
                      :test #'equal))
  (check "d scrolls the pack" nil (equip-act g view #\d))
  (check "the view holds the clamped offset" 3 (equip-view-top view))
  (check-true "scrolled pack: row 1 is the fourth item"
              (find-if (lambda (s) (search "1) Teq 4" s))
                       (menu-texts (equip-lines g view)))))

;; Usable items: DEFINE-ITEM :use validation.
(define-item 't-potion   :price 10 :use '(:heal "1d4+1") :consumed t)
(define-item 't-lantern  :price 12 :use '(:light t :duration 15))
(define-item 't-fx-torch :price 2  :use '(:light t :duration 30)
             :consumed t :image "fx-torch.iff")
(define-item 't-elixir   :price 5  :use '(:heal "1d4") :consumed t
             :classes '(:tester))
(check-error "define-item rejects a malformed :use"
  (define-item 't-bogus :use '(:frobnicate t)))
(check-error "a timed :use needs a duration"
  (define-item 't-bogus :use '(:light t)))
(check-error ":consumed without a :use is rejected"
  (define-item 't-bogus :consumed t))

;; USE-ITEM mechanics: refusals say why; a timed :use installs the
;; effect (title + image), a heal heals, :consumed spends the item.
(let* ((m (parse-map *art* :name "test"))
       (h (%combat-hero))
       (g (new-game m :party (list h)))
       (msgs (watch-messages g))
       (used '()))
  (on-event g :item-used
            (lambda (game hero name) (declare (ignore game hero))
              (push name used)))
  (check "use-item needs the item in the pack" nil (use-item g h 't-potion))
  (check-true "does-not-carry message"
              (find-if (lambda (s) (search "does not carry" s))
                       (funcall msgs)))
  (give-item g h 't-torch)
  (check "an item without a :use does nothing" nil (use-item g h 't-torch))
  (check-true "nothing-happens message"
              (find-if (lambda (s) (search "Nothing happens" s))
                       (funcall msgs)))
  (check "refusals emit nothing" '() used)
  (give-item g h 't-fx-torch)
  (check "usable-items sees only the :use item" '(t-fx-torch)
         (usable-items h))
  (check-true "a light item lights the party" (use-item g h 't-fx-torch))
  (check-true "the torch effect burns" (light-active-p g))
  (check "the effect keeps the item's title" "t fx torch"
         (effect-label (find-effect g "T Fx Torch")))
  (check "the effect carries the item's image" "fx-torch.iff"
         (effect-image (find-effect g "T Fx Torch")))
  (check "the effect is timed" (+ (game-time g) 30)
         (effect-expires-at (find-effect g "T Fx Torch")))
  (check "the consumed torch left the pack" nil
         (hero-carrying-p h 't-fx-torch))
  (check ":item-used emitted" '(t-fx-torch) used)
  ;; a potion heals its user by default
  (give-item g h 't-potion)
  (damage-hero g h 5)
  (let ((before (hero-hp h)))
    (check-true "a potion heals"
                (with-rng (2) (use-item g h 't-potion)))  ; 1d4+1 -> 4
    (check "the heal landed on the user" (+ before 4) (hero-hp h)))
  (check "the potion is spent" nil (hero-carrying-p h 't-potion))
  ;; a reusable item stays
  (give-item g h 't-lantern)
  (check-true "the lantern lights" (use-item g h 't-lantern))
  (check-true "a reusable item stays in the pack"
              (hero-carrying-p h 't-lantern)))

;; Class-gated use: carrying is not using.
(let* ((m (parse-map *art* :name "test"))
       (wiz (with-rng () (make-hero "Wiz" :t-wizard)))
       (g (new-game m :party (list wiz)))
       (msgs (watch-messages g)))
  (give-item g wiz 't-elixir)
  (check "usable-items respects the class gate" '() (usable-items wiz))
  (check "use-item refuses the wrong class" nil (use-item g wiz 't-elixir))
  (check-true "cannot-use message on use"
              (find-if (lambda (s) (search "cannot use" s))
                       (funcall msgs))))

;; The use menu: pick the user, the item, and — for a heal — the
;; target (the CAST-VIEW pattern).
(let* ((m (parse-map *art* :name "test"))
       (a (%combat-hero))
       (b (%combat-mage))
       (g (new-game m :party (list a b))))
  (give-item g a 't-fx-torch)
  (give-item g a 't-potion)
  (let ((v (make-use-view)))
    (check "Esc at the top cancels" :cancelled (use-act g v #\Escape)))
  (let ((v (make-use-view)))
    (check-true "the menu opens on the user pick"
                (member "Who uses?" (use-lines g v) :test #'equal))
    (use-act g v #\2)
    (check "a hero with nothing usable is not picked" nil
           (use-view-hero v))
    (use-act g v #\1)
    (check "hero 1 picked" a (use-view-hero v))
    (check-true "the item page lists the torch"
                (find-if (lambda (s) (search "T Fx Torch" s))
                         (menu-texts (use-lines g v))))
    (use-act g v #\Escape)
    (check "Esc backs out to the user pick" nil (use-view-hero v))
    (use-act g v #\1)
    ;; a timed item commits at once
    (check "using the torch resolves the menu" :done (use-act g v #\1))
    (check-true "the torch burns" (light-active-p g)))
  ;; a healing item asks for its target
  (let ((v (make-use-view)))
    (use-act g v #\1)
    (check "the potion wants a target first" nil (use-act g v #\1))
    (check-true "the target page asks on whom"
                (find-if (lambda (s) (search "on whom?" s))
                         (menu-texts (use-lines g v))))
    (damage-hero g b 4)
    (let ((before (hero-hp b)))
      (check "picking the target commits" :done
             (with-rng (0) (use-act g v #\2)))  ; 1d4+1 -> 2
      (check "the heal landed on hero 2" (+ before 2) (hero-hp b)))))

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

;; The committed fixture world carries its story layer: the keep is a
;; city whose shoppe and stairs are data, the crypt a dark dungeon
;; whose ladder travels back up.
(let ((m (load-map-file "tests/world/keep.map")))
  (check "keep is a city zone" :city (dungeon-map-kind m))
  (check "keep title" "Testhold" (map-title m))
  (check-true "keep shoppe location"
              (find-if (lambda (op) (string-equal (first op) "LOCATION"))
                       (cell-special m 1 0)))
  (check-true "keep tavern location"
              (find-if (lambda (op) (string-equal (first op) "LOCATION"))
                       (cell-special m 3 0)))
  (check-true "keep stairs lead to the crypt"
              (find-if (lambda (op) (string-equal (first op) "TRAVEL"))
                       (cell-special m 4 0))))
(let ((m (load-map-file "tests/world/crypt.map")))
  (check "crypt is a dungeon zone" :dungeon (dungeon-map-kind m))
  (check-true "crypt is dark" (dungeon-map-dark m))
  (check-true "crypt ladder leads back to the keep"
              (find-if (lambda (op) (string-equal (first op) "TRAVEL"))
                       (cell-special m 2 0))))

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
(let* ((m (parse-map *art* :name "worlds/w/x.map"))
       (g (new-game m)))
  (%apply-map-form m '(zone :gfx "city-pack/") "x.map")
  (check "zone pack falls back to the game directory"
         "city-pack/" (zone-gfx-dir g)))
(check "a zone without :gfx has no pack" nil
       (zone-gfx-dir (new-game (parse-map *art*))))

;;; The tile-pack cache (*GFX-CACHE-PACKS*): swapping packs on zone
;;; travel parks the old pack instead of freeing it, so walking back is
;;; free.  The policy is pure list work — WALLS stands in for the piece
;;; bitmaps and FREED records what the front end would have released.
(let ((freed '()))
  (flet ((free-fn (walls) (push walls freed) nil)
         (reset () (setf freed '())))
    ;; put/take: a round trip returns the very same pack and palette
    (let ((cache (%pack-cache-put '() "town/" :town-walls :town-pal)))
      (multiple-value-bind (walls pal rest) (%pack-cache-take cache "town/")
        (check "cached pack comes back" :town-walls walls)
        (check "cached palette comes back" :town-pal pal)
        (check "taking it empties the cache" '() rest))
      (multiple-value-bind (walls pal rest) (%pack-cache-take cache "cellar/")
        (check "a miss yields no pack" nil walls)
        (check "a miss yields no palette" nil pal)
        (check "a miss leaves the cache alone" cache rest)))
    ;; a failed load (wireframe fallback) is not worth caching
    (check "an unloaded pack is not cached" '()
           (%pack-cache-put '() "broken/" nil nil))
    ;; re-putting a directory replaces its entry — two would leak the
    ;; older entry's bitmaps, which nothing would ever free
    (let ((cache (%pack-cache-put (%pack-cache-put '() "town/" :old :pal)
                                  "town/" :new :pal)))
      (check "re-putting a pack does not double it" 1 (length cache))
      (check "re-putting a pack keeps the newest" :new
             (second (first cache))))
    ;; the budget: N inactive packs, least-recently-used evicted
    (let ((*gfx-cache-packs* 1))
      (reset)
      (let* ((cache (%pack-cache-put '() "old/" :old-walls nil))
             (cache (%pack-cache-put cache "new/" :new-walls nil))
             (kept (%pack-cache-trim cache #'free-fn)))
        (check "the budget keeps the most recent" 1 (length kept))
        (check "... and it is the newest" "new/" (first (first kept)))
        (check "the evicted pack is freed" '(:old-walls) freed)))
    (let ((*gfx-cache-packs* 0))
      (reset)
      (let ((kept (%pack-cache-trim
                   (%pack-cache-put '() "town/" :town-walls nil)
                   #'free-fn)))
        (check "a 0 budget caches nothing" '() kept)
        (check "... and frees what it drops" '(:town-walls) freed)))
    ;; :auto keeps one pack, but not when the machine is tight
    (let ((*gfx-cache-packs* :auto)
          (*gfx-cache-min-free* 1000000))
      (check ":auto keeps one pack" 1 (%pack-cache-limit))
      (reset)
      (check ":auto caches when memory is free" 1
             (length (%pack-cache-trim
                      (%pack-cache-put '() "town/" :town-walls nil)
                      #'free-fn 4000000)))
      (check "... freeing nothing" '() freed)
      (reset)
      (check ":auto drops the cache when memory is tight" '()
             (%pack-cache-trim
              (%pack-cache-put '() "town/" :town-walls nil)
              #'free-fn 500000))
      (check "... and frees the dropped pack" '(:town-walls) freed)
      (reset)
      (check ":auto caches when free memory is unknown" 1
             (length (%pack-cache-trim
                      (%pack-cache-put '() "town/" :town-walls nil)
                      #'free-fn nil))))
    ;; a nonsense setting is a clear error, not a silent no-cache
    (let ((*gfx-cache-packs* :sometimes))
      (check-error "*gfx-cache-packs* rejects a bad value"
        (%pack-cache-limit)))
    (let ((*gfx-cache-packs* -1))
      (check-error "*gfx-cache-packs* rejects a negative budget"
        (%pack-cache-limit)))
    ;; freeing the whole cache at session end releases every pack
    (reset)
    (check "dropping the cache frees everything" nil
           (%pack-cache-drop (%pack-cache-put
                              (%pack-cache-put '() "a/" :a nil) "b/" :b nil)
                             #'free-fn))
    ;; freed in cache order, most recently used first
    (check "... both packs" '(:b :a) (reverse freed))))

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

;; Walk the fixture world end-to-end: shoppe -> stairs -> crypt and
;; back up — a committed world's whole loop on real files.  The
;; shoppe's stock names campaign items, so the campaign loads first
;; (exactly what PLAY/PLAY-AMIGA do).  The Closure game's own suite
;; walks its shipped world the same way.
(load-campaign "tests/world/keep.map")
(let* ((m (load-map-file "tests/world/keep.map"))
       (g (new-game m :party (default-party))))
  (trigger-special g)
  (check "the keep has no zone pack (profile default)" nil
         (zone-gfx-dir g))
  ;; through the shoppe door to the east
  (turn-right g)
  (check "shoppe door opens" :door (move-party g))
  (check "the shoppe is a shop location" :shop
         (location-kind (game-location g)))
  (leave-location g)
  ;; east along the keep, stopping at the tavern
  (move-party g)                          ; (2,0)
  (move-party g)                          ; (3,0) — the tavern
  (check "the tavern is a tavern location" :tavern
         (location-kind (game-location g)))
  (check "the keep's drinks cost two gold" 2
         (tavern-price (game-location g)))
  (check "Esc leaves the tavern" :left
         (location-act g nil #\Escape))
  ;; on to the stairs
  (check "stairs drop into the crypt" :moved (move-party g))
  (check "stairs travel landed in the crypt" "the crypt"
         (map-title (game-map g)))
  (check-true "the crypt is dark" (game-dark-p g))
  (check "crypt arrival at its start" '(0 0)
         (list (game-x g) (game-y g)))
  ;; the ladder back up: teleport to the crypt's < cell and step on it
  (teleport-party g 2 0)
  (check "ladder returns to the keep" "Testhold"
         (map-title (game-map g)))
  (check "ladder lands between shoppe and tavern" '(2 0)
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
  (setf (hero-tunes b) 3)
  (decf (hero-sp c))
  (setf (game-time g) 700)
  (add-effect g "mage flame" :duration 60 :payload '(:light t)
                             :image "fx-flame.iff")
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
    (check "loaded effect keeps its image" "fx-flame.iff"
           (effect-image (find-effect g2 "mage flame")))
    (check "loaded undated effect stays undated" nil
           (effect-expires-at (find-effect g2 "blessing")))
    (check "loaded imageless effect stays imageless" nil
           (effect-image (find-effect g2 "blessing")))
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
      (check "loaded hero tunes" 3 (hero-tunes b2))
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
(check "resolve sibling path" "worlds/w/town.map"
       (%resolve-map-path "worlds/w/cellar.map" "town.map"))
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
                       (menu-texts (shop-lines g view))))
  (check "the hero row carries its pick key" #\1
         (menu-line-key
          (find-if (lambda (line)
                     (search "1) " (menu-line-text line)))
                   (shop-lines g view))))
  (check "digit picks the hero" nil (shop-act g view #\1))
  (check-true "hero selected" (eq h (shop-view-hero view)))
  (check-true "buy page lists the stock priced"
              (find-if (lambda (s) (search "1) T Sword  10 gp" s))
                       (menu-texts (shop-lines g view))))
  (check "the stock row carries its pick key" #\1
         (menu-line-key
          (find-if (lambda (line)
                     (search "T Sword" (menu-line-text line)))
                   (shop-lines g view))))
  (shop-act g view #\1)                 ; buy the sword
  (check "shop-act buys" 20 (hero-gold h))
  (check "s flips to the sell page" nil (shop-act g view #\s))
  (check "sell mode" :sell (shop-view-mode view))
  (check-true "sell page lists the pack with sell prices"
              (find-if (lambda (s) (search "1) T Sword*  5 gp" s))
                       (menu-texts (shop-lines g view))))
  (shop-act g view #\1)                 ; sell it again
  (check "shop-act sells" 25 (hero-gold h))
  (check "escape backs out to the pick page" nil
         (shop-act g view #\Escape))
  (check "hero deselected" nil (shop-view-hero view))
  (check "escape from the pick page leaves" :left
         (shop-act g view #\Escape))
  (check "location closed by the model" nil (game-location g)))

;; The shop marks stock (and pack) items the shopper's class cannot
;; use — buying stays allowed (another hero may carry it), the marker
;; just warns before the gold is gone.
(let* ((h (with-rng () (make-hero "Wiz" :t-wizard)))
       (g (new-game (parse-map *art* :name "test") :party (list h)))
       (view (make-shop-view)))
  (setf (hero-gold h) 100)
  (enter-location g '("The Fitting Room" :shop :stock (t-sword t-mail)))
  (shop-act g view #\1)                 ; the wizard shops
  (let ((texts (menu-texts (shop-lines g view))))
    (check-true "buy page marks unfit stock"
                (find-if (lambda (s)
                           (search "2) T Mail (unfit)  20 gp" s))
                         texts))
    (check-true "buy page leaves fitting stock unmarked"
                (find-if (lambda (s) (search "1) T Sword  10 gp" s))
                         texts)))
  (shop-act g view #\2)                 ; buy the unfit mail anyway
  (check "an unfit purchase is still allowed" 80 (hero-gold h))
  (check "but it does not auto-equip" nil (equipped-of-kind h :armor))
  (shop-act g view #\s)
  (check-true "sell page marks the unfit item"
              (find-if (lambda (s)
                         (search "1) T Mail (unfit)  10 gp" s))
                       (menu-texts (shop-lines g view))))
  (leave-location g))

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

;; Location pictures: the location op's :IMAGE resolves map-relative
;; (the effect-icon rule) — the Amiga front-end shows it in the view
;; column while the location's menu takes over the message area.
(let ((g (new-game (parse-map *art* :name "world/town"))))
  (enter-location g '("The Pictured Inn" :tavern :image "gfx/inn.iff"))
  (check "location-image reads the :IMAGE arg" "gfx/inn.iff"
         (location-image (game-location g)))
  (check "the picture resolves beside the map" "world/gfx/inn.iff"
         (location-image-path g))
  (leave-location g)
  (check "no location, no picture" nil (location-image-path g))
  (enter-location g '("Bare Hut" :hut))
  (check "a location without :IMAGE has no picture" nil
         (location-image-path g))
  (leave-location g))

;;; ---------------------------------------------------------------------
;;; Menu scrolling on the interaction models: a stock/pack/spell list
;;; deeper than a page windows with u/d and digits pick within the
;;; visible window — the front-ends inherit all of it from the models.

;; a nine-item stock: deeper than the page (7), windows to 5 rows
(dolist (spec '((tscr-1 1) (tscr-2 2) (tscr-3 3) (tscr-4 4) (tscr-5 5)
                (tscr-6 6) (tscr-7 7) (tscr-8 8) (tscr-9 9)))
  (define-item (first spec) :price (second spec)))
(let* ((h (%combat-hero))
       (g (new-game (parse-map *art* :name "test") :party (list h)))
       (view (make-shop-view)))
  (setf (hero-gold h) 100)
  (enter-location g '("The Deep Shoppe" :shop
                      :stock (tscr-1 tscr-2 tscr-3 tscr-4 tscr-5
                              tscr-6 tscr-7 tscr-8 tscr-9)))
  (shop-act g view #\1)                 ; the hero shops
  (let ((texts (menu-texts (shop-lines g view))))
    (check-true "deep stock: the first window starts at the head"
                (find-if (lambda (s) (search "1) Tscr 1" s)) texts))
    (check-true "deep stock: the below marker shows"
                (member "v more below [d]" texts :test #'equal))
    (check "deep stock: no above marker at the head" nil
           (member "^ more above [u]" texts :test #'equal)))
  (check "d scrolls the stock" nil (shop-act g view #\d))
  (check "the view holds the clamped offset" 4 (shop-view-top view))
  (let ((texts (menu-texts (shop-lines g view))))
    (check-true "scrolled stock: row 1 is the sixth item"
                (find-if (lambda (s) (search "1) Tscr 5" s)) texts))
    (check-true "scrolled stock: the above marker shows"
                (member "^ more above [u]" texts :test #'equal))
    (check "scrolled stock: no below marker at the tail" nil
           (member "v more below [d]" texts :test #'equal)))
  (shop-act g view #\2)                 ; buys the sixth item, tscr-6
  (check "a digit buys within the window" '(tscr-6) (hero-items h))
  (check "the windowed buy paid the right price" 94 (hero-gold h))
  (check "a digit past the window buys nothing" nil
         (progn (shop-act g view #\7) (rest (hero-items h))))
  (check "u scrolls back to the head" nil (shop-act g view #\u))
  (check "the offset is back at the head" 0 (shop-view-top view))
  ;; the sell page scrolls the pack the same way
  (dotimes (i 7) (give-item g h 'tscr-1))
  (shop-act g view #\s)
  (check "the page flip resets the offset" 0 (shop-view-top view))
  (let ((texts (menu-texts (shop-lines g view))))
    (check-true "a full pack scrolls on the sell page"
                (member "v more below [d]" texts :test #'equal)))
  (shop-act g view #\d)
  (check "the pack window clamps to its tail" 3 (shop-view-top view))
  (shop-act g view #\1)                 ; sells pack item 4 (tscr-1)
  (check "a digit sells within the window" 7 (length (hero-items h)))
  (check "escape resets the scroll offset" 0
         (progn (shop-act g view #\Escape) (shop-view-top view)))
  (leave-location g))

;; the use menu windows a full pack of usable items
(let* ((m (parse-map *art* :name "test"))
       (h (%combat-hero))
       (g (new-game m :party (list h)))
       (v (make-use-view)))
  (dotimes (i 8) (give-item g h 't-lantern))
  (use-act g v #\1)                     ; the hero uses
  (let ((texts (menu-texts (use-lines g v))))
    (check-true "a full pack scrolls on the use menu"
                (member "v more below [d]" texts :test #'equal)))
  (check "d scrolls the use list" 3
         (progn (use-act g v #\d) (use-view-top v)))
  (check "a windowed digit resolves the use" :done (use-act g v #\1))
  (check-true "the scrolled use landed" (light-active-p g)))

;; the cast menu windows a deep spell book
(dolist (name '(tscr-spell-1 tscr-spell-2 tscr-spell-3 tscr-spell-4))
  (define-spell name :cost 1 :level 1 :classes '(:t-mage) :heal "1d4"))
(let* ((m (parse-map *art* :name "test"))
       (h (with-rng () (make-hero "Mage" :t-mage)))
       (g (new-game m :party (list h)))
       (v (make-cast-view))
       (book (spells-for-hero h)))
  (check-true "the test book is deeper than a page"
              (> (length book) +menu-page-size+))
  (cast-act g v #\1)                    ; the mage casts
  (let ((texts (menu-texts (cast-lines g v))))
    (check-true "a deep book scrolls on the cast menu"
                (member "v more below [d]" texts :test #'equal)))
  (cast-act g v #\d)
  (check "the cast window scrolled" (- (length book) 5)
         (cast-view-top v))
  (let ((expected (nth (cast-view-top v) book)))
    (cast-act g v #\1)
    (check "a windowed digit picks the right spell" expected
           (cast-view-spell v))))

;; the save picker windows a slot list past the page
(let* ((m (parse-map *art* :name "test"))
       (g (new-game m))
       (v (%make-save-menu
           :mode :load
           :slots '("s1" "s2" "s3" "s4" "s5" "s6" "s7" "s8" "s9"))))
  (let ((texts (menu-texts (save-menu-lines g v))))
    (check-true "nine slots scroll in the picker"
                (member "v more below [d]" texts :test #'equal)))
  (check "d scrolls the slots" nil (save-menu-act g v #\d))
  (check "the slot window scrolled" 4 (save-menu-top v))
  (check "a windowed digit loads the right slot"
         (list :load (slot-path "s6"))
         (save-menu-act g v #\2)))

;; the character sheet windows a long stat block (a full pack lists
;; one row per item) and scrolls through HERO-SHEET-SCROLL
(let* ((m (parse-map *art* :name "test"))
       (h (%combat-hero))
       (g (new-game m :party (list h))))
  (check "an empty pack still says so" "Pack: nothing"
         (first (last (butlast (butlast (hero-sheet-lines g 0))))))
  (check "a short sheet keeps the plain hints"
         "[1-7] view another  [e] equip  [Esc] back"
         (first (last (hero-sheet-lines g 0))))
  (check "a short sheet does not scroll" nil
         (hero-sheet-scroll g 0 0 #\d))
  (give-item g h 't-sword)
  (equip-item g h 't-sword)
  (dotimes (i 7) (give-item g h 't-torch))
  (let ((texts (menu-texts (hero-sheet-lines g 0))))
    (check-true "a full pack scrolls the sheet"
                (member "v more below [d]" texts :test #'equal))
    (check-true "the sheet hints say so"
                (member
                 "[1-7] view another  [e] equip  [u/d] scroll  [Esc] back"
                 texts :test #'equal)))
  (let ((top (hero-sheet-scroll g 0 0 #\d)))
    (check "the sheet scrolls by its window" 6 top)
    (let ((texts (menu-texts (hero-sheet-lines g 0 top))))
      (check-true "the scrolled sheet reaches the pack rows"
                  (member "  T Sword*" texts :test #'equal))
      (check-true "the scrolled sheet shows both markers"
                  (and (member "^ more above [u]" texts :test #'equal)
                       (member "v more below [d]" texts :test #'equal))))
    (setf top (hero-sheet-scroll g 0 top #\d))
    (check "the sheet clamps at its tail" 9 top)
    (check-true "the tail window shows the last pack row"
                (member "  T Torch"
                        (menu-texts (hero-sheet-lines g 0 top))
                        :test #'equal))
    (check "u returns toward the head" 3
           (hero-sheet-scroll g 0 top #\u)))
  (check "an empty roster slot does not scroll" nil
         (hero-sheet-scroll g 4 0 #\d)))

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

;;; ---------------------------------------------------------------------
;;; Named saves: the save/load slot menu (src/save-menu.lisp)

(check "slot-path names saves/NAME.sav" "saves/alpha.sav"
       (slot-path "alpha"))
(check-true "slot names allow letters, digits, - and _"
            (every #'slot-name-char-p "Alpha-2_b"))
(check-true "slot names refuse path characters"
            (notany #'slot-name-char-p "/:. "))

(let ((*save-dir* "tests/tmp-saves/"))
  (check "no save dir means no slots" '() (save-slots))
  (let* ((m (load-map-file "tests/tmp-town.map"))
         (g (new-game m :party (list (%combat-hero))))
         (view (make-save-menu :save)))
    ;; an empty :save menu offers only the new-name entry
    (check-true "empty save menu says so"
                (find-if (lambda (s) (search "No saved games yet" s))
                         (save-menu-lines g view)))
    (check "digits without slots do nothing" nil (save-menu-act g view #\1))
    ;; 'n' starts name entry; name chars accumulate, junk is ignored,
    ;; backspace deletes, the live echo shows the name
    (check "n opens the name entry" nil (save-menu-act g view #\n))
    (dolist (c '(#\a #\l #\p #\h #\/ #\a))   ; the / must be ignored
      (save-menu-act g view c))
    (check "name entry keeps only name characters" "alpha"
           (save-menu-entry view))
    (save-menu-act g view #\Backspace)
    (check "backspace deletes" "alph" (save-menu-entry view))
    (save-menu-act g view #\a)
    (check-true "the entry line echoes the name"
                (find-if (lambda (s) (search "New name: alpha_" s))
                         (save-menu-lines g view)))
    ;; Return commits: the model returns the decision, the front-end
    ;; executes it — exactly what both UIs do
    (let ((r (save-menu-act g view #\Return)))
      (check "return commits the new name" '(:save "tests/tmp-saves/alpha.sav")
             r)
      (ensure-save-dir)
      (save-game g (second r)))
    (check "the slot now lists" '("alpha") (save-slots))
    ;; an empty name does not commit
    (let ((view (make-save-menu :save)))
      (save-menu-act g view #\n)
      (check "return on an empty name stays" nil
             (save-menu-act g view #\Return))
      (check "esc leaves the name entry" nil
             (save-menu-act g view #\Escape))
      (check "back on the slot list" nil (save-menu-entry view))
      ;; the fresh menu lists the existing slot; a digit overwrites it
      (check-true "existing slot listed"
                  (find-if (lambda (s) (search "1) alpha" s))
                           (menu-texts (save-menu-lines g view))))
      (check "the slot row carries its pick key" #\1
             (menu-line-key
              (find-if (lambda (line)
                         (search "1) alpha" (menu-line-text line)))
                       (save-menu-lines g view))))
      (check "digit picks the overwrite slot"
             '(:save "tests/tmp-saves/alpha.sav")
             (save-menu-act g view #\1))
      (check "esc cancels the menu" :closed
             (save-menu-act g view #\Escape)))
    ;; the name cap holds
    (let ((view (make-save-menu :save)))
      (save-menu-act g view #\n)
      (dotimes (i 20) (save-menu-act g view #\x))
      (check "slot names cap at the limit" +slot-name-limit+
             (length (save-menu-entry view))))
    ;; load mode: pick the slot, execute the decision, world restored
    (let ((view (make-save-menu :load)))
      (check-true "load menu lists the slot"
                  (find-if (lambda (s) (search "1) alpha" s))
                           (menu-texts (save-menu-lines g view))))
      (let ((r (save-menu-act g view #\1)))
        (check "digit picks the load slot"
               '(:load "tests/tmp-saves/alpha.sav") r)
        (check "the picked save loads" "Testville"
               (map-title (game-map (load-game (second r)))))))
    ;; the Amiga vanillakey Return (code 13) commits too
    (let ((view (make-save-menu :save)))
      (save-menu-act g view #\n)
      (save-menu-act g view #\b)
      (check "code-char 13 commits like Return"
             '(:save "tests/tmp-saves/b.sav")
             (save-menu-act g view (code-char 13))))
    ;; combat refuses politely: the page says so, digits do nothing,
    ;; only Esc reacts — the shared rule both front-ends inherit
    (start-combat g '(("test rat" 1)))
    (let ((view (make-save-menu :save)))
      (check-true "combat save page refuses"
                  (find-if (lambda (s) (search "No saving during combat" s))
                           (save-menu-lines g view)))
      (check "combat ignores slot digits" nil (save-menu-act g view #\1))
      (check "combat ignores the name key" nil (save-menu-act g view #\n))
      (check "esc still closes in combat" :closed
             (save-menu-act g view #\Escape)))
    ;; loading is not blocked by combat at the menu level (the fight is
    ;; abandoned with the old game object, like quitting to a save)
    (let ((view (make-save-menu :load)))
      (check "combat load still picks"
             '(:load "tests/tmp-saves/alpha.sav")
             (save-menu-act g view #\1))))
  (delete-file "tests/tmp-saves/alpha.sav")
  ;; the slot cap: with +MAX-SAVE-SLOTS+ slots already on disk, every
  ;; one stays reachable by its single digit only if 'n' refuses to
  ;; open a 10th — otherwise a name typed past the cap would be listed
  ;; but never pickable by number (a real bug: no cap plus a
  ;; single-digit-only picker leaves the extra slots orphaned)
  (let* ((g (new-game (load-map-file "tests/tmp-town.map")
                       :party (list (%combat-hero))))
         (view (%make-save-menu
                :mode :save
                :slots (loop for i from 1 to +max-save-slots+
                             collect (format nil "s~D" i)))))
    (check "n is refused once the slot cap is reached" nil
           (save-menu-act g view #\n))
    (check "no name entry opens at the cap" nil (save-menu-entry view))
    (check-true "the cap message is shown"
                (find-if (lambda (s) (search "Slot limit reached" s))
                         (menu-texts (save-menu-lines g view))))
    ;; the cap message clears once a slot is picked
    (check "picking a slot still works at the cap"
           '(:save "tests/tmp-saves/s1.sav")
           (save-menu-act g view #\1))
    (check-true "the cap message is gone after a pick"
                (notany (lambda (s) (search "Slot limit reached" s))
                        (menu-texts (save-menu-lines g view))))))

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

;;; The plane fold decodes a scanline's planes together, writes each pen
;;; once, and skips groups of eight pixels whose plane bytes are all
;;; zero.  These pin the paths that skip touches: a pen carried only by
;;; a high plane (the low planes' bytes for that group are zero, so the
;;; group must still be folded), a pen in the final partial group of a
;;; width that is not a multiple of eight, and an image that is almost
;;; entirely pen 0.
(dolist (compression '(1 0))
  (let ((img (make-image 20 3 3))          ; 20 wide: 3 groups, last is 4 px
        (path "tests/tmp-img.iff"))
    (setf (pixel-ref img 0 0) 4            ; plane 2 only, planes 0/1 zero
          (pixel-ref img 8 1) 1            ; plane 0 only, second group
          (pixel-ref img 19 2) 7           ; last pixel of a partial group
          (pixel-ref img 15 2) 6)          ; group boundary, planes 1+2
    (write-ilbm img path :compression compression)
    (let ((back (read-ilbm path)))
      (check-true (format nil "sparse image round trips exactly (cmp ~D)"
                          compression)
                  (equalp (image-pixels img) (image-pixels back)))
      (check (format nil "high-plane-only pen survives the zero-group skip ~
(cmp ~D)" compression)
             4 (pixel-ref back 0 0))
      (check (format nil "last pixel of a partial group decodes (cmp ~D)"
                     compression)
             7 (pixel-ref back 19 2))
      (check (format nil "untouched pens stay 0 (cmp ~D)" compression)
             0 (pixel-ref back 7 0)))))

;;; The planar reader is the same BODY decoded without folding to
;;; pens: READ-ILBM-PLANAR keeps the bitplane rows so the Amiga can
;;; poke them straight into a BitMap.  It has to agree with READ-ILBM
;;; exactly — that equivalence is the only thing making the fast path
;;; safe — so every pen is cross-checked here, on real pack art and on
;;; synthetic images whose widths straddle the row padding.
(flet ((planar-pen (img x y)
         ;; the pen at (X,Y), reassembled from the plane bits
         (let ((row-bytes (planar-image-row-bytes img))
               (pen 0))
           (dotimes (p (planar-image-depth img) pen)
             (let ((byte (aref (planar-image-plane img p)
                               (+ (* y row-bytes) (ash x -3)))))
               (when (logbitp (- 7 (logand x 7)) byte)
                 (setf pen (logior pen (ash 1 p)))))))))
  (dolist (compression '(1 0))
    (dolist (dims '((7 5 2) (16 4 3) (17 3 4) (20 3 3) (24 2 1) (33 2 8)))
      (destructuring-bind (w h depth) dims
        (let ((img (make-image w h depth))
              (path "tests/tmp-img.iff"))
          ;; a pattern with both dense and empty regions
          (dotimes (y h)
            (dotimes (x w)
              (setf (pixel-ref img x y)
                    (if (zerop (mod (+ x y) 3))
                        0
                        (mod (+ x (* 3 y)) (ash 1 depth))))))
          (write-ilbm img path :compression compression)
          (let ((chunky (read-ilbm path))
                (planar (read-ilbm-planar path))
                (bad '()))
            (check (format nil "planar geometry ~Dx~Dx~D cmp ~D"
                           w h depth compression)
                   (list w h depth)
                   (list (planar-image-width planar)
                         (planar-image-height planar)
                         (planar-image-depth planar)))
            (dotimes (y h)
              (dotimes (x w)
                (unless (= (pixel-ref chunky x y) (planar-pen planar x y))
                  (push (list x y) bad))))
            (check (format nil "planar pens match chunky ~Dx~Dx~D cmp ~D"
                           w h depth compression)
                   nil bad))))))
  ;; and on the shipped pack art, where the real widths and run
  ;; patterns live
  (dolist (file '("front-0.iff" "side-0-l.iff" "flank-3-r.iff"
                  "ceiling.iff" "floor.iff"))
    (let* ((path (engine-path (concatenate 'string "data/gfx/" file)))
           (chunky (read-ilbm path))
           (planar (read-ilbm-planar path))
           (bad 0))
      (dotimes (y (image-height chunky))
        (dotimes (x (image-width chunky))
          (unless (= (pixel-ref chunky x y) (planar-pen planar x y))
            (incf bad))))
      (check (format nil "planar ~A matches chunky pen for pen" file)
             0 bad)
      (check (format nil "planar ~A carries the palette" file)
             (aref (image-palette chunky) 1)
             (aref (planar-image-palette planar) 1)))))

;; The mask shortcut: for the usual pen-0 key the cookie-cut mask is
;; just the OR of the planes, which must equal what MASK-BYTES derives
;; from chunky pens.
(dolist (file '("front-0.iff" "side-0-l.iff" "ceiling.iff"))
  (let* ((path (engine-path (concatenate 'string "data/gfx/" file)))
         (chunky (read-ilbm path))
         (planar (read-ilbm-planar path)))
    (multiple-value-bind (want want-bpr)
        (mask-bytes (image-width chunky) (image-height chunky)
                    (image-pixels chunky))
      (multiple-value-bind (got got-bpr) (planar-mask-bytes planar)
        (check (format nil "planar mask row width matches for ~A" file)
               want-bpr got-bpr)
        (check-true (format nil "planar mask matches MASK-BYTES for ~A" file)
                    (equalp want got))))
    (check (format nil "planar transparency agrees for ~A" file)
           (not (null (image-transparent-p chunky)))
           (not (null (planar-image-transparent-p planar))))))

;; A non-zero transparent key has no OR shortcut — say so rather than
;; return a wrong mask.
(let ((planar (read-ilbm-planar (engine-path "data/gfx/front-0.iff"))))
  (check "planar mask declines a non-zero key" nil
         (planar-mask-bytes planar 3)))

;; A second BODY would decode against already-written pens, so it is
;; rejected rather than blended: duplicate the BODY chunk by surgery.
(let ((img (make-image 8 2 2)))
  (setf (pixel-ref img 1 1) 3)
  (write-ilbm img "tests/tmp-img.iff")
  (let* ((bytes (with-open-file (s "tests/tmp-img.iff"
                                   :element-type '(unsigned-byte 8))
                  (let ((v (make-array (file-length s)
                                       :element-type '(unsigned-byte 8))))
                    (read-sequence v s)
                    v)))
         (body (search (map 'vector #'char-code "BODY") bytes))
         (doubled (concatenate '(vector (unsigned-byte 8))
                               bytes (subseq bytes body))))
    (let ((len (- (length doubled) 8)))
      (setf (aref doubled 4) (ldb (byte 8 24) len)
            (aref doubled 5) (ldb (byte 8 16) len)
            (aref doubled 6) (ldb (byte 8 8) len)
            (aref doubled 7) (ldb (byte 8 0) len)))
    (with-open-file (s "tests/tmp-img.iff" :direction :output
                       :element-type '(unsigned-byte 8)
                       :if-exists :supersede)
      (write-sequence doubled s))
    (check-error "read-ilbm rejects a second BODY chunk"
      (read-ilbm "tests/tmp-img.iff"))))
(delete-file "tests/tmp-img.iff")

;;; ---------------------------------------------------------------------
;;; Pointer sprites: an image becomes hardware-sprite plane words, the
;;; hot spot is the topmost-leftmost inked pixel, and the built-in
;;; hand pointer honors both.  (The SetPointer glue is Amiga-only —
;;; see the amiga-ui pointer tests below.)

(let ((img (make-image 8 3 2)))
  (setf (pixel-ref img 1 0) 1           ; low plane
        (pixel-ref img 2 0) 2           ; high plane
        (pixel-ref img 3 0) 3)          ; both planes
  (check "pointer rows: pens split onto the two sprite planes"
         '((#x5000 #x3000) (0 0) (0 0))
         (pointer-sprite-rows img))
  (check "pointer hotspot: leftmost inked pixel of the topmost row"
         '(1 0)
         (multiple-value-bind (x y) (pointer-hotspot img) (list x y))))
(check "pointer hotspot of an empty image is the corner" '(0 0)
       (multiple-value-bind (x y) (pointer-hotspot (make-image 4 4 2))
         (list x y)))
(check-error "pointer rows reject an image wider than a sprite"
  (pointer-sprite-rows (make-image 17 2 2)))
(let ((img (make-image 8 2 3)))
  (setf (pixel-ref img 0 0) 4)
  (check-error "pointer rows reject pens above 3"
    (pointer-sprite-rows img)))

(let ((hand (hand-pointer-image)))
  (check "hand pointer is sprite-wide" 16 (image-width hand))
  (check "hand pointer converts row for row"
         (length *hand-pointer-art*)
         (length (pointer-sprite-rows hand)))
  (check "hand hotspot sits on the finger tip" '(4 0)
         (multiple-value-bind (x y) (pointer-hotspot hand) (list x y)))
  (check "hand palette holds the three sprite colors"
         *hand-pointer-colors*
         (list (aref (image-palette hand) 1)
               (aref (image-palette hand) 2)
               (aref (image-palette hand) 3))))

(let ((point (point-pointer-image)))
  (check "point pointer is sprite-wide" 16 (image-width point))
  (check "point pointer converts row for row"
         (length *point-pointer-art*)
         (length (pointer-sprite-rows point)))
  (check "point hotspot sits on the finger tip" '(4 0)
         (multiple-value-bind (x y) (pointer-hotspot point) (list x y)))
  (check "point pointer shares the sprite colors"
         *hand-pointer-colors*
         (list (aref (image-palette point) 1)
               (aref (image-palette point) 2)
               (aref (image-palette point) 3))))

;; The four move-zone arrows honor the same sprite contract: 16 wide,
;; one plane-word pair per art row, the shared sprite palette, and the
;; hot spot on the topmost row of the arrow.
(dolist (entry (list (list "forward" (forward-pointer-image)
                           *forward-pointer-art* '(7 0))
                     (list "back" (back-pointer-image)
                           *back-pointer-art* '(5 0))
                     (list "turn-left" (turn-left-pointer-image)
                           *turn-left-pointer-art* '(5 0))
                     (list "turn-right" (turn-right-pointer-image)
                           *turn-right-pointer-art* '(9 0))))
  (destructuring-bind (name img art spot) entry
    (check (format nil "~A arrow is sprite-wide" name) 16
           (image-width img))
    (check (format nil "~A arrow converts row for row" name)
           (length art) (length (pointer-sprite-rows img)))
    (check (format nil "~A arrow shares the sprite colors" name)
           *hand-pointer-colors*
           (list (aref (image-palette img) 1)
                 (aref (image-palette img) 2)
                 (aref (image-palette img) 3)))
    (check (format nil "~A arrow hotspot sits on the art" name) spot
           (multiple-value-bind (x y) (pointer-hotspot img)
             (list x y)))))

;; The busy hourglass draws glass and sand in separate pens — frame on
;; the high sprite plane, sand on the low one — and leaves part of the
;; glass empty, rather than a solid one-color silhouette.
(let* ((busy (busy-pointer-image))
       (rows (pointer-sprite-rows busy)))
  (check "busy pointer is sprite-wide" 16 (image-width busy))
  (check "busy pointer converts row for row"
         (length *busy-pointer-art*) (length rows))
  (check "busy pointer shares the sprite colors"
         *hand-pointer-colors*
         (list (aref (image-palette busy) 1)
               (aref (image-palette busy) 2)
               (aref (image-palette busy) 3)))
  (check-true "busy pointer inks sand in the sand pen"
              (some (lambda (row) (plusp (first row))) rows))
  (check-true "busy pointer inks the frame in the frame pen"
              (some (lambda (row) (plusp (second row))) rows))
  (check-true "busy pointer leaves empty glass between frame and sand"
              (some (lambda (row-art) (search ".1" row-art))
                    *busy-pointer-art*)))

;; The hover state machine behind the pointer swap: over a move zone
;; its directional arrow, over any other hotspot the finger, elsewhere
;; the hand.  *BUSY-POINTER-ACTIVE* suppresses the SetPointer call, so
;; the pure state transitions run without a display; the busy
;; bracket's unwind applies the pending state.  (amiga-ui.lisp only
;; loads on the Amiga — see also the on-screen hover checks in the
;; game-window test below.)
#+amigaos
(let ((*hotspots* '((30 30 40 40 #\w :forward)
                    (10 10 20 20 #\w)))
      (*pointer-hot* nil)
      (*busy-pointer-active* t))
  (%track-pointer-hot nil 15 15)
  (check "hover onto a click target arms the pointing finger" :point
         *pointer-hot*)
  (%track-pointer-hot nil 15 15)
  (check "resting on the target keeps the state" :point *pointer-hot*)
  (%track-pointer-hot nil 35 35)
  (check "hover onto a move zone arms its directional arrow" :forward
         *pointer-hot*)
  (%track-pointer-hot nil 5 5)
  (check "hover off the target goes back to the hand" nil
         *pointer-hot*)
  (let ((*hotspots* '()))
    (%track-pointer-hot nil 15 15)
    (check "a redraw that dropped the targets keeps the hand" nil
           *pointer-hot*)))

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

;; Flank pieces are the same flat wall as the front piece at their
;; depth, continued through the open side — their mortar joints must
;; land on the FRONT slot's brick grid (bond offsets 0 / brick/2),
;; carried across the seam via the pattern window, not on a grid
;; scaled to the flank's own narrow slot (which showed a flat wall
;; with three-times-smaller bricks on its adjacent segments).
(let ((planes (view-planes *fp-view-width* *fp-view-height*)))
  (dotimes (d 2)
    (destructuring-bind (fx fy fw fh)
        (wall-piece-rect planes (list :front d))
      (declare (ignore fx fy fh))
      (let ((brick (max 6 (round fw 5))))
        (dolist (side '(:l :r))
          (let* ((img (draw-wall-piece (list :flank d side) planes))
                 (w (image-width img))
                 (x0 (if (eq side :l) (- w) fw))  ; pattern window origin
                 (joints '()))
            ;; probe a row inside the first brick course (row 0 is the
            ;; white edge highlight)
            (dotimes (x w)
              (when (= 4 (pixel-ref img x 2)) (push x joints)))
            (check-true
             (format nil "depth-~D ~A flank joints sit on the front ~
brick grid" d side)
             (and joints
                  (every (lambda (x)
                           (member (mod (+ x0 x) brick)
                                   (list 0 (floor brick 2))))
                         joints)))))))))

;; Effects-band icons: the generator draws 16x16 pen-0-keyed art, and
;; the fixture world's checked-in fx-needle.iff is pinned to it the
;; same way as the packs (regenerate with `make assets`).
(dolist (kind '(:compass :flame :shield))
  (let ((img (draw-effect-icon kind)))
    (check (format nil "the ~A icon is 16x16" kind)
           (list *effect-icon-size* *effect-icon-size*)
           (list (image-width img) (image-height img)))
    (check-true (format nil "the ~A icon keeps the transparent key" kind)
                (image-transparent-p img))))
(let ((disk (read-ilbm "tests/world/fx-needle.iff"))
      (drawn (draw-effect-icon :compass)))
  (check-true "the fixture icon matches its generator"
              (equalp (image-pixels disk) (image-pixels drawn))))

;; Effect icons resolve map-relative, like zone tile packs.
(let* ((m (load-map-file "tests/world/keep.map"))
       (wanda (make-hero "W" :w-wizard))
       (g (new-game m :party (list wanda))))
  (check-true "the wizard casts the fixture compass"
              (cast-spell g wanda 'w-compass))
  (let ((e (find-effect g "w compass")))
    (check "the cast effect carries the campaign's image"
           "fx-needle.iff" (effect-image e))
    (check "the image resolves next to the map file"
           "tests/world/fx-needle.iff" (effect-image-path g e))
    (check-true "the resolved icon file exists"
                (probe-file (effect-image-path g e))))
  (add-effect g "plain" :payload '(:light t))
  (check "an imageless effect has no path" nil
         (effect-image-path g (find-effect g "plain")))
  (add-effect g "abs" :payload '(:light t) :image "/elsewhere/x.iff")
  (check "an absolute image path passes through" "/elsewhere/x.iff"
         (effect-image-path g (find-effect g "abs"))))

;; Takeover art: location scenes and portraits draw to the ordered
;; size and stay within the fixed UI pens 0-3 (black, white, grey,
;; amber) — a pack may only recolor pens 4+, so pictures painted with
;; higher pens would change color under foreign packs.
(dolist (kind '(:shop :tavern :hut))
  (let ((img (draw-location-scene kind 60 44))
        (maxpen 0))
    (check (format nil "the ~A scene sizes to order" kind) '(60 44)
           (list (image-width img) (image-height img)))
    (dotimes (y 44)
      (dotimes (x 60)
        (setf maxpen (max maxpen (pixel-ref img x y)))))
    (check-true (format nil "the ~A scene keeps to the UI pens" kind)
                (<= maxpen 3))))
(dolist (style '(:helm :crest :hood :cap :hat :plain))
  (let ((img (draw-portrait style)))
    (check (format nil "the ~A portrait is the standard size" style)
           (list *portrait-size* *portrait-size*)
           (list (image-width img) (image-height img)))))

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

;; Backdrops: the ceiling is solid distance bands split at the
;; perspective-plane rows (lores planes top at rows 0/22/37, horizon
;; between 55 and 56), darkening toward the horizon — the wall blits
;; go on top, so each band lines up with the corridor cell at its
;; depth.  The floor is one flat color, no distance shading.
(with-display-profile (:lores)
  (let* ((planes (view-planes *fp-view-width* *fp-view-height*))
         (ceiling (draw-backdrop-piece :ceiling planes))
         (floor (draw-backdrop-piece :floor planes)))
    (check "ceiling bands read near/mid/far down the slot"
           (list +pen-dim+ +pen-dark+ +pen-bg+)
           (list (pixel-ref ceiling 0 0)      ; band 0: rows 0-21
                 (pixel-ref ceiling 0 22)     ; band 1: rows 22-36
                 (pixel-ref ceiling 0 37)))   ; band 2: rows 37-horizon
    (check "the floor is one flat color edge to edge"
           (list +pen-mid+)
           (let ((pens '()))
             (dotimes (y (image-height floor))
               (dotimes (x (image-width floor))
                 (pushnew (pixel-ref floor x y) pens)))
             pens))
    (check "the far ceiling band reaches the horizon"
           +pen-bg+
           (pixel-ref ceiling 0 (1- (image-height ceiling))))))

;;; ---------------------------------------------------------------------
;;; The help page: pure text both front-ends draw verbatim.

(let ((lines (help-lines)))
  (check-true "help-lines is a non-empty list of strings"
              (and (consp lines) (every #'stringp lines)))
  (check-true "help mentions movement, map and help keys"
              (and (find-if (lambda (s) (search "W forward" s)) lines)
                   (find-if (lambda (s) (search "M map" s)) lines)
                   (find-if (lambda (s) (search "this help" s)) lines)))
  (check-true "help mentions combat and save keys"
              (and (find-if (lambda (s) (search "Combat:" s)) lines)
                   (find-if (lambda (s) (search "save" s)) lines)))
  (check-true "help mentions the gear page"
              (find-if (lambda (s) (search "equip" s)) lines)))

;;; ---------------------------------------------------------------------
;;; The roster's class codes and column plists.

;; Class codes are always two characters — the roster's CL column is
;; two cells wide, leaving the freed room to the name column.
(with-rng ()
  (check "single-word class abbreviates to two letters" "TE"
         (hero-class-abbrev (make-hero "A" :tester))))
(define-hero-class :war-mage :hp-dice "1d4" :caster t)
(with-rng ()
  (check "multi-word class abbreviates to two initials" "WM"
         (hero-class-abbrev (make-hero "A" :war-mage))))
(define-hero-class :knight-of-the-realm :hp-dice "1d8")
(with-rng ()
  (check "many-word class caps at two initials" "KO"
         (hero-class-abbrev (make-hero "A" :knight-of-the-realm))))

;; Both profiles carry the full Bard's Tale column set, in order.
(dolist (p (list *lores-profile* *hires-profile*))
  (let ((cols (display-profile-roster-cols p)))
    (check-true (format nil "~A roster columns are complete and ordered"
                        (display-profile-name p))
                (apply #'< (mapcar (lambda (k) (getf cols k))
                           '(:no :name :ac :hit :hpts :spl :spts :cl))))))

;;; ---------------------------------------------------------------------
;;; The microfont: the message log's compact 5x7 pixel font.

(check "microfont advance is 6 pixels" 6 +microfont-advance+)
(check "microfont line height is 8 pixels" 8 +microfont-line-height+)
(check "microfont text width" 30 (microfont-text-width "hello"))

;; Glyph shapes: 'A' has its crossbar, space is blank; anything
;; outside printable ASCII falls back to the hollow box.
(check-true "glyph A"
            (equalp #(#b01110 #b10001 #b10001 #b10001
                      #b11111 #b10001 #b10001)
                    (microfont-glyph #\A)))
(check-true "space is blank" (equalp #(0 0 0 0 0 0 0)
                                     (microfont-glyph #\Space)))
(check-true "non-ASCII falls back to the box"
            (eq *microfont-fallback* (microfont-glyph (code-char 200))))

;; Rendering: row-major pens, FG where a glyph bit is set, BG
;; elsewhere; WIDTH pads or cuts.
(multiple-value-bind (pens w h) (microfont-line "A" 7 2)
  (check "rendered width of one glyph cell" 6 w)
  (check "rendered height is the line height" 8 h)
  (check "buffer covers the cell" 48 (length pens))
  ;; row 0 of 'A' is 01110 -> pens 2 7 7 7 2, then the spacing column
  (check "top row pixels" '(2 7 7 7 2 2)
         (loop for x below 6 collect (aref pens x)))
  ;; row 4 is the 11111 crossbar
  (check "crossbar row pixels" '(7 7 7 7 7 2)
         (loop for x below 6 collect (aref pens (+ (* 4 6) x))))
  ;; row 7 is the spacing row
  (check "spacing row is background" '(2 2 2 2 2 2)
         (loop for x below 6 collect (aref pens (+ (* 7 6) x)))))
(multiple-value-bind (pens w h) (microfont-line "AB" 1 0 :width 8)
  (declare (ignore pens))
  (check "explicit width cuts the text" 8 w)
  (check "height stays fixed" 8 h))
(multiple-value-bind (pens w h) (microfont-line "" 1 0 :width 24)
  (check "explicit width pads short text" 24 w)
  (check "padded buffer is all background" 0
         (loop for p across pens maximize p))
  (check "padded height" 8 h))

;;; ---------------------------------------------------------------------
;;; Amiga front-end smoke tests in a window on the Workbench screen.
;;; DISABLED for now (gated on :LAMBDA-TALE-WINDOW-TESTS — push it
;;; onto *FEATURES* before loading to re-enable): the Workbench screen
;;; is not under the suite's control (size, font, depth, RTG promotion
;;; vary per setup), and the custom screen is the game's presentation
;;; — see the :SCREEN tests further down, which carry this coverage.

#+lambda-tale-window-tests
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
             ;; layout invariants: the taller plaque, the roster right
             ;; under it (no status line), the page/strip gap
             (check "plaque is two pixels taller than a text line"
                    (+ (ui-layout-plaque-y l) (ui-layout-lh l) 2)
                    (ui-layout-plaque-b l))
             (check "roster header sits right under the plaque"
                    (+ (ui-layout-plaque-b l) 5) (ui-layout-hdr-y l))
             (check-true "seven roster rows fit above the bottom edge"
                         (<= (+ (ui-layout-party-y l)
                                (* (ui-layout-lh l) +party-limit+))
                             (ui-layout-bottom l)))
             (check "the log page ends a gap above the effect strip"
                    (ui-layout-band-y l) (+ (ui-layout-page-b l) 4))
             (%amiga-draw-fp rp g (ui-layout-bx l) (ui-layout-by l)
                             (ui-layout-fp-w l) (ui-layout-fp-h l))
             (%amiga-draw-band rp g l)
             (%amiga-draw-log rp log l)
             ;; the cached-bitmap log path (the live session's) and
             ;; the help page draw too
             (let ((cache (make-hash-table :test #'equal)))
               (%amiga-draw-log rp log l cache)
               (check-true "log lines were cached as bitmaps"
                           (plusp (hash-table-count cache)))
               (%free-log-lines cache))
             (%amiga-draw-help rp l)
             ;; the full map mode over the same window
             (%amiga-draw-map-page rp g l nil)
             (%amiga-draw-map-page rp g l t)
             t))))

;; A zone title wider than the plaque must lose trailing characters
;; rather than overrun the border (the bug %PLAQUE-NAME fixes — see
;; %CHROME-FRAMES).
#+lambda-tale-window-tests
(let* ((m (parse-map *art* :name "A Very Long Location Name That Overflows The Plaque"))
       (g (new-game m)))
  (check "amiga-ui truncates a plaque title wider than the view column" t
         (amiga.intuition:with-window
             (win :title "Lambda's Tale Test"
                  :left 0 :top 0
                  :width (display-profile-win-width *display-profile*)
                  :height (display-profile-win-height *display-profile*)
                  :idcmp amiga.intuition:+idcmp-closewindow+)
           (let* ((rp (amiga.intuition:window-rastport win))
                  (l (%amiga-layout win rp))
                  (w (ui-layout-fp-w l))
                  (full (string-capitalize (map-title (game-map g))))
                  (name (%plaque-name rp full w)))
             (check-true "the untruncated title overruns the plaque"
                         (> (amiga.gfx:text-length rp full) (- w 2)))
             (check-true "the truncated title fits within the plaque"
                         (<= (amiga.gfx:text-length rp name) (- w 2)))
             (check-true "the title was actually shortened"
                         (< (length name) (length full)))
             t))))

;; The full map view must cope with a map bigger than the window —
;; the layout the spec is actually about.
#+lambda-tale-window-tests
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
#+lambda-tale-window-tests
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

;; The location interaction: the overlay page variant, the message-area
;; takeover (menu lines + rule + log tail on the white page) and the
;; view-column picture with its fall-back contract.
#+lambda-tale-window-tests
(let* ((m (parse-map *art* :name "test"))
       (g (new-game m :party (with-rng () (list (make-hero "A" :tester)))))
       (log (attach-message-log g))
       (view (make-shop-view)))
  (say g "Takeover smoke test line.")
  (enter-location g '("Smoke Shoppe" :shop :stock (t-sword t-torch)))
  (check "amiga-ui draws the location pages" t
         (amiga.intuition:with-window
             (win :title "Lambda's Tale Test"
                  :left 0 :top 0
                  :width (display-profile-win-width *display-profile*)
                  :height (display-profile-win-height *display-profile*)
                  :idcmp amiga.intuition:+idcmp-closewindow+)
           (let* ((rp (amiga.intuition:window-rastport win))
                  (l (%amiga-layout win rp)))
             (%amiga-draw-page rp (location-lines g view) l)   ; pick-hero
             (shop-act g view #\1)
             (%amiga-draw-page rp (location-lines g view) l)   ; buy page
             (shop-act g view #\s)
             (%amiga-draw-page rp (location-lines g view) l)   ; sell page
             (%amiga-draw-log rp log l)
             ;; the message-area takeover, uncached and cached: the
             ;; location menu and the character sheet
             (%amiga-draw-takeover rp (location-lines g view) log l)
             ;; roster rows click as their digits when enabled; the
             ;; empty row below the party registers nothing
             (let ((*hotspots* '()))
               (%amiga-party rp g l t)
               (check "roster: the hero row clicks as its digit" #\1
                      (%hotspot-at (+ (ui-layout-bx l) 3)
                                   (+ (ui-layout-party-y l) 2)))
               (check "roster: an empty row is not a target" nil
                      (%hotspot-at (+ (ui-layout-bx l) 3)
                                   (+ (ui-layout-party-y l)
                                      (ui-layout-lh l) 2))))
             (let ((*hotspots* '()))
               (%amiga-party rp g l)
               (check "roster: not clickable unless asked" nil
                      (%hotspot-at (+ (ui-layout-bx l) 3)
                                   (+ (ui-layout-party-y l) 2))))
             ;; the click-to-walk zones on the first-person view
             (let ((*hotspots* '()))
               (%register-move-zones l)
               (let* ((bx (ui-layout-bx l)) (by (ui-layout-by l))
                      (w (ui-layout-fp-w l)) (h (ui-layout-fp-h l))
                      (cx (+ bx (floor w 2))))
                 ;; each zone carries the arrow cursor of its move
                 (check "view: the middle walks forward" '(#\w :forward)
                        (multiple-value-list
                         (%hotspot-at cx (+ by 4))))
                 (check "view: the bottom middle steps back" '(#\s :back)
                        (multiple-value-list
                         (%hotspot-at cx (+ by h -4))))
                 (check "view: the left quarter turns left"
                        '(#\a :turn-left)
                        (multiple-value-list
                         (%hotspot-at (+ bx 2) (+ by (floor h 2)))))
                 (check "view: the right quarter turns right"
                        '(#\d :turn-right)
                        (multiple-value-list
                         (%hotspot-at (+ bx w -3) (+ by (floor h 2)))))
                 (check "view: outside the view is no target" nil
                        (%hotspot-at (+ bx w 20) (+ by (floor h 2))))))
             ;; the busy pointer brackets a load and restores, and a
             ;; nested use keeps the outer pointer up
             (check "busy pointer wraps a body and restores" :ok
                    (%call-with-busy-pointer win
                     (lambda ()
                       (check "nested busy pointer runs the body" :inner
                              (%call-with-busy-pointer
                               win (lambda () :inner)))
                       :ok)))
             (let ((cache (make-hash-table :test #'equal)))
               (%amiga-draw-takeover rp (hero-sheet-lines g 0) log l cache)
               (check-true "takeover lines were cached as bitmaps"
                           (plusp (hash-table-count cache)))
               (%free-log-lines cache))
             ;; the view-column picture: a real ILBM draws and centers;
             ;; a missing file defers to the caller (falls back to the
             ;; first-person view) after logging once
             (let ((images (make-hash-table :test #'equal))
                   (path "tests/tmp-pic.iff"))
               (write-ilbm (draw-location-scene :shop 40 30) path)
               (check-true "a location picture draws in the view column"
                           (%amiga-draw-picture rp images path l log))
               (check "a missing picture defers to the caller" nil
                      (%amiga-draw-picture rp images "tests/no-such.iff"
                                           l log))
               (check-true "the missing picture said so in the log"
                           (find-if (lambda (s) (search "No image" s))
                                    (log-recent log 5)))
               (%free-images images)
               (delete-file path))
             t)))
  (leave-location g))

;; Mouse hotspots on the menu renderers, with the game font selected
;; the way PLAY-AMIGA always does — the page budgets are tuned for
;; topaz 8, and on an RTG Workbench the system font would shrink the
;; overlay page below the footer row.  Row positions are recomputed
;; exactly the way the renderers wrap.
#+lambda-tale-window-tests
(let* ((m (parse-map *art* :name "test"))
       (g (new-game m :party (with-rng () (list (make-hero "A" :tester)))))
       (log (attach-message-log g))
       (view (make-shop-view)))
  (enter-location g '("Smoke Shoppe" :shop :stock (t-sword t-torch)))
  (check "amiga-ui menu hotspots sit on the drawn rows" t
         (%with-game-font
          (lambda (font)
            (amiga.intuition:with-window
                (win :title "Lambda's Tale Test"
                     :left 0 :top 0
                     :width (display-profile-win-width *display-profile*)
                     :height (display-profile-win-height *display-profile*)
                     :idcmp amiga.intuition:+idcmp-closewindow+)
              (let* ((rp (%game-rastport win font))
                     (l (%amiga-layout win rp)))
                ;; the overlay page (cast/use/sing/save look the same)
                (let* ((*hotspots* '())
                       (lh (1+ (amiga.gfx:rastport-tx-height rp)))
                       (cw (ui-layout-cw l))
                       (px (+ (ui-layout-bx l) 4))
                       (py (+ (ui-layout-by l) 4))
                       (max-chars (floor (- (ui-layout-fp-w l) 16) cw))
                       (rows (mapcan (lambda (line)
                                       (wrap-menu-line line max-chars))
                                     (location-lines g view)))
                       (hero-row (position-if #'menu-line-key rows))
                       (esc-row (position-if
                                 (lambda (r)
                                   (search "[Esc]" (menu-line-text r)))
                                 rows))
                       (esc-start
                         (first
                          (first
                           (menu-key-spans
                            (menu-line-text (nth esc-row rows))))))
                       (row-y (lambda (n) (+ py 4 (* n lh) 2))))
                  (%amiga-draw-page rp (location-lines g view) l)
                  (check "page: clicking the hero row picks it" #\1
                         (%hotspot-at (+ px 10)
                                      (funcall row-y hero-row)))
                  (check "page: clicking the footer's Esc hint leaves"
                         :esc
                         (%hotspot-at (+ px 8 (* esc-start cw) 2)
                                      (funcall row-y esc-row)))
                  (check "page: the title row is not a target" nil
                         (%hotspot-at (+ px 10) (funcall row-y 0))))
                ;; the message-area takeover (microfont geometry)
                (let* ((*hotspots* '())
                       (fresh (make-shop-view))
                       (ox (ui-layout-log-x l))
                       (oy (ui-layout-by l))
                       (max-chars (max 4 (floor (- (ui-layout-log-w l)
                                                   4)
                                                +microfont-advance+)))
                       (page-rows (max 1 (floor (- (ui-layout-page-b l)
                                                   oy 2)
                                                +microfont-line-height+)))
                       (rows (let ((all
                                     (mapcan
                                      (lambda (line)
                                        (wrap-menu-line line max-chars))
                                      (location-lines g fresh))))
                               ;; the renderer's overflow rule
                               (if (> (length all) page-rows)
                                   (delete-if (lambda (r)
                                                (equal
                                                 (menu-line-text r)
                                                 ""))
                                              all)
                                   all)))
                       (hero-row (position-if #'menu-line-key rows))
                       (esc-row (position-if
                                 (lambda (r)
                                   (search "[Esc]" (menu-line-text r)))
                                 rows))
                       (esc-start
                         (first
                          (first
                           (menu-key-spans
                            (menu-line-text (nth esc-row rows))))))
                       (row-y (lambda (n)
                                (+ oy 1
                                   (* n +microfont-line-height+) 3))))
                  (%amiga-draw-takeover rp (location-lines g fresh)
                                        log l)
                  (check "takeover: clicking the hero row picks it" #\1
                         (%hotspot-at (+ ox 2)
                                      (funcall row-y hero-row)))
                  (check "takeover: footer Esc hint leaves"
                         :esc
                         (%hotspot-at (+ ox (* esc-start
                                               +microfont-advance+)
                                         2)
                                      (funcall row-y esc-row)))
                  (check "takeover: the title row is not a target" nil
                         (%hotspot-at (+ ox 2) (funcall row-y 0))))
                t)))))
  (leave-location g))

;;; ---------------------------------------------------------------------
;;; Regression: OPEN-WINDOW/OPEN-SCREEN's WA_Title/SA_Title string must
;;; stay allocated until CLOSE-WINDOW/CLOSE-SCREEN — Intuition holds
;;; the pointer live for title-bar redraws, not just for the
;;; OpenWindowTagList/OpenScreenTagList call that installs it.  Force
;;; intervening foreign allocations (candidates to reuse a
;;; prematurely-freed title block) between open and close, confirm the
;;; title is tracked in AMIGA.INTUITION::*TITLE-STRINGS* while open,
;;; still readable through the live Window struct after the pressure,
;;; and untracked again once closed.
#+amigaos
(let ((title "Title Survival Test")
      (before (hash-table-count amiga.intuition::*title-strings*)))
  (amiga.intuition:with-window
      (win :title title :left 5 :top 5 :width 160 :height 60)
    (check "window title is tracked while the window is open"
           (1+ before)
           (hash-table-count amiga.intuition::*title-strings*))
    (dotimes (i 64)
      (ffi:free-foreign (ffi:alloc-foreign 64)))
    (check "window title string survives allocation pressure" title
           (ffi:foreign-to-string
            (ffi:make-foreign-pointer
             (amiga.intuition:window-title win)))))
  (check "window title is untracked after the window closes"
         before
         (hash-table-count amiga.intuition::*title-strings*)))

#+amigaos
(let ((before (hash-table-count amiga.intuition::*title-strings*)))
  (amiga.intuition:with-screen
      (scr :width (display-profile-screen-width *display-profile*)
           :height (display-profile-screen-height *display-profile*)
           :depth (display-profile-screen-depth *display-profile*)
           :title "Screen Title Survival Test")
    (dotimes (i 64)
      (ffi:free-foreign (ffi:alloc-foreign 64)))
    (check "screen title is tracked while the screen is open"
           (1+ before)
           (hash-table-count amiga.intuition::*title-strings*)))
  (check "screen title is untracked after the screen closes"
         before
         (hash-table-count amiga.intuition::*title-strings*)))

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
               (win :title nil          ; the game's backdrop is untitled
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
               (%amiga-party rp g l)
               ;; the view-column picture contract, on the game's own
               ;; screen: a real ILBM draws and centers; a missing
               ;; file defers to the caller (falls back to the
               ;; first-person view) after logging once
               (let ((images (make-hash-table :test #'equal))
                     (path "tests/tmp-pic.iff"))
                 (write-ilbm (draw-location-scene :shop 40 30) path)
                 (check-true "a location picture draws in the view column"
                             (%amiga-draw-picture rp images path l log))
                 (check "a missing picture defers to the caller" nil
                        (%amiga-draw-picture rp images "tests/no-such.iff"
                                             l log))
                 (check-true "the missing picture said so in the log"
                             (find-if (lambda (s) (search "No image" s))
                                      (log-recent log 5)))
                 (%free-images images)
                 (delete-file path))
               ;; The game hides the OS screen bar: ShowTitle NIL plus
               ;; the full-height backdrop window (%CALL-WITH-GAME-WINDOW
               ;; does the same).  Probe the screen's own rastport
               ;; (offset 84 in struct Screen): were the bar layer
               ;; still in front, the screen bitmap's top rows would
               ;; hold the bar's rendering, not our pixels.
               (amiga.intuition:show-title scr nil)
               (amiga.gfx:set-a-pen rp 3)
               (amiga.gfx:rect-fill rp 0 0 50 3)
               (amiga.gfx:set-a-pen rp 1)
               (check "the screen bar stays hidden behind the backdrop"
                      3
                      (amiga.gfx:read-pixel
                       (ffi:make-foreign-pointer
                        (+ (ffi:foreign-pointer-address scr) 84))
                       25 1))
               t)))))

;; The same probe through the production path: %CALL-WITH-GAME-WINDOW
;; (RTG-aware mode-id promotion + backdrop window + ShowTitle).  The
;; game once called ShowTitle before opening the window; on a
;; Picasso96-promoted screen the bar layer then stayed in front of the
;; later-opened backdrop and the title bar was visible in play.
;; ShowTitle must run after the window opens, and this probe holds it
;; to that.
#+amigaos
(check "the screen bar stays hidden on the game window path" 3
       (%call-with-game-window
        :screen
        (lambda (scr win)
          ;; the backdrop carries no WA_Title — even OPEN-WINDOW's
          ;; default title would cost a title bar (border-top) that
          ;; sits in front of the hidden screen bar
          (check "the game backdrop window has no top border" 0
                 (amiga.intuition:window-border-top win))
          ;; The standard pointer: the hand sprite loads, its palette
          ;; drives the sprite colors (screen colors 17-19 — read back
          ;; through the viewport's ColorMap, the diagnostic for RTG
          ;; setups where the pointer rendered black), a busy bracket
          ;; restores it, and dropping it clears the state.
          (let ((cm (amiga.gfx:viewport-color-map
                     (amiga.intuition:screen-viewport scr)))
                (*game-pointer* nil)
                (*point-pointer* nil)
                (*move-pointers* '())
                (*pointer-hot* nil))
            (%ensure-standard-pointer scr win :screen)
            (check-true "the hand pointer is loaded" *game-pointer*)
            (check-true "the pointing finger is loaded" *point-pointer*)
            (check "the four move arrows are loaded"
                   '(:forward :back :turn-left :turn-right)
                   (loop for entry on *move-pointers* by #'cddr
                         collect (first entry)))
            (check "sprite color 17 took the hand's skin tone" #x0EDB
                   (amiga.gfx:get-rgb4 cm 17))
            (check "sprite color 18 took the hand's outline" #x0111
                   (amiga.gfx:get-rgb4 cm 18))
            ;; hover feedback: crossing onto a click target shows the
            ;; finger sprite, a move zone its arrow, leaving them goes
            ;; back to the hand
            (let ((*hotspots* '((30 30 40 40 #\a :turn-left)
                                (10 10 20 20 #\w))))
              (%track-pointer-hot win 15 15)
              (check "over a click target the finger is up" :point
                     *pointer-hot*)
              (%track-pointer-hot win 35 35)
              (check "over a move zone its arrow is up" :turn-left
                     *pointer-hot*)
              (%track-pointer-hot win 5 5)
              (check "off the target the hand is back" nil
                     *pointer-hot*))
            (let ((outer *game-pointer*))
              (check "busy pointer wraps a body and restores" :ok
                     (%call-with-busy-pointer win
                      (lambda ()
                        (check "nested busy pointer runs the body"
                               :inner
                               (%call-with-busy-pointer
                                win (lambda () :inner)))
                        :ok)))
              (check "the busy bracket restores the hand pointer"
                     outer *game-pointer*))
            ;; a campaign overrides art and colors with a pointer.iff
            ;; in its tile pack: 8x2, one pen-1 pixel at (2,0) — the
            ;; hot spot — one pen-2 pixel below, a green/blue CMAP.
            ;; Every palette entry is set: WRITE-ILBM compacts NIL
            ;; entries out of the CMAP, which would shift the colors.
            (let ((img (make-image 8 2 2)))
              (setf (pixel-ref img 2 0) 1
                    (pixel-ref img 2 1) 2)
              (setf (aref (image-palette img) 0) '(0 0 0)
                    (aref (image-palette img) 1) '(0 255 0)
                    (aref (image-palette img) 2) '(0 0 255)
                    (aref (image-palette img) 3) '(255 255 255))
              (write-ilbm img "tests/pointer.iff"))
            (let ((*gfx-dir* "tests/"))
              (%ensure-standard-pointer scr win :screen))
            (destructuring-bind (chip height xoff yoff) *game-pointer*
              (check "pointer.iff: sprite height follows the art" 2
                     height)
              (check "pointer.iff: hot spot on the inked pixel" '(-2 0)
                     (list xoff yoff))
              (check "pointer.iff: plane words in chip RAM"
                     '(#x2000 #x0000)
                     (list (ffi:peek-u16 chip 4) (ffi:peek-u16 chip 6))))
            (check "pointer.iff: its CMAP drives sprite color 17" #x00F0
                   (amiga.gfx:get-rgb4 cm 17))
            (delete-file "tests/pointer.iff")
            (%free-standard-pointer win)
            (check "dropping the pointer clears the session state" nil
                   (or *game-pointer* *point-pointer* *move-pointers*
                       *pointer-hot*)))
          (let ((rp (amiga.intuition:window-rastport win)))
            (amiga.gfx:set-a-pen rp 3)
            (amiga.gfx:rect-fill rp 0 0 50 3)
            (amiga.gfx:set-a-pen rp 1)
            (amiga.gfx:read-pixel
             (ffi:make-foreign-pointer
              (+ (ffi:foreign-pointer-address scr) 84))
             25 1)))))

;; Wall-piece assets (M3): each profile's pack ILBMs load into
;; offscreen bitmaps and the first-person view composits them with
;; real OS blits.  Runs on the game's own custom screen — the
;; profile's nominal PAL geometry guarantees the layout keeps the full
;; asset-size viewport (the FS-UAE Workbench can be shorter than 256
;; lines, where the view correctly falls back to the wireframe).
;; Read-back probes, dead end at (0,0) facing north: the front wall
;; piece's top row is the white edge highlight; above it the ceiling
;; backdrop shows its near distance band (dim grey, pen 6); below it
;; the floor backdrop shows its flat color (mid grey, pen 5).
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
                (check (format nil "~A: blitted ceiling near-band pixel"
                               pname)
                       +pen-dim+
                       (amiga.gfx:read-pixel rp (+ (ui-layout-bx l) cx)
                                             (+ (ui-layout-by l) cy))))
              (destructuring-bind (sx sy) floor-xy
                (check (format nil "~A: blitted floor pixel is the ~
flat floor color" pname)
                       +pen-mid+
                       (amiga.gfx:read-pixel rp (+ (ui-layout-bx l) sx)
                                             (+ (ui-layout-by l) sy))))
              ;; The planar fast path (*WALL-LOAD-PLANAR*, the default)
              ;; pokes ILBM plane rows into a scratch BitMap and lets
              ;; the blitter move them into the piece bitmap, never
              ;; going through chunky pens.  It has to land the SAME
              ;; pens on screen as WriteChunkyPixels would — so blit a
              ;; whole opaque piece to a clear patch and read it back
              ;; against the pens READ-ILBM declares for that file.
              ;; (Sampled every 7th pixel: READ-PIXEL is a library call
              ;; per pixel, and a stride catches plane-order, row-pad
              ;; and stride mistakes alike.)
              (let* ((key '(:front 0))
                     (file (concatenate 'string *gfx-dir*
                                        (wall-piece-file key)))
                     (want (read-ilbm file))
                     (bm (car (gethash key walls)))
                     (pw (image-width want))
                     (ph (image-height want))
                     (bad '()))
                (amiga.gfx:set-a-pen rp 0)
                (amiga.gfx:rect-fill rp 0 0 (1- pw) (1- ph))
                (amiga.gfx:blt-bitmap-rastport bm 0 0 rp 0 0 pw ph)
                (dotimes (y ph)
                  (dotimes (x pw)
                    (when (and (zerop (mod (+ x (* 3 y)) 7))
                               (< (length bad) 8)) ; keep the report short
                      (let ((got (amiga.gfx:read-pixel rp x y))
                            (expect (pixel-ref want x y)))
                        (unless (= got expect)
                          (push (list x y :got got :want expect) bad))))))
                (check (format nil "~A: planar-loaded piece blits the ~
pens its ILBM declares" pname)
                       nil bad)
                (amiga.gfx:set-a-pen rp 1))
              ;; ... and the two loaders agree piece for piece: same
              ;; bitmap contents, same mask presence.  A pack loaded
              ;; the slow way is the reference.
              (let ((chunky-walls (let ((*wall-load-planar* nil))
                                    (%load-wall-assets rp nil)))
                    (mismatched '()))
                (maphash
                 (lambda (key entry)
                   (let ((other (gethash key chunky-walls)))
                     (unless (eq (not (cdr entry)) (not (cdr other)))
                       (push (list key :mask) mismatched))))
                 walls)
                (check (format nil "~A: both loaders agree on which ~
pieces need a mask" pname)
                       nil mismatched)
                (%free-wall-assets chunky-walls)))
            (%free-wall-assets walls)))))))))))

;; *autoplay* drives a full unattended PLAY-AMIGA session: scripted keys
;; are fed one per INTUITICK (~10/s), ending in #\q so the event loop
;; exits on its own.  Verifies the whole real event path — window, menu
;; strip, redraws, key dispatch — with no user at the keyboard.  The
;; script also opens the help page (h) and leaves it (Esc), enters map
;; mode (m), toggles the debug full view (f) twice, opens help from
;; the map view too (? — the second h returns to the map) and leaves
;; map mode (m) before quitting.
;; The scripted keys also open a character sheet (1), switch to another
;; roster slot (2) and leave it (:esc) — exercising the whole :sheet
;; mode through the real event loop.  The fixture crypt is a :DARK
;; zone, so the whole session renders at the one-cell darkness view
;; depth.  This session keeps :DISPLAY :WINDOW — the Workbench-window
;; development view — alive as a smoke test (it only checks the
;; session reaches :DONE, so it isn't sensitive to the Workbench
;; geometry variability that gates the detailed layout checks behind
;; :LAMBDA-TALE-WINDOW-TESTS above); the rest of the suite runs
;; :DISPLAY :SCREEN, the game's presentation.
#+amigaos
(check "amiga-ui autoplay plays a scripted session and quits" :done
       (let ((*autoplay* (list #\w #\d #\1 #\2 :esc #\w #\a
                               #\h :esc
                               #\m #\f #\f #\? #\h #\m #\s #\q)))
         (play-amiga "tests/world/crypt.map" :display :window)
         :done))

;; The keep: an unattended session first casts through the real event
;; loop — open the cast menu (c), pick Wanda the wizard (2), cast the
;; flame (1), then the compass (4: the band draws the rose and the
;; map footer shows the facing for the rest of the session) — and
;; Wilhelm strikes up the march through the sing menu (p, 1, 1 — his
;; one tune).  Then it saves and reloads through the slot picker (S,
;; n, type "t1", Return; L, 1 — the whole name-entry and re-wire path
;; through real vanillakeys), turns to the shoppe door (a LOCATION
;; special), shops for real — pick a hero (1), buy the sword (1) and
;; a torch (2), flip to the sell page (s), sell the sword again (1),
;; back out (Esc Esc) — walks east to the tavern, where Wilhelm's
;; drink (1) brings his tunes back (Esc leaves), drops down the stairs
;; into the crypt (a :DARK zone — Wanda's flame is what keeps the view
;; lit) and burns the bought torch through the use menu (u, hero 1,
;; item 1) before quitting.
#+amigaos
(check "amiga-ui autoplay casts, saves, shops, drops to the dark crypt"
       :done
       (let ((*autoplay* (list #\c #\2 #\1
                               #\c #\2 #\4
                               #\p #\1 #\1
                               #\S #\n #\t #\1 #\Return
                               #\L #\1
                               #\d #\w
                               #\1 #\1 #\2 #\s #\1 :esc :esc
                               #\w #\w #\1 :esc
                               #\w #\w
                               #\u #\1 #\1 #\q))
             ;; scratch save, like every other test's tests/tmp-* state —
             ;; keeps the real saves/ dir untouched by the test suite
             (*save-dir* "tests/tmp-saves/"))
         (play-amiga "tests/world/keep.map" :display :screen)
         (when (probe-file "tests/tmp-saves/t1.sav")
           (delete-file "tests/tmp-saves/t1.sav"))
         :done))

;; The same unattended session on an own custom screen (:display :screen)
;; — the whole open-screen/backdrop-window/menus/event-loop path, with
;; the tile pack named explicitly (:gfx-dir): the lores depth-5 screen,
;; the pack palette and the ceiling/floor backdrop all draw for real.
#+amigaos
(check "amiga-ui autoplay on an own custom screen" :done
       (let ((*autoplay* (list #\w #\d #\m #\m #\q)))
         (play-amiga "tests/world/crypt.map" :display :screen
                                             :gfx-dir (engine-path "data/gfx/"))
         :done))

;; The hires profile end to end: its 640x256 depth-4 screen, the
;; 240x130 viewport and the data/gfx-hires pack, through the same
;; scripted event loop.
#+amigaos
(check "amiga-ui autoplay on the hires profile" :done
       (let ((*autoplay* (list #\w #\d #\m #\m #\q)))
         (play-amiga "tests/world/crypt.map" :display :screen
                                             :profile :hires)
         :done))

;; Mouse control through the same scripted loop: (:CLICK X Y) entries
;; resolve through the live hotspot map exactly like a left click —
;; the view's walk zones, a roster row (opens that character sheet)
;; and the sheet's click-anywhere-else Esc.  The lores custom screen
;; lays out deterministically (borderless backdrop, pads 10/10, the
;; 120x112 view at 10,10; topaz-8 rows put party row 1 at y=150), so
;; the script clicks absolute pixels.
#+amigaos
(check "amiga-ui autoplay drives the game by mouse clicks" :done
       (let ((*autoplay* (list '(:click 90 60)   ; view middle: forward
                               '(:click 20 60)   ; left quarter: turn
                               '(:click 15 152)  ; roster row 1: sheet
                               '(:click 90 60)   ; off-target: Esc back
                               #\q)))
         (play-amiga "tests/world/crypt.map" :display :screen
                                             :gfx-dir (engine-path "data/gfx/"))
         :done))

;;; ---------------------------------------------------------------------
;;; The debug log (src/debug-log.lisp): a timestamped trace file —
;;; image/map/campaign loads with durations, emitted events, key
;;; presses — switchable at runtime and free when off.

(defun %slurp-file (path)
  (with-open-file (s path)
    (let ((out (make-string-output-stream)))
      (loop for line = (read-line s nil nil)
            while line
            do (progn (write-string line out)
                      (write-char #\Newline out)))
      (get-output-stream-string out))))

(let ((path "tests/tmp-saves/debug.log"))
  (when (probe-file path) (delete-file path))
  (check "debug log is off by default" nil (debug-log-enabled-p))
  (dlog "never written")
  (check-true "dlog while disabled writes no file"
              (not (probe-file path)))
  (check "enable returns the path" path (debug-log-enable path))
  (check-true "enabled-p reports the open log" (debug-log-enabled-p))
  (dlog "hello ~A ~D" "world" 42)
  (check "dlog-timed returns its body's value" 3
         (dlog-timed ("timed block") (+ 1 2)))
  (check "dlog-timed passes multiple values through" '(:a :b)
         (multiple-value-list (dlog-timed ("mv block") (values :a :b))))
  ;; events trace with brief arguments and the handler count
  (let ((g (new-game (parse-map *art* :name "log fixture"))))
    (on-event g :ping (lambda (gm n) (declare (ignore gm n))))
    (emit g :ping 7)
    (emit g :enter-zone (game-map g)))
  ;; the loaders leave timed lines
  (load-map-file "tests/world/keep.map")
  (read-ilbm (engine-path "data/gfx/ceiling.iff"))
  (debug-log-disable)
  (check "disable closes the log" nil (debug-log-enabled-p))
  (let ((text (%slurp-file path)))
    (check-true "the log opens with the session banner"
                (search "debug log enabled" text))
    (check-true "lines carry a bracketed timestamp"
                (eql #\[ (char text 0)))
    (check-true "the timestamp has a millisecond fraction"
                (let ((dot (position #\. text)))
                  (and dot (< dot (position #\] text)))))
    (check-true "dlog formats its arguments"
                (search "hello world 42" text))
    (check-true "dlog-timed writes the begin line"
                (search "timed block ..." text))
    (check-true "dlog-timed writes the timed done line"
                (search "timed block done [" text))
    (check-true "done lines carry a millisecond duration"
                (search " ms]" text))
    (check-true "an event line names topic, args and handler count"
                (search "event :PING 7 handlers=1" text))
    (check-true "event arguments print briefly"
                (search "#<map log fixture>" text))
    (check-true "a map load leaves a timed line"
                (search "map tests/world/keep.map done [" text))
    (check-true "an image load leaves a timed line"
                (search "ceiling.iff done [" text))
    (check-true "the log closes with the banner"
                (search "debug log disabled" text)))
  ;; off means off: nothing is written; re-enabling appends
  (let ((len (length (%slurp-file path))))
    (dlog "dropped")
    (check "dlog after disable writes nothing"
           len (length (%slurp-file path)))
    (debug-log-enable path)
    (debug-log-disable)
    (check-true "re-enabling appends a second session"
                (> (length (%slurp-file path)) len)))
  (delete-file path))

;; Regression: the whole-second fields (from GET-UNIVERSAL-TIME) and the
;; millisecond field (from GET-INTERNAL-REAL-TIME) once came from two
;; clocks with unrelated epochs, so the printed ".mmm" did not actually
;; belong to the printed HH:MM:SS.  Pin the wall-clock anchor to a known
;; moment and confirm the printed date/time matches DECODE-UNIVERSAL-TIME
;; of that exact anchor — that only holds when both fields derive from
;; the same anchored clock.
(let ((path "tests/tmp-saves/debug-clock.log"))
  (when (probe-file path) (delete-file path))
  (debug-log-enable path)
  (setf *debug-log-anchor-universal* 1000000000
        *debug-log-anchor-real* (get-internal-real-time))
  (dlog "clock check")
  (debug-log-disable)
  (let ((text (%slurp-file path)))
    (multiple-value-bind (sec min hour day month year)
        (decode-universal-time 1000000000)
      (check-true "sec/min/hour and the ms fraction derive from the same anchored clock"
                  (search (format nil "~4,'0D-~2,'0D-~2,'0D ~2,'0D:~2,'0D:~2,'0D."
                                  year month day hour min sec)
                          text))))
  (delete-file path))

;; The debug log traces a real session end to end: the session line,
;; every key through the real event loop, the wall-pack image loads
;; and the emitted events.
#+amigaos
(check-true "amiga-ui autoplay leaves a debug-log trace"
            (let ((path "tests/tmp-saves/debug-amiga.log")
                  (*autoplay* (list #\w #\q)))
              (when (probe-file path) (delete-file path))
              (debug-log-enable path)
              (play-amiga "tests/world/crypt.map" :display :screen)
              (debug-log-disable)
              (let ((text (%slurp-file path)))
                (prog1 (and (search "play-amiga tests/world/crypt.map" text)
                            (search "key #\\w mode :PLAY" text)
                            (search "wall pack" text)
                            (search "image" text)
                            (search "event :" text)
                            t)
                  (delete-file path)))))

;;; ---------------------------------------------------------------------
;;; Summary

(format t "~%Lambda's Tale engine tests: ~D checks, ~D failures.~%"
        *checks* *failures*)
(cl-user::quit (if (zerop *failures*) 0 1))
