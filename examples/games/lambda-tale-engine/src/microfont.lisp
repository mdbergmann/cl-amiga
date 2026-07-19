;;; Lambda's Tale — the microfont: a compact 5x7 pixel font.
;;;
;;; The smallest Amiga system font is topaz 8 (8x8 cells), which is
;;; wider and taller than the message log wants — so the engine brings
;;; its own: 5x7 glyphs on a 6x8 cell (one blank column and row of
;;; spacing), printable ASCII 32-126.  MICROFONT-LINE renders a text
;;; line into a chunky pen buffer (row-major (unsigned-byte 8) vector)
;;; that the Amiga front-end uploads with AMIGA.GFX:WRITE-CHUNKY —
;;; either straight into the window or into a cached offscreen bitmap
;;; (see %LOG-LINE-BITMAP in amiga-ui.lisp).  Pure pixel math, no OS
;;; calls, so the host test suite covers the glyphs and the layout.

(in-package :tale)

(defconstant +microfont-glyph-width+ 5)
(defconstant +microfont-glyph-height+ 7)
(defconstant +microfont-advance+ 6)      ; glyph + 1 column spacing
(defconstant +microfont-line-height+ 8)  ; glyph + 1 row spacing

;;; Each glyph is 7 rows, top to bottom, of 5 bits — #b10000 is the
;;; leftmost pixel.  Index = char-code - 32.
(defparameter *microfont-glyphs*
  (vector
   ;; space
   #(#b00000 #b00000 #b00000 #b00000 #b00000 #b00000 #b00000)
   ;; !
   #(#b00100 #b00100 #b00100 #b00100 #b00100 #b00000 #b00100)
   ;; "
   #(#b01010 #b01010 #b01010 #b00000 #b00000 #b00000 #b00000)
   ;; #
   #(#b01010 #b01010 #b11111 #b01010 #b11111 #b01010 #b01010)
   ;; $
   #(#b00100 #b01111 #b10100 #b01110 #b00101 #b11110 #b00100)
   ;; %
   #(#b11000 #b11001 #b00010 #b00100 #b01000 #b10011 #b00011)
   ;; &
   #(#b01100 #b10010 #b10100 #b01000 #b10101 #b10010 #b01101)
   ;; '
   #(#b00100 #b00100 #b01000 #b00000 #b00000 #b00000 #b00000)
   ;; (
   #(#b00010 #b00100 #b01000 #b01000 #b01000 #b00100 #b00010)
   ;; )
   #(#b01000 #b00100 #b00010 #b00010 #b00010 #b00100 #b01000)
   ;; *
   #(#b00000 #b00100 #b10101 #b01110 #b10101 #b00100 #b00000)
   ;; +
   #(#b00000 #b00100 #b00100 #b11111 #b00100 #b00100 #b00000)
   ;; ,
   #(#b00000 #b00000 #b00000 #b00000 #b01100 #b00100 #b01000)
   ;; -
   #(#b00000 #b00000 #b00000 #b11111 #b00000 #b00000 #b00000)
   ;; .
   #(#b00000 #b00000 #b00000 #b00000 #b00000 #b01100 #b01100)
   ;; /
   #(#b00000 #b00001 #b00010 #b00100 #b01000 #b10000 #b00000)
   ;; 0
   #(#b01110 #b10001 #b10011 #b10101 #b11001 #b10001 #b01110)
   ;; 1
   #(#b00100 #b01100 #b00100 #b00100 #b00100 #b00100 #b01110)
   ;; 2
   #(#b01110 #b10001 #b00001 #b00010 #b00100 #b01000 #b11111)
   ;; 3
   #(#b11111 #b00010 #b00100 #b00010 #b00001 #b10001 #b01110)
   ;; 4
   #(#b00010 #b00110 #b01010 #b10010 #b11111 #b00010 #b00010)
   ;; 5
   #(#b11111 #b10000 #b11110 #b00001 #b00001 #b10001 #b01110)
   ;; 6
   #(#b00110 #b01000 #b10000 #b11110 #b10001 #b10001 #b01110)
   ;; 7
   #(#b11111 #b00001 #b00010 #b00100 #b01000 #b01000 #b01000)
   ;; 8
   #(#b01110 #b10001 #b10001 #b01110 #b10001 #b10001 #b01110)
   ;; 9
   #(#b01110 #b10001 #b10001 #b01111 #b00001 #b00010 #b01100)
   ;; :
   #(#b00000 #b01100 #b01100 #b00000 #b01100 #b01100 #b00000)
   ;; ;
   #(#b00000 #b01100 #b01100 #b00000 #b01100 #b00100 #b01000)
   ;; <
   #(#b00010 #b00100 #b01000 #b10000 #b01000 #b00100 #b00010)
   ;; =
   #(#b00000 #b00000 #b11111 #b00000 #b11111 #b00000 #b00000)
   ;; >
   #(#b01000 #b00100 #b00010 #b00001 #b00010 #b00100 #b01000)
   ;; ?
   #(#b01110 #b10001 #b00001 #b00010 #b00100 #b00000 #b00100)
   ;; @
   #(#b01110 #b10001 #b00001 #b01101 #b10101 #b10101 #b01110)
   ;; A
   #(#b01110 #b10001 #b10001 #b10001 #b11111 #b10001 #b10001)
   ;; B
   #(#b11110 #b10001 #b10001 #b11110 #b10001 #b10001 #b11110)
   ;; C
   #(#b01110 #b10001 #b10000 #b10000 #b10000 #b10001 #b01110)
   ;; D
   #(#b11100 #b10010 #b10001 #b10001 #b10001 #b10010 #b11100)
   ;; E
   #(#b11111 #b10000 #b10000 #b11110 #b10000 #b10000 #b11111)
   ;; F
   #(#b11111 #b10000 #b10000 #b11110 #b10000 #b10000 #b10000)
   ;; G
   #(#b01110 #b10001 #b10000 #b10111 #b10001 #b10001 #b01111)
   ;; H
   #(#b10001 #b10001 #b10001 #b11111 #b10001 #b10001 #b10001)
   ;; I
   #(#b01110 #b00100 #b00100 #b00100 #b00100 #b00100 #b01110)
   ;; J
   #(#b00111 #b00010 #b00010 #b00010 #b00010 #b10010 #b01100)
   ;; K
   #(#b10001 #b10010 #b10100 #b11000 #b10100 #b10010 #b10001)
   ;; L
   #(#b10000 #b10000 #b10000 #b10000 #b10000 #b10000 #b11111)
   ;; M
   #(#b10001 #b11011 #b10101 #b10101 #b10001 #b10001 #b10001)
   ;; N
   #(#b10001 #b10001 #b11001 #b10101 #b10011 #b10001 #b10001)
   ;; O
   #(#b01110 #b10001 #b10001 #b10001 #b10001 #b10001 #b01110)
   ;; P
   #(#b11110 #b10001 #b10001 #b11110 #b10000 #b10000 #b10000)
   ;; Q
   #(#b01110 #b10001 #b10001 #b10001 #b10101 #b10010 #b01101)
   ;; R
   #(#b11110 #b10001 #b10001 #b11110 #b10100 #b10010 #b10001)
   ;; S
   #(#b01111 #b10000 #b10000 #b01110 #b00001 #b00001 #b11110)
   ;; T
   #(#b11111 #b00100 #b00100 #b00100 #b00100 #b00100 #b00100)
   ;; U
   #(#b10001 #b10001 #b10001 #b10001 #b10001 #b10001 #b01110)
   ;; V
   #(#b10001 #b10001 #b10001 #b10001 #b10001 #b01010 #b00100)
   ;; W
   #(#b10001 #b10001 #b10001 #b10101 #b10101 #b10101 #b01010)
   ;; X
   #(#b10001 #b10001 #b01010 #b00100 #b01010 #b10001 #b10001)
   ;; Y
   #(#b10001 #b10001 #b10001 #b01010 #b00100 #b00100 #b00100)
   ;; Z
   #(#b11111 #b00001 #b00010 #b00100 #b01000 #b10000 #b11111)
   ;; [
   #(#b01110 #b01000 #b01000 #b01000 #b01000 #b01000 #b01110)
   ;; backslash
   #(#b00000 #b10000 #b01000 #b00100 #b00010 #b00001 #b00000)
   ;; ]
   #(#b01110 #b00010 #b00010 #b00010 #b00010 #b00010 #b01110)
   ;; ^
   #(#b00100 #b01010 #b10001 #b00000 #b00000 #b00000 #b00000)
   ;; _
   #(#b00000 #b00000 #b00000 #b00000 #b00000 #b00000 #b11111)
   ;; `
   #(#b01000 #b00100 #b00010 #b00000 #b00000 #b00000 #b00000)
   ;; a
   #(#b00000 #b00000 #b01110 #b00001 #b01111 #b10001 #b01111)
   ;; b
   #(#b10000 #b10000 #b11110 #b10001 #b10001 #b10001 #b11110)
   ;; c
   #(#b00000 #b00000 #b01110 #b10000 #b10000 #b10001 #b01110)
   ;; d
   #(#b00001 #b00001 #b01111 #b10001 #b10001 #b10001 #b01111)
   ;; e
   #(#b00000 #b00000 #b01110 #b10001 #b11111 #b10000 #b01110)
   ;; f
   #(#b00110 #b01001 #b01000 #b11100 #b01000 #b01000 #b01000)
   ;; g
   #(#b00000 #b01111 #b10001 #b10001 #b01111 #b00001 #b01110)
   ;; h
   #(#b10000 #b10000 #b10110 #b11001 #b10001 #b10001 #b10001)
   ;; i
   #(#b00100 #b00000 #b01100 #b00100 #b00100 #b00100 #b01110)
   ;; j
   #(#b00010 #b00000 #b00110 #b00010 #b00010 #b10010 #b01100)
   ;; k
   #(#b10000 #b10000 #b10010 #b10100 #b11000 #b10100 #b10010)
   ;; l
   #(#b01100 #b00100 #b00100 #b00100 #b00100 #b00100 #b01110)
   ;; m
   #(#b00000 #b00000 #b11010 #b10101 #b10101 #b10101 #b10101)
   ;; n
   #(#b00000 #b00000 #b10110 #b11001 #b10001 #b10001 #b10001)
   ;; o
   #(#b00000 #b00000 #b01110 #b10001 #b10001 #b10001 #b01110)
   ;; p
   #(#b00000 #b00000 #b11110 #b10001 #b11110 #b10000 #b10000)
   ;; q
   #(#b00000 #b00000 #b01111 #b10001 #b01111 #b00001 #b00001)
   ;; r
   #(#b00000 #b00000 #b10110 #b11001 #b10000 #b10000 #b10000)
   ;; s
   #(#b00000 #b00000 #b01110 #b10000 #b01110 #b00001 #b11110)
   ;; t
   #(#b01000 #b01000 #b11100 #b01000 #b01000 #b01001 #b00110)
   ;; u
   #(#b00000 #b00000 #b10001 #b10001 #b10001 #b10011 #b01101)
   ;; v
   #(#b00000 #b00000 #b10001 #b10001 #b10001 #b01010 #b00100)
   ;; w
   #(#b00000 #b00000 #b10001 #b10001 #b10101 #b10101 #b01010)
   ;; x
   #(#b00000 #b00000 #b10001 #b01010 #b00100 #b01010 #b10001)
   ;; y
   #(#b00000 #b00000 #b10001 #b10001 #b01111 #b00001 #b01110)
   ;; z
   #(#b00000 #b00000 #b11111 #b00010 #b00100 #b01000 #b11111)
   ;; {
   #(#b00010 #b00100 #b00100 #b01000 #b00100 #b00100 #b00010)
   ;; |
   #(#b00100 #b00100 #b00100 #b00100 #b00100 #b00100 #b00100)
   ;; }
   #(#b01000 #b00100 #b00100 #b00010 #b00100 #b00100 #b01000)
   ;; ~
   #(#b00000 #b00000 #b01000 #b10101 #b00010 #b00000 #b00000)))

(defparameter *microfont-fallback*
  #(#b11111 #b10001 #b10001 #b10001 #b10001 #b10001 #b11111)
  "A hollow box, drawn for any character outside ASCII 32-126.")

(defun microfont-glyph (char)
  "The 7-row bit pattern for CHAR, or the fallback box."
  (let ((i (- (char-code char) 32)))
    (if (and (>= i 0) (< i (length *microfont-glyphs*)))
        (aref *microfont-glyphs* i)
        *microfont-fallback*)))

(defun microfont-text-width (text)
  "Pixel width of TEXT in the microfont (one 6px cell per character)."
  (* +microfont-advance+ (length text)))

(defun microfont-line (text fg bg &key width)
  "TEXT rendered as a chunky pen buffer: (VALUES PENS W H) where PENS
is a row-major (unsigned-byte 8) vector of W x H pen indices — glyph
pixels FG on a field of BG.  W is (MICROFONT-TEXT-WIDTH TEXT) or the
explicit WIDTH (longer text is cut, shorter padded with BG); H is
+MICROFONT-LINE-HEIGHT+.  The Amiga front-end feeds this straight to
AMIGA.GFX:WRITE-CHUNKY."
  (let* ((w (max 1 (or width (microfont-text-width text))))
         (h +microfont-line-height+)
         (pens (make-array (* w h) :element-type '(unsigned-byte 8)
                                   :initial-element bg)))
    (dotimes (i (length text))
      (let ((x0 (* i +microfont-advance+)))
        (when (>= x0 w) (return))
        (let ((rows (microfont-glyph (char text i))))
          (dotimes (row +microfont-glyph-height+)
            (let ((bits (aref rows row)))
              (dotimes (col +microfont-glyph-width+)
                (let ((x (+ x0 col)))
                  (when (and (< x w)
                             (logbitp (- +microfont-glyph-width+ 1 col)
                                      bits))
                    (setf (aref pens (+ (* row w) x)) fg)))))))))
    (values pens w h)))
