;;; test-gfx-examples.lisp — Amiga-side tests of the examples/amiga/gfx/
;;; programs: the NDK 3.1 double-buffering port, the RKM hardware-sprite
;;; port, bouncing-lines and the screen grabber.
;;;
;;; Loaded from run-tests.lisp via (load ...) inside #+amigaos, like
;;; test-iff.lisp: CHECK comes from run-tests.lisp.  Each example is
;;; loaded with AMIGA.INTUITION:*EVENT-LOOP-TIMEOUT* bound, so its
;;; auto-run returns after a second or two, then its RUN is called again
;;; for the result plist the checks look at.  The host mirror
;;; (tests/test_amiga_gfx_examples.sh) checks that everything loads and
;;; bows out where there is no Amiga.

(require "amiga/intuition")
(require "amiga/gadtools")
(require "amiga/raw/graphics")

(defun gfx-example-fn (package name)
  "The function NAME of the example's PACKAGE, or NIL when the load failed
\(so the checks below FAIL instead of the reader dying on a missing
package)."
  (let* ((pkg (find-package package))
         (sym (and pkg (find-symbol name pkg))))
    (and sym (fboundp sym) (symbol-function sym))))

;;; ---------------------------------------------------------------- gadtools

;; LAYOUT-MENUS wraps LayoutMenusA's BOOL result; a prior version reported
;; the polarity flipped ((zerop result), so success came back as NIL and
;; failure as T).  A real layout of a real menu strip must come back as
;; exactly T, the value the flipped version could never produce.
(check "gadtools-layout-menus-succeeds" t
  (amiga.intuition:with-pub-screen (scr)
    (amiga.gadtools:with-visual-info (vi scr)
      (multiple-value-bind (nm-array strings)
          (amiga.gadtools:make-new-menu-array
           `((,amiga.gadtools:+nm-title+ "Test")
             (,amiga.gadtools:+nm-item+ "Item")))
        (let ((menu (amiga.gadtools:create-menus nm-array)))
          (unwind-protect
               (eq t (amiga.gadtools:layout-menus menu vi))
            (amiga.gadtools:free-menus menu)
            (ffi:free-foreign nm-array)
            (dolist (s strings) (ffi:free-foreign s))))))))

;;; ---------------------------------------------------------------- doublebuffer

;; Loading runs the demo once for two seconds: HIRES canvas screen,
;; attached control screen, two ScreenBuffers swapping at the frame rate.
(check "gfx-doublebuffer-loads-and-runs" t
  (let ((amiga.intuition:*event-loop-timeout* 2))
    (load "examples/amiga/gfx/doublebuffer.lisp")
    t))

(check "gfx-doublebuffer-available" t
  (funcall (gfx-example-fn "DOUBLEBUFFER" "AVAILABLE-P")))

;; A second run, one second: frames were swapped in (each one needs the
;; dbi_SafeMessage of its predecessor to have come back on our port),
;; nothing ran away, and the result is what RUN documents.
(check "gfx-doublebuffer-swaps-frames" t
  (let ((r (funcall (gfx-example-fn "DOUBLEBUFFER" "RUN") :seconds 1)))
    (and (listp r)
         (integerp (getf r :frames)) (plusp (getf r :frames))
         (integerp (getf r :held-off)) (>= (getf r :held-off) 0)
         (floatp (getf r :seconds)) (>= (getf r :seconds) 1.0)
         (< (getf r :seconds) 10.0)
         t)))

;; The frame rate is bounded by the vertical blank: no more swaps than
;; there were frames (60 Hz NTSC / 50 Hz PAL, with slack for timing).
(check "gfx-doublebuffer-frame-rate-bounded" t
  (let ((r (funcall (gfx-example-fn "DOUBLEBUFFER" "RUN") :seconds 1)))
    (< (getf r :frames) (* 70 (ceiling (getf r :seconds))))))

;; Everything was torn down: no screen titled like the canvas is left in
;; Intuition's screen list.
(defun gfx-screen-titles ()
  "The titles of every open screen, read under LockIBase."
  (let ((titles '())
        (lock (amiga.raw.intuition:lock-i-base 0)))
    (unwind-protect
         (loop for scr = (amiga.raw.intuition:intuition-base-first-screen
                          amiga.raw.intuition:*intuition-base*)
                 then (amiga.raw.intuition:screen-next-screen scr)
               while scr
               do (let ((title (amiga.raw.intuition:screen-title scr)))
                    (push (if title (ffi:foreign-to-string title 200) "")
                          titles)))
      (amiga.raw.intuition:unlock-i-base lock))
    titles))

(check "gfx-doublebuffer-closed-its-screens" nil
  (and (member "Intuition double-buffering example" (gfx-screen-titles)
               :test #'string=)
       t))

;;; ---------------------------------------------------------------- sprite

;; Loading runs the walk for one second on its own LORES screen.
(check "gfx-sprite-loads-and-runs" t
  (let ((amiga.intuition:*event-loop-timeout* 1))
    (load "examples/amiga/gfx/sprite.lisp")
    t))

;; A short walk: sprite 2 was granted (0..7), its colour registers are
;; the pair's (16 + 4 * pair, so 20 for sprites 2/3), and MoveSprite
;; kept the SimpleSprite's x/y current: from (30,0), ten steps of +1.
;; MorphOS has no chipset sprites: GetSprite answers -1 there and the
;; example reports it instead of failing.
(check "gfx-sprite-walks" t
  (let* ((r (funcall (gfx-example-fn "SIMPLE-SPRITE" "RUN")
                     :passes 1 :frames 10 :seconds nil))
         (num (getf r :sprite)))
    (if (and (member :morphos *features*) (= num -1))
        t
        (and (<= 0 num 7)
             (= (getf r :color-reg) (+ 16 (ash (logand num 6) 1)))
             (= (getf r :frames) 10)
             (= (getf r :x) 40)
             (= (getf r :y) 10)))))

;; Two passes reverse direction: 10 steps out, 10 back — home again.
(check "gfx-sprite-returns-home" '(20 30 0)
  (let ((r (funcall (gfx-example-fn "SIMPLE-SPRITE" "RUN")
                    :passes 2 :frames 10 :seconds nil)))
    (if (and (member :morphos *features*) (= (getf r :sprite) -1))
        '(20 30 0)
        (list (getf r :frames) (getf r :x) (getf r :y)))))

;; The sprite was freed: claiming the same one again succeeds.
(check "gfx-sprite-freed" t
  (let ((r (funcall (gfx-example-fn "SIMPLE-SPRITE" "RUN")
                    :passes 1 :frames 1 :seconds nil)))
    (or (member :morphos *features*)
        (and (<= 0 (getf r :sprite) 7) t))))

;;; ---------------------------------------------------------------- bouncing-lines

(check "gfx-bouncing-lines-loads-and-runs" t
  (let ((amiga.intuition:*event-loop-timeout* 1))
    (load "examples/amiga/gfx/bouncing-lines.lisp")
    t))

;;; ---------------------------------------------------------------- screenshot

;; Loading shoots the front screen at once (the unattended knob skips
;; the countdown) into RAM:screenshot.ppm.
(check "gfx-screenshot-loads-and-runs" t
  (let ((amiga.intuition:*event-loop-timeout* 1))
    (load "examples/amiga/gfx/screenshot.lisp")
    (prog1 (and (probe-file "RAM:screenshot.ppm") t)
      (delete-file "RAM:screenshot.ppm"))))

(check "gfx-screenshot-available" t
  (funcall (gfx-example-fn "SCREENSHOT" "AVAILABLE-P")))

(defun screenshot-fn (name)
  (gfx-example-fn "SCREENSHOT" name))

(defun ppm-file-header (path)
  "The three header lines of the binary PPM at PATH, as one string."
  (with-open-file (s path :element-type '(unsigned-byte 8))
    (let ((chars '()) (newlines 0))
      (loop for b = (read-byte s nil nil)
            while (and b (< newlines 3))
            do (push (code-char b) chars)
               (when (= b 10) (incf newlines)))
      (coerce (nreverse chars) 'string))))

;; The Workbench to a file under build/amiga/ (host-mounted under FS-UAE:
;; `ffmpeg -i build/amiga/suite-screenshot.ppm x.png` shows what the
;; suite saw).  Header and size match the screen; the RTG reader is
;; whichever library the system has.  A system whose Workbench is RTG
;; without cybergraphics or Picasso96 cannot be photographed and says so.
(defparameter *suite-shot* "build/amiga/suite-screenshot.ppm")

(check "gfx-screenshot-workbench-file-matches-screen" t
  (let ((r (handler-case (funcall (screenshot-fn "GRAB")
                                  :screen :workbench :file *suite-shot*)
             (error (e) e))))
    (cond
      ((and (typep r 'error) (null (funcall (screenshot-fn "RTG-READER"))))
       (format t "  (no RTG reader on this system: ~A)~%" r)
       t)
      ((typep r 'error)
       (format t "  grab failed: ~A~%" r)
       nil)
      (t
       (amiga.intuition:with-pub-screen (scr)
         (let* ((w (amiga.raw.intuition:screen-width scr))
                (h (amiga.raw.intuition:screen-height scr))
                (header (format nil "P6~%~D ~D~%255~%" w h)))
           (and (= (getf r :width) w)
                (= (getf r :height) h)
                (member (getf r :method) '(:bitplanes :cgx :p96))
                (= (getf r :bytes) (+ (length header) (* w h 3)))
                (string= (ppm-file-header *suite-shot*) header)
                (with-open-file (s *suite-shot* :element-type '(unsigned-byte 8))
                  (= (file-length s) (getf r :bytes)))
                t)))))))

;; A 2-plane LORES custom screen of ours -- pen 1 white on pen 0 black,
;; a white rectangle drawn on it: SCREEN-RGB reads the colours back at
;; the right places, from the bitplanes on a chipset Amiga, through the
;; RTG library where such a screen is emulated on a graphics card
;; (MorphOS -- whose BitMap is then deeper than the 2 planes asked for).
(defun screenshot-colour (buffer width x y)
  "The pixel classified: :WHITE, :BLACK, or its RGB list.  16-bit RTG
screens round colours, hence the slack."
  (multiple-value-bind (r g b) (funcall (screenshot-fn "PIXEL") buffer width x y)
    (cond ((and (>= r 240) (>= g 240) (>= b 240)) :white)
          ((and (<= r 15) (<= g 15) (<= b 15)) :black)
          (t (list r g b)))))

(check "gfx-screenshot-custom-screen-pixels" '(:white :black 320 200 t t)
  (amiga.intuition:with-screen (scr :width 320 :height 200 :depth 2
                                    :title "Screenshot test"
                                    :mode-id amiga.raw.graphics:+lores-key+)
    (let ((vp (amiga.intuition:screen-viewport scr))
          (rp (let ((v (amiga.raw.intuition:screen-rastport scr)))
                (if (integerp v) (ffi:make-foreign-pointer v) v))))
      (amiga.raw.graphics:set-rgb4 vp 0 0 0 0)
      (amiga.raw.graphics:set-rgb4 vp 1 15 15 15)
      (amiga.raw.graphics:set-a-pen rp 1)
      (amiga.raw.graphics:rect-fill rp 20 40 139 139)
      (multiple-value-bind (buffer width height depth method)
          (funcall (screenshot-fn "SCREEN-RGB") scr)
        (unwind-protect
             (list (screenshot-colour buffer width 80 90)
                   (screenshot-colour buffer width 250 170)
                   width height
                   (>= depth 2)
                   (and (member method '(:bitplanes :cgx :p96)) t))
          (ffi:free-foreign buffer))))))

;; The same screen handed to GRAB by address, as a program that opened
;; its own screen would: the file is the screen's size.
(check "gfx-screenshot-grab-by-address" '(320 200 t)
  (amiga.intuition:with-screen (scr :width 320 :height 200 :depth 2
                                    :title "Screenshot test"
                                    :mode-id amiga.raw.graphics:+lores-key+)
    (let ((r (funcall (screenshot-fn "GRAB")
                      :screen (ffi:foreign-pointer-address scr)
                      :file "T:screenshot-test.ppm")))
      (prog1 (list (getf r :width) (getf r :height)
                   (with-open-file (s "T:screenshot-test.ppm"
                                      :element-type '(unsigned-byte 8))
                     (= (file-length s) (getf r :bytes))))
        (delete-file "T:screenshot-test.ppm")))))
