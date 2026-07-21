;;; Lambda's Tale — host-side preview of the BLITTED first-person view.
;;;
;;; The Amiga front end is the only thing that composites a tile pack
;;; (%AMIGA-DRAW-FP); on the host the view renders as ASCII wireframe.
;;; That makes checking new art a round trip through FS-UAE.  This does
;;; the same composite in pure Lisp and hands back an IMAGE you can
;;; WRITE-ILBM and look at: same VIEW-BLIT-LIST, same back-to-front
;;; order, same pen-0-is-transparent rule, same variant selection.
;;;
;;; It is a preview, not a second renderer: if it and the Amiga ever
;;; disagree, the Amiga is right.  Everything it can get wrong (slot
;;; geometry, blit order, cropping) comes from view.lisp, which both
;;; share.
;;;
;;; Definitions only — load src/load.lisp, then this file:
;;;
;;;   (write-ilbm (preview-view (parse-map *art*) 0 1 :north
;;;                             :dir "worlds/closure/gfx/")
;;;               "preview.iff")

(in-package :tale)

(defun %preview-load-pack (dir planes)
  "Load the tile pack in DIR the way %LOAD-WALL-ASSETS does: a hash of
piece key -> vector of IMAGEs (index 0 the base file, then the -vN
variants, probed until one is missing), plus (:CEILING)/(:FLOOR) when
the pack ships backdrops.  Signals on a missing or mis-sized piece,
with the same message shape the Amiga loader uses."
  (let ((walls (make-hash-table :test #'equal)))
    (flet ((add (key file w h)
             (let ((img (read-ilbm file)))
               (unless (and (= (image-width img) w)
                            (= (image-height img) h))
                 (error "~A is ~Dx~D, its slot needs ~Dx~D (see ~
PRINT-TILE-MANIFEST)"
                        file (image-width img) (image-height img) w h))
               (setf (gethash key walls)
                     (concatenate 'vector (gethash key walls) (vector img))))))
      (dolist (piece (wall-piece-names))
        (destructuring-bind (x y w h) (wall-piece-rect planes piece)
          (declare (ignore x y))
          (let ((file (concatenate 'string dir (wall-piece-file piece))))
            (unless (probe-file file)
              (error "missing wall asset ~A" file))
            (add piece file w h)
            (loop for v from 1
                  for vfile = (concatenate 'string dir
                                           (wall-piece-variant-file piece v))
                  while (probe-file vfile)
                  do (add piece vfile w h)))))
      (destructuring-bind (ceiling floor) (backdrop-rects planes)
        (loop for key in '((:ceiling) (:floor))
              for name in '("ceiling.iff" "floor.iff")
              for rect in (list ceiling floor)
              do (let ((file (concatenate 'string dir name)))
                   (when (probe-file file)
                     (add key file (third rect) (fourth rect)))))))
    walls))

(defun %preview-blit (dst src dx dy sx w h &key transparent)
  "Copy SRC's [SX,SX+W) x [0,H) into DST at (DX,DY), clipped to DST.
TRANSPARENT skips pen 0 — the cookie-cut blit the wall pieces get."
  (dotimes (y h)
    (dotimes (x w)
      (let ((tx (+ dx x))
            (ty (+ dy y)))
        (when (and (< -1 tx (image-width dst))
                   (< -1 ty (image-height dst))
                   (< (+ sx x) (image-width src))
                   (< y (image-height src)))
          (let ((pen (pixel-ref src (+ sx x) y)))
            (unless (and transparent (zerop pen))
              (setf (pixel-ref dst tx ty) pen))))))))

(defun preview-view (map x y facing &key dir (depth +view-depth+)
                                         (profile *display-profile*))
  "The blitted first-person view from (X,Y) facing FACING in MAP, as an
IMAGE the size of PROFILE's viewport, composited from the tile pack in
DIR (default: the profile's own).  The pack's palette rides along, so
WRITE-ILBM on the result produces a file that looks like what the Amiga
shows."
  (with-display-profile (profile)
    (let* ((dir (or dir *gfx-dir*))
           (planes (view-planes *fp-view-width* *fp-view-height*))
           (walls (%preview-load-pack dir planes))
           (screen-depth (display-profile-screen-depth *display-profile*))
           (palette (let ((file (concatenate 'string dir "palette.iff")))
                      (if (probe-file file)
                          (image-palette (read-ilbm file))
                          (image-palette
                           (aref (gethash '(:front 0) walls) 0)))))
           (out (make-image *fp-view-width* *fp-view-height* screen-depth
                            :palette palette))
           (slices (compute-view map x y facing depth)))
      ;; the backdrop first: ceiling above the horizon, floor below
      (destructuring-bind (ceiling floor) (backdrop-rects planes)
        (loop for key in '((:ceiling) (:floor))
              for (bx by bw bh) in (list ceiling floor)
              do (let ((entry (gethash key walls)))
                   (declare (ignorable bw bh))
                   (when entry
                     (%preview-blit out (aref entry 0) bx by 0
                                    (image-width (aref entry 0))
                                    (image-height (aref entry 0)))))))
      ;; then the pieces, back to front, each cookie-cut over it
      (dolist (rec (view-blit-list slices planes) out)
        (destructuring-bind (piece style bx by bw bh sx) rec
          (let ((entry (gethash piece walls)))
            (unless entry
              (error "preview: pack ~A has no piece ~S" dir piece))
            (%preview-blit out (aref entry (mod style (length entry)))
                           bx by sx bw bh :transparent t)))))))

;;; ---------------------------------------------------------------------
;;; The fixture street: a straight run of four cells walled both sides,
;;; one side opened at depth 1 so a flank piece shows too.  Every piece
;;; kind the view can draw appears at more than one depth, which is
;;; what makes a single picture enough to judge a pack by.

(defparameter *preview-street*
"+-+-+-+
| | | |
+ + + +
| | | |
+ + + +
|   | |
+ + + +
| |@| |
+-+-+-+")

(defun preview-pack (dir file &key (profile *display-profile*))
  "Composite *PREVIEW-STREET* from the tile pack in DIR and write it to
FILE as an ILBM — the one-shot look at a pack without booting an Amiga.
Returns FILE."
  (with-display-profile (profile)
    (let ((map (parse-map *preview-street* :name "preview")))
      (write-ilbm (preview-view map 1 3 :north :dir dir) file))))
