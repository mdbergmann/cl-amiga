;;; Lambda's Tale — interactive ASCII walkabout (host development view).
;;;
;;; Run from the project root (examples/games/lambda-tale):  make run
;;; Keys: w=forward  s=back-step  a=turn left  d=turn right
;;;       m=toggle explored/full map  S=save  L=load  q=quit
;;; In combat: a=attack  d=defend  f=flee

(load "src/load.lisp")

(in-package :tale)

(defparameter *save-file* "tale.sav")

(defun %clear-screen ()
  (format t "~C[2J~C[H" (code-char 27) (code-char 27)))

(defun %step-message (result)
  (ecase result
    (:moved "You walk on.")
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

(defun %combat-pane (game)
  (with-output-to-string (s)
    (write-string "*** COMBAT ***  " s)
    (dolist (group (combat-groups (game-combat game)))
      (format s "~D ~A~A  " (cdr group) (monster-type-name (car group))
              (if (> (cdr group) 1) "s" "")))))

(defun play (&optional (map-file "data/cellar.map"))
  "Interactive walkabout on MAP-FILE.  Uses raw TTY keys when available,
falls back to line input otherwise.  Loads data/campaign.lisp (classes,
monsters, party) when present."
  (when (probe-file "data/campaign.lisp")
    (load "data/campaign.lisp"))
  (let* ((map (load-map-file map-file))
         (game nil)
         (full nil)
         (recent '())
         (over nil))
    (labels ((wire (g)
               (on-event g :message
                         (lambda (game text)
                           (declare (ignore game))
                           (setf recent (last (append recent (list text)) 4))))
               (on-event g :game-won
                         (lambda (game)
                           (declare (ignore game))
                           (setf over :won)))
               (on-event g :party-defeated
                         (lambda (game)
                           (declare (ignore game))
                           (setf over :lost)))
               g)
             (draw ()
               (%clear-screen)
               (format t "=== Lambda's Tale ===  ~A (~Dx~D)~%~%"
                       (dungeon-map-name (game-map game))
                       (dungeon-map-width (game-map game))
                       (dungeon-map-height (game-map game)))
               (format t "~A~%~%"
                       (beside (render-first-person game)
                               (render-game game :full full)))
               (when (game-party game)
                 (format t "~A~%" (%party-pane game)))
               (format t "Pos (~D,~D) facing ~A~@[ [full map]~]~%"
                       (game-x game) (game-y game)
                       (dir-keyword (game-facing game))
                       full)
               (dolist (m recent)
                 (format t "~A~%" m))
               (if (game-combat game)
                   (format t "~A~%[a]ttack [d]efend [f]lee~%"
                           (%combat-pane game))
                   (format t "[w]=forward [s]=back [a]=left [d]=right ~
                              [m]=map mode [S]ave [L]oad [q]=quit~%"))
               (finish-output))
             (note (text)
               (setf recent (last (append recent (list text)) 4)))
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
                         (note "You turn left.")
                         nil)
                    (#\d (turn-right game)
                         (note "You turn right.")
                         nil)
                    (#\m (setf full (not full))
                         (note (if full
                                   "Map mode: full (debug)."
                                   "Map mode: explored."))
                         nil)
                    (#\q :quit)
                    (t nil)))))
             (act (c)
               (if (game-combat game)
                   (combat-act c)
                   (explore-act c)))
             (finished-p ()
               (when over
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
