;;; Lambda's Tale — interactive ASCII walkabout (host development view).
;;;
;;; Run from the project root (examples/games/lambda-tale):  make run
;;; Keys: w=forward  s=back-step  a=turn left  d=turn right
;;;       m=toggle explored/full map  q=quit

(load "src/load.lisp")

(in-package :tale)

(defun %clear-screen ()
  (format t "~C[2J~C[H" (code-char 27) (code-char 27)))

(defun %step-message (result)
  (ecase result
    (:moved "You walk on.")
    (:door "You pass through a door.")
    (:blocked "You bump into a wall.")))

(defun play (&optional (map-file "data/cellar.map"))
  "Interactive walkabout on MAP-FILE.  Uses raw TTY keys when available,
falls back to line input otherwise."
  (let* ((map (load-map-file map-file))
         (game (new-game map))
         (full nil)
         (message "You enter the cellar.  Find the stairs down (>)."))
    (labels ((draw ()
               (%clear-screen)
               (format t "=== Lambda's Tale ===  ~A (~Dx~D)~%~%"
                       (dungeon-map-name map)
                       (dungeon-map-width map)
                       (dungeon-map-height map))
               (format t "~A~%~%"
                       (beside (render-first-person game)
                               (render-game game :full full)))
               (format t "Pos (~D,~D) facing ~A~@[ [full map]~]~%"
                       (game-x game) (game-y game)
                       (dir-keyword (game-facing game))
                       full)
               (format t "~A~%" message)
               (format t "[w]=forward [s]=back [a]=left [d]=right [m]=map mode [q]=quit~%")
               (finish-output))
             (act (c)
               (case (char-downcase c)
                 (#\w (setf message (%step-message (move-party game :forward)))
                      nil)
                 (#\s (setf message (%step-message (move-party game :back)))
                      nil)
                 (#\a (turn-left game)
                      (setf message "You turn left.")
                      nil)
                 (#\d (turn-right game)
                      (setf message "You turn right.")
                      nil)
                 (#\m (setf full (not full))
                      (setf message (if full
                                        "Map mode: full (debug)."
                                        "Map mode: explored."))
                      nil)
                 (#\q :quit)
                 (t nil))))
      (if (ext:tty-p)
          (progn
            (ext:tty-raw-mode t)
            (unwind-protect
                (loop
                  (draw)
                  (let ((c (read-char *standard-input* nil nil)))
                    (when (or (null c) (eq (act c) :quit))
                      (return))))
              (ext:tty-raw-mode nil)))
          (loop
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
