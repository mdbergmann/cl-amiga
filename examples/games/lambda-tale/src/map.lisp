;;; Lambda's Tale — dungeon map model.
;;;
;;; A map is a W x H grid of cells.  Each cell stores its own four walls
;;; (:wall, :door or :open), one per direction.  Walls are stored per cell,
;;; not per shared edge: the ASCII map parser writes both sides of an edge
;;; from the same source character so they start out consistent, but the
;;; movement rules only ever consult the wall of the cell you are standing
;;; in — which makes one-way (phantom) walls expressible later, a genuine
;;; Bard's Tale feature.
;;;
;;; Map source format (see data/*.map): ASCII art on a (2W+1) x (2H+1)
;;; character grid.
;;;   - even column, even row   corner; any character, ignored
;;;   - odd  column, even row   horizontal edge: '-' wall, 'D' door, ' ' open
;;;   - even column, odd  row   vertical edge:   '|' wall, 'D' door, ' ' open
;;;   - odd  column, odd  row   cell interior:
;;;         ' '            nothing
;;;         @              party start position (facing from :start-facing)
;;;         anything else  stored as the cell's feature character
;;;                        (convention: '>' stairs down, '<' stairs up)
;;; Short lines are padded with spaces (missing edge characters read as open).
;;;
;;; After the art a map file may carry Lisp data forms (the story layer);
;;; see LOAD-MAP-FILE below and specials.lisp for the op vocabulary.

(in-package :tale)

;;; ---------------------------------------------------------------------
;;; Directions

(defconstant +north+ 0)
(defconstant +east+  1)
(defconstant +south+ 2)
(defconstant +west+  3)

(defparameter *dir-keywords* #(:north :east :south :west))
(defparameter *dir-dx* #(0 1 0 -1))
(defparameter *dir-dy* #(-1 0 1 0))
(defparameter *dir-arrows* "^>v<")

(defun dir-index (dir)
  "Normalize DIR (keyword or index 0..3) to an index."
  (cond ((and (integerp dir) (<= 0 dir 3)) dir)
        ((position dir *dir-keywords*))
        (t (error "Unknown direction: ~S (use :north :east :south :west or 0..3)"
                  dir))))

(defun dir-keyword (dir)
  (aref *dir-keywords* (dir-index dir)))

(defun dir-opposite (dir)
  (mod (+ (dir-index dir) 2) 4))

(defun turn-dir (dir delta)
  "Rotate DIR by DELTA quarter turns clockwise (negative = counter-clockwise)."
  (mod (+ (dir-index dir) delta) 4))

;;; ---------------------------------------------------------------------
;;; Map structure

(defstruct (dungeon-map (:constructor %make-dungeon-map))
  (name "unnamed")
  (width 0)
  (height 0)
  (wrap nil)          ; T = Bard's Tale-style toroidal map
  walls               ; (array (height width 4)) of :wall/:door/:open
  features            ; (array (height width)) of character or NIL
  specials            ; hash (x . y) -> special ops list (see specials.lisp)
  (start-x 0)
  (start-y 0)
  (start-facing :north))

(defun cell-wall (map x y dir)
  "The wall of cell (X,Y) in direction DIR, as seen from inside the cell."
  (aref (dungeon-map-walls map) y x (dir-index dir)))

(defun cell-feature (map x y)
  "The feature character of cell (X,Y), or NIL."
  (aref (dungeon-map-features map) y x))

(defun cell-special (map x y)
  "The special ops attached to cell (X,Y), or NIL.  SETF-able."
  (gethash (cons x y) (dungeon-map-specials map)))

(defun (setf cell-special) (ops map x y)
  (setf (gethash (cons x y) (dungeon-map-specials map)) ops))

(defun wall-passable-p (wall)
  (or (eq wall :open) (eq wall :door)))

(defun neighbor (map x y dir)
  "Coordinates of the cell adjacent to (X,Y) in DIR as (values NX NY).
Wrapping maps wrap around the edges; otherwise returns NIL off-map."
  (let* ((i (dir-index dir))
         (nx (+ x (aref *dir-dx* i)))
         (ny (+ y (aref *dir-dy* i)))
         (w (dungeon-map-width map))
         (h (dungeon-map-height map)))
    (cond ((dungeon-map-wrap map)
           (values (mod nx w) (mod ny h)))
          ((and (<= 0 nx) (< nx w) (<= 0 ny) (< ny h))
           (values nx ny))
          (t nil))))

;;; ---------------------------------------------------------------------
;;; Parsing ASCII map art

(defun %split-lines (string)
  (let ((lines '())
        (start 0))
    (dotimes (i (length string))
      (when (char= (char string i) #\Newline)
        (push (subseq string start i) lines)
        (setf start (1+ i))))
    (push (subseq string start) lines)
    (nreverse lines)))

(defun %blank-line-p (line)
  (every (lambda (c) (char= c #\Space)) line))

(defun %trim-blank-lines (lines)
  (let ((lines (copy-list lines)))
    (loop while (and lines (%blank-line-p (first lines)))
          do (pop lines))
    (setf lines (nreverse lines))
    (loop while (and lines (%blank-line-p (first lines)))
          do (pop lines))
    (nreverse lines)))

(defun parse-map (art &key (name "unnamed") wrap (start-facing :north))
  "Parse ASCII map ART (see file header for the format) into a DUNGEON-MAP."
  (let* ((lines (coerce (%trim-blank-lines (%split-lines art)) 'vector))
         (rows (length lines))
         (cols (let ((m 0))
                 (dotimes (i rows m)
                   (setf m (max m (length (aref lines i))))))))
    (unless (and (>= rows 3) (oddp rows) (>= cols 3) (oddp cols))
      (error "parse-map ~A: art must be a (2*W+1) x (2*H+1) character grid; ~
              got ~D lines with a maximum width of ~D" name rows cols))
    (let* ((h (floor (1- rows) 2))
           (w (floor (1- cols) 2))
           (walls (make-array (list h w 4) :initial-element :wall))
           (features (make-array (list h w) :initial-element nil))
           (start-x nil)
           (start-y nil))
      (setf start-facing (dir-keyword start-facing))
      (labels ((art-at (col row)
                 (let ((line (aref lines row)))
                   (if (< col (length line)) (char line col) #\Space)))
               (wall-value (col row horizontal)
                 (let ((c (art-at col row)))
                   (cond ((char= c #\Space) :open)
                         ((and horizontal (char= c #\-)) :wall)
                         ((and (not horizontal) (char= c #\|)) :wall)
                         ((char-equal c #\D) :door)
                         (t (error "parse-map ~A: invalid ~:[vertical~;horizontal~] ~
                                    wall character ~S at line ~D, column ~D"
                                   name horizontal c (1+ row) (1+ col)))))))
        (dotimes (y h)
          (dotimes (x w)
            (let ((cx (1+ (* 2 x)))
                  (cy (1+ (* 2 y))))
              (setf (aref walls y x +north+) (wall-value cx (1- cy) t))
              (setf (aref walls y x +south+) (wall-value cx (1+ cy) t))
              (setf (aref walls y x +west+)  (wall-value (1- cx) cy nil))
              (setf (aref walls y x +east+)  (wall-value (1+ cx) cy nil))
              (let ((c (art-at cx cy)))
                (case c
                  (#\Space nil)
                  (#\@ (setf start-x x start-y y))
                  (t (setf (aref features y x) c)))))))
        (%make-dungeon-map :name name :width w :height h :wrap wrap
                           :walls walls :features features
                           :specials (make-hash-table :test 'equal)
                           :start-x (or start-x 0)
                           :start-y (or start-y 0)
                           :start-facing start-facing)))))

(defun %apply-map-form (map form path)
  (unless (and (consp form) (symbolp (first form)))
    (error "~A: invalid map form ~S (expected (special (x y) op...))"
           path form))
  (cond ((string-equal (symbol-name (first form)) "SPECIAL")
         (destructuring-bind ((x y) &rest ops) (rest form)
           (unless (and (integerp x) (< -1 x (dungeon-map-width map))
                        (integerp y) (< -1 y (dungeon-map-height map)))
             (error "~A: special cell (~S ~S) is outside the ~Dx~D map"
                    path x y
                    (dungeon-map-width map) (dungeon-map-height map)))
           (setf (cell-special map x y) ops)))
        (t (error "~A: unknown map form ~S (expected (special (x y) op...))"
                  path (first form)))))

(defun %parse-map-forms (map string path)
  (with-input-from-string (in string)
    (let ((*read-eval* nil)
          (*package* (find-package :tale)))
      (loop
        (let ((form (read in nil in)))
          (when (eq form in)
            (return))
          (%apply-map-form map form path))))))

(defun load-map-file (path &key wrap (start-facing :north))
  "Read the ASCII map file at PATH and parse it into a DUNGEON-MAP.
After the art the file may carry Lisp data forms — the story layer of
the map, read with *READ-EVAL* bound to NIL and never evaluated:
    (special (X Y) OP...)    attach a special to cell (X,Y)
The forms section starts at the first line beginning with '(' or ';'
\(no valid art line starts with either)."
  (let ((art (make-string-output-stream))
        (forms (make-string-output-stream))
        (in-forms nil))
    (with-open-file (s path)
      (loop for line = (read-line s nil nil)
            while line
            do (progn
                 (when (and (not in-forms)
                            (> (length line) 0)
                            (member (char line 0) '(#\( #\;)))
                   (setf in-forms t))
                 (let ((out (if in-forms forms art)))
                   (write-string line out)
                   (write-char #\Newline out)))))
    (let ((map (parse-map (get-output-stream-string art)
                          :name path :wrap wrap :start-facing start-facing)))
      (%parse-map-forms map (get-output-stream-string forms) path)
      map)))
