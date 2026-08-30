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
                       "AREA-LEFT" "AREA-MBOTTOM" "AREA-MHEIGHT" "AREA-MLEFT" "AREA-MRIGHT"
                       "AREA-MTOP" "AREA-MWIDTH" "AREA-PENS" "AREA-RASTPORT" "AREA-RENDER-INFO"
                       "AREA-RIGHT" "AREA-SCREEN" "AREA-TOP" "AREA-WIDTH" "AREA-WINDOW"
                       "AVAILABLE-P" "CLASS-ID" "CREATE-CUSTOM-CLASS" "CUSTOM-CLASS-CLASS"
                       "DISPOSE-OBJECT" "DO-APPLICATION-EVENTS" "DO-METHOD" "DO-SUPER-METHOD"
                       "DRAW-FLAGS" "GET-ATTR" "GET-ATTR-POINTER" "GET-ATTR-STRING" "INST-DATA"
                       "MAKE-ID" "MAKE-OBJECT" "METHOD-ID" "MIN-MAX-INFO"
                       "NEW-OBJECT" "NOTIFY" "OBJECT-CLASS" "POOL-ALLOC" "POOL-FINALIZER"
                       "POOL-HOOK" "POOL-STRING"
                       "POOL-STRING-ARRAY" "REQUEST" "RETURN-ID" "SET-ATTRS"
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
  (let ((args (amiga.mui::%notify-args 1 t :self 9 '(:not-trigger-value 5 6))))
    (list (nth 3 (amiga.mui::%notify-args 1 t :self 9 '()))
          (nth 3 args)
          (nth 5 args))))

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

;;; --- the examples load on the host and bow out ------------------------

(defparameter *examples*
  '("hello" "layout" "balancing" "pages" "menus" "showhide" "slidorama"
    "virtual" "requester" "class1" "hooks"))

(dolist (name *examples*)
  (check (format nil "example-~A-loads-and-bows-out" name) t
    (let ((output (with-output-to-string (*standard-output*)
                    (load (format nil "examples/amiga/mui/~A.lisp" name)))))
      (and (search "not available" output) t))))

(format t "~%~D passed, ~D failed~%" *pass* *fail*)
(if (zerop *fail*)
    (format t "ALL MUI HOST CHECKS PASSED~%")
    (format t "SOME MUI HOST CHECKS FAILED~%"))
