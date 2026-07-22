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

(defparameter *engine-dir* cl-user::*lambda-tale-engine-root*
  "Absolute path of the engine's own directory (trailing separator
included), computed by src/load.lisp from *LOAD-TRUENAME*.  Engine
assets — the profiles' default tile packs under data/ — resolve here;
everything a GAME owns (maps, campaigns, zone packs, saves) resolves
against the working directory or the map file instead.")

(defun engine-path (relative)
  "RELATIVE, an engine-relative path like \"data/gfx/\", as an
absolute path under *ENGINE-DIR*."
  (concatenate 'string *engine-dir* relative))

(defstruct (display-profile (:constructor %make-display-profile))
  name                        ; :lores / :hires
  screen-width screen-height  ; custom-screen geometry (:display :screen)
  screen-depth                ; bitplanes; pens 0-3 UI, 4..2^depth-1 pack
  win-width win-height        ; window-mode size (:display :window)
  fp-width fp-height          ; first-person viewport the assets match
  gfx-dir                     ; the profile's default tile pack
  draw-depth                  ; default *DRAW-DEPTH*: how many depth
                              ; levels the view draws (see view.lisp).
                              ; Only a DEFAULT — the profile describes
                              ; a screen, not a CPU, and PLAY-AMIGA's
                              ; :DRAW-DEPTH overrides it per machine
  pad-x pad-y                 ; chrome ring pads (full-screen backdrop)
  view-gap                    ; px between the view column and the log
  band-height                 ; effects + compass band at the log's foot
  roster-cols)                ; roster columns as a plist of char cells:
                              ;   :no :name :ac :hit :hpts :spl :spts :cl
                              ;   (the Bard's Tale table — max/current
                              ;   hit and spell points, class code)

;;; The classic 640x256 PAL hires presentation: 16 colors, the 240x130
;;; viewport the original M3 wall packs were drawn for.
(defparameter *hires-profile*
  (%make-display-profile
   :name :hires
   :screen-width 640 :screen-height 256 :screen-depth 4
   :win-width 640 :win-height 256
   :fp-width 240 :fp-height 130
   :gfx-dir (engine-path "data/gfx-hires/")
   ;; One level short of the full view (+VIEW-DEPTH+ is 4; this file
   ;; loads before view.lisp, so the constant cannot be named here).
   ;; Hires blits roughly twice the lores pixel area per frame, and the
   ;; deepest level is the one that costs a blit to show almost nothing
   ;; — an 8x8 front wall — so it is the cheapest thing to give up.
   ;; Raise it with :DRAW-DEPTH 4 on a machine that can afford it.
   :draw-depth 3
   :pad-x 12 :pad-y 10 :view-gap 12 :band-height 20
   :roster-cols '(:no 0 :name 2 :ac 22 :hit 27 :hpts 32
                  :spl 38 :spts 43 :cl 48)))

;;; The ECS target: 320x256 PAL lores, 5 bitplanes (32 colors) — the
;;; Bard's Tale presentation.  Half the chip-RAM/DMA cost of hires and
;;; near-square pixels for the art.  The 120px viewport gives the view
;;; column 2/5 of the 300px content span and the message log the other
;;; 3/5 — the text matters more than the picture, and the log column
;;; always takes whatever the profile's FP-WIDTH leaves over, so the
;;; split is a profile knob, not engine code.
(defparameter *lores-profile*
  (%make-display-profile
   :name :lores
   :screen-width 320 :screen-height 256 :screen-depth 5
   :win-width 320 :win-height 256
   :fp-width 120 :fp-height 112
   :gfx-dir (engine-path "data/gfx/")
   :draw-depth 4                        ; the full view — the smaller
                                        ; viewport can afford it
   :pad-x 10 :pad-y 10 :view-gap 12 :band-height 20
   :roster-cols '(:no 0 :name 2 :ac 15 :hit 19 :hpts 23
                  :spl 27 :spts 31 :cl 35)))

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
  "Run BODY with *DISPLAY-PROFILE* and the viewport/pack/draw-distance
specials (*FP-VIEW-WIDTH*, *FP-VIEW-HEIGHT*, *GFX-DIR*, *DRAW-DEPTH*)
bound from DESIGNATOR's profile, so the layout, the asset
loader/generator and the manifest all agree on one target.

*DRAW-DEPTH* is bound to the profile's DEFAULT, so a caller that wants
its own draw distance must rebind it INSIDE this macro (PLAY-AMIGA's
:DRAW-DEPTH does) — an outer binding would be overwritten here."
  (let ((p (gensym "PROFILE")))
    `(let* ((,p (find-display-profile ,designator))
            (*display-profile* ,p)
            (*fp-view-width* (display-profile-fp-width ,p))
            (*fp-view-height* (display-profile-fp-height ,p))
            (*gfx-dir* (display-profile-gfx-dir ,p))
            (*draw-depth* (display-profile-draw-depth ,p)))
       ,@body)))
