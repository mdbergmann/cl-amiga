;;; amiga/graphics.lisp — Graphics library abstractions for CL-Amiga
;;;
;;; Loaded via (require "amiga/graphics").
;;; Provides drawing primitives on RastPort.

(require "amiga/ffi")

(defpackage "AMIGA.GFX"
  (:use "CL" "FFI" "AMIGA.FFI")
  (:export
   ;; Library
   "*GFX-BASE*"
   ;; Drawing
   "MOVE-TO" "DRAW-TO" "DRAW-LINE"
   "SET-A-PEN" "SET-B-PEN" "SET-DRMD"
   "RECT-FILL" "DRAW-ELLIPSE"
   ;; Text
   "GFX-TEXT" "TEXT-LENGTH"
   ;; RastPort accessors
   "RASTPORT-FGPEN" "RASTPORT-BGPEN" "RASTPORT-CP-X" "RASTPORT-CP-Y"
   ;; Draw modes
   "+JAM1+" "+JAM2+" "+COMPLEMENT+" "+INVERSVID+"))

(in-package "AMIGA.GFX")

;;; ================================================================
;;; Graphics library base (opened on load)
;;; ================================================================

(defvar *gfx-base* (amiga:open-library "graphics.library" 39))
(unless *gfx-base*
  (error "Cannot open graphics.library v39"))

;;; ================================================================
;;; LVO offsets (from amiga/graphics_lib.fd)
;;; ================================================================

(defconstant +lvo-move+        -240)
(defconstant +lvo-draw+        -246)
(defconstant +lvo-text+         -60)
(defconstant +lvo-text-length+  -54)
(defconstant +lvo-set-a-pen+   -342)
(defconstant +lvo-set-b-pen+   -348)
(defconstant +lvo-set-drmd+    -354)
(defconstant +lvo-rect-fill+   -306)
(defconstant +lvo-draw-ellipse+ -180)

;;; ================================================================
;;; Draw modes
;;; ================================================================

(defconstant +jam1+       0)
(defconstant +jam2+       1)
(defconstant +complement+ 2)
(defconstant +inversvid+  4)

;;; ================================================================
;;; RastPort struct layout (partial)
;;; ================================================================

(ffi:defcstruct rastport
  (fgpen    :u8   25)   ; FgPen (foreground)
  (bgpen    :u8   26)   ; BgPen (background)
  (cp-x     :u16  36)   ; current pen X
  (cp-y     :u16  38))  ; current pen Y

;;; ================================================================
;;; Drawing functions
;;; ================================================================

(amiga.ffi:defcfun move-to *gfx-base* -240
  (:a1 rastport :d0 x :d1 y))

(amiga.ffi:defcfun draw-to *gfx-base* -246
  (:a1 rastport :d0 x :d1 y))

(defun draw-line (rastport x1 y1 x2 y2)
  "Draw a line from (x1,y1) to (x2,y2)."
  (move-to rastport x1 y1)
  (draw-to rastport x2 y2))

(amiga.ffi:defcfun set-a-pen *gfx-base* -342
  (:a1 rastport :d0 pen))

(amiga.ffi:defcfun set-b-pen *gfx-base* -348
  (:a1 rastport :d0 pen))

(amiga.ffi:defcfun set-drmd *gfx-base* -354
  (:a1 rastport :d0 mode))

(defun rect-fill (rastport x-min y-min x-max y-max)
  "Fill a rectangle."
  (amiga:call-library *gfx-base* +lvo-rect-fill+
                      (list :a1 rastport
                            :d0 x-min :d1 y-min
                            :d2 x-max :d3 y-max)))

(defun draw-ellipse (rastport cx cy rx ry)
  "Draw an ellipse centered at (cx,cy) with radii (rx,ry)."
  (amiga:call-library *gfx-base* +lvo-draw-ellipse+
                      (list :a1 rastport
                            :d0 cx :d1 cy
                            :d2 rx :d3 ry)))

;;; ================================================================
;;; Text functions
;;; ================================================================

(defun gfx-text (rastport string)
  "Render text at the current pen position."
  (ffi:with-foreign-string (s string)
    (amiga:call-library *gfx-base* +lvo-text+
                        (list :a1 rastport
                              :a0 s
                              :d0 (length string)))))

(defun text-length (rastport string)
  "Return pixel width of text string."
  (ffi:with-foreign-string (s string)
    (amiga:call-library *gfx-base* +lvo-text-length+
                        (list :a1 rastport
                              :a0 s
                              :d0 (length string)))))

;;; ================================================================
;;; Provide module
;;; ================================================================

(provide "amiga/graphics")
