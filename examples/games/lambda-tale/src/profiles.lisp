;;; Lambda's Tale — display profiles.
;;;
;;; A DISPLAY-PROFILE bundles everything that depends on the screen a
;;; target machine can show: screen/window geometry, the first-person
;;; viewport the wall assets are drawn for, the tile-pack directory and
;;; the layout tuning (chrome pads, log/compass band, roster columns).
;;; The engine and the renderers read the profile — and the view.lisp
;;; specials it binds — at call time, so a new target (say a big RTG
;;; screen) is a new profile plus an asset pack, not new code.
;;;
;;; This file is platform-independent: the asset generator and the
;;; tile-pack manifest use profiles on the host too, so it loads before
;;; view.lisp (whose viewport specials initialize from the default
;;; profile).

(in-package :tale)

(defstruct (display-profile (:constructor %make-display-profile))
  name                        ; :lores / :hires
  screen-width screen-height  ; custom-screen geometry (:display :screen)
  screen-depth                ; bitplanes; pens 0-3 UI, 4..2^depth-1 pack
  win-width win-height        ; window-mode size (:display :window)
  fp-width fp-height          ; first-person viewport the assets match
  gfx-dir                     ; the profile's default tile pack
  pad-x pad-y                 ; chrome ring pads (full-screen backdrop)
  view-gap                    ; px between the view column and the log
  band-height                 ; effects + compass band at the log's foot
  roster-cols)                ; roster columns as a plist of char cells

;;; The classic 640x256 PAL hires presentation: 16 colors, the 240x130
;;; viewport the original M3 wall packs were drawn for.
(defparameter *hires-profile*
  (%make-display-profile
   :name :hires
   :screen-width 640 :screen-height 256 :screen-depth 4
   :win-width 640 :win-height 256
   :fp-width 240 :fp-height 130
   :gfx-dir "data/gfx-hires/"
   :pad-x 12 :pad-y 10 :view-gap 12 :band-height 48
   :roster-cols '(:no 0 :name 2 :lv 18 :hits 25 :gold 38 :down 46)))

;;; The ECS target: 320x256 PAL lores, 5 bitplanes (32 colors) — the
;;; Bard's Tale presentation.  Half the chip-RAM/DMA cost of hires and
;;; near-square pixels for the art.
(defparameter *lores-profile*
  (%make-display-profile
   :name :lores
   :screen-width 320 :screen-height 256 :screen-depth 5
   :win-width 320 :win-height 256
   :fp-width 160 :fp-height 112
   :gfx-dir "data/gfx/"
   :pad-x 10 :pad-y 10 :view-gap 12 :band-height 48
   :roster-cols '(:no 0 :name 2 :lv 15 :hits 18 :gold 26 :down 33)))

(defparameter *display-profiles*
  (list *lores-profile* *hires-profile*))

(defvar *display-profile* *lores-profile*
  "The active display profile — :lores, the ECS target, by default.
PLAY-AMIGA's :PROFILE argument and WITH-DISPLAY-PROFILE rebind it
together with the view.lisp viewport specials.")

(defun find-display-profile (designator)
  "Resolve DESIGNATOR — a DISPLAY-PROFILE or its :NAME keyword — to a
profile from *DISPLAY-PROFILES*."
  (etypecase designator
    (display-profile designator)
    (symbol (or (find designator *display-profiles*
                      :key #'display-profile-name)
                (error "Unknown display profile ~S (have ~{~S~^, ~})"
                       designator
                       (mapcar #'display-profile-name
                               *display-profiles*))))))

(defmacro with-display-profile ((designator) &body body)
  "Run BODY with *DISPLAY-PROFILE* and the viewport/pack specials
(*FP-VIEW-WIDTH*, *FP-VIEW-HEIGHT*, *GFX-DIR*) bound from DESIGNATOR's
profile, so the layout, the asset loader/generator and the manifest all
agree on one target."
  (let ((p (gensym "PROFILE")))
    `(let* ((,p (find-display-profile ,designator))
            (*display-profile* ,p)
            (*fp-view-width* (display-profile-fp-width ,p))
            (*fp-view-height* (display-profile-fp-height ,p))
            (*gfx-dir* (display-profile-gfx-dir ,p)))
       ,@body)))
