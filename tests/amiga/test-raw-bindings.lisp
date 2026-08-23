;;; test-raw-bindings.lisp — Amiga-side tests of the generated raw OS
;;; bindings (lib/amiga/raw/, see scripts/gen-amiga-bindings.lisp).
;;;
;;; Loaded from run-tests.lisp via (load ...) inside #+amigaos, like
;;; test-gui.lisp: the reader needs the AMIGA.RAW.* packages to exist
;;; before it can read the qualified symbols, and CHECK comes from
;;; run-tests.lisp (touch this file when CHECK changes, see the note in
;;; test-gui.lisp).
;;;
;;; What this exercises that the host cannot: the calls actually reach
;;; the OS through OP_AMIGA_CALL — every result kind (pointer / signed /
;;; unsigned / bool / void / u8), struct accessors on live OS objects,
;;; CALL-LIBRARY's >7-register path, and the platform/version guards as
;;; seen by the running system (AmigaOS 3.1 under FS-UAE, 3.2 on real
;;; hardware, MorphOS).

;; The first REQUIRE of each raw module compiles it from source (a few
;; thousand forms for intuition/graphics/dos) — on a 68k that takes a
;; while, so flush a progress line per module to keep the FS-UAE
;; watchdog (600 s without log output) from killing the run.  The line
;; also carries the module's heap cost and load time (ROOM delta after a
;; GC), so the suite log documents the binding footprint on the target —
;; the number specs/raw-bindings-footprint.md is about.
(defun %raw-heap-used ()
  (ext:gc)
  (let ((s (with-output-to-string (*standard-output*) (room))))
    (or (parse-integer s :start (+ 8 (search "Heap:" s)) :junk-allowed t) 0)))

(defun %raw-require (name)
  (let ((before (%raw-heap-used))
        (t0 (get-internal-real-time)))
    (require name)
    (format t "; raw-bindings: ~A loaded (~D KB heap, ~D ms)~%" name
            (round (- (%raw-heap-used) before) 1024)
            (- (get-internal-real-time) t0))
    (finish-output)))

(%raw-require "amiga/intuition")          ; curated module, for a cross-check
(%raw-require "amiga/raw/exec")
(%raw-require "amiga/raw/dos")
(%raw-require "amiga/raw/utility")
(%raw-require "amiga/raw/intuition")
(%raw-require "amiga/raw/graphics")
(%raw-require "amiga/raw/layers")
(%raw-require "amiga/raw/gadtools")
(%raw-require "amiga/raw/timer")
(%raw-require "amiga/raw/devices/audio")

;;; --- exec.library -------------------------------------------------

(check "raw-exec-base-open" t
  (and amiga.raw.exec:*exec-base*
       (not (ffi:null-pointer-p amiga.raw.exec:*exec-base*))))

(check "raw-exec-version-sane" t
  (and (integerp amiga.raw.exec:*exec-version*)
       (>= amiga.raw.exec:*exec-version* 37)))

;; struct accessor on the live library base == AMIGA.FFI's reader
(check "raw-exec-library-struct-version" amiga.raw.exec:*exec-version*
  (amiga.raw.exec:library-version amiga.raw.exec:*exec-base*))

(check "raw-exec-avail-mem-positive" t
  (> (amiga.raw.exec:avail-mem amiga.raw.exec:+memf-any+) 0))

(check "raw-exec-avail-mem-largest<=total" t
  (<= (amiga.raw.exec:avail-mem (logior amiga.raw.exec:+memf-any+
                                        amiga.raw.exec:+memf-largest+))
      (amiga.raw.exec:avail-mem amiga.raw.exec:+memf-any+)))

;; pointer result: AllocMem -> foreign pointer, MEMF_CLEAR zeroes it
(check "raw-exec-alloc-free-mem" t
  (let ((m (amiga.raw.exec:alloc-mem 64 (logior amiga.raw.exec:+memf-public+
                                                amiga.raw.exec:+memf-clear+))))
    (prog1 (and m (ffi:foreign-pointer-p m) (zerop (ffi:peek-u32 m 0)))
      (when m (amiga.raw.exec:free-mem m 64)))))

;; NULL pointer result -> NIL (an absurd allocation)
(check "raw-exec-alloc-mem-failure-is-nil" nil
  (amiga.raw.exec:alloc-mem #x7FFF0000 amiga.raw.exec:+memf-public+))

;; FindTask(NULL) = our own task; node type via the generated Node accessor
(check "raw-exec-find-task-self-node-type" t
  (let ((tc (amiga.raw.exec:find-task nil)))
    (and tc
         (member (amiga.raw.exec:node-type tc)
                 (list amiga.raw.exec:+nt-task+ amiga.raw.exec:+nt-process+))
         t)))

(check "raw-exec-struct-sizes" '(14 34 32 48 34)
  (list amiga.raw.exec:*node-size* amiga.raw.exec:*msg-port-size*
        amiga.raw.exec:*io-request-size* amiga.raw.exec:*io-std-req-size*
        amiga.raw.exec:*library-size*))

(check "raw-exec-cmd-nonstd" 9 amiga.raw.exec:+cmd-nonstd+)

;;; --- utility.library: signed / unsigned / u8 results ----------------

(check "raw-utility-stricmp-negative" t
  (ffi:with-foreign-string (a "abc")
    (ffi:with-foreign-string (b "abd")
      (< (amiga.raw.utility:stricmp a b) 0))))

(check "raw-utility-stricmp-case-insensitive-equal" 0
  (ffi:with-foreign-string (a "ABC")
    (ffi:with-foreign-string (b "abc")
      (amiga.raw.utility:stricmp a b))))

(check "raw-utility-umult32" 35 (amiga.raw.utility:u-mult32 5 7))
(check "raw-utility-smult32-negative" -35 (amiga.raw.utility:s-mult32 -5 7))
(check "raw-utility-to-upper-u8" (char-code #\A)
  (amiga.raw.utility:to-upper (char-code #\a)))

;;; --- the bindings are FFI stubs: indirect calls reach the OS too -----

;; (f args) above compiled to OP_AMIGA_CALL; FUNCALL / APPLY / MAPCAR go
;; through the stub object's own dispatch (cl_ffi_stub_call) instead.
(check "raw-stub-funcall-apply-mapcar" '(35 35 (35 70))
  (list (funcall #'amiga.raw.utility:u-mult32 5 7)
        (apply 'amiga.raw.utility:u-mult32 '(5 7))
        (mapcar #'amiga.raw.utility:u-mult32 '(5 10) '(7 7))))

(check "raw-stub-descriptor" '(:libcall -138 :signed 2)   ; SMult32 is LVO -138
  (let ((info (ffi::%ffi-stub-info #'amiga.raw.utility:s-mult32)))
    (list (getf info :kind) (getf info :lvo) (getf info :result)
          (getf info :nparams))))

;; struct accessors are field stubs: reader + %SET- writer, DEFSETF-linked
(check "raw-stub-field-accessors" '(:peek :i16 8 :poke)
  (let ((get (ffi::%ffi-stub-info #'amiga.raw.intuition:window-width))
        (set (ffi::%ffi-stub-info (fdefinition 'amiga.raw.intuition::%set-window-width))))
    (list (getf get :kind) (getf get :ctype) (getf get :offset) (getf set :kind))))

;; a stub is a function for every CL predicate; arity errors come from the
;; stub itself
(check "raw-stub-is-function" '(t t t t)
  (list (functionp #'amiga.raw.utility:to-upper)
        (compiled-function-p #'amiga.raw.utility:to-upper)
        (typep #'amiga.raw.utility:to-upper 'function)
        (handler-case (progn (funcall #'amiga.raw.utility:to-upper) nil)
          (error (e) (and (search "expected 1" (format nil "~A" e)) t)))))

;;; --- the modules are demand-interned binding tables (Phase 2) ----------

;; a module's package carries its table after REQUIRE, and only the names
;; this file has touched so far exist as symbols (a few dozen of ~1500)
(check "raw-bindtab-intuition-lazy" t
  (let ((info (clamiga::%binding-table-info "AMIGA.RAW.INTUITION")))
    (and info
         (> (getf info :entries) 1400)
         (< (getf info :symbols) 200)
         (eq (getf info :base) 'amiga.raw.intuition:*intuition-base*)
         t)))

;; first reference builds the symbol: exported, constant, right value
(check "raw-bindtab-materialise-on-find-symbol" '(:external t #x8000006A)   ; WA_IDCMP = WA_Dummy + 10
  (multiple-value-bind (s status) (find-symbol "+WA-IDCMP+" "AMIGA.RAW.INTUITION")
    (list status (constantp s) (symbol-value s))))

;; the %SET- writer of a field comes with its reader (DEFSETF-linked)
(check "raw-bindtab-setter-probe" '(:internal :poke)
  (multiple-value-bind (s status) (find-symbol "%SET-WINDOW-LEFT-EDGE" "AMIGA.RAW.INTUITION")
    (list status (getf (ffi::%ffi-stub-info s) :kind))))

;; the table rows decode back to what the generator emitted
(check "raw-bindtab-rows" t
  (let ((row (find-if (lambda (r) (and (eq (first r) :fn) (string= (second r) "OPEN-WINDOW-TAG-LIST")))
                      (clamiga::%binding-table-entries "AMIGA.RAW.INTUITION"))))
    (equal (cddr row) '(-606 (:a0 :a1) :pointer))))

;;; --- dos.library: shadowed names, BPTRs, an end-to-end file round trip

(check "raw-dos-open-is-not-cl-open" t
  (and (not (eq 'amiga.raw.dos:open 'cl:open))
       (not (eq 'amiga.raw.dos:read 'cl:read))
       (string= (format nil "~A" 42) "42")))

(check "raw-dos-lock-unlock-sys" t
  (ffi:with-foreign-string (n "SYS:")
    (let ((lk (amiga.raw.dos:lock n amiga.raw.dos:+shared-lock+)))
      (prog1 (and (integerp lk) (/= lk 0))
        (when (/= lk 0) (amiga.raw.dos:un-lock lk))))))

(check "raw-dos-lock-missing-object-and-io-err" amiga.raw.dos:+error-object-not-found+
  (ffi:with-foreign-string (n "SYS:no-such-dir-zzz/nothing")
    (amiga.raw.dos:lock n amiga.raw.dos:+shared-lock+)
    (amiga.raw.dos:io-err)))

(check "raw-dos-date-stamp-struct" t
  (let ((ds (ffi:alloc-foreign amiga.raw.dos:*date-stamp-size*)))
    (unwind-protect
         (progn (amiga.raw.dos:date-stamp ds)
                (> (amiga.raw.dos:date-stamp-days ds) 0))
      (ffi:free-foreign ds))))

(check "raw-dos-file-roundtrip" "raw ok"
  (ffi:with-foreign-string (name "T:raw-bindings-test")
    (ffi:with-foreign-string (payload "raw ok")
      (let ((fh (amiga.raw.dos:open name amiga.raw.dos:+mode-newfile+)))
        (unless (and (integerp fh) (/= fh 0)) (error "Open(MODE_NEWFILE) failed"))
        (let ((written (amiga.raw.dos:write fh payload 6)))
          (amiga.raw.dos:close fh)
          (unless (= written 6) (error "Write wrote ~D" written)))
        (let ((fh2 (amiga.raw.dos:open name amiga.raw.dos:+mode-oldfile+))
              (buf (ffi:alloc-foreign 32)))
          (unwind-protect
               (let ((n (amiga.raw.dos:read fh2 buf 32)))
                 (amiga.raw.dos:close fh2)
                 (amiga.raw.dos:delete-file name)
                 (ffi:foreign-to-string buf n))
            (ffi:free-foreign buf)))))))

;;; --- intuition.library: pointers, struct accessors, a real window ----

(check "raw-intuition-base-and-version" t
  (and amiga.raw.intuition:*intuition-base*
       (>= amiga.raw.intuition:*intuition-version* 37)))

;; the hand-written module's constant must agree with the NDK (it used to
;; say 4 = WFLG_DEPTHGADGET)
(check "raw-intuition-wflg-reportmouse-matches-curated"
       amiga.raw.intuition:+wflg-reportmouse+
  amiga.intuition:+wflg-reportmouse+)

(check "raw-intuition-constants" '(#x200 #x80000064 #x80000066)
  (list amiga.raw.intuition:+idcmp-closewindow+
        amiga.raw.intuition:+wa-left+ amiga.raw.intuition:+wa-width+))

(check "raw-intuition-lock-pub-screen" t
  (let ((scr (amiga.raw.intuition:lock-pub-screen nil)))
    (unwind-protect
         (and scr
              (> (amiga.raw.intuition:screen-width scr) 0)
              (> (amiga.raw.intuition:screen-height scr) 0)
              (ffi:foreign-pointer-p (amiga.raw.intuition:screen-rastport scr)))
      (when scr (amiga.raw.intuition:unlock-pub-screen nil scr)))))

(defvar *raw-test-window* nil)

(check "raw-intuition-open-window-tag-list" t
  (amiga.ffi:with-tag-list (tags amiga.raw.intuition:+wa-left+ 20
                                 amiga.raw.intuition:+wa-top+ 20
                                 amiga.raw.intuition:+wa-width+ 160
                                 amiga.raw.intuition:+wa-height+ 80
                                 amiga.raw.intuition:+wa-flags+
                                 (logior amiga.raw.intuition:+wflg-dragbar+
                                         amiga.raw.intuition:+wflg-depthgadget+)
                                 amiga.raw.intuition:+wa-idcmp+
                                 amiga.raw.intuition:+idcmp-closewindow+)
    (let ((win (amiga.raw.intuition:open-window-tag-list nil tags)))
      (setq *raw-test-window* win)
      (and win
           (= (amiga.raw.intuition:window-width win) 160)
           (= (amiga.raw.intuition:window-height win) 80)
           (ffi:foreign-pointer-p (amiga.raw.intuition:window-rport win))
           (ffi:foreign-pointer-p (amiga.raw.intuition:window-user-port win))
           (ffi:foreign-pointer-p (amiga.raw.intuition:window-w-screen win))
           t))))

;;; --- graphics.library on the window's RastPort ---------------------

(check "raw-graphics-pixel-roundtrip" t
  (let* ((win *raw-test-window*)
         (rp (amiga.raw.intuition:window-rport win))
         (scr (amiga.raw.intuition:window-w-screen win))
         ;; embedded struct accessor (sc_BitMap) feeding GetBitMapAttr
         (depth (amiga.raw.graphics:get-bitmap-attr
                 (amiga.raw.intuition:screen-bitmap scr)
                 amiga.raw.graphics:+bma-depth+))
         (x (+ (amiga.raw.intuition:window-border-left win) 10))
         (y (+ (amiga.raw.intuition:window-border-top win) 10)))
    (amiga.raw.graphics:set-a-pen rp 1)                 ; :void
    (amiga.raw.graphics:write-pixel rp x y)             ; :signed (0 on success)
    (let ((px (amiga.raw.graphics:read-pixel rp x y)))  ; :unsigned
      ;; A planar screen reads the pen back verbatim.  On a deep RTG/CGX
      ;; screen (MorphOS Workbench) ReadPixel maps the colour to a pen
      ;; number, which need not be 1 — any pen is fine there.
      (if (<= depth 8)
          (= px 1)
          (and (integerp px) (>= px 0))))))

(check "raw-graphics-write-pixel-offscreen-is-minus-one" -1
  (amiga.raw.graphics:write-pixel (amiga.raw.intuition:window-rport *raw-test-window*)
                                  -5000 -5000))

(check "raw-graphics-text-length-positive" t
  (ffi:with-foreign-string (s "Hello")
    (> (amiga.raw.graphics:text-length
        (amiga.raw.intuition:window-rport *raw-test-window*) s 5)
       0)))

(check "raw-graphics-rastport-struct" t
  (let ((rp (amiga.raw.intuition:window-rport *raw-test-window*)))
    (and (= amiga.raw.graphics:*rastport-size* 100)
         ;; rastport.i declares rp_BitMap as LONG, so the accessor is :i32
         (/= 0 (amiga.raw.graphics:rastport-bitmap rp))
         (= (amiga.raw.graphics:rastport-fg-pen rp) 1))))   ; set by SetAPen above

;; CALL-LIBRARY plist path: ClipBlit has 9 register args — copy a block of
;; the window onto itself and make sure nothing explodes.
(check "raw-graphics-clip-blit-9-args" t
  (let ((rp (amiga.raw.intuition:window-rport *raw-test-window*)))
    (amiga.raw.graphics:clip-blit rp 10 10 rp 30 30 8 8 #xC0)
    t))

;;; --- layers / gadtools: more libraries through the same pipeline ----

(check "raw-layers-which-layer-of-window" t
  (let* ((win *raw-test-window*)
         (li (amiga.raw.intuition:screen-layer-info (amiga.raw.intuition:window-w-screen win)))
         (x (+ (amiga.raw.intuition:window-left-edge win) 30))
         (y (+ (amiga.raw.intuition:window-top-edge win) 30)))
    (and (ffi:foreign-pointer-p (amiga.raw.layers:which-layer li x y)) t)))

(check "raw-gadtools-visual-info" t
  (let ((scr (amiga.raw.intuition:lock-pub-screen nil)))
    (unwind-protect
         (let ((vi (amiga.raw.gadtools:get-visual-info-a scr nil)))
           (prog1 (and vi t)
             (when vi (amiga.raw.gadtools:free-visual-info vi))))
      (when scr (amiga.raw.intuition:unlock-pub-screen nil scr)))))

(check "raw-intuition-close-window" t
  (progn (amiga.raw.intuition:close-window *raw-test-window*)
         (setq *raw-test-window* nil)
         t))

;;; --- guards as seen by the running system ---------------------------

;; ShowWindow: OS 3.2 (V46) addition, a private vector on MorphOS —
;; bound only on AmigaOS with intuition >= 46.
(check "raw-intuition-show-window-gate" t
  (eq (if (and (>= amiga.raw.intuition:*intuition-version* 46)
               (not (member :morphos *features*)))
          t nil)
      (if (fboundp 'amiga.raw.intuition::show-window) t nil)))

;; MorphOS-only exec extensions exist exactly on MorphOS
(check "raw-exec-morphos-only-gate" t
  (eq (if (member :morphos *features*) t nil)
      (if (fboundp 'amiga.raw.exec::alloc-vec-pooled) t nil)))

;; 3.2-only dos additions that collide with MorphOS vectors: never on MorphOS
#+morphos
(check "raw-dos-error-output-absent-on-morphos" nil
  (fboundp 'amiga.raw.dos::error-output))

;;; --- device / header-only modules -----------------------------------

(check "raw-timer-device-base-nil" nil amiga.raw.timer:*timer-base*)
(check "raw-timer-device-functions-defined" t
  (and (fboundp 'amiga.raw.timer:add-time) (fboundp 'amiga.raw.timer:cmp-time) t))
(check "raw-devices-audio-ioaudio-size" 68 amiga.raw.devices.audio:*io-audio-size*)
(check "raw-devices-audio-adcmd-allocate" 32 amiga.raw.devices.audio:+adcmd-allocate+)

;;; --- ReAction class libraries: gadgets/ images/ classes/ modules --------
;;; Their module opens the class at REQUIRE time, so they are loaded only
;;; where the classes exist (OS 3.5+/3.2, MorphOS — and the FS-UAE test
;;; setup, whose Workbench is OS 3.9 on the 3.1 ROM); on a bare 3.1 the
;;; checks assert that nothing was loaded.  The symbols are looked up at
;;; run time: the packages do not exist at read time on such a system.

(defvar *raw-reaction-p*
  (let ((b (amiga:open-library "gadgets/button.gadget" 0)))
    (when b (amiga:close-library b) t)))

(format t "; raw-bindings: ReAction classes ~:[absent — skipping~;present~]~%" *raw-reaction-p*)
(when *raw-reaction-p*
  (%raw-require "amiga/raw/gadgets/button")
  (%raw-require "amiga/raw/classes/window"))

(defun %raw-sym (pkg name)
  (or (find-symbol name pkg) (error "~A::~A not found" pkg name)))

;; BUTTON_GetClass() -> Class *, a foreign pointer
(check "raw-reaction-button-get-class" t
  (if *raw-reaction-p*
      (let ((cls (funcall (%raw-sym "AMIGA.RAW.GADGETS.BUTTON" "BUTTON-GET-CLASS"))))
        (and cls (ffi:foreign-pointer-p cls) (not (ffi:null-pointer-p cls)) t))
      (null (find-package "AMIGA.RAW.GADGETS.BUTTON"))))

;; the tags come from gadgets/button.h (no .i in the NDK): a button object
;; built with them through intuition's NewObjectA, then disposed
(check "raw-reaction-button-object-from-header-tags" t
  (if *raw-reaction-p*
      (let ((cls (funcall (%raw-sym "AMIGA.RAW.GADGETS.BUTTON" "BUTTON-GET-CLASS")))
            (ga-text (symbol-value (%raw-sym "AMIGA.RAW.INTUITION" "+GA-TEXT+")))
            (ga-id (symbol-value (%raw-sym "AMIGA.RAW.INTUITION" "+GA-ID+")))
            (justify (symbol-value (%raw-sym "AMIGA.RAW.GADGETS.BUTTON" "+BUTTON-JUSTIFICATION+")))
            (bcj-center (symbol-value (%raw-sym "AMIGA.RAW.GADGETS.BUTTON" "+BCJ-CENTER+"))))
        (and (= justify #x84000010)
             (ffi:with-foreign-string (label "OK")
               (amiga.ffi:with-tag-list (tags ga-text label ga-id 1 justify bcj-center)
                 (let ((obj (amiga.raw.intuition:new-object-a cls nil tags)))
                   (prog1 (and obj (ffi:foreign-pointer-p obj) t)
                     (when obj (amiga.raw.intuition:dispose-object obj))))))))
      (null (find-package "AMIGA.RAW.GADGETS.BUTTON"))))

;; window.class: opened by its bare name, WMHI_* from classes/window.h
(check "raw-reaction-window-class" t
  (if *raw-reaction-p*
      (let ((cls (funcall (%raw-sym "AMIGA.RAW.CLASSES.WINDOW" "WINDOW-GET-CLASS"))))
        (and cls (ffi:foreign-pointer-p cls)
             (= (symbol-value (%raw-sym "AMIGA.RAW.CLASSES.WINDOW" "+WMHI-CLOSEWINDOW+")) #x10000)
             (= (symbol-value (%raw-sym "AMIGA.RAW.CLASSES.WINDOW" "+WMHI-IGNORE+")) -1)
             t))
      (null (find-package "AMIGA.RAW.CLASSES.WINDOW"))))
