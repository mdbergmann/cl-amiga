;;; Lambda's Tale — the party's map knowledge (automap data).
;;;
;;; Per cell we track one fixnum of bits:
;;;   bits 0..3  wall in direction N/E/S/W has been seen from this cell
;;;   bit  4     cell has been explored (party stood here)
;;; The knowledge lives with the save game, so the automap fills in
;;; permanently as the party explores.

(in-package :tale)

(defconstant +know-explored+ 16)
(defconstant +know-all-walls+ 15)

(defstruct (map-knowledge (:constructor %make-map-knowledge))
  (width 0)
  (height 0)
  bits)               ; (array (height width)) of fixnum

(defun make-map-knowledge (map)
  (%make-map-knowledge
   :width (dungeon-map-width map)
   :height (dungeon-map-height map)
   :bits (make-array (list (dungeon-map-height map) (dungeon-map-width map))
                     :initial-element 0)))

(defun know-wall (knowledge x y dir)
  "Record that the party has seen cell (X,Y)'s wall in direction DIR."
  (let ((bits (map-knowledge-bits knowledge)))
    (setf (aref bits y x)
          (logior (aref bits y x) (ash 1 (dir-index dir))))))

(defun know-cell (knowledge x y)
  "Record that the party stood in cell (X,Y): explored, all four walls seen."
  (let ((bits (map-knowledge-bits knowledge)))
    (setf (aref bits y x)
          (logior (aref bits y x) +know-explored+ +know-all-walls+))))

(defun cell-explored-p (knowledge x y)
  (/= 0 (logand (aref (map-knowledge-bits knowledge) y x) +know-explored+)))

(defun wall-known-p (knowledge x y dir)
  (/= 0 (logand (aref (map-knowledge-bits knowledge) y x)
                (ash 1 (dir-index dir)))))
