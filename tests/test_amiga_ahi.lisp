;;; test_amiga_ahi.lisp — host-side checks of lib/amiga/ahi.lisp
;;; (AMIGA.AHI, the opt-in AHI module), driven by tests/test_amiga_ahi.sh.
;;;
;;; ahi.device exists on AmigaOS/MorphOS only (tests/amiga/test-ahi.lisp
;;; is the functional specification, run by the FS-UAE suite whose
;;; Workbench carries AHI 4.18).  What the host pins down: the module
;;; compiles and loads, and the generated AMIGA.RAW.AHI table with it;
;;; OPEN-AHI answers NIL here instead of erroring; the pure helpers
;;; (fixed point, sample types, sample packing) are right; every argument
;;; check fires before a device would be touched; the function interface
;;; refuses cleanly while nothing is open; the example loads and bows out.

(defvar *pass* 0)
(defvar *fail* 0)

(defmacro check (name expected form)
  `(handler-case
       (let ((e ,expected) (a ,form))
         (if (equal e a)
             (progn (incf *pass*) (format t "PASS: ~A~%" ,name))
             (progn (incf *fail*) (format t "FAIL: ~A - expected ~S got ~S~%" ,name e a))))
     (error (c)
       (incf *fail*)
       (format t "FAIL: ~A - signaled error: ~A~%" ,name c))))

(defun error-mentions (needle thunk)
  "T when THUNK signals an error whose message contains NEEDLE."
  (handler-case (progn (funcall thunk) nil)
    (error (e) (and (search needle (format nil "~A" e)) t))))

(require "amiga/ahi")

;;; --- no device off AmigaOS: NIL, not an error ---
(check "host-ahi-open-not-available" nil (amiga.ahi:open-ahi))
(check "host-ahi-open-none-not-available" nil (amiga.ahi:open-ahi :unit :none))
(check "host-ahi-open-bad-unit-errors" t
  (error-mentions "unit" (lambda () (amiga.ahi:open-ahi :unit 300))))
(check "host-ahi-with-ahi-signals" t
  (error-mentions "ahi.device" (lambda () (amiga.ahi:with-ahi (a) a))))
(check "host-ahi-raw-base-stays-nil" nil amiga.raw.ahi:*ahi-base*)

;;; --- AMIGA.EXEC:DO-IO (the synchronous exec DoIO wrapper AMIGA.AHI's
;;; module doc mentions but never calls itself, since the device
;;; interface stays non-blocking): off AmigaOS every AMIGA:CALL-LIBRARY
;;; is a clean error, so this is DO-IO's host-side check ---
(check "host-exec-do-io-not-available" t
  (error-mentions "only available on"
    (lambda () (amiga.exec:do-io (ffi:make-foreign-pointer 0)))))

;;; --- OPEN-AHI/CLOSE-AHI's shared *OPEN-HANDLES* counter is guarded by
;;; AMIGA.AHI::*OPEN-HANDLES-LOCK* against a lost update when two threads
;;; race through it (MP threads are real preemptive threads on host too,
;;; not just AmigaOS): 20 threads each doing 500 locked increments must
;;; land on exactly 10000, not less.
(check "host-ahi-open-handles-lock-serializes" (* 20 500)
  (progn
    (setf amiga.ahi::*open-handles* 0)
    (let ((threads
            (loop repeat 20
                  collect (mp:make-thread
                           (lambda ()
                             (dotimes (i 500)
                               (mp:with-lock-held (amiga.ahi::*open-handles-lock*)
                                 (incf amiga.ahi::*open-handles*))))
                           :name "ahi-lock-test"))))
      (dolist (th threads) (mp:join-thread th)))
    (prog1 amiga.ahi::*open-handles*
      (setf amiga.ahi::*open-handles* 0))))

;;; --- pure helpers ---
(check "host-ahi-to-fixed" '(65536 32768 0)
  (list (amiga.ahi:to-fixed 1.0) (amiga.ahi:to-fixed 1/2) (amiga.ahi:to-fixed 0)))
(check "host-ahi-from-fixed" 1/2 (amiga.ahi:from-fixed #x8000))
(check "host-ahi-frame-sizes" '(1 2 2 4)
  (mapcar #'amiga.ahi:sample-frame-size '(:mono8 :mono16 :stereo8 :stereo16)))
(check "host-ahi-type-codes"
  (list amiga.raw.ahi:+ahist-m8-s+ amiga.raw.ahi:+ahist-m16-s+
        amiga.raw.ahi:+ahist-s8-s+ amiga.raw.ahi:+ahist-s16-s+)
  (mapcar #'amiga.ahi:sample-type-code '(:mono8 :mono16 :stereo8 :stereo16)))
(check "host-ahi-type-code-passthrough" amiga.raw.ahi:+ahist-s8-s+
  (amiga.ahi:sample-type-code amiga.raw.ahi:+ahist-s8-s+))
(check "host-ahi-unknown-type-errors" t
  (error-mentions "sample type" (lambda () (amiga.ahi:sample-type-code :quad))))
(check "host-ahi-unknown-type-code-errors" t
  (error-mentions "sample type" (lambda () (amiga.ahi:sample-frame-size 77))))
(check "host-ahi-sample-bytes-8" '(0 127 128 255)
  (coerce (amiga.ahi:sample-bytes #(0 127 -128 -1) :bits 8) 'list))
(check "host-ahi-sample-bytes-16-big-endian" '(0 1 255 255 128 0 127 255)
  (coerce (amiga.ahi:sample-bytes #(1 -1 -32768 32767) :bits 16) 'list))
(check "host-ahi-sample-bytes-16-from-packed-vector" '(0 1 255 255)
  (let ((v (make-array 2 :element-type '(signed-byte 16))))
    (setf (aref v 0) 1 (aref v 1) -1)
    (coerce (amiga.ahi:sample-bytes v :bits 16) 'list)))
(check "host-ahi-sample-bytes-bad-bits-errors" t
  (handler-case (progn (amiga.ahi:sample-bytes #(1) :bits 12) nil)
    (error () t)))
(check "host-ahi-make-sample-buffer-roundtrip" '(1 2 3 254)
  (let* ((p (amiga.ahi:make-sample-buffer
             (make-array 4 :element-type '(unsigned-byte 8)
                           :initial-contents '(1 2 3 254))))
         (v (make-array 4 :element-type '(unsigned-byte 8))))
    (ffi:peek-bytes p v)
    (ffi:free-foreign p)
    (coerce v 'list)))
(check "host-ahi-units" '(0 255)
  (list amiga.ahi:+ahi-default-unit+ amiga.ahi:+ahi-no-unit+))

;;; --- argument checks fire before any device is touched ---
(check "host-ahi-blank-handle-not-open" nil
  (amiga.ahi:ahi-open-p (amiga.ahi::%make-ahi)))
(check "host-ahi-length-not-frame-multiple-errors" t
  (error-mentions "multiple"
    (lambda () (amiga.ahi:play-sample (amiga.ahi::%make-ahi) (ffi:make-foreign-pointer 0)
                                      401 :type :mono16))))
(check "host-ahi-stereo16-length-checked" t
  (error-mentions "4-byte"
    (lambda () (amiga.ahi:queue-sample (amiga.ahi::%make-ahi) (ffi:make-foreign-pointer 0)
                                       6 :type :stereo16))))
(check "host-ahi-zero-length-errors" t
  (error-mentions "positive"
    (lambda () (amiga.ahi:play-sample (amiga.ahi::%make-ahi) (ffi:make-foreign-pointer 0) 0))))
(check "host-ahi-closed-handle-errors" t
  (error-mentions "closed" (lambda () (amiga.ahi:playing-p (amiga.ahi::%make-ahi)))))
(check "host-ahi-not-a-handle-errors" t
  (error-mentions "AHI handle" (lambda () (amiga.ahi:stop-sample 42))))
(check "host-ahi-close-not-a-handle-errors" t
  (error-mentions "AHI handle" (lambda () (amiga.ahi:close-ahi nil))))
(check "host-ahi-none-handle-has-no-playback" t
  (error-mentions ":NONE"
    (lambda ()
      (amiga.ahi:playing-p (amiga.ahi::%make-ahi :io-a (ffi:make-foreign-pointer 0)
                                                  :unit amiga.ahi:+ahi-no-unit+)))))

;;; --- the function interface refuses while nothing is open ---
(check "host-ahi-unarmed-audio-modes-errors" t
  (error-mentions "not open" (lambda () (amiga.ahi:audio-modes))))
(check "host-ahi-unarmed-mode-name-errors" t
  (error-mentions "not open" (lambda () (amiga.ahi:audio-mode-name 1))))
(check "host-ahi-unarmed-best-mode-errors" t
  (error-mentions "not open" (lambda () (amiga.ahi:best-audio-mode :stereo t))))
(check "host-ahi-unarmed-alloc-audio-errors" t
  (error-mentions "not open" (lambda () (amiga.ahi:alloc-audio :channels 2 :sounds 1))))
(check "host-ahi-unarmed-with-audio-ctrl-errors" t
  (error-mentions "not open" (lambda () (amiga.ahi:with-audio-ctrl (c) c))))
(check "host-ahi-unarmed-play-errors" t
  (error-mentions "not open" (lambda () (amiga.ahi:play nil 0 :sound 0 :frequency 8000))))

;;; --- the example loads (all of its code compiles) and bows out ---
(check "example-ahi-play-loads-and-bows-out" t
  (let ((output (with-output-to-string (*standard-output*)
                  (load "examples/amiga/audio/ahi-play.lisp"))))
    (and (search "not available" output) t)))

(format t "~%~D passed, ~D failed~%" *pass* *fail*)
(if (zerop *fail*)
    (format t "ALL AHI HOST CHECKS PASSED~%")
    (format t "SOME AHI HOST CHECKS FAILED~%"))
