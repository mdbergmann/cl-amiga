;;; hooks.lisp — struct Hooks written in Lisp, called by MUI: a list whose
;;; rows are formatted by a MUIA_List_DisplayHook (two columns, a title
;;; row), buttons whose MUIA_Pressed notification runs a Lisp hook through
;;; MUIM_CallHook, and a double-click on the list that reports the entry
;;; the same way.  The SDK uses these three idioms in psi.c (display
;;; hooks), Layout.c and AppWindow.c (MUIM_CallHook); this program is
;;; theirs boiled down.
;;;
;;; What it shows: AMIGA.MUI:POOL-HOOK (AMIGA.FFI:MAKE-HOOK, a struct Hook
;;; whose entry is a Lisp function of hook / object / message, freed by
;;; the foreign pool), MUIA_List_DisplayHook filling the STRPTR array MUI
;;; hands it, MUIA_List_ConstructHook / DestructHook with the builtin
;;; string hooks so MUI copies the entries, MUIM_List_InsertSingle,
;;; MUIM_CallHook as a NOTIFY method with a parameter and with
;;; MUIV_TriggerValue, MUIA_Listview_DoubleClick, MUIM_List_GetEntry.
;;;
;;; A hook runs inside MUI, on its stack: an error in it is caught at the
;;; callback, the hook returns 0, and the condition is re-signaled in Lisp
;;; when the MUI call that invoked it returns -- here that is
;;; APPLICATION-INPUT inside the event loop.
;;;
;;; Run on AmigaOS 3.x with MUI 3.8+ installed, or on MorphOS:
;;;   clamiga --load examples/amiga/mui/hooks.lisp
;;; Unattended (auto-closes after 5 s):
;;;   clamiga --eval '(require "amiga/mui")' \
;;;           --eval '(setf amiga.mui:*event-loop-timeout* 5)' \
;;;           --load examples/amiga/mui/hooks.lisp

(require "amiga/mui")

(when (amiga.mui:available-p)
  (require "amiga/raw/muimaster"))

(defpackage "MUI-HOOKS"
  (:use "CL")
  (:local-nicknames ("MUI" "AMIGA.MUI")))

(in-package "MUI-HOOKS")

(defun m (name)
  "The value of the amiga/raw/muimaster constant NAME -- looked up at run
time, so this file reads on a system where that package does not exist."
  (symbol-value (or (find-symbol name "AMIGA.RAW.MUIMASTER")
                    (error "hooks: amiga.raw.muimaster:~A not found" name))))

(defparameter *planets*
  '(("Mercury" 0) ("Venus" 0) ("Earth" 1) ("Mars" 2)
    ("Jupiter" 95) ("Saturn" 146) ("Uranus" 28) ("Neptune" 16)))

(defun children (objects)
  (loop for o in objects collect (m "+MUIA-GROUP-CHILD+") collect o))

(defun vgroup (tags &rest objects)
  (apply #'mui:new-object :group (append tags (children objects))))

(defun hgroup (tags &rest objects)
  (apply #'vgroup (list* (m "+MUIA-GROUP-HORIZ+") t tags) objects))

;;; The display hook.  MUI calls it for every row it renders (and again
;;; for the title row, with a NULL entry) with a2 = the STRPTR array to
;;; fill, one slot per column, and a1 = the entry -- here the string MUI
;;; copied at insert time (the String construct hook).  The strings the
;;; hook hands back must stay valid until the next call: the entry itself
;;; does; the moon count is a pooled string, made once per planet and
;;; cached, so the hook never allocates foreign memory on the fly.

(defun make-display-hook (moons-column)
  "MOONS-COLUMN: planet name -> pooled foreign string of its moon count."
  (let ((title-name  (mui:pool-string "Planet"))
        (title-moons (mui:pool-string "Moons")))
    (mui:pool-hook
     (lambda (hook array entry)
       (declare (ignore hook))
       (if (ffi:null-pointer-p entry)
           (progn                                                 ; the title row
             (ffi:poke-u32 array (ffi:foreign-pointer-address title-name) 0)
             (ffi:poke-u32 array (ffi:foreign-pointer-address title-moons) 4))
           (let ((moons (gethash (ffi:foreign-to-string entry) moons-column)))
             (ffi:poke-u32 array (ffi:foreign-pointer-address entry) 0)
             (ffi:poke-u32 array (ffi:foreign-pointer-address moons) 4)))
       0))))

(defun run ()
  (mui:with-foreign-pool ()
    (let* ((moons-column (make-hash-table :test #'equal))
           (report (mui:new-object :text
                     (m "+MUIA-FRAME+") (m "+MUIV-FRAME-TEXT+")
                     (m "+MUIA-BACKGROUND+") (m "+MUII-TEXT-BACK+")
                     (m "+MUIA-TEXT-SET-MIN+") nil
                     (m "+MUIA-TEXT-CONTENTS+")
                     (concatenate 'string (m "+MUIX-C+") "Press a button or double-click a planet.")))
           (calls 0)
           ;; MUIM_CallHook's hook: a1 = the parameters after the hook in
           ;; the MUIM_CallHook message, here one longword
           (press-hook (mui:pool-hook
                        (lambda (hook app message)
                          (declare (ignore hook app))
                          (let ((n (ffi:peek-u32 message 0)))
                            (incf calls)
                            (mui:set-attrs report (m "+MUIA-TEXT-CONTENTS+")
                                           (format nil "~AButton ~D pressed -- hook call ~D."
                                                   (m "+MUIX-C+") n calls)))
                          0)))
           ;; the same, with MUIV_TriggerValue = the active entry's index
           (click-hook (mui:pool-hook
                        (lambda (hook app message)
                          (declare (ignore hook app))
                          (let* ((index (ffi:peek-u32 message 0))
                                 (planet (nth index *planets*)))
                            (incf calls)
                            (mui:set-attrs report (m "+MUIA-TEXT-CONTENTS+")
                                           (format nil "~A~A has ~D moon~:P -- hook call ~D."
                                                   (m "+MUIX-C+") (first planet) (second planet) calls)))
                          0)))
           (list (mui:new-object :list
                   (m "+MUIA-FRAME+") (m "+MUIV-FRAME-INPUT-LIST+")
                   (m "+MUIA-LIST-FORMAT+") "BAR,"                    ; two columns
                   (m "+MUIA-LIST-TITLE+") t                          ; the hook sees a NULL entry
                   (m "+MUIA-LIST-DISPLAY-HOOK+") (make-display-hook moons-column)
                   (m "+MUIA-LIST-CONSTRUCT-HOOK+") (m "+MUIV-LIST-CONSTRUCT-HOOK-STRING+")
                   (m "+MUIA-LIST-DESTRUCT-HOOK+") (m "+MUIV-LIST-DESTRUCT-HOOK-STRING+")))
           (listview (mui:new-object :listview
                       (m "+MUIA-LISTVIEW-LIST+") list))
           (one   (mui:make-object :button "Hook _1"))
           (two   (mui:make-object :button "Hook _2"))
           (three (mui:make-object :button "Hook _3"))
           (quit  (mui:make-object :button "_Quit"))
           (win (mui:new-object :window
                  (m "+MUIA-WINDOW-TITLE+") "Hooks"
                  (m "+MUIA-WINDOW-ID+")    (mui:make-id "HOOK")
                  (m "+MUIA-WINDOW-ROOT-OBJECT+")
                  (vgroup '()
                    (mui:new-object :text
                      (m "+MUIA-FRAME+") (m "+MUIV-FRAME-TEXT+")
                      (m "+MUIA-BACKGROUND+") (m "+MUII-TEXT-BACK+")
                      (m "+MUIA-TEXT-CONTENTS+")
                      (concatenate 'string (m "+MUIX-C+")
                                   (format nil "The list rows come from a Lisp display hook,~%the buttons and a double-click call Lisp hooks through MUIM_CallHook.")))
                    listview
                    report
                    (hgroup (list (m "+MUIA-GROUP-SAME-SIZE+") t) one two three quit))))
           (app (mui:new-object :application
                  (m "+MUIA-APPLICATION-TITLE+")       "Hooks"
                  (m "+MUIA-APPLICATION-VERSION+")     "$VER: Hooks 1.0"
                  (m "+MUIA-APPLICATION-DESCRIPTION+") "Lisp hooks called by MUI"
                  (m "+MUIA-APPLICATION-BASE+")        "HOOKS"
                  (m "+MUIA-APPLICATION-WINDOW+")      win)))
      (unwind-protect
           (progn
             ;; the entries: MUI copies each string (String construct hook),
             ;; the display hook's second column is made once per planet
             (loop for (name moons) in *planets*
                   do (setf (gethash name moons-column)
                            (mui:pool-string (format nil "~D" moons)))
                      (mui:do-method list (m "+MUIM-LIST-INSERT-SINGLE+")
                                     (mui:pool-string name) (m "+MUIV-LIST-INSERT-BOTTOM+")))
             (mui:notify win (m "+MUIA-WINDOW-CLOSE-REQUEST+") t
                         :application (m "+MUIM-APPLICATION-RETURN-ID+")
                         (m "+MUIV-APPLICATION-RETURN-ID-QUIT+"))
             (mui:notify quit (m "+MUIA-PRESSED+") nil
                         :application (m "+MUIM-APPLICATION-RETURN-ID+")
                         (m "+MUIV-APPLICATION-RETURN-ID-QUIT+"))
             ;; DoMethod(button, MUIM_Notify, MUIA_Pressed, FALSE,
             ;;          app, 3, MUIM_CallHook, &hook, i)
             (loop for button in (list one two three)
                   for i from 1
                   do (mui:notify button (m "+MUIA-PRESSED+") nil
                                  :application (m "+MUIM-CALL-HOOK+") press-hook i))
             ;; a double-click hands the hook the entry's index
             (mui:notify listview (m "+MUIA-LISTVIEW-DOUBLE-CLICK+") t
                         :application (m "+MUIM-CALL-HOOK+") click-hook :trigger-value)
             (mui:notify listview (m "+MUIA-LISTVIEW-DOUBLE-CLICK+") t
                         list (m "+MUIM-SET+") (m "+MUIA-LIST-ACTIVE+") (m "+MUIV-LIST-ACTIVE-OFF+"))
             (mui:set-attrs win (m "+MUIA-WINDOW-OPEN+") t)
             (when (zerop (or (mui:get-attr (m "+MUIA-WINDOW-OPEN+") win) 0))
               (error "hooks: the window would not open"))
             (mui:do-application-events ((id) app)
               nil)                   ; no return IDs of our own
             (mui:set-attrs win (m "+MUIA-WINDOW-OPEN+") nil)
             (format t "~&; hooks: the Lisp hooks were called ~D time~:P~%" calls))
        (mui:dispose-object app)))))          ; then the pool frees the hooks

(if (mui:available-p)
    (run)
    (format t "~&; hooks: MUI is not available on this system (muimaster.library 3.8+ on AmigaOS, or MorphOS, needed) — nothing to show.~%"))
