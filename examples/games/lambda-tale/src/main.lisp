;;; Lambda's Tale — interactive ASCII walkabout (host development view).
;;;
;;; Run from the project root (examples/games/lambda-tale):  make run
;;; Keys: w=forward  s=back-step  a=turn left  d=turn right
;;;       m=full map view  S=save  L=load  q=quit
;;; In the map view: m/Esc=back  f=toggle omniscient (debug)  q=quit
;;; In combat: a=attack  d=defend  f=flee
;;;
;;; Layout per specs/ui-and-engine.md: first-person view beside the
;;; active-spells strip, party roster (up to 7 rows), message log with
;;; the newest line at the bottom; the automap lives under 'm'.

(load "src/load.lisp")

(in-package :tale)

(defparameter *save-file* "tale.sav")

(defparameter *log-lines* 10
  "Trailing message-log lines shown below the play view.")

(defun %clear-screen ()
  (format t "~C[2J~C[H" (code-char 27) (code-char 27)))

(defun %step-message (result)
  "Log line for a step result, or NIL for a plain quiet step."
  (ecase result
    (:moved nil)
    (:door "You pass through a door.")
    (:blocked "You bump into a wall.")))

(defun %party-pane (game)
  (with-output-to-string (s)
    (let ((i 0))
      (dolist (h (game-party game))
        (incf i)
        (format s "~D ~12A ~10A Lv~2D  HP ~3D/~3D  ~5D gp~A~%"
                i (hero-name h)
                (string-downcase (symbol-name (hero-class h)))
                (hero-level h) (hero-hp h) (hero-max-hp h) (hero-gold h)
                (if (hero-alive-p h) "" "  (down)"))))))

(defun %effects-pane (game)
  "The active-spells strip: one line per active effect."
  (with-output-to-string (s)
    (dolist (e (game-effects game))
      (format s "~A~%" (string-downcase (princ-to-string e))))))

(defun %combat-pane (game)
  (with-output-to-string (s)
    (write-string "*** COMBAT ***  " s)
    (dolist (group (combat-groups (game-combat game)))
      (format s "~D ~A~A  " (cdr group) (monster-type-name (car group))
              (if (> (cdr group) 1) "s" "")))))

(defun %map-page-viewport (game)
  "Region of the automap that fits the terminal in map mode, centered
on the party: (values X0 Y0 W H).  Cells are 2 characters each in the
ASCII rendering; falls back to 38x11 cells for unknown terminals."
  (let* ((size (ext:tty-size))
         (cells-w (if size (max 4 (floor (- (car size) 1) 2)) 38))
         (cells-h (if size (max 4 (floor (- (cdr size) 5) 2)) 11)))
    (map-viewport (game-map game) (game-x game) (game-y game)
                  cells-w cells-h)))

(defun play (&optional (map-file "data/cellar.map"))
  "Interactive walkabout on MAP-FILE.  Uses raw TTY keys when available,
falls back to line input otherwise.  Loads data/campaign.lisp (classes,
monsters, party) when present."
  (when (probe-file "data/campaign.lisp")
    (load "data/campaign.lisp"))
  (let* ((map (load-map-file map-file))
         (game nil)
         (log nil)
         (mode :play)        ; :play or :map (the full-map view)
         (full nil)          ; omniscient automap (debug), map mode only
         (over nil))
    (labels ((wire (g)
               (setf log (attach-message-log g))
               (on-event g :game-won
                         (lambda (game)
                           (declare (ignore game))
                           (setf over :won)))
               (on-event g :party-defeated
                         (lambda (game)
                           (declare (ignore game))
                           (setf over :lost)))
               g)
             (draw-map-page ()
               (multiple-value-bind (x0 y0 w h) (%map-page-viewport game)
                 (format t "~A~%~%"
                         (render-dungeon (game-map game)
                                         :knowledge (if full
                                                        nil
                                                        (game-knowledge game))
                                         :px (game-x game)
                                         :py (game-y game)
                                         :facing (game-facing game)
                                         :x0 x0 :y0 y0 :w w :h h))
                 (format t "Map ~D,~D..~D,~D of ~Dx~D~@[ [full]~]   ~
                            [m]/[Esc] back  [f] full  [q] quit~%"
                         x0 y0 (+ x0 w -1) (+ y0 h -1)
                         (dungeon-map-width (game-map game))
                         (dungeon-map-height (game-map game))
                         full)))
             (draw-play-page ()
               (format t "~A~%~%"
                       (beside (render-first-person game)
                               (%effects-pane game)))
               (when (game-party game)
                 (format t "~A~%" (%party-pane game)))
               (format t "Pos (~D,~D) facing ~A~%"
                       (game-x game) (game-y game)
                       (dir-keyword (game-facing game)))
               (dolist (m (log-recent log *log-lines*))
                 (format t "> ~A~%" m))
               (if (game-combat game)
                   (format t "~A~%[a]ttack [d]efend [f]lee~%"
                           (%combat-pane game))
                   (format t "[w]=forward [s]=back [a]=left [d]=right ~
                              [m]=map [S]ave [L]oad [q]=quit~%")))
             (draw ()
               (%clear-screen)
               (format t "=== Lambda's Tale ===  ~A (~Dx~D)~%~%"
                       (dungeon-map-name (game-map game))
                       (dungeon-map-width (game-map game))
                       (dungeon-map-height (game-map game)))
               (if (eq mode :map)
                   (draw-map-page)
                   (draw-play-page))
               (finish-output))
             (note (text)
               (when text
                 (log-message log text)))
             (combat-act (c)
               (case (char-downcase c)
                 (#\a (combat-round game))
                 (#\d (combat-round game
                                    (mapcar (lambda (h)
                                              (declare (ignore h))
                                              :defend)
                                            (alive-heroes game))))
                 (#\f (attempt-flee game))
                 (#\q :quit)
                 (t nil)))
             (map-act (c)
               (case c
                 ((#\m #\M #\Escape) (setf mode :play) nil)
                 ((#\f #\F) (setf full (not full)) nil)
                 ((#\q #\Q) :quit)
                 (t nil)))
             (explore-act (c)
               (case c
                 (#\S (save-game game *save-file*)
                      (note (format nil "Game saved to ~A." *save-file*))
                      nil)
                 (#\L (if (probe-file *save-file*)
                          (progn
                            (setf game (wire (load-game *save-file*)))
                            (note "Game loaded."))
                          (note (format nil "No save at ~A." *save-file*)))
                      nil)
                 (t
                  (case (char-downcase c)
                    (#\w (note (%step-message (move-party game :forward)))
                         nil)
                    (#\s (note (%step-message (move-party game :back)))
                         nil)
                    (#\a (turn-left game)
                         nil)
                    (#\d (turn-right game)
                         nil)
                    (#\m (setf mode :map)
                         nil)
                    (#\q :quit)
                    (t nil)))))
             (act (c)
               (cond ((eq mode :map) (map-act c))
                     ((game-combat game) (combat-act c))
                     (t (explore-act c))))
             (finished-p ()
               (when over
                 (setf mode :play)
                 (draw)
                 (format t "~%~A~%"
                         (if (eq over :won)
                             "The tale ends here — for now.  You win!"
                             "All heroes have fallen.  Game over."))
                 t)))
      (setf game (wire (new-game map
                                 :party (when (fboundp 'default-party)
                                          (funcall 'default-party)))))
      (trigger-special game)
      (if (ext:tty-p)
          (progn
            (ext:tty-raw-mode t)
            (unwind-protect
                (loop
                  (when (finished-p) (return))
                  (draw)
                  (let ((c (read-char *standard-input* nil nil)))
                    (when (or (null c) (eq (act c) :quit))
                      (return))))
              (ext:tty-raw-mode nil)))
          (loop
            (when (finished-p) (return))
            (draw)
            (format t "> ")
            (finish-output)
            (let ((line (read-line *standard-input* nil nil)))
              (when (or (null line)
                        (and (> (length line) 0)
                             (eq (act (char line 0)) :quit)))
                (return)))))
      (format t "~%Goodbye, adventurer.~%"))))

(play)
(cl-user::quit 0)
