;;; Closure — the game's test suite: the shipped world (the town of
;;; Closure and its cellar), the campaign data, the city tile pack and
;;; — on AmigaOS — unattended autoplay sessions through the real event
;;; loop.  Engine mechanics are the engine suite's business
;;; (../lambda-tale-engine/tests/run-tests.lisp); this suite checks
;;; the game built on top of them.
;;; Run from the game root (examples/games/closure):  make test

(load "src/load.lisp")

(in-package :tale)

;;; ---------------------------------------------------------------------
;;; Tiny test harness (same shape as the engine suite's)

(defvar *checks* 0)
(defvar *failures* 0)

(defun check (label expected actual)
  (incf *checks*)
  (unless (equal expected actual)
    (incf *failures*)
    (format t "FAIL ~A~%  expected: ~S~%  actual:   ~S~%"
            label expected actual)))

(defun check-true (label value)
  (check label t (not (not value))))

;;; ---------------------------------------------------------------------
;;; The shipped maps and their story layer

(let ((m (load-map-file "worlds/closure/cellar.map")))
  (check "cellar width" 6 (dungeon-map-width m))
  (check "cellar height" 5 (dungeon-map-height m))
  (check "cellar stairs down" #\> (cell-feature m 3 2))
  (check "cellar stairs up" #\< (cell-feature m 5 4))
  (check "cellar is a dungeon zone" :dungeon (dungeon-map-kind m))
  (check-true "cellar start special" (cell-special m 0 0))
  (check-true "cellar stairs-down special" (cell-special m 3 2))
  (check-true "cellar ladder leads back to town"
              (find-if (lambda (op) (string-equal (first op) "TRAVEL"))
                       (cell-special m 5 4))))

(let ((m (load-map-file "worlds/closure/town.map")))
  (check "town is a city zone" :city (dungeon-map-kind m))
  (check "town title" "Closure" (map-title m))
  (check "town is Skara Brae sized" '(30 30)
         (list (dungeon-map-width m) (dungeon-map-height m)))
  (check "the party starts at the south gate" '(15 29)
         (list (dungeon-map-start-x m) (dungeon-map-start-y m)))
  (check-true "town shoppe location"
              (find-if (lambda (op) (string-equal (first op) "LOCATION"))
                       (cell-special m 12 28)))
  (check-true "town tavern is a location with the trapdoor below"
              (let ((op (find-if (lambda (op)
                                   (string-equal (first op) "LOCATION"))
                                 (cell-special m 17 28))))
                (and op (equal "cellar.map" (getf (cdddr op) :down)))))
  (check "town declares the city tile pack" "gfx/"
         (dungeon-map-gfx m))
  ;; the town's houses (gen-town.lisp): a real city's worth of doors,
  ;; every one drawing one of the three facade pictures from the
  ;; street (:facade) and the style-matched interior inside (:image) —
  ;; and all three styles are actually dealt out
  (let ((houses 0)
        (facades '())
        (bad '()))
    (maphash (lambda (cell ops)
               (let ((loc (find-if (lambda (op)
                                     (string-equal (first op) "LOCATION"))
                                   ops)))
                 (when (and loc (eq (third loc) :house))
                   (incf houses)
                   (let* ((facade (getf (cdddr loc) :facade))
                          (image (getf (cdddr loc) :image))
                          (style (position facade
                                           '("gfx/house-0.iff"
                                             "gfx/house-1.iff"
                                             "gfx/house-2.iff")
                                           :test #'equal)))
                     ;; interior and perspective piece variant (:style,
                     ;; the pack's base timber/v1 stone/v2 townhouse
                     ;; order) both match the facade's house type
                     (if (and style
                              (equal image
                                     (format nil "gfx/interior-~D.iff"
                                             style))
                              (eql (getf (cdddr loc) :style)
                                   (aref #(1 0 2) style)))
                         (pushnew facade facades :test #'equal)
                         (push (list cell facade image) bad))))))
             (dungeon-map-specials m))
    (check-true "the town holds at least thirty houses" (>= houses 30))
    (check "every house pairs facade, interior and piece style"
           nil bad)
    (check "all three facade styles appear in the town" 3
           (length facades)))
  ;; the map legend: the shoppe and the tavern head the list before
  ;; the plain houses (the omniscient debug view lists everything the
  ;; marker alphabet can carry)
  (let ((legend (map-legend-entries m nil :full t)))
    (check-true "the full legend carries a city's worth of markers"
                (> (length legend) 30))
    (check "Wolfgar's heads the legend" "Wolfgar's Arms & Armour"
           (fourth (first legend)))
    (check "the Adventurer's Rest follows" "The Adventurer's Rest"
           (fourth (second legend)))
    (check "legend markers start at 1" #\1 (first (first legend)))))

;;; ---------------------------------------------------------------------
;;; The campaign: classes, spells, items, monsters, the starting party

(load-campaign "worlds/closure/town.map")

(check-true "the conjurer is a caster class"
            (let ((h (make-hero "T" :conjurer)))
              (hero-caster-p h)))
(check-true "the conjurer's book is complete"
            (every #'find-spell-type
                   '(mage-flame arc-fire minor-mend stone-skin
                     magic-compass)))
(check-true "the bard is a singer class"
            (hero-singer-p (make-hero "M" :bard)))
(check-true "Melody's repertoire is complete"
            (every #'find-song-type '(travellers-tune seekers-ballad)))
(check-true "Wolfgar's stock exists"
            (every #'find-item-type
                   '(torch healing-potion lantern dagger short-sword
                     mace war-axe broadsword leather-armor splint-mail
                     chain-mail buckler tower-shield)))
(check-true "the cellar's monsters exist"
            (every #'find-monster-type
                   '("giant rat" "kobold" "skeleton" "footpad")))
(let ((party (default-party)))
  (check "the starting party is four strong" 4 (length party))
  (check "Zzgo brings up the rear" "Zzgo"
         (hero-name (fourth party)))
  (check-true "Zzgo is the caster" (hero-caster-p (fourth party))))

;; The timed spells' effects-band icons ship inside the world's gfx
;; dir (regenerate with worlds/closure/gfx/make-pack.lisp) and resolve
;; map-relative when cast.
(let ((stale '()))
  (dolist (file '("fx-flame.iff" "fx-shield.iff" "fx-compass.iff"))
    (let ((path (concatenate 'string "worlds/closure/gfx/" file)))
      (if (probe-file path)
          (let ((img (read-ilbm path)))
            (unless (and (= 16 (image-width img))
                         (= 16 (image-height img))
                         (image-transparent-p img))
              (push (list file :malformed) stale)))
          (push (list file :missing) stale))))
  (check "the spell icons exist, 16x16 with the transparent key"
         nil stale))

;; The takeover art ships inside the world's gfx dir too: the location
;; pictures (shown in the view column while the shop/tavern menu takes
;; over the message area) fill the lores viewport exactly; the class
;; portraits (beside the character sheet) are the generator's standard
;; square.  Regenerate with worlds/closure/gfx/make-pack.lisp.
(let ((stale '()))
  (dolist (file '("shop.iff" "tavern.iff"
                  "house-0.iff" "house-1.iff" "house-2.iff"
                  "interior-0.iff" "interior-1.iff" "interior-2.iff"))
    (let ((path (concatenate 'string "worlds/closure/gfx/" file)))
      (if (probe-file path)
          (let ((img (read-ilbm path)))
            (unless (and (= (image-width img) *fp-view-width*)
                         (= (image-height img) *fp-view-height*))
              (push (list file :mis-sized) stale)))
          (push (list file :missing) stale))))
  (dolist (file '("hero-warrior.iff" "hero-paladin.iff" "hero-rogue.iff"
                  "hero-bard.iff" "hero-conjurer.iff"))
    (let ((path (concatenate 'string "worlds/closure/gfx/" file)))
      (if (probe-file path)
          (let ((img (read-ilbm path)))
            (unless (and (= 64 (image-width img))
                         (= 64 (image-height img)))
              (push (list file :mis-sized) stale)))
          (push (list file :missing) stale))))
  (check "the location pictures and portraits exist at their sizes"
         nil stale))

;; The town's ceiling is a plain, starless night sky: every pixel is
;; the sky pen (regenerate with worlds/closure/gfx/make-pack.lisp).
(let ((img (read-ilbm "worlds/closure/gfx/ceiling.iff"))
      (pens '()))
  (dotimes (y (image-height img))
    (dotimes (x (image-width img))
      (pushnew (pixel-ref img x y) pens)))
  (check "the town ceiling is a flat starless sky" '(5) pens))

;;; The city pack's wall pieces are HOUSES (the engine's city house
;;; style, draw-city-wall-piece) — the street view shows rows of
;;; timber houses, Bard's Tale style, not dungeon stone.  Same
;;; loader contract as any pack: every piece at its slot size.
(load (engine-path "tools/gen-walls.lisp"))   ; *house-colors*
(let ((dir "worlds/closure/gfx/")
      (planes (view-planes *fp-view-width* *fp-view-height*))
      (stale '()))
  (dolist (piece (wall-piece-names))
    (destructuring-bind (x y w h) (wall-piece-rect planes piece)
      (declare (ignore x y))
      ;; base piece plus the two style variants (v1 stone cottage,
      ;; v2 townhouse) the engine deals out per building
      (dolist (file (list (wall-piece-file piece)
                          (wall-piece-variant-file piece 1)
                          (wall-piece-variant-file piece 2)))
        (let ((path (concatenate 'string dir file)))
          (if (probe-file path)
              (let ((img (read-ilbm path)))
                (unless (and (= (image-width img) w)
                             (= (image-height img) h))
                  (push (list file :mis-sized) stale)))
              (push (list file :missing) stale))))))
  (check "every town wall piece + style variants at their slot size"
         nil stale)
  ;; the three styles actually differ where the pieces are big enough
  ;; to read (the near front)
  (let ((imgs (mapcar (lambda (file)
                        (image-pixels
                         (read-ilbm (concatenate 'string dir file))))
                      (list (wall-piece-file '(:front 0))
                            (wall-piece-variant-file '(:front 0) 1)
                            (wall-piece-variant-file '(:front 0) 2)))))
    (check-true "the town front's three house styles are distinct"
                (and (not (equalp (first imgs) (second imgs)))
                     (not (equalp (first imgs) (third imgs)))
                     (not (equalp (second imgs) (third imgs))))))
  ;; the near front is a house: plaster walls, a thatch roof band,
  ;; and lit windows (the amber pen) — not the dungeon's grey brick
  (let ((img (read-ilbm (concatenate 'string dir "front-0.iff")))
        (pens '()))
    (dotimes (y (image-height img))
      (dotimes (x (image-width img))
        (pushnew (pixel-ref img x y) pens)))
    (check-true "the town front is plastered (pen 7)" (member 7 pens))
    (check-true "the town front is thatched (pen 9)" (member 9 pens))
    (check-true "the town front has lit windows (pen 3)" (member 3 pens)))
  ;; the receding side keeps the cookie-cut contract: its ceiling and
  ;; floor corners stay pen 0 so the sky and street show through
  (let ((img (read-ilbm (concatenate 'string dir "side-0-l.iff"))))
    (check "the town side keeps its transparent sky corner" 0
           (pixel-ref img (1- (image-width img)) 0))
    (check "the town side keeps its transparent street corner" 0
           (pixel-ref img (1- (image-width img))
                      (1- (image-height img)))))
  ;; palette contract: pen 4 stays black, pens 7-9 carry the engine's
  ;; house colors the pieces are drawn with
  (let ((pal (image-palette (read-ilbm (concatenate 'string
                                                    dir "palette.iff")))))
    (check "the town pack leaves the frame pen black"
           '(0 0 0) (aref pal 4))
    (dolist (entry *house-colors*)
      (check (format nil "the town pack carries house pen ~D"
                     (first entry))
             (second entry) (aref pal (first entry))))))

;;; The cellar's pack (regenerate with gfx-cellar/make-pack.lisp): the
;;; loader demands every wall piece at its exact slot size and falls
;;; back to wireframe otherwise, so the whole contract is checked here
;;; rather than discovered on the Amiga.
(let ((dir "worlds/closure/gfx-cellar/")
      (planes (view-planes *fp-view-width* *fp-view-height*))
      (stale '()))
  (dolist (piece (wall-piece-names))
    (let ((path (concatenate 'string dir (wall-piece-file piece))))
      (if (probe-file path)
          (let ((img (read-ilbm path)))
            (destructuring-bind (x y w h) (wall-piece-rect planes piece)
              (declare (ignore x y))
              (unless (and (= (image-width img) w) (= (image-height img) h))
                (push (list (wall-piece-file piece) :mis-sized) stale))))
          (push (list (wall-piece-file piece) :missing) stale))))
  (check "every cellar wall piece is present at its slot size" nil stale)
  ;; the backdrops fill their slots (they are blitted opaque, so a
  ;; short one would leave the previous frame showing)
  (destructuring-bind (ceiling floor) (backdrop-rects planes)
    (dolist (entry (list (list "ceiling.iff" ceiling)
                         (list "floor.iff" floor)))
      (destructuring-bind (file (x y w h)) entry
        (declare (ignore x y))
        (let ((img (read-ilbm (concatenate 'string dir file))))
          (check (format nil "the cellar ~A fills its slot" file)
                 (list w h)
                 (list (image-width img) (image-height img)))))))
  ;; the floor is flat packed earth (pen 5), and the ceiling darkens
  ;; through its bands to black — a cellar roof, not the town's sky
  (let ((floor-pens '()))
    (let ((img (read-ilbm (concatenate 'string dir "floor.iff"))))
      (dotimes (y (image-height img))
        (dotimes (x (image-width img))
          (pushnew (pixel-ref img x y) floor-pens))))
    (check "the cellar floor is flat packed earth" '(5) floor-pens))
  (let ((img (read-ilbm (concatenate 'string dir "ceiling.iff")))
        (ceiling-pens '()))
    (dotimes (y (image-height img))
      (dotimes (x (image-width img))
        (pushnew (pixel-ref img x y) ceiling-pens)))
    (check "the cellar ceiling darkens in bands to black"
           '(0 6 7) (sort ceiling-pens #'<)))
  ;; pen 4 must stay black: the wall pieces draw mortar and door frames
  ;; with it, and the loader applies the pack's palette from pen 4 up
  (let ((pal (image-palette (read-ilbm (concatenate 'string
                                                    dir "palette.iff")))))
    (check "the cellar pack leaves the mortar pen black"
           '(0 0 0) (aref pal 4))
    (check "the cellar pack paints its own floor pen"
           '(102 85 68) (aref pal 5))))
(let* ((m (load-map-file "worlds/closure/town.map"))
       (g (new-game m :party (default-party)))
       (zzgo (fourth (game-party g))))
  ;; rolled stats can leave a level-1 conjurer short of the 2 sp —
  ;; top him up so the check is deterministic
  (setf (hero-sp zzgo) (max (hero-sp zzgo) 2))
  (check-true "Zzgo casts the magic compass"
              (cast-spell g zzgo 'magic-compass))
  (check-true "the party knows its facing" (compass-active-p g))
  (check "the compass icon resolves inside the world"
         "worlds/closure/gfx/fx-compass.iff"
         (effect-image-path g (find-effect g "magic compass")))
  ;; Melody strikes up the ballad: the song displaces nothing (the
  ;; compass is a spell, not a song) and shows its icon
  (let ((melody (third (game-party g))))
    (check-true "Melody sings the seeker's ballad"
                (sing-song g melody 'seekers-ballad))
    (check "the ballad is the current song" "seekers ballad"
           (effect-name (current-song g)))
    (check "the ballad's icon resolves inside the world"
           "worlds/closure/gfx/fx-compass.iff"
           (effect-image-path g (current-song g)))
    (check "the spell effect was not displaced" 2
           (length (game-effects g)))))

;; The torch is usable at last: burning one lights the party for an
;; hour, spends it, and shows the flame icon in the band.
(let* ((m (load-map-file "worlds/closure/cellar.map"))
       (g (new-game m :party (default-party)))
       (cid (first (game-party g))))
  (check-true "the cellar is dark" (game-dark-p g))
  (check "the cellar grants three cells of sight (:dark 3)" 3
         (game-view-depth g))
  (give-item g cid 'torch)
  (check-true "El Cid burns a torch" (use-item g cid 'torch))
  (check-true "the torch defeats the dark" (not (game-dark-p g)))
  (check "the torch buys the full view depth" +view-depth+
         (game-view-depth g))
  (check "the torch burns for an hour" (+ (game-time g) 60)
         (effect-expires-at (find-effect g "Torch")))
  (check "the torch shows the flame icon"
         "worlds/closure/gfx/fx-flame.iff"
         (effect-image-path g (find-effect g "Torch")))
  (check "the torch is spent" nil (hero-carrying-p cid 'torch)))

;;; ---------------------------------------------------------------------
;;; Walk the shipped world end-to-end: gate -> shoppe -> tavern ->
;;; cellar and back up — the campaign's whole overworld loop on real
;;; data.

(let* ((m (load-map-file "worlds/closure/town.map"))
       (g (new-game m :party (default-party))))
  (trigger-special g)
  (check "the town's zone pack resolves inside the world directory"
         "worlds/closure/gfx/" (zone-gfx-dir g))
  ;; to the shoppe: west along the wall street, through the door
  (turn-left g)
  (move-party g) (move-party g) (move-party g)   ; (12,29)
  (turn-right g)
  (check "facing the shoppe door shows its front from the street"
         "worlds/closure/gfx/shop.iff" (facing-location-image-path g))
  (check "shoppe door opens" :door (move-party g))
  (check "the shoppe is a shop location" :shop
         (location-kind (game-location g)))
  (check "the shoppe's picture resolves inside the world"
         "worlds/closure/gfx/shop.iff" (location-image-path g))
  (check "El Cid's portrait resolves inside the world"
         "worlds/closure/gfx/hero-warrior.iff"
         (hero-image-path g (first (game-party g))))
  ;; Wolfgar's stock is deeper than a menu page: the buy list scrolls,
  ;; and a digit picks within the visible window
  (check-true "the stock outgrows a menu page"
              (> (length (shop-stock (game-location g)))
                 +menu-page-size+))
  (let ((cid (first (game-party g)))
        (view (make-shop-view)))
    (setf (hero-gold cid) 200)
    (shop-act g view #\1)               ; El Cid shops
    (check-true "the deep stock shows its below marker"
                (member "v more below [d]"
                        (menu-texts (shop-lines g view)) :test #'equal))
    (shop-act g view #\d)
    (shop-act g view #\d)               ; to the bottom of the stock
    (check "the stock window clamps at the tail" 8 (shop-view-top view))
    (check-true "the tail window lists the tower shield"
                (find-if (lambda (s) (search "5) Tower Shield" s))
                         (menu-texts (shop-lines g view))))
    (shop-act g view #\5)               ; buy it from the scrolled window
    (check-true "the windowed digit buys the tower shield"
                (hero-carrying-p cid 'tower-shield))
    (check "the fresh shield auto-equips" 'tower-shield
           (equipped-of-kind cid :shield))
    (shop-act g view #\Escape))
  (leave-location g)
  ;; over to the tavern: back out, east along the street, up the door
  (move-party g :back)                    ; (12,29)
  (turn-right g)
  (move-party g) (move-party g) (move-party g)
  (move-party g) (move-party g)           ; (17,29)
  (turn-left g)
  (check "the tavern door opens" :door (move-party g))
  (check "the Adventurer's Rest is a tavern" :tavern
         (location-kind (game-location g)))
  (check "the tavern's picture resolves inside the world"
         "worlds/closure/gfx/tavern.iff" (location-image-path g))
  (check "a drink at the Rest is four gold" 4
         (tavern-price (game-location g)))
  ;; Melody drinks (her tunes refill), then down the trapdoor
  (let ((melody (third (game-party g))))
    (setf (hero-tunes melody) 0)
    (tavern-act g #\3)
    (check "Melody's tunes came back" (hero-max-tunes melody)
           (hero-tunes melody)))
  (check "the trapdoor drops into the cellar" :left (tavern-act g #\d))
  (check "the trapdoor landed in the cellar" "the cellar"
         (map-title (game-map g)))
  ;; the cellar brings its own pack, resolved inside the world — the
  ;; trapdoor is a pack swap, and the town's pack is not what a cellar
  ;; should look like
  (check "the cellar zone declares its own pack"
         "worlds/closure/gfx-cellar/" (zone-gfx-dir g))
  (check "cellar arrival at its start" '(0 0)
         (list (game-x g) (game-y g)))
  ;; the ladder back up: teleport to the cellar's < cell and step on it
  (teleport-party g 5 4)
  (check "ladder returns to the town" "Closure"
         (map-title (game-map g)))
  (check "the town's pack comes back with it"
         "worlds/closure/gfx/" (zone-gfx-dir g))
  (check "ladder lands on the tavern doorstep" '(17 29)
         (list (game-x g) (game-y g))))

;;; ---------------------------------------------------------------------
;;; Unattended autoplay sessions (AmigaOS only): the real event loop —
;;; window, menu strip, redraws, key dispatch — with no user at the
;;; keyboard.  Scripted keys are fed one per INTUITICK, ending in #\q.

;; The cellar is a :DARK 3 zone, so the whole session renders at the
;; three-cell darkness view depth; the script walks, opens a character
;; sheet (1), switches hero (2), leaves it (:esc), and toggles map
;; mode and its debug view.  This session keeps :DISPLAY :WINDOW —
;; the Workbench-window development view — alive as a smoke test; the
;; rest of the suite runs :DISPLAY :SCREEN, the game's presentation.
#+amigaos
(check "autoplay plays a scripted cellar session and quits" :done
       (let ((*autoplay* (list #\w #\d #\1 #\2 :esc #\w #\a
                               #\m #\f #\f #\m #\s #\q)))
         (play-amiga "worlds/closure/cellar.map" :display :window)
         :done))

;; The town: cast mage flame through the real event loop (c, Zzgo is
;; 4, spell 1) and let Melody strike up the traveller's tune (p, 3,
;; song 1 — her one tune), save and reload through the slot picker
;; (S n "t1" Return; L 1), walk from the gate to Wolfgar's shoppe,
;; shop for real (scroll the deep stock down and back up, buy two
;; torches, sell one back), then east to the
;; Adventurer's Rest: Melody drinks her tunes back (3) and the party
;; takes the trapdoor down (d) into the dark cellar — the town's city
;; pack swaps for the cellar's default pack on the way, and Zzgo's
;; flame keeps the view lit — where El Cid burns the bought torch
;; through the use menu (u, hero 1, item 1), then quit.
#+amigaos
(check "autoplay casts, saves, shops, drops to the dark cellar" :done
       (let ((*autoplay* (list #\c #\4 #\1
                               #\p #\3 #\1
                               #\S #\n #\t #\1 #\Return
                               #\L #\1
                               #\a #\w #\w #\w #\d #\w
                               #\1 #\d #\u #\1 #\1 #\s #\1 :esc :esc
                               #\s #\d #\w #\w #\w #\w #\w #\a #\w
                               #\3 #\d
                               #\u #\1 #\1 #\q))
             ;; scratch save dir — keeps the real saves/ untouched
             (*save-dir* "tests/tmp-saves/"))
         (play-amiga "worlds/closure/town.map" :display :screen)
         (when (probe-file "tests/tmp-saves/t1.sav")
           (delete-file "tests/tmp-saves/t1.sav"))
         :done))

;; The same on the game's own custom screen with the city pack named
;; explicitly — screen, pack palette and backdrop draw for real.  The
;; cellar declares its own (ZONE :GFX "gfx-cellar/"), so this also
;; pins the precedence: an explicit :GFX-DIR outranks the zone's pack.
#+amigaos
(check "autoplay on an own custom screen with the city pack" :done
       (let ((*autoplay* (list #\w #\d #\m #\m #\q)))
         (play-amiga "worlds/closure/cellar.map" :display :screen
                                                 :gfx-dir "worlds/closure/gfx/")
         :done))

;; A real pack swap on the real machine: the town loads its city pack,
;; the trapdoor drops into the cellar and swaps in gfx-cellar/ — both
;; packs load, both palettes apply, and the cellar's backdrops blit.
;; The opening m/f/f/m draws the CITY map page for real — the 30x30
;; grid at 7px cells is the merged-runs fast path, and the omniscient
;; f view fills the legend and stamps every marker through the
;; small-cell microfont path.  The route is the player's (d w w a w
;; reaches the tavern door from the gate — facing it blits the
;; tavern's facade into the view column — d takes the trapdoor down);
;; it stops there because the way back is through the cellar's maze,
;; where a scripted walk meets the random encounter at (1,2).  The
;; cache's swap-back is covered by the engine suite's
;; *GFX-CACHE-PACKS* policy tests.
#+amigaos
(check "autoplay drops through the trapdoor into the cellar's pack" :done
       (let ((*autoplay* (list #\m #\f #\f #\m        ; the city map page
                               #\d #\w #\w #\a #\w    ; to the tavern
                               #\d                    ; down the trapdoor
                               #\m #\m                ; look around below
                               #\q)))
         (play-amiga "worlds/closure/town.map" :display :screen)
         :done))

;;; ---------------------------------------------------------------------
;;; Summary

(format t "~%Closure tests: ~D checks, ~D failures.~%" *checks* *failures*)

;; The summary also goes to a file: an unattended FS-UAE run loses the
;; console, so the host checks this after the emulator quits.
(with-open-file (out "tests/results.txt"
                     :direction :output :if-exists :supersede)
  (format out "Closure tests: ~D checks, ~D failures.~%"
          *checks* *failures*))

(cl-user::quit (if (zerop *failures*) 0 1))
