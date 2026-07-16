;;; Lambda's Tale — cell specials.
;;;
;;; A special is a list of ops attached to a map cell — in the .map file
;;; after the art (see map.lisp) or via (SETF CELL-SPECIAL).  Ops are
;;; pure data interpreted here; map files are never evaluated.  The ops:
;;;
;;;   (message TEXT...)          show each TEXT (a :MESSAGE event)
;;;   (set-flag KEY) (clear-flag KEY)
;;;   (when-flag KEY OP...)      run OP... if story flag KEY is set
;;;   (unless-flag KEY OP...)    ... if it is not
;;;   (once OP...)               run OP... only the first time ever
;;;                              (one ONCE per cell; keyed by map + cell)
;;;   (teleport X Y [FACING])    relocate the party; the target cell's
;;;                              special triggers too (depth-capped)
;;;   (spin)                     face a random direction (silently —
;;;                              classic spinner squares)
;;;   (damage DICE [TEXT])       hurt every living hero (DICE each)
;;;   (heal DICE)                heal every living hero
;;;   (gold DICE)                treasure (goes to the leading hero)
;;;   (encounter (MONSTER COUNT)...)  start combat; ops after this one
;;;                              are skipped (combat interrupts)
;;;   (event TOPIC ARG...)       emit a story event for subscribers

(in-package :tale)

(defvar *special-depth* 0)

(defun trigger-special (game)
  "Run the special of the cell the party stands on, if any.  Movement
and teleports call this; call it once by hand after NEW-GAME to fire
the start cell's special (after subscribing your event handlers)."
  (let ((ops (cell-special (game-map game) (game-x game) (game-y game))))
    (when ops
      (let ((*special-depth* (1+ *special-depth*)))
        (when (> *special-depth* 8)
          (error "Special recursion deeper than 8 at (~D,~D) of ~A — ~
                  teleport loop in the map data?"
                 (game-x game) (game-y game)
                 (dungeon-map-name (game-map game))))
        (run-special game ops)))
    (values)))

(defun run-special (game ops)
  "Interpret the special OPS in order.  Once combat starts the remaining
ops are skipped — they would run mid-fight."
  (dolist (op ops)
    (when (game-combat game)
      (return))
    (run-special-op game op))
  (values))

(defun run-special-op (game op)
  (unless (and (consp op) (symbolp (first op)))
    (error "Invalid special op ~S (must be a list starting with an op name)"
           op))
  (case (first op)
    (message
     (dolist (text (rest op))
       (say game "~A" text)))
    (set-flag (set-flag game (second op)))
    (clear-flag (clear-flag game (second op)))
    (when-flag
     (when (flag game (second op))
       (run-special game (cddr op))))
    (unless-flag
     (unless (flag game (second op))
       (run-special game (cddr op))))
    (once
     (let ((key (list :once (dungeon-map-name (game-map game))
                      (game-x game) (game-y game))))
       (unless (flag game key)
         (set-flag game key)
         (run-special game (rest op)))))
    (teleport
     (teleport-party game (second op) (third op) (fourth op)))
    (spin
     (setf (game-facing game) (roll 4))
     (observe game))
    (damage
     (let ((dice (second op))
           (text (third op)))
       (when text
         (say game "~A" text))
       (dolist (h (alive-heroes game))
         (let ((n (max 0 (roll-dice dice))))
           (say game "~A takes ~D damage." (hero-name h) n)
           (damage-hero game h n)))))
    (heal
     (dolist (h (alive-heroes game))
       (heal-hero game h (max 0 (roll-dice (second op))))))
    (gold
     (let ((n (roll-dice (second op)))
           (h (first (alive-heroes game))))
       (when h
         (incf (hero-gold h) n)
         (say game "The party finds ~D gold!" n))))
    (encounter (start-combat game (rest op)))
    (event (apply #'emit game (rest op)))
    (t (error "Unknown special op ~S in cell (~D,~D) of ~A"
              (first op) (game-x game) (game-y game)
              (dungeon-map-name (game-map game)))))
  (values))

(defun teleport-party (game x y &optional facing)
  "Relocate the party to cell (X,Y), optionally FACING a direction, and
trigger the destination cell's special."
  (let ((map (game-map game)))
    (unless (and (integerp x) (< -1 x (dungeon-map-width map))
                 (integerp y) (< -1 y (dungeon-map-height map)))
      (error "Teleport target (~S,~S) is outside the ~Dx~D map ~A"
             x y (dungeon-map-width map) (dungeon-map-height map)
             (dungeon-map-name map)))
    (setf (game-x game) x
          (game-y game) y)
    (when facing
      (setf (game-facing game) (dir-index facing)))
    (observe game)
    (emit game :enter-cell x y)
    (trigger-special game)))
