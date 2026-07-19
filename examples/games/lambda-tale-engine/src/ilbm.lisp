;;; Lambda's Tale — IFF ILBM image reader/writer (pure Common Lisp).
;;;
;;; READ-ILBM parses an IFF FORM ILBM file (EA IFF 85: "EA IFF 85
;;; Standard for Interchange Format Files" / the ILBM.doc registry
;;; entry) into an IMAGE: dimensions, depth, the CMAP palette and the
;;; BODY bitplanes decoded to one *chunky* pen index per pixel — the
;;; planar layout never leaves this file, which is what keeps the
;;; Amiga renderer RTG-safe (chunky pixels go to the OS through
;;; WriteChunkyPixels, never poked into planes).
;;;
;;; WRITE-ILBM is the inverse; the wall-art generator uses it to emit
;;; the game's data/gfx assets, and the test suite round-trips images
;;; through it.  Both ByteRun1 compression (cmpByteRun1 = 1) and
;;; uncompressed BODYs (cmpNone = 0) are supported in both directions.
;;;
;;; Only what the game needs is implemented: interleaved masks
;;; (masking = mskHasMask) are parsed and skipped, unknown chunks
;;; (GRAB, DPPS, CRNG, ...) are skipped, EHB/HAM display modes are not
;;; interpreted (the pen indices come through untouched).

(in-package :tale)

;;; ---------------------------------------------------------------------
;;; The image model

(defstruct (image (:constructor %make-image))
  (width 0)
  (height 0)
  (depth 0)              ; bitplane count; pens are 0 .. 2^depth - 1
  palette                ; vector of (r g b) lists, 0-255 components
  pixels)                ; (unsigned-byte 8) vector, row-major w*h pens

(defun make-image (width height depth &key palette)
  "A blank (pen 0) WIDTH x HEIGHT image with DEPTH bitplanes."
  (unless (and (< 0 width) (< 0 height) (<= 1 depth 8))
    (error "ILBM: bad image geometry ~Dx~Dx~D" width height depth))
  (%make-image :width width :height height :depth depth
               :palette (or palette
                            (make-array (ash 1 depth) :initial-element nil))
               :pixels (make-array (* width height)
                                   :element-type '(unsigned-byte 8)
                                   :initial-element 0)))

(defun pixel-ref (image x y)
  (aref (image-pixels image) (+ (* y (image-width image)) x)))

(defun (setf pixel-ref) (pen image x y)
  (setf (aref (image-pixels image) (+ (* y (image-width image)) x)) pen))

(defun image-transparent-p (image &optional (transparent 0))
  "True when IMAGE uses the TRANSPARENT pen anywhere — i.e. it needs a
cookie-cut mask rather than a plain opaque blit."
  (find transparent (image-pixels image)))

(defun mask-bytes (width height pixels &optional (transparent 0))
  "A cookie-cut mask for the Amiga's BltMaskBitMapRastPort: one
bitplane, row-major, MSB first (bit 7 = leftmost pixel of each byte),
rows padded to a 16-pixel word like ILBM planes, with a 1 bit wherever
PIXELS (row-major pen indices, WIDTH x HEIGHT) is not TRANSPARENT.
Returns (VALUES byte-vector bytes-per-row).  Pure — the Amiga front
end copies the bytes into a chip-RAM plane."
  (let* ((bpr (%row-bytes width))
         (out (make-array (* bpr height)
                          :element-type '(unsigned-byte 8)
                          :initial-element 0)))
    (dotimes (y height)
      (dotimes (x width)
        (unless (= (aref pixels (+ (* y width) x)) transparent)
          (let ((byte (+ (* y bpr) (ash x -3)))
                (bit (- 7 (logand x 7))))
            (setf (aref out byte) (logior (aref out byte) (ash 1 bit)))))))
    (values out bpr)))

;;; ---------------------------------------------------------------------
;;; Big-endian byte plumbing

(defun %u16be (bytes pos)
  (logior (ash (aref bytes pos) 8) (aref bytes (1+ pos))))

(defun %u32be (bytes pos)
  (logior (ash (aref bytes pos) 24)
          (ash (aref bytes (1+ pos)) 16)
          (ash (aref bytes (+ pos 2)) 8)
          (aref bytes (+ pos 3))))

(defun %chunk-id (bytes pos)
  (map 'string #'code-char (subseq bytes pos (+ pos 4))))

(defun %row-bytes (width)
  "ILBM plane rows are padded to a multiple of 16 pixels."
  (* 2 (ceiling width 16)))

;;; ---------------------------------------------------------------------
;;; ByteRun1 (cmpByteRun1) — ILBM.doc appendix C

(defun %unpack-byte-run1 (src pos end dst dst-len file)
  "Decode ByteRun1 data from SRC[POS..END) until DST-LEN bytes are
produced; returns the new source position.  Signals on truncated or
overlong data — a corrupt BODY must not silently misalign every
following row."
  (let ((out 0))
    (loop while (< out dst-len)
          do (when (>= pos end)
               (error "ILBM ~A: truncated ByteRun1 data in BODY" file))
             (let ((code (aref src pos)))
               (incf pos)
               (cond ((< code 128)          ; literal run of code+1 bytes
                      (let ((n (1+ code)))
                        (when (or (> (+ out n) dst-len) (> (+ pos n) end))
                          (error "ILBM ~A: ByteRun1 literal run overflows a row" file))
                        (dotimes (i n)
                          (setf (aref dst out) (aref src pos))
                          (incf out) (incf pos))))
                     ((> code 128)          ; repeat next byte 257-code times
                      (let ((n (- 257 code)))
                        (when (> (+ out n) dst-len)
                          (error "ILBM ~A: ByteRun1 repeat run overflows a row" file))
                        (when (>= pos end)
                          (error "ILBM ~A: truncated ByteRun1 data in BODY" file))
                        (let ((b (aref src pos)))
                          (incf pos)
                          (dotimes (i n)
                            (setf (aref dst out) b)
                            (incf out)))))
                     (t nil))))             ; 128: no-op
    pos))

(defun %pack-byte-run1 (row)
  "ByteRun1-encode the byte vector ROW; returns a list of bytes.
Repeat runs of 3+ are encoded, everything else goes into literal runs
\(the ILBM.doc packer policy); code 128 is never emitted."
  (let ((out '())
        (n (length row))
        (i 0))
    (flet ((run-length (start)
             (let ((b (aref row start))
                   (len 1))
               (loop while (and (< (+ start len) n)
                                (= (aref row (+ start len)) b)
                                (< len 128))
                     do (incf len))
               len)))
      (loop while (< i n)
            do (let ((run (run-length i)))
                 (if (>= run 3)
                     (progn
                       (push (- 257 run) out)
                       (push (aref row i) out)
                       (incf i run))
                     ;; gather a literal run up to the next 3+ repeat
                     (let ((start i))
                       (loop while (and (< i n)
                                        (< (- i start) 128)
                                        (< (run-length i) 3))
                             do (incf i))
                       (push (1- (- i start)) out)
                       (loop for j from start below i
                             do (push (aref row j) out)))))))
    (nreverse out)))

;;; ---------------------------------------------------------------------
;;; Reader

(defun %parse-body (image bytes pos len compression masking file)
  "Decode a BODY chunk at POS/LEN into IMAGE's chunky pixel vector."
  (let* ((w (image-width image))
         (h (image-height image))
         (depth (image-depth image))
         (row-bytes (%row-bytes w))
         (planes (+ depth (if (= masking 1) 1 0)))  ; mskHasMask interleaves
         (row (make-array row-bytes :element-type '(unsigned-byte 8)))
         (end (+ pos len))
         (pixels (image-pixels image)))
    (dotimes (y h)
      (dotimes (p planes)
        (ecase compression
          (0 (when (> (+ pos row-bytes) end)
               (error "ILBM ~A: BODY too short (row ~D plane ~D)" file y p))
             (replace row bytes :start2 pos :end2 (+ pos row-bytes))
             (incf pos row-bytes))
          (1 (setf pos (%unpack-byte-run1 bytes pos end row row-bytes file))))
        ;; fold the plane row into the chunky pixels (mask plane: skip)
        (when (< p depth)
          (let ((bit (ash 1 p))
                (base (* y w)))
            (dotimes (x w)
              (when (logbitp (- 7 (mod x 8)) (aref row (ash x -3)))
                (setf (aref pixels (+ base x))
                      (logior (aref pixels (+ base x)) bit))))))))
    image))

(defun read-ilbm (file)
  "Read an IFF ILBM FILE; returns an IMAGE (chunky pens + palette).
Signals a clear error on anything that isn't a well-formed ILBM.
Every load leaves a timed line in the debug log when it is enabled."
  (dlog-timed ("image ~A" file)
    (%read-ilbm file)))

(defun %read-ilbm (file)
  (let ((bytes
          (with-open-file (s file :element-type '(unsigned-byte 8))
            (let ((v (make-array (file-length s)
                                 :element-type '(unsigned-byte 8))))
              (read-sequence v s)
              v))))
    (when (or (< (length bytes) 12)
              (string/= (%chunk-id bytes 0) "FORM")
              (string/= (%chunk-id bytes 8) "ILBM"))
      (error "ILBM ~A: not an IFF FORM ILBM file" file))
    (let ((form-end (min (length bytes) (+ 8 (%u32be bytes 4))))
          (pos 12)
          (image nil)
          (compression 0)
          (masking 0)
          (body-seen nil))
      (loop while (<= (+ pos 8) form-end)
            do (let* ((id (%chunk-id bytes pos))
                      (len (%u32be bytes (+ pos 4)))
                      (data (+ pos 8)))
                 (when (> (+ data len) (length bytes))
                   (error "ILBM ~A: chunk ~A (~D bytes) runs past end of file"
                          file id len))
                 (cond
                   ((string= id "BMHD")
                    (when (< len 11)
                      (error "ILBM ~A: BMHD chunk too short" file))
                    (let ((w (%u16be bytes data))
                          (h (%u16be bytes (+ data 2)))
                          (depth (aref bytes (+ data 8))))
                      (setf masking (aref bytes (+ data 9))
                            compression (aref bytes (+ data 10)))
                      (unless (member compression '(0 1))
                        (error "ILBM ~A: unsupported compression ~D (only cmpNone/cmpByteRun1)"
                               file compression))
                      (setf image (make-image w h depth))))
                   ((string= id "CMAP")
                    (unless image
                      (error "ILBM ~A: CMAP before BMHD" file))
                    (let ((n (min (floor len 3)
                                  (length (image-palette image)))))
                      (dotimes (i n)
                        (setf (aref (image-palette image) i)
                              (list (aref bytes (+ data (* i 3)))
                                    (aref bytes (+ data (* i 3) 1))
                                    (aref bytes (+ data (* i 3) 2)))))))
                   ((string= id "BODY")
                    (unless image
                      (error "ILBM ~A: BODY before BMHD" file))
                    (%parse-body image bytes data len compression masking file)
                    (setf body-seen t))
                   (t nil))                 ; skip unknown chunks
                 (setf pos (+ data len (mod len 2)))))  ; chunks pad to even
      (unless image
        (error "ILBM ~A: no BMHD chunk" file))
      (unless body-seen
        (error "ILBM ~A: no BODY chunk" file))
      image)))

;;; ---------------------------------------------------------------------
;;; Writer

(defun %chunky-row-to-planes (image y row-bytes)
  "IMAGE row Y as a list of DEPTH plane-row byte vectors."
  (let ((w (image-width image))
        (pixels (image-pixels image)))
    (loop for p below (image-depth image)
          collect (let ((row (make-array row-bytes
                                         :element-type '(unsigned-byte 8)
                                         :initial-element 0))
                        (bit (ash 1 p))
                        (base (* y w)))
                    (dotimes (x w row)
                      (unless (zerop (logand (aref pixels (+ base x)) bit))
                        (setf (aref row (ash x -3))
                              (logior (aref row (ash x -3))
                                      (ash 1 (- 7 (mod x 8)))))))))))

(defun write-ilbm (image file &key (compression 1))
  "Write IMAGE to FILE as FORM ILBM (BMHD, CMAP when the palette has
entries, BODY).  COMPRESSION is 1 (ByteRun1, the default) or 0."
  (unless (member compression '(0 1))
    (error "ILBM: compression must be 0 or 1, got ~S" compression))
  (let* ((row-bytes (%row-bytes (image-width image)))
         (body '())                        ; reversed list of body bytes
         (palette (remove nil (coerce (image-palette image) 'list))))
    (dotimes (y (image-height image))
      (dolist (row (%chunky-row-to-planes image y row-bytes))
        (if (= compression 1)
            (dolist (b (%pack-byte-run1 row)) (push b body))
            (dotimes (i row-bytes) (push (aref row i) body)))))
    (setf body (nreverse body))
    (let ((chunks '()))                    ; list of (id . byte-list)
      (push (list "BMHD"
                  ;; w h x y nPlanes masking compression pad1
                  ;; transparentColor xAspect yAspect pageWidth pageHeight
                  (ash (image-width image) -8) (logand (image-width image) #xFF)
                  (ash (image-height image) -8) (logand (image-height image) #xFF)
                  0 0 0 0
                  (image-depth image) 0 compression 0
                  0 0 10 11
                  (ash (image-width image) -8) (logand (image-width image) #xFF)
                  (ash (image-height image) -8) (logand (image-height image) #xFF))
            chunks)
      (when palette
        (push (cons "CMAP" (loop for rgb in palette append rgb)) chunks))
      (push (cons "BODY" body) chunks)
      (setf chunks (nreverse chunks))
      (with-open-file (s file :direction :output
                              :element-type '(unsigned-byte 8)
                              :if-exists :supersede)
        (flet ((u32 (v)
                 (write-byte (ldb (byte 8 24) v) s)
                 (write-byte (ldb (byte 8 16) v) s)
                 (write-byte (ldb (byte 8 8) v) s)
                 (write-byte (ldb (byte 8 0) v) s))
               (id (str) (map nil (lambda (c) (write-byte (char-code c) s)) str)))
          (let ((form-len (+ 4                ; "ILBM"
                             (loop for (nil . data) in chunks
                                   sum (+ 8 (length data)
                                          (mod (length data) 2))))))
            (id "FORM") (u32 form-len) (id "ILBM")
            (dolist (chunk chunks)
              (id (car chunk))
              (u32 (length (cdr chunk)))
              (dolist (b (cdr chunk)) (write-byte b s))
              (when (oddp (length (cdr chunk)))
                (write-byte 0 s))))))))     ; chunks pad to even
  file)

;;; ---------------------------------------------------------------------
;;; Pointer sprites.  A mouse pointer is a 2-plane hardware sprite:
;;; at most 16 pixels wide, pens 0-3, where pen 1/2/3 shows screen
;;; color 17/18/19 and pen 0 is transparent.  The functions below turn
;;; an IMAGE — the built-in hand, or a campaign's pointer.iff — into
;;; the (LOW HIGH) plane words SET-POINTER wants; they are pure so the
;;; host suite exercises them without a display.

(defun pointer-sprite-rows (image)
  "IMAGE as a list of (LOW HIGH) 16-bit plane-word pairs, one pair per
row — the hardware-sprite layout.  Pen 1 sets the low plane (screen
color 17), pen 2 the high plane (18), pen 3 both (19).  Errors when the
image is wider than 16 pixels or uses a pen above 3."
  (let ((w (image-width image))
        (h (image-height image)))
    (unless (<= w 16)
      (error "pointer image is ~D pixels wide; a sprite holds 16" w))
    (loop for y below h
          collect (let ((low 0) (high 0))
                    (dotimes (x w (list low high))
                      (let ((pen (pixel-ref image x y))
                            (bit (ash 1 (- 15 x))))
                        (when (> pen 3)
                          (error "pointer image uses pen ~D; a sprite ~
holds pens 0-3" pen))
                        (when (logtest pen 1) (setf low (logior low bit)))
                        (when (logtest pen 2)
                          (setf high (logior high bit)))))))))

(defun pointer-hotspot (image)
  "The pointer's hot spot as (VALUES X Y): the leftmost non-transparent
pixel of the topmost row that has one — the finger tip of a pointing
hand, the arrow tip of an arrow.  (VALUES 0 0) for an empty image."
  (dotimes (y (image-height image) (values 0 0))
    (dotimes (x (image-width image))
      (when (plusp (pixel-ref image x y))
        (return-from pointer-hotspot (values x y))))))

;;; The built-in pointer: a pointing hand, finger tip up.  Rows of
;;; characters, `.` transparent, `1`-`3` the sprite pens.  A campaign
;;; overrides both art and colors by shipping a pointer.iff in its tile
;;; pack (see %ENSURE-STANDARD-POINTER in amiga-ui.lisp).

(defparameter *hand-pointer-art*
  '("....22.........."
    "...2112........."
    "...2112........."
    "...2112........."
    "...211222222...."
    "...2112112112..."
    "...21121121122.."
    ".22211211211212."
    "21122111111112.."
    "21121111111112.."
    ".211111111112..."
    "..21111111112..."
    "...222222222...."))

(defparameter *hand-pointer-colors*
  '((238 221 187) (17 17 17) (221 34 34))
  "Default sprite colors (screen colors 17-19), 0-255 components: the
hand's light skin, its near-black outline, a red accent.")

(defun hand-pointer-image ()
  "The built-in pointing-hand pointer as an IMAGE, its palette entries
1-3 filled with *HAND-POINTER-COLORS*."
  (let* ((h (length *hand-pointer-art*))
         (img (make-image 16 h 2)))
    (loop for row in *hand-pointer-art*
          for y from 0
          do (loop for ch across row
                   for x from 0
                   unless (char= ch #\.)
                     do (setf (pixel-ref img x y)
                              (- (char-code ch) (char-code #\0)))))
    (loop for rgb in *hand-pointer-colors*
          for i from 1
          do (setf (aref (image-palette img) i) (copy-list rgb)))
    img))
