;;; test-gfx-examples.lisp — Amiga-side tests of the examples/amiga/gfx/
;;; programs: the NDK 3.1 double-buffering port, the RKM hardware-sprite
;;; port and bouncing-lines.
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
