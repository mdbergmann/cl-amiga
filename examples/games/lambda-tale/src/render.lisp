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

(defun render-dungeon (map &key knowledge px py facing)
  "Render MAP as a multi-line string.
KNOWLEDGE non-NIL filters to explored cells and seen walls.
PX/PY/FACING draw the party arrow."
  (let* ((w (dungeon-map-width map))
         (h (dungeon-map-height map))
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
                      (cx (1+ (* 2 x)))
                      (cy (1+ (* 2 y))))
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
      (dotimes (y h)
        (dotimes (x w)
          (dotimes (d 4)
            (when (or (null knowledge) (wall-known-p knowledge x y d))
              (draw-edge x y d)))
          (when (or (null knowledge) (cell-explored-p knowledge x y))
            (let ((f (cell-feature map x y)))
              (when f
                (put (1+ (* 2 x)) (1+ (* 2 y)) f))))))
      (when (and px py)
        (put (1+ (* 2 px)) (1+ (* 2 py))
             (char *dir-arrows* (dir-index (or facing +north+)))))
      (%grid->string grid))))

(defun render-game (game &key full)
  "Render GAME's map with the party arrow.  FULL non-NIL shows the whole
map (debug view); otherwise only what the party has explored."
  (render-dungeon (game-map game)
                  :knowledge (if full nil (game-knowledge game))
                  :px (game-x game)
                  :py (game-y game)
                  :facing (game-facing game)))
