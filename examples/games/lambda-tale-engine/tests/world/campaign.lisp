;;; Lambda's Tale engine — the fixture world's campaign (tests/world/).
;;;
;;; The minimal campaign the engine test suite plays: one melee class,
;;; one caster class with a spell of each walkabout-relevant effect
;;; kind (light against the crypt's darkness, damage, heal, compass),
;;; the shoppe's stock, one monster, and a two-hero starting party (the
;;; caster in slot 2 — the autoplay scripts rely on that).  Game
;;; campaigns live elsewhere; this one exists only so the suite can
;;; exercise a committed world end-to-end.

(in-package :tale)

(define-hero-class :w-fighter :hp-dice "1d10+4" :damage "1d8" :ac 8
                              :singer t)  ; the fixture's singing fighter
(define-hero-class :w-wizard  :hp-dice "1d6+2"  :damage "1d4" :ac 10
                              :caster t)

(define-spell 'w-flame :cost 1 :level 1 :classes '(:w-wizard)
  :light t :duration 60)
(define-spell 'w-bolt  :cost 2 :level 1 :classes '(:w-wizard)
  :damage "1d4+1")
(define-spell 'w-mend  :cost 2 :level 1 :classes '(:w-wizard)
  :heal "1d8")
(define-spell 'w-compass :cost 1 :level 1 :classes '(:w-wizard)
  :compass t :duration 120 :image "fx-needle.iff")

(define-song 'w-march :buff-ac 1 :duration 30)

(define-item 'w-torch :price 2 :use '(:light t :duration 30) :consumed t)
(define-item 'w-sword :kind :weapon :price 20 :damage "1d6+1")

(define-monster "crypt rat"
  :level 1 :hp-dice "1d6" :ac 10 :damage "1d3" :xp 12 :gold "1d6")

(defun default-party ()
  (list (make-hero "Wilhelm" :w-fighter :gold "3d20+60")
        (make-hero "Wanda"   :w-wizard  :gold "3d20+60")))
