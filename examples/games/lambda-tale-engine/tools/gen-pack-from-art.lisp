;;; Lambda's Tale — tile packs generated from hand-drawn art.
;;;
;;; The twin of gen-walls.lisp: instead of *drawing* the 40 wall pieces
;;; procedurally, this derives them from one flat, front-on facade
;;; picture.  The perspective is pure geometry — a front or flank piece
;;; is a rectangle, a side piece is a trapezoid — so the whole pack
;;; falls out of resampling that single image into the slots
;;; WALL-PIECE-RECT names (see PRINT-TILE-MANIFEST for the contract).
;;;
;;; Definitions only — load src/load.lisp, then tools/gen-walls.lisp
;;; (this file reuses %IMG-MIRROR-X and the pen constants, and a pack
;;; usually wants its placeholder icons/portraits too), then this file,
;;; then call GENERATE-PACK-FROM-ART.
;;;
;;; What you provide is one IFF ILBM of the wall, front-on.  Any size
;;; (it is resampled) and any depth: an indexed ILBM is expanded
;;; through its CMAP, a 24/32-bit "deep" ILBM — what modern paint
;;; programs export — is read directly.  The natural size is the
;;; viewport (120x112 lores, 240x130 hires), which is exactly one wall
;;; cell at the nearest plane, so every piece is a downscale.
;;;
;;; Palette.  A pack is one shared Amiga CMAP and the loader only
;;; applies pens 4 and up, so the art cannot bring its own colors per
;;; picture: the pens are quantized ONCE over every source image
;;; (walls and pictures together) and everything is mapped into that
;;; one table.  The layout, which extends the hand-written packs in
;;; Closure's worlds/closure/gfx/:
;;;
;;;   0        transparent key in wall pieces — never assigned by the
;;;            quantizer, so art black lands on pen 4 instead
;;;   1 2 3    the fixed UI colors (white, grey, amber): the loader
;;;            leaves them alone, but art may map onto them
;;;   4        opaque black (frames, joints) — must stay black
;;;   5        sky / ceiling
;;;   6        ground / floor
;;;   7...     the quantized art colors (25 at lores, 9 at hires)
;;;
;;; Doors.  Every piece kind has a -door- twin.  By default both are
;;; drawn from the same source — a facade that already has a door in it
;;; makes every wall segment a door, which is what a dense city street
;;; looks like.  Pass :DOOR-SOURCE to give the -door- pieces their own
;;; art (and then SOURCE should be a doorless wall), which is what you
;;; want as soon as the player must *see* which walls can be entered.

(in-package :tale)

;;; ---------------------------------------------------------------------
;;; RGB source images.  Everything upstream of quantization works in
;;; truecolor: resampling indexed pens would average pen *numbers*.

(defstruct (rgb-image (:constructor %make-rgb-image))
  (width 0)
  (height 0)
  pixels)              ; (unsigned-byte 8), 3 per pixel, row-major RGB

(defun %make-rgb (width height)
  (unless (and (< 0 width) (< 0 height))
    (error "art: bad image geometry ~Dx~D" width height))
  (%make-rgb-image :width width :height height
                   :pixels (make-array (* 3 width height)
                                       :element-type '(unsigned-byte 8)
                                       :initial-element 0)))

(defun rgb-ref (img x y)
  "IMG's pixel at (X,Y) as (VALUES R G B)."
  (let ((i (* 3 (+ (* y (rgb-image-width img)) x)))
        (px (rgb-image-pixels img)))
    (values (aref px i) (aref px (+ i 1)) (aref px (+ i 2)))))

(defun (setf rgb-ref) (rgb img x y)
  (let ((i (* 3 (+ (* y (rgb-image-width img)) x)))
        (px (rgb-image-pixels img)))
    (setf (aref px i) (first rgb)
          (aref px (+ i 1)) (second rgb)
          (aref px (+ i 2)) (third rgb))
    rgb))

;;; ---------------------------------------------------------------------
;;; Deep (24/32-bit) ILBM reading.  READ-ILBM caps at 8 bitplanes — it
;;; is the Amiga loader's path and pens are all the machine can blit —
;;; so this host-side tool parses deep BODYs itself.  The chunk walk is
;;; the same shape as %READ-ILBM; the BODY is the same interleaved
;;; planar rows, only there are 24 of them and plane P carries bit
;;; (MOD P 8) of color component (FLOOR P 8).

(defun %read-deep-body (bytes pos len compression planes row-bytes h file)
  "The BODY's raw plane rows: (* ROW-BYTES PLANES H) bytes, decoded."
  (let* ((need (* row-bytes planes h))
         (raw (make-array need :element-type '(unsigned-byte 8)
                               :initial-element 0)))
    (ecase compression
      (0 (when (< len need)
           (error "ILBM ~A: BODY too short (~D bytes, needs ~D)"
                  file len need))
         (dotimes (i need) (setf (aref raw i) (aref bytes (+ pos i)))))
      (1 (%unpack-byte-run1 bytes pos (+ pos len) raw need file 0)))
    raw))

(defun %deep-planes-to-rgb (raw w h planes row-bytes)
  "Fold interleaved plane rows RAW into an RGB-IMAGE.  Planes 0-7 are
red bits (LSB first), 8-15 green, 16-23 blue; anything above 23 (a
32-bit file's alpha, or an mskHasMask plane) is ignored."
  (let* ((img (%make-rgb w h))
         (px (rgb-image-pixels img)))
    (dotimes (y h img)
      (dotimes (p planes)
        (let ((comp (floor p 8)))
          (when (< comp 3)
            (let ((base (* (+ (* y planes) p) row-bytes))
                  (bit (ash 1 (mod p 8))))
              (dotimes (x w)
                (when (logbitp (- 7 (mod x 8))
                               (aref raw (+ base (ash x -3))))
                  (let ((i (+ (* 3 (+ (* y w) x)) comp)))
                    (setf (aref px i) (logior (aref px i) bit))))))))))))

(defun read-deep-ilbm (file)
  "Read a 24- or 32-bit IFF ILBM FILE as an RGB-IMAGE."
  (let ((bytes (with-open-file (s file :element-type '(unsigned-byte 8))
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
          (w 0) (h 0) (depth 0) (masking 0) (compression 0)
          (img nil))
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
                    (setf w (%u16be bytes data)
                          h (%u16be bytes (+ data 2))
                          depth (aref bytes (+ data 8))
                          masking (aref bytes (+ data 9))
                          compression (aref bytes (+ data 10)))
                    (unless (member compression '(0 1))
                      (error "ILBM ~A: unsupported compression ~D ~
(only cmpNone/cmpByteRun1)" file compression))
                    (unless (member depth '(24 32))
                      (error "ILBM ~A: ~D bitplanes — READ-DEEP-ILBM ~
reads 24- or 32-bit files (use READ-ILBM for indexed ones)"
                             file depth)))
                   ((string= id "BODY")
                    (when (zerop w)
                      (error "ILBM ~A: BODY before BMHD" file))
                    (let* ((planes (+ depth (if (= masking 1) 1 0)))
                           (row-bytes (%row-bytes w)))
                      (setf img (%deep-planes-to-rgb
                                 (%read-deep-body bytes data len compression
                                                  planes row-bytes h file)
                                 w h planes row-bytes))))
                   (t nil))
                 (setf pos (+ data len (mod len 2)))))
      (unless img
        (error "ILBM ~A: no BODY chunk" file))
      img)))

(defun %ilbm-depth (file)
  "FILE's BMHD bitplane count, read without decoding the BODY."
  (with-open-file (s file :element-type '(unsigned-byte 8))
    (let ((head (make-array 32 :element-type '(unsigned-byte 8)
                               :initial-element 0)))
      (read-sequence head s)
      (unless (and (string= (%chunk-id head 0) "FORM")
                   (string= (%chunk-id head 8) "ILBM")
                   (string= (%chunk-id head 12) "BMHD"))
        (error "ILBM ~A: not an IFF FORM ILBM file with a leading BMHD"
               file))
      (aref head 28))))                 ; BMHD+8: nPlanes

(defun write-deep-ilbm (rgb file &key (compression 1))
  "Write RGB-IMAGE RGB to FILE as a 24-bit FORM ILBM — the inverse of
READ-DEEP-ILBM.  Deep ILBM is the lossless interchange format an Amiga
paint program understands, so this is how art that arrived in some
other format becomes a source you can keep editing."
  (unless (member compression '(0 1))
    (error "ILBM: compression must be 0 or 1, got ~S" compression))
  (let* ((w (rgb-image-width rgb))
         (h (rgb-image-height rgb))
         (row-bytes (%row-bytes w))
         (body '()))
    (dotimes (y h)
      (dotimes (p 24)
        (let ((row (make-array row-bytes :element-type '(unsigned-byte 8)
                                         :initial-element 0))
              (comp (floor p 8))
              (bit (ash 1 (mod p 8))))
          (dotimes (x w)
            (let ((v (nth comp (multiple-value-list (rgb-ref rgb x y)))))
              (unless (zerop (logand v bit))
                (setf (aref row (ash x -3))
                      (logior (aref row (ash x -3))
                              (ash 1 (- 7 (mod x 8))))))))
          (if (= compression 1)
              (dolist (b (%pack-byte-run1 row)) (push b body))
              (dotimes (i row-bytes) (push (aref row i) body))))))
    (setf body (nreverse body))
    (let ((bmhd (list (ash w -8) (logand w #xFF) (ash h -8) (logand h #xFF)
                      0 0 0 0 24 0 compression 0 0 0 10 11
                      (ash w -8) (logand w #xFF) (ash h -8) (logand h #xFF))))
      (with-open-file (s file :direction :output
                              :element-type '(unsigned-byte 8)
                              :if-exists :supersede)
        (flet ((id (str) (map nil (lambda (c) (write-byte (char-code c) s))
                             str))
               (u32 (v)
                 (write-byte (ldb (byte 8 24) v) s)
                 (write-byte (ldb (byte 8 16) v) s)
                 (write-byte (ldb (byte 8 8) v) s)
                 (write-byte (ldb (byte 8 0) v) s)))
          (id "FORM")
          (u32 (+ 4 8 (length bmhd) 8 (length body) (mod (length body) 2)))
          (id "ILBM")
          (id "BMHD") (u32 (length bmhd)) (dolist (b bmhd) (write-byte b s))
          (id "BODY") (u32 (length body)) (dolist (b body) (write-byte b s))
          (when (oddp (length body)) (write-byte 0 s))))))
  file)

;;; ---------------------------------------------------------------------
;;; PPM (P6): the bridge for art that was never an IFF.  Every paint
;;; program and converter emits it, it is self-describing, and it is
;;; raw RGB after a three-number header — so art from the modern side
;;; of the fence gets in with no decompressor:
;;;
;;;   ffmpeg -i house.png -pix_fmt rgb24 house.ppm

(defun %ppm-token (bytes pos)
  "The next PPM header token at or after POS: (VALUES INTEGER NEXT-POS).
Whitespace separates tokens and # runs to end of line."
  (let ((n (length bytes)))
    (loop while (< pos n)
          do (let ((b (aref bytes pos)))
               (cond ((member b '(32 9 10 13)) (incf pos))
                     ((= b 35)          ; '#': skip the comment line
                      (loop while (and (< pos n) (/= (aref bytes pos) 10))
                            do (incf pos)))
                     (t (return)))))
    (let ((v 0) (digits 0))
      (loop while (and (< pos n) (<= 48 (aref bytes pos) 57))
            do (setf v (+ (* v 10) (- (aref bytes pos) 48)))
               (incf digits) (incf pos))
      (when (zerop digits)
        (error "PPM: expected a number in the header"))
      (values v pos))))

(defun read-ppm (file)
  "Read a binary PPM (P6) FILE as an RGB-IMAGE."
  (let ((bytes (with-open-file (s file :element-type '(unsigned-byte 8))
                 (let ((v (make-array (file-length s)
                                      :element-type '(unsigned-byte 8))))
                   (read-sequence v s)
                   v))))
    (unless (and (> (length bytes) 2)
                 (= (aref bytes 0) 80) (= (aref bytes 1) 54))   ; "P6"
      (error "PPM ~A: not a binary PPM (P6) file" file))
    (multiple-value-bind (w pos) (%ppm-token bytes 2)
      (multiple-value-bind (h pos) (%ppm-token bytes pos)
        (multiple-value-bind (maxval pos) (%ppm-token bytes pos)
          (unless (= maxval 255)
            (error "PPM ~A: maxval ~D — only 8-bit (255) files are read"
                   file maxval))
          (let* ((start (1+ pos))      ; exactly one whitespace byte follows
                 (img (%make-rgb w h))
                 (px (rgb-image-pixels img))
                 (need (* 3 w h)))
            (when (< (- (length bytes) start) need)
              (error "PPM ~A: pixel data is short (~D bytes, needs ~D)"
                     file (- (length bytes) start) need))
            (dotimes (i need img)
              (setf (aref px i) (aref bytes (+ start i))))))))))

(defun read-art (file)
  "Read FILE as an RGB-IMAGE whatever it is: a binary PPM (the bridge
for art from off the Amiga — see READ-PPM), a deep (24/32-bit) ILBM
directly, or an indexed ILBM expanded through its CMAP.  This is the
front door for source art — draw in whatever your paint program
exports."
  (if (%ppm-file-p file)
      (read-ppm file)
      (let ((depth (%ilbm-depth file)))
        (if (> depth 8)
            (read-deep-ilbm file)
            (let* ((src (read-ilbm file))
                   (pal (image-palette src))
                   (out (%make-rgb (image-width src) (image-height src))))
              (dotimes (y (image-height src) out)
                (dotimes (x (image-width src))
                  (let ((rgb (aref pal (pixel-ref src x y))))
                    (setf (rgb-ref out x y) (or rgb '(0 0 0)))))))))))

(defun %ppm-file-p (file)
  "True when FILE starts with the P6 magic — sniffed, not guessed from
the name, so a .ppm that is really an ILBM still loads correctly."
  (with-open-file (s file :element-type '(unsigned-byte 8))
    (and (= (read-byte s nil 0) 80)
         (= (read-byte s nil 0) 54))))

;;; ---------------------------------------------------------------------
;;; Resampling.  Every piece is smaller than its source, so a box
;;; filter over the covered source rect is both correct and cheap —
;;; and it keeps the thin timber/mortar lines in the art from
;;; disappearing the way point sampling drops them.

(defun %rgb-box (img x0 y0 x1 y1)
  "Average of IMG over the source rect [X0,X1) x [Y0,Y1), clamped to
the image and never empty; returns (R G B)."
  (let* ((w (rgb-image-width img))
         (h (rgb-image-height img))
         (x0 (max 0 (min x0 (1- w))))
         (y0 (max 0 (min y0 (1- h))))
         (x1 (max (1+ x0) (min x1 w)))
         (y1 (max (1+ y0) (min y1 h)))
         (r 0) (g 0) (b 0)
         (n (* (- x1 x0) (- y1 y0))))
    (loop for y from y0 below y1
          do (loop for x from x0 below x1
                   do (multiple-value-bind (pr pg pb) (rgb-ref img x y)
                        (incf r pr) (incf g pg) (incf b pb))))
    (list (round r n) (round g n) (round b n))))

(defun %rgb-scale (img w h)
  "IMG box-filtered to W x H."
  (let ((sw (rgb-image-width img))
        (sh (rgb-image-height img))
        (out (%make-rgb w h)))
    (dotimes (y h out)
      (dotimes (x w)
        (setf (rgb-ref out x y)
              (%rgb-box img
                        (floor (* x sw) w) (floor (* y sh) h)
                        (ceiling (* (1+ x) sw) w)
                        (ceiling (* (1+ y) sh) h)))))))

(defun %rgb-crop (img x0 w)
  "Columns [X0, X0+W) of IMG (full height)."
  (let ((out (%make-rgb w (rgb-image-height img))))
    (dotimes (y (rgb-image-height img) out)
      (dotimes (x w)
        (setf (rgb-ref out x y)
              (multiple-value-bind (r g b) (rgb-ref img (+ x0 x) y)
                (list r g b)))))))

;;; ---------------------------------------------------------------------
;;; Quantization: median cut over the pooled source pixels.

;;; The screen is 12-bit.  A pack's CMAP is loaded with SET-RGB4 (four
;;; bits a channel, on RTG as much as on ECS — see %APPLY-PACK-PALETTE),
;;; so only 4096 colors are realizable and the engine takes the top
;;; nibble with (FLOOR v 17).  Quantizing in 24-bit space therefore
;;; spends pens on colors the machine cannot tell apart: the first pack
;;; built this way had three pens that all displayed as $000.  Every
;;; color the quantizer considers is snapped to the grid first, stored
;;; as N*17 so it floors back to exactly the nibble it stands for.

(defun snap-12-bit (rgb)
  "RGB rounded to the nearest color the Amiga can actually show."
  (mapcar (lambda (v) (* 17 (min 15 (round v 17)))) rgb))

(defun %color-counts (images)
  "Hash of packed RGB -> pixel count over every image in IMAGES, each
pixel snapped to the 12-bit screen grid so colors that would display
identically are counted as one."
  (let ((counts (make-hash-table :test #'eql)))
    (dolist (img images counts)
      (let ((px (rgb-image-pixels img)))
        (dotimes (i (* (rgb-image-width img) (rgb-image-height img)))
          (let* ((snapped (snap-12-bit (list (aref px (* 3 i))
                                             (aref px (+ (* 3 i) 1))
                                             (aref px (+ (* 3 i) 2)))))
                 (key (+ (ash (first snapped) 16)
                         (ash (second snapped) 8)
                         (third snapped))))
            (incf (gethash key counts 0))))))))

(defun %unpack-rgb (key) (list (ash key -16) (logand (ash key -8) #xFF)
                               (logand key #xFF)))

(defun %box-extent (box)
  "(VALUES CHANNEL RANGE) of BOX's widest color channel.  BOX is a list
of (R G B . COUNT)."
  (let ((best 0) (range -1))
    (dotimes (c 3 (values best range))
      (let ((lo 256) (hi -1))
        (dolist (entry box)
          (let ((v (nth c entry)))
            (setf lo (min lo v) hi (max hi v))))
        (when (> (- hi lo) range)
          (setf range (- hi lo) best c))))))

(defun %split-box (box)
  "BOX halved at the median of its widest channel, weighted by pixel
count; returns a list of one or two boxes — one only when BOX is a
single color and so cannot be split at all.

The cut is clamped so both halves are non-empty.  Without that, a box
whose heaviest color sorts last swallows every entry into the low half
and the split silently fails — which cost a real pack six of its
twenty-two art pens, left black at the end of the CMAP."
  (multiple-value-bind (channel range) (%box-extent box)
    (if (or (zerop range) (< (length box) 2))
        (list box)
        (let* ((sorted (sort (copy-list box) #'< :key (lambda (e)
                                                        (nth channel e))))
               (total (reduce #'+ sorted :key #'cdddr))
               (half (floor total 2))
               (n (length sorted))
               (acc 0)
               (cut 0))
          ;; the shortest prefix whose pixel count reaches half the box
          (loop for entry in sorted
                for i from 1
                do (incf acc (cdddr entry))
                   (setf cut i)
                until (>= acc half))
          (setf cut (max 1 (min cut (1- n))))
          (list (subseq sorted 0 cut) (subseq sorted cut))))))

(defun %box-average (box)
  "BOX's pixel-count-weighted mean color as (R G B), snapped to the
12-bit screen grid — the mean of realizable colors need not itself be
realizable."
  (let ((r 0) (g 0) (b 0) (n 0))
    (dolist (entry box)
      (let ((c (cdddr entry)))
        (incf r (* c (first entry)))
        (incf g (* c (second entry)))
        (incf b (* c (third entry)))
        (incf n c)))
    (snap-12-bit (list (round r n) (round g n) (round b n)))))

(defun median-cut (images n &key exclude)
  "The N most representative colors of IMAGES (a list of RGB-IMAGEs),
as a list of (R G B) on the 12-bit screen grid — the classic median-cut
boxes, split widest-range first and averaged by pixel count.

Splitting continues until N *distinct* colors are in hand, not merely N
boxes: two boxes whose averages snap to the same screen color would
otherwise burn two pens on one visible color.  EXCLUDE is a list of
colors already in the palette (the fixed pens), likewise not worth a
pen of their own.  Fewer than N distinct colors in the sources yields
fewer entries."
  (let ((box '()))
    (maphash (lambda (key count)
               (push (append (%unpack-rgb key) count) box))
             (%color-counts images))
    (when (null box)
      (error "art: the source images have no pixels"))
    (labels ((distinct (boxes)
               (let ((seen '()))
                 (dolist (b boxes (nreverse seen))
                   (let ((c (%box-average b)))
                     (unless (or (member c seen :test #'equal)
                                 (member c exclude :test #'equal))
                       (push c seen)))))))
      (let ((boxes (list box))
            (stuck '()))                ; boxes that cannot be split
        (loop while (< (length (distinct boxes)) n)
              do (let ((target nil) (best 0))
                   (dolist (b boxes)
                     (unless (member b stuck :test #'eq)
                       (multiple-value-bind (channel range) (%box-extent b)
                         (declare (ignore channel))
                         (when (and (> range best) (> (length b) 1))
                           (setf best range target b)))))
                   ;; nothing splittable left: the sources simply do
                   ;; not hold N distinct colors
                   (unless target (return))
                   (let ((split (%split-box target)))
                     ;; a box that will not divide is set aside, not a
                     ;; reason to stop splitting the others
                     (if (< (length split) 2)
                         (push target stuck)
                         (setf boxes (append (remove target boxes :test #'eq)
                                             split))))))
        (let ((colors (distinct boxes)))
          (if (> (length colors) n) (subseq colors 0 n) colors))))))

;;; ---------------------------------------------------------------------
;;; The pack palette and the RGB -> pen mapping.
;;;
;;; The pen contract itself — who owns which pen, and the colors of
;;; the ones the engine fixes — lives in src/palette.lisp, because the
;;; front end has to assert exactly what this tool bakes into a CMAP.
;;; What is here is only the generator's side: the defaults a pack gets
;;; for the two pens it owns outright, and the quantizer.

(defparameter *default-sky* '(0 0 136)
  "Pen 5 when GENERATE-PACK-FROM-ART is given no :SKY — the night blue
of Closure's street pack.")

(defparameter *default-ground* '(204 153 102)
  "Pen 6 when given no :GROUND — the tan street of Closure's pack.")

(defun art-pack-palette (colors depth &key (sky *default-sky*)
                                           (ground *default-ground*))
  "The pack CMAP: every engine-fixed pen at its contract color (see
src/palette.lisp), the pack's own sky and ground, and COLORS spread
over the art pens — a vector of (R G B) of length 2^DEPTH.  Surplus
pens repeat black, which costs nothing and keeps the CMAP truthful.

The engine ignores the fixed entries when it loads a pack (only
PACK-PENS are applied), but writing them keeps the CMAP an honest
picture of what the screen will show — which is what the .gpl beside
it, and any artist drawing against it, needs."
  (let ((pal (make-array (ash 1 depth) :initial-element '(0 0 0))))
    (loop for pen from 0 below (ash 1 depth)
          for rgb = (fixed-pen-color pen)
          when rgb do (setf (aref pal pen) rgb))
    (setf (aref pal +art-pen-sky+) sky
          (aref pal +art-pen-ground+) ground)
    (loop for rgb in colors
          for p in (art-pen-plan depth)
          do (setf (aref pal p) rgb))
    pal))

(defun art-fixed-colors (depth &key (sky *default-sky*)
                                    (ground *default-ground*))
  "The colors a pack already carries before any art is quantized: every
engine-fixed pen that exists at DEPTH — UI, pointer, figure core — plus
the pack's own sky and ground.  Passed to MEDIAN-CUT as :EXCLUDE so no
art pen is spent re-deriving a color the screen already shows."
  (append (list (snap-12-bit sky) (snap-12-bit ground))
          (loop for pen from 0 below (ash 1 depth)
                for rgb = (fixed-pen-color pen)
                when rgb collect rgb)))

;;; ---------------------------------------------------------------------
;;; Palette files.  A pack's palette.iff is what the *engine* reads; a
;;; GPL is what a paint program reads, so art can be drawn against the
;;; pack's colors instead of being quantized into them afterwards.
;;; Round-trip: build a pack, write its GPL, draw the next house with
;;; that palette loaded, then pass palette.iff back as :PALETTE-SOURCE
;;; and the quantizer becomes a lossless lookup.

(defun write-palette-gpl (palette file &key (name "Lambda's Tale pack"))
  "Write PALETTE (a CMAP vector of (R G B)) as a GIMP palette (.gpl) —
the format GIMP, Aseprite, Krita and Inkscape all import.  Each entry
is named with its pen number and its role, and every entry says
outright whether it is FIXED (engine-owned, the same color in every
zone) or the pack's to change — which is the whole artist-facing
contract, carried in the file they open."
  (with-open-file (s file :direction :output :if-exists :supersede)
    (format s "GIMP Palette~%Name: ~A~%Columns: 8~%#~%" name)
    (format s "# FIXED pens are the engine's and are the same color in ~
every zone:~%~
# 0 the transparent key in wall pieces, 1-3 the UI colors, 4 opaque~%~
# black, 17-19 the mouse pointer's sprite registers, 24-31 the shared~%~
# figure core.  Art that must survive a zone change — monsters,~%~
# portraits, effect icons — may use ONLY 1-4 and 24-31 (plus 0 for~%~
# transparency).~%~
# The rest are this pack's: 5 sky, 6 ground, and the art pens.  They~%~
# change when the player walks into another zone.~%~
# All colors are on the Amiga's 12-bit grid.~%")
    (dotimes (p (length palette) file)
      (let ((rgb (or (aref palette p) '(0 0 0))))
        (format s "~3D ~3D ~3D~C~D ~A~A~%"
                (first rgb) (second rgb) (third rgb) #\Tab p
                (fixed-pen-role p)
                (if (fixed-pen-color p) " [FIXED]" ""))))))

(defun palette-from-source (file depth &key (sky *default-sky*)
                                            (ground *default-ground*))
  "A pack CMAP whose art pens come from FILE's CMAP (any ILBM — a
pack's own palette.iff, or a swatch exported from a paint program)
instead of from the quantizer.  The contract pens are re-asserted, so a
hand-edited palette can never break transparency or the pointer."
  (let* ((src (image-palette (read-ilbm file)))
         (colors (loop for p in (art-pen-plan depth)
                       collect (snap-12-bit
                                (or (and (< p (length src)) (aref src p))
                                    '(0 0 0))))))
    (art-pack-palette colors depth :sky sky :ground ground)))

(defun %nearest-pen-mapper (palette usable)
  "A memoized closure (R G B) -> the pen of USABLE whose PALETTE color
is nearest.  USABLE must be non-empty; its first pen is the fallback,
so pens the caller must never emit simply stay off the list."
  (let ((cache (make-hash-table :test #'eql))
        (fallback (first usable)))
    (lambda (rgb)
      (let ((key (+ (ash (first rgb) 16) (ash (second rgb) 8) (third rgb))))
        (or (gethash key cache)
            (setf (gethash key cache)
                  (let ((best fallback) (dist most-positive-fixnum))
                    (dolist (p usable best)
                      (let ((c (aref palette p)))
                        (let ((d (+ (expt (- (first rgb) (first c)) 2)
                                    (expt (- (second rgb) (second c)) 2)
                                    (expt (- (third rgb) (third c)) 2))))
                          (when (< d dist)
                            (setf dist d best p))))))))))))

(defun %make-pen-mapper (palette &optional (depth nil))
  "A closure (R G B) -> nearest pen in PALETTE for WALL art, memoized.
Two pens are never returned: 0, the wall pieces' transparent key (so
art black lands on pen 4), and the pointer's 17-19, which the front end
overwrites after a pack loads.  DEPTH defaults to PALETTE's own size.

The figure core IS offered here.  A fixed pen is shared, not lost — a
wall may paint in the guaranteed figure colors, it just cannot re-color
them — so handing them to the quantizer costs the pack nothing and
buys it eight more colors to match against."
  (let* ((depth (or depth (round (log (length palette) 2))))
         (usable (cons 1 (remove 1 (append (loop for p from 1
                                                   below +art-pen-base+
                                                 collect p)
                                          (art-pen-plan depth)
                                          (figure-pens depth))))))
    (%nearest-pen-mapper palette usable)))

(defun %make-figure-pen-mapper (palette &optional (depth nil))
  "A closure (R G B) -> nearest pen for art that OUTLIVES a pack
switch — monsters, portraits, effect icons.  Restricted to
FIGURE-PALETTE-PENS: the opaque UI colors and the figure core, never
pen 0 (transparency is a source color's job, not the quantizer's),
never the pointer's, and never a pen whose color a pack may change.

This is the whole reason the core exists.  A monster quantized through
%MAKE-PEN-MAPPER would look right in the zone it was built against and
wrong in every other one."
  (let ((depth (or depth (round (log (length palette) 2)))))
    (%nearest-pen-mapper palette (figure-palette-pens depth))))

(defun quantize-image (rgb palette depth mapper)
  "RGB-IMAGE RGB as an IMAGE of DEPTH bitplanes with PALETTE, every
pixel mapped through MAPPER."
  (let* ((w (rgb-image-width rgb))
         (h (rgb-image-height rgb))
         (img (make-image w h depth :palette palette)))
    (dotimes (y h img)
      (dotimes (x w)
        (setf (pixel-ref img x y)
              (funcall mapper (multiple-value-bind (r g b) (rgb-ref rgb x y)
                                (list r g b))))))))

;;; ---------------------------------------------------------------------
;;; The pieces.  Front and flank slots are rectangles cut from the wall
;;; at the front slot's scale; a side slot is the trapezoid between two
;;; perspective planes, its ceiling/floor corners left at pen 0 so the
;;; backdrop shows through (the cookie-cut blit — the same contract
;;; %DRAW-SIDE-WALL meets).

(defun %art-front (src w h)
  "The wall scaled into a W x H front slot."
  (%rgb-scale src w h))

(defun %art-flank (src front-w w h side)
  "A flank slot: the neighbour cell's front wall at the SAME distance,
so it carries the front slot's scale (FRONT-W wide) and is cropped, not
squeezed, to the W the viewport leaves visible.  A left flank stands to
the left of the front slot, so what remains on screen is the wall's
right edge; a right flank shows its left edge."
  (let ((full (%rgb-scale src front-w h)))
    (if (>= w front-w)
        full
        (%rgb-crop full (if (eq side :l) (- front-w w) 0) w))))

(defun %art-side (src w h top-far bot-far side palette depth mapper)
  "A receding side wall as an IMAGE: the near edge (x = 0) spans the
full height, the far edge (x = W-1) spans TOP-FAR..BOT-FAR, with the
wall texture compressed into each column's visible span.  Corners stay
pen 0.  SIDE :R mirrors, as the brick pieces do.

The source column a screen column samples is linear in x — inside one
depth band (plane D to D+1) the error against a true 1/z walk is under
a pixel at every slot this engine has."
  (let ((img (make-image w h depth :palette palette))
        (sw (rgb-image-width src))
        (sh (rgb-image-height src)))
    (flet ((top-at (x) (round (* top-far x) (max 1 (1- w))))
           (bot-at (x) (- (1- h) (round (* (- (1- h) bot-far) x)
                                        (max 1 (1- w))))))
      (dotimes (x w)
        (let* ((top (top-at x))
               (bot (bot-at x))
               (span (1+ (- bot top)))
               (u0 (floor (* x sw) w))
               (u1 (ceiling (* (1+ x) sw) w)))
          (loop for y from top to bot
                do (setf (pixel-ref img x y)
                         (funcall mapper
                                  (%rgb-box src u0
                                            (floor (* (- y top) sh) span)
                                            u1
                                            (ceiling (* (1+ (- y top)) sh)
                                                     span))))))))
    (if (eq side :r) (%img-mirror-x img) img)))

(defun art-wall-piece (piece planes src palette depth mapper)
  "PIECE drawn at its slot size from the RGB source SRC — the
art-driven twin of DRAW-WALL-PIECE."
  (destructuring-bind (kind depth-index &optional side) piece
    (destructuring-bind (px0 py0 px1 py1) (aref planes depth-index)
      (declare (ignore px0 px1 py1))
      (destructuring-bind (qx0 qy0 qx1 qy1) (aref planes (1+ depth-index))
        (declare (ignore qx0 qx1))
        (destructuring-bind (x y w h) (wall-piece-rect planes piece)
          (declare (ignore x y))
          (ecase kind
            ((:front :front-door)
             (quantize-image (%art-front src w h) palette depth mapper))
            ((:flank :flank-door)
             (destructuring-bind (fx fy fw fh)
                 (wall-piece-rect planes (list :front depth-index))
               (declare (ignore fx fy fh))
               (quantize-image (%art-flank src fw w h side)
                               palette depth mapper)))
            ((:side :side-door)
             (%art-side src w h (- qy0 py0) (- qy1 py0) side
                        palette depth mapper))))))))

;;; ---------------------------------------------------------------------
;;; The generator

(defun %flat-image (w h pen palette depth)
  "A W x H image filled with PEN — the flat sky/ground backdrops."
  (let ((img (make-image w h depth :palette palette)))
    (dotimes (y h img)
      (dotimes (x w)
        (setf (pixel-ref img x y) pen)))))

(defun generate-pack-from-art (source &key out door-source variants pictures
                                           palette-source
                                           (profile *display-profile*)
                                           (sky *default-sky*)
                                           (ground *default-ground*))
  "Write a complete tile pack derived from the facade art in SOURCE.

SOURCE is a picture of the wall, front-on, at any size and in any
format READ-ART understands.  DOOR-SOURCE, when given, is the same wall
with a door and feeds the -door- pieces; without it both twins come
from SOURCE, so every wall segment shows whatever SOURCE has.

VARIANTS is a list of further wall pictures, each a whole extra look:
they are written as the -v1, -v2, ... files the engine deals out **per
building**, so a street becomes a row of different houses instead of
one facade repeated.  A `(location ... :style N)` op pins a building's
look, N counting SOURCE as 0 and VARIANTS from 1.

PICTURES is a list of (FILE . NAME) — facade/interior/portrait art
quantized into the same CMAP and written to NAME, which is what makes a
shop's takeover picture belong to the same world as the street (a
picture blits with the live screen palette, never its own; see
%AMIGA-DRAW-PICTURE).

Every source above is quantized together into the pack's ONE CMAP, so
the more looks a pack carries the fewer pens each gets.  PALETTE-SOURCE
takes that CMAP from a file instead of deriving it — pass a pack's own
palette.iff (or a swatch drawn in a paint program) and art already
drawn to those colors is mapped losslessly rather than requantized.
WRITE-PALETTE-GPL exports the palette for the paint program that closes
that loop.

OUT is the pack directory (default: PROFILE's own), PROFILE the
display profile whose viewport the slots come from.  SKY and GROUND are
the (R G B) of pens 5 and 6, i.e. of ceiling.iff and floor.iff.

Returns the number of files written."
  (with-display-profile (profile)
    (let* ((dir (or out *gfx-dir*))
           (depth (display-profile-screen-depth *display-profile*))
           (planes (view-planes *fp-view-width* *fp-view-height*))
           (looks (mapcar #'read-art (cons source variants)))
           (door-wall (when door-source (read-art door-source)))
           (picture-art (loop for (file . nil) in pictures
                              collect (read-art file)))
           ;; one quantization over every source: walls, variants and
           ;; pictures share the pack's single CMAP
           (palette
             (if palette-source
                 (palette-from-source palette-source depth
                                      :sky sky :ground ground)
                 (art-pack-palette
                  (median-cut (append looks
                                      (when door-wall (list door-wall))
                                      picture-art)
                              (length (art-pen-plan depth))
                              :exclude (art-fixed-colors depth :sky sky
                                                               :ground ground))
                  depth :sky sky :ground ground)))
           (mapper (%make-pen-mapper palette depth))
           (n 0))
      (ensure-directories-exist dir)
      (dolist (piece (wall-piece-names))
        (loop for look in looks
              for v from 0
              do (let ((src (if (and door-wall
                                     (member (first piece)
                                             '(:front-door :side-door
                                               :flank-door)))
                                door-wall
                                look))
                       (file (if (zerop v)
                                 (wall-piece-file piece)
                                 (wall-piece-variant-file piece v))))
                   (write-ilbm (art-wall-piece piece planes src palette
                                               depth mapper)
                               (concatenate 'string dir file))
                   (incf n))))
      (destructuring-bind (ceiling floor) (backdrop-rects planes)
        (write-ilbm (%flat-image (third ceiling) (fourth ceiling)
                                 +art-pen-sky+ palette depth)
                    (concatenate 'string dir "ceiling.iff"))
        (write-ilbm (%flat-image (third floor) (fourth floor)
                                 +art-pen-ground+ palette depth)
                    (concatenate 'string dir "floor.iff"))
        (incf n 2))
      ;; palette.iff: one pixel per pen, so the loader takes the pack's
      ;; CMAP whole rather than the first wall piece's.  palette.gpl is
      ;; the same colors for a paint program — draw the next house
      ;; against it and pass palette.iff back as :PALETTE-SOURCE.
      (let* ((pens (ash 1 depth))
             (img (make-image pens 1 depth :palette palette)))
        (dotimes (x pens) (setf (pixel-ref img x 0) x))
        (write-ilbm img (concatenate 'string dir "palette.iff"))
        (write-palette-gpl palette (concatenate 'string dir "palette.gpl"))
        (incf n 2))
      (loop for (nil . name) in pictures
            for art in picture-art
            do (write-ilbm (quantize-image art palette depth mapper)
                           (concatenate 'string dir name))
               (incf n))
      n)))

;;; ---------------------------------------------------------------------
;;; Figures: art that outlives a pack switch.
;;;
;;; A monster, a portrait or an effect icon is not part of any pack —
;;; it is cached by path (%CACHED-IMAGE) and keeps rendering after the
;;; player walks into a zone whose CMAP is different.  It therefore
;;; must be quantized into the pens no pack may change, and the check
;;; belongs HERE, on the host at build time, where it is free: a
;;; per-pixel pen audit on a 68020 at load time is not.

(defun figure-palette (depth)
  "The CMAP a figure is written with: every engine-fixed pen at its
contract color, pack pens black.  A figure ILBM carries this so it can
be viewed and re-imported standalone; the engine ignores a figure's
CMAP entirely and blits it against the live screen palette, which is
precisely why only fixed pens may appear in the pixels."
  (let ((pal (make-array (ash 1 depth) :initial-element '(0 0 0))))
    (loop for pen from 0 below (ash 1 depth)
          for rgb = (fixed-pen-color pen)
          when rgb do (setf (aref pal pen) rgb))
    pal))

(defun %check-figure-pens (img file depth)
  "Signal an error unless every pixel of IMG is a pen a figure may
use — the transparent key or one of FIGURE-PALETTE-PENS.  Names the
file, the offending pen, its role and where it first appears, because
the whole value of this check is that it says what to fix."
  (let ((allowed (cons +pen-transparent+ (figure-palette-pens depth))))
    (dotimes (y (image-height img) img)
      (dotimes (x (image-width img))
        (let ((pen (pixel-ref img x y)))
          (unless (member pen allowed)
            (error "~A: pen ~D (~A) at (~D,~D) is not a figure pen.~%~
Art that outlives a zone change may only use pen 0 (transparent), ~
1-4 and ~{~D~^ ~} — every other pen's color belongs to a tile pack ~
and changes when the player travels."
                   file pen (fixed-pen-role pen) x y
                   (figure-pens depth))))))))

(defun generate-figure (source out &key (profile *display-profile*)
                                        transparent)
  "Quantize the picture SOURCE into the shared figure palette and write
it to OUT as an ILBM — the build step for a monster, a hero portrait or
an effect icon.

TRANSPARENT, when given, is the source (R G B) that means \"nothing
here\": those pixels become pen 0, which the front end cookie-cuts away
(%PLANAR-PIECE-MASK).  Without it the figure is opaque and rectangular.

Every other pixel lands on a pen whose color the engine fixes, so the
result reads the same in every zone.  That is verified, not assumed:
the written image is audited pen by pen and a violation is an error
naming the pixel."
  (let* ((depth (display-profile-screen-depth profile))
         (palette (figure-palette depth))
         (mapper (%make-figure-pen-mapper palette depth))
         (art (read-art source))
         (key (and transparent (snap-12-bit transparent)))
         (w (rgb-image-width art))
         (h (rgb-image-height art))
         (img (make-image w h depth :palette palette)))
    (dotimes (y h)
      (dotimes (x w)
        (let ((rgb (multiple-value-bind (r g b) (rgb-ref art x y)
                     (list r g b))))
          (setf (pixel-ref img x y)
                (if (and key (equal (snap-12-bit rgb) key))
                    +pen-transparent+
                    (funcall mapper rgb))))))
    (%check-figure-pens img out depth)
    (write-ilbm img out)
    out))
