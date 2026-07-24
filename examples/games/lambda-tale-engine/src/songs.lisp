;;; Lambda's Tale — bard songs.
;;;
;;; Song types are campaign data, not engine facts: the campaign
;;; registers them with DEFINE-SONG in its campaign.lisp; the engine
;;; only knows the mechanics.  Singers (hero classes with :SINGER T)
;;; pay TUNES — one charge per song, one charge per level when rested
;;; (Bard's Tale songs-per-day), refilled with a drink at a :TAVERN
;;; location (see locations.lisp).
;;;
;;; A song is always a timed effect over the shared vocabulary
;;; (*TIMED-EFFECT-KEYS* in game.lisp — :buff-ac, :light, :compass,
;;; :buff-damage, :regen-sp, :extra-attacks, :combat-heal ... plus
;;; :duration; keys combine, so one tune can quicken sp on the road
;;; AND arms in a fight), and only ONE song plays at a time: its
;;; effect carries a :SONG marker in the payload, and striking up a
;;; new song displaces the old — the Bard's Tale rule.
;;;
;;; The sing interaction is modeled here too, platform-free (the
;;; CAST-VIEW pattern): a SING-VIEW holds the menu state, SING-LINES
;;; renders it as text lines and SING-ACT maps a key press to
;;; mechanics — both front-ends drive the same model.  In combat,
;;; (:sing SONG) is a party action beside :attack and (:cast ...).

(in-package :tale)

(defstruct (song-type (:constructor %make-song-type))
  name                ; symbol, e.g. TRAVELLERS-TUNE
  title               ; display string, e.g. "travellers tune"
  (level 1)           ; minimum singer level
  effect              ; timed effect plist, e.g. (:buff-ac 2 :duration 60)
                      ; or (:regen-sp 2 :extra-attacks 1 :duration 60)
  image               ; effects-band icon file, or NIL
  notes)              ; designer notes (canon effect text, stand-in
                      ; remarks) — carried as data so generated
                      ; songbooks show them; no mechanics

(defvar *song-types* (make-hash-table :test 'eq))
(defvar *song-names* '()
  "Song names in registration order — the stable order of the menus.")

(defparameter %song-meta-keys '(:title :level :image :notes)
  "DEFINE-SONG's non-effect keywords; everything else in the argument
plist is the effect spec.")

(defun define-song (name &rest args)
  "Register song type NAME (a symbol).  Campaign data calls this.
ARGS is a plist: :TITLE, :LEVEL (minimum singer level, default 1),
:IMAGE (the effects-band icon), :NOTES (a designer-facing string —
canon effect text, stand-in remarks — data, not mechanics, so
generated songbooks can surface it) — and the effect spec itself, one
or more keys of the TIMED vocabulary (*TIMED-EFFECT-KEYS* in game.lisp;
keys combine into one effect record).  Songs are always timed, so
:DURATION (game minutes, or :INDEFINITE) is required.  TITLE defaults
to the downcased name (TRAVELLERS-TUNE -> \"travellers tune\")."
  (loop for tail on args by #'cddr
        unless (and (keywordp (first tail)) (cdr tail))
          do (error "define-song ~S: malformed argument plist at ~S"
                    name tail))
  (let ((notes (getf args :notes)))
    (when (and notes (not (stringp notes)))
      (error "define-song ~S: :notes must be a string (got ~S)"
             name notes)))
  (let ((spec (loop for (key value) on args by #'cddr
                    unless (member key %song-meta-keys)
                      append (list key value))))
    (check-effect-spec "define-song" name spec :timed-only t)
    (setf (gethash name *song-types*)
          (%make-song-type
           :name name
           :title (or (getf args :title)
                      (string-downcase (substitute #\Space #\- (string name))))
           :level (or (getf args :level) 1)
           :effect spec
           :image (getf args :image)
           :notes (getf args :notes))))
  ;; keep the registration order; a re-registration keeps its spot
  (unless (member name *song-names*)
    (setf *song-names* (append *song-names* (list name))))
  name)

(defun find-song-type (name)
  (or (gethash name *song-types*)
      (error "Unknown song ~S (register it with DEFINE-SONG)" name)))

(defun song-title (name)
  (song-type-title (find-song-type name)))

(defun song-known-p (hero name)
  "Does HERO know song NAME?  A singer who has reached the song's
level — every singer learns every song of their level."
  (and (hero-singer-p hero)
       (>= (hero-level hero) (song-type-level (find-song-type name)))
       t))

(defun songs-for-hero (hero)
  "The songs HERO knows, in registration order."
  (remove-if-not (lambda (name) (song-known-p hero name))
                 *song-names*))

(defun current-song (game)
  "The active song's EFFECT (the one whose payload carries the :SONG
marker), or NIL when no song plays."
  (find-if (lambda (e) (getf (effect-payload e) :song))
           (game-effects game)))

(defun sing-song (game hero name)
  "HERO strikes up song NAME.  Says why and returns NIL when the hero
does not know it or has no tunes left; otherwise spends one tune,
displaces any song already playing, installs the song's timed effect
(:SONG-marked), emits :SONG-SUNG and returns T."
  (let ((type (find-song-type name)))
    (cond
      ((not (song-known-p hero name))
       (say game "~A does not know ~A." (hero-name hero)
            (song-type-title type))
       nil)
      ((< (hero-tunes hero) 1)
       (say game "~A has no tunes left — the tavern would help."
            (hero-name hero))
       nil)
      (t
       (decf (hero-tunes hero))
       (let ((old (current-song game)))
         (when old
           (setf (game-effects game)
                 (remove old (game-effects game)))))
       (say game "~A strikes up ~A!" (hero-name hero)
            (song-type-title type))
       (apply-effect-spec game (song-type-title type)
                          (song-type-effect type)
                          :image (song-type-image type)
                          :extra-payload '(:song t))
       (emit game :song-sung hero name)
       t))))

;;; ---------------------------------------------------------------------
;;; The sing interaction model (shared by both front-ends).

(defstruct (sing-view (:constructor %make-sing-view))
  hero                ; the chosen singer, or NIL while picking
  in-combat           ; T: committing fights one COMBAT-ROUND;
                      ; :ORDERS: committing returns the pick as a
                      ; round action (the combat-orders flow)
  (top 0))            ; scroll offset into the song list

(defun make-sing-view (&key in-combat hero)
  "HERO presets the singer (the combat-orders flow asks for one hero's
pick); NIL starts at the who-plays page."
  (%make-sing-view :in-combat in-combat :hero hero))

(defun %sing-commit (game view name)
  "Resolve the completed pick: sing directly; in combat fight one
round where the singer sings and everyone else attacks; in :ORDERS
mode hand the pick back as (:ACTION (:SING NAME))."
  (let ((hero (sing-view-hero view)))
    (cond
      ((eq (sing-view-in-combat view) :orders)
       (list :action (list :sing name)))
      ((sing-view-in-combat view)
       (combat-round game
                     (mapcar (lambda (h)
                               (if (eq h hero) (list :sing name) :attack))
                             (alive-heroes game)))
       :done)
      (t (sing-song game hero name)
         :done))))

(defun sing-lines (game view)
  "The current sing menu as a list of menu lines — the front-ends draw
these verbatim (the SHOP-LINES pattern); option rows carry their pick
key (see MENU-NUMBERED)."
  (let ((hero (sing-view-hero view)))
    (append
     (list "*** Play a Song ***" "")
     (if (null hero)
         (append
          (list "Who plays?" "")
          (let ((i 0))
            (mapcan (lambda (h)
                      (incf i)
                      (when (hero-singer-p h)
                        (list (menu-numbered
                               i (format nil "~D) ~A  (Tunes ~D/~D)"
                                         i (hero-name h)
                                         (hero-tunes h)
                                         (hero-max-tunes h))))))
                    (game-party game)))
          (list "" "[1-7] choose  [Esc] cancel"))
         (append
          (list (format nil "~A plays.  Tunes ~D/~D"
                        (hero-name hero) (hero-tunes hero)
                        (hero-max-tunes hero))
                "")
          (menu-scrolled-lines
           (songs-for-hero hero) (sing-view-top view)
           (lambda (i name)
             (menu-numbered
              i (format nil "~D) ~A" i (song-title name)))))
          (list "" "[1-9] play  [Esc] back"))))))

(defun sing-act (game view char)
  "Apply key CHAR to the sing menu.  Returns :DONE when a song
resolved (the front-end drops the view) — in :ORDERS mode
\(:ACTION (:SING ...)) instead — :CANCELLED on Esc at the top level,
else NIL."
  (let ((hero (sing-view-hero view))
        (digit (digit-char-p char)))
    (cond
      ;; picking the singer
      ((null hero)
       (cond ((and digit (<= 1 digit (length (game-party game))))
              (let ((h (nth (1- digit) (game-party game))))
                (when (and (hero-singer-p h) (hero-alive-p h))
                  (setf (sing-view-hero view) h)))
              nil)
             ((eql char #\Escape) :cancelled)
             (t nil)))
      ;; picking the song
      (t
       (cond (digit
              (let ((name (menu-window-pick (songs-for-hero hero)
                                            (sing-view-top view) digit)))
                (when name
                  (%sing-commit game view name))))
             ((eql char #\Escape)
              (setf (sing-view-hero view) nil
                    (sing-view-top view) 0)
              nil)
             (t
              (let ((top (menu-scroll (sing-view-top view) char
                                      (length (songs-for-hero hero)))))
                (when top (setf (sing-view-top view) top)))
              nil))))))
