;;; bouncing-lines.lisp — Bouncing color-cycling lines.
;;;
;;; Common Lisp port of ACE BASIC examples/gfx/bouncing_lines.b.
;;; Five lines bounce off the window edges; pen color is cycled per line.
;;;
;;; Run on Amiga (or in FS-UAE):
;;;   clamiga --load examples/gfx/bouncing-lines.lisp
;;; Click the close gadget to quit.

(require "amiga/intuition")
(require "amiga/graphics")

(defpackage "BOUNCING-LINES"
  (:use "CL" "AMIGA.INTUITION" "AMIGA.GFX"))

(in-package "BOUNCING-LINES")

(defconstant +num-lines+ 5)
(defconstant +frame-delay+ 0.02)

(defstruct line
  start-x start-y end-x end-y
  dx1 dy1 dx2 dy2
  color)

(defun nz-delta ()
  "Random delta in [-3,3], never 0."
  (let ((d (- (random 7) 3)))
    (if (zerop d) 1 d)))

(defun init-lines (w h)
  (loop for i from 0 below +num-lines+
        collect (make-line
                  :start-x (+ 50 (random (max 1 (- w 100))))
                  :start-y (+ 50 (random (max 1 (- h 100))))
                  :end-x   (+ 50 (random (max 1 (- w 100))))
                  :end-y   (+ 50 (random (max 1 (- h 100))))
                  :dx1 (nz-delta) :dy1 (nz-delta)
                  :dx2 (nz-delta) :dy2 (nz-delta)
                  :color (1+ i))))

(defun clamp (v lo hi) (max lo (min hi v)))

(defun bounce-delta (v d limit)
  (if (or (<= v 0) (>= v limit)) (- d) d))

(defun step-line (ln w h)
  (setf (line-start-x ln) (+ (line-start-x ln) (line-dx1 ln))
        (line-start-y ln) (+ (line-start-y ln) (line-dy1 ln))
        (line-end-x   ln) (+ (line-end-x   ln) (line-dx2 ln))
        (line-end-y   ln) (+ (line-end-y   ln) (line-dy2 ln)))
  (setf (line-dx1 ln) (bounce-delta (line-start-x ln) (line-dx1 ln) w)
        (line-dy1 ln) (bounce-delta (line-start-y ln) (line-dy1 ln) h)
        (line-dx2 ln) (bounce-delta (line-end-x   ln) (line-dx2 ln) w)
        (line-dy2 ln) (bounce-delta (line-end-y   ln) (line-dy2 ln) h))
  (setf (line-start-x ln) (clamp (line-start-x ln) 0 w)
        (line-start-y ln) (clamp (line-start-y ln) 0 h)
        (line-end-x   ln) (clamp (line-end-x   ln) 0 w)
        (line-end-y   ln) (clamp (line-end-y   ln) 0 h)))

(defun draw-frame (rp lines w h)
  (set-a-pen rp 0)
  (rect-fill rp 0 0 w h)
  (dolist (ln lines)
    (step-line ln w h)
    (set-a-pen rp (line-color ln))
    (draw-line rp
               (line-start-x ln) (line-start-y ln)
               (line-end-x ln)   (line-end-y ln))))

(defun close-requested-p (win)
  "Drain pending IDCMP messages; return T if a CLOSEWINDOW was seen."
  (let ((port (window-user-port win))
        (closed nil))
    (loop for msg = (get-msg port)
          while msg
          do (when (= (msg-class msg) +idcmp-closewindow+)
               (setf closed t))
             (reply-msg msg))
    closed))

(defun run (&key (width 600) (height 400))
  (with-window (win :title "Bouncing Color Lines"
                    :left 50 :top 50
                    :width width :height height
                    :idcmp +idcmp-closewindow+)
    (let* ((rp (window-rastport win))
           ;; Stay clear of the window borders / size gadget.
           (iw (max 1 (- (window-width win)  20)))
           (ih (max 1 (- (window-height win) 30)))
           (lines (init-lines iw ih))
           (frames 0)
           (t0 (get-internal-real-time)))
      (loop until (close-requested-p win) do
        (draw-frame rp lines iw ih)
        (incf frames)
        (let ((elapsed (/ (- (get-internal-real-time) t0)
                          internal-time-units-per-second)))
          (when (>= elapsed 1)
            (format t "~&FPS: ~,1F~%" (/ frames elapsed))
            (setf frames 0
                  t0 (get-internal-real-time))))
        (sleep +frame-delay+)))))

(run)
