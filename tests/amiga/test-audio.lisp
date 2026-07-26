;;; test-audio.lisp — audio.device tests (AMIGA.AUDIO)
;;;
;;; Loaded from run-tests.lisp via (load ...) inside #+amigaos.
;;; Must be a separate file because the reader needs the AMIGA.AUDIO
;;; package to exist before it can read the qualified symbols.
;;;
;;; NOTE: depends on the CHECK macro defined in run-tests.lisp.  The
;;; FASL system only invalidates this file's cache when *this* file's
;;; mtime changes, not when CHECK's definition changes — so if CHECK's
;;; expansion changes, touch this file too or you'll get "Undefined
;;; function: CHECK" at FASL load.

(require "amiga/audio")

(check "audio-package-exists" "AMIGA.AUDIO"
  (package-name (find-package "AMIGA.AUDIO")))

; --- Pure helpers ---
(check "audio-period-for-8kHz" 443 (amiga.audio:period-for-rate 8000))
(check "audio-period-clamps-to-hw-min" 124 (amiga.audio:period-for-rate 100000))
(check "audio-max-volume" 64 amiga.audio:+max-volume+)

; --- Delay via dos.library, so polling below is paced, not spinning ---
(defun audio-test-delay (ticks)
  (amiga.ffi:with-library (dos "dos.library")
    (amiga:call-library dos -198 (list :d1 ticks))))

; A second of square wave at 8 kHz: 50 samples up, 50 down (~80 Hz),
; as unsigned bytes carrying two's-complement signed values.
(defparameter *audio-test-wave*
  (let ((v (make-array 8000 :element-type '(unsigned-byte 8))))
    (dotimes (i 8000 v)
      (setf (aref v i) (if (evenp (floor i 50)) 100 156)))))

(defparameter *audio-test-chip* (amiga.exec:alloc-chip-bytes *audio-test-wave*))

; A buffer sized to the exact CMD_WRITE boundary (NDK audio.doc:
; ioa_Length valid "2 thru 131072, must be even number") so the
; length check can be probed right at the edge, not just past it.
(defparameter *audio-test-max-wave*
  (make-array amiga.audio:+max-sample-bytes+ :element-type '(unsigned-byte 8)
              :initial-element 128))

(defparameter *audio-test-max-chip*
  (amiga.exec:alloc-chip-bytes *audio-test-max-wave*))

(let ((audio (amiga.audio:open-audio)))
  (check "audio-open" t (not (null audio)))
  (when audio
    (check "audio-channel-mask-is-one-channel" t
      (not (null (member (amiga.audio:audio-channel-mask audio) '(1 2 4 8)))))

    ; Full second queued: still audibly playing right after SendIO.
    (check "audio-play-sample-queues" t
      (amiga.audio:play-sample audio *audio-test-chip* 8000
                               :period (amiga.audio:period-for-rate 8000)))
    (check "audio-playing-after-start" t (amiga.audio:playing-p audio))
    (check "audio-stop-sample" nil (amiga.audio:stop-sample audio))
    (check "audio-not-playing-after-stop" nil (amiga.audio:playing-p audio))

    ; A short one-shot (~50 ms) completes on its own within 2 s of polling.
    (check "audio-short-sample-completes" t
      (progn
        (amiga.audio:play-sample audio *audio-test-chip* 400)
        (let ((done nil))
          (dotimes (i 100)
            (unless (amiga.audio:playing-p audio)
              (setq done t)
              (return))
            (audio-test-delay 1))
          done)))

    ; CYCLES 0 loops until stopped: still playing long after 400 bytes
    ; (~50 ms) would have run out.
    (check "audio-cycles-zero-loops" t
      (progn
        (amiga.audio:play-sample audio *audio-test-chip* 400 :cycles 0)
        (audio-test-delay 25)
        (prog1 (amiga.audio:playing-p audio)
          (amiga.audio:stop-sample audio))))

    ; Odd or oversized lengths are rejected before touching the device.
    (check "audio-odd-length-errors" t
      (handler-case
          (progn (amiga.audio:play-sample audio *audio-test-chip* 401) nil)
        (error () t)))
    (check "audio-oversized-length-errors" t
      (handler-case
          (progn (amiga.audio:play-sample
                  audio *audio-test-max-chip*
                  (+ amiga.audio:+max-sample-bytes+ 2))
                 nil)
        (error () t)))

    ; The boundary itself (NDK audio.doc's documented max, 131072) is
    ; accepted, not rejected as out of range.
    (check "audio-max-sample-bytes-is-131072" 131072 amiga.audio:+max-sample-bytes+)
    (check "audio-max-length-accepted" t
      (prog1 (amiga.audio:play-sample audio *audio-test-max-chip*
                                       amiga.audio:+max-sample-bytes+)
        (amiga.audio:stop-sample audio)))

    ; A second handle gets a different channel; both close cleanly.
    (let ((audio2 (amiga.audio:open-audio)))
      (check "audio-second-channel" t
        (and audio2
             (member (amiga.audio:audio-channel-mask audio2) '(1 2 4 8))
             (/= (amiga.audio:audio-channel-mask audio2)
                 (amiga.audio:audio-channel-mask audio))))
      (when audio2 (amiga.audio:close-audio audio2)))

    (check "audio-close" nil (amiga.audio:close-audio audio))))

; Channel freed on close: reopening still finds one, WITH-AUDIO cleans up.
(check "audio-with-audio-reopens" t
  (amiga.audio:with-audio (a)
    (not (null (member (amiga.audio:audio-channel-mask a) '(1 2 4 8))))))

(amiga:free-chip *audio-test-chip*)
(amiga:free-chip *audio-test-max-chip*)
