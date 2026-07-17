;;; Lambda's Tale on its own custom screen with the "city" tile pack
;;; (tan street, night sky with stars, grey stone walls).  Run from
;;; the game directory; regenerate the pack with make-pack.lisp.

(load "src/load.lisp")

(in-package :tale)

(play-amiga "data/cellar.map" :gfx-dir "gfx-city-demo/")

(cl-user::quit 0)
