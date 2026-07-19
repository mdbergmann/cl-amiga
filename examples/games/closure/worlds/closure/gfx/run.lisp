;;; Closure's cellar on its own custom screen wearing the town's
;;; "city" tile pack (tan street, plain night sky, grey stone
;;; walls — normally the cellar keeps the default dungeon stone).
;;; Run from the game root (examples/games/closure); regenerate the
;;; pack with make-pack.lisp.

(load "src/load.lisp")

(in-package :tale)

(play-amiga "worlds/closure/cellar.map" :gfx-dir "worlds/closure/gfx/")

(cl-user::quit 0)
