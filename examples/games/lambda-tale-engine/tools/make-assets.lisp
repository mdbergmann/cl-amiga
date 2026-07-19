;;; Lambda's Tale — regenerate the wall-piece assets: one pack per
;;; display profile (data/gfx/ for :lores, data/gfx-hires/ for :hires).
;;; Self-locating (make assets works from any directory): the engine
;;; root is this file's parent directory.

(let ((tools (directory-namestring *load-truename*)))
  (load (concatenate 'string (subseq tools 0 (- (length tools) 6))
                     "src/load.lisp")))
(load (tale:engine-path "tools/gen-walls.lisp"))

(dolist (p tale::*display-profiles*)
  (format t "Generating ~A wall pieces into ~A ...~%"
          (string-downcase
           (princ-to-string (tale::display-profile-name p)))
          (tale::display-profile-gfx-dir p))
  (format t "~D pieces written.~%"
          (tale::generate-wall-assets :profile p)))

;; The fixture world's effects-band icon (w-compass :image) — checked
;; in like the packs; the test suite pixel-compares it against
;; DRAW-EFFECT-ICON so it cannot drift.
(tale:write-ilbm (tale::draw-effect-icon :compass)
                 (tale:engine-path "tests/world/fx-needle.iff"))
(format t "fixture icon written.~%")
(cl-user::quit 0)
