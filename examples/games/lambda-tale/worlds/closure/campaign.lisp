;;; Lambda's Tale — demo campaign data.
;;;
;;; Everything story-specific lives here and in the map files: hero
;;; classes, monster types, the starting party.  The engine itself
;;; knows none of these facts.

(in-package :tale)

;;; Hero classes

(define-hero-class :warrior  :hp-dice "1d10+4" :damage "1d8" :ac 8)
(define-hero-class :paladin  :hp-dice "1d10+3" :damage "1d8" :ac 8)
(define-hero-class :rogue    :hp-dice "1d8+2"  :damage "1d6" :ac 9)
(define-hero-class :bard     :hp-dice "1d8+2"  :damage "1d6" :ac 9)
(define-hero-class :conjurer :hp-dice "1d6+1"  :damage "1d4" :ac 10
                             :caster t)

;;; Spells (the conjurer's book — one of each engine effect kind)

(define-spell 'mage-flame :cost 1 :level 1 :classes '(:conjurer)
  :light t :duration 60)
(define-spell 'arc-fire   :cost 2 :level 1 :classes '(:conjurer)
  :damage "1d4+1")
(define-spell 'minor-mend :cost 2 :level 1 :classes '(:conjurer)
  :heal "1d8")
(define-spell 'stone-skin :cost 3 :level 2 :classes '(:conjurer)
  :buff-ac 2 :duration 30)

;;; Items (Wolfgar's stock — see worlds/closure/town.map)

(define-item 'torch         :price 2)
(define-item 'short-sword   :kind :weapon :price 20 :damage "1d6+1")
(define-item 'war-axe       :kind :weapon :price 40 :damage "1d8+1"
             :classes '(:warrior :paladin))
(define-item 'broadsword    :kind :weapon :price 80 :damage "2d4+2"
             :classes '(:warrior :paladin :rogue))
(define-item 'leather-armor :kind :armor :price 25 :ac 2)
(define-item 'chain-mail    :kind :armor :price 90 :ac 4
             :classes '(:warrior :paladin))
(define-item 'buckler       :kind :shield :price 30 :ac 1
             :classes '(:warrior :paladin :rogue :bard))

;;; Monsters

(define-monster "giant rat"
  :level 1 :hp-dice "1d6"  :ac 10 :damage "1d3" :xp 12 :gold "1d6")
(define-monster "kobold"
  :level 1 :hp-dice "1d8"  :ac 9  :damage "1d4" :xp 18 :gold "1d10")
(define-monster "skeleton"
  :level 2 :hp-dice "2d8"  :ac 8  :damage "1d6" :xp 35 :gold "2d8")
(define-monster "footpad"
  :level 1 :hp-dice "1d8+1" :ac 9 :damage "1d4" :xp 20 :gold "2d6")

;;; The starting party

(defun default-party ()
  (list (make-hero "El Cid" :warrior  :gold "3d20+60")
        (make-hero "Corfid" :rogue    :gold "3d20+60")
        (make-hero "Melody" :bard     :gold "3d20+60")
        (make-hero "Zzgo"   :conjurer :gold "3d20+60")))
