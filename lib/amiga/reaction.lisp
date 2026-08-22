;;; amiga/reaction.lisp — ReAction / BOOPSI helpers for CL-Amiga
;;;
;;; Loaded via (require "amiga/reaction").
;;;
;;; ReAction (the OS 3.5+/3.2 and MorphOS GUI toolkit) is a set of
;;; BOOPSI class libraries — window.class, gadgets/layout.gadget,
;;; gadgets/button.gadget, ... — driven through intuition.library's
;;; NewObjectA / SetAttrsA / GetAttr / DisposeObject and through object
;;; METHODS.  The generated raw bindings (lib/amiga/raw/classes/,
;;; gadgets/, images/) already provide every class's tags, method IDs and
;;; the few real library functions they export.  What a C program gets
;;; from amiga.lib / reaction.lib — DoMethod(), the RA_OpenWindow /
;;; RA_HandleInput macros, NewList(), string literals that live as long
;;; as the objects that reference them — is what this module adds:
;;;
;;;   DO-METHOD          IDoMethodA: CallHookPkt on the object's class
;;;                      dispatcher (OCLASS(obj) = the IClass whose first
;;;                      member is the dispatcher Hook)
;;;   NEW-OBJECT         NewObjectA with a Lisp tag plist — integers,
;;;                      foreign pointers, T/NIL and strings, the strings
;;;                      copied into a WITH-FOREIGN-POOL that outlives the
;;;                      objects
;;;   GET-ATTR / SET-ATTRS / SET-GADGET-ATTRS
;;;   OPEN-WINDOW / CLOSE-WINDOW / ICONIFY / HANDLE-INPUT
;;;                      the RA_* macros as functions
;;;   DO-WINDOW-EVENTS   the canonical Wait()/RA_HandleInput event loop,
;;;                      with an optional timeout for unattended runs
;;;   OPEN-REQUESTER     requester.class RM_OPENREQ
;;;   NEW-LIST / FREE-LIST-NODES   exec list headers for label lists
;;;
;;; The module itself loads on any system (including the host and an
;;; AmigaOS 3.1 without ReAction): it only depends on exec/dos/utility/
;;; intuition.  AVAILABLE-P tells whether the classes can actually be
;;; opened; the per-class raw modules (amiga/raw/classes/window, ...)
;;; open their class at REQUIRE time and fail with a clear error where
;;; it is missing.
;;;
;;; See examples/amiga/reaction/ for ports of the NDK 3.2 ReAction
;;; examples, and tests/amiga/test-reaction.lisp for the executable
;;; specification of this module.

(require "amiga/ffi")
(require "amiga/raw/exec")
(require "amiga/raw/dos")
(require "amiga/raw/utility")
(require "amiga/raw/intuition")

(defpackage "AMIGA.REACTION"
  (:use "CL" "FFI" "AMIGA.FFI")
  (:export
   ;; Availability
   "AVAILABLE-P"
   ;; Methods
   "DO-METHOD" "OBJECT-CLASS"
   ;; Foreign memory whose lifetime is the GUI's lifetime
   "WITH-FOREIGN-POOL" "POOL-ALLOC" "POOL-STRING" "NEW-LIST" "FREE-LIST-NODES"
   "WITH-TAGS"
   ;; Objects and attributes
   "NEW-OBJECT" "DISPOSE-OBJECT" "GET-ATTR" "GET-ATTR-POINTER"
   "SET-ATTRS" "SET-GADGET-ATTRS"
   ;; window.class
   "OPEN-WINDOW" "CLOSE-WINDOW" "ICONIFY" "HANDLE-INPUT" "WINDOW-SIGNAL-MASK"
   "DO-WINDOW-EVENTS" "*EVENT-LOOP-TIMEOUT*"
   ;; requester.class
   "OPEN-REQUESTER"))

(in-package "AMIGA.REACTION")

;;; ================================================================
;;; Constants
;;; ================================================================

;;; window.class / requester.class values this module needs itself.
;;; They are duplicated from the generated amiga/raw/classes/window and
;;; amiga/raw/classes/requester modules on purpose: those open their
;;; class library at REQUIRE time, and this module must load (and
;;; AVAILABLE-P must answer) on systems where the classes are absent.
;;; tests/amiga/test-reaction.lisp cross-checks them against the
;;; generated constants.

(defconstant +wm-handleinput+  #x570001)     ; classes/window.h WM_HANDLEINPUT
(defconstant +wm-open+         #x570002)     ; WM_OPEN
(defconstant +wm-close+        #x570003)     ; WM_CLOSE
(defconstant +wm-iconify+      #x570005)     ; WM_ICONIFY
(defconstant +window-sig-mask+ #x85025002)   ; WINDOW_SigMask
(defconstant +wmhi-lastmsg+    0)            ; WMHI_LASTMSG
(defconstant +rm-openreq+      #x650001)     ; classes/requester.h RM_OPENREQ
(defconstant +tag-done+        0)            ; utility/tagitem.h TAG_DONE

;;; ================================================================
;;; Availability
;;; ================================================================

(defun available-p ()
  "True when the ReAction classes can be opened on this system: an
AmigaOS/MorphOS runtime whose window.class opens.  NIL on the host and on
an AmigaOS without ReAction (a bare 3.1), where the per-class raw
modules would fail to REQUIRE."
  (and (member :amigaos *features*)
       (let ((base (amiga:open-library "window.class" 0)))
         (when base
           (amiga:close-library base)
           t))))

;;; ================================================================
;;; ULONG coercion — what may stand for a tag value / message longword
;;; ================================================================

(defun %ulong (value)
  "Coerce VALUE to the unsigned 32-bit longword the OS sees: integers
\(negative ones two's-complement wrapped), foreign pointers (their
address), T/NIL (TRUE/FALSE or a NULL pointer)."
  (cond ((null value) 0)
        ((eq value t) 1)
        ((integerp value) (logand value #xFFFFFFFF))
        ((ffi:foreign-pointer-p value) (ffi:foreign-pointer-address value))
        (t (error "AMIGA.REACTION: ~S cannot be passed as a ULONG — expected an integer, a foreign pointer, a string (tag values only), T or NIL"
                  value))))

;;; ================================================================
;;; Foreign pool — allocations that must outlive a call
;;; ================================================================

;;; BOOPSI objects keep the POINTERS they are given: GA_Text, the
;;; LABEL_Text pieces, CHOOSER_Labels lists, REQS_Buffer... all must stay
;;; valid until the object is disposed.  A C program gets that for free
;;; from string literals and statics; here WITH-FOREIGN-POOL collects
;;; the foreign copies and frees them when the GUI's dynamic extent ends.

(defvar *foreign-pool* nil
  "The innermost WITH-FOREIGN-POOL: a cons whose CAR is the list of
foreign pointers to free on exit, or NIL outside any pool.")

(defmacro with-foreign-pool (() &body body)
  "Run BODY with a fresh foreign pool; every POOL-ALLOC / POOL-STRING /
NEW-LIST made inside (directly or through NEW-OBJECT, SET-ATTRS,
SET-GADGET-ATTRS and OPEN-REQUESTER string tag values) is freed when
BODY exits, normally or not.  Wrap the whole life of a GUI in one."
  `(let ((*foreign-pool* (list nil)))
     (unwind-protect
          (progn ,@body)
       (dolist (p (car *foreign-pool*))
         (ffi:free-foreign p)))))

(defun %pool-register (pointer)
  (unless *foreign-pool*
    (error "AMIGA.REACTION: a foreign allocation with the GUI's lifetime was requested outside WITH-FOREIGN-POOL — wrap the code that builds and runs the GUI in (AMIGA.REACTION:WITH-FOREIGN-POOL () ...)"))
  (push pointer (car *foreign-pool*))
  pointer)

(defun pool-alloc (size)
  "Allocate SIZE zeroed bytes of foreign memory that live until the
enclosing WITH-FOREIGN-POOL exits."
  (%pool-register (ffi:alloc-foreign size)))

(defun pool-string (string)
  "A NUL-terminated foreign copy of STRING that lives until the
enclosing WITH-FOREIGN-POOL exits."
  (%pool-register (ffi:foreign-string string)))

(defun new-list ()
  "A fresh, initialised exec struct List (what amiga.lib's NewList()
leaves behind), allocated in the enclosing WITH-FOREIGN-POOL.  Feed it
the nodes of a CHOOSER_Labels / CLICKTAB_Labels / LISTBROWSER_Labels
list with AMIGA.RAW.EXEC:ADD-TAIL."
  (let* ((list (pool-alloc amiga.raw.exec:*list-size*))
         (addr (ffi:foreign-pointer-address list)))
    ;; lh_Head = &lh_Tail, lh_Tail = NULL, lh_TailPred = &lh_Head
    (ffi:poke-u32 list (+ addr 4) 0)
    (ffi:poke-u32 list 0 4)
    (ffi:poke-u32 list addr 8)
    list))

(defun free-list-nodes (list free-node)
  "Remove every node from the exec LIST (RemHead until empty) and call
FREE-NODE on each — e.g. AMIGA.RAW.GADGETS.CHOOSER:FREE-CHOOSER-NODE.
Returns the number of nodes freed."
  (loop for node = (amiga.raw.exec:rem-head list)
        while node
        count (progn (funcall free-node node) t)))

;;; ================================================================
;;; Tag lists with Lisp values
;;; ================================================================

(defun %build-tags (tags)
  "Allocate a TagItem array from the plist TAGS (tag value ...).  Values go
through %ULONG; strings are copied into the foreign pool, because the
object the list is handed to keeps the pointer.  Returns the array; the
caller frees it (the ARRAY is not retained by NewObjectA / SetAttrsA)."
  (unless (evenp (length tags))
    (error "AMIGA.REACTION: odd-length tag list ~S — expected (tag value ...) pairs" tags))
  (let* ((n (floor (length tags) 2))
         (array (ffi:alloc-foreign (* 8 (1+ n))))
         (ok nil))
    (unwind-protect
         (progn
           (loop for (tag value) on tags by #'cddr
                 for offset from 0 by 8
                 do (unless (integerp tag)
                      (error "AMIGA.REACTION: tag ~S is not an integer (value ~S) — the tag constants come from the amiga/raw/... modules" tag value))
                    (ffi:poke-u32 array (logand tag #xFFFFFFFF) offset)
                    (ffi:poke-u32 array
                                  (if (stringp value)
                                      (ffi:foreign-pointer-address (pool-string value))
                                      (%ulong value))
                                  (+ offset 4)))
           (ffi:poke-u32 array +tag-done+ (* 8 n))
           (ffi:poke-u32 array 0 (+ (* 8 n) 4))
           (setf ok t)
           array)
      (unless ok (ffi:free-foreign array)))))

(defmacro %with-tags ((var tags) &body body)
  `(let ((,var (%build-tags ,tags)))
     (unwind-protect (progn ,@body)
       (ffi:free-foreign ,var))))

(defmacro with-tags ((var &rest tags) &body body)
  "Bind VAR to a TagItem array built from the TAGS plist (tag value
...) for the extent of BODY — for the class-library functions that take
a tag list themselves: AllocListBrowserNodeA, AllocChooserNodeA,
AllocClickTabNodeA, GetListBrowserNodeAttrsA...  Same value rules as
NEW-OBJECT (string values are copied into the enclosing
WITH-FOREIGN-POOL); the array is freed on exit."
  `(%with-tags (,var (list ,@tags))
     ,@body))

;;; ================================================================
;;; Methods
;;; ================================================================

(defun object-class (object)
  "OCLASS(object): the IClass of a BOOPSI object.  The object pointer
points just past its struct _Object header, whose last longword is
o_Class."
  (ffi:peek-pointer (ffi:pointer+ object -4)))

(defun do-method (object method-id &rest args)
  "IDoMethodA: invoke METHOD-ID on the BOOPSI OBJECT.  ARGS become the
longwords following the MethodID in the message (integers, foreign
pointers, T/NIL).  Returns the dispatcher's d0 as an unsigned integer —
wrap it with FFI:MAKE-FOREIGN-POINTER when the method returns a pointer
\(OPEN-WINDOW does).  A class's dispatcher is the Hook at the start of
its IClass, so this is CallHookPkt(OCLASS(obj), obj, msg) — exactly what
amiga.lib's DoMethodA does."
  (let* ((n (length args))
         (msg (ffi:alloc-foreign (* 4 (1+ n)))))
    (unwind-protect
         (progn
           (ffi:poke-u32 msg (logand method-id #xFFFFFFFF) 0)
           (loop for a in args
                 for offset from 4 by 4
                 do (ffi:poke-u32 msg (%ulong a) offset))
           (amiga.raw.utility:call-hook-pkt (object-class object) object msg))
      (ffi:free-foreign msg))))

;;; ================================================================
;;; Objects and attributes
;;; ================================================================

(defun new-object (class &rest tags)
  "NewObjectA(CLASS, NULL, TAGS): create a BOOPSI object.  CLASS is the
Class pointer a raw module's xxx-GET-CLASS returns (BUTTON-GET-CLASS,
LAYOUT-GET-CLASS, WINDOW-GET-CLASS...).  TAGS is a plist of tag constant
and value; integer, foreign-pointer, T/NIL and string values are
accepted, strings being copied into the enclosing WITH-FOREIGN-POOL
\(the object keeps the pointer).  Returns the object as a foreign
pointer; signals an error when the class returns NULL (out of memory, a
missing required attribute) — a NULL child silently handed on to a
layout is worse than an error here."
  (unless (and class (ffi:foreign-pointer-p class) (not (ffi:null-pointer-p class)))
    (error "AMIGA.REACTION:NEW-OBJECT: ~S is not a class pointer — pass what the class module's xxx-GET-CLASS returns (is that module loaded on an AmigaOS/MorphOS with ReAction?)"
           class))
  (or (%with-tags (array tags)
        (amiga.raw.intuition:new-object-a class nil array))
      (error "AMIGA.REACTION:NEW-OBJECT: NewObjectA returned NULL for class #x~X with ~D tags — out of memory, or a required attribute is missing/invalid"
             (ffi:foreign-pointer-address class) (floor (length tags) 2))))

(defun dispose-object (object)
  "DisposeObject(OBJECT) — disposes the object and, for a window or
layout object, everything attached to it.  NIL is ignored."
  (when object
    (amiga.raw.intuition:dispose-object object))
  nil)

(defun get-attr (attribute object)
  "GetAttr(ATTRIBUTE, OBJECT): the attribute's current value as an
unsigned integer, or NIL if the object does not know the attribute."
  (let ((storage (ffi:alloc-foreign 4)))
    (unwind-protect
         (if (zerop (amiga.raw.intuition:get-attr attribute object storage))
             nil
             (ffi:peek-u32 storage 0))
      (ffi:free-foreign storage))))

(defun get-attr-pointer (attribute object)
  "GET-ATTR for a pointer-valued attribute (WINDOW_Window, ...): a
foreign pointer, or NIL for NULL / unknown."
  (let ((value (get-attr attribute object)))
    (if (or (null value) (zerop value))
        nil
        (ffi:make-foreign-pointer value))))

(defun set-attrs (object &rest tags)
  "SetAttrsA(OBJECT, TAGS) — same value rules as NEW-OBJECT.  Returns
the class's result (non-zero usually means a visual refresh is due)."
  (%with-tags (array tags)
    (amiga.raw.intuition:set-attrs-a object array)))

(defun set-gadget-attrs (gadget window &rest tags)
  "SetGadgetAttrsA(GADGET, WINDOW, NULL, TAGS): change attributes of a
gadget that is displayed in WINDOW, refreshing it.  WINDOW may be NIL
for a gadget that is not (yet) in a window.  Same value rules as
NEW-OBJECT."
  (%with-tags (array tags)
    (amiga.raw.intuition:set-gadget-attrs-a gadget window nil array)))

;;; ================================================================
;;; window.class
;;; ================================================================

(defun open-window (window-object)
  "RA_OpenWindow: open (or re-open after ICONIFY) the window.class
WINDOW-OBJECT.  Returns the struct Window as a foreign pointer, or NIL."
  (let ((window (do-method window-object +wm-open+)))
    (if (zerop window)
        nil
        (ffi:make-foreign-pointer window))))

(defun close-window (window-object)
  "RA_CloseWindow: close the window of WINDOW-OBJECT without disposing
the object (DISPOSE-OBJECT closes it as well)."
  (do-method window-object +wm-close+)
  nil)

(defun iconify (window-object)
  "RA_Iconify: close the window and put its icon on the Workbench; needs
WINDOW_IconifyGadget / WINDOW_AppPort at creation.  True on success."
  (not (zerop (do-method window-object +wm-iconify+))))

(defun window-signal-mask (window-object)
  "WINDOW_SigMask: the signals to Wait() on for this window's events
\(0 while the window is closed or iconified)."
  (or (get-attr +window-sig-mask+ window-object) 0))

(defun handle-input (window-object)
  "RA_HandleInput: process the next pending input message of
WINDOW-OBJECT.  Returns two values: the WMHI_* result (class bits in
WMHI_CLASSMASK, the gadget ID in WMHI_GADGETMASK, WMHI_LASTMSG = 0 when
nothing is pending) and the message's Code word (a checkbox's new state,
a raw key, ...)."
  (let ((code (ffi:alloc-foreign 2)))
    (unwind-protect
         (values (do-method window-object +wm-handleinput+ code)
                 (ffi:peek-u16 code 0))
      (ffi:free-foreign code))))

(defvar *event-loop-timeout* nil
  "Default :TIMEOUT of DO-WINDOW-EVENTS, in seconds; NIL = interactive
\(block until the user closes the window).  Set it before loading a GUI
program to run it unattended — the examples' harness and the test suite
do:  --eval '(require \"amiga/reaction\")'
     --eval '(setf amiga.reaction:*event-loop-timeout* 5)' --load ...")

(defmacro do-window-events (((result code) window-object
                             &key (timeout '*event-loop-timeout*) signals)
                            &body body)
  "The ReAction event loop.  Waits on WINDOW-OBJECT's WINDOW_SigMask
\(plus SIGBREAKF_CTRL_C and the extra SIGNALS mask, e.g. the signal of a
WINDOW_AppPort) and runs BODY once per input message with RESULT bound to
the WMHI_* result and CODE to the message Code — see HANDLE-INPUT.  BODY
calls (RETURN) to leave the loop; Ctrl-C leaves it too.

With TIMEOUT (seconds; defaults to *EVENT-LOOP-TIMEOUT*) the loop polls
instead of blocking and returns when the time is up — for unattended
runs (test suites, screenshots), where nobody will click the close
gadget."
  (let ((win (gensym "WIN")) (deadline (gensym "DEADLINE"))
        (extra (gensym "EXTRA")) (sigs (gensym "SIGS")))
    `(let* ((,win ,window-object)
            (,extra (or ,signals 0))
            (,deadline (let ((timeout ,timeout))
                         (and timeout
                              (+ (get-internal-real-time)
                                 (round (* timeout internal-time-units-per-second)))))))
       (block nil
         (tagbody
          :wait
            (cond (,deadline
                   (when (>= (get-internal-real-time) ,deadline)
                     (return))
                   (amiga.raw.dos:delay 5))
                  (t
                   (let ((,sigs (amiga.raw.exec:wait
                                 (logior (window-signal-mask ,win) ,extra
                                         amiga.raw.dos:+sigbreakf-ctrl-c+))))
                     (when (logtest ,sigs amiga.raw.dos:+sigbreakf-ctrl-c+)
                       (return)))))
          :next
            (multiple-value-bind (,result ,code) (handle-input ,win)
              (declare (ignorable ,code))
              (when (= ,result +wmhi-lastmsg+)
                (go :wait))
              ,@body)
            (go :next))))))

;;; ================================================================
;;; requester.class
;;; ================================================================

(defun open-requester (requester window &rest tags)
  "RM_OPENREQ: open the requester.class object REQUESTER over WINDOW
\(a struct Window, or NIL for the default public screen) with the
REQ_* / REQS_* / REQI_* attributes in TAGS, and wait for the user.
Returns the number of the gadget chosen (1 = leftmost, 0 = the
rightmost / cancel), as the C OpenRequesterTags pattern does."
  (%with-tags (array tags)
    (let ((msg (ffi:alloc-foreign 16)))
      (unwind-protect
           (progn
             ;; struct orRequest { MethodID; or_Attrs; or_Window; or_Screen; }
             (ffi:poke-u32 msg +rm-openreq+ 0)
             (ffi:poke-u32 msg (ffi:foreign-pointer-address array) 4)
             (ffi:poke-u32 msg (%ulong window) 8)
             (ffi:poke-u32 msg 0 12)
             (amiga.raw.utility:call-hook-pkt (object-class requester) requester msg))
        (ffi:free-foreign msg)))))

(provide "amiga/reaction")
