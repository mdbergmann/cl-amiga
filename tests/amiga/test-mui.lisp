;;; test-mui.lisp — Amiga-side tests of lib/amiga/mui.lisp (AMIGA.MUI,
;;; the MUI helpers over AMIGA.BOOPSI and muimaster.library).
;;;
;;; Loaded from run-tests.lisp via (load ...) inside #+amigaos, like
;;; test-reaction.lisp: CHECK comes from run-tests.lisp.  The portable
;;; half (CLASS-ID, MAKE-ID, the message packing, the diagnostics) is
;;; mirrored on the host by tests/test_amiga_mui.sh and only spot-checked
;;; here on real 32-bit addresses; the object half needs MUI (3.8 in the
;;; FS-UAE Workbench, MUI 4 built into MorphOS) and is skipped -- with an
;;; assertion that AVAILABLE-P says so -- where it is absent.  Nothing
;;; here needs a user: the notification test fires MUIM_Notify by setting
;;; the attribute from Lisp, the loop test relies on the timeout.
;;; MUI_RequestA is interactive and covered by the example only.

(require "amiga/mui")
(format t "; mui: amiga/mui loaded~%")
(finish-output)

;;; --- portable half, on real 32-bit addresses ----------------------------

;; the same symbols as AMIGA.BOOPSI, not homonyms
(check "mui-shares-boopsi-symbols" t
  (let ((ok t))
    (dolist (name '("DO-METHOD" "OBJECT-CLASS" "WITH-FOREIGN-POOL" "POOL-ALLOC"
                    "POOL-STRING" "WITH-TAGS" "GET-ATTR" "GET-ATTR-POINTER" "SET-ATTRS"
                    "%ULONG" "%WITH-TAGS"))
      (unless (eq (find-symbol name "AMIGA.MUI") (find-symbol name "AMIGA.BOOPSI"))
        (setf ok nil)))
    ok))

(check "mui-class-id" '("Window.mui" "Numericbutton.mui" "Group.mui")
  (mapcar #'amiga.mui:class-id '(:window :numeric-button "Group.mui")))

(check "mui-make-id" #x4D41494E (amiga.mui:make-id "MAIN"))

;; POOL-STRING-ARRAY: the STRPTR[] reads back, NULL-terminated
(check "mui-pool-string-array-reads-back" '("Fast" "Slow" 0)
  (amiga.mui:with-foreign-pool ()
    (let ((a (amiga.mui:pool-string-array '("Fast" "Slow"))))
      (list (ffi:foreign-to-string (ffi:make-foreign-pointer (ffi:peek-u32 a 0)))
            (ffi:foreign-to-string (ffi:make-foreign-pointer (ffi:peek-u32 a 4)))
            (ffi:peek-u32 a 8)))))

;; the MUI_MakeObject parameter array: a pooled label, a pooled STRPTR[]
(check "mui-ulong-array-strings-read-back" '(7 "Mode" "a" "b" 0)
  (amiga.mui:with-foreign-pool ()
    (amiga.mui::%with-ulong-array (a '(7 "Mode" ("a" "b")) :pooled t)
      (let ((entries (ffi:make-foreign-pointer (ffi:peek-u32 a 8))))
        (list (ffi:peek-u32 a 0)
              (ffi:foreign-to-string (ffi:make-foreign-pointer (ffi:peek-u32 a 4)))
              (ffi:foreign-to-string (ffi:make-foreign-pointer (ffi:peek-u32 entries 0)))
              (ffi:foreign-to-string (ffi:make-foreign-pointer (ffi:peek-u32 entries 4)))
              (ffi:peek-u32 entries 8))))))

(check "mui-notify-rejects-bad-destination" t
  (handler-case (progn (amiga.mui:notify nil 1 t :nowhere 2) nil)
    (error (e) (and (search ":NOWHERE" (format nil "~A" e)) t))))

;;; --- MUI half -------------------------------------------------------------

(defvar *mui-p* (amiga.mui:available-p))
(format t "; mui: muimaster.library ~:[absent — skipping the object checks~;present~]~%" *mui-p*)
(finish-output)

;; AVAILABLE-P agrees with the OS: muimaster.library >= MUIMASTER_VMIN opens
(check "mui-available-p-matches-muimaster" t
  (eq (if *mui-p* t nil)
      (let ((b (amiga:open-library "muimaster.library" 11)))
        (when b (amiga:close-library b))
        (if b t nil))))

;; ... and a T answer left the module's base open (a real library base:
;; lib_Version is at least MUIMASTER_VMIN)
(check "mui-available-p-opened-the-base" t
  (if *mui-p*
      (and amiga.mui::*muimaster-base*
           (>= (amiga.ffi:library-version amiga.mui::*muimaster-base*) 11))
      (null amiga.mui::*muimaster-base*)))

;; The raw module is only REQUIREd where MUI exists, so nothing in this
;; file may spell an amiga.raw.muimaster: symbol -- the reader would need
;; the package before the REQUIRE has run (it did not exist in a MUI-only
;; run on MorphOS).  Every constant goes through %M instead.
(defun %mui-sym (name)
  (or (find-symbol name "AMIGA.RAW.MUIMASTER")
      (error "AMIGA.RAW.MUIMASTER::~A not found" name)))
(defun %m (name) (symbol-value (%mui-sym name)))

(when *mui-p*
  (require "amiga/raw/muimaster")
  (format t "; mui: raw module loaded (muimaster ~D)~%" (%m "*MUIMASTER-VERSION*"))
  (finish-output))

;; the module's private copies of the mui.h values must agree with the
;; generated ones (the host cross-checks all of them; here the ones the
;; loop and NOTIFY depend on, against the library actually running)
(check "mui-constants-match-generated" t
  (if *mui-p*
      (and (= amiga.mui::+muim-notify+ (%m "+MUIM-NOTIFY+"))
           (= amiga.mui::+muim-application-new-input+ (%m "+MUIM-APPLICATION-NEW-INPUT+"))
           (= amiga.mui::+muim-application-return-id+ (%m "+MUIM-APPLICATION-RETURN-ID+"))
           (= amiga.mui::+muiv-application-return-id-quit+ (%m "+MUIV-APPLICATION-RETURN-ID-QUIT+"))
           (= amiga.mui::+muiv-every-time+ (%m "+MUIV-EVERY-TIME+"))
           (= amiga.mui::+muiv-notify-application+ (%m "+MUIV-NOTIFY-APPLICATION+"))
           (= amiga.mui::+muio-button+ (%m "+MUIO-BUTTON+"))
           (= amiga.mui::+muimaster-vmin+ (%m "+MUIMASTER-VMIN+"))
           t)
      t))

;; CLASS-ID's keyword rule against the raw MUIC_ strings, on the Amiga too
(check "mui-class-id-matches-raw-muic" t
  (if *mui-p*
      (and (string= (amiga.mui:class-id :window) (%m "+MUIC-WINDOW+"))
           (string= (amiga.mui:class-id :application) (%m "+MUIC-APPLICATION+"))
           (string= (amiga.mui:class-id :numeric-button) (%m "+MUIC-NUMERICBUTTON+"))
           (string= (amiga.mui:class-id :scrollgroup) (%m "+MUIC-SCROLLGROUP+")))
      t))

;; NEW-OBJECT by keyword and by the raw MUIC_ string: a Rectangle, a Text
;; with a pooled string tag; OBJECT-CLASS is a class; DISPOSE-OBJECT
(check "mui-new-object-rectangle-text-dispose" '(t t t t nil)
  (if *mui-p*
      (amiga.mui:with-foreign-pool ()
        (let ((rect (amiga.mui:new-object :rectangle))
              (text (amiga.mui:new-object (%m "+MUIC-TEXT+")
                                          (%m "+MUIA-TEXT-CONTENTS+") "Hello")))
          (list (and rect (ffi:foreign-pointer-p rect) (not (ffi:null-pointer-p rect)))
                (and text (ffi:foreign-pointer-p text) t)
                (let ((cls (amiga.mui:object-class rect)))
                  (and cls (ffi:foreign-pointer-p cls) (not (ffi:null-pointer-p cls))))
                ;; MUIA_Text_Contents reads back through GET-ATTR-STRING
                (equal "Hello" (amiga.mui:get-attr-string (%m "+MUIA-TEXT-CONTENTS+") text))
                (progn (amiga.mui:dispose-object text)
                       (amiga.mui:dispose-object rect)))))
      '(t t t t nil)))

;; an unknown class -> MUI_NewObjectA returns NULL -> an error naming the
;; class (and the keyword it came from).  (Not "Nonexistent.mui": a real
;; MorphOS box turned out to have a class of that name in MUI:Libs/mui.)
(check "mui-new-object-unknown-class-is-error" '(t t)
  (if *mui-p*
      (list (handler-case (progn (amiga.mui:new-object "Clamigabogus.mui") nil)
              (error (e) (and (search "Clamigabogus.mui" (format nil "~A" e)) t)))
            (handler-case (progn (amiga.mui:new-object :no-such-class) nil)
              (error (e) (let ((m (format nil "~A" e)))
                           (and (search "Nosuchclass.mui" m) (search ":NO-SUCH-CLASS" m) t)))))
      '(t t)))

;; MAKE-OBJECT: MUI_MakeObject's builtin button, a slider with min/max, a
;; cycle whose entries are a list of strings
(check "mui-make-object-button-slider-cycle" '(t t t)
  (if *mui-p*
      (amiga.mui:with-foreign-pool ()
        (let ((btn (amiga.mui:make-object :button "_OK"))
              (sld (amiga.mui:make-object :slider "Volume" 0 64))
              (cyc (amiga.mui:make-object :cycle "Mode" '("Fast" "Slow"))))
          (prog1 (list (and btn (ffi:foreign-pointer-p btn) t)
                       (and sld (ffi:foreign-pointer-p sld) t)
                       (and cyc (ffi:foreign-pointer-p cyc) t))
            (amiga.mui:dispose-object cyc)
            (amiga.mui:dispose-object sld)
            (amiga.mui:dispose-object btn))))
      '(t t t)))

;; a String object: SET-ATTRS of MUIA_String_Contents (a pooled string),
;; read back with GET-ATTR-STRING; GET-ATTR of an unknown attribute is NIL
(check "mui-string-contents-round-trip" '("" "typed" nil)
  (if *mui-p*
      (amiga.mui:with-foreign-pool ()
        (let ((str (amiga.mui:new-object :string
                                         (%m "+MUIA-STRING-MAX-LEN+") 32)))
          (unwind-protect
               (list (amiga.mui:get-attr-string (%m "+MUIA-STRING-CONTENTS+") str)
                     (progn (amiga.mui:set-attrs str (%m "+MUIA-STRING-CONTENTS+") "typed")
                            (amiga.mui:get-attr-string (%m "+MUIA-STRING-CONTENTS+") str))
                     (amiga.mui:get-attr #x8EADBEEF str))
            (amiga.mui:dispose-object str))))
      '("" "typed" nil)))

;;; The full life of an application: Application / Window / Group / String
;;; / Text tree, notifications, the return-ID queue, open / close, the
;;; bounded event loop, disposal of everything through the application.

(defun %mui-make-app (string-cell)
  "An Application owning one Window; the String object is stored in
STRING-CELL for the notification test.  Returns the application."
  (let* ((str (amiga.mui:new-object :string
                                    (%m "+MUIA-STRING-MAX-LEN+") 64
                                    (%m "+MUIA-STRING-CONTENTS+") "initial"))
         (win (amiga.mui:new-object :window
                                    (%m "+MUIA-WINDOW-TITLE+") "AMIGA.MUI test"
                                    (%m "+MUIA-WINDOW-ID+") (amiga.mui:make-id "TEST")
                                    (%m "+MUIA-WINDOW-ROOT-OBJECT+")
                                    (amiga.mui:new-object :group
                                                          (%m "+MUIA-GROUP-CHILD+")
                                                          (amiga.mui:new-object :text
                                                                                (%m "+MUIA-TEXT-CONTENTS+") "Testing MUI from Lisp")
                                                          (%m "+MUIA-GROUP-CHILD+") str)))
         (app (amiga.mui:new-object :application
                                    (%m "+MUIA-APPLICATION-TITLE+") "CLAmigaMUITest"
                                    (%m "+MUIA-APPLICATION-BASE+") "CLAMIGAMUITEST"
                                    (%m "+MUIA-APPLICATION-WINDOW+") win)))
    (setf (car string-cell) str)
    (values app win)))

;; RETURN-ID queues an ID that APPLICATION-INPUT hands back, with a signal
;; mask to wait on; the queue empties to NIL
(check "mui-return-id-then-application-input" '(7 t nil)
  (if *mui-p*
      (amiga.mui:with-foreign-pool ()
        (let ((cell (list nil)))
          (let ((app (%mui-make-app cell)))
            (unwind-protect
                 (progn
                   (amiga.mui:return-id app 7)
                   (multiple-value-bind (id sigs) (amiga.mui:application-input app)
                     (list id
                           (integerp sigs)
                           (amiga.mui:application-input app))))
              (amiga.mui:dispose-object app)))))
      '(7 t nil)))

;; MUIV_Application_ReturnID_Quit comes back as :QUIT
(check "mui-return-id-quit-is-keyword" :quit
  (if *mui-p*
      (amiga.mui:with-foreign-pool ()
        (let* ((cell (list nil))
               (app (%mui-make-app cell)))
          (unwind-protect
               (progn
                 (amiga.mui:return-id app (%m "+MUIV-APPLICATION-RETURN-ID-QUIT+"))
                 (amiga.mui:application-input app))
            (amiga.mui:dispose-object app))))
      :quit))

;; NOTIFY without a user: MUIA_String_Contents :EVERY-TIME -> the
;; application's MUIM_Application_ReturnID 42; setting the contents from
;; Lisp fires it, and APPLICATION-INPUT returns 42.  Both destination
;; spellings -- :APPLICATION (MUIV_Notify_Application) and the object.
;;
;; The window is OPENED first, on purpose: :APPLICATION resolves through
;; the object's application pointer, which MUI 3.8 propagates down the
;; tree when the window is attached to the application, while MorphOS's
;; MUI (muimaster 22) sets it at window setup -- from an object in a
;; never-opened window the notification fires on 3.8 and is silently
;; dropped on MorphOS.  The object destination works on both regardless.
;; A program that sets its notifications up before opening its windows,
;; as every C MUI program does, is fine either way: they fire later.
(check "mui-notify-fires-on-set-attrs" '(42 43)
  (if *mui-p*
      (amiga.mui:with-foreign-pool ()
        (let ((cell (list nil)))
          (multiple-value-bind (app win) (%mui-make-app cell)
            (let ((str (car cell)))
              (unwind-protect
                   (progn
                     (amiga.mui:set-attrs win (%m "+MUIA-WINDOW-OPEN+") t)
                     (amiga.mui:notify str (%m "+MUIA-STRING-CONTENTS+") :every-time
                                       :application (%m "+MUIM-APPLICATION-RETURN-ID+") 42)
                     (amiga.mui:set-attrs str (%m "+MUIA-STRING-CONTENTS+") "changed")
                     (let ((first (amiga.mui:application-input app)))
                       (amiga.mui:notify str (%m "+MUIA-STRING-CONTENTS+") :every-time
                                         app (%m "+MUIM-APPLICATION-RETURN-ID+") 43)
                       (amiga.mui:set-attrs str (%m "+MUIA-STRING-CONTENTS+") "again")
                       ;; the first notification fires 42 again, then 43
                       (let ((again (amiga.mui:application-input app)))
                         (list first
                               (if (eql again 42) (amiga.mui:application-input app) again)))))
                (amiga.mui:set-attrs win (%m "+MUIA-WINDOW-OPEN+") nil)
                (amiga.mui:dispose-object app))))))
      '(42 43)))

;; a notification whose trigger value does not match stays silent
;; (window open, see above)
(check "mui-notify-trigger-value-selects" '(nil 5)
  (if *mui-p*
      (amiga.mui:with-foreign-pool ()
        (let ((cell (list nil)))
          (multiple-value-bind (app win) (%mui-make-app cell)
            (let ((str (car cell)))
              (unwind-protect
                   (let ((disabled (%m "+MUIA-DISABLED+")))
                     (amiga.mui:set-attrs win (%m "+MUIA-WINDOW-OPEN+") t)
                     (amiga.mui:notify str disabled t
                                       :application (%m "+MUIM-APPLICATION-RETURN-ID+") 5)
                     (amiga.mui:set-attrs str disabled nil)
                     (list (amiga.mui:application-input app)
                           (progn (amiga.mui:set-attrs str disabled t)
                                  (amiga.mui:application-input app))))
                (amiga.mui:set-attrs win (%m "+MUIA-WINDOW-OPEN+") nil)
                (amiga.mui:dispose-object app))))))
      '(nil 5)))

;; the window: MUIA_Window_Open set and read back, the window's struct
;; Window through GET-ATTR-POINTER, the bounded loop returning on its
;; timeout, close, and the application disposing the whole tree
(check "mui-window-open-loop-close" '(0 1 t :looped 0)
  (if *mui-p*
      (amiga.mui:with-foreign-pool ()
        (let ((cell (list nil)))
          (multiple-value-bind (app win) (%mui-make-app cell)
            (unwind-protect
                 (let ((open (%m "+MUIA-WINDOW-OPEN+")))
                   (list (amiga.mui:get-attr open win)
                         (progn (amiga.mui:set-attrs win open t)
                                (amiga.mui:get-attr open win))
                         (let ((w (amiga.mui:get-attr-pointer (%m "+MUIA-WINDOW-WINDOW+") win)))
                           (and w (ffi:foreign-pointer-p w) t))
                         (let ((looped :looped))
                           ;; nobody clicks: NewInput yields no ID, the
                           ;; loop polls until the timeout
                           (amiga.mui:do-application-events ((id) app :timeout 1)
                             (setq looped (list :unexpected-id id)))
                           looped)
                         (progn (amiga.mui:set-attrs win open nil)
                                (amiga.mui:get-attr open win))))
              (amiga.mui:dispose-object app)))))
      '(0 1 t :looped 0)))

;; the loop leaves on :QUIT from the queue, running the body for the IDs
;; before it.  MUI's return-ID queue is FIFO and Quit is just an entry in
;; it: an ID queued after Quit is NOT dropped -- the next loop over the
;; same application sees it first (MUI 3.8 and 4 alike).  And (RETURN)
;; from the body leaves the loop with the rest still queued.
(check "mui-do-application-events-ids-and-quit" '((1 2) (9) (3))
  (if *mui-p*
      (amiga.mui:with-foreign-pool ()
        (let* ((cell (list nil))
               (app (%mui-make-app cell))
               (seen '()))
          (unwind-protect
               (flet ((collect (stop)
                        (setf seen '())
                        (amiga.mui:do-application-events ((id) app :timeout 2)
                          (push id seen)
                          (when stop (return)))
                        (reverse seen)))
                 (amiga.mui:return-id app 1)
                 (amiga.mui:return-id app 2)
                 (amiga.mui:return-id app (%m "+MUIV-APPLICATION-RETURN-ID-QUIT+"))
                 (amiga.mui:return-id app 9)          ; survives the Quit
                 (let* ((first (collect nil))          ; 1, 2, then Quit ends it
                        (leftover (collect t))         ; 9 was still queued
                        (third (progn (amiga.mui:return-id app 3)
                                      (amiga.mui:return-id app 4)
                                      (collect t))))   ; 3, then (RETURN)
                   (list first leftover third)))
            (amiga.mui:dispose-object app))))
      '((1 2) (9) (3))))

(format t "; mui: done~%")
(finish-output)
