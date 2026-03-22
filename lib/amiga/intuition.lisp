;;; amiga/intuition.lisp — Intuition library abstractions for CL-Amiga
;;;
;;; Loaded via (require "amiga/intuition").
;;; Provides high-level window/screen/IDCMP management.

(require "amiga/ffi")

(defpackage "AMIGA.INTUITION"
  (:use "CL" "FFI" "AMIGA.FFI")
  (:export
   ;; Library
   "*INTUITION-BASE*"
   ;; Window
   "OPEN-WINDOW" "CLOSE-WINDOW" "WITH-WINDOW"
   "WINDOW-RASTPORT" "WINDOW-WIDTH" "WINDOW-HEIGHT"
   "WINDOW-LEFT" "WINDOW-TOP" "WINDOW-TITLE"
   ;; Screen
   "OPEN-SCREEN" "CLOSE-SCREEN" "WITH-SCREEN"
   ;; IDCMP
   "GET-MSG" "REPLY-MSG" "WAIT-PORT"
   "MSG-CLASS" "MSG-CODE" "MSG-MOUSE-X" "MSG-MOUSE-Y"
   "EVENT-LOOP"
   ;; IDCMP class constants
   "+IDCMP-CLOSEWINDOW+" "+IDCMP-GADGETUP+" "+IDCMP-GADGETDOWN+"
   "+IDCMP-MOUSEBUTTONS+" "+IDCMP-MOUSEMOVE+" "+IDCMP-RAWKEY+"
   "+IDCMP-MENUPICK+" "+IDCMP-REFRESHWINDOW+" "+IDCMP-NEWSIZE+"
   "+IDCMP-VANILLAKEY+"
   ;; Window flag constants
   "+WFLG-CLOSEGADGET+" "+WFLG-DRAGBAR+" "+WFLG-DEPTHGADGET+"
   "+WFLG-SIZEGADGET+" "+WFLG-ACTIVATE+" "+WFLG-SMART-REFRESH+"
   "+WFLG-SIMPLE-REFRESH+" "+WFLG-BACKDROP+" "+WFLG-BORDERLESS+"
   "+WFLG-GIMMEZEROZERO+" "+WFLG-REPORTMOUSE+" "+WFLG-RMBTRAP+"
   ;; Tag constants
   "+WA-LEFT+" "+WA-TOP+" "+WA-WIDTH+" "+WA-HEIGHT+"
   "+WA-TITLE+" "+WA-IDCMP+" "+WA-FLAGS+" "+WA-CUSTOMSCREEN+"
   "+WA-GADGETS+"
   ;; Menu/gadget list
   "SET-MENU-STRIP" "CLEAR-MENU-STRIP"
   "ADD-GADGET-LIST" "REFRESH-GADGET-LIST"
   ;; Public screens
   "LOCK-PUB-SCREEN" "UNLOCK-PUB-SCREEN" "WITH-PUB-SCREEN"))

(in-package "AMIGA.INTUITION")

;;; ================================================================
;;; Intuition library base (opened on load)
;;; ================================================================

(defvar *intuition-base* (amiga:open-library "intuition.library" 39))
(unless *intuition-base*
  (error "Cannot open intuition.library v39"))

;;; ================================================================
;;; LVO offsets (from amiga/intuition_lib.fd)
;;; ================================================================

(defconstant +lvo-open-window+         -204)
(defconstant +lvo-close-window+         -72)
(defconstant +lvo-open-screen+         -198)
(defconstant +lvo-close-screen+         -66)
(defconstant +lvo-open-window-tag-list+ -606)
(defconstant +lvo-open-screen-tag-list+ -612)
(defconstant +lvo-set-menu-strip+      -264)
(defconstant +lvo-clear-menu-strip+     -54)
(defconstant +lvo-add-g-list+          -438)
(defconstant +lvo-refresh-g-list+      -432)
(defconstant +lvo-lock-pub-screen+     -510)
(defconstant +lvo-unlock-pub-screen+   -516)

;;; Exec LVOs for message handling
(defvar *exec-base* (ffi:make-foreign-pointer 4))  ; ExecBase at absolute addr 4
(defconstant +lvo-wait-port+    -384)
(defconstant +lvo-get-msg+      -372)
(defconstant +lvo-reply-msg+    -378)

;;; ================================================================
;;; IDCMP class constants
;;; ================================================================

(defconstant +idcmp-sizeverify+     #x00000001)
(defconstant +idcmp-newsize+        #x00000002)
(defconstant +idcmp-refreshwindow+  #x00000004)
(defconstant +idcmp-mousebuttons+   #x00000008)
(defconstant +idcmp-mousemove+      #x00000010)
(defconstant +idcmp-gadgetdown+     #x00000020)
(defconstant +idcmp-gadgetup+       #x00000040)
(defconstant +idcmp-menupick+       #x00000100)
(defconstant +idcmp-closewindow+    #x00000200)
(defconstant +idcmp-rawkey+         #x00000400)
(defconstant +idcmp-vanillakey+     #x00200000)

;;; ================================================================
;;; Window flag constants
;;; ================================================================

(defconstant +wflg-sizegadget+      #x0001)
(defconstant +wflg-dragbar+         #x0002)
(defconstant +wflg-depthgadget+     #x0004)
(defconstant +wflg-closegadget+     #x0008)
(defconstant +wflg-smart-refresh+   #x0000)
(defconstant +wflg-simple-refresh+  #x0040)
(defconstant +wflg-backdrop+        #x0100)
(defconstant +wflg-borderless+      #x0800)
(defconstant +wflg-activate+        #x1000)
(defconstant +wflg-gimmezerozero+   #x0400)
(defconstant +wflg-reportmouse+     #x0004)
(defconstant +wflg-rmbtrap+         #x00010000)

;;; ================================================================
;;; Window tag constants (for OpenWindowTagList)
;;; ================================================================

(defconstant +tag-user+       #x80000000)
(defconstant +wa-dummy+       (+ +tag-user+ 99))   ; 0x80000063
(defconstant +wa-left+        (+ +wa-dummy+ #x01))
(defconstant +wa-top+         (+ +wa-dummy+ #x02))
(defconstant +wa-width+       (+ +wa-dummy+ #x03))
(defconstant +wa-height+      (+ +wa-dummy+ #x04))
(defconstant +wa-idcmp+       (+ +wa-dummy+ #x07))
(defconstant +wa-flags+       (+ +wa-dummy+ #x08))
(defconstant +wa-gadgets+     (+ +wa-dummy+ #x09))
(defconstant +wa-title+       (+ +wa-dummy+ #x0B))
(defconstant +wa-customscreen+ (+ +wa-dummy+ #x0D))

;;; ================================================================
;;; Struct layouts
;;; ================================================================

;;; struct Window (partial — key fields only)
;;; struct Window layout (from intuition/intuition.h):
;;;   0: NextWindow*    4: LeftEdge(W)  6: TopEdge(W)
;;;   8: Width(W)      10: Height(W)   32: Title*
;;;  50: RPort*         86: UserPort*
(ffi:defcstruct window
  (left-edge   :u16  4)
  (top-edge    :u16  6)
  (width       :u16  8)
  (height      :u16 10)
  (rport       :pointer 50)    ; Window->RPort
  (user-port   :pointer 86)    ; Window->UserPort (MsgPort for IDCMP)
  (title       :pointer 32))

;;; struct IntuiMessage (partial)
(ffi:defcstruct intui-message
  (class       :u32  20)   ; im->Class (IDCMP flags)
  (code        :u16  24)   ; im->Code
  (mouse-x     :u16  32)   ; im->MouseX
  (mouse-y     :u16  34))  ; im->MouseY

;;; ================================================================
;;; Window management
;;; ================================================================

(defun open-window (&key (title "CL-Amiga") (left 0) (top 0)
                         (width 320) (height 200) screen
                         (idcmp +idcmp-closewindow+)
                         (flags (logior +wflg-closegadget+
                                        +wflg-dragbar+
                                        +wflg-depthgadget+
                                        +wflg-sizegadget+
                                        +wflg-activate+)))
  "Open an Intuition window via OpenWindowTagList.
Returns a foreign pointer to the Window struct, or signals an error."
  (ffi:with-foreign-string (title-ptr title)
    (let ((tag-pairs (list +wa-left+ left
                           +wa-top+ top
                           +wa-width+ width
                           +wa-height+ height
                           +wa-idcmp+ idcmp
                           +wa-flags+ flags
                           +wa-title+ title-ptr)))
      (when screen
        (setf tag-pairs (append tag-pairs
                                (list +wa-customscreen+ screen))))
      (let* ((tags (amiga.ffi:make-tag-list tag-pairs))
             (result (amiga:call-library *intuition-base*
                                         +lvo-open-window-tag-list+
                                         (list :a0 (ffi:make-foreign-pointer 0)
                                               :a1 tags))))
        (ffi:free-foreign tags)
        (if (zerop result)
            (error "INTUITION:OPEN-WINDOW failed")
            (ffi:make-foreign-pointer result))))))

(defun close-window (window)
  "Close an Intuition window."
  (amiga:call-library *intuition-base* +lvo-close-window+
                      (list :a0 window))
  t)

(defmacro with-window ((var &rest args) &body body)
  "Open a window, bind to VAR, close on exit."
  `(let ((,var (open-window ,@args)))
     (unwind-protect
       (progn ,@body)
       (close-window ,var))))

;;; ================================================================
;;; Window accessors
;;; ================================================================

(defun window-rastport (window)
  "Get the RastPort pointer from a Window struct."
  (ffi:make-foreign-pointer (window-rport window)))

(defun window-left (window)   (window-left-edge window))
(defun window-top (window)    (window-top-edge window))

;;; window-width and window-height are generated by defcstruct

;;; ================================================================
;;; IDCMP message handling
;;; ================================================================

(defun wait-port (port)
  "Wait for a message on a message port (blocking).
Uses exec.library WaitPort."
  ;; Need ExecBase — read from absolute address 4
  (let ((exec (ffi:make-foreign-pointer
               (ffi:peek-u32 (ffi:make-foreign-pointer 4)))))
    (amiga:call-library exec +lvo-wait-port+
                        (list :a0 port))))

(defun get-msg (port)
  "Get next message from a port, or NIL if none available."
  (let* ((exec (ffi:make-foreign-pointer
                (ffi:peek-u32 (ffi:make-foreign-pointer 4))))
         (result (amiga:call-library exec +lvo-get-msg+
                                     (list :a0 port))))
    (if (zerop result) nil (ffi:make-foreign-pointer result))))

(defun reply-msg (msg)
  "Reply to a received message."
  (let ((exec (ffi:make-foreign-pointer
               (ffi:peek-u32 (ffi:make-foreign-pointer 4)))))
    (amiga:call-library exec +lvo-reply-msg+
                        (list :a1 msg))))

(defun msg-class (msg)
  "Get the IDCMP class from an IntuiMessage."
  (intui-message-class msg))

(defun msg-code (msg)
  "Get the Code field from an IntuiMessage."
  (intui-message-code msg))

(defun msg-mouse-x (msg)
  "Get MouseX from an IntuiMessage."
  (intui-message-mouse-x msg))

(defun msg-mouse-y (msg)
  "Get MouseY from an IntuiMessage."
  (intui-message-mouse-y msg))

;;; ================================================================
;;; Event loop
;;; ================================================================

(defun window-user-port (window)
  "Get the UserPort (IDCMP message port) from a Window."
  (ffi:make-foreign-pointer (window-user-port window)))

(defmacro event-loop (window &body clauses)
  "Process IDCMP messages for WINDOW until a clause returns :quit.
Each clause is (idcmp-class (msg) &body body).
Example:
  (event-loop win
    (#.+idcmp-closewindow+ (msg) (return))
    (#.+idcmp-rawkey+ (msg)
      (format t \"Key: ~A~%\" (msg-code msg))))"
  (let ((port (gensym "PORT"))
        (msg (gensym "MSG"))
        (class (gensym "CLASS")))
    `(let ((,port (window-user-port ,window)))
       (block nil
         (loop
           (wait-port ,port)
           (loop
             (let ((,msg (get-msg ,port)))
               (unless ,msg (return))  ; inner loop: no more messages
               (let ((,class (msg-class ,msg)))
                 (reply-msg ,msg)
                 (cond
                   ,@(mapcar
                      (lambda (clause)
                        (destructuring-bind (idcmp-val (msg-var) &body body) clause
                          `((eql ,class ,idcmp-val)
                            (let ((,msg-var ,msg))
                              (declare (ignorable ,msg-var))
                              ,@body))))
                      clauses))))))))))

;;; ================================================================
;;; Menu strip and gadget list management
;;; ================================================================

(defun set-menu-strip (window menu)
  "Attach a menu strip to a window."
  (amiga:call-library *intuition-base* +lvo-set-menu-strip+
                      (list :a0 window :a1 menu)))

(defun clear-menu-strip (window)
  "Remove the menu strip from a window."
  (amiga:call-library *intuition-base* +lvo-clear-menu-strip+
                      (list :a0 window)))

(defun add-gadget-list (window gadget-list)
  "Add a gadget list to a window. Returns position."
  (amiga:call-library *intuition-base* +lvo-add-g-list+
                      (list :a0 window :a1 gadget-list
                            :d0 -1  ; position = end
                            :d1 -1  ; numgad = all
                            :a2 (ffi:make-foreign-pointer 0))))  ; requester = NULL

(defun refresh-gadget-list (window gadget-list)
  "Refresh all gadgets in the list."
  (amiga:call-library *intuition-base* +lvo-refresh-g-list+
                      (list :a0 gadget-list :a1 window
                            :a2 (ffi:make-foreign-pointer 0)  ; requester = NULL
                            :d0 -1)))  ; numgad = all

;;; ================================================================
;;; Public screen management
;;; ================================================================

(defun lock-pub-screen (&optional name)
  "Lock a public screen by name (NIL = default/Workbench).
Returns a foreign pointer to the Screen, or NIL."
  (let ((result (amiga:call-library *intuition-base* +lvo-lock-pub-screen+
                                    (list :a0 (if name
                                                  (ffi:foreign-string name)
                                                  (ffi:make-foreign-pointer 0))))))
    (if (zerop result) nil (ffi:make-foreign-pointer result))))

(defun unlock-pub-screen (screen &optional name)
  "Unlock a previously locked public screen."
  (amiga:call-library *intuition-base* +lvo-unlock-pub-screen+
                      (list :a0 (if name
                                    (ffi:foreign-string name)
                                    (ffi:make-foreign-pointer 0))
                            :a1 screen)))

(defmacro with-pub-screen ((var &optional name) &body body)
  "Lock a public screen, bind to VAR, unlock on exit."
  `(let ((,var (lock-pub-screen ,name)))
     (unless ,var
       (error "Cannot lock public screen ~A" ,(or name "default")))
     (unwind-protect
       (progn ,@body)
       (unlock-pub-screen ,var ,name))))

;;; ================================================================
;;; Provide module
;;; ================================================================

(provide "amiga/intuition")
