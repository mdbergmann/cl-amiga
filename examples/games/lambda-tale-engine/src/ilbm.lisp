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
    ;; Built a byte at a time — eight pixels are accumulated in a
    ;; register and stored once, rather than read-modify-writing OUT
    ;; per pixel.  Same reasoning as %PARSE-BODY: this runs over every
    ;; pixel of every masked piece in a pack.
    (dotimes (y height)
      (let ((src (* y width))
            (dst (* y bpr)))
        (dotimes (bx (ceiling width 8))
          (let ((x0 (ash bx 3))
                (acc 0))
            (dotimes (k (min 8 (- width x0)))
              (unless (= (aref pixels (+ src x0 k)) transparent)
                (setf acc (logior acc (ash 1 (- 7 k))))))
            (unless (zerop acc)
              (setf (aref out (+ dst bx)) acc))))))
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

(defun %unpack-byte-run1 (src pos end dst dst-len file &optional (dst-start 0))
  "Decode ByteRun1 data from SRC[POS..END) until DST-LEN bytes are
produced, writing them to DST from DST-START; returns the new source
position.  Signals on truncated or overlong data — a corrupt BODY must
not silently misalign every following row.

On clamiga the decode is EXT:UNPACK-BYTERUN1, a C builtin: the Lisp
loop below costs a VM round-trip per output byte, which made unpacking
one wall piece take seconds on a 14MHz 68020.  The Lisp loop remains
as the portable fallback (same contract, pinned by the test suite)."
  #+cl-amiga
  (handler-case
      (ext:unpack-byterun1 src pos end dst dst-len dst-start)
    (error (e)
      (error "ILBM ~A: ~A" file e)))
  #-cl-amiga
  (let ((out dst-start)
        (dst-len (+ dst-start dst-len)))
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

(defmacro %spread-plane-byte (byte bit scratch)
  "OR BIT into SCRATCH[0..7] wherever BYTE has a set bit, MSB first.
Unrolled: the eight bit positions are constants, so the fold costs no
shift/mask arithmetic per pixel.  BYTE and BIT are evaluated once."
  (let ((b (gensym "BYTE")) (m (gensym "BIT")))
    `(let ((,b ,byte) (,m ,bit))
       ,@(loop for k below 8
               collect `(when (logbitp ,(- 7 k) ,b)
                          (setf (aref ,scratch ,k)
                                (logior (aref ,scratch ,k) ,m)))))))

(defun %parse-body (image bytes pos len compression masking file)
  "Decode a BODY chunk at POS/LEN into IMAGE's chunky pixel vector.

The BODY is planar and interleaved per scanline: all PLANES rows for y
come before y+1's.  This exploits that — a scanline's plane rows are
unpacked into ROWS, then folded to chunky pens in a single pass over
groups of eight pixels, which

  - writes each pixel once instead of read-modify-writing it once per
    plane, and
  - skips a group outright when every plane's byte for it is zero.

Both matter at 14MHz: the fold is the whole cost of loading a tile
pack, and over half the plane bytes in the game's packs are zero (all
background), so the skip alone removes most of the work."
  (let* ((w (image-width image))
         (h (image-height image))
         (depth (image-depth image))
         (row-bytes (%row-bytes w))
         (planes (+ depth (if (= masking 1) 1 0)))  ; mskHasMask interleaves
         (rows (let ((v (make-array planes)))
                 (dotimes (p planes v)
                   (setf (svref v p)
                         (make-array row-bytes
                                     :element-type '(unsigned-byte 8))))))
         (scratch (make-array 8 :element-type '(unsigned-byte 8)))
         ;; plane bytes holding real pixels; the row padding beyond W
         ;; is decoded (it is in the stream) but never folded
         (fold-bytes (ceiling w 8))
         (end (+ pos len))
         (pixels (image-pixels image)))
    (dotimes (y h)
      ;; the scanline: PLANES packed rows, in plane order
      (dotimes (p planes)
        (let ((row (svref rows p)))
          (ecase compression
            (0 (when (> (+ pos row-bytes) end)
                 (error "ILBM ~A: BODY too short (row ~D plane ~D)" file y p))
               (replace row bytes :start2 pos :end2 (+ pos row-bytes))
               (incf pos row-bytes))
            (1 (setf pos (%unpack-byte-run1 bytes pos end row row-bytes
                                            file))))))
      ;; fold the scanline's planes into pens, eight pixels at a time
      ;; (mask plane: decoded above, never folded)
      (let ((base (* y w)))
        (dotimes (bx fold-bytes)
          (let ((any nil)
                (bit 1))
            (dotimes (p depth)
              (let ((byte (aref (svref rows p) bx)))
                (unless (zerop byte)
                  (unless any            ; first plane to touch this group
                    (fill scratch 0)
                    (setf any t))
                  (%spread-plane-byte byte bit scratch)))
              (setf bit (ash bit 1)))
            ;; all planes zero: the pens are already 0 from MAKE-IMAGE
            (when any
              (let* ((x0 (ash bx 3))
                     (n (min 8 (- w x0))))
                (dotimes (k n)
                  (setf (aref pixels (+ base x0 k))
                        (aref scratch k)))))))))
    image))

;;; ---------------------------------------------------------------------
;;; Planar decoding: the BODY's bitplane rows, unfolded.
;;;
;;; An ILBM plane row and an Amiga BitMap plane row have the same
;;; layout — MSB-first, padded to a 16-pixel word — so a piece can go
;;; to the display without ever becoming chunky: decode the BODY into
;;; the plane buffers, poke the buffers into a BitMap's planes,
;;; and let the blitter do the rest.  That skips the per-pixel fold in
;;; %PARSE-BODY entirely, which is the whole cost of loading a pack on
;;; a 68020.  READ-ILBM (chunky) remains the general reader: the host
;;; renderer, the pointer sprites and the asset generator all work in
;;; pens.  See %LOAD-WALL-ASSETS for the Amiga path.

(defstruct (planar-image (:constructor %make-planar-image))
  (width 0)
  (height 0)
  (depth 0)
  palette                ; vector of (r g b) lists, as in IMAGE
  planes                 ; simple-vector of DEPTH (unsigned-byte 8) vectors,
                         ; each ROW-BYTES * HEIGHT, row-major
  (row-bytes 0))

(defun planar-image-plane (img p)
  "Plane P's rows of IMG (ROW-BYTES per row, HEIGHT rows)."
  (svref (planar-image-planes img) p))

(defun %copy-rows (dst src count chunk dst-start dst-stride src-start src-stride)
  "Copy COUNT rows of CHUNK bytes each: row I goes from
SRC[src-start + I*src-stride ...) to DST[dst-start + I*dst-stride ...).
On clamiga this is EXT:COPY-ROWS, a C builtin — the gather step that
pulls one plane's rows out of an interleaved BODY in a single call
instead of one VM round-trip per row.  The REPLACE loop remains as the
portable fallback (same contract, pinned by the test suite)."
  #+cl-amiga
  (ext:copy-rows dst src count chunk dst-start dst-stride src-start src-stride)
  #-cl-amiga
  (dotimes (i count dst)
    (replace dst src
             :start1 (+ dst-start (* i dst-stride))
             :end1 (+ dst-start (* i dst-stride) chunk)
             :start2 (+ src-start (* i src-stride)))))

(defun %parse-body-planar (img bytes pos len compression masking file)
  "Decode a BODY chunk at POS/LEN into IMG's plane buffers, with no
per-row VM work: the BODY *is* the planes' rows interleaved per
scanline, so the whole chunk is decoded in one ByteRun1 call (or used
in place when uncompressed) and each plane's rows are then gathered
out with one strided copy per plane.  The interleaved mask plane
(masking = mskHasMask) is simply never gathered.  That is a handful of
builtin calls per image where decoding row by row cost h*planes VM
round-trips — most of a tile-pack load on a 14MHz 68020."
  (let* ((h (planar-image-height img))
         (depth (planar-image-depth img))
         (row-bytes (planar-image-row-bytes img))
         (planes (+ depth (if (= masking 1) 1 0)))
         (stride (* planes row-bytes))
         (total (* stride h))
         (end (+ pos len)))
    (multiple-value-bind (src start)
        (ecase compression
          (0 (when (> (+ pos total) end)
               (error "ILBM ~A: BODY too short (~D bytes, needs ~D for ~
~D rows of ~D planes x ~D bytes)" file len total h planes row-bytes))
             (values bytes pos))
          (1 (let ((buf (make-array total :element-type '(unsigned-byte 8))))
               (%unpack-byte-run1 bytes pos end buf total file)
               (values buf 0))))
      (dotimes (p depth img)
        (%copy-rows (planar-image-plane img p) src
                    h row-bytes
                    0 row-bytes
                    (+ start (* p row-bytes)) stride)))))

(defun planar-mask-bytes (img &optional (transparent 0))
  "A cookie-cut mask for IMG: one bitplane, the same row layout as its
planes, with a 1 bit wherever the pen is not TRANSPARENT.  For the
usual key (pen 0) that is just the bitwise OR of every plane — a pen
is non-zero exactly when some plane's bit is set — folded with
MAP-INTO #'LOGIOR, which runs as a C loop over the packed plane bytes.
Bits past W (the file's row padding) are cleared afterwards, so the
result is byte-for-byte what MASK-BYTES produces from chunky pens.
Returns (VALUES byte-vector bytes-per-row), or NIL when TRANSPARENT is
not pen 0 (no shortcut then; the caller falls back to MASK-BYTES)."
  (when (zerop transparent)
    (let* ((row-bytes (planar-image-row-bytes img))
           (w (planar-image-width img))
           (h (planar-image-height img))
           (out (make-array (* row-bytes h)
                            :element-type '(unsigned-byte 8)
                            :initial-element 0)))
      (dotimes (p (planar-image-depth img))
        (map-into out #'logior out (planar-image-plane img p)))
      ;; canonicalize the edge: padding bits beyond W are not pixels —
      ;; a foreign ILBM may carry junk there in its plane rows
      (let* ((full (floor w 8))
             (rem (mod w 8))
             (partial (logand #xFF (ash #xFF (- 8 rem)))))
        (when (or (plusp rem) (> row-bytes (ceiling w 8)))
          (dotimes (y h)
            (let ((base (* y row-bytes)))
              (when (plusp rem)
                (setf (aref out (+ base full))
                      (logand (aref out (+ base full)) partial)))
              (loop for i from (ceiling w 8) below row-bytes
                    do (setf (aref out (+ base i)) 0))))))
      (values out row-bytes))))

(defun %mask-opaque-p (mask row-bytes w h)
  "True when MASK (a canonical cookie-cut plane from PLANAR-MASK-BYTES
or MASK-BYTES: ROW-BYTES per row, no bits past W) has every real
pixel's bit set — a fully painted piece that needs no cookie-cut blit.
The full bytes are checked in one COUNT pass (a C loop over the packed
bytes); only the partial edge column, H bytes, is walked by hand."
  (let* ((full (floor w 8))
         (rem (mod w 8))
         (partial (logand #xFF (ash #xFF (- 8 rem)))))
    (and (= (count 255 mask) (* h full))
         (or (zerop rem)
             (dotimes (y h t)
               (unless (= (aref mask (+ (* y row-bytes) full)) partial)
                 (return nil)))))))

(defun planar-image-transparent-p (img &optional (transparent 0))
  "True when IMG uses the TRANSPARENT pen anywhere — i.e. it needs a
cookie-cut mask rather than a plain opaque blit.  For pen 0 that means
some pixel has no plane bit set at all.  Callers that also want the
mask itself should call PLANAR-MASK-BYTES once and test the result
with %MASK-OPAQUE-P instead of paying for two mask folds."
  (if (zerop transparent)
      (multiple-value-bind (mask row-bytes) (planar-mask-bytes img)
        (not (%mask-opaque-p mask row-bytes
                             (planar-image-width img)
                             (planar-image-height img))))
      t))                               ; non-zero key: no shortcut

(defun read-ilbm-planar (file)
  "Read an IFF ILBM FILE as a PLANAR-IMAGE — bitplane rows, undecoded.
Same validation as READ-ILBM."
  (dlog-timed ("image ~A (planar)" file)
    (%read-ilbm file t)))

(defun read-ilbm (file)
  "Read an IFF ILBM FILE; returns an IMAGE (chunky pens + palette).
Signals a clear error on anything that isn't a well-formed ILBM.
Every load leaves a timed line in the debug log when it is enabled."
  (dlog-timed ("image ~A" file)
    (%read-ilbm file)))

(defun %read-ilbm (file &optional planar)
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
                      (setf image
                            (if planar
                                (%make-planar-image
                                 :width w :height h :depth depth
                                 :palette (make-array (ash 1 depth)
                                                      :initial-element nil)
                                 :row-bytes (%row-bytes w)
                                 :planes
                                 (let ((v (make-array depth)))
                                   (dotimes (p depth v)
                                     (setf (svref v p)
                                           (make-array (* (%row-bytes w) h)
                                                       :element-type
                                                       '(unsigned-byte 8)
                                                       :initial-element 0)))))
                                (make-image w h depth)))))
                   ((string= id "CMAP")
                    (unless image
                      (error "ILBM ~A: CMAP before BMHD" file))
                    (let* ((pal (if planar
                                    (planar-image-palette image)
                                    (image-palette image)))
                           (n (min (floor len 3) (length pal))))
                      (dotimes (i n)
                        (setf (aref pal i)
                              (list (aref bytes (+ data (* i 3)))
                                    (aref bytes (+ data (* i 3) 1))
                                    (aref bytes (+ data (* i 3) 2)))))))
                   ((string= id "BODY")
                    (unless image
                      (error "ILBM ~A: BODY before BMHD" file))
                    ;; One BODY per FORM ILBM.  %PARSE-BODY decodes into
                    ;; freshly zeroed pens (it skips all-zero pixel
                    ;; groups rather than clearing them), so a second
                    ;; BODY would decode against dirty pixels; reject it
                    ;; instead of quietly producing a blended image.
                    (when body-seen
                      (error "ILBM ~A: a second BODY chunk (only one is ~
allowed in a FORM ILBM)" file))
                    (if planar
                        (%parse-body-planar image bytes data len
                                            compression masking file)
                        (%parse-body image bytes data len
                                     compression masking file))
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

;;; The built-in pointers: a relaxed open hand while nothing under the
;;; pointer reacts, a pointing hand — index finger out — over a click
;;; target, and four directional arrows over the first-person view's
;;; click-to-walk zones — turn left/right on the side quarters, walk
;;; forward on the middle, back-step on its bottom band (the hover
;;; feedback, see *HOTSPOTS* in amiga-ui.lisp).  Rows of characters,
;;; `.` transparent, `1`-`3` the sprite pens.  A campaign overrides
;;; art and colors by shipping a pointer.iff (the hand), a
;;; pointer-click.iff (the pointing finger) and/or
;;; pointer-forward.iff / pointer-back.iff / pointer-turn-left.iff /
;;; pointer-turn-right.iff (the arrows) in its tile pack (see
;;; %ENSURE-STANDARD-POINTER in amiga-ui.lisp).

(defparameter *hand-pointer-art*
  '("....22.........."
    ".22211222......."
    "2112112112......"
    "211211211222...."
    "2112112112112.22"
    "2112112112112212"
    "2111111111112112"
    "211111111111112."
    ".21111111111112."
    ".2111111111112.."
    "..211111111112.."
    "...2222222222...")
  "The neutral pointer: an open hand, fingers up — four fingers two
pixels of skin wide (single-pixel fingers dissolve into a stripe
pattern at sprite scale), the thumb out to the right.")

(defparameter *point-pointer-art*
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
    "...222222222....")
  "The click-target pointer: a pointing hand, index finger tip up.")

(defparameter *forward-pointer-art*
  '(".......22......."
    "......2112......"
    ".....211112....."
    "....21111112...."
    "...2111111112..."
    "..211111111112.."
    "..222211112222.."
    ".....211112....."
    ".....211112....."
    ".....211112....."
    ".....211112....."
    ".....222222.....")
  "The walk-forward cursor: an arrow pointing up.")

(defparameter *back-pointer-art*
  '(".....222222....."
    ".....211112....."
    ".....211112....."
    ".....211112....."
    ".....211112....."
    "..222211112222.."
    "..211111111112.."
    "...2111111112..."
    "....21111112...."
    ".....211112....."
    "......2112......"
    ".......22.......")
  "The back-step cursor: an arrow pointing down.")

(defparameter *turn-left-pointer-art*
  '(".....22........."
    "....212........."
    "...2112........."
    "..2111222222222."
    ".21111111111112."
    "211111111111112."
    "211111111111112."
    ".21111111111112."
    "..2111222222222."
    "...2112........."
    "....212........."
    ".....22.........")
  "The turn-left cursor: an arrow pointing left.")

(defparameter *turn-right-pointer-art*
  '(".........22....."
    ".........212...."
    ".........2112..."
    ".2222222221112.."
    ".21111111111112."
    ".211111111111112"
    ".211111111111112"
    ".21111111111112."
    ".2222222221112.."
    ".........2112..."
    ".........212...."
    ".........22.....")
  "The turn-right cursor: an arrow pointing right.")

(defparameter *busy-pointer-art*
  '(".22222222222222."
    ".2............2."
    "..2..........2.."
    "...2........2..."
    "....2......2...."
    ".....211112....."
    "......2112......"
    "......2112......"
    ".....2.11.2....."
    "....2..11..2...."
    "...2..1111..2..."
    "..2.11111111.2.."
    ".21111111111112."
    ".22222222222222.")
  "The busy cursor: an hourglass with the sand in its own pen — the
remainder in the top bulb, the falling stream, the heap below — so it
reads as glass and sand, not a solid silhouette.")

(defparameter *hand-pointer-colors*
  '((238 221 187) (17 17 17) (221 34 34))
  "Default sprite colors (screen colors 17-19), 0-255 components: the
hands' light skin, their near-black outline, a red accent.  Both
built-in pointers share them — the hardware sprite has one palette.")

(defun %pointer-art-image (art)
  "ART (rows of `.`/pen characters) as an IMAGE, its palette entries
1-3 filled with *HAND-POINTER-COLORS*."
  (let* ((h (length art))
         (img (make-image 16 h 2)))
    (loop for row in art
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

(defun hand-pointer-image ()
  "The built-in open-hand pointer (the neutral state) as an IMAGE."
  (%pointer-art-image *hand-pointer-art*))

(defun point-pointer-image ()
  "The built-in pointing-finger pointer (shown over a click target)
as an IMAGE."
  (%pointer-art-image *point-pointer-art*))

(defun forward-pointer-image ()
  "The built-in walk-forward pointer (an up arrow, shown over the
view's middle walk zone) as an IMAGE."
  (%pointer-art-image *forward-pointer-art*))

(defun back-pointer-image ()
  "The built-in back-step pointer (a down arrow, shown over the walk
zone's bottom band) as an IMAGE."
  (%pointer-art-image *back-pointer-art*))

(defun turn-left-pointer-image ()
  "The built-in turn-left pointer (a left arrow, shown over the view's
left quarter) as an IMAGE."
  (%pointer-art-image *turn-left-pointer-art*))

(defun turn-right-pointer-image ()
  "The built-in turn-right pointer (a right arrow, shown over the
view's right quarter) as an IMAGE."
  (%pointer-art-image *turn-right-pointer-art*))

(defun busy-pointer-image ()
  "The built-in busy pointer (an hourglass, shown during loads that
take real seconds) as an IMAGE."
  (%pointer-art-image *busy-pointer-art*))
