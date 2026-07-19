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
;;;   :enter-zone MAP        the party traveled to another zone (map)
;;;   :enter-location LOC    the party entered a location (shop, ...)
;;;   :leave-location LOC    ... and left it again
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
  "Emit event TOPIC: call each subscribed handler with GAME and ARGS.
With the debug log enabled, every emission leaves a trace line: the
topic, the arguments (briefly), the handler count and the time the
handlers took."
  (let ((handlers (cdr (assoc topic (game-handlers game)))))
    (if *debug-log-stream*
        (let ((start (get-internal-real-time)))
          (dolist (h handlers)
            (apply h game args))
          (%dlog "event ~S~{ ~A~} handlers=~D [~D ms]"
                 topic (mapcar #'%dlog-brief args) (length handlers)
                 (%dlog-elapsed-ms start)))
        (dolist (h handlers)
          (apply h game args))))
  (values))

(defun say (game control &rest args)
  "Emit a :MESSAGE event with the FORMAT-ted text."
  (emit game :message (apply #'format nil control args)))

;;; ---------------------------------------------------------------------
;;; Message log: the Bard's Tale-style text column.  Front-ends attach
;;; one to a game and render its trailing lines, newest at the bottom.

(defstruct (message-log (:constructor %make-message-log))
  (lines '())         ; newest first
  (limit 100))

(defun attach-message-log (game &key (limit 100))
  "Subscribe a fresh MESSAGE-LOG to GAME's :MESSAGE events and return
it.  The log keeps the most recent LIMIT messages."
  (let ((log (%make-message-log :limit limit)))
    (on-event game :message
              (lambda (g text)
                (declare (ignore g))
                (log-message log text)))
    log))

(defun log-message (log text)
  "Append TEXT to LOG, dropping the oldest line beyond the limit."
  (push text (message-log-lines log))
  (let ((tail (nthcdr (1- (message-log-limit log))
                      (message-log-lines log))))
    (when (consp tail)
      (setf (cdr tail) nil)))
  text)

(defun log-recent (log n)
  "The last N messages logged to LOG, oldest first — ready to draw top
to bottom with the newest line at the bottom."
  (let ((lines (message-log-lines log)))
    (reverse (subseq lines 0 (min n (length lines))))))

(defun wrap-text (text width)
  "Split TEXT into a list of lines at most WIDTH characters long,
breaking at spaces; a word longer than WIDTH breaks mid-word.  Always
returns at least one line (\"\" wraps to (\"\"))."
  (let ((width (max 1 width))
        (lines '()))
    (loop
      (when (<= (length text) width)
        (push text lines)
        (return))
      (let ((break (position #\Space text :from-end t :end (1+ width))))
        (if (and break (plusp break))
            (setf lines (cons (subseq text 0 break) lines)
                  text (subseq text (1+ break)))
            (setf lines (cons (subseq text 0 width) lines)
                  text (subseq text width)))))
    (nreverse lines)))

(defun wrap-message (text width)
  "Wrap TEXT like WRAP-TEXT, marking where the message starts: the
first line is prefixed with \"> \", continuation lines are indented to
align.  Lines stay at most WIDTH characters long."
  (let ((first t))
    (mapcar (lambda (line)
              (prog1 (concatenate 'string (if first "> " "  ") line)
                (setf first nil)))
            (wrap-text text (- (max 3 width) 2)))))

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
