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
;;; Structured menu lines.  The menu generators (SHOP-LINES, CAST-LINES,
;;; SAVE-MENU-LINES, ...) return text lines; a line that stands for a
;;; pickable option carries the key that picks it as (TEXT . KEY), so a
;;; pointing front-end can turn a click on the line into that key press
;;; without parsing the text.  Plain informational lines stay strings.
;;; Footer hints keep their bracket convention — "[s] sell  [Esc] back"
;;; — and MENU-KEY-SPANS locates those tokens for per-segment hotspots.

(defun menu-option (key text)
  "TEXT as a menu line that key KEY picks."
  (cons text key))

(defun menu-numbered (i text)
  "TEXT as menu option row I, picked by the digit key I (1-9); rows
past 9 stay plain text — no key reaches them (the models only accept
single-digit picks)."
  (if (<= 1 i 9) (menu-option (digit-char i) text) text))

(defun menu-line-text (line)
  "The display text of a menu line (string or (TEXT . KEY))."
  (if (consp line) (car line) line))

(defun menu-line-key (line)
  "The key a menu line stands for, or NIL for a plain line."
  (if (consp line) (cdr line) nil))

(defun menu-texts (lines)
  "LINES with each menu line reduced to its display text."
  (mapcar #'menu-line-text lines))

(defun wrap-menu-line (line width)
  "Wrap a menu line like WRAP-TEXT, carrying its key (when it has one)
onto every wrapped row — a click on any row of a wrapped option picks
it."
  (let ((key (menu-line-key line)))
    (mapcar (lambda (row) (if key (menu-option key row) row))
            (wrap-text (menu-line-text line) width))))

;;; Menu scrolling: a list longer than a page shows a window of it,
;;; bracketed by "more" marker rows that carry the scroll keys (u up,
;;; d down) as their pick keys — so the markers click like any option
;;; row.  Digits pick within the visible window (row 1 is the window's
;;; first row), which also keeps every item of a long list reachable
;;; with the single-digit keys the models speak.  The window math is
;;; pure and shared: the *-LINES renderers and the *-ACT key handlers
;;; both go through MENU-WINDOW, so what is drawn and what a digit
;;; picks can never disagree.

(defconstant +menu-page-size+ 7
  "Menu rows a scrolling list may occupy at once.  At most this many
items show whole; a longer list scrolls, showing +MENU-PAGE-SIZE+ - 2
items per window (two rows go to the more-above/more-below markers).
Seven keeps every party-sized list (+PARTY-LIMIT+ rows) un-scrolled.")

(defun menu-window (n top &optional (page +menu-page-size+))
  "The visible window of an N-item list scrolled to offset TOP:
values (START END ABOVE-P BELOW-P), END exclusive.  A list of at most
PAGE items shows whole; a longer one shows PAGE - 2 items starting at
TOP (clamped so the window never runs off either end), ABOVE-P/BELOW-P
telling whether hidden items remain above/below."
  (if (<= n page)
      (values 0 n nil nil)
      (let* ((visible (- page 2))
             (start (max 0 (min top (- n visible))))
             (end (+ start visible)))
        (values start end (> start 0) (< end n)))))

(defun menu-window-pick (items top digit &optional (page +menu-page-size+))
  "The element of ITEMS that DIGIT picks in the window at TOP — digit 1
is the window's first visible row — or NIL when the digit runs past
the window.  Returns (values ITEM ABSOLUTE-INDEX)."
  (multiple-value-bind (start end) (menu-window (length items) top page)
    (let ((index (+ start (1- digit))))
      (when (and (<= 1 digit) (< index end))
        (values (nth index items) index)))))

(defun menu-scroll (top char n &optional (page +menu-page-size+))
  "The scroll offset after key CHAR on an N-item list at TOP: u/U a
window up, d/D a window down (each clamped at the ends), or NIL when
CHAR is no scroll key or the list does not scroll."
  (when (and (characterp char) (> n page))
    (let ((visible (- page 2)))
      (multiple-value-bind (start) (menu-window n top page)
        (case char
          ((#\u #\U) (max 0 (- start visible)))
          ((#\d #\D) (min (- n visible) (+ start visible)))
          (t nil))))))

(defun menu-scrolled-lines (items top render &optional (page +menu-page-size+))
  "Menu rows for ITEMS scrolled to TOP: each visible item rendered by
RENDER — called with the 1-based display row number and the item, and
returning a menu line (see MENU-NUMBERED) — with clickable marker rows
above/below the window when hidden items remain there.  The whole
section occupies at most PAGE rows."
  (multiple-value-bind (start end above below)
      (menu-window (length items) top page)
    (append
     (when above (list (menu-option #\u "^ more above [u]")))
     (let ((i 0))
       (mapcar (lambda (item)
                 (incf i)
                 (funcall render i item))
               (subseq items start end)))
     (when below (list (menu-option #\d "v more below [d]"))))))

(defun %menu-token-key (token)
  "The key a footer bracket token names: a single character stands for
itself, \"Esc\" and \"Return\" for those keys; range tokens (\"1-9\")
and anything else give NIL — the numbered option lines carry those."
  (cond ((= (length token) 1) (char token 0))
        ((string-equal token "Esc") #\Escape)
        ((string-equal token "Return") #\Return)
        (t nil)))

(defun menu-key-spans (text)
  "The clickable key hints in a footer line, by the generators' bracket
convention (\"[1-9] buy  [s] sell  [Esc] back\"): a list of
(START END KEY), END exclusive, each span running from its '[' over the
following words up to the next hint (two spaces or the next '[') so the
hint's label is part of its click target.  Range tokens and unmatched
brackets yield no span."
  (let ((spans '())
        (len (length text))
        (i 0))
    (loop
      (let ((open (position #\[ text :start i)))
        (unless open (return))
        (let ((close (position #\] text :start (1+ open))))
          (unless close (return))
          (let ((key (%menu-token-key (subseq text (1+ open) close)))
                ;; the span runs to the next hint: a "  " gap or '['
                (end (1+ close)))
            (loop while (and (< end len)
                             (char/= (char text end) #\[)
                             (not (and (char= (char text end) #\Space)
                                       (< (1+ end) len)
                                       (char= (char text (1+ end))
                                              #\Space))))
                  do (incf end))
            (loop while (and (> end (1+ close))
                             (char= (char text (1- end)) #\Space))
                  do (decf end))
            (when key
              (push (list open end key) spans))
            (setf i (1+ close))))))
    (nreverse spans)))

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
