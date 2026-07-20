;;; Lambda's Tale — host front-end: interactive ASCII walkabout (PLAY).
;;;
;;; Keys: w=forward  s=back-step  a=turn left  d=turn right
;;;       m=full map view  h/?=help page  c=cast a spell
;;;       1-7=character sheet  S=save  L=load  q=quit
;;; In the map view: m/Esc=back  f=toggle omniscient (debug)  q=quit
;;; In the help page: h/Esc=back  q=quit
;;; In combat every living hero picks an action in turn (the round
;;; orders page): a=attack  d=defend  c=cast  p=play  Esc=undo the
;;; previous pick; f=flee (party-level), +/-=transcript speed.  The
;;; round runs once the last hero picked, each message lingering
;;; COMBAT-MESSAGE-DELAY seconds.
;;; In a location (shop): 1-9=choose  s/b=sell/buy page  Esc=back/leave
;;; In the cast menu: 1-9=choose caster/spell/target  Esc=back/cancel
;;; Long menu lists (shop stock, packs, the character sheet) scroll
;;; with u/d — digits pick within the visible window (see MENU-WINDOW)
;;; In the save/load menu: 1-9=pick a slot  n=new name (:save)
;;;   Esc=back/cancel (see src/save-menu.lisp)
;;;
;;; Layout per specs/ui-and-engine.md: first-person view beside the
;;; active-spells strip, party roster (up to 7 rows), message log with
;;; the newest line at the bottom; the automap lives under 'm'.

(in-package :tale)

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
      (format s "~A~%" (effect-label e)))))

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

(defun play (map-file)
  "Interactive walkabout on MAP-FILE.  Uses raw TTY keys when available,
falls back to line input otherwise.  Loads the campaign.lisp next to
the map file (classes, monsters, items, party) when present — the
engine has no default world; the game names its starting map."
  (dlog "play ~A" map-file)
  (load-campaign map-file)
  (let* ((map (load-map-file map-file))
         (game nil)
         (log nil)
         (mode :play)        ; :play, :map (full-map view), :help or :sheet
         (full nil)          ; omniscient automap (debug), map mode only
         (sheet-hero 0)      ; party index shown in :sheet mode
         (sheet-top 0)       ; sheet scroll offset (u/d)
         (shop nil)          ; SHOP-VIEW while inside a location
         (cast nil)          ; CAST-VIEW while the cast menu is open
         (use nil)           ; USE-VIEW while the use menu is open
         (sing nil)          ; SING-VIEW while the sing menu is open
         (menu nil)          ; SAVE-MENU while the save/load picker is open
         (orders nil)        ; COMBAT-ORDERS while a round is picked
         (pacing nil)        ; a combat round is running: pace messages
         (over nil))
    (labels ((wire (g)
               (setf log (attach-message-log g))
               (setf shop (when (game-location g) (make-shop-view)))
               (setf cast nil)
               (setf use nil)
               (setf sing nil)
               (setf menu nil)
               (setf orders nil)
               ;; pace the combat transcript: linger on each message a
               ;; round says (the log handler above already caught it)
               (on-event g :message
                         (lambda (game text)
                           (declare (ignore game text))
                           (when (and pacing
                                      (plusp (combat-message-delay)))
                             (draw)
                             (sleep (combat-message-delay)))))
               (on-event g :combat-start
                         (lambda (game monsters)
                           (declare (ignore game monsters))
                           (setf orders (make-combat-orders))))
               (on-event g :combat-end
                         (lambda (game result)
                           (declare (ignore game result))
                           (setf orders nil)))
               (on-event g :enter-location
                         (lambda (game loc)
                           (declare (ignore game loc))
                           (setf shop (make-shop-view))))
               (on-event g :leave-location
                         (lambda (game loc)
                           (declare (ignore game loc))
                           (setf shop nil)))
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
               ;; menu lines may carry their pick key (see MENU-OPTION);
               ;; the terminal draws the text only
               (cond (menu
                      (dolist (line (save-menu-lines game menu))
                        (format t "~A~%" (menu-line-text line))))
                     (cast
                      (dolist (line (cast-lines game cast))
                        (format t "~A~%" (menu-line-text line))))
                     (use
                      (dolist (line (use-lines game use))
                        (format t "~A~%" (menu-line-text line))))
                     (sing
                      (dolist (line (sing-lines game sing))
                        (format t "~A~%" (menu-line-text line))))
                     ((game-location game)
                      (dolist (line (location-lines game shop))
                        (format t "~A~%" (menu-line-text line))))
                     (t
                      (format t "~A~%"
                              (beside (render-first-person game)
                                      (%effects-pane game)))))
               (terpri)
               (when (game-party game)
                 (format t "~A~%" (%party-pane game)))
               ;; the facing is compass-granted (a :compass effect)
               (format t "Pos (~D,~D)~@[ facing ~A~]   ~A~%"
                       (game-x game) (game-y game)
                       (when (compass-active-p game)
                         (dir-keyword (game-facing game)))
                       (clock-line game))
               (dolist (m (log-recent log *log-lines*))
                 (format t "> ~A~%" m))
               (cond ((or menu cast use sing)
                      (when (game-combat game)
                        (format t "~A~%" (%combat-pane game))))
                     ((and (game-combat game) orders)
                      ;; the round orders page: every hero picks
                      (dolist (line (combat-orders-lines game orders))
                        (format t "~A~%" (menu-line-text line))))
                     ((game-combat game)
                      ;; a round is playing out (paced transcript)
                      (format t "~A~%" (%combat-pane game)))
                     ((game-location game))
                     (t
                      (format t "[w]=forward [s]=back [a]=left [d]=right ~
                                 [m]=map [h]elp [c]ast [u]se [p]lay ~
                                 [S]ave [L]oad [q]=quit~%"))))
             (draw-help-page ()
               (dolist (line (help-lines))
                 (format t "~A~%" line)))
             (draw-sheet-page ()
               (dolist (line (hero-sheet-lines game sheet-hero sheet-top))
                 (format t "~A~%" (menu-line-text line))))
             (draw ()
               (%clear-screen)
               (format t "=== Lambda's Tale ===  ~A (~Dx~D)~%~%"
                       (map-title (game-map game))
                       (dungeon-map-width (game-map game))
                       (dungeon-map-height (game-map game)))
               (case mode
                 (:map (draw-map-page))
                 (:help (draw-help-page))
                 (:sheet (draw-sheet-page))
                 (t (draw-play-page)))
               (finish-output))
             (note (text)
               (when text
                 (log-message log text)))
             (fight (thunk)
               ;; run one round (or a flee attempt) with the paced
               ;; transcript, then open fresh orders when it goes on
               (setf orders nil)
               (unwind-protect
                   (progn (setf pacing t) (funcall thunk))
                 (setf pacing nil))
               (when (game-combat game)
                 (setf orders (make-combat-orders))))
             (combat-act (c)
               (if (member c '(#\q #\Q))
                   :quit
                   (let ((r (combat-orders-act
                             game
                             (or orders
                                 (setf orders (make-combat-orders)))
                             c)))
                     (cond ((eq r :flee)
                            (fight (lambda () (attempt-flee game))))
                           ((and (consp r) (eq (first r) :fight))
                            (fight (lambda ()
                                     (combat-round game (second r))))))
                     nil)))
             (open-cast (in-combat)
               (if (some #'hero-caster-p (alive-heroes game))
                   (setf cast (make-cast-view :in-combat in-combat))
                   (note "No one here can cast."))
               nil)
             (cast-menu-act (c)
               (case (cast-act game cast c)
                 ((:done :cancelled) (setf cast nil)))
               nil)
             (open-use ()
               (if (some #'usable-items (alive-heroes game))
                   (setf use (make-use-view))
                   (note "No one carries anything to use."))
               nil)
             (use-menu-act (c)
               (case (use-act game use c)
                 ((:done :cancelled) (setf use nil)))
               nil)
             (open-sing (in-combat)
               (if (some #'hero-singer-p (alive-heroes game))
                   (setf sing (make-sing-view :in-combat in-combat))
                   (note "No one here can play."))
               nil)
             (sing-menu-act (c)
               (case (sing-act game sing c)
                 ((:done :cancelled) (setf sing nil)))
               nil)
             (saves-act (c)
               (let ((r (save-menu-act game menu c)))
                 (cond ((eq r :closed) (setf menu nil))
                       ((and (consp r) (eq (first r) :save))
                        (ensure-save-dir)
                        (save-game game (second r))
                        (note (format nil "Game saved to ~A." (second r)))
                        (setf menu nil))
                       ((and (consp r) (eq (first r) :load))
                        (setf game (wire (load-game (second r)))
                              over nil
                              mode :play)
                        (note "Game loaded."))))
               nil)
             (map-act (c)
               (case c
                 ((#\m #\M #\Escape) (setf mode :play) nil)
                 ((#\f #\F) (setf full (not full)) nil)
                 ((#\h #\H #\?) (setf mode :help) nil)
                 ((#\q #\Q) :quit)
                 (t nil)))
             (help-act (c)
               (case c
                 ((#\h #\H #\? #\Escape) (setf mode :play) nil)
                 ((#\q #\Q) :quit)
                 (t nil)))
             (open-sheet (i)
               ;; '1'-'7': show that roster slot if it holds a hero
               (when (nth i (game-party game))
                 (setf sheet-hero i
                       sheet-top 0
                       mode :sheet))
               nil)
             (sheet-act (c)
               (let ((digit (digit-char-p c))
                     (top (hero-sheet-scroll game sheet-hero
                                             sheet-top c)))
                 (cond ((and digit (<= 1 digit +party-limit+))
                        (open-sheet (1- digit)))
                       (top (setf sheet-top top) nil)
                       ((eql c #\Escape) (setf mode :play) nil)
                       ((member c '(#\q #\Q)) :quit)
                       (t nil))))
             (explore-act (c)
               (case c
                 (#\S (setf menu (make-save-menu :save))
                      nil)
                 (#\L (setf menu (make-save-menu :load))
                      nil)
                 ((#\1 #\2 #\3 #\4 #\5 #\6 #\7)
                  (open-sheet (1- (digit-char-p c))))
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
                    (#\h (setf mode :help)
                         nil)
                    (#\? (setf mode :help)
                         nil)
                    (#\c (open-cast nil))
                    (#\u (open-use))
                    (#\p (open-sing nil))
                    (#\q :quit)
                    (t nil)))))
             (act (c)
               (dlog "key ~S mode ~S" c mode)
               (cond ((eq mode :map) (map-act c))
                     ((eq mode :help) (help-act c))
                     ((eq mode :sheet) (sheet-act c))
                     (menu (saves-act c))
                     (cast (cast-menu-act c))
                     (use (use-menu-act c))
                     (sing (sing-menu-act c))
                     ((game-combat game) (combat-act c))
                     ((game-location game)
                      (location-act game shop c)
                      nil)
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
