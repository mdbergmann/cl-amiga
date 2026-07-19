;;; The town of Closure's "city" tile pack (BT2 night street): the
;;; engine's default wall pieces plus a tan street, a plain night sky,
;;; and a palette.iff carrying the pack colors (pens 4 up to
;;; the active profile's depth).  Lives inside the world directory —
;;; town.map declares it as (zone ... :gfx "gfx/"), resolved next to
;;; the map file — and doubles as the worked example of the engine's
;;; tile-pack contract (see PRINT-TILE-MANIFEST and the READMEs).
;;; Built for the default display profile — its viewport sizes and pen
;;; count come from *DISPLAY-PROFILE*.
;;;
;;; Regenerate from the game root (examples/games/closure):
;;;   clamiga --heap 16M --non-interactive --load worlds/closure/gfx/make-pack.lisp
;;; Try it:
;;;   clamiga --heap 8M --load worlds/closure/gfx/run.lisp

(load "src/load.lisp")
(load (tale:engine-path "tools/gen-walls.lisp"))

(in-package :tale)

(defparameter *city-depth*
  (display-profile-screen-depth *display-profile*))

(defparameter *city-palette*
  (let ((pal (make-array (ash 1 *city-depth*) :initial-element '(0 0 0))))
    ;; pens 0-3: the fixed UI colors (ignored by the loader, but keep
    ;; the CMAP truthful)
    (setf (aref pal 0) '(0 0 0) (aref pal 1) '(255 255 255)
          (aref pal 2) '(136 136 136) (aref pal 3) '(255 170 51))
    ;; the pack's colors.  Pen 4 stays black — the default wall pieces
    ;; draw their opaque mortar/joints with it (pen 0 is the
    ;; transparent key), so a pack must not recolor it.
    (setf (aref pal 4) '(0 0 0))         ; opaque black (wall mortar)
    (setf (aref pal 5) '(0 0 136))       ; night sky
    (setf (aref pal 6) '(204 153 102))   ; tan street
    pal))

(defparameter *out* "worlds/closure/gfx/")
(ensure-directories-exist *out*)

;; the 40 wall pieces: the default profile's art as-is (grey stone
;; reads as city walls)
(dolist (piece (wall-piece-names))
  (let ((img (read-ilbm (concatenate 'string *gfx-dir*
                                     (wall-piece-file piece)))))
    (write-ilbm img (concatenate 'string *out* (wall-piece-file piece)))))

(let ((planes (view-planes *fp-view-width* *fp-view-height*)))
  (destructuring-bind (ceiling floor) (backdrop-rects planes)
    ;; night sky: plain dark blue — flat on purpose (the sky is at
    ;; infinity, so distance bands would be wrong here) and starless
    ;; (a town ceiling full of white speckles read as noise over the
    ;; buildings)
    (let* ((w (third ceiling)) (h (fourth ceiling))
           (img (make-image w h *city-depth* :palette *city-palette*)))
      (dotimes (y h)
        (dotimes (x w)
          (setf (pixel-ref img x y) 5)))
      (write-ilbm img (concatenate 'string *out* "ceiling.iff")))
    ;; tan street: one flat color (pen 6), like the engine's default
    ;; floor — no distance shading
    (write-ilbm (%draw-backdrop (third floor) (fourth floor) planes
                                (second floor) nil #(6)
                                :depth *city-depth*
                                :palette *city-palette*)
                (concatenate 'string *out* "floor.iff"))))

;; palette.iff: one pixel per pen, CMAP = the pack colors
(let* ((pens (ash 1 *city-depth*))
       (img (make-image pens 1 *city-depth* :palette *city-palette*)))
  (dotimes (x pens) (setf (pixel-ref img x 0) x))
  (write-ilbm img (concatenate 'string *out* "palette.iff")))

;; effects-band icons for the campaign's timed spells
;; (define-spell :image, resolved map-relative like the pack itself)
(loop for (kind file) in '((:flame "fx-flame.iff")
                           (:shield "fx-shield.iff")
                           (:compass "fx-compass.iff"))
      do (write-ilbm (draw-effect-icon kind)
                     (concatenate 'string *out* file)))

(format t "city pack written to ~A~%" *out*)
(cl-user::quit 0)
