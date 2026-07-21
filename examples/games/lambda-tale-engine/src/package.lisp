;;; Lambda's Tale — package definition.

(defpackage :tale
  (:use :common-lisp)
  (:export
   ;; directions
   #:+north+ #:+east+ #:+south+ #:+west+
   #:dir-index #:dir-keyword #:dir-opposite #:turn-dir
   ;; dice
   #:*rng* #:roll #:parse-dice #:roll-dice
   ;; ILBM images
   #:make-image #:read-ilbm #:write-ilbm #:pixel-ref
   #:image-width #:image-height #:image-depth #:image-palette #:image-pixels
   #:image-transparent-p #:mask-bytes
   ;; map model
   #:parse-map #:load-map-file #:map-viewport
   #:dungeon-map-name #:dungeon-map-width #:dungeon-map-height
   #:dungeon-map-wrap #:dungeon-map-start-x #:dungeon-map-start-y
   #:dungeon-map-start-facing #:dungeon-map-kind #:dungeon-map-title
   #:dungeon-map-dark #:map-title
   #:cell-wall #:cell-feature #:cell-special #:wall-passable-p #:neighbor
   ;; knowledge
   #:make-map-knowledge #:know-cell #:know-wall
   #:cell-explored-p #:wall-known-p
   ;; first-person view geometry
   #:+view-depth+ #:compute-view #:view-planes #:view-display-list
   #:view-blit-list #:wall-piece-rect #:wall-piece-names #:wall-piece-file
   #:wall-piece-variant-file
   #:backdrop-rects #:print-tile-manifest
   #:view-slice-depth #:view-slice-cx #:view-slice-cy
   #:view-slice-front #:view-slice-left #:view-slice-right
   #:view-slice-lx #:view-slice-ly #:view-slice-left-front
   #:view-slice-rx #:view-slice-ry #:view-slice-right-front
   #:compass-points
   ;; game state / movement
   #:new-game #:game-map #:game-knowledge #:game-x #:game-y #:game-facing
   #:game-party #:game-flags #:game-combat
   #:game-effects #:add-effect #:remove-effect #:find-effect
   #:effect-name #:effect-expires-at #:effect-payload #:effect-label
   #:effect-image #:effect-image-path #:apply-effect-spec
   #:effects-ac-bonus #:light-active-p #:compass-active-p
   #:turn-left #:turn-right #:turn-around #:move-party #:observe
   ;; game time, day and night
   #:game-time #:advance-time #:daylight-p #:clock-line
   #:game-dark-p #:game-view-depth
   #:*minutes-per-action* #:*new-game-minutes* #:+minutes-per-day+
   ;; the world: zones and travel
   #:game-zones #:travel-party #:load-campaign
   ;; events and story flags
   #:on-event #:emit #:say #:flag #:set-flag #:clear-flag
   ;; message log (the Bard's Tale text column)
   #:attach-message-log #:log-message #:log-recent
   #:wrap-text #:wrap-message
   ;; structured menu lines (option rows carry their pick key)
   #:menu-option #:menu-numbered #:menu-line-text #:menu-line-key
   #:menu-texts #:wrap-menu-line #:menu-key-spans
   ;; menu scrolling (long lists window with u/d + marker rows)
   #:+menu-page-size+ #:menu-window #:menu-window-pick
   #:menu-scroll #:menu-scrolled-lines
   ;; cell specials
   #:trigger-special #:run-special #:teleport-party
   ;; heroes and the party
   #:define-hero-class #:hero-classes
   #:make-hero #:hero-name #:hero-class #:hero-level
   #:hero-xp #:hero-max-hp #:hero-hp #:hero-max-sp #:hero-sp
   #:hero-caster-p #:hero-str #:hero-dex #:hero-iq
   #:hero-con #:hero-lck #:hero-ac #:hero-damage #:hero-gold
   #:hero-alive-p #:alive-heroes #:party-alive-p #:front-ranks
   #:hero-class-title #:hero-class-abbrev #:hero-summary-lines
   #:hero-image #:hero-image-path #:hero-sheet-lines #:hero-sheet-scroll
   #:+party-limit+ #:party-full-p #:join-party
   #:damage-hero #:heal-hero #:stat-bonus #:award-xp #:xp-for-level
   ;; items, inventory and equipment
   #:define-item #:find-item-type #:item-title #:item-usable-p
   #:item-fit-marker
   #:+inventory-limit+ #:hero-items #:hero-equipped #:hero-carrying-p
   #:give-item #:drop-item #:equip-item #:unequip-item #:equipped-of-kind
   #:toggle-equip #:hero-attack-dice #:hero-effective-ac
   ;; the gear page ('e' on the character sheet)
   #:make-equip-view #:equip-view-hero #:equip-view-top
   #:equip-lines #:equip-act
   ;; using items (torch, potion — the use menu)
   #:usable-items #:use-item
   #:make-use-view #:use-view-hero #:use-view-item #:use-view-top
   #:use-lines #:use-act
   ;; locations and shops
   #:game-location #:enter-location #:leave-location
   #:location-title #:location-kind #:location-arg
   #:location-image #:location-image-path
   #:cell-location-op #:facing-location-image-path
   #:shop-stock #:item-price #:item-sell-price #:buy-item #:sell-item
   #:make-shop-view #:shop-view-hero #:shop-view-mode #:shop-view-top
   #:shop-lines #:shop-act #:location-lines #:location-act
   ;; spells
   #:define-spell #:find-spell-type #:spell-title #:spell-target-kind
   #:spell-known-p #:spell-castable-p #:spells-for-hero #:cast-spell
   #:make-cast-view #:cast-view-hero #:cast-view-spell #:cast-view-top
   #:cast-lines #:cast-act #:*sp-regen-minutes*
   ;; bard songs
   #:define-song #:find-song-type #:song-title #:song-known-p
   #:songs-for-hero #:sing-song #:current-song
   #:hero-singer-p #:hero-tunes #:hero-max-tunes
   #:make-sing-view #:sing-view-hero #:sing-view-top
   #:sing-lines #:sing-act
   ;; taverns (drinks refill a singer's tunes)
   #:tavern-price #:buy-drink #:tavern-lines #:tavern-act
   ;; combat
   #:define-monster #:find-monster-type #:monster-type-name
   #:monster-type-level #:monster-type-ac #:monster-type-xp
   #:monster-kind #:monster-hp #:monster-alive-p
   #:start-combat #:combat-round #:attempt-flee
   #:combat-monsters #:alive-monsters #:combat-groups #:combat-banner
   #:combat-round-no
   ;; combat round orders (every hero picks, then the round runs)
   #:make-combat-orders #:combat-orders-hero #:combat-orders-chosen
   #:combat-orders-lines #:combat-orders-act
   ;; combat transcript speed (the +/- keys)
   #:*combat-speed* #:+combat-speed-max+
   #:combat-message-delay #:adjust-combat-speed
   ;; save games
   #:save-game #:load-game
   ;; named saves (the save/load slot menu)
   #:*save-dir* #:slot-path #:ensure-save-dir #:save-slots
   #:make-save-menu #:save-menu-mode #:save-menu-entry
   #:save-menu-lines #:save-menu-act
   ;; rendering
   #:render-dungeon #:render-game #:render-first-person #:beside
   #:map-edge-runs #:map-legend-entries #:*legend-markers*
   ;; the help screen
   #:help-lines
   ;; the microfont (the message log's compact 5x7 pixel font)
   #:+microfont-advance+ #:+microfont-line-height+
   #:microfont-glyph #:microfont-text-width #:microfont-line
   ;; debug log (timestamped trace file, see src/debug-log.lisp)
   #:*debug-log-path* #:debug-log-enable #:debug-log-disable
   #:debug-log-enabled-p #:dlog #:dlog-timed
   ;; interactive walkabout
   #:play #:play-amiga #:*gfx-dir*
   ;; engine home (self-located; default packs live under it)
   #:*engine-dir* #:engine-path))

(in-package :tale)
