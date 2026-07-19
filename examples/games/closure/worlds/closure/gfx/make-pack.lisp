;;; The town of Closure's "city" tile pack (BT2 night street): the
;;; engine's default wall pieces plus a tan street, a night sky with
;;; stars, and a palette.iff carrying the pack colors (pens 4 up to
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

(destructuring-bind (ceiling floor)
    (backdrop-rects (view-planes *fp-view-width* *fp-view-height*))
  ;; night sky: dark blue with sparse white stars
  (let* ((w (third ceiling)) (h (fourth ceiling))
         (img (make-image w h *city-depth* :palette *city-palette*)))
    (dotimes (y h)
      (dotimes (x w)
        (setf (pixel-ref img x y)
              (if (zerop (mod (+ (* 7 x) (* 13 y)) 89)) 1 5))))
    (write-ilbm img (concatenate 'string *out* "ceiling.iff")))
  ;; tan street: the flagstone joints over a tan fill
  (let* ((w (third floor)) (h (fourth floor))
         (grey (%draw-floor w h))
         (img (make-image w h *city-depth* :palette *city-palette*)))
    (dotimes (y h)
      (dotimes (x w)
        (setf (pixel-ref img x y)
              (if (= (pixel-ref grey x y) +pen-brick+) 6 0))))
    (write-ilbm img (concatenate 'string *out* "floor.iff"))))

;; palette.iff: one pixel per pen, CMAP = the pack colors
(let* ((pens (ash 1 *city-depth*))
       (img (make-image pens 1 *city-depth* :palette *city-palette*)))
  (dotimes (x pens) (setf (pixel-ref img x 0) x))
  (write-ilbm img (concatenate 'string *out* "palette.iff")))

(format t "city pack written to ~A~%" *out*)
(cl-user::quit 0)
