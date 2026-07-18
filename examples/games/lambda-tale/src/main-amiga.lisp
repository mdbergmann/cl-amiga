;;; Lambda's Tale — AmigaOS entry point.
;;;
;;; Run from the project directory on AmigaOS (the repo is mounted as
;;; CLAmiga: in the FS-UAE setup):
;;;   cd CLAmiga:examples/games/lambda-tale
;;;   stack 128000
;;;   CLAmiga:build/amiga/clamiga --heap 8M --load src/main-amiga.lisp

(load "src/load.lisp")

(in-package :tale)

;; Opens the game's own PAL 640x256 16-color custom screen (RTG-aware
;; via BestModeIDA).  The game starts in the town (data/town.map); the
;; cellar dungeon lies below the tavern.  For a development view in a
;; window on the Workbench screen instead:
;;   (play-amiga "data/town.map" :display :window)
(play-amiga)

(cl-user::quit 0)
