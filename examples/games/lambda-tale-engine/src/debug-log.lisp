;;; Lambda's Tale — the debug log.
;;;
;;; A timestamped trace of what the engine is doing, written to a file
;;; so a session on real hardware (or in the emulator) leaves evidence:
;;; image loads with their durations, zone/location transitions, every
;;; key press, every event emitted to its handlers.  Off by default and
;;; free when off — DLOG compiles to one special-variable test.
;;;
;;;   (debug-log-enable)            start logging to *DEBUG-LOG-PATH*
;;;   (debug-log-enable "ram:t.log") ... or to a named file (appends)
;;;   (debug-log-disable)           stop and close the file
;;;
;;; Setting the environment variable TALE_DEBUG_LOG enables the log at
;;; engine load: "1"/"t"/"yes"/"on" use the default path, any other
;;; value is the path itself.
;;;
;;; Every line carries a wall-clock timestamp with a millisecond
;;; fraction; both are derived from GET-INTERNAL-REAL-TIME against a
;;; wall-clock anchor captured at DEBUG-LOG-ENABLE, so the printed
;;; ".mmm" is the true sub-second remainder of the printed HH:MM:SS
;;; even where GET-UNIVERSAL-TIME itself only ticks in seconds. The
;;; stream is flushed per line — a crash keeps the trace up to the
;;; moment it happened, which is the whole point on a 68020.

(in-package :tale)

(defvar *debug-log-stream* nil
  "The open debug-log character stream, or NIL — logging disabled.
Not for direct use: DEBUG-LOG-ENABLE / DEBUG-LOG-DISABLE manage it,
DLOG / DLOG-TIMED write through it.")

(defvar *debug-log-path* "tale-debug.log"
  "Where DEBUG-LOG-ENABLE opens the log when no path is given.
Resolved like any relative path — against the working directory, which
belongs to the game.")

(defvar *debug-log-anchor-real* nil
  "GET-INTERNAL-REAL-TIME captured at DEBUG-LOG-ENABLE.  Paired with
*DEBUG-LOG-ANCHOR-UNIVERSAL* so every printed timestamp — whole
seconds and millisecond fraction alike — derives from the same clock;
GET-UNIVERSAL-TIME and GET-INTERNAL-REAL-TIME have independent epochs
and must never be mixed within one timestamp.")

(defvar *debug-log-anchor-universal* nil
  "GET-UNIVERSAL-TIME captured at the same moment as
*DEBUG-LOG-ANCHOR-REAL*.")

(defun debug-log-enabled-p ()
  "True while the debug log is open and DLOG lines are being written."
  (not (null *debug-log-stream*)))

(defun %dlog-elapsed-ms (start)
  "Milliseconds of internal real time since START."
  (round (* 1000 (- (get-internal-real-time) start))
         internal-time-units-per-second))

(defun %dlog-write (text)
  "Write TEXT to the debug log as one timestamped, flushed line.
The whole-second and millisecond fields are both derived from
GET-INTERNAL-REAL-TIME, offset against the wall-clock anchor captured
at DEBUG-LOG-ENABLE, so the printed \".mmm\" is always the true
sub-second remainder of the printed HH:MM:SS instead of a value from
an unrelated clock."
  (let ((s *debug-log-stream*))
    (when s
      (let* ((elapsed-ms (round (* 1000 (- (get-internal-real-time)
                                            *debug-log-anchor-real*))
                                 internal-time-units-per-second))
             (total-ms (+ (* 1000 *debug-log-anchor-universal*) elapsed-ms)))
        (multiple-value-bind (whole-sec ms) (floor total-ms 1000)
          (multiple-value-bind (sec min hour day month year)
              (decode-universal-time whole-sec)
            (format s "[~4,'0D-~2,'0D-~2,'0D ~2,'0D:~2,'0D:~2,'0D.~3,'0D] ~A~%"
                    year month day hour min sec ms text))))
      (force-output s))))

(defun %dlog (control &rest args)
  "FORMAT one line into the debug log (see DLOG for the guarded entry)."
  (%dlog-write (apply #'format nil control args)))

(defmacro dlog (control &rest args)
  "Log one FORMAT-ted line to the debug log.  When the log is disabled
this is a single special-variable test — the arguments are not even
evaluated — so DLOG may sit on hot paths."
  `(when *debug-log-stream*
     (%dlog ,control ,@args)))

(defmacro dlog-timed ((control &rest args) &body body)
  "Run BODY, returning its values.  When the debug log is enabled,
bracket it in the log: the FORMAT-ted label with \"...\" before, and
with \"done [N ms]\" after — so the log shows both what was in flight
when a session died and how long each load took.  Disabled, BODY runs
bare."
  (let ((label (gensym "LABEL"))
        (start (gensym "START"))
        (vals (gensym "VALS")))
    `(if *debug-log-stream*
         (let ((,label (format nil ,control ,@args))
               (,start (get-internal-real-time)))
           (%dlog "~A ..." ,label)
           (let ((,vals (multiple-value-list (progn ,@body))))
             (%dlog "~A done [~D ms]" ,label (%dlog-elapsed-ms ,start))
             (values-list ,vals)))
         (progn ,@body))))

(defun %dlog-brief (x)
  "X as a short string for an event-trace line: engine structures show
their identity (a map's title, a hero's name) instead of their whole
graph; anything else prints bounded."
  (typecase x
    (dungeon-map (format nil "#<map ~A>" (map-title x)))
    (location (format nil "#<location ~A>" (location-title x)))
    (hero (format nil "#<hero ~A>" (hero-name x)))
    (effect (format nil "#<effect ~A>" (effect-name x)))
    (t (let ((*print-length* 8)
             (*print-level* 3))
         (prin1-to-string x)))))

(defun debug-log-enable (&optional (path *debug-log-path*))
  "Open the debug log at PATH (appending to an existing file) and
start logging; returns PATH, which also becomes *DEBUG-LOG-PATH*.
Enabling while enabled switches files."
  (debug-log-disable)
  (setf *debug-log-path* path
        *debug-log-anchor-real* (get-internal-real-time)
        *debug-log-anchor-universal* (get-universal-time)
        *debug-log-stream* (open path :direction :output
                                      :if-exists :append
                                      :if-does-not-exist :create))
  (%dlog "=== Lambda's Tale debug log enabled (~A) ==="
         (or #+amigaos "amigaos" "host"))
  path)

(defun debug-log-disable ()
  "Close the debug log and stop logging; a no-op when disabled."
  (let ((s *debug-log-stream*))
    (when s
      (%dlog "=== debug log disabled ===")
      (setf *debug-log-stream* nil)
      (close s)))
  (values))

;;; TALE_DEBUG_LOG in the environment enables the log as the engine
;;; loads — the way to trace a session without touching game code.
(let ((spec (ext:getenv "TALE_DEBUG_LOG")))
  (when (and spec (string/= spec ""))
    (debug-log-enable
     (if (member spec '("1" "t" "yes" "on") :test #'string-equal)
         *debug-log-path*
         spec))))
