;;; Lambda's Tale — package definition.

(defpackage :tale
  (:use :common-lisp)
  (:export
   ;; directions
   #:+north+ #:+east+ #:+south+ #:+west+
   #:dir-index #:dir-keyword #:dir-opposite #:turn-dir
   ;; map model
   #:parse-map #:load-map-file
   #:dungeon-map-name #:dungeon-map-width #:dungeon-map-height
   #:dungeon-map-wrap #:dungeon-map-start-x #:dungeon-map-start-y
   #:dungeon-map-start-facing
   #:cell-wall #:cell-feature #:wall-passable-p #:neighbor
   ;; knowledge
   #:make-map-knowledge #:know-cell #:know-wall
   #:cell-explored-p #:wall-known-p
   ;; first-person view geometry
   #:+view-depth+ #:compute-view #:view-planes #:view-display-list
   #:view-slice-depth #:view-slice-cx #:view-slice-cy
   #:view-slice-front #:view-slice-left #:view-slice-right
   #:view-slice-lx #:view-slice-ly #:view-slice-left-front
   #:view-slice-rx #:view-slice-ry #:view-slice-right-front
   ;; game state / movement
   #:new-game #:game-map #:game-knowledge #:game-x #:game-y #:game-facing
   #:turn-left #:turn-right #:turn-around #:move-party #:observe
   ;; rendering
   #:render-dungeon #:render-game #:render-first-person #:beside
   ;; interactive walkabout
   #:play #:play-amiga))

(in-package :tale)
