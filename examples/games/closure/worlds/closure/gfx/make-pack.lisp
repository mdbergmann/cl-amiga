;;; The town of Closure's "city" tile pack (BT2 night street): every
;;; wall piece a timber-framed house (the engine's city house style —
;;; DRAW-CITY-WALL-PIECE: thatch roof, plaster and dark framing, lit
;;; amber windows, stone foundation) over a tan street under a plain
;;; night sky, plus a palette.iff carrying the pack colors (pens 4 up
;;; to the active profile's depth).  Lives inside the world directory —
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
    ;; the pack's colors.  Pen 4 stays black — the wall pieces draw
    ;; their opaque frames/joints with it (pen 0 is the transparent
    ;; key), so a pack must not recolor it.
    (setf (aref pal 4) '(0 0 0))         ; opaque black (frames)
    (setf (aref pal 5) '(0 0 136))       ; night sky
    (setf (aref pal 6) '(204 153 102))   ; tan street
    ;; pens 7-9: the engine's house-piece colors (plaster, timber,
    ;; thatch — see *HOUSE-COLORS* in tools/gen-walls.lisp)
    (dolist (entry *house-colors*)
      (setf (aref pal (first entry)) (second entry)))
    pal))

(defparameter *out* "worlds/closure/gfx/")
(ensure-directories-exist *out*)

;; the 40 wall pieces: the engine's city house style — the streets
;; read as rows of timber houses, Skara Brae style, not dungeon stone
(let ((planes (view-planes *fp-view-width* *fp-view-height*))
      ;; depth-4 pieces: pens 0-9, CMAP = the pack colors' first 16
      (piece-pal (subseq *city-palette* 0 16)))
  (dolist (piece (wall-piece-names))
    (write-ilbm (draw-city-wall-piece piece planes piece-pal)
                (concatenate 'string *out* (wall-piece-file piece)))))

(let ((planes (view-planes *fp-view-width* *fp-view-height*)))
  (destructuring-bind (ceiling floor) (backdrop-rects planes)
    ;; night sky: plain dark blue — flat on purpose (the sky is at
    ;; infinity, so distance bands would be wrong here) and starless
    ;; (a town ceiling full of white speckles read as noise over the
    ;; buildings).  This is also NOT optional: it must stay a hand-
    ;; drawn fill rather than DRAW-BACKDROP-PIECE, whose dungeon bands
    ;; use pens 6-7 (+PEN-DIM+/+PEN-DARK+) — pen 7 aliases
    ;; +PEN-PLASTER+, the house wall color this pack draws with, and
    ;; both share one Amiga CMAP (see the WARNING in gen-walls.lisp
    ;; above DRAW-CITY-WALL-PIECE).
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

;; location pictures (the location op's :image): shown in the view
;; column while the shop/tavern menu takes over the message area —
;; drawn at the viewport size so they fill the view slot exactly
(loop for (kind file) in '((:shop "shop.iff")
                           (:tavern "tavern.iff"))
      do (write-ilbm (draw-location-scene kind
                                          *fp-view-width* *fp-view-height*)
                     (concatenate 'string *out* file)))

;; the three house facades (gen-town.lisp deals them out over the
;; town's blocks): same picture contract as the shop/tavern scenes —
;; viewport-sized, fixed UI pens only.  STYLE 0 is a stone cottage,
;; 1 a timber-framed house, 2 a tall townhouse.
(defun draw-house-facade (style w h)
  (let ((img (make-image w h 2 :palette *picture-palette*)))
    ;; the street in front, common to all three
    (%img-fill img 0 (floor (* 7 h) 8) (1- w) (1- h) 2)
    (ecase style
      (0 ;; stone cottage: low grey wall under a wide thatch roof
       (let ((x0 (floor w 8)) (x1 (floor (* 7 w) 8))
             (y0 (floor (* 3 h) 8)) (y1 (floor (* 7 h) 8)))
         (%img-fill img (- x0 (floor w 16)) (floor h 4)
                    (+ x1 (floor w 16)) y0 3)     ; thatch, with eaves
         (%img-fill img x0 y0 x1 y1 2)            ; the wall
         ;; stone courses: white joints every eighth
         (loop for y from (+ y0 (floor h 8)) below y1 by (floor h 8)
               do (%img-fill img x0 y x1 y 1))
         ;; the door and a lit window
         (let ((dx (floor (* 3 w) 8)))
           (%img-fill img dx (floor (* 9 h) 16)
                      (+ dx (floor w 10)) y1 0)
           (%chrome-scene-rect img dx (floor (* 9 h) 16)
                               (+ dx (floor w 10)) y1 1))
         (%img-fill img (floor (* 5 w) 8) (floor (* 8 h) 16)
                    (+ (floor (* 5 w) 8) (floor w 12))
                    (floor (* 10 h) 16) 3)))
      (1 ;; timber house: white plaster crossed by black beams under
         ;; a grey gable
       (let ((x0 (floor w 8)) (x1 (floor (* 7 w) 8))
             (y0 (floor (* 3 h) 8)) (y1 (floor (* 7 h) 8))
             (cx (floor w 2)))
         ;; the gable: grey triangle over the wall
         (loop for y from (floor h 8) to y0
               for half = (floor (* (- x1 x0) (- y (floor h 8)))
                                 (* 2 (- y0 (floor h 8))))
               do (%img-fill img (- cx half) y (+ cx half) y 2))
         (%img-fill img x0 y0 x1 y1 1)            ; plaster
         ;; the timber frame: posts, sill and mid-rail
         (dolist (x (list x0 (floor (* 3 w) 8) (floor (* 5 w) 8) x1))
           (%img-fill img x y0 (1+ x) y1 0))
         (%img-fill img x0 y0 x1 (1+ y0) 0)
         (%img-fill img x0 (floor (* 9 h) 16) x1
                    (1+ (floor (* 9 h) 16)) 0)
         (%img-fill img x0 y1 x1 y1 0)
         ;; two lit windows over the door bay
         (%img-fill img (+ x0 (floor w 24)) (floor (* 7 h) 16)
                    (- (floor (* 3 w) 8) (floor w 24))
                    (floor (* 8 h) 16) 3)
         (%img-fill img (+ (floor (* 5 w) 8) (floor w 24))
                    (floor (* 7 h) 16)
                    (- x1 (floor w 24)) (floor (* 8 h) 16) 3)
         (%img-fill img (+ (floor (* 3 w) 8) (floor w 24))
                    (floor (* 10 h) 16)
                    (- (floor (* 5 w) 8) (floor w 24)) y1 0)))
      (2 ;; tall townhouse: narrow grey front, rows of windows, a
         ;; white parapet under a flat black roof
       (let ((x0 (floor w 4)) (x1 (floor (* 3 w) 4))
             (y0 (floor h 8)) (y1 (floor (* 7 h) 8)))
         (%img-fill img x0 y0 x1 y1 2)
         (%img-fill img x0 y0 x1 (+ y0 (floor h 24)) 0)   ; the roof
         (%img-fill img x0 (+ y0 (floor h 24) 1) x1
                    (+ y0 (floor h 24) 1) 1)              ; the parapet
         (%chrome-scene-rect img x0 y0 x1 y1 1)
         ;; three storeys of lit windows
         (loop for wy in (list (floor (* 5 h) 16) (floor (* 8 h) 16)
                               (floor (* 11 h) 16))
               do (dolist (wx (list (+ x0 (floor w 16))
                                    (- x1 (floor w 16) (floor w 14))))
                    (%img-fill img wx wy (+ wx (floor w 14))
                               (+ wy (floor h 16)) 3)))
         ;; the amber door
         (%img-fill img (- (floor w 2) (floor w 20)) (floor (* 12 h) 16)
                    (+ (floor w 2) (floor w 20)) y1 3)
         (%chrome-scene-rect img (- (floor w 2) (floor w 20))
                             (floor (* 12 h) 16)
                             (+ (floor w 2) (floor w 20)) y1 1))))
    img))

(dotimes (style 3)
  (write-ilbm (draw-house-facade style *fp-view-width* *fp-view-height*)
              (format nil "~Ahouse-~D.iff" *out* style)))

;; class portraits (define-hero-class :image): shown in the view
;; column beside the character-sheet takeover
(loop for (style file) in '((:helm "hero-warrior.iff")
                            (:crest "hero-paladin.iff")
                            (:hood "hero-rogue.iff")
                            (:cap "hero-bard.iff")
                            (:hat "hero-conjurer.iff"))
      do (write-ilbm (draw-portrait style)
                     (concatenate 'string *out* file)))

(format t "city pack written to ~A~%" *out*)
(cl-user::quit 0)
