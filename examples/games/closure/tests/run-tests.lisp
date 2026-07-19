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
  (check-true "town tavern leads to the cellar"
              (find-if (lambda (op) (string-equal (first op) "TRAVEL"))
                       (cell-special m 6 1)))
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
                   '(mage-flame arc-fire minor-mend stone-skin)))
(check-true "Wolfgar's stock exists"
            (every #'find-item-type
                   '(torch short-sword war-axe broadsword
                     leather-armor chain-mail buckler)))
(check-true "the cellar's monsters exist"
            (every #'find-monster-type
                   '("giant rat" "kobold" "skeleton" "footpad")))
(let ((party (default-party)))
  (check "the starting party is four strong" 4 (length party))
  (check "Zzgo brings up the rear" "Zzgo"
         (hero-name (fourth party)))
  (check-true "Zzgo is the caster" (hero-caster-p (fourth party))))

;;; ---------------------------------------------------------------------
;;; Walk the shipped world end-to-end: gate -> shoppe -> tavern ->
;;; cellar and back up — the campaign's whole overworld loop on real
;;; data.

(let* ((m (load-map-file "worlds/closure/town.map"))
       (g (new-game m)))
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
  (leave-location g)
  ;; over to the tavern: back out, east along the street, up the door
  (move-party g :back)                    ; (2,2)
  (turn-right g)
  (move-party g) (move-party g) (move-party g) (move-party g) ; (6,2)
  (turn-left g)
  (check "tavern trapdoor drops into the cellar" :door (move-party g))
  (check "tavern travel landed in the cellar" "the cellar"
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
;; 4, spell 1), save and reload through the slot picker (S n "t1"
;; Return; L 1), walk from the gate to Wolfgar's shoppe, shop for
;; real (buy a torch, sell it back), then east to the tavern and down
;; the trapdoor into the dark cellar — the town's city pack swaps for
;; the cellar's default pack on the way, and Zzgo's flame keeps the
;; view lit — then quit.
#+amigaos
(check "autoplay casts, saves, shops, drops to the dark cellar" :done
       (let ((*autoplay* (list #\c #\4 #\1
                               #\S #\n #\t #\1 #\Return
                               #\L #\1
                               #\w #\a #\w #\w #\d #\w #\w #\w
                               #\1 #\1 #\s #\1 :esc :esc
                               #\s #\d #\w #\w #\w #\w #\a #\w #\q))
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
