;;; Lambda's Tale — regenerate the data/gfx wall-piece assets.
;;; Run from the project root:  make assets

(load "src/load.lisp")
(load "tools/gen-walls.lisp")

(format t "Generating wall pieces into data/gfx/ ...~%")
(format t "~D pieces written.~%" (tale::generate-wall-assets))
(cl-user::quit 0)
