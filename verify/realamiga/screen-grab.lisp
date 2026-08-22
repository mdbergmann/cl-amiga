;;; screen-grab.lisp — FS-UAE harness helper: photograph new windows.
;;;
;;; Run detached (Run >log clamiga --load verify/realamiga/screen-grab.lisp)
;;; next to an unattended GUI run.  It watches the default public screen;
;;; whenever a window it has not seen before appears, it waits a moment
;;; for the window to render and saves the whole screen as a binary PPM
;;; (P6) under build/amiga/shots/ — which the host converts to PNG with
;;; ffmpeg.  It leaves when T:examples-done exists or after *MAX-SECONDS*.
;;;
;;; The pixels come from Picasso96API.library's p96ReadPixelArray, which
;;; converts any RTG screen (the FS-UAE Workbench is a 16-bit uaegfx
;;; screen) to 24-bit RGB; without Picasso96 it falls back to
;;; graphics.library ReadPixel + the screen's palette, which is right for
;;; palette screens only.

(require "amiga/ffi")
(require "amiga/raw/exec")
(require "amiga/raw/dos")
(require "amiga/raw/intuition")
(require "amiga/raw/graphics")

(defpackage "SCREEN-GRAB"
  (:use "CL")
  (:local-nicknames ("EXEC"  "AMIGA.RAW.EXEC")
                    ("DOS"   "AMIGA.RAW.DOS")
                    ("INTUI" "AMIGA.RAW.INTUITION")
                    ("GFX"   "AMIGA.RAW.GRAPHICS")))

(in-package "SCREEN-GRAB")

(defparameter *shot-dir* "build/amiga/shots/")
(defparameter *done-file* "T:examples-done")
(defparameter *max-seconds* 900)
(defparameter *settle-ticks* 100)       ; 2 s after a new window shows up

(defvar *p96-base* (amiga:open-library "Picasso96API.library" 0))

;; Our own log file: a detached (Run) process's stdout is not reliably
;; where the harness looks.
(defvar *log* (open "build/amiga/screen-grab.log" :direction :output
                                                  :if-exists :supersede
                                                  :if-does-not-exist :create))

(defun say (fmt &rest args)
  (let ((line (apply #'format nil fmt args)))
    (format t "~A~%" line)
    (finish-output)
    (format *log* "~A~%" line)
    (finish-output *log*)))

(defun file-exists-p (name)
  (ffi:with-foreign-string (n name)
    (let ((lock (dos:lock n dos:+shared-lock+)))
      (when (and (integerp lock) (/= lock 0))
        (dos:un-lock lock)
        t))))

(defun foreign-ptr (value)
  "Struct accessors hand back NIL, an integer or a foreign pointer."
  (cond ((null value) nil)
        ((integerp value) (if (zerop value) nil (ffi:make-foreign-pointer value)))
        ((ffi:null-pointer-p value) nil)
        (t value)))

;;; ---------------------------------------------------------------- pixels

(defun read-rgb-p96 (rp width height buffer)
  "p96ReadPixelArray(ri, 0, 0, rp, 0, 0, w, h) into BUFFER as RGBFB_R8G8B8."
  (let ((ri (ffi:alloc-foreign 12)))
    (unwind-protect
         (progn
           (ffi:poke-u32 ri (ffi:foreign-pointer-address buffer) 0) ; Memory
           (ffi:poke-i16 ri (* width 3) 4)                           ; BytesPerRow
           (ffi:poke-u32 ri 2 8)                                     ; RGBFB_R8G8B8
           (amiga:call-library *p96-base* -108
                               (list :a0 ri :d0 0 :d1 0 :a1 rp
                                     :d2 0 :d3 0 :d4 width :d5 height)
                               1))                                   ; void
      (ffi:free-foreign ri))))

(defun read-rgb-palette (scr rp width height buffer)
  "Palette screens: ReadPixel per pixel, colours from the ViewPort's ColorMap."
  (let* ((cm (foreign-ptr (gfx:view-port-color-map (intui:screen-view-port scr))))
         (table (ffi:alloc-foreign (* 256 12)))
         (palette (make-array 256)))
    (unwind-protect
         (progn
           (gfx:get-rgb32 cm 0 256 table)
           (dotimes (i 256)
             (setf (aref palette i)
                   (list (ffi:peek-u8 table (* i 12))
                         (ffi:peek-u8 table (+ (* i 12) 4))
                         (ffi:peek-u8 table (+ (* i 12) 8)))))
           (dotimes (y height)
             (dotimes (x width)
               (let* ((pen (logand (gfx:read-pixel rp x y) 255))
                      (rgb (aref palette pen))
                      (off (* 3 (+ x (* y width)))))
                 (ffi:poke-u8 buffer (first rgb) off)
                 (ffi:poke-u8 buffer (second rgb) (1+ off))
                 (ffi:poke-u8 buffer (third rgb) (+ off 2))))))
      (ffi:free-foreign table))))

(defun write-file-bytes (name header buffer length)
  (ffi:with-foreign-string (n name)
    (ffi:with-foreign-string (h header)
      (let ((fh (dos:open n dos:+mode-newfile+)))
        (unless (and (integerp fh) (/= fh 0))
          (error "cannot create ~A" name))
        (unwind-protect
             (progn (dos:write fh h (length header))
                    (dos:write fh buffer length))
          (dos:close fh))))))

(defun grab-screen (path)
  "Save the default public screen as a P6 PPM at PATH."
  (let ((scr (intui:lock-pub-screen nil)))
    (unless scr (error "no public screen"))
    (unwind-protect
         (let* ((width (intui:screen-width scr))
                (height (intui:screen-height scr))
                (rp (foreign-ptr (intui:screen-rastport scr)))
                (size (* width height 3))
                (buffer (ffi:alloc-foreign size)))
           (unwind-protect
                (progn
                  (if *p96-base*
                      (read-rgb-p96 rp width height buffer)
                      (read-rgb-palette scr rp width height buffer))
                  (write-file-bytes path (format nil "P6~%~D ~D~%255~%" width height)
                                    buffer size)
                  (say "; shot ~A (~Dx~D, ~:[ReadPixel~;p96~])" path width height *p96-base*))
             (ffi:free-foreign buffer)))
      (intui:unlock-pub-screen nil scr))))

;;; ---------------------------------------------------------------- windows

(defun window-titles ()
  "The (address . title) of every window on the default public screen."
  (let ((scr (intui:lock-pub-screen nil))
        (result '()))
    (when scr
      (unwind-protect
           (let ((lock (intui:lock-i-base 0)))
             (unwind-protect
                  (loop for win = (foreign-ptr (intui:screen-first-window scr))
                          then (foreign-ptr (intui:window-next-window win))
                        while win
                        do (let ((title (foreign-ptr (intui:window-title win))))
                             (push (cons (ffi:foreign-pointer-address win)
                                         (if title (ffi:foreign-to-string title 200) ""))
                                   result)))
               (intui:unlock-i-base lock)))
        (intui:unlock-pub-screen nil scr)))
    result))

(defun sanitize (title)
  (let ((s (string-downcase (substitute-if #\- (lambda (c) (not (alphanumericp c))) title))))
    (subseq s 0 (min 40 (length s)))))

(defun watch ()
  (let ((seen (window-titles))
        (shots 0)
        (deadline (+ (get-internal-real-time)
                     (* *max-seconds* internal-time-units-per-second))))
    (say "; screen-grab: watching, ~D windows at start, p96 ~A"
         (length seen) (if *p96-base* "yes" "no"))
    (loop
      (when (or (file-exists-p *done-file*)
                (> (get-internal-real-time) deadline))
        (return))
      (dos:delay 10)
      (let ((new (remove-if (lambda (w) (member w seen :test #'equal)) (window-titles))))
        (when new
          (dos:delay *settle-ticks*)
          (dolist (w new)
            (incf shots)
            (let ((path (format nil "~A~2,'0D-~A.ppm" *shot-dir* shots (sanitize (cdr w)))))
              (say "; new window ~S" (cdr w))
              (handler-case (grab-screen path)
                (error (e) (say "; grab failed: ~A" e))))
            (push w seen)))))
    (say "; screen-grab: done, ~D shots" shots)))

(watch)
