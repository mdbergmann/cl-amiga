;;; test-gui.lisp — Amiga GUI tests (Intuition, Graphics, GadTools)
;;;
;;; Loaded from run-tests.lisp via (load ...) inside #+amigaos.
;;; Must be a separate file because the reader needs AMIGA.INTUITION,
;;; AMIGA.GFX, and AMIGA.GADTOOLS packages to exist before it can
;;; read the qualified symbols.
;;;
;;; NOTE: depends on the CHECK macro defined in run-tests.lisp.  The
;;; FASL system only invalidates this file's cache when *this* file's
;;; mtime changes, not when CHECK's definition changes — so if CHECK's
;;; expansion changes, touch this file too or you'll get "Undefined
;;; function: CHECK" at FASL load.

; --- Intuition tests ---
(require "amiga/intuition")

(check "intuition-library-open" t
  (not (null amiga.intuition:*intuition-base*)))

;; Open a window, verify struct fields, close it
(check "intuition-open-close-window" t
  (let ((win (amiga.intuition:open-window
               :title "Test" :left 10 :top 10
               :width 200 :height 100
               :idcmp amiga.intuition:+idcmp-closewindow+)))
    (prog1 (and (not (ffi:null-pointer-p win))
                (= (amiga.intuition:window-width win) 200)
                (= (amiga.intuition:window-height win) 100))
      (amiga.intuition:close-window win))))

;; with-window macro
(check "intuition-with-window" 150
  (amiga.intuition:with-window (win :title "Macro Test"
                                    :width 150 :height 80)
    (amiga.intuition:window-width win)))

;; Window has a non-null RastPort
(check "intuition-window-rastport" t
  (amiga.intuition:with-window (win :title "RP Test"
                                    :width 100 :height 50)
    (let ((rp (amiga.intuition:window-rastport win)))
      (not (ffi:null-pointer-p rp)))))

;; Lock/unlock public screen
(check "intuition-pub-screen" t
  (amiga.intuition:with-pub-screen (scr)
    (not (ffi:null-pointer-p scr))))

;; TITLE NIL opens an untitled window (no WA_Title tag sent).  On a
;; BORDERLESS window that means a genuinely zero top border — with a
;; WA_Title even a borderless window pays a title bar, which is what
;; the :title nil support exists for (full-screen backdrop windows).
;; A normal bordered window keeps its thin frame either way, so the
;; zero-border assertion only holds for the borderless case.
(check "intuition-open-window-title-nil" t
  (amiga.intuition:with-window
      (win :title nil :left 10 :top 10
           :width 150 :height 80
           :flags amiga.intuition:+wflg-borderless+)
    (and (not (ffi:null-pointer-p win))
         (= (amiga.intuition:window-border-top win) 0))))

;; event-loop: (RETURN) from a clause body exits the WHOLE loop.
;; Regression: clause bodies used to be spliced into the inner drain
;; LOOP, whose implicit BLOCK NIL swallowed (RETURN) — quit keys and
;; the close gadget did nothing.  Also verifies the body may read the
;; message's fields (the message is replied only AFTER the body runs).
;; Unattended: ACTIVEWINDOW arrives on open (window has WFLG_ACTIVATE)
;; and INTUITICKS tick ~10/s for the active window, so one of the two
;; clauses fires without any user input.
;; *EVENT-LOOP-MAX-WAITS* bounds the wait: if the window never becomes
;; active in the unattended run (e.g. a stolen focus in FS-UAE), this
;; check fails cleanly with an error instead of blocking the whole
;; suite on an unbounded WaitPort until the external watchdog kills it.
(check "intuition-event-loop-return" :looped
  (let ((amiga.intuition:*event-loop-max-waits* 250))  ; ~5s at 1 tick/poll
    (amiga.intuition:with-window
        (win :title "Loop Test" :width 150 :height 60
             :idcmp (logior amiga.intuition:+idcmp-activewindow+
                            amiga.intuition:+idcmp-intuiticks+))
      (amiga.intuition:event-loop win
        (amiga.intuition:+idcmp-activewindow+ (msg)
          (when (plusp (amiga.intuition:msg-class msg))
            (return :looped)))
        (amiga.intuition:+idcmp-intuiticks+ (msg)
          (when (plusp (amiga.intuition:msg-class msg))
            (return :looped)))))))

; --- Graphics tests ---
(require "amiga/graphics")

(check "graphics-library-open" t
  (not (null amiga.gfx:*gfx-base*)))

;; Move changes the RastPort current pen position
(check "graphics-move-updates-cp" '(50 30)
  (amiga.intuition:with-window (win :title "GFX Test"
                                    :width 200 :height 100)
    (let ((rp (amiga.intuition:window-rastport win)))
      (amiga.gfx:move-to rp 50 30)
      (list (amiga.gfx:rastport-cp-x rp)
            (amiga.gfx:rastport-cp-y rp)))))

;; Draw changes cp to the endpoint
(check "graphics-draw-updates-cp" '(100 80)
  (amiga.intuition:with-window (win :title "Draw Test"
                                    :width 200 :height 100)
    (let ((rp (amiga.intuition:window-rastport win)))
      (amiga.gfx:move-to rp 10 10)
      (amiga.gfx:draw-to rp 100 80)
      (list (amiga.gfx:rastport-cp-x rp)
            (amiga.gfx:rastport-cp-y rp)))))

;; SetAPen changes the foreground pen
(check "graphics-set-a-pen" 2
  (amiga.intuition:with-window (win :title "Pen Test"
                                    :width 100 :height 50)
    (let ((rp (amiga.intuition:window-rastport win)))
      (amiga.gfx:set-a-pen rp 2)
      (amiga.gfx:rastport-fgpen rp))))

;; draw-line (compound: move + draw)
(check "graphics-draw-line" '(90 70)
  (amiga.intuition:with-window (win :title "Line Test"
                                    :width 200 :height 100)
    (let ((rp (amiga.intuition:window-rastport win)))
      (amiga.gfx:draw-line rp 10 10 90 70)
      (list (amiga.gfx:rastport-cp-x rp)
            (amiga.gfx:rastport-cp-y rp)))))

;; rect-fill (should not crash, just verify it returns)
(check "graphics-rect-fill" t
  (amiga.intuition:with-window (win :title "Rect Test"
                                    :width 200 :height 100)
    (let ((rp (amiga.intuition:window-rastport win)))
      (amiga.gfx:rect-fill rp 10 10 50 50)
      t)))

;; gfx-text (render text, verify no crash)
(check "graphics-text" t
  (amiga.intuition:with-window (win :title "Text Test"
                                    :width 200 :height 100)
    (let ((rp (amiga.intuition:window-rastport win)))
      (amiga.gfx:move-to rp 10 30)
      (amiga.gfx:gfx-text rp "Hello")
      t)))

;; Font metric accessors: a fresh window's RastPort carries the screen
;; font, so TxHeight is a small positive pixel count and TxBaseline
;; sits inside the glyph (0 < baseline <= height).
(check "graphics-font-metrics" t
  (amiga.intuition:with-window (win :title "Font Test"
                                    :width 200 :height 100)
    (let* ((rp (amiga.intuition:window-rastport win))
           (h (amiga.gfx:rastport-tx-height rp))
           (b (amiga.gfx:rastport-tx-baseline rp)))
      (and (> h 0) (< h 100) (> b 0) (<= b h)))))

;; OPEN-FONT/SET-FONT/CLOSE-FONT: open the ROM topaz.font at 8px, set it
;; on a window's RastPort, verify the font metrics changed to match, and
;; close it again (CLOSE-FONT must run before the last user goes away).
(check "graphics-open-set-close-font" t
  (amiga.intuition:with-window (win :title "Open Font Test"
                                    :width 200 :height 100)
    (let* ((rp (amiga.intuition:window-rastport win))
           (font (amiga.gfx:open-font "topaz.font" 8)))
      (unwind-protect
          (and font
               (progn (amiga.gfx:set-font rp font)
                      (let ((h (amiga.gfx:rastport-tx-height rp))
                            (b (amiga.gfx:rastport-tx-baseline rp)))
                        (and (= h 8) (> b 0) (<= b h)))))
        (when font (amiga.gfx:close-font font))))))

; --- Bitmap / blit tests (offscreen, no window needed) ---

(check "graphics-version-known" t (>= (amiga.gfx:gfx-version) 39))

;; AllocBitMap + GetBitMapAttr: attributes are at least what was asked
;; for (RTG drivers may round the width up)
(check "graphics-alloc-bitmap-attrs" t
  (amiga.gfx:with-bitmap (bm 64 32 2)
    (and (>= (amiga.gfx:get-bitmap-attr bm amiga.gfx:+bma-width+) 64)
         (>= (amiga.gfx:get-bitmap-attr bm amiga.gfx:+bma-height+) 32)
         (>= (amiga.gfx:get-bitmap-attr bm amiga.gfx:+bma-depth+) 2))))

;; Chunky pens in, pixels back: write a 4x2 pattern into an offscreen
;; bitmap through a scratch rastport, read every pixel back, and check
;; a neighbor outside the written rect stayed clear (BMF_CLEAR).
(check "graphics-write-chunky-read-pixel" '(0 1 2 3 3 2 1 0 0)
  (amiga.gfx:with-bitmap (bm 32 8 2)
    (amiga.gfx:with-bitmap-rastport (rp bm)
      (amiga.gfx:write-chunky rp 2 1 4 2 #(0 1 2 3 3 2 1 0))
      (append (loop for (x y) in '((2 1) (3 1) (4 1) (5 1)
                                   (2 2) (3 2) (4 2) (5 2))
                    collect (amiga.gfx:read-pixel rp x y))
              (list (amiga.gfx:read-pixel rp 6 1))))))

;; Same pattern, forced through the V39 per-pixel WritePixel fallback
;; (both FS-UAE test configs boot graphics.library v40, so this path
;; is otherwise never exercised in CI).
(check "graphics-write-chunky-fallback-read-pixel" '(0 1 2 3 3 2 1 0 0)
  (let ((amiga.gfx:*write-chunky-force-fallback* t))
    (amiga.gfx:with-bitmap (bm 32 8 2)
      (amiga.gfx:with-bitmap-rastport (rp bm)
        (amiga.gfx:write-chunky rp 2 1 4 2 #(0 1 2 3 3 2 1 0))
        (append (loop for (x y) in '((2 1) (3 1) (4 1) (5 1)
                                     (2 2) (3 2) (4 2) (5 2))
                      collect (amiga.gfx:read-pixel rp x y))
                (list (amiga.gfx:read-pixel rp 6 1)))))))

(check "graphics-write-chunky-rejects-short-vector" t
  (amiga.gfx:with-bitmap (bm 16 4 2)
    (amiga.gfx:with-bitmap-rastport (rp bm)
      (handler-case (progn (amiga.gfx:write-chunky rp 0 0 4 2 #(1 2 3))
                           nil)
        (error () t)))))

;; BltBitMapRastPort: copy a region bitmap-to-bitmap and verify the
;; pattern lands at the destination offset (and only there).
(check "graphics-blt-bitmap-rastport" '(3 2 0)
  (amiga.gfx:with-bitmap (src 32 8 2)
    (amiga.gfx:with-bitmap (dst 32 8 2)
      (amiga.gfx:with-bitmap-rastport (srp src)
        (amiga.gfx:write-chunky srp 1 1 2 1 #(3 2)))
      (amiga.gfx:with-bitmap-rastport (drp dst)
        (amiga.gfx:blt-bitmap-rastport src 0 0 drp 10 2 8 4)
        (list (amiga.gfx:read-pixel drp 11 3)   ; (1,1) -> (11,3)
              (amiga.gfx:read-pixel drp 12 3)   ; (2,1) -> (12,3)
              (amiga.gfx:read-pixel drp 9 3))))))  ; outside the blit

;; BltMaskBitMapRastPort: cookie-cut a source through a 1-bit mask so
;; only the masked-in pixels reach the destination (the rest keep what
;; was there — how transparent wall corners let a backdrop show).
(check "graphics-blt-mask-bitmap-rastport" '(3 0 0)
  (amiga.gfx:with-bitmap (src 32 8 2)
    (amiga.gfx:with-bitmap (dst 32 8 2)
      ;; source: pen 3 across a 2x2 block at (0,0)
      (amiga.gfx:with-bitmap-rastport (srp src)
        (amiga.gfx:write-chunky srp 0 0 2 2 #(3 3 3 3)))
      ;; mask: 16-wide blit -> 2 bytes/row, 4 rows; only row0 pixel0 set
      (let ((mask (amiga:alloc-chip 8)))
        (unwind-protect
             (progn
               (dotimes (i 8) (ffi:poke-u8 mask 0 i))
               (ffi:poke-u8 mask #x80 0)
               (amiga.gfx:with-bitmap-rastport (drp dst)
                 (amiga.gfx:blt-mask-bitmap-rastport src 0 0 drp 10 2 16 4
                                                     mask)
                 (list (amiga.gfx:read-pixel drp 10 2)   ; masked in: 3
                       (amiga.gfx:read-pixel drp 11 2)   ; src 3 but masked out
                       (amiga.gfx:read-pixel drp 9 2)))) ; outside the blit
          (amiga:free-chip mask))))))

; --- GadTools tests ---
(require "amiga/gadtools")

(check "gadtools-library-open" t
  (not (null amiga.gadtools:*gadtools-base*)))

;; Create and free a gadget context
(check "gadtools-create-context" t
  (ffi:with-foreign-alloc (glist 4)
    (ffi:poke-u32 glist 0)
    (let ((ctx (amiga.gadtools:create-context glist)))
      (prog1 (not (ffi:null-pointer-p ctx))
        (amiga.gadtools:free-gadgets
          (ffi:make-foreign-pointer (ffi:peek-u32 glist)))))))

;; Create a button gadget via the full chain
(check "gadtools-create-button" t
  (amiga.intuition:with-pub-screen (scr)
    (amiga.gadtools:with-visual-info (vi scr)
      (ffi:with-foreign-alloc (glist 4)
        (ffi:poke-u32 glist 0)
        (let* ((ctx (amiga.gadtools:create-context glist))
               (btn (amiga.gadtools:create-gadget
                      amiga.gadtools:+button-kind+ ctx vi
                      :left 10 :top 20 :width 80 :height 14
                      :text "OK" :gadget-id 1)))
          (prog1 (not (null btn))
            (amiga.gadtools:free-gadgets
              (ffi:make-foreign-pointer (ffi:peek-u32 glist)))))))))
