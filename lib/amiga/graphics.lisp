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
   ;; Display database / palette
   "BEST-MODE-ID" "SET-RGB4"
   ;; RastPort accessors
   "RASTPORT-FGPEN" "RASTPORT-BGPEN" "RASTPORT-CP-X" "RASTPORT-CP-Y"
   "RASTPORT-TX-HEIGHT" "RASTPORT-TX-BASELINE"
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
  (fgpen       :u8   25)   ; FgPen (foreground)
  (bgpen       :u8   26)   ; BgPen (background)
  (cp-x        :u16  36)   ; current pen X
  (cp-y        :u16  38)   ; current pen Y
  (tx-height   :u16  58)   ; rp_TxHeight: current font height in pixels
  (tx-baseline :u16  62))  ; rp_TxBaseline: baseline offset from glyph top

;;; ================================================================
;;; Drawing functions
;;; ================================================================

;; All graphics.library drawing primitives below have void return semantics —
;; :VOID T tells defcfun to skip the d0 result boxing in OP_AMIGA_CALL.
(amiga.ffi:defcfun move-to *gfx-base* -240
  (:a1 rastport :d0 x :d1 y) :void t)

(amiga.ffi:defcfun draw-to *gfx-base* -246
  (:a1 rastport :d0 x :d1 y) :void t)

(defun draw-line (rastport x1 y1 x2 y2)
  "Draw a line from (x1,y1) to (x2,y2)."
  (move-to rastport x1 y1)
  (draw-to rastport x2 y2))

(amiga.ffi:defcfun set-a-pen *gfx-base* -342
  (:a1 rastport :d0 pen) :void t)

(amiga.ffi:defcfun set-b-pen *gfx-base* -348
  (:a1 rastport :d0 pen) :void t)

(amiga.ffi:defcfun set-drmd *gfx-base* -354
  (:a1 rastport :d0 mode) :void t)

(amiga.ffi:defcfun rect-fill *gfx-base* -306
  (:a1 rastport :d0 x-min :d1 y-min :d2 x-max :d3 y-max) :void t)

(amiga.ffi:defcfun draw-ellipse *gfx-base* -180
  (:a1 rastport :d0 cx :d1 cy :d2 rx :d3 ry) :void t)

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
;;; Display database (BestModeIDA) and palette
;;; ================================================================

(defconstant +lvo-best-mode-id-a+ -1050)
(defconstant +lvo-set-rgb4+       -288)

;;; BIDTAG_* from graphics/modeid.h
(defconstant +bidtag-nominal-width+  #x80000004)
(defconstant +bidtag-nominal-height+ #x80000005)
(defconstant +bidtag-desired-width+  #x80000006)
(defconstant +bidtag-desired-height+ #x80000007)
(defconstant +bidtag-depth+          #x80000008)

(defun best-mode-id (&key (width 640) (height 256) (depth 2))
  "Ask the display database (graphics.library/BestModeIDA) for the mode
that best fits WIDTH x HEIGHT at DEPTH.  This is the RTG-safe way to
pick a screen mode — on Picasso96/CyberGraphX/MorphOS it returns a
suitable RTG mode, on a chipset Amiga a native one (e.g. PAL hires for
640x256).  Returns the mode ID, or NIL when the database has no match
\(caller falls back to opening the screen without SA_DisplayID)."
  (let* ((tags (amiga.ffi:make-tag-list
                (list +bidtag-nominal-width+  width
                      +bidtag-nominal-height+ height
                      +bidtag-desired-width+  width
                      +bidtag-desired-height+ height
                      +bidtag-depth+          depth)))
         (id (amiga:call-library *gfx-base* +lvo-best-mode-id-a+
                                 (list :a0 tags))))
    (ffi:free-foreign tags)
    ;; INVALID_ID is ~0; the call result may come back signed or unsigned.
    (if (or (eql id -1) (eql id #xFFFFFFFF))
        nil
        id)))

(amiga.ffi:defcfun set-rgb4 *gfx-base* -288
  (:a0 viewport :d0 index :d1 red :d2 green :d3 blue) :void t)

;;; ================================================================
;;; Provide module
;;; ================================================================

(provide "amiga/graphics")
