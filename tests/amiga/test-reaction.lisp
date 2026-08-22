;;; test-reaction.lisp — Amiga-side tests of lib/amiga/reaction.lisp
;;; (AMIGA.REACTION, the ReAction / BOOPSI helpers over the raw class
;;; bindings).
;;;
;;; Loaded from run-tests.lisp via (load ...) inside #+amigaos, like
;;; test-raw-bindings.lisp: CHECK comes from run-tests.lisp.  The portable
;;; half (ULONG coercion, the foreign pool, NEW-LIST, WITH-TAGS, the
;;; error paths) runs everywhere and is mirrored on the host by
;;; tests/test_amiga_reaction.sh; the ReAction half needs the classes
;;; (OS 3.5+/3.2, MorphOS, the FS-UAE OS 3.9 Workbench) and is skipped —
;;; with an assertion that AVAILABLE-P says so — where they are absent.

(require "amiga/reaction")
(format t "; reaction: amiga/reaction loaded~%")
(finish-output)

;;; --- portable half -----------------------------------------------------

(check "reaction-ulong-coercions" '(0 1 5 #xFFFFFFFF #x80000000)
  (list (amiga.reaction::%ulong nil) (amiga.reaction::%ulong t)
        (amiga.reaction::%ulong 5) (amiga.reaction::%ulong -1)
        (amiga.reaction::%ulong #x80000000)))

(check "reaction-ulong-pointer-is-its-address" t
  (let ((p (ffi:alloc-foreign 4)))
    (prog1 (= (amiga.reaction::%ulong p) (ffi:foreign-pointer-address p))
      (ffi:free-foreign p))))

(check "reaction-ulong-rejects-strings-outside-tags" t
  (handler-case (progn (amiga.reaction::%ulong "no") nil)
    (error () t)))

;; pool-string / pool-alloc need an enclosing pool, and free on exit
(check "reaction-pool-required" t
  (handler-case (progn (amiga.reaction:pool-string "x") nil)
    (error (e) (and (search "WITH-FOREIGN-POOL" (format nil "~A" e)) t))))

(check "reaction-pool-string-roundtrip" "ReAction"
  (amiga.reaction:with-foreign-pool ()
    (ffi:foreign-to-string (amiga.reaction:pool-string "ReAction"))))

(check "reaction-pool-alloc-zeroed" 0
  (amiga.reaction:with-foreign-pool ()
    (let ((p (amiga.reaction:pool-alloc 16)))
      (logior (ffi:peek-u32 p 0) (ffi:peek-u32 p 12)))))

;; NewList(): lh_Head = &lh_Tail, lh_Tail = 0, lh_TailPred = &lh_Head
(check "reaction-new-list-initialised" '(4 0 0)
  (amiga.reaction:with-foreign-pool ()
    (let* ((l (amiga.reaction:new-list))
           (addr (ffi:foreign-pointer-address l)))
      (list (logand (- (ffi:peek-u32 l 0) addr) #xFFFFFFFF)
            (ffi:peek-u32 l 4)
            (logand (- (ffi:peek-u32 l 8) addr) #xFFFFFFFF)))))

;; an initialised list is empty for exec: RemHead -> NULL
(check "reaction-new-list-is-empty-for-exec" nil
  (amiga.reaction:with-foreign-pool ()
    (amiga.raw.exec:rem-head (amiga.reaction:new-list))))

;; exec nodes through the list: AddTail x3, FREE-LIST-NODES pops them all
(check "reaction-free-list-nodes-count" '(3 nil)
  (amiga.reaction:with-foreign-pool ()
    (let ((l (amiga.reaction:new-list))
          (freed 0))
      (dotimes (i 3)
        (amiga.raw.exec:add-tail l (amiga.reaction:pool-alloc amiga.raw.exec:*node-size*)))
      (list (amiga.reaction:free-list-nodes l (lambda (node) (declare (ignore node)) (incf freed)))
            (amiga.raw.exec:rem-head l)))))

;; WITH-TAGS: (tag value) pairs, strings pooled, TAG_DONE terminator
(check "reaction-with-tags-layout" '(#x80000001 7 #x80000002 "abc" #x80000003 #xFFFFFFFF 0)
  (amiga.reaction:with-foreign-pool ()
    (amiga.reaction:with-tags (tags #x80000001 7 #x80000002 "abc" #x80000003 -1)
      (list (ffi:peek-u32 tags 0) (ffi:peek-u32 tags 4)
            (ffi:peek-u32 tags 8)
            (ffi:foreign-to-string (ffi:make-foreign-pointer (ffi:peek-u32 tags 12)))
            (ffi:peek-u32 tags 16) (ffi:peek-u32 tags 20)
            (ffi:peek-u32 tags 24)))))

(check "reaction-with-tags-odd-length-is-error" t
  (handler-case (amiga.reaction:with-tags (tags 1 2 3) (declare (ignore tags)) nil)
    (error () t)))

(check "reaction-new-object-rejects-nil-class" t
  (handler-case (progn (amiga.reaction:new-object nil 1 2) nil)
    (error (e) (and (search "class pointer" (format nil "~A" e)) t))))

;;; --- ReAction half ------------------------------------------------------

(defvar *reaction-p* (amiga.reaction:available-p))
(format t "; reaction: classes ~:[absent — skipping the class checks~;present~]~%" *reaction-p*)
(finish-output)

(check "reaction-available-p-matches-window-class" t
  (eq (if *reaction-p* t nil)
      (let ((b (amiga:open-library "window.class" 0)))
        (when b (amiga:close-library b))
        (if b t nil))))

(when *reaction-p*
  (require "amiga/raw/classes/window")
  (require "amiga/raw/gadgets/layout")
  (require "amiga/raw/gadgets/button")
  (require "amiga/raw/gadgets/integer")
  (format t "; reaction: class modules loaded~%")
  (finish-output))

(defun %ra-sym (pkg name)
  (or (find-symbol name pkg) (error "~A::~A not found" pkg name)))
(defun %ra-val (pkg name) (symbol-value (%ra-sym pkg name)))

;; the module's private copies of the window.class values must agree
;; with the generated ones
(check "reaction-constants-match-generated" t
  (if *reaction-p*
      (and (= amiga.reaction::+wm-handleinput+ (%ra-val "AMIGA.RAW.CLASSES.WINDOW" "+WM-HANDLEINPUT+"))
           (= amiga.reaction::+wm-open+ (%ra-val "AMIGA.RAW.CLASSES.WINDOW" "+WM-OPEN+"))
           (= amiga.reaction::+wm-close+ (%ra-val "AMIGA.RAW.CLASSES.WINDOW" "+WM-CLOSE+"))
           (= amiga.reaction::+wm-iconify+ (%ra-val "AMIGA.RAW.CLASSES.WINDOW" "+WM-ICONIFY+"))
           (= amiga.reaction::+window-sig-mask+ (%ra-val "AMIGA.RAW.CLASSES.WINDOW" "+WINDOW-SIG-MASK+"))
           (= amiga.reaction::+wmhi-lastmsg+ (%ra-val "AMIGA.RAW.CLASSES.WINDOW" "+WMHI-LASTMSG+"))
           t)
      t))

(defvar *ra-button* nil)
(defvar *ra-window* nil)

;; a button: NEW-OBJECT with an integer, a T and a string tag; OCLASS is
;; the class BUTTON_GetClass returned.  (GA_ID / GA_Disabled are not
;; gettable from button.gadget — GetAttr returns 0 for them — so the
;; attribute round trip is checked on integer.gadget below.)
(check "reaction-new-object-button-and-object-class" '(t t)
  (if *reaction-p*
      (amiga.reaction:with-foreign-pool ()
        (let* ((cls (funcall (%ra-sym "AMIGA.RAW.GADGETS.BUTTON" "BUTTON-GET-CLASS")))
               (btn (amiga.reaction:new-object cls
                                               amiga.raw.intuition:+ga-id+ 7
                                               amiga.raw.intuition:+ga-rel-verify+ t
                                               amiga.raw.intuition:+ga-text+ "Seven")))
          (prog1 (list (ffi:pointer-eq (amiga.reaction:object-class btn) cls)
                       (ffi:foreign-pointer-p btn))
            (amiga.reaction:dispose-object btn))))
      '(t t)))

;; GET-ATTR / SET-GADGET-ATTRS round trip on a gettable attribute:
;; INTEGER_Number (ISGNU) of integer.gadget
(check "reaction-get-attr-set-gadget-attrs-integer" '(42 7)
  (if *reaction-p*
      (amiga.reaction:with-foreign-pool ()
        (let* ((number (%ra-val "AMIGA.RAW.GADGETS.INTEGER" "+INTEGER-NUMBER+"))
               (obj (amiga.reaction:new-object
                     (funcall (%ra-sym "AMIGA.RAW.GADGETS.INTEGER" "INTEGER-GET-CLASS"))
                     amiga.raw.intuition:+ga-id+ 1
                     number 42)))
          (unwind-protect
               (list (amiga.reaction:get-attr number obj)
                     (progn (amiga.reaction:set-gadget-attrs obj nil number 7)
                            (amiga.reaction:get-attr number obj)))
            (amiga.reaction:dispose-object obj))))
      '(42 7)))

;; an unknown attribute -> NIL, not an error
(check "reaction-get-attr-unknown-is-nil" nil
  (if *reaction-p*
      (amiga.reaction:with-foreign-pool ()
        (let ((btn (amiga.reaction:new-object
                    (funcall (%ra-sym "AMIGA.RAW.GADGETS.BUTTON" "BUTTON-GET-CLASS"))
                    amiga.raw.intuition:+ga-id+ 1)))
          (prog1 (amiga.reaction:get-attr #x8FFF0001 btn)
            (amiga.reaction:dispose-object btn))))
      nil))

;; the full life of a window.class window: open, attributes, an empty
;; input queue, a bounded event loop, SetGadgetAttrs, close, dispose
(check "reaction-window-open-handle-input-close" '(t t 0 0 :looped t t)
  (if *reaction-p*
      (amiga.reaction:with-foreign-pool ()
        (let* ((layout-cls (funcall (%ra-sym "AMIGA.RAW.GADGETS.LAYOUT" "LAYOUT-GET-CLASS")))
               (button-cls (funcall (%ra-sym "AMIGA.RAW.GADGETS.BUTTON" "BUTTON-GET-CLASS")))
               (win-cls (funcall (%ra-sym "AMIGA.RAW.CLASSES.WINDOW" "WINDOW-GET-CLASS")))
               (btn (amiga.reaction:new-object button-cls
                                               amiga.raw.intuition:+ga-id+ 5
                                               amiga.raw.intuition:+ga-rel-verify+ t
                                               amiga.raw.intuition:+ga-text+ "Test"))
               (win-obj (amiga.reaction:new-object
                         win-cls
                         amiga.raw.intuition:+wa-title+ "AMIGA.REACTION test"
                         amiga.raw.intuition:+wa-left+ 30
                         amiga.raw.intuition:+wa-top+ 30
                         amiga.raw.intuition:+wa-drag-bar+ t
                         amiga.raw.intuition:+wa-close-gadget+ t
                         (%ra-val "AMIGA.RAW.CLASSES.WINDOW" "+WINDOW-PARENT-GROUP+")
                         (amiga.reaction:new-object
                          layout-cls
                          (%ra-val "AMIGA.RAW.GADGETS.LAYOUT" "+LAYOUT-ORIENTATION+")
                          (%ra-val "AMIGA.RAW.GADGETS.LAYOUT" "+LAYOUT-VERTICAL+")
                          (%ra-val "AMIGA.RAW.GADGETS.LAYOUT" "+LAYOUT-ADD-CHILD+") btn))))
          (unwind-protect
               (let* ((window (amiga.reaction:open-window win-obj))
                      (sigmask (amiga.reaction:window-signal-mask win-obj))
                      (looped nil))
                 (multiple-value-bind (result code) (amiga.reaction:handle-input win-obj)
                   ;; nobody clicked: the queue is empty
                   (amiga.reaction:do-window-events ((r c) win-obj :timeout 0.5)
                     (declare (ignorable r c))
                     (setq looped :event))
                   (setq looped (or looped :looped))
                   (list (and window (ffi:foreign-pointer-p window) t)
                         (and (integerp sigmask) (plusp sigmask))
                         result code
                         looped
                         ;; SetGadgetAttrs on the displayed button: a new
                         ;; label, disabled — returns, the window repaints
                         (progn
                           (amiga.reaction:set-gadget-attrs btn window
                                                            amiga.raw.intuition:+ga-text+ "Changed"
                                                            amiga.raw.intuition:+ga-disabled+ t)
                           t)
                         ;; WINDOW_Window is the struct Window we were given
                         (ffi:pointer-eq
                          (amiga.reaction:get-attr-pointer
                           (%ra-val "AMIGA.RAW.CLASSES.WINDOW" "+WINDOW-WINDOW+") win-obj)
                          window))))
            (amiga.reaction:close-window win-obj)
            (amiga.reaction:dispose-object win-obj))))
      '(t t 0 0 :looped 1 t)))

;; a raw method through DO-METHOD: WM_CLOSE on a never-opened window is
;; harmless and returns; WM_OPEN via DO-METHOD is what OPEN-WINDOW does
(check "reaction-do-method-open-close" t
  (if *reaction-p*
      (amiga.reaction:with-foreign-pool ()
        (let ((win-obj (amiga.reaction:new-object
                        (funcall (%ra-sym "AMIGA.RAW.CLASSES.WINDOW" "WINDOW-GET-CLASS"))
                        amiga.raw.intuition:+wa-title+ "DO-METHOD"
                        amiga.raw.intuition:+wa-width+ 120
                        amiga.raw.intuition:+wa-height+ 60)))
          (unwind-protect
               (let ((w (amiga.reaction:do-method win-obj amiga.reaction::+wm-open+)))
                 (prog1 (and (integerp w) (plusp w))
                   (amiga.reaction:do-method win-obj amiga.reaction::+wm-close+)))
            (amiga.reaction:dispose-object win-obj))))
      t))
