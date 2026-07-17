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
   ;; Fonts
   "OPEN-FONT" "CLOSE-FONT" "SET-FONT"
   ;; Display database / palette
   "BEST-MODE-ID" "SET-RGB4"
   ;; Bitmaps and blits (RTG-safe: all through OS calls)
   "ALLOC-BITMAP" "FREE-BITMAP" "GET-BITMAP-ATTR" "WITH-BITMAP"
   "INIT-RASTPORT" "WITH-BITMAP-RASTPORT"
   "WRITE-CHUNKY" "WRITE-PIXEL" "READ-PIXEL" "BLT-BITMAP-RASTPORT"
   "GFX-VERSION" "*WRITE-CHUNKY-FORCE-FALLBACK*"
   "+BMF-CLEAR+" "+BMF-DISPLAYABLE+" "+BMF-INTERLEAVED+"
   "+BMF-STANDARD+" "+BMF-MINPLANES+"
   "+BMA-HEIGHT+" "+BMA-DEPTH+" "+BMA-WIDTH+" "+BMA-FLAGS+"
   "+MINTERM-COPY+"
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
;;; Fonts (graphics.library OpenFont — ROM fonts like topaz 8/9;
;;; disk fonts need diskfont.library, not wrapped here)
;;; ================================================================

(defconstant +lvo-open-font+  -72)
(defconstant +lvo-close-font+ -78)
(defconstant +lvo-set-font+   -66)

(defun open-font (name ysize)
  "OpenFont via a TextAttr: the ROM font NAME (e.g. \"topaz.font\") at
YSIZE pixels.  Returns a TextFont pointer, or NIL when the exact
name/size isn't available.  Close with CLOSE-FONT after the last
rastport using it is done."
  (ffi:with-foreign-string (fname name)
    ;; struct TextAttr: ta_Name (STRPTR), ta_YSize (UWORD),
    ;; ta_Style (UBYTE), ta_Flags (UBYTE)
    (ffi:with-foreign-alloc (ta 8)
      (ffi:poke-u32 ta (ffi:foreign-pointer-address fname) 0)
      (ffi:poke-u16 ta ysize 4)
      (ffi:poke-u8 ta 0 6)
      (ffi:poke-u8 ta 0 7)
      (let ((font (amiga:call-library *gfx-base* +lvo-open-font+
                                      (list :a0 ta))))
        (if (zerop font)
            nil
            (ffi:make-foreign-pointer font))))))

(defun close-font (font)
  (amiga:call-library *gfx-base* +lvo-close-font+ (list :a1 font))
  t)

(amiga.ffi:defcfun set-font *gfx-base* -66
  (:a1 rastport :a0 font))

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
;;; Bitmaps and blits
;;;
;;; RTG-safe by construction: bitmaps come from AllocBitMap with a
;;; friend bitmap (so Picasso96/CyberGraphX/MorphOS allocate their own
;;; native format), pixels go in as chunky bytes through
;;; WriteChunkyPixels (V40+; WritePixel fallback on plain V39), and
;;; copies run through BltBitMapRastPort.  No planar layout, chip-ram
;;; or bytes-per-row assumptions anywhere.
;;; ================================================================

(defconstant +lvo-alloc-bitmap+          -918)
(defconstant +lvo-free-bitmap+           -924)
(defconstant +lvo-get-bitmap-attr+       -960)
(defconstant +lvo-blt-bitmap-rastport+   -606)

;;; graphics/gfx.h
(defconstant +bmf-clear+       #x0001)
(defconstant +bmf-displayable+ #x0002)
(defconstant +bmf-interleaved+ #x0004)
(defconstant +bmf-standard+    #x0008)
(defconstant +bmf-minplanes+   #x0010)
(defconstant +bma-height+ 0)
(defconstant +bma-depth+  4)
(defconstant +bma-width+  8)
(defconstant +bma-flags+  12)

(defconstant +minterm-copy+ #xC0)      ; ABC|ABNC: plain source copy

(defun gfx-version ()
  "graphics.library version (lib_Version); WriteChunkyPixels needs 40+."
  (ffi:peek-u16 *gfx-base* 20))

(defun alloc-bitmap (width height depth &key (flags +bmf-clear+) friend)
  "AllocBitMap: an offscreen bitmap, cleared by default.  Pass the
screen's or window's bitmap as FRIEND so RTG systems allocate it in
the display's native format.  Signals on failure."
  (let ((bm (amiga:call-library *gfx-base* +lvo-alloc-bitmap+
                                (list :d0 width :d1 height :d2 depth
                                      :d3 flags
                                      :a0 (or friend
                                              (ffi:make-foreign-pointer 0))))))
    (when (zerop bm)
      (error "GFX:ALLOC-BITMAP failed (~Dx~Dx~D)" width height depth))
    (ffi:make-foreign-pointer bm)))

(defun free-bitmap (bitmap)
  (amiga:call-library *gfx-base* +lvo-free-bitmap+ (list :a0 bitmap))
  t)

(defmacro with-bitmap ((var width height depth &rest keys) &body body)
  "Allocate a bitmap, bind to VAR, free on exit."
  `(let ((,var (alloc-bitmap ,width ,height ,depth ,@keys)))
     (unwind-protect
       (progn ,@body)
       (free-bitmap ,var))))

(amiga.ffi:defcfun get-bitmap-attr *gfx-base* -960
  (:a0 bitmap :d1 attribute))

(amiga.ffi:defcfun init-rastport *gfx-base* -198
  (:a1 rastport) :void t)

;;; struct RastPort is 100 bytes; rp_BitMap sits at offset 4.
(defconstant +rastport-size+ 100)
(defconstant +rp-bitmap-offset+ 4)

(defmacro with-bitmap-rastport ((var bitmap) &body body)
  "A scratch RastPort rendering into BITMAP: allocated, InitRastPort'd
and pointed at the bitmap; freed on exit.  This is how chunky pixels
get into an offscreen bitmap (WRITE-CHUNKY) and how drawing primitives
can target one."
  `(ffi:with-foreign-alloc (,var +rastport-size+)
     (init-rastport ,var)
     (ffi:poke-u32 ,var (ffi:foreign-pointer-address ,bitmap)
                   +rp-bitmap-offset+)
     ,@body))

(amiga.ffi:defcfun write-pixel *gfx-base* -324
  (:a1 rastport :d0 x :d1 y))

(amiga.ffi:defcfun read-pixel *gfx-base* -318
  (:a1 rastport :d0 x :d1 y))

(amiga.ffi:defcfun %write-chunky-pixels *gfx-base* -1056
  (:a0 rastport :d0 xstart :d1 ystart :d2 xstop :d3 ystop
   :a2 array :d4 bytes-per-row) :void t)

(defvar *write-chunky-force-fallback* nil
  "When bound to non-NIL, WRITE-CHUNKY always takes the V39 per-pixel
WritePixel path regardless of the actual graphics.library version.
Lets the fallback path be exercised on CI's V40 test hosts, where
\(>= (GFX-VERSION) 40) would otherwise always be true.")

(defun write-chunky (rastport x y width height pens)
  "Write the (unsigned-byte 8) vector PENS (row-major WIDTH x HEIGHT
pen indices) into RASTPORT at (X,Y).  Uses WriteChunkyPixels on
graphics.library V40+, falls back to per-pixel WritePixel on V39."
  (let ((n (* width height)))
    (when (< (length pens) n)
      (error "GFX:WRITE-CHUNKY: pen vector has ~D elements, needs ~Dx~D=~D"
             (length pens) width height n))
    (if (and (not *write-chunky-force-fallback*) (>= (gfx-version) 40))
        (ffi:with-foreign-alloc (buf n)
          (dotimes (i n)
            (ffi:poke-u8 buf (aref pens i) i))
          (%write-chunky-pixels rastport x y
                                (+ x width -1) (+ y height -1)
                                buf width))
        ;; V39: WritePixel draws with the foreground pen
        (let ((i 0))
          (dotimes (row height)
            (dotimes (col width)
              (set-a-pen rastport (aref pens i))
              (write-pixel rastport (+ x col) (+ y row))
              (incf i)))))
    t))

(defun blt-bitmap-rastport (src-bitmap src-x src-y dest-rastport
                            dest-x dest-y width height
                            &optional (minterm +minterm-copy+))
  "BltBitMapRastPort: copy a WIDTH x HEIGHT region of SRC-BITMAP into
DEST-RASTPORT at (DEST-X,DEST-Y)."
  (amiga:call-library *gfx-base* +lvo-blt-bitmap-rastport+
                      (list :a0 src-bitmap :d0 src-x :d1 src-y
                            :a1 dest-rastport :d2 dest-x :d3 dest-y
                            :d4 width :d5 height :d6 minterm))
  t)

;;; ================================================================
;;; Provide module
;;; ================================================================

(provide "amiga/graphics")
