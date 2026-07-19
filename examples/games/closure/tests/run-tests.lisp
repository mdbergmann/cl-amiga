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
  (check-true "town shoppe location"
              (find-if (lambda (op) (string-equal (first op) "LOCATION"))
                       (cell-special m 2 1)))
  (check-true "town tavern is a location with the trapdoor below"
              (let ((op (find-if (lambda (op)
                                   (string-equal (first op) "LOCATION"))
                                 (cell-special m 6 1))))
                (and op (equal "cellar.map" (getf (cdddr op) :down)))))
  (check "town declares the city tile pack" "gfx/"
         (dungeon-map-gfx m)))

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
                   '(torch healing-potion short-sword war-axe broadsword
                     leather-armor chain-mail buckler)))
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
  (dolist (file '("shop.iff" "tavern.iff"))
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
  (give-item g cid 'torch)
  (check-true "El Cid burns a torch" (use-item g cid 'torch))
  (check-true "the torch defeats the dark" (not (game-dark-p g)))
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
  ;; to the shoppe: N, around the well block, through the door
  (move-party g)                          ; (4,4)
  (turn-left g)
  (move-party g) (move-party g)           ; (2,4)
  (turn-right g)
  (move-party g) (move-party g)           ; (2,2)
  (check "shoppe door opens" :door (move-party g))
  (check "the shoppe is a shop location" :shop
         (location-kind (game-location g)))
  (check "the shoppe's picture resolves inside the world"
         "worlds/closure/gfx/shop.iff" (location-image-path g))
  (check "El Cid's portrait resolves inside the world"
         "worlds/closure/gfx/hero-warrior.iff"
         (hero-image-path g (first (game-party g))))
  (leave-location g)
  ;; over to the tavern: back out, east along the street, up the door
  (move-party g :back)                    ; (2,2)
  (turn-right g)
  (move-party g) (move-party g) (move-party g) (move-party g) ; (6,2)
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
  (check "the cellar zone has no pack (profile default)" nil
         (zone-gfx-dir g))
  (check "cellar arrival at its start" '(0 0)
         (list (game-x g) (game-y g)))
  ;; the ladder back up: teleport to the cellar's < cell and step on it
  (teleport-party g 5 4)
  (check "ladder returns to the town" "Closure"
         (map-title (game-map g)))
  (check "ladder lands on the tavern doorstep" '(6 2)
         (list (game-x g) (game-y g))))

;;; ---------------------------------------------------------------------
;;; Unattended autoplay sessions (AmigaOS only): the real event loop —
;;; window, menu strip, redraws, key dispatch — with no user at the
;;; keyboard.  Scripted keys are fed one per INTUITICK, ending in #\q.

;; The cellar is a :DARK zone, so the whole session renders at the
;; one-cell darkness view depth; the script walks, opens a character
;; sheet (1), switches hero (2), leaves it (:esc), and toggles map
;; mode and its debug view.
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
;; shop for real (buy two torches, sell one back), then east to the
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
                               #\w #\a #\w #\w #\d #\w #\w #\w
                               #\1 #\1 #\1 #\s #\1 :esc :esc
                               #\s #\d #\w #\w #\w #\w #\a #\w
                               #\3 #\d
                               #\u #\1 #\1 #\q))
             ;; scratch save dir — keeps the real saves/ untouched
             (*save-dir* "tests/tmp-saves/"))
         (play-amiga "worlds/closure/town.map" :display :window)
         (when (probe-file "tests/tmp-saves/t1.sav")
           (delete-file "tests/tmp-saves/t1.sav"))
         :done))

;; The same on the game's own custom screen with the city pack named
;; explicitly — screen, pack palette and backdrop draw for real.
#+amigaos
(check "autoplay on an own custom screen with the city pack" :done
       (let ((*autoplay* (list #\w #\d #\m #\m #\q)))
         (play-amiga "worlds/closure/cellar.map" :display :screen
                                                 :gfx-dir "worlds/closure/gfx/")
         :done))

;;; ---------------------------------------------------------------------
;;; Summary

(format t "~%Closure tests: ~D checks, ~D failures.~%" *checks* *failures*)
(cl-user::quit (if (zerop *failures*) 0 1))
