;;; Closure — AmigaOS entry point.
;;;
;;; Run from the game directory on AmigaOS (the repo is mounted as
;;; CLAmiga: in the FS-UAE setup):
;;;   cd CLAmiga:examples/games/closure
;;;   stack 128000
;;;   CLAmiga:build/amiga/clamiga --heap 8M --load src/main-amiga.lisp

(load "src/load.lisp")

(in-package :tale)

;; Opens the game's own custom screen per the default display profile
;; (:lores — PAL 320x256, 32 colors, RTG-aware via BestModeIDA; pass
;; :profile :hires for the 640x256 16-color presentation).  The game
;; starts in the town of Closure (worlds/closure/town.map); the cellar
;; dungeon lies below the tavern.  For a development view in a window
;; on the Workbench screen instead:
;;   (play-amiga "worlds/closure/town.map" :display :window)
(play-amiga "worlds/closure/town.map")

(cl-user::quit 0)
