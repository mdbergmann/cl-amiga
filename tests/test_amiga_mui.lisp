;;; test_amiga_mui.lisp — host-side checks of lib/amiga/mui.lisp
;;; (AMIGA.MUI), driven by tests/test_amiga_mui.sh.
;;;
;;; The host has no MUI (no AmigaOS at all): what can be checked here is
;;; the portable surface -- AVAILABLE-P, CLASS-ID (against every MUIC_
;;; string of the generated raw module, which loads on the host with a
;;; NIL library base), MAKE-ID, the mui.h size macros, the MUIM_Notify
;;; message and MUI_MakeObject parameter packing (peeked before any call),
;;; POOL-STRING-ARRAY, the diagnostics of every entry point -- that the
;;; toolkit-neutral half is the very AMIGA.BOOPSI implementation
;;; re-exported rather than a copy (its own checks are
;;; tests/test_amiga_boopsi.lisp), and that every example under
;;; examples/amiga/mui/ loads (all of its code compiles) and bows out with
;;; its "not available" line.  tests/amiga/test-mui.lisp adds the object
;;; half on the Amiga; the hand-typed constants are pinned to the raw
;;; table by tests/test_amiga_curated_vs_raw.sh.

(defvar *pass* 0)
(defvar *fail* 0)

(defmacro check (name expected form)
  `(handler-case
       (let ((e ,expected) (a ,form))
         (if (equal e a)
             (progn (incf *pass*) (format t "PASS: ~A~%" ,name))
             (progn (incf *fail*) (format t "FAIL: ~A - expected ~S got ~S~%" ,name e a))))
     (error (c)
       (incf *fail*)
       (format t "FAIL: ~A - signaled error: ~A~%" ,name c))))

(defun error-message (thunk)
  "The message of the error THUNK signals, or :NO-ERROR."
  (handler-case (progn (funcall thunk) :no-error)
    (error (e) (format nil "~A" e))))

(defun message-mentions (thunk &rest needles)
  "True when THUNK signals an error whose message contains every needle."
  (let ((m (error-message thunk)))
    (and (stringp m)
         (every (lambda (n) (search n m)) needles)
         t)))

;;; --- layering ------------------------------------------------------------

(check "mui-not-loaded-yet" nil (find-package "AMIGA.MUI"))

(require "amiga/mui")

(check "host-mui-not-available" nil (amiga.mui:available-p))

(check "mui-exports" '("*EVENT-LOOP-TIMEOUT*" "ADD-MIN-MAX" "APPLICATION-INPUT"
                       "AREA-BOTTOM" "AREA-DRAW-INFO" "AREA-FLAGS" "AREA-FONT" "AREA-HEIGHT"
                       "AREA-LEFT" "AREA-MBOTTOM" "AREA-MHEIGHT" "AREA-MIN-HEIGHT"
                       "AREA-MIN-WIDTH" "AREA-MLEFT" "AREA-MRIGHT"
                       "AREA-MTOP" "AREA-MWIDTH" "AREA-PENS" "AREA-RASTPORT" "AREA-RENDER-INFO"
                       "AREA-RIGHT" "AREA-SCREEN" "AREA-TOP" "AREA-WIDTH" "AREA-WINDOW"
                       "AVAILABLE-P" "CLASS-ID" "CREATE-CUSTOM-CLASS" "CUSTOM-CLASS-CLASS"
                       "DISPOSE-OBJECT" "DO-APPLICATION-EVENTS" "DO-METHOD" "DO-SUPER-METHOD"
                       "DRAW-FLAGS" "FREE-STRING-KEY-HOOK" "GET-ATTR" "GET-ATTR-POINTER"
                       "GET-ATTR-STRING" "INST-DATA"
                       "LAYOUT-CHILD" "LAYOUT-CHILDREN" "LAYOUT-MSG-HEIGHT"
                       "LAYOUT-MSG-MIN-MAX" "LAYOUT-MSG-TYPE" "LAYOUT-MSG-WIDTH"
                       "MAKE-ID" "MAKE-OBJECT" "MAKE-STRING-KEY-HOOK" "METHOD-ID" "MIN-MAX-INFO"
                       "NEW-OBJECT" "NOTIFY" "OBJECT-CLASS" "POOL-ALLOC" "POOL-FINALIZER"
                       "POOL-HOOK" "POOL-STRING"
                       "POOL-STRING-ARRAY" "REJECT-IDCMP" "REQUEST" "REQUEST-IDCMP"
                       "RETURN-ID" "SET-ATTRS" "SET-MIN-MAX"
                       "STRING-KEY-HOOK-ENTRY" "STRING-KEY-HOOK-STATS"
                       "WINDOW-EDGE-DELTA" "WINDOW-SIZE-MINMAX" "WINDOW-SIZE-SCREEN"
                       "WINDOW-SIZE-VISIBLE" "WITH-FOREIGN-POOL" "WITH-TAGS")
  (let ((names '()))
    (do-external-symbols (s "AMIGA.MUI") (push (symbol-name s) names))
    (sort names #'string<)))

;; the toolkit-neutral half is AMIGA.BOOPSI's, re-exported: the same
;; symbols (one implementation), and the %-helpers this module uses itself
;; are imported, not copied
(check "mui-shares-boopsi-symbols" t
  (let ((ok t))
    (dolist (name '("DO-METHOD" "OBJECT-CLASS" "WITH-FOREIGN-POOL" "POOL-ALLOC"
                    "POOL-STRING" "WITH-TAGS" "GET-ATTR" "GET-ATTR-POINTER" "SET-ATTRS"
                    "%ULONG" "%WITH-TAGS"))
      (unless (eq (find-symbol name "AMIGA.MUI") (find-symbol name "AMIGA.BOOPSI"))
        (format t "  ~A differs~%" name)
        (setf ok nil)))
    ok))

;; ... and the exec label-list helpers, which mean nothing to MUI, are
;; inherited but not re-exported
(check "mui-does-not-reexport-exec-lists" '(:inherited :inherited)
  (list (nth-value 1 (find-symbol "NEW-LIST" "AMIGA.MUI"))
        (nth-value 1 (find-symbol "FREE-LIST-NODES" "AMIGA.MUI"))))

(check "mui-pool-is-boopsi-pool" "shared"
  (amiga.mui:with-foreign-pool ()
    (ffi:foreign-to-string (amiga.boopsi:pool-string "shared"))))

(check "mui-event-loop-timeout-default-nil" nil amiga.mui:*event-loop-timeout*)

(check "mui-muimaster-base-stays-nil-on-host" nil amiga.mui::*muimaster-base*)

;;; --- class designators ---------------------------------------------------

(check "mui-class-id-keywords" '("Window.mui" "Application.mui" "Numericbutton.mui"
                                 "Numericbutton.mui" "Scrollgroup.mui" "Dtpic.mui")
  (mapcar #'amiga.mui:class-id
          '(:window :application :numeric-button :numericbutton :scroll-group :dtpic)))

(check "mui-class-id-string-passes-through" "Window.mui" (amiga.mui:class-id "Window.mui"))

(check "mui-class-id-rejects-non-designators" '(t t t)
  (list (message-mentions (lambda () (amiga.mui:class-id 42)) "CLASS-ID" "class designator")
        (message-mentions (lambda () (amiga.mui:class-id 'window)) "class designator")
        (message-mentions (lambda () (amiga.mui:class-id :|a b|)) ":WINDOW")))

;; every MUIC_ name of mui.h is reached by the keyword rule: the raw
;; module's +MUIC-FOO+ = "Foo.mui" must be (class-id :foo) -- so the
;; keyword spelling is proven against the generated table, without a
;; class table of its own to keep in sync
(require "amiga/raw/muimaster")

(check "mui-class-id-covers-every-muic-constant" t
  (let ((n 0) (ok t))
    (do-external-symbols (s "AMIGA.RAW.MUIMASTER")
      (let ((name (symbol-name s)))
        (when (and (> (length name) 7) (string= "+MUIC-" name :end2 6)
                   (constantp s) (boundp s) (stringp (symbol-value s)))
          (incf n)
          (let* ((kw (intern (subseq name 6 (1- (length name))) "KEYWORD"))
                 (got (amiga.mui:class-id kw)))
            (unless (string= got (symbol-value s))
              (format t "  ~A: (class-id ~S) = ~S, raw says ~S~%" name kw got (symbol-value s))
              (setf ok nil))))))
    (format t "  ~D MUIC_ constants checked~%" n)
    (and ok (>= n 60))))

;; the hand-typed constants are the generated ones (test_amiga_curated_vs_raw
;; pins every one; the two the loop depends on are asserted here as well)
(check "mui-loop-constants-match-raw" t
  (and (= amiga.mui::+muim-application-new-input+
          amiga.raw.muimaster:+muim-application-new-input+)
       (= amiga.mui::+muiv-application-return-id-quit+
          amiga.raw.muimaster:+muiv-application-return-id-quit+)
       (= amiga.mui::+muim-notify+ amiga.raw.muimaster:+muim-notify+)
       (= amiga.mui::+muio-numeric-button+ amiga.raw.muimaster:+muio-numeric-button+)))

;;; --- the mui.h helper macros ---------------------------------------------

(check "mui-make-id" #x4D41494E (amiga.mui:make-id "MAIN"))

(check "mui-make-id-rejects-bad-ids" '(t t t)
  (list (message-mentions (lambda () (amiga.mui:make-id "MAI")) "MAKE-ID" "four-character")
        (message-mentions (lambda () (amiga.mui:make-id 42)) "four-character")
        (message-mentions (lambda () (amiga.mui:make-id "MAINX")) "four-character")))

;; MUIV_Window_Width_MinMax(p) = -p, _Visible(p) = -100-p, _Screen(p) =
;; -200-p, MUIV_Window_TopEdge_Delta(p) = -3-p
(check "mui-window-size-macros" '(-50 -150 -250 -7 0 -100 -200 -3)
  (list (amiga.mui:window-size-minmax 50) (amiga.mui:window-size-visible 50)
        (amiga.mui:window-size-screen 50) (amiga.mui:window-edge-delta 4)
        (amiga.mui:window-size-minmax 0) (amiga.mui:window-size-visible 0)
        (amiga.mui:window-size-screen 0) (amiga.mui:window-edge-delta 0)))

;; ... and as tag values they wrap to the longword MUI reads
(check "mui-window-size-as-tag-value" #xFFFFFF06
  (amiga.mui:with-tags (tags 1 (amiga.mui:window-size-screen 50))
    (ffi:peek-u32 tags 4)))

;;; --- MUIM_Notify packing -------------------------------------------------

;; MUIP_Notify after MethodID: TrigAttr, TrigVal, DestObj, FollowParams,
;; then the method and its parameters; FollowParams counts the method
(check "mui-notify-args-every-time-application" '(#x80420001 #x49893131 3 3 #x80420002 1 #x49893131)
  (amiga.mui::%notify-args #x80420001 :every-time :application #x80420002 '(1 :trigger-value)))

(check "mui-notify-args-t-nil-triggers-and-dests" '((1 1) (0 2) (7 4))
  (list (subseq (amiga.mui::%notify-args 1 t :self 2 '()) 1 3)
        (subseq (amiga.mui::%notify-args 1 nil :window 2 '()) 1 3)
        (subseq (amiga.mui::%notify-args 1 7 :parent 2 '()) 1 3)))

(check "mui-notify-args-follow-params-and-not-trigger" '(1 4 #x49893133)
  (let ((args (amiga.mui::%notify-args 1 :every-time :self 9 '(:not-trigger-value 5 6))))
    (list (nth 3 (amiga.mui::%notify-args 1 t :self 9 '()))
          (nth 3 args)
          (nth 5 args))))

;; MUI substitutes MUIV_TriggerValue / MUIV_NotTriggerValue only under a
;; MUIV_EveryTime trigger (Notify.mui).  Asking for one under a fixed trigger
;; used to pack the raw 0x49893131 into the message and hand it to the method
;; -- a bignum, above MOST-POSITIVE-FIXNUM, once it reached Lisp.
(check "mui-notify-trigger-value-needs-every-time" '(t t t t)
  (list (message-mentions (lambda () (amiga.mui::%notify-args 1 t :application 2
                                                              '(:trigger-value)))
                          ":TRIGGER-VALUE" ":EVERY-TIME")
        (message-mentions (lambda () (amiga.mui::%notify-args 1 5 :application 2
                                                              '(7 :not-trigger-value)))
                          ":NOT-TRIGGER-VALUE" ":EVERY-TIME")
        ;; ... and stays a plain parameter list when the trigger is right
        (equal '(#x49893131 #x49893133)
               (subseq (amiga.mui::%notify-args 1 :every-time :self 2
                                                '(:trigger-value :not-trigger-value))
                       5))
        ;; a fixed trigger with ordinary parameters is untouched
        (equal '(1 1 1 2 2 7)
               (amiga.mui::%notify-args 1 t :self 2 '(7)))))

;; a destination object goes in as it is (DO-METHOD turns it into its address)
(check "mui-notify-args-object-destination" t
  (let ((p (ffi:alloc-foreign 4)))
    (prog1 (eq p (nth 2 (amiga.mui::%notify-args 1 t p 2 '())))
      (ffi:free-foreign p))))

;; a string parameter is pooled: the notification keeps the pointer
(check "mui-notify-args-string-param-is-pooled" '(t t)
  (list (amiga.mui:with-foreign-pool ()
          (let ((args (amiga.mui::%notify-args 1 t :self 2 '("text"))))
            (and (integerp (nth 5 args)) (plusp (nth 5 args))
                 (= 1 (length (car amiga.boopsi::*foreign-pool*))))))
        (message-mentions (lambda () (amiga.mui::%notify-args 1 t :self 2 '("text")))
                          "WITH-FOREIGN-POOL")))

(check "mui-notify-diagnostics" '(t t t t t t)
  (list (message-mentions (lambda () (amiga.mui::%notify-args 1 t :nowhere 2 '()))
                          "NOTIFY" ":NOWHERE" ":APPLICATION")
        (message-mentions (lambda () (amiga.mui::%notify-args 1 t nil 2 '()))
                          "NOTIFY" "destination")
        (message-mentions (lambda () (amiga.mui::%notify-args 1 t :self "MUIM_Set" '()))
                          "method" "MUIM_")
        (message-mentions (lambda () (amiga.mui::%notify-args nil t :self 2 '()))
                          "attribute" "MUIA_")
        (message-mentions (lambda () (amiga.mui::%notify-args 1 :always :self 2 '()))
                          ":ALWAYS" ":EVERY-TIME")
        (message-mentions (lambda () (amiga.mui::%notify-args 1 t :self 2 '(:quit)))
                          ":QUIT" ":TRIGGER-VALUE")))

;; NOTIFY itself validates before it touches the object: no OS needed for
;; the argument errors
(check "mui-notify-validates-before-calling" t
  (message-mentions (lambda () (amiga.mui:notify nil 1 t :elsewhere 2)) ":ELSEWHERE"))

;;; --- MUI_MakeObject types and parameters ---------------------------------

(check "mui-make-object-types" '(2 16 7 9)
  (mapcar #'amiga.mui::%make-object-type '(:button :numeric-button 7 :h-space)))

(check "mui-make-object-type-table-complete" 16
  (length (remove-duplicates (mapcar #'cdr amiga.mui::*make-object-types*))))

(check "mui-make-object-rejects-unknown-type" t
  (message-mentions (lambda () (amiga.mui:make-object :foo)) "MAKE-OBJECT" ":FOO" ":BUTTON"))

;; the ULONG[] MUI_MakeObjectA reads: integers, T/NIL, pooled strings
;; (the object keeps the label), a list of strings as a pooled STRPTR[]
;; -- on the host the foreign addresses are 64-bit and the array holds
;; longwords, so only their presence is checked; the Amiga test reads
;; the strings back
(check "mui-ulong-array-pooled" '(7 t 1 0 t 2 3)
  (amiga.mui:with-foreign-pool ()
    (amiga.mui::%with-ulong-array (a '(7 "label" t nil ("a" "b")) :pooled t)
      (list (ffi:peek-u32 a 0)
            (not (zerop (ffi:peek-u32 a 4)))
            (ffi:peek-u32 a 8) (ffi:peek-u32 a 12)
            (not (zerop (ffi:peek-u32 a 16)))
            ;; the pool now holds: "label", "a", "b", the STRPTR[] -- and
            ;; nothing else; the ULONG[] itself is temporary
            (count-if (lambda (p) (declare (ignore p)) t)
                      (car amiga.boopsi::*foreign-pool*) :end 2)
            (count-if (lambda (p) (declare (ignore p)) t)
                      (car amiga.boopsi::*foreign-pool*) :start 1)))))

;; REQUEST's parameters are temporaries: no pool needed, freed after
(check "mui-ulong-array-temporaries" '(1 t t)
  (multiple-value-bind (a temps) (amiga.mui::%build-ulong-array '("text" 5) nil)
    (prog1 (list (length temps)
                 (not (zerop (ffi:peek-u32 a 0)))
                 (= 5 (ffi:peek-u32 a 4)))
      (ffi:free-foreign a)
      (dolist (s temps) (ffi:free-foreign s)))))

(check "mui-ulong-array-list-only-for-make-object" t
  (message-mentions (lambda () (amiga.mui::%build-ulong-array '(("a" "b")) nil))
                    "MAKE-OBJECT" "REQUEST"))

(check "mui-ulong-array-empty-is-still-an-array" t
  (multiple-value-bind (a temps) (amiga.mui::%build-ulong-array '() nil)
    (prog1 (and (ffi:foreign-pointer-p a) (null temps))
      (ffi:free-foreign a))))

;;; --- POOL-STRING-ARRAY ---------------------------------------------------

(check "mui-pool-string-array-layout" '(t t 0 3)
  (amiga.mui:with-foreign-pool ()
    (let ((a (amiga.mui:pool-string-array '("Fast" "Slow"))))
      (list (not (zerop (ffi:peek-u32 a 0)))
            (not (zerop (ffi:peek-u32 a 4)))
            (ffi:peek-u32 a 8)
            (length (car amiga.boopsi::*foreign-pool*))))))

(check "mui-pool-string-array-needs-pool" t
  (message-mentions (lambda () (amiga.mui:pool-string-array '("x"))) "WITH-FOREIGN-POOL"))

(check "mui-pool-string-array-rejects-non-strings" t
  (amiga.mui:with-foreign-pool ()
    (message-mentions (lambda () (amiga.mui:pool-string-array '("x" 3))) "POOL-STRING-ARRAY" "3")))

;;; --- entry points without MUI: clear errors, no crash ----------------------

(check "mui-new-object-without-mui" t
  (message-mentions (lambda () (amiga.mui:new-object :window 1 2))
                    "NEW-OBJECT" "muimaster.library" "AVAILABLE-P"))

;; the class designator is validated first, so a wrong one is reported
;; as such even where MUI is missing
(check "mui-new-object-bad-class-first" t
  (message-mentions (lambda () (amiga.mui:new-object 42 1 2)) "class designator"))

(check "mui-make-object-without-mui" t
  (message-mentions (lambda () (amiga.mui:make-object :button "OK"))
                    "MAKE-OBJECT" "muimaster.library"))

(check "mui-request-without-mui" t
  (message-mentions (lambda () (amiga.mui:request nil nil "T" "_OK" "hi"))
                    "REQUEST" "muimaster.library"))

(check "mui-dispose-object-nil-is-fine-anywhere" nil (amiga.mui:dispose-object nil))

(check "mui-do-application-events-expands" :done
  (progn (macroexpand-1 '(amiga.mui:do-application-events ((id) app :timeout 2) (print id) (return)))
         :done))

;;; --- custom classes: the portable half -------------------------------------
;;; The class functions need MUI; what the host can check is their
;;; validation, the pool requirement, and the struct arithmetic of the
;;; mui.h shortcuts (offsets into a fake object).

(check "mui-create-custom-class-validates-before-mui" '(t t t t)
  (flet ((msg-has (needle thunk)
           (handler-case (progn (funcall thunk) nil)
             (error (e) (and (search needle (format nil "~A" e)) t)))))
    (list (msg-has "not a function"
                   (lambda () (amiga.mui:create-custom-class :area 5)))
          (msg-has "DATA-SIZE"
                   (lambda () (amiga.mui:create-custom-class :area (lambda (c o m) c o m 0) :data-size -1)))
          (msg-has "CLASS-ID"
                   (lambda () (amiga.mui:create-custom-class 42 (lambda (c o m) c o m 0))))
          (msg-has "WITH-FOREIGN-POOL"
                   (lambda () (amiga.mui:create-custom-class :area (lambda (c o m) c o m 0)))))))

(check "mui-create-custom-class-without-mui" t
  (handler-case (progn (amiga.mui:with-foreign-pool ()
                         (amiga.mui:create-custom-class :area (lambda (c o m) c o m 0)))
                       nil)
    (error (e) (and (search "muimaster.library" (format nil "~A" e)) t))))

;; POOL-HOOK is AMIGA.BOOPSI's (its own checks are in test_amiga_boopsi.lisp)
(check "mui-pool-hook-is-boopsi-pool-hook" t
  (eq (find-symbol "POOL-HOOK" "AMIGA.MUI") (find-symbol "POOL-HOOK" "AMIGA.BOOPSI")))

;; the _left/_top/_width/_height, _addleft.., _mleft.. shortcuts over a
;; fake MUI_AreaData: mad_Box at +52 (WORDs), the four BYTE margins at +60
(check "mui-area-shortcuts-arithmetic" '(100 50 200 20 299 69 103 52 194 16 296 67)
  (let ((obj (ffi:alloc-foreign 80)))
    (ffi:poke-i16 obj 100 52) (ffi:poke-i16 obj 50 54)
    (ffi:poke-i16 obj 200 56) (ffi:poke-i16 obj 20 58)
    (ffi:poke-i8 obj 3 60) (ffi:poke-i8 obj 2 61) (ffi:poke-i8 obj 6 62) (ffi:poke-i8 obj 4 63)
    (prog1 (list (amiga.mui:area-left obj) (amiga.mui:area-top obj)
                 (amiga.mui:area-width obj) (amiga.mui:area-height obj)
                 (amiga.mui:area-right obj) (amiga.mui:area-bottom obj)
                 (amiga.mui:area-mleft obj) (amiga.mui:area-mtop obj)
                 (amiga.mui:area-mwidth obj) (amiga.mui:area-mheight obj)
                 (amiga.mui:area-mright obj) (amiga.mui:area-mbottom obj))
      (ffi:free-foreign obj))))

;; a method message: MethodID first; MUIP_Draw.flags second
(check "mui-method-id-and-draw-flags" '(#x80426F3F 3)
  (let ((msg (ffi:alloc-foreign 8)))
    (ffi:poke-u32 msg #x80426F3F 0) (ffi:poke-u32 msg 3 4)
    (prog1 (list (amiga.mui:method-id msg) (amiga.mui:draw-flags msg))
      (ffi:free-foreign msg))))

;; _minwidth(obj) / _minheight(obj): mad_MinMax at +28 (MUI_NotifyData)
;; +12 (mad_RenderInfo, priv7, mad_Font), its first two WORDs
(check "mui-area-min-size-shortcuts" '(133 47)
  (let ((obj (ffi:alloc-foreign 80)))
    (ffi:poke-i16 obj 133 40) (ffi:poke-i16 obj 47 42)
    (prog1 (list (amiga.mui:area-min-width obj) (amiga.mui:area-min-height obj))
      (ffi:free-foreign obj))))

;;; --- custom layout hooks: the struct MUI_LayoutMsg -------------------------
;;; MUI_Layout itself needs MUI; the message a layout hook is handed is
;;; plain memory, so the accessors and SET-MIN-MAX are checked here on a
;;; hand-built one (lm_Type 0, lm_Children 4, lm_MinMax 8, Width 20,
;;; Height 24 -- 36 bytes in all).

(check "mui-layout-msg-accessors" '(2 640 480 100 200)
  (let ((msg (ffi:alloc-foreign 36)))
    (ffi:poke-u32 msg 2 0)                      ; MUILM_LAYOUT
    (ffi:poke-i32 msg 640 20) (ffi:poke-i32 msg 480 24)
    (prog1 (list (amiga.mui:layout-msg-type msg)
                 (amiga.mui:layout-msg-width msg)
                 (amiga.mui:layout-msg-height msg)
                 ;; SETF-able: a virtual group writes back what it needs
                 (setf (amiga.mui:layout-msg-width msg) 100)
                 (setf (amiga.mui:layout-msg-height msg) 200))
      (ffi:free-foreign msg))))

;; lm_MinMax is embedded in the message, not a pointer to one as in
;; MUIP_AskMinMax: LAYOUT-MSG-MIN-MAX is the message plus 8
(check "mui-layout-msg-min-max-is-embedded" t
  (let ((msg (ffi:alloc-foreign 36)))
    (prog1 (= (ffi:foreign-pointer-address (amiga.mui:layout-msg-min-max msg))
              (+ (ffi:foreign-pointer-address msg) 8))
      (ffi:free-foreign msg))))

;; SET-MIN-MAX writes the six WORDs (MinW, MinH, MaxW, MaxH, DefW, DefH);
;; NIL leaves a field alone
(check "mui-set-min-max" '(20 10 10000 10000 40 -1)
  (let ((msg (ffi:alloc-foreign 36)))
    (ffi:poke-i16 msg -1 18)                    ; DefHeight: left alone below
    (amiga.mui:set-min-max (amiga.mui:layout-msg-min-max msg)
                           :min-width 20 :min-height 10
                           :max-width 10000 :max-height 10000
                           :def-width 40 :def-height nil)
    (prog1 (loop for offset from 8 below 20 by 2 collect (ffi:peek-i16 msg offset))
      (ffi:free-foreign msg))))

(check "mui-set-min-max-rejects-non-integers" t
  (let ((msg (ffi:alloc-foreign 36)))
    (prog1 (message-mentions
            (lambda () (amiga.mui:set-min-max (amiga.mui:layout-msg-min-max msg)
                                              :min-width "wide"))
            "SET-MIN-MAX" "not an integer" "MUI_MAXMAX")
      (ffi:free-foreign msg))))

;; an empty lm_Children (a NULL MinList) is no children, not a crash
(check "mui-layout-children-of-null-list" nil
  (let ((msg (ffi:alloc-foreign 36)))
    (prog1 (amiga.mui:layout-children msg)
      (ffi:free-foreign msg))))

;; the three entry points that need the library report it, not a NULL call
(check "mui-layout-entry-points-without-mui" '(t t t)
  (list (message-mentions (lambda () (amiga.mui:layout-child nil 0 0 10 10))
                          "LAYOUT-CHILD" "muimaster.library")
        (message-mentions (lambda () (amiga.mui:request-idcmp nil 0))
                          "REQUEST-IDCMP" "muimaster.library")
        (message-mentions (lambda () (amiga.mui:reject-idcmp nil 0))
                          "REJECT-IDCMP" "muimaster.library")))

;;; --- the string-key hook: the C matcher, driven by hand ----------------
;;;
;;; The hook is C (src/core/builtins_amiga.c) so that Intuition can call
;;; it on input.device's task; on the host its entry is a C function of
;;; (hook, sgwork, message) that CALL-FOREIGN reaches.  The structs are
;;; built by hand at the offsets of intuition/sghooks.h and
;;; devices/inputevent.h, and the push -- MUIM_Application_PushMethod on
;;; the Amiga -- is recorded by the host platform (%LAST-PUSHED-METHOD).

(defconstant +t-sgw-size+ 44)
(defconstant +t-ie-size+ 22)
(defconstant +t-sgh-key+ 1)
(defconstant +t-sgh-click+ 2)

(defun call-string-key-hook (hook sgw msg)
  (ffi:call-foreign (amiga.mui:string-key-hook-entry hook) :uint32
                    '(:pointer :pointer :pointer) (list hook sgw msg)))

(defmacro with-sgwork ((sgw ie msg &key (code 0) (qualifier 0) (command +t-sgh-key+)
                                       (actions #x23) (editop 8))
                       &body body)
  "A struct SGWork with its InputEvent (a key press CODE with QUALIFIER)
and an SGH_* message, as Intuition hands them to an edit hook; ACTIONS
starts as SGA_USE|SGA_END|SGA_NEXTACTIVE, EDITOP as EO_INSERTCHAR.  The
IEvent pointer at 20 is 8 bytes on the host and covers Code at 24 (the
layout is Intuition's, 32-bit), so Code's value here is whatever the
pointer's upper half is: the checks compare it against the pre-call
state, and a taken key must leave it 0."
  `(let ((,sgw (ffi:alloc-foreign +t-sgw-size+))
         (,ie (ffi:alloc-foreign +t-ie-size+))
         (,msg (ffi:alloc-foreign 4)))
     (unwind-protect
          (progn
            (ffi:poke-u8 ,ie 1 4)                                 ; IECLASS_RAWKEY
            (ffi:poke-u16 ,ie ,code 6)
            (ffi:poke-u16 ,ie ,qualifier 8)
            (ffi:poke-pointer ,sgw ,ie 20)                        ; IEvent
            (ffi:poke-u32 ,sgw ,actions 30)
            (ffi:poke-u16 ,sgw ,editop 42)
            (ffi:poke-u32 ,msg ,command 0)
            ,@body)
       (ffi:free-foreign ,msg) (ffi:free-foreign ,ie) (ffi:free-foreign ,sgw))))

(defun sgwork-state (sgw ie)
  "What the hook may have rewritten: ie_Code, ie_Qualifier, Code, Actions, EditOp."
  (list (ffi:peek-u16 ie 6) (ffi:peek-u16 ie 8)
        (ffi:peek-u16 sgw 24) (ffi:peek-u32 sgw 30) (ffi:peek-u16 sgw 42)))

;; TAB (#x42) with no qualifier, C-g (#x24 + CONTROL) and Alt-x (#x32 + LALT)
;; are taken; the mask #x19 = LSHIFT|CONTROL|LALT after the right-hand fold
(defparameter *t-entries* '((#x42 #x19 #x00 1001)     ; TAB
                            (#x24 #x19 #x08 1002)     ; C-g
                            (#x32 #x19 #x10 1003)))   ; M-x

(check "string-key-hook-makes-and-frees" '(t 0 0 3 nil)
  (let ((hook (amiga.mui:make-string-key-hook 4096 8192 #x8100 *t-entries*)))
    (multiple-value-bind (calls matches count) (amiga.mui:string-key-hook-stats hook)
      (prog1 (list (ffi:foreign-pointer-p hook) calls matches count
                   (amiga.mui:free-string-key-hook hook))))))

(check "string-key-hook-takes-a-listed-key-and-pushes-it"
    '(#xFFFFFFFF (#xFF 0 0 #x00 1) (4096 #x80429EF8 8192 #x8100 1001) (1 1 3))
  (let ((hook (amiga.mui:make-string-key-hook 4096 8192 #x8100 *t-entries*)))
    (amiga::%last-pushed-method)                                ; clear
    (unwind-protect
         (with-sgwork (sgw ie msg :code #x42 :qualifier 0)
           (let ((rc (call-string-key-hook hook sgw msg)))
             (list rc (sgwork-state sgw ie) (amiga::%last-pushed-method)
                   (multiple-value-list (amiga.mui:string-key-hook-stats hook)))))
      (amiga.mui:free-string-key-hook hook))))

(check "string-key-hook-leaves-an-unlisted-key-to-the-gadget"
    '(#xFFFFFFFF t nil (1 0 3))
  (let ((hook (amiga.mui:make-string-key-hook 4096 8192 #x8100 *t-entries*)))
    (unwind-protect
         (with-sgwork (sgw ie msg :code #x20 :qualifier 0)        ; `a'
           (let* ((before (sgwork-state sgw ie))
                  (rc (call-string-key-hook hook sgw msg)))
             (list rc (equal before (sgwork-state sgw ie)) (amiga::%last-pushed-method)
                   (multiple-value-list (amiga.mui:string-key-hook-stats hook)))))
      (amiga.mui:free-string-key-hook hook))))

;; the qualifiers matter: TAB with Control is not the TAB entry, C-g needs
;; Control, Alt-x with either Alt; a Command key (outside the mask) is ignored
;; by the mask but a Shift inside it is not
(check "string-key-hook-matches-qualifiers-through-the-mask"
    '(nil 1002 1003 1003 1002 nil)
  (let ((hook (amiga.mui:make-string-key-hook 4096 8192 #x8100 *t-entries*)))
    (flet ((pushed (code qualifier)
             (amiga::%last-pushed-method)
             (with-sgwork (sgw ie msg :code code :qualifier qualifier)
               (call-string-key-hook hook sgw msg)
               (fifth (amiga::%last-pushed-method)))))
      (unwind-protect
           (list (pushed #x42 #x08)          ; C-TAB: not listed
                 (pushed #x24 #x08)          ; C-g
                 (pushed #x32 #x10)          ; LALT-x
                 (pushed #x32 #x20)          ; RALT-x: folded into LALT
                 (pushed #x24 #x48)          ; C-g with the left Amiga key: outside the mask
                 (pushed #x24 #x09))         ; C-S-g: Shift is inside the mask
        (amiga.mui:free-string-key-hook hook)))))

(check "string-key-hook-ignores-releases-and-other-commands"
    '((#xFFFFFFFF nil) (0 nil) (0 nil))
  (let ((hook (amiga.mui:make-string-key-hook 4096 8192 #x8100 *t-entries*)))
    (unwind-protect
         (list (with-sgwork (sgw ie msg :code #xC2 :qualifier 0)          ; TAB released
                 (list (call-string-key-hook hook sgw msg) (amiga::%last-pushed-method)))
               (with-sgwork (sgw ie msg :code #x42 :command +t-sgh-click+)  ; a click
                 (list (call-string-key-hook hook sgw msg) (amiga::%last-pushed-method)))
               (with-sgwork (sgw ie msg :code #x42)                       ; no SGWork at all
                 (list (ffi:call-foreign (amiga.mui:string-key-hook-entry hook) :uint32
                                         '(:pointer :pointer :pointer)
                                         (list hook (ffi:make-foreign-pointer 0) msg))
                       (amiga::%last-pushed-method))))
      (amiga.mui:free-string-key-hook hook))))

(check "string-key-hook-with-no-ievent-answers-done-and-pushes-nothing"
    '(#xFFFFFFFF nil)
  (let ((hook (amiga.mui:make-string-key-hook 4096 8192 #x8100 *t-entries*)))
    (unwind-protect
         (with-sgwork (sgw ie msg :code #x42)
           (ffi:poke-pointer sgw (ffi:make-foreign-pointer 0) 20)
           (list (call-string-key-hook hook sgw msg) (amiga::%last-pushed-method)))
      (amiga.mui:free-string-key-hook hook))))

(check "string-key-hook-value-and-objects-may-be-pointers-and-bignums"
    '(t t #x8100 #xFFFFFFF0)
  (let ((hook (amiga.mui:make-string-key-hook (ffi:make-foreign-pointer #x80424C33)
                                              (ffi:make-foreign-pointer 12) #x8100
                                              '((#x42 #x19 0 #xFFFFFFF0)))))
    (unwind-protect
         (with-sgwork (sgw ie msg :code #x42)
           (call-string-key-hook hook sgw msg)
           (let ((p (amiga::%last-pushed-method)))
             (list (integerp (first p)) (integerp (third p)) (fourth p) (fifth p))))
      (amiga.mui:free-string-key-hook hook))))

(check "string-key-hook-rejects-bad-entries" '(t t t t t)
  (list (message-mentions (lambda () (amiga.mui:make-string-key-hook 1 2 3 '((1 2 3))))
                          "MAKE-STRING-KEY-HOOK" "code qual-mask qual-value value")
        (message-mentions (lambda () (amiga.mui:make-string-key-hook 1 2 3 '((#xC2 0 0 1))))
                          "release")
        (message-mentions (lambda () (amiga.mui:make-string-key-hook 1 2 3 '((#x42 70000 0 1))))
                          "65535")
        (message-mentions (lambda () (amiga.mui:make-string-key-hook 1 2 3 '((#x42 0 0 "x"))))
                          "unsigned 32-bit")
        (message-mentions (lambda () (amiga.mui:free-string-key-hook (ffi:make-foreign-pointer 12)))
                          "not a live string-key hook")))

(check "string-key-hook-nil-free-is-ignored" nil
  (amiga.mui:free-string-key-hook nil))

;;; STRING-KEY-HOOK-STATS conses its (calls matches count) list and
;;; %LAST-PUSHED-METHOD conses its 5-element result (both call cl_cons /
;;; cl_amiga_box_result in src/core/builtins_amiga.c) while the hook
;;; object and the pushed values are live.  EXT:GC forces a compacting
;;; collection (CL_GENGC on the host, see CLAUDE.md "GC Safety") around
;;; every step, so a missing CL_GC_PROTECT in either builtin would show
;;; up here as a stale or corrupted result instead of passing quietly.
(check "string-key-hook-stats-and-last-pushed-survive-forced-gc" t
  (let ((hook (amiga.mui:make-string-key-hook 4096 8192 #x8100 *t-entries*)))
    (ext:gc)
    (unwind-protect
         (dotimes (i 20 t)
           (with-sgwork (sgw ie msg :code #x42 :qualifier 0)
             (ext:gc)
             (call-string-key-hook hook sgw msg)
             (ext:gc))
           (multiple-value-bind (calls matches count)
               (progn (ext:gc) (amiga.mui:string-key-hook-stats hook))
             (ext:gc)
             (let ((pushed (amiga::%last-pushed-method)))
               (unless (and (= calls (1+ i)) (= matches (1+ i)) (= count 3)
                            (equal pushed '(4096 #x80429EF8 8192 #x8100 1001)))
                 (return nil)))))
      (amiga.mui:free-string-key-hook hook))))

;;; --- the examples load on the host and bow out ------------------------

(defparameter *examples*
  '("hello" "layout" "group-layout" "balancing" "pages" "menus" "showhide"
    "slidorama" "numeric" "virtual" "requester" "class1" "hooks"))

(dolist (name *examples*)
  (check (format nil "example-~A-loads-and-bows-out" name) t
    (let ((output (with-output-to-string (*standard-output*)
                    (load (format nil "examples/amiga/mui/~A.lisp" name)))))
      (and (search "not available" output) t))))

(format t "~%~D passed, ~D failed~%" *pass* *fail*)
(if (zerop *fail*)
    (format t "ALL MUI HOST CHECKS PASSED~%")
    (format t "SOME MUI HOST CHECKS FAILED~%"))
