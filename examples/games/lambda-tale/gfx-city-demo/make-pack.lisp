;;; A "city" tile pack (BT2 night street): the demo wall pieces plus a
;;; tan street, a night sky with stars, and a palette.iff carrying the
;;; pack colors in pens 4-15.  The worked example of the tile-pack
;;; contract (see PRINT-TILE-MANIFEST and the README).
;;;
;;; Regenerate from the game directory:
;;;   clamiga --heap 16M --non-interactive --load gfx-city-demo/make-pack.lisp
;;; Try it:
;;;   clamiga --heap 8M --load gfx-city-demo/run.lisp

(load "src/load.lisp")
(load "tools/gen-walls.lisp")

(in-package :tale)

(defparameter *city-palette*
  (let ((pal (make-array 16 :initial-element '(0 0 0))))
    ;; pens 0-3: the fixed UI colors (ignored by the loader, but keep
    ;; the CMAP truthful)
    (setf (aref pal 0) '(0 0 0) (aref pal 1) '(255 255 255)
          (aref pal 2) '(136 136 136) (aref pal 3) '(255 170 51))
    ;; pens 4-15: the pack's colors.  Pen 4 stays black — the demo
    ;; wall pieces draw their opaque mortar/joints with it (pen 0 is
    ;; the transparent key), so a pack must not recolor it.
    (setf (aref pal 4) '(0 0 0))         ; opaque black (wall mortar)
    (setf (aref pal 5) '(0 0 136))       ; night sky
    (setf (aref pal 6) '(204 153 102))   ; tan street
    pal))

(defparameter *out* "gfx-city-demo/")
(ensure-directories-exist *out*)

;; the 40 wall pieces: the demo art as-is (grey stone reads as city walls)
(dolist (piece (wall-piece-names))
  (let ((img (read-ilbm (concatenate 'string "data/gfx/"
                                     (wall-piece-file piece)))))
    (write-ilbm img (concatenate 'string *out* (wall-piece-file piece)))))

(destructuring-bind (ceiling floor)
    (backdrop-rects (view-planes *fp-view-width* *fp-view-height*))
  ;; night sky: dark blue with sparse white stars
  (let* ((w (third ceiling)) (h (fourth ceiling))
         (img (make-image w h 4 :palette *city-palette*)))
    (dotimes (y h)
      (dotimes (x w)
        (setf (pixel-ref img x y)
              (if (zerop (mod (+ (* 7 x) (* 13 y)) 89)) 1 5))))
    (write-ilbm img (concatenate 'string *out* "ceiling.iff")))
  ;; tan street: the flagstone joints over a tan fill
  (let* ((w (third floor)) (h (fourth floor))
         (grey (%draw-floor w h))
         (img (make-image w h 4 :palette *city-palette*)))
    (dotimes (y h)
      (dotimes (x w)
        (setf (pixel-ref img x y)
              (if (= (pixel-ref grey x y) +pen-brick+) 6 0))))
    (write-ilbm img (concatenate 'string *out* "floor.iff"))))

;; palette.iff: a 16x1 strip, one pixel per pen, CMAP = the pack colors
(let ((img (make-image 16 1 4 :palette *city-palette*)))
  (dotimes (x 16) (setf (pixel-ref img x 0) x))
  (write-ilbm img (concatenate 'string *out* "palette.iff")))

(format t "city pack written to ~A~%" *out*)
(cl-user::quit 0)
