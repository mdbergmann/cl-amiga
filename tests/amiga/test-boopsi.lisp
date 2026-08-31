;;; test-boopsi.lisp — Amiga-side tests of lib/amiga/boopsi.lisp
;;; (AMIGA.BOOPSI, the toolkit-neutral BOOPSI helpers that AMIGA.REACTION
;;; and AMIGA.MUI share).
;;;
;;; Loaded from run-tests.lisp via (load ...) inside #+amigaos, like
;;; test-reaction.lisp: CHECK comes from run-tests.lisp.  Nothing here
;;; needs a toolkit: the objects are instances of intuition's built-in
;;; public classes ("propgclass", "buttongclass"), which every AmigaOS
;;; since 2.0 has -- so this is the one place the BOOPSI layer is proven
;;; on a bare 3.1 with neither ReAction nor MUI installed.  The portable
;;; half (ULONG coercion, the pool, WITH-TAGS) is mirrored on the host by
;;; tests/test_amiga_boopsi.sh; test-reaction.lisp runs it again through
;;; the AMIGA.REACTION re-exports.

(require "amiga/boopsi")
(require "amiga/raw/intuition")
(format t "; boopsi: amiga/boopsi loaded~%")
(finish-output)

;;; --- portable half, on real 32-bit addresses ----------------------------

(check "boopsi-ulong-coercions" '(0 1 5 #xFFFFFFFF #x80000000)
  (list (amiga.boopsi::%ulong nil) (amiga.boopsi::%ulong t)
        (amiga.boopsi::%ulong 5) (amiga.boopsi::%ulong -1)
        (amiga.boopsi::%ulong #x80000000)))

(check "boopsi-pool-required" t
  (handler-case (progn (amiga.boopsi:pool-string "x") nil)
    (error (e) (and (search "WITH-FOREIGN-POOL" (format nil "~A" e)) t))))

(check "boopsi-pool-string-roundtrip" "BOOPSI"
  (amiga.boopsi:with-foreign-pool ()
    (ffi:foreign-to-string (amiga.boopsi:pool-string "BOOPSI"))))

;; WITH-TAGS: (tag value) pairs, strings pooled, TAG_DONE terminator --
;; here the pooled string's address fits the longword and reads back
(check "boopsi-with-tags-layout" '(#x80000001 7 #x80000002 "abc" #x80000003 #xFFFFFFFF 0)
  (amiga.boopsi:with-foreign-pool ()
    (amiga.boopsi:with-tags (tags #x80000001 7 #x80000002 "abc" #x80000003 -1)
      (list (ffi:peek-u32 tags 0) (ffi:peek-u32 tags 4)
            (ffi:peek-u32 tags 8)
            (ffi:foreign-to-string (ffi:make-foreign-pointer (ffi:peek-u32 tags 12)))
            (ffi:peek-u32 tags 16) (ffi:peek-u32 tags 20)
            (ffi:peek-u32 tags 24)))))

;; NewList() + exec: an initialised list is empty (RemHead -> NULL), and
;; FREE-LIST-NODES pops what AddTail put in
(check "boopsi-new-list-empty-then-three-nodes" '(nil 3 nil)
  (amiga.boopsi:with-foreign-pool ()
    (let ((l (amiga.boopsi:new-list))
          (freed 0))
      (list (amiga.raw.exec:rem-head l)
            (progn
              (dotimes (i 3)
                (amiga.raw.exec:add-tail l (amiga.boopsi:pool-alloc amiga.raw.exec:*node-size*)))
              (amiga.boopsi:free-list-nodes l (lambda (node) (declare (ignore node)) (incf freed))))
            (amiga.raw.exec:rem-head l)))))

;;; --- objects of the built-in intuition classes ---------------------------

;; NewObjectA(NULL, "propgclass", tags): a proportional gadget object that
;; never sees a window.  Of its attributes only PGA_Top is documented
;; gettable (ISGNU) on the classic intuition -- GetAttr(PGA_Top, ...) is
;; what every scroller program relies on; PGA_Visible / PGA_Total are ISU
;; there (OS 3.1/3.9 in FS-UAE answers NIL) while MorphOS's propgclass
;; happens to answer them too.  The checks stick to PGA_Top so they are a
;; specification of the layer, not of one OS's class.
(defun %boopsi-new-prop ()
  ;; the raw binding takes registers as integers / foreign pointers, so the
  ;; class ID goes over as a foreign string; NewObjectA looks the public
  ;; class up by it and does not keep the pointer, so it is freed right after
  (let ((class-id (ffi:foreign-string "propgclass")))
    (unwind-protect
         (amiga.boopsi:with-tags (array
                                  amiga.raw.intuition:+ga-left+ 0
                                  amiga.raw.intuition:+ga-top+ 0
                                  amiga.raw.intuition:+ga-width+ 16
                                  amiga.raw.intuition:+ga-height+ 100
                                  amiga.raw.intuition:+pga-freedom+ amiga.raw.intuition:+freevert+
                                  amiga.raw.intuition:+pga-total+ 200
                                  amiga.raw.intuition:+pga-visible+ 50
                                  amiga.raw.intuition:+pga-top+ 30)
           (amiga.raw.intuition:new-object-a nil class-id array))
      (ffi:free-foreign class-id))))

(check "boopsi-builtin-class-object-created" t
  (let ((obj (%boopsi-new-prop)))
    (unwind-protect
         (and obj (ffi:foreign-pointer-p obj) (not (ffi:null-pointer-p obj)))
      (when obj (amiga.raw.intuition:dispose-object obj)))))

;; OCLASS(obj) is a real IClass, and the class's own ID is what we asked
;; for: intuition's public classes are named, so IClass->cl_ID -- offset
;; 28 in amiga/raw/intuition's ICLASS struct table: cl_Dispatcher hook (20)
;; + cl_Reserved (4) + cl_Super (4) -- is the STRPTR "propgclass"
(check "boopsi-object-class-is-propgclass" "propgclass"
  (let ((obj (%boopsi-new-prop)))
    (unwind-protect
         (let* ((class (amiga.boopsi:object-class obj))
                (id (ffi:peek-pointer (ffi:pointer+ class 28))))
           (ffi:foreign-to-string id))
      (amiga.raw.intuition:dispose-object obj))))

;; GET-ATTR through intuition's GetAttr: PGA_Top as set at creation
(check "boopsi-get-attr-reads-prop-top" 30
  (let ((obj (%boopsi-new-prop)))
    (unwind-protect
         (amiga.boopsi:get-attr amiga.raw.intuition:+pga-top+ obj)
      (amiga.raw.intuition:dispose-object obj))))

;; an attribute the class does not know -> NIL (GetAttr returned 0)
(check "boopsi-get-attr-unknown-is-nil" nil
  (let ((obj (%boopsi-new-prop)))
    (unwind-protect
         (amiga.boopsi:get-attr #x8EADBEEF obj)
      (amiga.raw.intuition:dispose-object obj))))

;; SET-ATTRS (SetAttrsA with a NULL GadgetInfo -- the gadget is in no
;; window) changes what GET-ATTR reads back; the pool is not needed for
;; integer-only tag lists
(check "boopsi-set-attrs-then-get-attr" '(30 75)
  (let ((obj (%boopsi-new-prop)))
    (unwind-protect
         (let ((before (amiga.boopsi:get-attr amiga.raw.intuition:+pga-top+ obj)))
           (amiga.boopsi:set-attrs obj amiga.raw.intuition:+pga-top+ 75)
           (list before (amiga.boopsi:get-attr amiga.raw.intuition:+pga-top+ obj)))
      (amiga.raw.intuition:dispose-object obj))))

;; DO-METHOD builds the message from its arguments: OM_GET's is
;; { MethodID, opg_AttrID, opg_Storage } -- exactly (do-method obj OM_GET
;; attr storage).  The dispatcher answers TRUE and fills the storage.
(check "boopsi-do-method-om-get" '(1 30)
  (let ((obj (%boopsi-new-prop))
        (storage (ffi:alloc-foreign 4)))
    (unwind-protect
         (list (amiga.boopsi:do-method obj amiga.raw.intuition:+om-get+
                                       amiga.raw.intuition:+pga-top+ storage)
               (ffi:peek-u32 storage 0))
      (ffi:free-foreign storage)
      (amiga.raw.intuition:dispose-object obj))))

;; ... and OM_SET's is { MethodID, ops_AttrList, ops_GInfo }: the tag
;; array from WITH-TAGS as the attribute list, NIL for the GadgetInfo
(check "boopsi-do-method-om-set" 42
  (let ((obj (%boopsi-new-prop)))
    (unwind-protect
         (progn
           (amiga.boopsi:with-tags (tags amiga.raw.intuition:+pga-top+ 42)
             (amiga.boopsi:do-method obj amiga.raw.intuition:+om-set+ tags nil))
           (amiga.boopsi:get-attr amiga.raw.intuition:+pga-top+ obj))
      (amiga.raw.intuition:dispose-object obj))))

;; GET-ATTR-POINTER: NIL for an unknown attribute and for a zero value, a
;; foreign pointer carrying the longword otherwise.  No built-in class has
;; a gettable pointer attribute on the classic intuition (GA_UserData is
;; IS only), so the longword under test is PGA_Top itself: 0 -> NIL, 77 ->
;; a pointer whose address is 77
(check "boopsi-get-attr-pointer" '(nil nil 77)
  (let ((obj (%boopsi-new-prop)))
    (unwind-protect
         (list (amiga.boopsi:get-attr-pointer #x8EADBEEF obj)
               (progn
                 (amiga.boopsi:set-attrs obj amiga.raw.intuition:+pga-top+ 0)
                 (amiga.boopsi:get-attr-pointer amiga.raw.intuition:+pga-top+ obj))
               (progn
                 (amiga.boopsi:set-attrs obj amiga.raw.intuition:+pga-top+ 77)
                 (let ((p (amiga.boopsi:get-attr-pointer amiga.raw.intuition:+pga-top+ obj)))
                   (and p (ffi:foreign-pointer-address p)))))
      (amiga.raw.intuition:dispose-object obj))))

(format t "; boopsi: done~%")
(finish-output)
