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
;;
;; TALE_DRAW_DEPTH in the environment (1-4) trades view distance for
;; frames on a slower machine — see the engine README's "Draw distance"
;; section, and DRAW_DEPTH= in run-amiga.sh.  Unset means the display
;; profile's own default (the full view).
(let* ((spec (ext:getenv "TALE_DRAW_DEPTH"))
       (depth (and spec (parse-integer spec :junk-allowed t))))
  (play-amiga "worlds/closure/town.map" :draw-depth depth))

(cl-user::quit 0)
