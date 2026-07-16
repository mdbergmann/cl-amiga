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
(define-hero-class :conjurer :hp-dice "1d6+1"  :damage "1d4" :ac 10)

;;; Monsters

(define-monster "giant rat"
  :level 1 :hp-dice "1d6"  :ac 10 :damage "1d3" :xp 12 :gold "1d6")
(define-monster "kobold"
  :level 1 :hp-dice "1d8"  :ac 9  :damage "1d4" :xp 18 :gold "1d10")
(define-monster "skeleton"
  :level 2 :hp-dice "2d8"  :ac 8  :damage "1d6" :xp 35 :gold "2d8")

;;; The starting party

(defun default-party ()
  (list (make-hero "El Cid" :warrior)
        (make-hero "Corfid" :rogue)
        (make-hero "Melody" :bard)
        (make-hero "Zzgo"   :conjurer)))
