;;; test-gui.lisp — Amiga GUI tests (Intuition, Graphics, GadTools)
;;;
;;; Loaded from run-tests.lisp via (load ...) inside #+amigaos.
;;; Must be a separate file because the reader needs AMIGA.INTUITION,
;;; AMIGA.GFX, and AMIGA.GADTOOLS packages to exist before it can
;;; read the qualified symbols.

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
