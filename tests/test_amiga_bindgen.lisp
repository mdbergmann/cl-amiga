;;; test_amiga_bindgen.lisp — checks for tests/test_amiga_bindgen.sh.
;;;
;;; Two modes, selected by the BINDGEN_CHECK environment variable:
;;;
;;;   fixture    the generator was run on tests/fixtures/bindgen/; RAWDIR
;;;              holds its output.  Pins the generator's behaviour: LVOs,
;;;              register encodings, result kinds, platform and version
;;;              guards, shadowing, skips, constant evaluation, struct
;;;              layouts.
;;;   committed  RAWDIR is lib/amiga/raw — every committed module must load
;;;              on the host and a handful of well-known OS values must be
;;;              right.  This guards the committed output against a
;;;              half-run regeneration or a hand edit.
;;;
;;; Run twice by the shell test: once as-is and once with :MORPHOS pushed
;;; on *FEATURES* (the guards are load-time), so both sides of every
;;; platform conditional are exercised on the host.
;;;
;;; Prints one "ok"/"FAIL" line per check and a final
;;; "BINDGEN-RESULT pass=N fail=M" line for the shell test to parse.

;; The checks below name AMIGA.FFI symbols; the package must exist before
;; the forms are read.
(require "amiga/ffi")

(defvar *raw-dir* (ext:getenv "RAWDIR"))
(defvar *mode* (or (ext:getenv "BINDGEN_CHECK") "fixture"))
(defvar *mos* (if (member :morphos *features*) t nil))
(defvar *pass* 0)
(defvar *fail* 0)

(defun chk (name ok)
  (if ok (incf *pass*) (incf *fail*))
  (format t "~:[FAIL~;ok  ~] ~A~@[ [morphos]~]~%" ok name *mos*)
  ok)

(defun raw-file (rel)
  (concatenate 'string *raw-dir* "/" rel ".lisp"))

(defun load-raw (rel)
  (handler-case (progn (load (raw-file rel)) t)
    (error (e) (format t "FAIL load ~A: ~A~%" rel e) (incf *fail*) nil)))

(defun sym (pkg name)
  (find-symbol (string-upcase name) pkg))

(defun fbound (pkg name)
  (let ((s (sym pkg name))) (and s (fboundp s) t)))

(defun sym-value (pkg name)
  (let ((s (sym pkg name))) (and s (boundp s) (symbol-value s))))

(defun external-p (pkg name)
  (multiple-value-bind (s status) (find-symbol (string-upcase name) pkg)
    (and s (eq status :external))))

(defvar *file-cache* (make-hash-table :test 'equal))

(defun file-text (rel)
  (or (gethash rel *file-cache*)
      (setf (gethash rel *file-cache*)
            (with-open-file (in (raw-file rel) :direction :input)
              (with-output-to-string (out)
                (loop for line = (read-line in nil nil)
                      while line
                      do (write-string line out) (write-char #\Newline out)))))))

(defun file-contains (rel needle)
  "NEEDLE may span lines (it is matched against the whole file text)."
  (and (search needle (file-text rel)) t))

;;; Wrapper arity without calling it: the wrapper errors at the NIL base,
;;; but only after the arg-count check (a wrong count is an arity error).
(defun arity-ok-p (fn n)
  (handler-case (progn (apply fn (make-list n :initial-element 0)) nil)
    (error (e)
      (let ((msg (format nil "~A" e)))
        (and (search "not open" msg) t)))))

(defun compiler-macro-p (pkg name)
  (let ((s (sym pkg name)))
    (and s (compiler-macro-function s) t)))

;;; ----------------------------------------------------------------
(defun fixture-checks ()
  (load-raw "example")
  (load-raw "exec/exbase")
  (let ((p "AMIGA.RAW.EXAMPLE"))
    ;; package / base
    (chk "package exists" (find-package p))
    (chk "base var NIL on host" (and (boundp (sym p "*example-base*"))
                                     (null (sym-value p "*example-base*"))))
    (chk "version var NIL on host" (null (sym-value p "*example-version*")))
    (chk "base is opened via open-library-or-die on amigaos"
         (file-contains "example" "(amiga.ffi:open-library-or-die \"example.library\" 0)"))
    ;; plain functions: names, LVOs, registers, result kinds
    (chk "ex-flush defined + exported" (and (fbound p "ex-flush") (external-p p "ex-flush")))
    (chk "ex-flush: regs on the next line parsed, LVO -30, void"
         (file-contains "example" "(amiga.ffi:defcfun ex-flush *example-base* -30 (:a0 thing)"))
    (chk "ex-flush :result :void" (file-contains "example" "(:a0 thing)
    :result :void"))
    (chk "ex-create: pointer result, LVO -36, (:a0 name :d0 flags)"
         (and (file-contains "example" "defcfun ex-create *example-base* -36 (:a0 name :d0 flags)")
              (file-contains "example" "ExCreate(CONST_STRPTR name, ULONG flags) (A0,D0) LVO -36")))
    (chk "ex-check :result :bool" (file-contains "example" "ex-check *example-base* -42 (:a0 thing :d0 value)
    :result :bool"))
    (chk "ex-compare :result :signed" (file-contains "example" "ex-compare *example-base* -48 (:a0 a :a1 b)
    :result :signed"))
    (chk "ex-count :result :u16" (file-contains "example" "ex-count *example-base* -54 (:a0 thing)
    :result :u16"))
    (chk "ex-flags :result :unsigned" (file-contains "example" "ex-flags *example-base* -60 (:a0 thing)
    :result :unsigned"))
    (chk "ex-callback: function-pointer param named hook, LVO -66"
         (file-contains "example" "ex-callback *example-base* -66 (:a0 thing :a1 hook)"))
    (chk "ex-callback arity 2" (arity-ok-p (symbol-function (sym p "ex-callback")) 2))
    (chk "compiler macro registered on ex-create" (compiler-macro-p p "ex-create"))
    ;; varargs / alias / private are not bound
    (chk "varargs ExCreateTags not emitted" (not (sym p "ex-create-tags")))
    (chk "private ExPrivate not emitted" (not (sym p "ex-private")))
    (chk "alias ExMovedAlias not emitted" (not (sym p "ex-moved-alias")))
    ;; ==reserve 2 skipped two LVOs: Open is at -90
    (chk "==reserve: Open at LVO -90" (file-contains "example" "defcfun open *example-base* -90 (:d1 name :d2 mode)"))
    (chk "Open shadows CL:OPEN" (and (sym p "open") (not (eq (sym p "open") 'cl:open))
                                     (fbound p "open") (external-p p "open")))
    (chk "CL:OPEN still CL's" (eq (symbol-package 'cl:open) (find-package "COMMON-LISP")))
    ;; >7 registers -> call-library, no compiler macro
    (chk "ex-big-blit defined (8 regs, plist path)" (fbound p "ex-big-blit"))
    (chk "ex-big-blit has no compiler macro" (not (compiler-macro-p p "ex-big-blit")))
    (chk "ex-big-blit uses call-library with kind 3 (signed)"
         (file-contains "example" "(:a0 a :d0 x :d1 y :a1 b :d2 w :d3 h :d4 minterm :d5 mask)"))
    ;; skips
    (chk "DOUBLE result skipped" (and (not (sym p "ex-double"))
                                      (file-contains "example" ";; skipped ExDouble: DOUBLE result")))
    (chk "A5 argument skipped" (and (not (sym p "ex-supervisor"))
                                    (file-contains "example" ";; skipped ExSupervisor: argument in A5")))
    (chk "register-pair argument skipped" (and (not (sym p "ex-pair"))
                                               (file-contains "example" ";; skipped ExPair: 64-bit register-pair")))
    (chk "sysv entry skipped" (and (not (sym p "ex-ppc-only"))
                                   (file-contains "example" ";; skipped ExPpcOnly: not a 68k register call (base,sysv)")))
    ;; version guards (version NIL on host -> not defined either way)
    (chk "ExNewV45 guarded by (%version>= 45), not defined on host"
         (and (not (fbound p "ex-new-v45"))
              (file-contains "example" "(when (and (not (member :morphos *features*)) (%version>= 45))")))
    (chk "ExNewV47 guarded by (%version>= 47)"
         (file-contains "example" "(when (and (not (member :morphos *features*)) (%version>= 47))"))
    ;; platform guards
    (chk "MorphOS-only ExMosOwn defined iff :morphos"
         (eq *mos* (fbound p "ex-mos-own")))
    (chk "ExMosOwn guard text" (file-contains "example" "(when (member :morphos *features*)
  (amiga.ffi:defcfun ex-mos-own *example-base* -126 (:a0 thing)"))
    (chk "ExMoved: NDK variant at -132 only without :morphos"
         (file-contains "example" "(when (and (not (member :morphos *features*)) (%version>= 47))
  (amiga.ffi:defcfun ex-moved *example-base* -132 (:a0 thing)"))
    (chk "ExMoved: MorphOS variant at -138 under :morphos"
         (file-contains "example" "(when (member :morphos *features*)
  (amiga.ffi:defcfun ex-moved *example-base* -138 (:a0 thing)"))
    (chk "ExMoved defined iff :morphos (host has no version)" (eq *mos* (fbound p "ex-moved")))
    ;; constants
    (chk "+exf-base+ #x1000" (eql (sym-value p "+exf-base+") #x1000))
    (chk "+exf-first+ = base+1" (eql (sym-value p "+exf-first+") #x1001))
    (chk "+exf-mask+ (1<<4)!(1<<5) with trailing * remark" (eql (sym-value p "+exf-mask+") #x30))
    (chk "+exf-later+ forward reference" (eql (sym-value p "+exf-later+") 6))
    (chk "+exf-hex+ 0x1f" (eql (sym-value p "+exf-hex+") 31))
    (chk "+exf-char+ packed char literal" (eql (sym-value p "+exf-char+") #x4558414D))
    (chk "+exf-neg+ -5" (eql (sym-value p "+exf-neg+") -5))
    (chk "+exf-not+ ~0" (eql (sym-value p "+exf-not+") -1))
    (chk "+exf-bin+ %1010" (eql (sym-value p "+exf-bin+") 10))
    (chk "+exf-spaced+ spaces inside parens" (eql (sym-value p "+exf-spaced+") #x444F))
    (chk "BITDEF EX,READY,3 -> +exb-ready+ 3 / +exf-ready+ 8"
         (and (eql (sym-value p "+exb-ready+") 3) (eql (sym-value p "+exf-ready+") 8)))
    (chk "ENUM/EITEM with trailing comma" (and (eql (sym-value p "+exa-first+") #x1100)
                                               (eql (sym-value p "+exa-second+") #x1101)
                                               (eql (sym-value p "+exz-zero+") 0)))
    (chk "DEVINIT/DEVCMD from CMD_NONSTD (cross-file symbol)"
         (and (eql (sym-value p "+excmd-ping+") 9) (eql (sym-value p "+excmd-pong+") 10)))
    (chk "LIBINIT/LIBDEF LVO constants" (and (eql (sym-value p "+lvo-ex-flush+") -30)
                                             (eql (sym-value p "+lvo-ex-create+") -36)))
    (chk "macro body skipped (no EXAMPLENAME symbol)" (not (sym p "examplename")))
    (chk "SET symbols not emitted" (not (sym p "+libraries-example-i+")))
    (chk "constants exported" (external-p p "+exf-base+"))
    ;; structures
    (chk "*ex-thing-size* 36 (base from included file + ALIGNWORD)"
         (eql (sym-value p "*ex-thing-size*") 36))
    (chk "ex-thing-name :fptr at 6" (file-contains "example" "  (name :fptr 6)"))
    (chk "ex-thing-x :i16 at 10" (file-contains "example" "  (x :i16 10)"))
    (chk "ex-thing-flag :u8 at 14, count :u32 at 16 after ALIGNWORD"
         (and (file-contains "example" "  (flag :u8 14)") (file-contains "example" "  (count :u32 16)")))
    (chk "ex-thing-inner (:struct 8) at 20 via <...>" (file-contains "example" "  (inner (:struct 8) 20)"))
    (chk "ex-thing-marker LABEL -> (:struct 0) at 28" (file-contains "example" "  (marker (:struct 0) 28)"))
    (chk "ex-thing-lock BPTR :u32, scale FLOAT :single"
         (and (file-contains "example" "  (lock :u32 28)") (file-contains "example" "  (scale :single 32)")))
    (chk "accessors work: x/y/flag on foreign memory"
         (let ((m (ffi:alloc-foreign 36)))
           (unwind-protect
                (progn
                  (funcall (fdefinition (list 'setf (sym p "ex-thing-x"))) -7 m)
                  (funcall (fdefinition (list 'setf (sym p "ex-thing-flag"))) 200 m)
                  (and (eql (funcall (sym p "ex-thing-x") m) -7)
                       (eql (funcall (sym p "ex-thing-flag") m) 200)
                       (null (funcall (sym p "ex-thing-name") m))
                       (= 20 (- (ffi:foreign-pointer-address (funcall (sym p "ex-thing-inner") m))
                                (ffi:foreign-pointer-address m)))))
             (ffi:free-foreign m))))
    (chk "IO/IOSTD size labels -> io-request (4) and io-std-req (8)"
         (and (eql (sym-value p "*io-request-size*") 4)
              (eql (sym-value p "*io-std-req-size*") 8)
              (fbound p "io-request-device") (fbound p "io-std-req-actual")
              (not (fbound p "io-request-actual"))))
    (chk "struct accessors exported" (and (external-p p "ex-thing-x") (external-p p "*ex-thing-size*"))))
  ;; header-only module from the included file
  (let ((p "AMIGA.RAW.EXEC.EXBASE"))
    (chk "header-only module package" (find-package p))
    (chk "header-only: no base var" (not (sym p "*exbase-base*")))
    (chk "header-only: +exb-magic+ #xCAFE" (eql (sym-value p "+exb-magic+") #xCAFE))
    (chk "header-only: ex-base struct size 6" (eql (sym-value p "*ex-base-size*") 6))
    (chk "header-only: +cmd-nonstd+ 9" (eql (sym-value p "+cmd-nonstd+") 9)))
  ;; MorphOS-only library
  (chk "mosonly module generated (BINDGEN_MOS_ONLY)" (probe-file (raw-file "mosonly")))
  (when (probe-file (raw-file "mosonly"))
    (load-raw "mosonly")
    (let ((p "AMIGA.RAW.MOSONLY"))
      (chk "mosonly: functions unconditional (whole lib is MorphOS-only)"
           (and (fbound p "mo-version") (fbound p "mo-create")
                (file-contains "mosonly" "(amiga.ffi:defcfun mo-create *mosonly-base* -36 (:d0 size)")))
      (chk "mosonly: header names MorphOS SDK as the source"
           (file-contains "mosonly" "MorphOS SDK mosonly_lib.fd"))))
  ;; --- class libraries: module path by kind, tags from the C header ---
  (chk "class libs: no top-level fixgad / fixreq modules"
       (and (not (probe-file (raw-file "fixgad"))) (not (probe-file (raw-file "fixreq")))))
  (chk ".h with a .i twin yields no module" (not (probe-file (raw-file "libraries/example"))))
  (load-raw "gadgets/fixgad")
  (load-raw "classes/fixreq")
  (load-raw "reaction/reaction")
  (let ((p "AMIGA.RAW.GADGETS.FIXGAD"))
    (chk "gadgets/fixgad: package AMIGA.RAW.GADGETS.FIXGAD" (find-package p))
    (chk "gadgets/fixgad: base var named after the library, opened as gadgets/fixgad.gadget"
         (and (boundp (sym p "*fixgad-base*")) (null (sym-value p "*fixgad-base*"))
              (file-contains "gadgets/fixgad" "(amiga.ffi:open-library-or-die \"gadgets/fixgad.gadget\" 0)")))
    (chk "gadgets/fixgad: functions (FIXGAD_GetClass pointer, FIXGAD_Refresh void)"
         (and (fbound p "fixgad-get-class") (external-p p "fixgad-get-class")
              (file-contains "gadgets/fixgad" "defcfun fixgad-get-class *fixgad-base* -30 ()
    :result :pointer")
              (file-contains "gadgets/fixgad" "defcfun fixgad-refresh *fixgad-base* -36 (:a0 gadget)
    :result :void")))
    (chk "gadgets/fixgad: header lists gadgets/fixgad.h as a source"
         (file-contains "gadgets/fixgad" ";;;   gadgets/fixgad.h"))
    ;; #define expressions
    (chk "C: (EXF_BASE + 0x400) — asm constant + hex" (eql (sym-value p "+fixgad-dummy+") #x1400))
    (chk "C: (FIXGAD_Dummy + 1) macro reference" (eql (sym-value p "+fixgad-text-pen+") #x1401))
    (chk "C: 2L suffix" (eql (sym-value p "+fixgad-long+") #x1402))
    (chk "C: (1<<16)" (eql (sym-value p "+fixgad-shift+") #x10000))
    (chk "C: (0xffff0000)" (eql (sym-value p "+fixgad-mask+") #xFFFF0000))
    (chk "C: (~0L) -> -1" (eql (sym-value p "+fixgad-ignore+") -1))
    (chk "C: 'A' / 'FORM' / '\\n' char literals"
         (and (eql (sym-value p "+fixgad-char+") 65)
              (eql (sym-value p "+fixgad-packed+") #x464F524D)
              (eql (sym-value p "+fixgad-escaped+") 10)))
    (chk "C: casts narrow — (UWORD)~0, (ULONG)(-1), (BYTE)0xFF"
         (and (eql (sym-value p "+fixgad-word+") #xFFFF)
              (eql (sym-value p "+fixgad-ulong+") #xFFFFFFFF)
              (eql (sym-value p "+fixgad-byte+") -1)))
    (chk "C: alias of an asm constant (EXF_FIRST)" (eql (sym-value p "+fixgad-alias+") #x1001))
    (chk "C: macro from the #included twin-less header (REACTION_Dummy + 3)"
         (eql (sym-value p "+fixgad-reaction+") #x6003))
    (chk "C: ?: / octal / relational" (and (eql (sym-value p "+fixgad-ternary+") 5)
                                          (eql (sym-value p "+fixgad-oct+") 8)
                                          (eql (sym-value p "+fixgad-rel+") 1)))
    (chk "C: backslash continuation" (eql (sym-value p "+fixgad-multi+") #x10041))
    (chk "C: forward reference resolved at emit" (and (eql (sym-value p "+fixgad-forward+") 41)
                                                    (eql (sym-value p "+fixgad-later+") 40)))
    (chk "C: block comment spanning lines / line comment"
         (and (eql (sym-value p "+fixgad-comment+") 8) (eql (sym-value p "+fixgad-slash+") 9)))
    (chk "C: empty-body macro and include guard are not constants"
         (and (not (sym p "+fixgad-flag+")) (not (sym p "+gadgets-fixgad-h+"))))
    ;; what is skipped
    (chk "C: string / float / NewObject / sizeof / statement / function-like macros not emitted"
         (and (not (sym p "+fixgad-name+")) (not (sym p "+fixgad-float+"))
              (not (sym p "+fix-gad-object+")) (not (sym p "+fixgad-size+"))
              (not (sym p "+fixgad-stmt+")) (not (sym p "+fix-gad-set+"))))
    (chk "C: skipped macros counted in the header (5)"
         (file-contains "gadgets/fixgad" ";;; 5 C macros skipped: not an integer constant"))
    ;; conditionals
    (chk "C: #ifdef __cplusplus block skipped, #ifndef taken, #else of it skipped"
         (and (not (sym p "+fixgad-cpp+")) (eql (sym-value p "+fixgad-not-cpp+") 2)
              (not (sym p "+fixgad-cpp-else+"))))
    (chk "C: #if 0 / #elif defined(__VBCC__) skipped, #else taken"
         (and (not (sym p "+fixgad-if-zero+")) (not (sym p "+fixgad-vbcc+"))
              (eql (sym-value p "+fixgad-if-else+") 6)))
    (chk "C: #if defined(X) && !defined(Y) over the header's own macros"
         (eql (sym-value p "+fixgad-if-defined+") 8))
    ;; #undef + redefinition
    (chk "C: #undef/redefine — first value kept, later refs use the new one, no duplicate"
         (and (eql (sym-value p "+fixgad-dummy+") #x1400)
              (eql (sym-value p "+fixgad-after+") #x3001)
              (= 1 (count-matches "(defconstant +fixgad-dummy+ " (file-text "gadgets/fixgad")))))
    ;; enums
    (chk "C: anonymous enum — implicit, explicit, expression, trailing comma"
         (and (eql (sym-value p "+fixgad-img-default+") 0) (eql (sym-value p "+fixgad-img-info+") 1)
              (eql (sym-value p "+fixgad-img-skip+") 5) (eql (sym-value p "+fixgad-img-next+") 6)
              (eql (sym-value p "+fixgad-img-expr+") 10) (eql (sym-value p "+fixgad-img-last+") 11)))
    (chk "C: named enum and typedef enum" (and (eql (sym-value p "+fixgad-save+") 0)
                                               (eql (sym-value p "+fixgad-use+") 1)
                                               (eql (sym-value p "+fixgad-td-a+") 100)
                                               (eql (sym-value p "+fixgad-td-b+") 101)))
    (chk "C: enum inside a struct body is not read" (not (sym p "+fixgad-inner+")))
    (chk "C: constants exported" (and (external-p p "+fixgad-dummy+") (external-p p "+fixgad-td-b+"))))
  (let ((p "AMIGA.RAW.CLASSES.FIXREQ"))
    (chk "classes/fixreq: package, .class opened by bare name, tags from classes/fixreq.h"
         (and (find-package p) (fbound p "fixreq-get-class")
              (file-contains "classes/fixreq" "(amiga.ffi:open-library-or-die \"fixreq.class\" 0)")
              (eql (sym-value p "+fixreq-dummy+") #x1500)
              (eql (sym-value p "+fixreq-title+") #x1501))))
  (let ((p "AMIGA.RAW.REACTION.REACTION"))
    (chk "reaction/reaction: header-only module from an unclaimed twin-less .h"
         (and (find-package p) (not (sym p "*reaction-base*"))
              (eql (sym-value p "+reaction-dummy+") #x6000)
              (eql (sym-value p "+reaction-text-attr+") #x6005)
              (not (sym p "+make-id+")) (not (sym p "+reaction-reaction-h+"))))))

(defun count-matches (needle text)
  (let ((n 0) (start 0))
    (loop for p = (search needle text :start2 start)
          while p do (incf n) (setf start (1+ p)))
    n))

;;; ----------------------------------------------------------------
(defun committed-checks ()
  (let* ((files (sort (append (directory (concatenate 'string *raw-dir* "/*.lisp"))
                              (directory (concatenate 'string *raw-dir* "/*/*.lisp")))
                      #'string< :key #'namestring))
         (failed 0))
    (chk "committed modules present (>= 100)" (>= (length files) 100))
    (dolist (f files)
      (handler-case (load f)
        (error (e)
          (incf failed)
          (format t "FAIL load ~A: ~A~%" (file-namestring f) e))))
    (chk (format nil "all ~D committed modules load on host" (length files)) (zerop failed))
    ;; generated headers
    (chk "modules carry the DO NOT EDIT banner"
         (every (lambda (f)
                  (with-open-file (in f) (search "GENERATED by scripts/gen-amiga-bindings.lisp" (read-line in))))
                files))
    ;; well-known OS values
    (chk "intuition: +idcmp-closewindow+ #x200" (eql (sym-value "AMIGA.RAW.INTUITION" "+idcmp-closewindow+") #x200))
    (chk "intuition: +wflg-reportmouse+ #x200" (eql (sym-value "AMIGA.RAW.INTUITION" "+wflg-reportmouse+") #x200))
    (chk "intuition: +wa-left+ #x80000064" (eql (sym-value "AMIGA.RAW.INTUITION" "+wa-left+") #x80000064))
    (chk "intuition: *window-size* 136" (eql (sym-value "AMIGA.RAW.INTUITION" "*window-size*") 136))
    (chk "intuition: window-rport / window-user-port accessors"
         (and (fbound "AMIGA.RAW.INTUITION" "window-rport") (fbound "AMIGA.RAW.INTUITION" "window-user-port")))
    (chk "intuition: open-window-tag-list -606 pointer result"
         (file-contains "intuition" "defcfun open-window-tag-list *intuition-base* -606 (:a0 new-window :a1 tag-list)
    :result :pointer"))
    (chk "intuition: show-window (V46, private on MorphOS) guarded"
         (file-contains "intuition" "(when (and (not (member :morphos *features*)) (%version>= 46))
  (amiga.ffi:defcfun show-window"))
    (chk "intuition: MorphOS-only get-monitor-list iff :morphos"
         (eq *mos* (fbound "AMIGA.RAW.INTUITION" "get-monitor-list")))
    (chk "exec: +memf-chip+ 2, +memf-clear+ #x10000"
         (and (eql (sym-value "AMIGA.RAW.EXEC" "+memf-chip+") 2)
              (eql (sym-value "AMIGA.RAW.EXEC" "+memf-clear+") #x10000)))
    (chk "exec: avail-mem at LVO -216 (:d1 requirements)"
         (file-contains "exec" "defcfun avail-mem *exec-base* -216 (:d1 requirements)"))
    (chk "exec: node 14 / msg-port 34 / io-request 32 / io-std-req 48 / library 34"
         (and (eql (sym-value "AMIGA.RAW.EXEC" "*node-size*") 14)
              (eql (sym-value "AMIGA.RAW.EXEC" "*msg-port-size*") 34)
              (eql (sym-value "AMIGA.RAW.EXEC" "*io-request-size*") 32)
              (eql (sym-value "AMIGA.RAW.EXEC" "*io-std-req-size*") 48)
              (eql (sym-value "AMIGA.RAW.EXEC" "*library-size*") 34)))
    (chk "exec: library-version shadowed (struct accessor != amiga.ffi's)"
         (not (eq (sym "AMIGA.RAW.EXEC" "library-version") 'amiga.ffi:library-version)))
    (chk "exec: NewMinList (V45, clashes on MorphOS) is AmigaOS-only"
         (file-contains "exec" "(when (and (not (member :morphos *features*)) (%version>= 45))
  (amiga.ffi:defcfun new-min-list"))
    (chk "exec: MorphOS-only alloc-vec-pooled iff :morphos"
         (eq *mos* (fbound "AMIGA.RAW.EXEC" "alloc-vec-pooled")))
    (chk "exec: +cmd-nonstd+ 9 (DEVCMD macro)" (eql (sym-value "AMIGA.RAW.EXEC" "+cmd-nonstd+") 9))
    (chk "dos: open/close/read/write shadow CL"
         (and (not (eq (sym "AMIGA.RAW.DOS" "open") 'cl:open))
              (not (eq (sym "AMIGA.RAW.DOS" "close") 'cl:close))
              (not (eq (sym "AMIGA.RAW.DOS" "read") 'cl:read))
              (not (eq (sym "AMIGA.RAW.DOS" "format") 'cl:format))
              (string= (format nil "~A" 42) "42")))
    (chk "dos: +mode-oldfile+ 1005 / +mode-newfile+ 1006"
         (and (eql (sym-value "AMIGA.RAW.DOS" "+mode-oldfile+") 1005)
              (eql (sym-value "AMIGA.RAW.DOS" "+mode-newfile+") 1006)))
    (chk "dos: *file-info-block-size* 260" (eql (sym-value "AMIGA.RAW.DOS" "*file-info-block-size*") 260))
    (chk "dos: ErrorOutput (OS 3.2, clashes on MorphOS) AmigaOS-only"
         (file-contains "dos" "(when (not (member :morphos *features*))
  (amiga.ffi:defcfun error-output"))
    (chk "graphics: rastport spelling, *rastport-size* 100"
         (eql (sym-value "AMIGA.RAW.GRAPHICS" "*rastport-size*") 100))
    (chk "graphics: blt-bitmap (11 registers) via call-library"
         (and (fbound "AMIGA.RAW.GRAPHICS" "blt-bitmap")
              (not (compiler-macro-p "AMIGA.RAW.GRAPHICS" "blt-bitmap"))))
    (chk "graphics: read-pixel ULONG -> :unsigned, write-pixel LONG -> :signed, set-a-pen :void"
         (and (file-contains "graphics" "defcfun read-pixel *graphics-base* -318 (:a1 rp :d0 x :d1 y)
    :result :unsigned")
              (file-contains "graphics" "defcfun write-pixel *graphics-base* -324 (:a1 rp :d0 x :d1 y)
    :result :signed")
              (file-contains "graphics" "defcfun set-a-pen *graphics-base* -342 (:a1 rp :d0 pen)
    :result :void")))
    (chk "naming: SetAPen/AddGList/ModifyIDCMP/BltBitMap/RPort"
         (and (fbound "AMIGA.RAW.GRAPHICS" "set-a-pen")
              (fbound "AMIGA.RAW.INTUITION" "add-g-list")
              (fbound "AMIGA.RAW.INTUITION" "modify-idcmp")
              (fbound "AMIGA.RAW.GRAPHICS" "blt-bitmap")
              (fbound "AMIGA.RAW.INTUITION" "window-rport")
              (fbound "AMIGA.RAW.UTILITY" "u-mult32")))
    (chk "utility: +tag-user+ #x80000000 (shadowed AMIGA.FFI:+TAG-DONE+ etc. coexist)"
         (and (eql (sym-value "AMIGA.RAW.UTILITY" "+tag-user+") #x80000000)
              (eql (sym-value "AMIGA.RAW.UTILITY" "+tag-done+") 0)))
    (chk "gadtools: *new-gadget-size* 30, create-gadget-a"
         (and (eql (sym-value "AMIGA.RAW.GADTOOLS" "*new-gadget-size*") 30)
              (fbound "AMIGA.RAW.GADTOOLS" "create-gadget-a")))
    (chk "devices/audio header module: *io-audio-size* 68"
         (eql (sym-value "AMIGA.RAW.DEVICES.AUDIO" "*io-audio-size*") 68))
    (chk "timer: device module leaves base NIL (no OpenLibrary)"
         (and (file-contains "timer" "timer.device is a device/resource")
              (null (sym-value "AMIGA.RAW.TIMER" "*timer-base*"))))
    (chk "muimaster: MorphOS-only module present"
         (and (probe-file (raw-file "muimaster")) (fbound "AMIGA.RAW.MUIMASTER" "mui-new-object-a")))
    ;; ReAction class libraries: under gadgets/ images/ classes/, with the
    ;; tags that exist only in the NDK's C headers
    (chk "class libs: no top-level button / bevel / window modules"
         (and (not (probe-file (raw-file "button"))) (not (probe-file (raw-file "bevel")))
              (not (probe-file (raw-file "window"))) (not (probe-file (raw-file "layout")))))
    ;; (the class functions are V40+ guarded, so they are unbound on the host)
    (chk "gadgets/button: BUTTON_GetClass + tags (BUTTON_Dummy TAG_USER+0x04000000)"
         (and (file-contains "gadgets/button" "(amiga.ffi:defcfun button-get-class *button-base* -30 ()
    :result :pointer")
              (file-contains "gadgets/button" "(amiga.ffi:open-library-or-die \"gadgets/button.gadget\" 0)")
              (eql (sym-value "AMIGA.RAW.GADGETS.BUTTON" "+button-dummy+") #x84000000)
              (eql (sym-value "AMIGA.RAW.GADGETS.BUTTON" "+button-justification+") #x84000010)
              ;; BUTTON_RenderImage is an alias of GA_Image
              (eql (sym-value "AMIGA.RAW.GADGETS.BUTTON" "+button-render-image+")
                   (sym-value "AMIGA.RAW.INTUITION" "+ga-image+"))))
    (chk "gadgets/layout: LAYOUT_AddChild, RethinkLayout"
         (and (eql (sym-value "AMIGA.RAW.GADGETS.LAYOUT" "+layout-add-child+") #x85007014)
              (file-contains "gadgets/layout" "defcfun rethink-layout *layout-base* -48 (:a0 gadget :a1 window :a2 requester :d0 refresh)")))
    (chk "images/bevel: opened as images/bevel.image, BEVEL_Dummy"
         (and (file-contains "images/bevel" "(amiga.ffi:open-library-or-die \"images/bevel.image\" 0)")
              (eql (sym-value "AMIGA.RAW.IMAGES.BEVEL" "+bevel-dummy+") #x85016000)))
    (chk "classes/window: window.class, WINDOW_Position, WMHI_CLOSEWINDOW (1<<16), WMHI_IGNORE (~0L)"
         (and (file-contains "classes/window" "(amiga.ffi:open-library-or-die \"window.class\" 0)")
              (file-contains "classes/window" "(amiga.ffi:defcfun window-get-class *window-base* -30 ()")
              (eql (sym-value "AMIGA.RAW.CLASSES.WINDOW" "+window-position+") #x8502500E)
              (eql (sym-value "AMIGA.RAW.CLASSES.WINDOW" "+wmhi-closewindow+") #x10000)
              (eql (sym-value "AMIGA.RAW.CLASSES.WINDOW" "+wmhi-ignore+") -1)
              (eql (sym-value "AMIGA.RAW.CLASSES.WINDOW" "+wpos-centerscreen+") 1)))
    (chk "classes/requester: enum REQIMAGE_WARNING 2"
         (eql (sym-value "AMIGA.RAW.CLASSES.REQUESTER" "+reqimage-warning+") 2))
    (chk "gadgets/texteditor: TEXTEDITOR_Dummy keeps its first value across #undef"
         (and (eql (sym-value "AMIGA.RAW.GADGETS.TEXTEDITOR" "+texteditor-dummy+") #x85026000)
              (eql (sym-value "AMIGA.RAW.GADGETS.TEXTEDITOR" "+gm-texteditor-handle-error+") #x4501F)))
    (chk "reaction/reaction: REACTION_Dummy TAG_USER+0x5000000"
         (eql (sym-value "AMIGA.RAW.REACTION.REACTION" "+reaction-dummy+") #x85000000))
    (chk "keymap: RAWKEY_* from libraries/keymap.h next to devices/keymap.i"
         (and (eql (sym-value "AMIGA.RAW.KEYMAP" "+rawkey-space+") #x40)
              (eql (sym-value "AMIGA.RAW.KEYMAP" "+kcf-shift+") 1)))
    (chk "trackfile: TFERROR_UnitBusy from devices/trackfile.h"
         (eql (sym-value "AMIGA.RAW.TRACKFILE" "+tferror-unit-busy+") -202041))))

;;; ----------------------------------------------------------------
(cond ((string= *mode* "fixture") (fixture-checks))
      ((string= *mode* "committed") (committed-checks))
      (t (error "unknown BINDGEN_CHECK mode ~S" *mode*)))

(format t "~%BINDGEN-RESULT mode=~A morphos=~A pass=~D fail=~D~%" *mode* *mos* *pass* *fail*)
