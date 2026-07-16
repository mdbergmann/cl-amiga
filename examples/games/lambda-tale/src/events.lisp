;;; Lambda's Tale — engine events and story flags.
;;;
;;; The engine never hard-codes story facts: it emits events, the story
;;; (campaign data) and the front-end subscribe.  Everything the game
;;; wants to tell the player travels as a :MESSAGE event, so front-ends
;;; subscribe once and stay independent of what generates the text.
;;;
;;; Events the engine emits:
;;;   :message TEXT          something to show the player
;;;   :enter-cell X Y        the party entered a cell (move or teleport)
;;;   :blocked DIR           the party bumped into a wall
;;;   :combat-start MONSTERS combat began
;;;   :combat-end RESULT     combat ended (:victory, :defeat or :fled)
;;;   :hero-died HERO        a hero dropped to 0 hp
;;;   :party-defeated        the last hero fell
;;; Story specials can emit arbitrary further topics via the EVENT op
;;; (see specials.lisp); front-ends and campaign code subscribe alike.

(in-package :tale)

(defun on-event (game topic handler)
  "Subscribe HANDLER, a function of GAME and the event's arguments, to
TOPIC (a keyword).  Handlers on one topic run in subscription order."
  (let ((entry (assoc topic (game-handlers game))))
    (if entry
        (setf (cdr entry) (append (cdr entry) (list handler)))
        (setf (game-handlers game)
              (append (game-handlers game)
                      (list (cons topic (list handler)))))))
  topic)

(defun emit (game topic &rest args)
  "Emit event TOPIC: call each subscribed handler with GAME and ARGS."
  (dolist (h (cdr (assoc topic (game-handlers game))))
    (apply h game args))
  (values))

(defun say (game control &rest args)
  "Emit a :MESSAGE event with the FORMAT-ted text."
  (emit game :message (apply #'format nil control args)))

;;; ---------------------------------------------------------------------
;;; Story flags: arbitrary EQUAL-comparable keys the story sets and tests.
;;; Flags live in the save game, so anything stored must print readably.

(defun flag (game key)
  "The value of story flag KEY, or NIL when unset."
  (gethash key (game-flags game)))

(defun set-flag (game key &optional (value t))
  "Set story flag KEY to VALUE (default T)."
  (setf (gethash key (game-flags game)) value))

(defun clear-flag (game key)
  "Remove story flag KEY."
  (remhash key (game-flags game))
  (values))
