;;; Lambda's Tale — the screen pen contract.
;;;
;;; The game runs on one custom screen with ONE palette, and a zone
;;; change swaps the tile pack under it (%APPLY-PACK-PALETTE).  A
;;; bitmap, though, is nothing but pen indices: an image loaded in one
;;; zone and still cached in the next (%CACHED-IMAGE keys by path, not
;;; by pack) re-colors the moment the new pack's CMAP lands.  So the
;;; screen's 32 pens are split in two, and this file is where the line
;;; is drawn:
;;;
;;;   ENGINE pens have a fixed color no pack may change.  Art painted
;;;   in them means the same thing in every zone, so anything that
;;;   TRAVELS — monster art, hero portraits, effect icons — may only
;;;   ink these.
;;;
;;;   PACK pens are the pack's own.  A zone's walls, backdrops and
;;;   location pictures live here, and their colors change under the
;;;   player's feet on zone travel, which is exactly the point: the
;;;   night street and the cellar are the same pens in different
;;;   colors.
;;;
;;; The 32-color :lores layout (the canonical art target):
;;;
;;;   0        engine  transparent key in wall pieces
;;;   1 2 3    engine  the UI colors: white, grey, amber
;;;   4        engine  opaque black (frames, joints, outlines)
;;;   5 6      PACK    sky / ceiling, ground / floor
;;;   7-16     PACK    art
;;;   17 18 19 engine  the mouse pointer's sprite registers
;;;   20-23    PACK    art
;;;   24-31    engine  the shared FIGURE core
;;;
;;; A fixed pen is shared, not lost: a wall piece may ink the figure
;;; core freely (the quantizer does — see %MAKE-PEN-MAPPER), it just
;;; cannot RE-COLOR it.  A pack therefore has 16 pens of its own plus
;;; 12 more to draw with, and travelling art has 12 solid colors plus
;;; transparency, which is a Bard's Tale bestiary's worth.
;;;
;;; The figure core sits at the TOP of the range on purpose.  Every
;;; pack that exists — the procedural dungeon and city packs from
;;; gen-walls.lisp, Closure's street and cellar — inks pens 0-9 only,
;;; so reserving downward from 31 costs the shipped art nothing.
;;;
;;; :hires (16 colors) has no pen 24, hence no figure core and no
;;; pointer pens: its layout is 0-4 engine, 5-6 and 7-15 pack, exactly
;;; as before.  It is a wall-pack target only — travelling art on a
;;; 16-color screen is limited to the UI pens, which is what hero
;;; portraits have always done.  :lores is where the contract lives
;;; and where new art should be drawn.

(in-package :tale)

;;; ---------------------------------------------------------------------
;;; The fixed pens.

(defconstant +pen-transparent+ 0
  "The transparent key in a wall piece: the ceiling/floor backdrop
shows through it (see %PLANAR-PIECE-MASK).  Never assigned by the
quantizer — art black lands on +PEN-OPAQUE-BLACK+ instead.")

(defconstant +pen-ui-white+ 1)
(defconstant +pen-ui-grey+ 2)
(defconstant +pen-ui-amber+ 3)

(defconstant +pen-opaque-black+ 4
  "Solid black inside a piece — mortar, joints, door frames, figure
outlines.  Distinct from pen 0 so it blits as black, not as a hole.")

(defconstant +art-pen-sky+ 5)
(defconstant +art-pen-ground+ 6)

(defconstant +art-pen-base+ 7
  "First pen a pack's art may fill; 0-6 are spoken for.")

(defparameter *ui-pens*
  '((0 (0 0 0)) (1 (255 255 255)) (2 (136 136 136)) (3 (255 170 51))
    (4 (0 0 0)))
  "(PEN (R G B)) of the fixed UI pens — text and wireframe stay
readable in any pack, so no pack may move them.")

;;; Pens 17-19 are the mouse pointer's: the hardware sprite shares
;;; those color registers on the 32-color screen, and the front end
;;; re-latches them from the pointer art *after* loading a pack's
;;; palette (%ENSURE-STANDARD-POINTER follows %APPLY-PACK-PALETTE).
;;; Art quantized into them would come out in the pointer's red.  On a
;;; 16-color profile they are out of range and cost nothing.

(defparameter *pointer-pens*
  '((17 (238 68 68)) (18 (51 0 0)) (19 (238 238 204)))
  "(PEN (R G B)) of the mouse-pointer sprite's registers — the classic
red pointer, dark outline, light gleam (%GAME-SCREEN-PALETTE).")

;;; The figure core.  These are hand-picked constants, deliberately
;;; NOT derived from the art that uses them: a core computed over the
;;; bestiary would make monster #17 change the core, and every pack in
;;; every world would be stale.  Fixed means a new monster is a new
;;; file and nothing else moves.
;;;
;;; What they have to cover, given white/grey/amber/black come free
;;; from the UI pens: skin, and the things that hang off a figure.  A
;;; three-step flesh ramp doubles as wood and leather, the two steels
;;; as armour and cast shadow, and red/green carry cloth, blood,
;;; scales and slime.  Every value is on the Amiga's 12-bit grid (a
;;; multiple of 17) so SET-RGB4 shows it exactly.

(defconstant +figure-pen-base+ 24
  "First pen of the shared figure core (:lores only).")

(defparameter *figure-pens*
  '((24 (255 204 153))    ; flesh light   — skin, bone, lit wood
    (25 (204 136 102))    ; flesh mid     — skin, leather, timber
    (26 (136  68  51))    ; flesh dark    — shadowed skin, dark wood
    (27 (170  17  17))    ; blood red     — cloth, blood, eyes
    (28 ( 68 136  68))    ; leaf green    — scales, slime, cloth
    (29 (102 119 153))    ; steel light   — armour, blades
    (30 ( 51  51  68))    ; steel dark    — armour shadow, cold shade
    (31 (238 221 187)))   ; bone light    — highlights, teeth, cloth
  "(PEN (R G B)) of the shared figure core: the colors every zone
guarantees, so art that outlives a pack switch can be drawn in them.
See FIGURE-PALETTE-PENS for the full set such art may ink.")

;;; ---------------------------------------------------------------------
;;; Who owns which pen at a given depth.

(defun %pens-in-range (spec depth)
  "The pens of SPEC — an alist of (PEN (R G B)) — that exist on a
DEPTH-bitplane screen."
  (loop for entry in spec
        when (< (first entry) (ash 1 depth))
          collect (first entry)))

(defun figure-pens (depth)
  "The shared figure-core pens on a DEPTH-bitplane screen — NIL below
5 planes, where the core does not fit (see the header)."
  (%pens-in-range *figure-pens* depth))

(defun pointer-pens (depth)
  "The mouse-pointer sprite's pens on a DEPTH-bitplane screen."
  (%pens-in-range *pointer-pens* depth))

(defun art-pen-plan (depth)
  "The pens a pack of DEPTH bitplanes may fill with quantized art, in
order — everything from +ART-PEN-BASE+ up except the pointer's and the
figure core's.  14 at :lores, 9 at :hires."
  (let ((reserved (append (pointer-pens depth) (figure-pens depth))))
    (loop for p from +art-pen-base+ below (ash 1 depth)
          unless (member p reserved)
            collect p)))

(defun pack-pens (depth)
  "Every pen whose color a tile pack owns on a DEPTH-bitplane screen:
the sky and ground, then the art pens.  This is exactly what
%APPLY-PACK-PALETTE loads from a pack's CMAP — every other pen belongs
to the engine and is asserted by %GAME-SCREEN-PALETTE, so a
mis-exported palette.iff cannot move the UI, the pointer or the
figures."
  (list* +art-pen-sky+ +art-pen-ground+ (art-pen-plan depth)))

(defun figure-palette-pens (depth)
  "Every pen art that OUTLIVES a pack switch — monsters, portraits,
effect icons — may ink: the opaque UI colors and the figure core.  Pen
0 is excluded because it is the transparency key rather than a color,
and pens 5/6 because sky and ground are the pack's to change."
  (append (list +pen-ui-white+ +pen-ui-grey+ +pen-ui-amber+
                +pen-opaque-black+)
          (figure-pens depth)))

(defun fixed-pen-color (pen)
  "The engine-fixed (R G B) of PEN, or NIL when PEN belongs to the
packs."
  (second (or (assoc pen *ui-pens*)
              (assoc pen *pointer-pens*)
              (assoc pen *figure-pens*))))

;;; ---------------------------------------------------------------------
;;; Day-time sky and ground colour.
;;;
;;; The first-person sky (pen +ART-PEN-SKY+) and ground (pen
;;; +ART-PEN-GROUND+) take a different colour in each band of the day
;;; (see TIME.LISP).  It is a palette-only effect — no new art, no
;;; redraw: when the band turns the front-end reloads just those two
;;; colour registers (%APPLY-DAYTIME-PALETTE), so a 14MHz 020 pays a
;;; couple of SET-RGB4 calls for the whole change.
;;;
;;; A zone declares its own sky/ground colour with (ZONE :SKY C :GROUND
;;; C); that colour is the NOON base, and the engine derives the other
;;; bands by blending the base toward a per-band ANCHOR by a WEIGHT in
;;; 0..1 (0 = the base untouched, 1 = the anchor).  So noon is exactly
;;; what the zone declared, morning lifts it a touch brighter, evening
;;; warms and dims it, and night sinks it to near-black — and a zone
;;; that paints a red alien sky still goes dark at nightfall.  A zone
;;; that declares nothing falls back to *DEFAULT-SKY* / *DEFAULT-GROUND*.
;;;
;;; Colours are (R G B) lists of 0-255 components, exactly as the rest
;;; of this file and READ-ILBM's CMAPs carry them.  Weights are rationals
;;; so the blend is integer-exact and identical on host and Amiga.

(defparameter *default-sky* '(102 170 204)
  "The noon sky colour of a zone that declares no (ZONE :SKY ...) — a
bright daylight blue.")

(defparameter *default-ground* '(102 85 68)
  "The noon ground colour of a zone that declares no (ZONE :GROUND ...)
— an earthy brown.")

(defparameter *sky-band-tints*
  '((:morning   (204 221 238) 3/10)    ; toward a pale dawn blue
    (:noon      (0 0 0)        0)       ; the base, untouched
    (:afternoon (170 187 204) 3/20)    ; a touch softer than noon
    (:evening   (170 85 68)   9/20)    ; warm dusk
    (:night     (0 0 17)      17/20))  ; near-black, a hint of blue
  "(BAND ANCHOR WEIGHT) — the sky's per-band blend of the zone's noon
base toward ANCHOR by WEIGHT.  See SKY-COLOR-FOR.")

(defparameter *ground-band-tints*
  '((:morning   (136 119 102) 1/5)     ; catching the dawn light
    (:noon      (0 0 0)        0)
    (:afternoon (85 68 51)     3/20)
    (:evening   (85 51 34)     2/5)     ; lengthening shadow
    (:night     (0 0 0)        4/5))    ; all but black
  "(BAND ANCHOR WEIGHT) — the ground's per-band blend of the zone's
noon base toward ANCHOR by WEIGHT.  See GROUND-COLOR-FOR.")

(defun %clamp-byte (n)
  (max 0 (min 255 n)))

(defun %blend-rgb (base anchor weight)
  "Blend (R G B) BASE toward ANCHOR by WEIGHT (0..1): the channel is
BASE*(1-WEIGHT) + ANCHOR*WEIGHT, rounded and clamped to 0-255."
  (mapcar (lambda (a b)
            (%clamp-byte (round (+ (* a (- 1 weight)) (* b weight)))))
          base anchor))

(defun %band-color-for (base band table default)
  (let ((base (or base default))
        (entry (cdr (assoc band table))))
    (if entry
        (%blend-rgb base (first entry) (second entry))
        (copy-list base))))

(defun sky-color-for (base band)
  "The sky colour (R G B) of a zone whose NOON base sky is BASE (NIL =
*DEFAULT-SKY*) at day-band BAND (see *SKY-BAND-TINTS*)."
  (%band-color-for base band *sky-band-tints* *default-sky*))

(defun ground-color-for (base band)
  "The ground colour (R G B) of a zone whose NOON base ground is BASE
\(NIL = *DEFAULT-GROUND*) at day-band BAND (see *GROUND-BAND-TINTS*)."
  (%band-color-for base band *ground-band-tints* *default-ground*))

(defun fixed-pen-role (pen)
  "A short human-readable role for PEN — what the palette.gpl and the
tile manifest call it."
  (cond ((= pen +pen-transparent+) "transparent key")
        ((= pen +pen-ui-white+) "UI white")
        ((= pen +pen-ui-grey+) "UI grey")
        ((= pen +pen-ui-amber+) "UI amber")
        ((= pen +pen-opaque-black+) "opaque black")
        ((= pen +art-pen-sky+) "sky (pack)")
        ((= pen +art-pen-ground+) "ground (pack)")
        ((assoc pen *pointer-pens*) "mouse pointer")
        ((assoc pen *figure-pens*) "figure core")
        (t "art (pack)")))
