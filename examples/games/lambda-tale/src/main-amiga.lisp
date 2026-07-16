;;; Lambda's Tale — AmigaOS entry point.
;;;
;;; Run from the project directory on AmigaOS (the repo is mounted as
;;; CLAmiga: in the FS-UAE setup):
;;;   cd CLAmiga:examples/games/lambda-tale
;;;   stack 128000
;;;   CLAmiga:build/amiga/clamiga --heap 8M --load src/main-amiga.lisp

(load "src/load.lisp")

(in-package :tale)

;; Default: a window on the Workbench screen.  For the real thing —
;; an own PAL custom screen (RTG-aware) — run:
;;   (play-amiga "data/cellar.map" :display :screen)
(play-amiga)

(cl-user::quit 0)
