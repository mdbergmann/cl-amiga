;;; Lambda's Tale — save games.
;;;
;;; A save is a single readable Lisp form — data only, written with
;;; PRIN1 and read back with *READ-EVAL* bound to NIL.  Maps themselves
;;; are not saved: the save records the current zone's map file path
;;; and reloads it (map files self-describe via their ZONE form), then
;;; restores position, per-zone automap knowledge, story flags and the
;;; party.  Zones other than the current one reload lazily when the
;;; party travels back to them.  Story flag keys and values must print
;;; readably (symbols, numbers, strings, lists thereof).

(in-package :tale)

(defconstant +save-version+ 2)
;; v2: multi-zone world — the save carries every visited zone's map file
;; and automap knowledge (:zones), plus hero :items and :equipped.

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
        :ac (hero-ac h) :damage (hero-damage h) :gold (hero-gold h)
        :items (hero-items h) :equipped (hero-equipped h)))

(defun %zones->alist (game)
  "Every zone's automap knowledge: visited zones from the world table,
plus save-restored zones the party has not revisited yet."
  (let ((zones '()))
    (maphash (lambda (path zone)
               (push (cons path (%knowledge->rows (cdr zone))) zones))
             (game-zones game))
    (append (nreverse zones) (game-zone-knowledge game))))

(defun save-game (game path)
  "Write GAME to PATH as one readable Lisp form.  Saving during combat
is not allowed.  Returns PATH."
  (when (game-combat game)
    (error "Cannot save during combat"))
  (let* ((map (game-map game))
         (form (list :lambda-tale-save +save-version+
                     :map-file (dungeon-map-name map)
                     :x (game-x game) :y (game-y game)
                     :facing (game-facing game)
                     :zones (%zones->alist game)
                     :flags (%flags->alist game)
                     :party (mapcar #'%hero->plist (game-party game)))))
    (with-open-file (s path :direction :output :if-exists :supersede)
      (let ((*package* (find-package :tale))
            (*print-pretty* nil))
        (prin1 form s)
        (terpri s))))
  path)

(defun load-game (path)
  "Load the save at PATH: reload its current map file and return a
fresh GAME with position, per-zone automap knowledge, flags and party
restored.  Other visited zones' maps reload lazily on travel.  Event
handlers are not saved — subscribe them again on the returned game."
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
           (map-file (getf data :map-file))
           (map (load-map-file map-file))
           (game (new-game map
                           :party (mapcar (lambda (plist)
                                            (apply #'%make-hero plist))
                                          (getf data :party)))))
      (setf (game-x game) (getf data :x)
            (game-y game) (getf data :y)
            (game-facing game) (getf data :facing))
      (dolist (zone (getf data :zones))
        (if (equal (car zone) map-file)
            (%rows->knowledge (game-knowledge game) (cdr zone))
            (push zone (game-zone-knowledge game))))
      (dolist (kv (getf data :flags))
        (set-flag game (car kv) (cdr kv)))
      (observe game)
      game)))
