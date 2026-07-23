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
  (name "unnamed")    ; identity: the file path the map was loaded from
  (kind :dungeon)     ; zone kind: :dungeon, :city, ... (map data, open set)
  title               ; display name from the ZONE form, or NIL
  (width 0)
  (height 0)
  (wrap nil)          ; T = Bard's Tale-style toroidal map
  walls               ; (unsigned-byte 8) vector, H*W*4 wall codes in
                      ;   N,E,S,W order per cell (see *WALL-DECODE*) —
                      ;   packed so the map cache loads it in one read
  features            ; (unsigned-byte 8) vector, H*W char-codes, 0 = none
  specials            ; hash (x . y) -> special ops list (see specials.lisp)
  (start-x 0)
  (start-y 0)
  (start-facing :north)
  gfx                 ; zone's tile-pack dir from (ZONE :GFX ...), or NIL
  dark                ; always dark (ZONE :DARK D) — needs a light;
                      ; T = one cell of sight, an integer = that many
  sky                 ; (ZONE :SKY (R G B)) noon sky colour, or NIL for
                      ;   *DEFAULT-SKY* (see SKY-COLOR-FOR)
  ground)             ; (ZONE :GROUND (R G B)) noon ground colour, or NIL

(defparameter *wall-decode* #(:open :wall :door)
  "Wall byte codes, index = code — the packed walls representation and
the .mapc sidecar share it.")

(defun %wall-code (wall)
  (ecase wall (:open 0) (:wall 1) (:door 2)))

(defun map-title (map)
  "The map's display name: its ZONE :title, else its file name."
  (or (dungeon-map-title map) (dungeon-map-name map)))

(defun cell-wall (map x y dir)
  "The wall of cell (X,Y) in direction DIR, as seen from inside the cell."
  (svref *wall-decode*
         (aref (dungeon-map-walls map)
               (+ (dir-index dir)
                  (* 4 (+ (* y (dungeon-map-width map)) x))))))

(defun cell-feature (map x y)
  "The feature character of cell (X,Y), or NIL."
  (let ((c (aref (dungeon-map-features map)
                 (+ (* y (dungeon-map-width map)) x))))
    (unless (zerop c)
      (code-char c))))

(defvar *building-styles* nil
  "One-entry cache (MAP . STYLE-VECTOR) of the per-building wall styles
the first-person view deals out — filled by %BUILDING-STYLES in
view.lisp, which cannot own the variable because attaching a special
(below) has to drop the cache: a LOCATION op's :STYLE is what pins a
building's look.")

(defun cell-special (map x y)
  "The special ops attached to cell (X,Y), or NIL.  SETF-able."
  (gethash (cons x y) (dungeon-map-specials map)))

(defun (setf cell-special) (ops map x y)
  (setf *building-styles* nil)          ; a LOCATION op may pin a style
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

(defun map-viewport (map px py view-w view-h)
  "The VIEW-W x VIEW-H cell window into MAP centered on (PX,PY), clamped
to the map bounds — the full map view uses it to show what fits around
the party (see specs/ui-and-engine.md).  Returns (values X0 Y0 W H) —
the top-left cell and the actual size, smaller than requested when the
map itself is smaller."
  (let* ((mw (dungeon-map-width map))
         (mh (dungeon-map-height map))
         (w (min view-w mw))
         (h (min view-h mh))
         (x0 (max 0 (min (- px (floor w 2)) (- mw w))))
         (y0 (max 0 (min (- py (floor h 2)) (- mh h)))))
    (values x0 y0 w h)))

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
           (walls (make-array (* h w 4)
                              :element-type '(unsigned-byte 8)
                              :initial-element 1))         ; 1 = :wall
           (features (make-array (* h w)
                                 :element-type '(unsigned-byte 8)
                                 :initial-element 0))
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
                  (cy (1+ (* 2 y)))
                  (base (* 4 (+ (* y w) x))))
              (setf (aref walls (+ base +north+))
                    (%wall-code (wall-value cx (1- cy) t)))
              (setf (aref walls (+ base +south+))
                    (%wall-code (wall-value cx (1+ cy) t)))
              (setf (aref walls (+ base +west+))
                    (%wall-code (wall-value (1- cx) cy nil)))
              (setf (aref walls (+ base +east+))
                    (%wall-code (wall-value (1+ cx) cy nil)))
              (let ((c (art-at cx cy)))
                (case c
                  (#\Space nil)
                  (#\@ (setf start-x x start-y y))
                  (t (let ((code (char-code c)))
                       (unless (<= 1 code 255)
                         (error "parse-map ~A: feature character ~S at ~
cell (~D,~D) is not an 8-bit character" name c x y))
                       (setf (aref features (+ (* y w) x)) code))))))))
        (%make-dungeon-map :name name :width w :height h :wrap wrap
                           :walls walls :features features
                           :specials (make-hash-table :test 'equal)
                           :start-x (or start-x 0)
                           :start-y (or start-y 0)
                           :start-facing start-facing)))))

(defun %zone-color (path key spec)
  "Validate SPEC, a zone (ZONE KEY ...) colour — a list or vector of
three 0-255 components — and return it as an (R G B) list.  KEY is :SKY
or :GROUND, for the error message."
  (let ((rgb (cond ((and (listp spec) (= (length spec) 3)) spec)
                   ((and (vectorp spec) (= (length spec) 3))
                    (coerce spec 'list))
                   (t (error "~A: zone ~S ~S must be three 0-255 ~
components, e.g. (102 170 204) or #(102 170 204)" path key spec)))))
    (dolist (c rgb rgb)
      (unless (and (integerp c) (<= 0 c 255))
        (error "~A: zone ~S component ~S must be an integer 0-255"
               path key c)))))

(defun %apply-map-form (map form path)
  (unless (and (consp form) (symbolp (first form)))
    (error "~A: invalid map form ~S (expected (zone ...) or ~
            (special (x y) op...))"
           path form))
  (cond ((string-equal (symbol-name (first form)) "SPECIAL")
         (destructuring-bind ((x y) &rest ops) (rest form)
           (unless (and (integerp x) (< -1 x (dungeon-map-width map))
                        (integerp y) (< -1 y (dungeon-map-height map)))
             (error "~A: special cell (~S ~S) is outside the ~Dx~D map"
                    path x y
                    (dungeon-map-width map) (dungeon-map-height map)))
           (setf (cell-special map x y) ops)))
        ((string-equal (symbol-name (first form)) "ZONE")
         (destructuring-bind (&key kind title wrap start-facing gfx dark
                                   sky ground)
             (rest form)
           (when kind
             (unless (keywordp kind)
               (error "~A: zone :kind ~S must be a keyword (:city, :dungeon, ...)"
                      path kind))
             (setf (dungeon-map-kind map) kind))
           (when title (setf (dungeon-map-title map) title))
           (when wrap (setf (dungeon-map-wrap map) wrap))
           (when start-facing
             (setf (dungeon-map-start-facing map)
                   (dir-keyword start-facing)))
           (when gfx
             (unless (stringp gfx)
               (error "~A: zone :gfx ~S must be a directory string ~
(e.g. \"gfx/\")" path gfx))
             (setf (dungeon-map-gfx map) gfx))
           (when dark
             (unless (or (eq dark t) (and (integerp dark) (plusp dark)))
               (error "~A: zone :dark ~S must be T (one cell of sight) ~
or a positive integer (cells of sight in the dark)" path dark))
             (setf (dungeon-map-dark map) dark))
           (when sky
             (setf (dungeon-map-sky map) (%zone-color path :sky sky)))
           (when ground
             (setf (dungeon-map-ground map)
                   (%zone-color path :ground ground)))))
        (t (error "~A: unknown map form ~S (expected (zone ...) or ~
                   (special (x y) op...))"
                  path (first form)))))

(defun %parse-map-forms-stream (map in path)
  "Read map data forms from stream IN until EOF and apply them to MAP
— *READ-EVAL* bound to NIL, forms never evaluated.  When the debug log
is enabled, leaves one line splitting the cost between READ and apply
(the story-forms leg is the bulk of a cached city load on a 68020 —
the split says where); when disabled this timing bookkeeping is
skipped entirely, same as DLOG-TIMED."
  (let ((*read-eval* nil)
        (*package* (find-package :tale))
        (forms 0))
    (if (debug-log-enabled-p)
        (let ((read-ticks 0)
              (apply-ticks 0)
              (worst-ticks 0)
              (gc0 #+cl-amiga (clamiga::%get-gc-count) #-cl-amiga 0))
          (loop
            (let* ((t0 (get-internal-real-time))
                   (form (read in nil in))
                   (t1 (get-internal-real-time)))
              (incf read-ticks (- t1 t0))
              (setf worst-ticks (max worst-ticks (- t1 t0)))
              (when (eq form in)
                (return))
              (%apply-map-form map form path)
              (incf apply-ticks (- (get-internal-real-time) t1))
              (incf forms)))
          (dlog "story forms ~A: ~D forms, read ~Dms (worst ~Dms), apply ~Dms, ~D gcs"
                path forms
                (round (* 1000 read-ticks) internal-time-units-per-second)
                (round (* 1000 worst-ticks) internal-time-units-per-second)
                (round (* 1000 apply-ticks) internal-time-units-per-second)
                (- #+cl-amiga (clamiga::%get-gc-count) #-cl-amiga 0 gc0)))
        (loop
          (let ((form (read in nil in)))
            (when (eq form in)
              (return))
            (%apply-map-form map form path)
            (incf forms))))))

(defun %parse-map-forms (map string path)
  (with-input-from-string (in string)
    (%parse-map-forms-stream map in path)))

(defun load-map-file (path &key wrap (start-facing :north))
  "Read the ASCII map file at PATH and parse it into a DUNGEON-MAP.
After the art the file may carry Lisp data forms — the story layer of
the map, read with *READ-EVAL* bound to NIL and never evaluated:
    (zone :kind KIND :title TITLE :wrap W :start-facing DIR :gfx PACK
          :dark D :sky C :ground C)
                             zone metadata: KIND is :dungeon (default),
                             :city, ... — maps self-describe what they
                             are; PACK names the zone's tile-pack
                             directory (see ZONE-GFX-DIR); :dark D makes
                             the zone dark at all hours (see GAME-DARK-P):
                             T = one cell of sight, a positive integer =
                             that many cells (see GAME-VIEW-DEPTH); :sky
                             and :ground are (R G B) colours — the zone's
                             NOON sky/ground, from which the engine tints
                             every day-band (see SKY-COLOR-FOR); omitted,
                             the zone uses *DEFAULT-SKY* / *DEFAULT-GROUND*
    (special (X Y) OP...)    attach a special to cell (X,Y)
The forms section starts at the first line beginning with '(' or ';'
\(no valid art line starts with either).  The :wrap and :start-facing
keyword arguments below are overridden by a ZONE form in the file.

The character-by-character art parse scales with map area — ~20s for a
30x30 city on a 14MHz 68020 — so a successful parse leaves a binary
sidecar (PATH + \"c\", see %WRITE-MAP-CACHE) holding the art-derived
data; while the sidecar is newer than the map it loads in one
READ-SEQUENCE instead of a reparse, and the story forms are read from
the map file itself at the recorded offset.  Editing the .map (or
deleting the .mapc) transparently reparses and rewrites the sidecar;
a read-only game directory just never caches."
  (dlog-timed ("map ~A" path)
    (or (%load-map-cache path wrap start-facing)
        (%load-map-file path wrap start-facing))))

(defun %load-map-file (path wrap start-facing)
  (let ((art (make-string-output-stream))
        (forms (make-string-output-stream))
        (forms-offset nil)
        (in-forms nil))
    (with-open-file (s path)
      (loop for pos = (file-position s)
            for line = (read-line s nil nil)
            while line
            do (progn
                 (when (and (not in-forms)
                            (> (length line) 0)
                            (member (char line 0) '(#\( #\;)))
                   (setf in-forms t
                         forms-offset pos))
                 (let ((out (if in-forms forms art)))
                   (write-string line out)
                   (write-char #\Newline out)))))
    (let ((map (parse-map (get-output-stream-string art)
                          :name path :wrap wrap :start-facing start-facing)))
      (%parse-map-forms map (get-output-stream-string forms) path)
      (%write-map-cache map path forms-offset)
      map)))

;;; ---------------------------------------------------------------------
;;; The map cache: a binary sidecar for the art-derived data.
;;;
;;; Layout (big-endian, the Amiga's native order — host-written caches
;;; work on the Amiga and vice versa):
;;;   "TMC1"                         magic + version
;;;   u16 W, u16 H                   grid size
;;;   u16 START-X, u16 START-Y       the @ cell (or 0,0)
;;;   u32 FORMS-OFFSET               byte offset of the story forms in
;;;                                  the .map file, #xFFFFFFFF = none
;;;   H*W*4 wall bytes               dirs N,E,S,W: 0 open, 1 wall, 2 door
;;;   H*W feature bytes              char-code, 0 = no feature
;;;
;;; Only what the ASCII art produces is cached; everything the ZONE and
;;; SPECIAL forms contribute (kind, title, wrap, gfx, dark, specials) is
;;; re-read from the .map file so the cache can never go stale against
;;; the story layer without also being older than the file.

(defconstant +map-cache-no-forms+ #xFFFFFFFF)

(defun %map-cache-path (path)
  (concatenate 'string path "c"))

(defun %write-map-cache (map path forms-offset)
  "Write MAP's art-derived data to the .mapc sidecar; best-effort — a
failure (read-only media, odd feature characters) is logged and the
game continues uncached."
  (handler-case
      (let* ((w (dungeon-map-width map))
             (h (dungeon-map-height map))
             (walls (dungeon-map-walls map))
             (features (dungeon-map-features map))
             (buf (make-array (+ 16 (* h w 5))
                              :element-type '(unsigned-byte 8)))
             (i 0))
        (labels ((u8 (v) (setf (aref buf i) v) (incf i))
                 (u16 (v) (u8 (ldb (byte 8 8) v)) (u8 (ldb (byte 8 0) v)))
                 (u32 (v) (u16 (ldb (byte 16 16) v)) (u16 (ldb (byte 16 0) v))))
          (u8 84) (u8 77) (u8 67) (u8 49)          ; "TMC1"
          (u16 w) (u16 h)
          (u16 (dungeon-map-start-x map))
          (u16 (dungeon-map-start-y map))
          (u32 (or forms-offset +map-cache-no-forms+))
          ;; walls and features are already packed byte vectors in
          ;; exactly the sidecar's layout — two bulk copies
          (replace buf walls :start1 16)
          (replace buf features :start1 (+ 16 (* h w 4))))
        (with-open-file (s (%map-cache-path path)
                           :direction :output
                           :element-type '(unsigned-byte 8)
                           :if-exists :supersede)
          (write-sequence buf s))
        t)
    (error (e)
      (dlog "map cache for ~A not written: ~A" path e)
      nil)))

(defun %load-map-cache (path wrap start-facing)
  "The DUNGEON-MAP from PATH's .mapc sidecar, or NIL when there is no
usable cache (missing, older than the map, wrong magic, mis-sized,
corrupt) — the caller reparses the source and rewrites it.  The story
forms still come from the .map file itself, read with the C reader at
the cached offset."
  (handler-case
      (let* ((cache (%map-cache-path path))
             (map-date (file-write-date path))
             (cache-date (and (probe-file cache) (file-write-date cache))))
        (when (and map-date cache-date (> cache-date map-date))
          (let ((buf (with-open-file (s cache :element-type '(unsigned-byte 8))
                       (let* ((n (file-length s))
                              (v (make-array n :element-type '(unsigned-byte 8))))
                         (unless (and (>= n 16) (= (read-sequence v s) n))
                           (error "short map cache"))
                         v))))
            (%decode-map-cache buf path wrap start-facing))))
    (error (e)
      (dlog "map cache for ~A ignored: ~A" path e)
      nil)))

(defun %decode-map-cache (buf path wrap start-facing)
  (let ((i 0))
    (labels ((u8 () (prog1 (aref buf i) (incf i)))
             (u16 () (logior (ash (u8) 8) (u8)))
             (u32 () (logior (ash (u16) 16) (u16))))
      (unless (and (= (u8) 84) (= (u8) 77) (= (u8) 67) (= (u8) 49))
        (error "bad map cache magic"))
      (let* ((w (u16)) (h (u16))
             (sx (u16)) (sy (u16))
             (forms-offset (u32)))
        (unless (= (length buf) (+ 16 (* h w 5)))
          (error "map cache size mismatch"))
        ;; walls/features are stored exactly as the packed in-memory
        ;; representation — two bulk copies, no per-cell work (that per-cell
        ;; fill was most of a 30x30 city's cached load on a 68020)
        (let ((walls (subseq buf 16 (+ 16 (* h w 4))))
              (features (subseq buf (+ 16 (* h w 4)))))
          ;; every wall byte must be a valid code — COUNT is a C pass
          (unless (= (+ (count 0 walls) (count 1 walls) (count 2 walls))
                     (length walls))
            (error "map cache wall codes out of range"))
          (let ((map (%make-dungeon-map
                      :name path :width w :height h :wrap wrap
                      :walls walls :features features
                      :specials (make-hash-table :test 'equal)
                      :start-x sx :start-y sy
                      :start-facing (dir-keyword start-facing))))
            (unless (= forms-offset +map-cache-no-forms+)
              (with-open-file (s path)
                (file-position s forms-offset)
                (%parse-map-forms-stream map s path)))
            map))))))
