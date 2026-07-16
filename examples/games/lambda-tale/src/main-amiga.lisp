;;; Lambda's Tale — AmigaOS entry point.
;;;
;;; Run from the project directory on AmigaOS (the repo is mounted as
;;; CLAmiga: in the FS-UAE setup):
;;;   cd CLAmiga:examples/games/lambda-tale
;;;   stack 128000
;;;   CLAmiga:build/amiga/clamiga --heap 8M --load src/main-amiga.lisp

(load "src/load.lisp")

(in-package :tale)

(play-amiga)

(cl-user::quit 0)
