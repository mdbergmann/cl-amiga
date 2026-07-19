;;; Closure — host entry point.
;;;
;;; Run from the game root (examples/games/closure):  make run
;;; The walkabout UI lives in the engine (PLAY, in the Lambda's Tale
;;; engine's src/host-ui.lisp); this script starts the Closure
;;; campaign.  For your own world:  (tale:play "mygame/village.map")

(load "src/load.lisp")

(in-package :tale)

;; The campaign starts in the town of Closure (worlds/closure/town.map);
;; the cellar dungeon lies below the tavern.
(play "worlds/closure/town.map")

(cl-user::quit 0)
