;;; The cellar's tile pack: the same stone wall pieces as the engine's
;;; default dungeon art, over a packed-earth floor and a ceiling that
;;; darkens to black — the cellar under the Adventurer's Rest, not the
;;; town's night street.  cellar.map declares it as
;;; (zone ... :gfx "gfx-cellar/"), resolved next to the map file, so
;;; the world directory stays self-contained.
;;;
;;; What a pack can and cannot recolor (see PRINT-TILE-MANIFEST):
;;; pens 0-3 are the fixed UI colors and pen 4 is the wall pieces'
;;; opaque mortar — the loader only applies pens 4 and up, and the
;;; pieces draw their joints with pen 4, so a pack's own look lives in
;;; pens 5+ (here: the floor and the ceiling's distance bands).
;;;
;;; Regenerate from the game root (examples/games/closure):
;;;   clamiga --heap 16M --non-interactive --load worlds/closure/gfx-cellar/make-pack.lisp

(load "src/load.lisp")
(load (tale:engine-path "tools/gen-walls.lisp"))

(in-package :tale)

(defparameter *cellar-depth*
  (display-profile-screen-depth *display-profile*))

(defparameter *cellar-palette*
  (let ((pal (make-array (ash 1 *cellar-depth*) :initial-element '(0 0 0))))
    ;; pens 0-3: the fixed UI colors (ignored by the loader, but keep
    ;; the CMAP truthful)
    (setf (aref pal 0) '(0 0 0) (aref pal 1) '(255 255 255)
          (aref pal 2) '(136 136 136) (aref pal 3) '(255 170 51))
    ;; pen 4 stays black: the wall pieces' mortar and door frames
    (setf (aref pal 4) '(0 0 0))
    ;; the cellar's own colors
    (setf (aref pal 5) '(102 85 68))     ; packed-earth floor
    (setf (aref pal 6) '(68 60 51))      ; near ceiling: damp stone
    (setf (aref pal 7) '(34 30 26))      ; mid ceiling, fading to black
    pal))

(defparameter *out* "worlds/closure/gfx-cellar/")
(ensure-directories-exist *out*)

;; the 40 wall pieces: the engine's stone art as-is — a cellar is what
;; that art was drawn for
(dolist (piece (wall-piece-names))
  (let ((img (read-ilbm (concatenate 'string *gfx-dir*
                                     (wall-piece-file piece)))))
    (write-ilbm img (concatenate 'string *out* (wall-piece-file piece)))))

(let ((planes (view-planes *fp-view-width* *fp-view-height*)))
  (destructuring-bind (ceiling floor) (backdrop-rects planes)
    ;; ceiling: three bands darkening toward the horizon and ending in
    ;; black — a low cellar roof swallowed by the dark.  (The zone is
    ;; (zone ... :dark 3), so most of this is only ever seen by
    ;; torchlight anyway.)
    (write-ilbm (%draw-backdrop (third ceiling) (fourth ceiling) planes
                                (second ceiling) t
                                (vector +pen-dim+ +pen-dark+ +pen-bg+)
                                :depth *cellar-depth*
                                :palette *cellar-palette*)
                (concatenate 'string *out* "ceiling.iff"))
    ;; floor: flat packed earth, no distance shading (the same choice
    ;; the engine's default floor and the city's street make — banded
    ;; floors read as steps)
    (write-ilbm (%draw-backdrop (third floor) (fourth floor) planes
                                (second floor) nil #(5)
                                :depth *cellar-depth*
                                :palette *cellar-palette*)
                (concatenate 'string *out* "floor.iff"))))

;; palette.iff: one pixel per pen, CMAP = the pack colors
(let* ((pens (ash 1 *cellar-depth*))
       (img (make-image pens 1 *cellar-depth* :palette *cellar-palette*)))
  (dotimes (x pens) (setf (pixel-ref img x 0) x))
  (write-ilbm img (concatenate 'string *out* "palette.iff")))

(format t "cellar pack written to ~A~%" *out*)
(cl-user::quit 0)
