;;; The town of Closure — map generator (Skara Brae scale).
;;;
;;; Writes worlds/closure/town.map: a 30x30 walled city on a street
;;; grid — building blocks between the streets, a plaza at the center,
;;; the south gate as the party's start, Wolfgar's shoppe and the
;;; Adventurer's Rest (with the cellar trapdoor) flanking it.  Every
;;; block gets one or two doors onto the street; each door is a house
;;; the party can step into, drawn from three facade pictures
;;; (gfx/house-0..2.iff, the location's :facade) with the matching
;;; interior (gfx/interior-0..2.iff, its :image, shown inside) —
;;; picked per BLOCK by a seeded PRNG (a block is one building mass,
;;; and the engine gives a mass one look — see %WALL-STYLE), so the
;;; same seed always writes the same town, the map is checked in and
;;; this script only reruns to change the layout.
;;;
;;; Regenerate from the game root (examples/games/closure):
;;;   clamiga --heap 16M --non-interactive --load worlds/closure/gen-town.lisp

(load "src/load.lisp")

(in-package :tale)

(defparameter *town-w* 30)
(defparameter *town-h* 30)

;; Streets run along these rows and columns; the bands between them
;; are the building blocks (Skara Brae's grid, squared off).
(defparameter *street-lines* '(0 5 10 15 20 25 29))

;; Band between consecutive street lines: ((lo . hi) ...)
(defparameter *bands*
  (loop for (a b) on *street-lines* while b
        collect (cons (1+ a) (1- b))))

;; The plaza: these two blocks (band-x . band-y) stay open ground.
(defparameter *plaza-blocks* '((2 . 2) (3 . 2)))

(defparameter *gate-x* 15)              ; the south gate, street col 15
(defparameter *gate-y* 29)
(defparameter *shop-cell* '(12 . 28))   ; door south onto the gate street
(defparameter *tavern-cell* '(17 . 28))

;;; A tiny deterministic PRNG (16-bit LCG) — CL:RANDOM's sequence is
;;; implementation-defined, and the whole point is a reproducible map.
(defvar *lcg* 4711)
(defun rnd (n)
  (setf *lcg* (mod (+ (* *lcg* 1309) 13849) 65536))
  (floor (* *lcg* n) 65536))

;;; Edge grids.  HWALL[y][x] is the edge between cell (x,y-1) and
;;; (x,y); VWALL[y][x] the edge between (x-1,y) and (x,y).
(defparameter *hwall* (make-array (list (1+ *town-h*) *town-w*)
                                  :initial-element nil))
(defparameter *vwall* (make-array (list *town-h* (1+ *town-w*))
                                  :initial-element nil))

(defun solid-block (x0 y0 x1 y1)
  "Wall every edge of every cell in the rectangle — a solid mass."
  (loop for y from y0 to (1+ y1)
        do (loop for x from x0 to x1
                 do (setf (aref *hwall* y x) :wall)))
  (loop for y from y0 to y1
        do (loop for x from x0 to (1+ x1)
                 do (setf (aref *vwall* y x) :wall))))

(defun carve-door (x y side)
  "Turn cell (X,Y)'s SIDE edge (:north :east :south :west) into a door."
  (ecase side
    (:north (setf (aref *hwall* y x) :door))
    (:south (setf (aref *hwall* (1+ y) x) :door))
    (:west  (setf (aref *vwall* y x) :door))
    (:east  (setf (aref *vwall* y (1+ x)) :door))))

;;; ---------------------------------------------------------------------
;;; Build the city

;; the city wall
(dotimes (x *town-w*)
  (setf (aref *hwall* 0 x) :wall
        (aref *hwall* *town-h* x) :wall))
(dotimes (y *town-h*)
  (setf (aref *vwall* y 0) :wall
        (aref *vwall* y *town-w*) :wall))

;; the blocks, with their doors and house specials
(defparameter *house-styles*
  #("A Stone Cottage" "A Timber House" "A Tall Townhouse"))
(defparameter *householders*
  #("Baker" "Weaver" "Cooper" "Chandler" "Mason" "Fletcher" "Tanner"
    "Miller" "Scribe" "Herbalist" "Cartwright" "Smith" "Potter" "Dyer"
    "Brewer" "Glazier"))

(defparameter *specials* '())           ; ((x y) form-string ...) in map order
(defparameter *used-cells*
  (list *shop-cell* *tavern-cell*))     ; one location per cell

(defun add-special (x y text)
  (push (list x y text) *specials*))

;; The perspective street pieces come in three looks too (the pack's
;; -vN variants: base timber, v1 stone, v2 townhouse); :style pins a
;; house's pieces to the variant matching its facade picture.  House
;; style N (0 stone cottage / 1 timber / 2 townhouse) -> variant index:
(defparameter *style->variant* #(1 0 2))

(defun add-house (x y side n style)
  "Carve a door into cell (X,Y)'s SIDE and make it house number N.
STYLE is the BLOCK's look, not the house's: one solid block of cells
is one building mass, and the engine wears one wall-piece variant over
the whole of it (see %WALL-STYLE) — so a block's two doors must be two
front doors of the same building, or the facade the party stands
before would not be the wall it sees from the street."
  (carve-door x y side)
  (let ((holder (aref *householders* (mod n (length *householders*)))))
    ;; Plain ASCII in the title: it is displayed by the Amiga's topaz
    ;; font, which has no em dash — a UTF-8 one arrives as three
    ;; box glyphs.
    (add-special x y
                 (format nil "(location \"~A - the ~A's\" :house~%~
                              ~11T:image \"gfx/interior-~D.iff\"~%~
                              ~11T:facade \"gfx/house-~D.iff\" ~
                              :style ~D)"
                         (aref *house-styles* style) holder style style
                         (aref *style->variant* style)))))

(let ((n 0))
  (loop for by from 0 below (length *bands*)
        for (y0 . y1) = (nth by *bands*)
        do (loop for bx from 0 below (length *bands*)
                 for (x0 . x1) = (nth bx *bands*)
                 unless (member (cons bx by) *plaza-blocks* :test #'equal)
                 do (solid-block x0 y0 x1 y1)
                    ;; one or two doors, on random sides at random
                    ;; spots — both into the same building, so they
                    ;; share the block's house style
                    (let ((style (rnd 3)))
                      (dotimes (d (1+ (rnd 2)))
                        (let* ((side (aref #(:north :east :south :west)
                                           (rnd 4)))
                               (x (if (member side '(:north :south))
                                      (+ x0 (rnd (1+ (- x1 x0))))
                                      (if (eq side :west) x0 x1)))
                               (y (if (member side '(:east :west))
                                      (+ y0 (rnd (1+ (- y1 y0))))
                                      (if (eq side :north) y0 y1))))
                          (unless (member (cons x y) *used-cells*
                                          :test #'equal)
                            (push (cons x y) *used-cells*)
                            (add-house x y side (incf n) style))))))))

;; the shoppe and the tavern, doors south onto the gate street
(carve-door (car *shop-cell*) (cdr *shop-cell*) :south)
(add-special (car *shop-cell*) (cdr *shop-cell*)
             (format nil "(location \"Wolfgar's Arms & Armour\" :shop~%~
                          ~11T:image \"gfx/shop.iff\"~%~
                          ~11T:stock (torch healing-potion lantern dagger short-sword~%~
                          ~19Tmace war-axe broadsword leather-armor splint-mail~%~
                          ~19Tchain-mail buckler tower-shield))"))
(carve-door (car *tavern-cell*) (cdr *tavern-cell*) :south)
(add-special (car *tavern-cell*) (cdr *tavern-cell*)
             (format nil "(location \"The Adventurer's Rest\" :tavern ~
                          :price 4 :down \"cellar.map\"~%~
                          ~11T:image \"gfx/tavern.iff\")"))

;; the gate, the plaza, the streets' name plaques, the night alley
(add-special *gate-x* *gate-y*
             (format nil "(once (message \"Welcome to Closure, last stop ~
                          before the depths.\"~%~
                          ~9T\"Wolfgar's shoppe lies west along the wall; ~
                          the tavern east.\"))"))
(add-special 15 13
             (format nil "(once (message \"The Gran Plaza of Closure.  ~
                          A statue of the First Lambda\"~%~
                          ~9T\"looks south toward the gate.\"))"))
(loop for (y name) in '((5 "Cons Street") (10 "Cdr Lane") (20 "Macro Row")
                        (25 "Tail Call Alley"))
      do (add-special 2 y (format nil "(once (message \"~A.\"))" name)))
(loop for (x name) in '((5 "Lambda Lane") (10 "Quote Street")
                        (20 "Symbol Street") (25 "Collector's Walk"))
      do (add-special x 2 (format nil "(once (message \"~A.\"))" name)))
(add-special 15 22
             (format nil "(at-night (message \"Footpads leap from the ~
                          shadows!\")~%~12T(encounter (\"footpad\" \"1d2\")))"))

;;; ---------------------------------------------------------------------
;;; Emit the map file

(defun edge-char (v horizontal)
  (case v
    (:wall (if horizontal #\- #\|))
    (:door #\D)
    (t #\Space)))

(with-open-file (out "worlds/closure/town.map"
                     :direction :output :if-exists :supersede)
  ;; the art
  (dotimes (r (1+ (* 2 *town-h*)))
    (if (evenp r)
        (let ((y (floor r 2)))
          (dotimes (x *town-w*)
            (write-char #\+ out)
            (write-char (edge-char (aref *hwall* y x) t) out))
          (write-line "+" out))
        (let ((y (floor r 2)))
          (dotimes (x *town-w*)
            (write-char (edge-char (aref *vwall* y x) nil) out)
            (write-char (if (and (= x *gate-x*) (= y *gate-y*))
                            #\@ #\Space)
                        out))
          (write-char (edge-char (aref *vwall* y *town-w*) nil) out)
          (terpri out))))
  (terpri out)
  ;; the story layer
  ;; Plain ASCII throughout the file: its titles are drawn by the
  ;; Amiga's topaz font, which has no em dash (a UTF-8 one shows up as
  ;; three box glyphs), and the test suite holds the whole map to it.
  (format out ";; The town of Closure - the campaign's city, Skara Brae~%~
               ;; sized: a 30x30 walled grid of streets and house blocks.~%~
               ;; GENERATED by worlds/closure/gen-town.lisp - edit and rerun~%~
               ;; that script instead of this file.  Houses draw one of the~%~
               ;; three facade pictures (gfx/house-0..2.iff, :facade) from~%~
               ;; the street and the matching interior (gfx/interior-0..2.iff,~%~
               ;; :image) inside; :style pins the house's perspective wall~%~
               ;; pieces to the matching pack variant.  One block of the~%~
               ;; city grid is one building, so its houses share a style.~%~
               ;; The shoppe and the tavern (trapdoor to cellar.map below)~%~
               ;; flank the south gate where the party starts.~%~%")
  (format out "(zone :kind :city :title \"Closure\" :gfx \"gfx/\")~%")
  (dolist (entry (reverse *specials*))
    (destructuring-bind (x y text) entry
      (format out "~%(special (~D ~D)~%  ~A)~%" x y text))))

;;; Self-check: parse what we wrote, report size, houses and load time.
(let* ((t0 (get-internal-real-time))
       (m (load-map-file "worlds/closure/town.map"))
       (ms (round (* 1000 (- (get-internal-real-time) t0))
                  internal-time-units-per-second))
       (houses 0)
       (mismatched 0)
       (facades '()))
  (maphash (lambda (cell ops)
             (declare (ignore cell))
             (let ((loc (find-if (lambda (op)
                                   (string-equal (first op) "LOCATION"))
                                 ops)))
               (when (and loc (eq (third loc) :house))
                 (incf houses)
                 (let* ((facade (getf (cdddr loc) :facade))
                        (image (getf (cdddr loc) :image))
                        (style (getf (cdddr loc) :style))
                        (n (digit-char-p (char facade 10))))
                   (pushnew facade facades :test #'equal)
                   ;; gfx/house-N.iff pairs with gfx/interior-N.iff and
                   ;; the matching piece-variant :style
                   (unless (and (equal image
                                       (format nil "gfx/interior-~D.iff" n))
                                (eql style (aref *style->variant* n)))
                     (incf mismatched))))))
           (dungeon-map-specials m))
  (format t "town.map written: ~Dx~D, ~D specials, ~D houses in ~D styles ~
             (~D facade/interior mismatches), parsed in ~Dms~%"
          (dungeon-map-width m) (dungeon-map-height m)
          (hash-table-count (dungeon-map-specials m))
          houses (length facades) mismatched ms))

(cl-user::quit 0)
