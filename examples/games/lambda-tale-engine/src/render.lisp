;;; Lambda's Tale — ASCII map renderer (the automap view).
;;;
;;; Renders a dungeon map back into the same character conventions the
;;; parser reads: '-' '|' walls, 'D' doors, '+' corners, feature characters
;;; in cell interiors, and the party as a facing arrow (^ > v <).
;;;
;;; With a MAP-KNOWLEDGE the output is filtered to what the party has
;;; actually seen (the player automap); without one it is omniscient
;;; (the map-authoring / debug view).

(in-package :tale)

;;; ---------------------------------------------------------------------
;;; Character-grid helpers (shared with the first-person renderer)

(defun %make-grid (width height)
  (make-array (list height width) :initial-element #\Space))

(defun %grid-put (grid x y ch)
  (when (and (<= 0 y) (< y (array-dimension grid 0))
             (<= 0 x) (< x (array-dimension grid 1)))
    (setf (aref grid y x) ch)))

(defun %grid->string (grid)
  "Assemble GRID into a multi-line string, right-trimming each line."
  (let ((rows (array-dimension grid 0))
        (cols (array-dimension grid 1)))
    (with-output-to-string (s)
      (dotimes (r rows)
        (let ((last -1))
          (dotimes (c cols)
            (unless (char= (aref grid r c) #\Space)
              (setf last c)))
          (dotimes (c (1+ last))
            (write-char (aref grid r c) s)))
        (when (< r (1- rows))
          (write-char #\Newline s))))))

(defun beside (left right &key (gap 3))
  "Compose two multi-line strings side by side."
  (let* ((ll (%split-lines left))
         (rl (%split-lines right))
         (lw (let ((m 0))
               (dolist (l ll m)
                 (setf m (max m (length l))))))
         (n (max (length ll) (length rl))))
    (with-output-to-string (s)
      (dotimes (i n)
        (let ((l (or (nth i ll) ""))
              (r (or (nth i rl) "")))
          (write-string l s)
          (unless (zerop (length r))
            (dotimes (j (- (+ lw gap) (length l)))
              (write-char #\Space s))
            (write-string r s)))
        (when (< i (1- n))
          (write-char #\Newline s))))))

;;; ---------------------------------------------------------------------
;;; Automap

(defun render-dungeon (map &key knowledge px py facing
                                (x0 0) (y0 0) w h)
  "Render MAP as a multi-line string.
KNOWLEDGE non-NIL filters to explored cells and seen walls.
PX/PY/FACING draw the party arrow.
X0/Y0/W/H render only that cell region (default: the whole map) —
the full map view uses this with MAP-VIEWPORT's result when the map
is larger than the display."
  (let* ((w (or w (dungeon-map-width map)))
         (h (or h (dungeon-map-height map)))
         (grid (%make-grid (1+ (* 2 w)) (1+ (* 2 h)))))
    (labels ((put (col row ch)
               (%grid-put grid col row ch))
             (draw-edge (x y dir)
               (let* ((i (dir-index dir))
                      (horizontal (or (= i +north+) (= i +south+)))
                      (wall (cell-wall map x y i))
                      (ch (case wall
                            (:wall (if horizontal #\- #\|))
                            (:door #\D)
                            (t nil)))
                      (cx (1+ (* 2 (- x x0))))
                      (cy (1+ (* 2 (- y y0)))))
                 (when ch
                   (multiple-value-bind (col row)
                       (ecase i
                         (0 (values cx (1- cy)))
                         (2 (values cx (1+ cy)))
                         (3 (values (1- cx) cy))
                         (1 (values (1+ cx) cy)))
                     (put col row ch)
                     ;; corners at the two ends of the edge
                     (if horizontal
                         (progn (put (1- col) row #\+)
                                (put (1+ col) row #\+))
                         (progn (put col (1- row) #\+)
                                (put col (1+ row) #\+))))))))
      (dotimes (ry h)
        (dotimes (rx w)
          (let ((x (+ rx x0))
                (y (+ ry y0)))
            (dotimes (d 4)
              (when (or (null knowledge) (wall-known-p knowledge x y d))
                (draw-edge x y d)))
            (when (or (null knowledge) (cell-explored-p knowledge x y))
              (let ((f (cell-feature map x y)))
                (when f
                  (put (1+ (* 2 rx)) (1+ (* 2 ry)) f)))))))
      (when (and px py
                 (<= x0 px) (< px (+ x0 w))
                 (<= y0 py) (< py (+ y0 h)))
        (put (1+ (* 2 (- px x0))) (1+ (* 2 (- py y0)))
             (char *dir-arrows* (dir-index (or facing +north+)))))
      (%grid->string grid))))

;;; ---------------------------------------------------------------------
;;; Automap wall runs: the map's wall geometry as merged straight
;;; segments.  The Amiga map page draws one OS line call per run
;;; instead of one per cell edge — a 30x30 city's long street walls
;;; collapse from thousands of per-edge draws into a few hundred runs,
;;; the difference between seconds and a blink at 14MHz.  Pure map
;;; math, no OS calls, so the host suite covers it.

(defun map-edge-runs (map knowledge x0 y0 vw vh)
  "The walls of cells [X0,X0+VW) x [Y0,Y0+VH) of MAP as merged runs,
filtered to what KNOWLEDGE holds (NIL = omniscient).  Returns
\(VALUES WALL-RUNS DOOR-RUNS); a run is (HORIZONTAL EX EY LEN) in
region-relative edge coordinates: a horizontal run spans cell columns
EX..EX+LEN-1 along edge row EY (the boundary above cell row EY), a
vertical run spans cell rows EY..EY+LEN-1 along edge column EX (the
boundary left of cell column EX).  Where the two cells sharing an
edge disagree (one-way walls), the south/east cell's wall wins — the
same result as drawing every cell's own four walls in row-major
order, where the later draw lands on top."
  (let* ((mw (dungeon-map-width map))
         (walls (dungeon-map-walls map))
         (bits (and knowledge (map-knowledge-bits knowledge)))
         (wall-runs '())
         (door-runs '()))
    (labels ((code (x y d)
               ;; cell (X,Y)'s wall byte toward D — 0 (:open) unless
               ;; KNOWLEDGE has seen it
               (if (or (null bits) (logbitp d (aref bits y x)))
                   (aref walls (+ d (* 4 (+ (* y mw) x))))
                   0))
             (edge (near-p x1 y1 d1 far-p x2 y2 d2)
               ;; the edge's wall byte: the near (south/east) cell's
               ;; when it shows one, else the far cell's
               (let ((c (if near-p (code x1 y1 d1) 0)))
                 (if (and (zerop c) far-p) (code x2 y2 d2) c)))
             (add (code run)
               (cond ((zerop code))
                     ((= code 2) (push run door-runs))
                     (t (push run wall-runs)))))
      ;; horizontal edge rows: EY = 0..VH, the boundary above cell row EY
      (dotimes (ey (1+ vh))
        (let ((run-code 0)
              (run-start 0))
          (dotimes (ex (1+ vw))         ; one past the end flushes the row
            (let ((c (if (< ex vw)
                         (edge (< ey vh) (+ x0 ex) (+ y0 ey) +north+
                               (plusp ey) (+ x0 ex) (+ y0 ey -1) +south+)
                         0)))
              (unless (= c run-code)
                (add run-code (list t run-start ey (- ex run-start)))
                (setf run-code c run-start ex))))))
      ;; vertical edge columns: EX = 0..VW, the boundary left of column EX
      (dotimes (ex (1+ vw))
        (let ((run-code 0)
              (run-start 0))
          (dotimes (ey (1+ vh))
            (let ((c (if (< ey vh)
                         (edge (< ex vw) (+ x0 ex) (+ y0 ey) +west+
                               (plusp ex) (+ x0 ex -1) (+ y0 ey) +east+)
                         0)))
              (unless (= c run-code)
                (add run-code (list nil ex run-start (- ey run-start)))
                (setf run-code c run-start ey)))))))
    (values (nreverse wall-runs) (nreverse door-runs))))

;;; ---------------------------------------------------------------------
;;; The map legend: the locations the party has found, as marker + name.

(defparameter *legend-markers* "123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"
  "Marker characters dealt out to legend entries, in order.")

(defun map-legend-entries (map knowledge &key full)
  "MAP's discovered locations as legend entries (MARKER X Y TITLE),
MARKER a character from *LEGEND-MARKERS*.  A location counts as found
once its cell is explored — the party has stepped inside.  FULL
non-NIL lists every location (the omniscient debug map).  Important
places first: locations whose kind is not :HOUSE (shops, taverns,
temples) precede the plain houses; within each group map order (top
to bottom, left to right).  Entries beyond the marker alphabet are
dropped."
  (let ((cells '()))
    (maphash (lambda (cell ops)
               (declare (ignore ops))
               (push cell cells))
             (dungeon-map-specials map))
    ;; the specials hash has no stable order — impose map order
    (setf cells (sort cells (lambda (a b)
                              (or (< (cdr a) (cdr b))
                                  (and (= (cdr a) (cdr b))
                                       (< (car a) (car b)))))))
    (let ((important '())
          (houses '()))
      (dolist (cell cells)
        (let* ((x (car cell))
               (y (cdr cell))
               (loc (cell-location-op map x y)))
          (when (and loc
                     (or full
                         (and knowledge (cell-explored-p knowledge x y))))
            (if (eq (second loc) :house)
                (push (list x y (first loc)) houses)
                (push (list x y (first loc)) important)))))
      (let ((entries (append (nreverse important) (nreverse houses)))
            (i -1))
        (mapcar (lambda (e) (cons (char *legend-markers* (incf i)) e))
                (subseq entries 0 (min (length entries)
                                       (length *legend-markers*))))))))

(defun render-game (game &key full)
  "Render GAME's map with the party arrow.  FULL non-NIL shows the whole
map (debug view); otherwise only what the party has explored."
  (render-dungeon (game-map game)
                  :knowledge (if full nil (game-knowledge game))
                  :px (game-x game)
                  :py (game-y game)
                  :facing (game-facing game)))
