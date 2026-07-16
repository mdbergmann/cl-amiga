;;; Lambda's Tale — save games.
;;;
;;; A save is a single readable Lisp form — data only, written with
;;; PRIN1 and read back with *READ-EVAL* bound to NIL.  The map itself
;;; is not saved: the save records the map file path (plus the load
;;; options) and reloads it, then restores position, automap knowledge,
;;; story flags and the party.  Story flag keys and values must print
;;; readably (symbols, numbers, strings, lists thereof).

(in-package :tale)

(defconstant +save-version+ 1)

(defun %knowledge->rows (knowledge)
  (let ((bits (map-knowledge-bits knowledge))
        (rows '()))
    (dotimes (y (map-knowledge-height knowledge) (nreverse rows))
      (let ((row '()))
        (dotimes (x (map-knowledge-width knowledge))
          (push (aref bits y x) row))
        (push (nreverse row) rows)))))

(defun %rows->knowledge (knowledge rows)
  (let ((bits (map-knowledge-bits knowledge))
        (y 0))
    (dolist (row rows)
      (let ((x 0))
        (dolist (cell row)
          (setf (aref bits y x) cell)
          (incf x)))
      (incf y))))

(defun %flags->alist (game)
  (let ((alist '()))
    (maphash (lambda (k v) (push (cons k v) alist))
             (game-flags game))
    (nreverse alist)))

(defun %hero->plist (h)
  (list :name (hero-name h) :class (hero-class h)
        :level (hero-level h) :xp (hero-xp h)
        :max-hp (hero-max-hp h) :hp (hero-hp h)
        :str (hero-str h) :dex (hero-dex h) :iq (hero-iq h)
        :con (hero-con h) :lck (hero-lck h)
        :ac (hero-ac h) :damage (hero-damage h) :gold (hero-gold h)))

(defun save-game (game path)
  "Write GAME to PATH as one readable Lisp form.  Saving during combat
is not allowed.  Returns PATH."
  (when (game-combat game)
    (error "Cannot save during combat"))
  (let* ((map (game-map game))
         (form (list :lambda-tale-save +save-version+
                     :map-file (dungeon-map-name map)
                     :wrap (dungeon-map-wrap map)
                     :start-facing (dungeon-map-start-facing map)
                     :x (game-x game) :y (game-y game)
                     :facing (game-facing game)
                     :knowledge (%knowledge->rows (game-knowledge game))
                     :flags (%flags->alist game)
                     :party (mapcar #'%hero->plist (game-party game)))))
    (with-open-file (s path :direction :output :if-exists :supersede)
      (let ((*package* (find-package :tale))
            (*print-pretty* nil))
        (prin1 form s)
        (terpri s))))
  path)

(defun load-game (path)
  "Load the save at PATH: reload its map file and return a fresh GAME
with position, knowledge, flags and party restored.  Event handlers are
not saved — subscribe them again on the returned game."
  (let ((form (with-open-file (s path)
                (let ((*read-eval* nil)
                      (*package* (find-package :tale)))
                  (read s)))))
    (unless (and (consp form) (eq (first form) :lambda-tale-save))
      (error "~A is not a Lambda's Tale save file" path))
    (unless (eql (second form) +save-version+)
      (error "~A: save version ~S, this build reads version ~D"
             path (second form) +save-version+))
    (let* ((data (cddr form))
           (map (load-map-file (getf data :map-file)
                               :wrap (getf data :wrap)
                               :start-facing (getf data :start-facing)))
           (game (new-game map
                           :party (mapcar (lambda (plist)
                                            (apply #'%make-hero plist))
                                          (getf data :party)))))
      (setf (game-x game) (getf data :x)
            (game-y game) (getf data :y)
            (game-facing game) (getf data :facing))
      (%rows->knowledge (game-knowledge game) (getf data :knowledge))
      (dolist (kv (getf data :flags))
        (set-flag game (car kv) (cdr kv)))
      (observe game)
      game)))
