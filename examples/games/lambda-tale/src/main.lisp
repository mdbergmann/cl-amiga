;;; Lambda's Tale — host entry point.
;;;
;;; Run from the project root (examples/games/lambda-tale):  make run
;;; The walkabout UI itself lives in src/host-ui.lisp (PLAY); this
;;; script just starts the demo campaign.  For your own world:
;;;   (tale:play "mygame/village.map")

(load "src/load.lisp")

(in-package :tale)

;; The demo campaign starts in the town (worlds/closure/town.map);
;; the cellar dungeon lies below the tavern.
(play)

(cl-user::quit 0)
