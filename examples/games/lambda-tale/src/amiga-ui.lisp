;;; Lambda's Tale — AmigaOS front-end (Intuition window, wireframe view).
;;;
;;; Loaded only on AmigaOS (see src/load.lisp); the requires below must run
;;; before the rest of this file is read so AMIGA.INTUITION / AMIGA.GFX /
;;; AMIGA.GADTOOLS symbols resolve.
;;;
;;; Pens: 0 background, 1 wireframe/text, 3 doors and the party marker.
;;;
;;; Save/Load/Quit live in an Intuition menu strip (right mouse button),
;;; built with gadtools.library — the Amiga-native place for them.

(require "amiga/intuition")
(require "amiga/graphics")
(require "amiga/gadtools")

(in-package :tale)

(defparameter *save-file* "tale.sav")

(defvar *autoplay* nil
  "Testing hook: a list of key characters fed to the game one per
Intuition tick (~10/s), driving a full unattended session.  :ESC quits.")

(defparameter *amiga-win-width* 460)
(defparameter *amiga-win-height* 260)
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
         (cell (max 4 (min 24 (floor avail-w (max w 1))
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
  (amiga.gfx:rect-fill rp ox (- oy 12) (+ ox w -1) (+ oy 4))
  (amiga.gfx:set-a-pen rp 1)
  (amiga.gfx:move-to rp ox oy)
  (amiga.gfx:gfx-text rp (format nil "(~D,~D) ~A  ~A"
                                 (game-x game) (game-y game)
                                 (dir-keyword (game-facing game))
                                 message)))

(defun %amiga-party (rp game ox oy w)
  "Party roster below the status row: one line per hero, fields drawn at
fixed pixel columns (text-width independent), line spacing from the
rastport's font metrics — a fixed 10 px spacing clipped the descenders
of taller Workbench fonts."
  (let ((lh (+ (amiga.gfx:rastport-tx-height rp) 2))
        (n (length (game-party game))))
    (amiga.gfx:set-a-pen rp 0)
    (amiga.gfx:rect-fill rp ox (- oy 12) (+ ox w -1) (+ oy (* lh n)))
    (amiga.gfx:set-a-pen rp 1)
    (let ((y oy))
      (dolist (h (game-party game))
        (labels ((col (x text)
                   (amiga.gfx:move-to rp (+ ox x) y)
                   (amiga.gfx:gfx-text rp text)))
          (col 0   (hero-name h))
          (col 130 (format nil "Lv~D" (hero-level h)))
          (col 185 (format nil "HP ~D/~D" (hero-hp h) (hero-max-hp h)))
          (col 290 (format nil "~D gp" (hero-gold h)))
          (unless (hero-alive-p h)
            (col 360 "(down)")))
        (incf y lh)))))

;;; The Game menu.  Item numbers (the bar counts as an item) are decoded
;;; from the MENUPICK code below: Save 0, Load 1, Quit 3.
(defparameter *menu-entries*
  (list (list amiga.gadtools:+nm-title+ "Game")
        (list amiga.gadtools:+nm-item+ "Save" :commkey "S")
        (list amiga.gadtools:+nm-item+ "Load" :commkey "L")
        :bar
        (list amiga.gadtools:+nm-item+ "Quit" :commkey "Q")))

(defconstant +menu-null+ #xFFFF)

(defun %menu-item-number (code)
  "The item number packed into a MENUPICK code (FULLMENUNUM layout:
menu bits 0-4, item bits 5-10, sub-item bits 11-15)."
  (logand (ash code -5) #x3F))


(defun play-amiga (&optional (map-file "data/cellar.map"))
  "Interactive walkabout in an Intuition window.  Loads
data/campaign.lisp (classes, monsters, party) when present.
Keys: W forward, S back-step, A/D turn, M map mode, Q/Esc quit;
in combat A attack, D defend, F flee.  Save/Load/Quit sit in the
window's menu strip (right mouse button)."
  (when (probe-file "data/campaign.lisp")
    (load "data/campaign.lisp"))
  (let* ((map (load-map-file map-file))
         (game nil)
         (full nil)
         (message "Welcome!")
         (over nil))
    (labels ((wire (g)
               (on-event g :message
                         (lambda (gm text) (declare (ignore gm))
                           (setf message text)))
               (on-event g :game-won
                         (lambda (gm) (declare (ignore gm))
                           (setf over :won)))
               (on-event g :party-defeated
                         (lambda (gm) (declare (ignore gm))
                           (setf over :lost)))
               g))
      (setf game (wire (new-game map
                                 :party (when (fboundp 'default-party)
                                          (funcall 'default-party)))))
      (trigger-special game)
      (amiga.intuition:with-pub-screen (scr)
        (amiga.gadtools:with-visual-info (vi scr)
          (amiga.intuition:with-window
              (win :title "Lambda's Tale"
                   :left 20 :top 20
                   :width *amiga-win-width* :height *amiga-win-height*
                   :idcmp (logior amiga.intuition:+idcmp-closewindow+
                                  amiga.intuition:+idcmp-vanillakey+
                                  amiga.intuition:+idcmp-menupick+
                                  ;; the *autoplay* heartbeat
                                  amiga.intuition:+idcmp-intuiticks+))
            (amiga.gadtools:with-menus (menu *menu-entries* vi win)
              (let* ((rp (amiga.intuition:window-rastport win))
                     (bx (+ (amiga.intuition:window-border-left win) 6))
                     (by (+ (amiga.intuition:window-border-top win) 6))
                     (map-x (+ bx *amiga-fp-width* 16))
                     (map-w (- *amiga-win-width* map-x 10))
                     (status-y (+ by *amiga-fp-height* 18))
                     (party-y (+ status-y 18)))
                (labels ((redraw ()
                           (when over
                             (setf message (if (eq over :won)
                                               "You win!  Press Q."
                                               "Game over.  Press Q.")))
                           (%amiga-draw-fp rp game bx by
                                           *amiga-fp-width* *amiga-fp-height*)
                           (%amiga-draw-map rp game map-x by
                                            map-w *amiga-fp-height* full)
                           (%amiga-status rp game bx status-y
                                          (- *amiga-win-width* bx 10)
                                          (if (game-combat game)
                                              (format nil "COMBAT! ~A" message)
                                              message))
                           (%amiga-party rp game bx party-y
                                         (- *amiga-win-width* bx 10)))
                         (%step (relative)
                           (setf message
                                 (ecase (move-party game relative)
                                   (:moved "You walk on.")
                                   (:door "You pass through a door.")
                                   (:blocked "You bump into a wall."))))
                         (do-save ()
                           (if (game-combat game)
                               (setf message "No saving during combat.")
                               (progn
                                 (save-game game *save-file*)
                                 (setf message "Game saved."))))
                         (do-load ()
                           (if (probe-file *save-file*)
                               (progn
                                 (setf game (wire (load-game *save-file*)))
                                 (setf over nil
                                       message "Game loaded."))
                               (setf message "No saved game found.")))
                         (act (c)
                           "Handle key C; :quit means leave the event loop."
                           (let ((lc (if (characterp c) (char-downcase c) c)))
                             (cond ((or (eql lc #\q) (eql c :esc)) :quit)
                                   (over nil) ; game ended: only Q/Esc react
                                   ((game-combat game)
                                    (case lc
                                      (#\a (combat-round game) (redraw))
                                      (#\d (combat-round game
                                                         (mapcar
                                                          (lambda (h)
                                                            (declare (ignore h))
                                                            :defend)
                                                          (alive-heroes game)))
                                           (redraw))
                                      (#\f (attempt-flee game) (redraw)))
                                    nil)
                                   ((eql c #\S) (do-save) (redraw) nil)
                                   ((eql c #\L) (do-load) (redraw) nil)
                                   (t
                                    (case lc
                                      (#\w (%step :forward) (redraw))
                                      (#\s (%step :back) (redraw))
                                      (#\a (turn-left game)
                                           (setf message "You turn left.")
                                           (redraw))
                                      (#\d (turn-right game)
                                           (setf message "You turn right.")
                                           (redraw))
                                      (#\m (setf full (not full)
                                                  message (if full
                                                              "Full map."
                                                              "Explored map."))
                                           (redraw)))
                                    nil)))))
                  (redraw)
                  (amiga.intuition:event-loop win
                    (amiga.intuition:+idcmp-closewindow+ (msg)
                      (return))
                    (amiga.intuition:+idcmp-menupick+ (msg)
                      (let ((code (amiga.intuition:msg-code msg)))
                        (unless (= code +menu-null+)
                          (case (%menu-item-number code)
                            (0 (do-save) (redraw))
                            (1 (do-load) (redraw))
                            (3 (return))))))
                    (amiga.intuition:+idcmp-vanillakey+ (msg)
                      (let* ((code (amiga.intuition:msg-code msg))
                             (c (if (= code 27) :esc (code-char code))))
                        (when (eq (act c) :quit)
                          (return))))
                    (amiga.intuition:+idcmp-intuiticks+ (msg)
                      (when *autoplay*
                        (when (eq (act (pop *autoplay*)) :quit)
                          (return))))))))))))))
