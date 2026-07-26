;;; amiga/audio.lisp — audio.device abstractions for CL-Amiga
;;;
;;; Loaded via (require "amiga/audio").
;;; Allocates a Paula channel through audio.device and plays 8-bit
;;; signed samples from chip RAM.  Playback is strictly non-blocking:
;;; a sound is started with SendIO and reclaimed with CheckIO/AbortIO
;;; — WAIT-IO is only ever issued once the request is complete or
;;; aborted, so no call here stalls the caller (or the stop-the-world
;;; GC) for the duration of a sample.
;;;
;;; See tests/amiga/test-audio.lisp for usage end-to-end.

(require "amiga/ffi")
(require "amiga/exec")

(defpackage "AMIGA.AUDIO"
  (:use "CL" "FFI" "AMIGA.FFI")
  (:export
   ;; Channel handle
   "OPEN-AUDIO" "CLOSE-AUDIO" "WITH-AUDIO"
   "AUDIO-CHANNEL-MASK"
   ;; Playback
   "PLAY-SAMPLE" "STOP-SAMPLE" "PLAYING-P"
   ;; Helpers
   "PERIOD-FOR-RATE"
   "+MAX-VOLUME+" "+MAX-SAMPLE-BYTES+"))

(in-package "AMIGA.AUDIO")

;;; ================================================================
;;; LVO offsets (from amiga/exec_lib.fd)
;;; ================================================================

(defconstant +lvo-open-device+       -444)  ; A0 name, D0 unit, A1 io, D1 flags
(defconstant +lvo-close-device+      -450)  ; A1 io
(defconstant +lvo-send-io+           -462)  ; A1 io
(defconstant +lvo-check-io+          -468)  ; A1 io
(defconstant +lvo-wait-io+           -474)  ; A1 io
(defconstant +lvo-abort-io+          -480)  ; A1 io
(defconstant +lvo-create-io-request+ -654)  ; A0 port, D0 size
(defconstant +lvo-delete-io-request+ -660)  ; A0 io
(defconstant +lvo-create-msg-port+   -666)
(defconstant +lvo-delete-msg-port+   -672)  ; A0 port

;;; ================================================================
;;; struct IOAudio (devices/audio.h) — 68 bytes.
;;; The embedded IORequest fields the module touches, then the
;;; audio-specific tail.  m68k struct alignment is 2 bytes, so the
;;; ioa_Data pointer really does sit at offset 34.
;;; ================================================================

(ffi:defcstruct ioaudio
  (ln-pri    :i8      9)   ; allocation precedence (message node priority)
  (unit      :u32    24)   ; allocated channel mask, filled by OpenDevice
  (command   :u16    28)
  (flags     :u8     30)
  (error     :i8     31)
  (alloc-key :u16    32)
  (data      :u32    34)   ; sample/channel-map address — poke the integer address
  (length    :u32    38)
  (period    :u16    42)
  (volume    :u16    44)
  (cycles    :u16    46))

(defconstant +ioaudio-size+ 68)

;;; Commands (exec/io.h, devices/audio.h)
(defconstant +cmd-write+ 3)

;;; io_Flags bits (devices/audio.h)
(defconstant +adiof-pervol+ (ash 1 4))  ; take ioa_Period/ioa_Volume from the request
(defconstant +adiof-nowait+ (ash 1 6))  ; fail allocation instead of queueing for a channel

(defconstant +max-volume+ 64)

;;; Paula's per-channel length register counts 16-bit words, so one
;;; request carries at most 65536 words of sample data (NDK audio.doc:
;;; CMD_WRITE's ioa_Length is valid "2 thru 131072, must be even number").
(defconstant +max-sample-bytes+ 131072)

;;; PAL colour clock — period = clock / sample rate.  (NTSC differs by
;;; 0.9%, far below what an 8-bit sample effect can carry.)
(defconstant +pal-clock+ 3546895)

(defun period-for-rate (rate)
  "Paula period for a sample RATE in Hz (PAL clock).  Clamped to the
hardware minimum of 124 (about 28.6 kHz)."
  (max 124 (round +pal-clock+ rate)))

;;; ================================================================
;;; Exec device-I/O calls — dynamic CALL-LIBRARY on ExecBase, the
;;; same idiom as AMIGA.EXEC's AVAIL-MEM.
;;; ================================================================

(defun %exec (lvo regs)
  (amiga:call-library amiga.exec:*exec-base* lvo regs))

;;; ================================================================
;;; The audio handle — one allocated channel, one reusable IOAudio.
;;; ================================================================

(defstruct (audio (:constructor %make-audio))
  port           ; MsgPort the device replies to
  io             ; the IOAudio request (foreign pointer)
  (pending nil)) ; T while a CMD_WRITE is out with the device

(defun audio-channel-mask (audio)
  "The Paula channel mask (1, 2, 4 or 8) OpenDevice allocated."
  (ioaudio-unit (audio-io audio)))

(defun open-audio (&key (precedence 0))
  "Allocate one Paula channel through audio.device and return an AUDIO
handle, or NIL if no channel (or no audio.device) is available.
PRECEDENCE (-128..127) is the allocation priority; equal-or-louder
users can steal the channel back.  Never blocks: allocation runs with
ADIOF_NOWAIT."
  (let ((port nil) (io nil) (chanmap nil) (name nil) (ok nil))
    (unwind-protect
        (progn
          (setf port (%exec +lvo-create-msg-port+ nil))
          (when (zerop port) (return-from open-audio nil))
          (setf port (ffi:make-foreign-pointer port))
          (setf io (%exec +lvo-create-io-request+
                          (list :a0 port :d0 +ioaudio-size+)))
          (when (zerop io) (return-from open-audio nil))
          (setf io (ffi:make-foreign-pointer io))
          ;; OpenDevice on audio.device doubles as ADCMD_ALLOCATE when
          ;; ioa_Length is non-zero: ioa_Data points at an array of
          ;; acceptable channel masks, one byte each — any single
          ;; channel suits us.
          (setf chanmap (ffi:alloc-foreign 4))
          (ffi:poke-bytes chanmap #(1 2 4 8))
          (setf (ioaudio-ln-pri io) (logand precedence #xFF))
          (setf (ioaudio-flags io) +adiof-nowait+)
          (setf (ioaudio-data io) (ffi:foreign-pointer-address chanmap))
          (setf (ioaudio-length io) 4)
          (setf name (ffi:foreign-string "audio.device"))
          (unless (zerop (%exec +lvo-open-device+
                                (list :a0 name :d0 0 :a1 io :d1 0)))
            (return-from open-audio nil))
          (setf ok t)
          (%make-audio :port port :io io))
      ;; The channel-map and name buffers are only read inside
      ;; OpenDevice; the partial plumbing only survives a success.
      (when chanmap (ffi:free-foreign chanmap))
      (when name (ffi:free-foreign name))
      (unless ok
        (when (ffi:foreign-pointer-p io)
          (%exec +lvo-delete-io-request+ (list :a0 io)))
        (when (ffi:foreign-pointer-p port)
          (%exec +lvo-delete-msg-port+ (list :a0 port)))))))

(defun %reclaim (audio)
  "Take the in-flight request back from the device, aborting it if it
is still playing.  WAIT-IO here only ever completes an already done or
just-aborted request, so the wait is bounded and effectively instant."
  (when (audio-pending audio)
    (let ((io (audio-io audio)))
      (when (zerop (%exec +lvo-check-io+ (list :a1 io)))
        (%exec +lvo-abort-io+ (list :a1 io)))
      (%exec +lvo-wait-io+ (list :a1 io)))
    (setf (audio-pending audio) nil)))

(defun playing-p (audio)
  "True while the last PLAY-SAMPLE is still sounding.  Reclaims the
request as a side effect once the device has finished with it."
  (when (audio-pending audio)
    (if (zerop (%exec +lvo-check-io+ (list :a1 (audio-io audio))))
        t
        (progn (%reclaim audio) nil))))

(defun stop-sample (audio)
  "Silence the channel: abort any in-flight write."
  (%reclaim audio)
  nil)

(defun play-sample (audio chip-data length &key (period 443)
                                                (volume +max-volume+)
                                                (cycles 1))
  "Start LENGTH bytes of signed 8-bit sample data at CHIP-DATA (a
chip-RAM pointer, e.g. from AMIGA.EXEC:ALLOC-CHIP-BYTES) on the
handle's channel.  Any sound still playing is cut off first.  PERIOD
is the Paula period (see PERIOD-FOR-RATE), VOLUME 0..64, CYCLES the
repeat count (0 loops until STOP-SAMPLE).  Returns immediately; T when
the write was queued, NIL if the device rejected it."
  (when (or (oddp length) (< length 2) (> length +max-sample-bytes+))
    (error "PLAY-SAMPLE: length must be even and in 2..~D: ~D"
           +max-sample-bytes+ length))
  (%reclaim audio)
  (let ((io (audio-io audio)))
    (setf (ioaudio-command io) +cmd-write+)
    (setf (ioaudio-flags io) +adiof-pervol+)
    (setf (ioaudio-data io) (ffi:foreign-pointer-address chip-data))
    (setf (ioaudio-length io) length)
    (setf (ioaudio-period io) period)
    (setf (ioaudio-volume io) (min +max-volume+ (max 0 volume)))
    (setf (ioaudio-cycles io) cycles)
    (%exec +lvo-send-io+ (list :a1 io))
    (setf (audio-pending audio) t)
    ;; SendIO has no return value; a rejected request comes back
    ;; completed with io_Error set.
    (if (and (not (zerop (%exec +lvo-check-io+ (list :a1 io))))
             (not (zerop (ioaudio-error io))))
        (progn (%reclaim audio) nil)
        t)))

(defun close-audio (audio)
  "Stop playback, free the channel and all request plumbing."
  (%reclaim audio)
  (%exec +lvo-close-device+ (list :a1 (audio-io audio)))
  (%exec +lvo-delete-io-request+ (list :a0 (audio-io audio)))
  (%exec +lvo-delete-msg-port+ (list :a0 (audio-port audio)))
  (setf (audio-io audio) nil (audio-port audio) nil)
  nil)

(defmacro with-audio ((var &rest open-args) &body body)
  "Open an audio channel, bind the handle to VAR, close on exit.
Signals an error when no channel is available."
  `(let ((,var (open-audio ,@open-args)))
     (unless ,var (error "Cannot allocate an audio.device channel"))
     (unwind-protect (progn ,@body)
       (close-audio ,var))))

;;; ================================================================
;;; Provide module
;;; ================================================================

(provide "amiga/audio")
