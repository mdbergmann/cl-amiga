;;; Lambda's Tale — package definition.

(defpackage :tale
  (:use :common-lisp)
  (:export
   ;; directions
   #:+north+ #:+east+ #:+south+ #:+west+
   #:dir-index #:dir-keyword #:dir-opposite #:turn-dir
   ;; dice
   #:*rng* #:roll #:parse-dice #:roll-dice
   ;; map model
   #:parse-map #:load-map-file #:map-viewport
   #:dungeon-map-name #:dungeon-map-width #:dungeon-map-height
   #:dungeon-map-wrap #:dungeon-map-start-x #:dungeon-map-start-y
   #:dungeon-map-start-facing
   #:cell-wall #:cell-feature #:cell-special #:wall-passable-p #:neighbor
   ;; knowledge
   #:make-map-knowledge #:know-cell #:know-wall
   #:cell-explored-p #:wall-known-p
   ;; first-person view geometry
   #:+view-depth+ #:compute-view #:view-planes #:view-display-list
   #:view-slice-depth #:view-slice-cx #:view-slice-cy
   #:view-slice-front #:view-slice-left #:view-slice-right
   #:view-slice-lx #:view-slice-ly #:view-slice-left-front
   #:view-slice-rx #:view-slice-ry #:view-slice-right-front
   #:compass-points
   ;; game state / movement
   #:new-game #:game-map #:game-knowledge #:game-x #:game-y #:game-facing
   #:game-party #:game-flags #:game-combat
   #:game-effects #:add-effect #:remove-effect
   #:turn-left #:turn-right #:turn-around #:move-party #:observe
   ;; events and story flags
   #:on-event #:emit #:say #:flag #:set-flag #:clear-flag
   ;; message log (the Bard's Tale text column)
   #:attach-message-log #:log-message #:log-recent
   #:wrap-text #:wrap-message
   ;; cell specials
   #:trigger-special #:run-special #:teleport-party
   ;; heroes and the party
   #:define-hero-class #:make-hero #:hero-name #:hero-class #:hero-level
   #:hero-xp #:hero-max-hp #:hero-hp #:hero-str #:hero-dex #:hero-iq
   #:hero-con #:hero-lck #:hero-ac #:hero-damage #:hero-gold
   #:hero-alive-p #:alive-heroes #:party-alive-p #:front-ranks
   #:+party-limit+ #:party-full-p #:join-party
   #:damage-hero #:heal-hero #:stat-bonus #:award-xp #:xp-for-level
   ;; combat
   #:define-monster #:find-monster-type #:monster-type-name
   #:monster-type-level #:monster-type-ac #:monster-type-xp
   #:monster-kind #:monster-hp #:monster-alive-p
   #:start-combat #:combat-round #:attempt-flee
   #:combat-monsters #:alive-monsters #:combat-groups #:combat-banner
   ;; save games
   #:save-game #:load-game
   ;; rendering
   #:render-dungeon #:render-game #:render-first-person #:beside
   ;; interactive walkabout
   #:play #:play-amiga))

(in-package :tale)
