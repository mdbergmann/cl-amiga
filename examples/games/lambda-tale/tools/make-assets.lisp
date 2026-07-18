;;; Lambda's Tale — regenerate the wall-piece assets: one pack per
;;; display profile (data/gfx/ for :lores, data/gfx-hires/ for :hires).
;;; Run from the project root:  make assets

(load "src/load.lisp")
(load "tools/gen-walls.lisp")

(dolist (p tale::*display-profiles*)
  (format t "Generating ~A wall pieces into ~A ...~%"
          (string-downcase
           (princ-to-string (tale::display-profile-name p)))
          (tale::display-profile-gfx-dir p))
  (format t "~D pieces written.~%"
          (tale::generate-wall-assets :profile p)))
(cl-user::quit 0)
