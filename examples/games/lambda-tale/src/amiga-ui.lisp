;;; Lambda's Tale — AmigaOS front-end (Intuition window, wireframe view).
;;;
;;; Loaded only on AmigaOS (see src/load.lisp); the requires below must run
;;; before the rest of this file is read so AMIGA.INTUITION / AMIGA.GFX
;;; symbols resolve.
;;;
;;; Pens: 0 background, 1 wireframe/text, 3 doors and the party marker.

(require "amiga/intuition")
(require "amiga/graphics")

(in-package :tale)

(defparameter *amiga-win-width* 460)
(defparameter *amiga-win-height* 180)
(defparameter *amiga-fp-width* 240)
(defparameter *amiga-fp-height* 130)

(defun %amiga-door-rect (rp cx cy hw hh)
  (amiga.gfx:set-a-pen rp 3)
  (amiga.gfx:draw-line rp (- cx hw) (- cy hh) (+ cx hw) (- cy hh))
  (amiga.gfx:draw-line rp (- cx hw) (+ cy hh) (+ cx hw) (+ cy hh))
  (amiga.gfx:draw-line rp (- cx hw) (- cy hh) (- cx hw) (+ cy hh))
  (amiga.gfx:draw-line rp (+ cx hw) (- cy hh) (+ cx hw) (+ cy hh))
  (amiga.gfx:set-a-pen rp 1))

(defun %amiga-draw-fp (rp game ox oy w h)
  "Draw the wireframe first-person view into the rastport at (OX,OY)."
  (amiga.gfx:set-a-pen rp 0)
  (amiga.gfx:rect-fill rp ox oy (+ ox w -1) (+ oy h -1))
  (amiga.gfx:set-a-pen rp 1)
  (let ((slices (compute-view (game-map game) (game-x game) (game-y game)
                              (game-facing game)))
        (planes (view-planes w h)))
    (dolist (prim (view-display-list slices planes))
      (ecase (first prim)
        (:line (destructuring-bind (x0 y0 x1 y1) (rest prim)
                 (amiga.gfx:draw-line rp (+ ox x0) (+ oy y0)
                                      (+ ox x1) (+ oy y1))))
        (:door (destructuring-bind (cx cy hw hh) (rest prim)
                 (%amiga-door-rect rp (+ ox cx) (+ oy cy) hw hh)))))))

(defun %amiga-draw-map (rp game ox oy avail-w avail-h full)
  "Draw the automap with (OX,OY) as its top-left corner."
  (amiga.gfx:set-a-pen rp 0)
  (amiga.gfx:rect-fill rp ox oy (+ ox avail-w -1) (+ oy avail-h -1))
  (amiga.gfx:set-a-pen rp 1)
  (let* ((map (game-map game))
         (k (game-knowledge game))
         (w (dungeon-map-width map))
         (h (dungeon-map-height map))
         (cell (max 4 (min 12 (floor avail-w (max w 1))
                           (floor avail-h (max h 1))))))
    (dotimes (y h)
      (dotimes (x w)
        (let ((px (+ ox (* x cell)))
              (py (+ oy (* y cell))))
          ;; walls
          (dotimes (d 4)
            (let ((wall (cell-wall map x y d)))
              (when (and (not (eq wall :open))
                         (or full (wall-known-p k x y d)))
                (amiga.gfx:set-a-pen rp (if (eq wall :door) 3 1))
                (ecase d
                  (0 (amiga.gfx:draw-line rp px py (+ px cell) py))
                  (2 (amiga.gfx:draw-line rp px (+ py cell)
                                          (+ px cell) (+ py cell)))
                  (3 (amiga.gfx:draw-line rp px py px (+ py cell)))
                  (1 (amiga.gfx:draw-line rp (+ px cell) py
                                          (+ px cell) (+ py cell)))))))
          ;; feature glyph
          (let ((f (cell-feature map x y)))
            (when (and f (or full (cell-explored-p k x y)))
              (amiga.gfx:set-a-pen rp 1)
              (amiga.gfx:move-to rp (+ px 2) (+ py cell -2))
              (amiga.gfx:gfx-text rp (string f)))))))
    ;; party marker: filled block + facing tick
    (let* ((px (+ ox (* (game-x game) cell)))
           (py (+ oy (* (game-y game) cell)))
           (cx (+ px (floor cell 2)))
           (cy (+ py (floor cell 2)))
           (r (max 1 (floor cell 4)))
           (f (game-facing game)))
      (amiga.gfx:set-a-pen rp 3)
      (amiga.gfx:rect-fill rp (- cx r) (- cy r) (+ cx r) (+ cy r))
      (amiga.gfx:draw-line rp cx cy
                           (+ cx (* (aref *dir-dx* f) (- (floor cell 2) 1)))
                           (+ cy (* (aref *dir-dy* f) (- (floor cell 2) 1))))
      (amiga.gfx:set-a-pen rp 1))))

(defun %amiga-status (rp game ox oy w message)
  (amiga.gfx:set-a-pen rp 0)
  (amiga.gfx:rect-fill rp ox (- oy 10) (+ ox w -1) (+ oy 4))
  (amiga.gfx:set-a-pen rp 1)
  (amiga.gfx:move-to rp ox oy)
  (amiga.gfx:gfx-text rp (format nil "(~D,~D) ~A  ~A"
                                 (game-x game) (game-y game)
                                 (dir-keyword (game-facing game))
                                 message)))

(defun play-amiga (&optional (map-file "data/cellar.map"))
  "Interactive walkabout in an Intuition window.
Keys: W forward, S back-step, A/D turn, M map mode, Q/Esc quit."
  (let* ((map (load-map-file map-file))
         (game (new-game map))
         (full nil)
         (message "Welcome!"))
    (amiga.intuition:with-window
        (win :title "Lambda's Tale"
             :left 20 :top 20
             :width *amiga-win-width* :height *amiga-win-height*
             :idcmp (logior amiga.intuition:+idcmp-closewindow+
                            amiga.intuition:+idcmp-vanillakey+))
      (let* ((rp (amiga.intuition:window-rastport win))
             (bx (+ (amiga.intuition:window-border-left win) 6))
             (by (+ (amiga.intuition:window-border-top win) 6))
             (map-x (+ bx *amiga-fp-width* 16))
             (map-w (- *amiga-win-width* map-x 10))
             (status-y (+ by *amiga-fp-height* 18)))
        (labels ((redraw ()
                   (%amiga-draw-fp rp game bx by
                                   *amiga-fp-width* *amiga-fp-height*)
                   (%amiga-draw-map rp game map-x by
                                    map-w *amiga-fp-height* full)
                   (%amiga-status rp game bx status-y
                                  (- *amiga-win-width* bx 10) message))
                 (%step (relative)
                   (setf message (ecase (move-party game relative)
                                   (:moved "You walk on.")
                                   (:door "You pass through a door.")
                                   (:blocked "You bump into a wall.")))))
          (redraw)
          (amiga.intuition:event-loop win
            (amiga.intuition:+idcmp-closewindow+ (msg)
              (return))
            (amiga.intuition:+idcmp-vanillakey+ (msg)
              (let* ((code (amiga.intuition:msg-code msg))
                     (c (char-downcase (code-char code))))
                (case c
                  (#\w (%step :forward) (redraw))
                  (#\s (%step :back) (redraw))
                  (#\a (turn-left game)
                       (setf message "You turn left.")
                       (redraw))
                  (#\d (turn-right game)
                       (setf message "You turn right.")
                       (redraw))
                  (#\m (setf full (not full)
                              message (if full "Full map." "Explored map."))
                       (redraw))
                  (#\q (return))
                  (t (when (= code 27)  ; Esc
                       (return))))))))))))
