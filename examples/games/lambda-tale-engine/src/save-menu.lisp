;;; Lambda's Tale — named saves: the save/load slot menu.
;;;
;;; Saves live as NAME.sav files in *SAVE-DIR* (created on the first
;;; save).  The picker is a shared menu model (the SHOP-VIEW pattern,
;;; see locations.lisp): SAVE-MENU-LINES renders the slot list as text
;;; lines, SAVE-MENU-ACT maps a key press to a decision — both
;;; front-ends drive the same model.  The model never touches the
;;; front-end's GAME binding: committing returns an instruction,
;;; (:SAVE PATH) or (:LOAD PATH), and the front-end executes it
;;; (SAVE-GAME, or LOAD-GAME plus its own re-wiring).
;;;
;;; In :save mode 'n' starts new-name entry: name characters (letters,
;;; digits, - and _, at most +SLOT-NAME-LIMIT+) accumulate with a live
;;; echo, Backspace deletes, Return commits, Esc cancels.  Every other
;;; key is ignored while the menu is open — digits are slot picks and
;;; letters are name characters, so the walkabout keys cannot leak
;;; through (which also makes scripted *AUTOPLAY* sessions safe).
;;;
;;; Slot picks are a single decimal digit, so at most +MAX-SAVE-SLOTS+
;;; slots can ever exist: 'n' is refused (with an on-screen message)
;;; once that many are already on disk, so every slot stays reachable.
;;; A slot list longer than a menu page scrolls (u/d, digits pick
;;; within the visible window — see MENU-WINDOW in events.lisp).

(in-package :tale)

(defparameter *save-dir* "saves/"
  "Directory holding the named saves (NAME.sav), relative to the game
directory; created on the first save.")

(defconstant +slot-name-limit+ 16
  "Longest save-slot name the entry field accepts.")

(defconstant +max-save-slots+ 9
  "Most save slots the picker keeps reachable: slot selection is a
single decimal digit (1-9), so new-name entry is refused once this
many slots already exist.")

(defun slot-path (name)
  "The save file path for slot NAME."
  (concatenate 'string *save-dir* name ".sav"))

(defun ensure-save-dir ()
  "Create *SAVE-DIR* if it is missing.  Returns *SAVE-DIR*."
  (ensure-directories-exist *save-dir*)
  *save-dir*)

(defun save-slots ()
  "The existing save-slot names (no directory, no .sav), sorted."
  (sort (mapcar (lambda (p)
                  (let ((file (file-namestring (namestring p))))
                    (subseq file 0 (- (length file) 4))))
                (directory (concatenate 'string *save-dir* "*.sav")))
        #'string-lessp))

(defun slot-name-char-p (ch)
  "Characters a slot name may hold: letters, digits, - and _ (they
must survive as file names on every platform, AmigaOS included)."
  (or (alphanumericp ch) (char= ch #\-) (char= ch #\_)))

;;; ---------------------------------------------------------------------
;;; The save/load menu model (shared by both front-ends).

(defstruct (save-menu (:constructor %make-save-menu))
  mode                ; :save or :load
  slots               ; existing slot names, cached when the menu opens
  entry               ; NIL, or the new name being typed (:save mode)
  error               ; NIL, or a message line (e.g. slot cap reached)
  (top 0))            ; scroll offset into the slot list

(defun make-save-menu (mode)
  "Open the picker: MODE is :SAVE or :LOAD."
  (%make-save-menu :mode mode :slots (save-slots)))

(defun save-menu-lines (game view)
  "The current save/load menu as a list of menu lines — the front-ends
draw these verbatim (the SHOP-LINES pattern); slot rows carry their
pick key (see MENU-NUMBERED)."
  (let ((mode (save-menu-mode view))
        (slots (save-menu-slots view))
        (entry (save-menu-entry view))
        (err (save-menu-error view)))
    (append
     (list (if (eq mode :save) "*** Save Game ***" "*** Load Game ***") "")
     (cond
       ((and (eq mode :save) (game-combat game))
        (list "No saving during combat." "" "[Esc] back"))
       (entry
        (list (format nil "New name: ~A_" entry)
              ""
              "type a name  [Return] save  [Esc] cancel"))
       (t
        (append
         (if slots
             (menu-scrolled-lines
              slots (save-menu-top view)
              (lambda (i name)
                (menu-numbered
                 i (format nil "~D) ~A" i name))))
             (list (if (eq mode :save) "No saved games yet." "No saved games.")))
         (if err (list "" err) nil)
         (list ""
               (if (eq mode :save)
                   "[1-9] overwrite  [n] new name  [Esc] cancel"
                   "[1-9] load  [Esc] cancel"))))))))

(defun save-menu-act (game view char)
  "Apply key CHAR to the save/load menu.  Returns NIL (stay open),
:CLOSED (cancelled), or the front-end instruction (:SAVE PATH) /
(:LOAD PATH) — the front-end runs SAVE-GAME/LOAD-GAME and closes."
  (let ((mode (save-menu-mode view))
        (slots (save-menu-slots view))
        (entry (save-menu-entry view)))
    (cond
      ;; combat blocks saving: only Esc reacts
      ((and (eq mode :save) (game-combat game))
       (when (eql char #\Escape) :closed))
      ;; typing a new name
      (entry
       (cond ((or (eql char #\Return) (eql char #\Newline)
                  (eql char (code-char 13)))
              (when (plusp (length entry))
                (list :save (slot-path entry))))
             ((eql char #\Escape)
              (setf (save-menu-entry view) nil)
              nil)
             ((or (eql char #\Backspace) (eql char (code-char 8)))
              (when (plusp (length entry))
                (setf (save-menu-entry view)
                      (subseq entry 0 (1- (length entry)))))
              nil)
             ((and (characterp char)
                   (slot-name-char-p char)
                   (< (length entry) +slot-name-limit+))
              (setf (save-menu-entry view)
                    (concatenate 'string entry (string char)))
              nil)
             (t nil)))
      ;; the slot list
      (t
       (let* ((digit (and (characterp char) (digit-char-p char)))
              (pick (and digit
                         (menu-window-pick slots (save-menu-top view) digit)))
              (top (menu-scroll (save-menu-top view) char (length slots))))
         (cond (pick
                (setf (save-menu-error view) nil)
                (list (if (eq mode :save) :save :load) (slot-path pick)))
               (top
                (setf (save-menu-top view) top)
                nil)
               ((and (eq mode :save) (member char '(#\n #\N)))
                (if (>= (length slots) +max-save-slots+)
                    (setf (save-menu-error view)
                          (format nil "Slot limit reached (~D) — delete a save first."
                                  +max-save-slots+))
                    (progn
                      (setf (save-menu-error view) nil)
                      (setf (save-menu-entry view) "")))
                nil)
               ((eql char #\Escape)
                (setf (save-menu-error view) nil)
                :closed)
               (t nil)))))))
