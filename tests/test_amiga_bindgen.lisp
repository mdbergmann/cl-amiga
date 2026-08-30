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

;; One check = one line.  A check that SIGNALS counts as a failure and
;; names the condition on its FAIL line -- it must not take the rest of the
;; run with it (LOAD would recover at the next top-level form, print the
;; summary with everything after the error silently unrun, and the shell
;; driver only shows ok/FAIL lines).
(defun %chk (name thunk)
  (let* ((err nil)
         (ok (handler-case (funcall thunk)
               (error (e)
                 (setf err (substitute #\Space #\Newline (princ-to-string e)))
                 nil))))
    (if ok (incf *pass*) (incf *fail*))
    (format t "~:[FAIL~;ok  ~] ~A~:[~; [morphos]~]~@[ -- ERROR: ~A~]~%" ok name *mos* err)
    ok))

(defmacro chk (name form)
  `(%chk ,name (lambda () ,form)))

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

;; Line endings: the text is rebuilt with #\Newline only and the needle is
;; stripped of #\Return, so a CRLF checkout of THIS file (Git for Windows'
;; autocrlf default puts "\r\n" inside a string literal that spans lines)
;; or a CRLF-written module compare equal to an LF one.
(defun file-text (rel)
  (or (gethash rel *file-cache*)
      (setf (gethash rel *file-cache*)
            (with-open-file (in (raw-file rel) :direction :input)
              (with-output-to-string (out)
                (loop for line = (read-line in nil nil)
                      while line
                      do (write-string (string-right-trim '(#\Return) line) out)
                         (write-char #\Newline out)))))))

(defun file-contains (rel needle)
  "NEEDLE may span lines (it is matched against the whole file text)."
  (and (search (remove #\Return needle) (file-text rel)) t))

;;; Wrapper arity without calling it: the wrapper errors at the NIL base,
;;; but only after the arg-count check (a wrong count is an arity error).
(defun arity-ok-p (fn n)
  (handler-case (progn (apply fn (make-list n :initial-element 0)) nil)
    (error (e)
      (let ((msg (format nil "~A" e)))
        (and (search "not open" msg) t)))))

;; A DEFCFUN binding with <= 7 register args is an FFI stub (a binding
;; descriptor in the function cell, see lib/amiga/ffi.lisp); the >7
;; plist path is an ordinary DEFUN.  STUB-INFO is the descriptor's plist
;; or NIL.
(defun stub-info (pkg name)
  (let ((s (sym pkg name)))
    (and s (fboundp s) (ffi::%ffi-stub-info s))))

(defun libcall-stub-p (pkg name &key lvo result nparams)
  (let ((info (stub-info pkg name)))
    (and info
         (eq (getf info :kind) :libcall)
         (or (null lvo) (eql (getf info :lvo) lvo))
         (or (null result) (eq (getf info :result) result))
         (or (null nparams) (eql (getf info :nparams) nparams)))))

;;; A module is one AMIGA.FFI:DEFINE-BINDING-TABLE form; its rows come back
;;; from the registered table through CLAMIGA::%BINDING-TABLE-ENTRIES —
;;; format-agnostic checks of what the generator emitted (LVOs, registers,
;;; result kinds, guards, field layouts) without grepping source text.
;;; Cached per package: an enumeration (DO-SYMBOLS) drops a table.
(defvar *rows-cache* (make-hash-table :test 'equal))

(defun table-rows (pkg)
  (or (gethash pkg *rows-cache*)
      (setf (gethash pkg *rows-cache*) (clamiga::%binding-table-entries pkg))))

(defun rows-named (pkg kind name)
  (remove-if-not (lambda (r) (and (eq (first r) kind)
                                  (string= (second r) (string-upcase name))))
                 (table-rows pkg)))

(defun fn-row-p (pkg name &key lvo regs result guard min-version)
  "Some (:fn NAME lvo regs result opts...) row matches every given property."
  (some (lambda (r)
          (let ((r-lvo (third r)) (r-regs (fourth r)) (r-result (fifth r))
                (opts (nthcdr 5 r)))
            (and (or (null lvo) (eql lvo r-lvo))
                 (or (null regs) (equal regs r-regs))
                 (or (null result) (eq result r-result))
                 (or (null guard) (and (member guard opts) t))
                 (or (null min-version) (and (member min-version opts) t)))))
        (rows-named pkg :fn name)))

(defun fn-row-count (pkg name) (length (rows-named pkg :fn name)))

(defun field-row-p (pkg name type offset)
  "Some (:field NAME type offset) row; TYPE :struct for an embedded struct."
  (some (lambda (r) (and (equal (third r) type) (eql (fourth r) offset)))
        (rows-named pkg :field name)))

(defun const-row-p (pkg name value)
  "Some (:const NAME value) row; VALUE an integer or a string."
  (some (lambda (r) (equal (third r) value)) (rows-named pkg :const name)))

(defun name-row-p (pkg name) (and (rows-named pkg :name name) t))

(defun fn-row-unguarded-p (pkg name)
  "Every (:fn NAME ...) row is free of a :morphos / :not-morphos guard."
  (and (rows-named pkg :fn name)
       (every (lambda (r) (null (intersection '(:morphos :not-morphos) (nthcdr 5 r))))
              (rows-named pkg :fn name))))

;;; (setf (ACCESSOR ptr) value) through the accessor's DEFSETF -- the
;;; user-facing contract of a struct field setter (there is no (SETF
;;; ACCESSOR) function).  The accessor symbol exists only at run time, so
;;; the SETF form is built and compiled here.
(defun set-field (accessor ptr value)
  (funcall (eval `(lambda (p v) (setf (,accessor p) v))) ptr value))

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
    ;; the module is one binding table (plus the base/version defvars)
    (chk "module carries a binding table with :base/:version"
         (let ((info (clamiga::%binding-table-info p)))
           (and info (> (getf info :entries) 40)
                (eq (getf info :base) (sym p "*example-base*"))
                (eq (getf info :version) (sym p "*example-version*")))))
    (chk "define-binding-table form in the source"
         (file-contains "example" "(amiga.ffi:define-binding-table \"AMIGA.RAW.EXAMPLE\"
    (:base *example-base* :version *example-version*)"))
    (chk "names are materialised on first reference, not at load"
         (let ((before (getf (clamiga::%binding-table-info p) :symbols)))
           (sym p "ex-flags")            ; first reference
           (= (getf (clamiga::%binding-table-info p) :symbols) (1+ before))))
    ;; plain functions: names, LVOs, registers, result kinds
    (chk "ex-flush defined + exported" (and (fbound p "ex-flush") (external-p p "ex-flush")))
    (chk "ex-flush: regs on the next line parsed, LVO -30, void"
         (fn-row-p p "ex-flush" :lvo -30 :regs '(:a0) :result :void))
    (chk "ex-create: pointer result, LVO -36, (:a0 name :d0 flags) with the prototype comment"
         (and (fn-row-p p "ex-create" :lvo -36 :regs '(:a0 :d0) :result :pointer)
              (file-contains "example" "ExCreate(CONST_STRPTR name, ULONG flags) (A0,D0) LVO -36")))
    (chk "ex-check :result :bool" (fn-row-p p "ex-check" :lvo -42 :regs '(:a0 :d0) :result :bool))
    (chk "ex-compare :result :signed" (fn-row-p p "ex-compare" :lvo -48 :regs '(:a0 :a1) :result :signed))
    (chk "ex-count :result :u16" (fn-row-p p "ex-count" :lvo -54 :regs '(:a0) :result :u16))
    (chk "ex-flags :result :unsigned" (fn-row-p p "ex-flags" :lvo -60 :regs '(:a0) :result :unsigned))
    (chk "ex-callback: function-pointer param named hook, LVO -66"
         (and (fn-row-p p "ex-callback" :lvo -66 :regs '(:a0 :a1) :result :bool)
              (file-contains "example" "ExCallback(struct ExThing * thing, APTR hook) (A0,A1) LVO -66")))
    (chk "ex-callback arity 2" (arity-ok-p (symbol-function (sym p "ex-callback")) 2))
    (chk "ex-create is an FFI stub: LVO -36, :pointer result, 2 args"
         (libcall-stub-p p "ex-create" :lvo -36 :result :pointer :nparams 2))
    (chk "ex-check stub carries the :bool result kind"
         (libcall-stub-p p "ex-check" :lvo -42 :result :bool :nparams 2))
    (chk "ex-count stub carries the :u16 result kind"
         (libcall-stub-p p "ex-count" :result :u16))
    ;; varargs / alias / private are not bound
    (chk "varargs ExCreateTags not emitted" (not (sym p "ex-create-tags")))
    (chk "private ExPrivate not emitted" (not (sym p "ex-private")))
    (chk "alias ExMovedAlias not emitted" (not (sym p "ex-moved-alias")))
    ;; ==reserve 2 skipped two LVOs: Open is at -90
    (chk "==reserve: Open at LVO -90" (fn-row-p p "open" :lvo -90 :regs '(:d1 :d2)))
    (chk "Open shadows CL:OPEN" (and (sym p "open") (not (eq (sym p "open") 'cl:open))
                                     (fbound p "open") (external-p p "open")))
    (chk "Open: the :shadow symbol got the table's definition"
         (libcall-stub-p p "open" :lvo -90 :nparams 2))
    (chk "CL:OPEN still CL's" (eq (symbol-package 'cl:open) (find-package "COMMON-LISP")))
    ;; >7 registers -> a DEFUN over call-library, not an FFI stub
    (chk "ex-big-blit defined (8 regs, plist path)" (fbound p "ex-big-blit"))
    (chk "ex-big-blit (>7 registers) is a plain function, not a stub"
         (and (fbound p "ex-big-blit") (not (stub-info p "ex-big-blit"))))
    (chk "ex-big-blit exported by a (:name) row, defined after the table"
         (and (name-row-p p "ex-big-blit") (external-p p "ex-big-blit")
              (file-contains "example" "(amiga.ffi:defcfun ex-big-blit *example-base* -96 (:a0 a :d0 x :d1 y :a1 b :d2 w :d3 h :d4 minterm :d5 mask)")))
    ;; skips
    (chk "DOUBLE result skipped" (and (not (sym p "ex-double"))
                                      (file-contains "example" ";; skipped ExDouble: DOUBLE result")))
    (chk "A5 argument skipped" (and (not (sym p "ex-supervisor"))
                                    (file-contains "example" ";; skipped ExSupervisor: argument in A5")))
    (chk "register-pair argument skipped" (and (not (sym p "ex-pair"))
                                               (file-contains "example" ";; skipped ExPair: 64-bit register-pair")))
    (chk "sysv entry skipped" (and (not (sym p "ex-ppc-only"))
                                   (file-contains "example" ";; skipped ExPpcOnly: not a 68k register call (base,sysv)")))
    ;; version guards (version NIL on host -> present, exported, unbound)
    (chk "ExNewV45 guarded by min-version 45 (:not-morphos), not defined on host"
         (and (not (fbound p "ex-new-v45")) (external-p p "ex-new-v45")
              (fn-row-p p "ex-new-v45" :lvo -120 :guard :not-morphos :min-version 45)))
    (chk "ExNewV47 guarded by min-version 47"
         (fn-row-p p "ex-new-v47" :lvo -126 :guard :not-morphos :min-version 47))
    ;; platform guards
    (chk "MorphOS-only ExMosOwn defined iff :morphos"
         (eq *mos* (fbound p "ex-mos-own")))
    (chk "ExMosOwn row carries :morphos" (fn-row-p p "ex-mos-own" :lvo -126 :regs '(:a0) :guard :morphos))
    (chk "ExMoved: NDK variant at -132 only without :morphos"
         (fn-row-p p "ex-moved" :lvo -132 :guard :not-morphos :min-version 47))
    (chk "ExMoved: MorphOS variant at -138 under :morphos"
         (and (fn-row-p p "ex-moved" :lvo -138 :guard :morphos)
              (= 2 (fn-row-count p "ex-moved"))))
    (chk "ExMoved defined iff :morphos (host has no version)" (eq *mos* (fbound p "ex-moved")))
    (chk "ExMoved under :morphos resolves to the -138 variant"
         (or (not *mos*) (libcall-stub-p p "ex-moved" :lvo -138)))
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
         (and (eql (sym-value p "*ex-thing-size*") 36)
              (file-contains "example" "(:struct \"EX-THING\" 36   ; ExThing (libraries/example.i)")))
    (chk "ex-thing-name :fptr at 6" (field-row-p p "ex-thing-name" :fptr 6))
    (chk "ex-thing-x :i16 at 10" (field-row-p p "ex-thing-x" :i16 10))
    (chk "ex-thing-flag :u8 at 14, count :u32 at 16 after ALIGNWORD"
         (and (field-row-p p "ex-thing-flag" :u8 14) (field-row-p p "ex-thing-count" :u32 16)))
    (chk "ex-thing-inner (:struct 8) at 20 via <...>"
         (and (field-row-p p "ex-thing-inner" :struct 20)
              (file-contains "example" "(\"INNER\" (:struct 8) 20)")))
    (chk "ex-thing-marker LABEL -> (:struct 0) at 28"
         (and (field-row-p p "ex-thing-marker" :struct 28)
              (file-contains "example" "(\"MARKER\" (:struct 0) 28)")))
    (chk "ex-thing-lock BPTR :u32, scale FLOAT :single"
         (and (field-row-p p "ex-thing-lock" :u32 28) (field-row-p p "ex-thing-scale" :single 32)))
    (chk "field setter %SET-EX-THING-X is internal, its DEFSETF registered"
         (multiple-value-bind (s status) (find-symbol "%SET-EX-THING-X" p)
           (and s (eq status :internal) (fboundp s)
                (eq (getf (ffi::%ffi-stub-info s) :kind) :poke))))
    ;; DEFSETF is the whole setter contract: there is no (SETF EX-THING-X)
    ;; function, and asking for one says so by name
    (chk "no (SETF EX-THING-X) function: FDEFINITION signals UNDEFINED-FUNCTION naming it"
         (handler-case (progn (fdefinition (list 'setf (sym p "ex-thing-x"))) nil)
           (undefined-function (e)
             (and (equal (cell-error-name e) (list 'setf (sym p "ex-thing-x")))
                  (search "(SETF EX-THING-X)" (princ-to-string e))
                  t))))
    (chk "accessors work: x/y/flag on foreign memory"
         (let ((m (ffi:alloc-foreign 36)))
           (unwind-protect
                (progn
                  (set-field (sym p "ex-thing-x") m -7)
                  (set-field (sym p "ex-thing-flag") m 200)
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
    (chk "struct accessors exported" (and (external-p p "ex-thing-x") (external-p p "*ex-thing-size*")))
    ;; laziness is invisible to CL: enumeration builds everything, then the
    ;; package is ordinary (same symbols, EQ to the ones touched before)
    (let ((before (sym p "ex-flush")) (n 0))
      (do-symbols (s p) (when (eq (symbol-package s) (find-package p)) (incf n)))
      (chk "do-symbols flips the package eager (table dropped), symbols stay EQ"
           (and (null (clamiga::%binding-table-info p))
                (eq before (sym p "ex-flush"))
                (> n 40)
                (fbound p "ex-flush") (eql (sym-value p "+exf-base+") #x1000)))))
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
                (fn-row-unguarded-p p "mo-version") (fn-row-unguarded-p p "mo-create")
                (fn-row-p p "mo-create" :lvo -36 :regs '(:d0) :result :pointer)
                (file-contains "mosonly" "MO_Create(ULONG size) (D0) LVO -36")))
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
              (fn-row-p p "fixgad-get-class" :lvo -30 :result :pointer)
              (file-contains "gadgets/fixgad" "FIXGAD_GetClass() () LVO -30")
              (fn-row-p p "fixgad-refresh" :lvo -36 :regs '(:a0) :result :void)))
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
    ;; a string #define is a string constant (the class name of a
    ;; ReAction gadget, mui.h's MUIC_* names)
    (chk "C: string macro FIXGAD_Name -> string constant"
         (and (equal (sym-value p "+fixgad-name+") "gadgets/fixgad.gadget")
              (constantp (sym p "+fixgad-name+")) (external-p p "+fixgad-name+")
              (const-row-p p "+fixgad-name+" "gadgets/fixgad.gadget")))
    ;; what is skipped
    (chk "C: float / NewObject / sizeof / statement / function-like macros not emitted"
         (and (not (sym p "+fixgad-float+"))
              (not (sym p "+fix-gad-object+")) (not (sym p "+fixgad-size+"))
              (not (sym p "+fixgad-stmt+")) (not (sym p "+fix-gad-set+"))))
    (chk "C: skipped macros counted in the header (4)"
         (file-contains "gadgets/fixgad" ";;; 4 C macros skipped: not an integer or ASCII-string constant"))
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
              (= 1 (length (rows-named p :const "+fixgad-dummy+")))))
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
              (not (sym p "+make-id+")) (not (sym p "+reaction-reaction-h+")))))
  ;; --- the MUI SDK: an sfd joining the primary table (with private gaps
  ;; and a MorphOS twin), a header under the SECOND C-header root claimed
  ;; through *module-includes*, string constants ---
  (chk "muimaster: libraries/mui.h yields no header-only module (claimed by muimaster)"
       (not (probe-file (raw-file "libraries/mui"))))
  (load-raw "muimaster")
  (let ((p "AMIGA.RAW.MUIMASTER"))
    (chk "muimaster: package, muimaster.library opened at load, header names both SDKs and mui.h"
         (and (find-package p) (boundp (sym p "*muimaster-base*")) (null (sym-value p "*muimaster-base*"))
              (file-contains "muimaster" "(amiga.ffi:open-library-or-die \"muimaster.library\" 0)")
              (file-contains "muimaster" ";;;   MUI 3.8 SDK muimaster_lib.fd + clib/muimaster_protos.h (via fd2sfd)")
              (file-contains "muimaster" ";;;   MorphOS SDK muimaster_lib.fd + clib/muimaster_protos.h (via fd2sfd)")
              (file-contains "muimaster" ";;;   libraries/mui.h")
              (file-contains "muimaster" ";; --- functions (MUI SDK + MorphOS SDK) ---")))
    (chk "muimaster: ##private gaps advance the bias — ObtainPen -60, MakeObjectA -66; shared vectors unguarded"
         (and (fn-row-p p "mui-new-object-a" :lvo -30 :regs '(:a0 :a1) :result :pointer)
              (fn-row-p p "mui-dispose-object" :lvo -36 :regs '(:a0) :result :void)
              (fn-row-p p "mui-request-a" :lvo -42 :regs '(:d0 :d1 :d2 :a0 :a1 :a2 :a3) :result :signed)
              (fn-row-p p "mui-obtain-pen" :lvo -60 :regs '(:a0 :a1 :d0) :result :signed)
              (fn-row-p p "mui-make-object-a" :lvo -66 :regs '(:d0 :a0) :result :pointer)
              (fn-row-unguarded-p p "mui-new-object-a") (fn-row-unguarded-p p "mui-obtain-pen")
              (= 1 (fn-row-count p "mui-obtain-pen"))
              (fbound p "mui-new-object-a")
              (not (sym p "mui-private")) (not (sym p "mui-new-object"))))
    (chk "muimaster: MorphOS-only MUI_GetRGBColor at -690 carries :morphos, defined iff :morphos"
         (and (fn-row-p p "mui-get-rgb-color" :lvo -690 :regs '(:a0 :a1 :a2) :result :signed :guard :morphos)
              (external-p p "mui-get-rgb-color")
              (eq *mos* (fbound p "mui-get-rgb-color"))))
    ;; string constants
    (chk "muimaster: string #defines -> string constants (MUIC_* names, the #else of #ifdef _DCC)"
         (and (equal (sym-value p "+muic-window+") "Window.mui")
              (equal (sym-value p "+muic-notify+") "Notify.mui")
              (equal (sym-value p "+muimaster-name+") "muimaster.library")
              (constantp (sym p "+muic-window+"))
              (external-p p "+muic-window+")
              (const-row-p p "+muic-window+" "Window.mui")
              (file-contains "muimaster" "(:const \"+MUIC-WINDOW+\" \"Window.mui\")")))
    (chk "muimaster: escapes decoded and re-quoted; a control character through a #. form"
         (and (equal (sym-value p "+fix-esc+") "say \"hi\"\\")
              (file-contains "muimaster" "(:const \"+FIX-ESC+\" \"say \\\"hi\\\"\\\\\")")
              (equal (sym-value p "+muix-c+") (format nil "~Cc" (code-char 27)))
              (file-contains "muimaster" "(:const \"+MUIX-C+\" #.(map 'string #'code-char '(27 99)))")))
    (chk "muimaster: concatenation, non-ASCII, MAKE_ID(), string alias and NewObject( shortcuts skipped (6)"
         (and (not (sym p "+fix-cat+")) (not (sym p "+fix-lat1+")) (not (sym p "+fix-id+"))
              (not (sym p "+fix-alias+")) (not (sym p "+window-object+")) (not (sym p "+end+"))
              (not (sym p "+muiv-window-alt-height-screen+")) (not (sym p "+muia-window-open-obsolete+"))
              (file-contains "muimaster" ";;; 6 C macros skipped: not an integer or ASCII-string constant")))
    (chk "muimaster: header counts 6 functions, 25 constants (5 strings)"
         (file-contains "muimaster" ";;; 6 functions, 25 constants (5 of them strings), 0 structs."))
    ;; values
    (chk "muimaster: negative MUIV_, ((STRPTR)~0) -> #xFFFFFFFF, ((STRPTR) 0) -> 0, (1<<0)"
         (and (eql (sym-value p "+muiv-application-return-id-quit+") -1)
              (eql (sym-value p "+mc-template-id+") #xFFFFFFFF)
              (eql (sym-value p "+muiv-application-save-envarc+") #xFFFFFFFF)
              (eql (sym-value p "+muiv-application-save-env+") 0)
              (eql (sym-value p "+mui-ehf-alwayskeys+") 1)
              (eql (sym-value p "+muiv-trigger-value+") #x49893131)
              (eql (sym-value p "+muiv-every-time+") #x49893131)
              (eql (sym-value p "+muimaster-vmin+") 11)))
    (chk "muimaster: the header's own #define MUI_OBSOLETE makes its #ifdef blocks live"
         (and (eql (sym-value p "+muim-application-input+") #x8042D0F5)
              (eql (sym-value p "+muim-notify+") #x8042C9CB)
              (eql (sym-value p "+muim-application-new-input+") #x80423BA6)
              (eql (sym-value p "+muia-window-close-request+") #x8042E86E)
              (not (sym p "+mui-obsolete+")) (not (sym p "+libraries-mui-h+"))))
    (chk "muimaster: enumerators with negative starts; PSD_NUMMUIPENS = MPEN_COUNT forward reference"
         (and (eql (sym-value p "+muikey-release+") -2) (eql (sym-value p "+muikey-none+") -1)
              (eql (sym-value p "+muikey-press+") 0) (eql (sym-value p "+muikey-toggle+") 1)
              (eql (sym-value p "+mpen-shine+") 0) (eql (sym-value p "+mpen-count+") 8)
              (eql (sym-value p "+psd-nummuipens+") 8)))
    (chk "muimaster: C structs of the header are not read" (not (sym p "*muip-notify-size*")))))

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
    ;; every module is a binding table: the package carries one after load,
    ;; and loading materialised nothing beyond the eager defvars/defuns
    (let ((pkgs (remove-if-not (lambda (pk)
                                 (let ((n (package-name pk)))
                                   (and (> (length n) 10) (string= "AMIGA.RAW." n :end2 10))))
                               (list-all-packages))))
      (chk (format nil "all ~D AMIGA.RAW.* packages carry a binding table" (length pkgs))
           (and (>= (length pkgs) 100)
                (every #'clamiga::%binding-table-info pkgs)))
      ;; (the base/version vars, %VERSION>=, and the >7-register DEFCFUNs'
      ;; parameter names — cybergraphics has the most, 46)
      (chk "loading a module materialises only its eager symbols (< 60)"
           (every (lambda (pk) (< (getf (clamiga::%binding-table-info pk) :symbols) 60)) pkgs))
      (chk "intuition: ~1500 table entries in ~47 KB of table"
           (let ((info (clamiga::%binding-table-info "AMIGA.RAW.INTUITION")))
             (and (> (getf info :entries) 1400) (< (getf info :bytes) 60000)))))
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
         (fn-row-p "AMIGA.RAW.INTUITION" "open-window-tag-list" :lvo -606 :regs '(:a0 :a1) :result :pointer))
    (chk "intuition: show-window (V46, private on MorphOS) guarded"
         (fn-row-p "AMIGA.RAW.INTUITION" "show-window" :lvo -834 :guard :not-morphos :min-version 46))
    (chk "intuition: MorphOS-only get-monitor-list iff :morphos"
         (eq *mos* (fbound "AMIGA.RAW.INTUITION" "get-monitor-list")))
    (chk "exec: +memf-chip+ 2, +memf-clear+ #x10000"
         (and (eql (sym-value "AMIGA.RAW.EXEC" "+memf-chip+") 2)
              (eql (sym-value "AMIGA.RAW.EXEC" "+memf-clear+") #x10000)))
    (chk "exec: avail-mem at LVO -216 (:d1 requirements)"
         (and (fn-row-p "AMIGA.RAW.EXEC" "avail-mem" :lvo -216 :regs '(:d1) :result :unsigned)
              (file-contains "exec" "AvailMem(ULONG requirements) (D1) LVO -216")))
    (chk "exec: node 14 / msg-port 34 / io-request 32 / io-std-req 48 / library 34"
         (and (eql (sym-value "AMIGA.RAW.EXEC" "*node-size*") 14)
              (eql (sym-value "AMIGA.RAW.EXEC" "*msg-port-size*") 34)
              (eql (sym-value "AMIGA.RAW.EXEC" "*io-request-size*") 32)
              (eql (sym-value "AMIGA.RAW.EXEC" "*io-std-req-size*") 48)
              (eql (sym-value "AMIGA.RAW.EXEC" "*library-size*") 34)))
    (chk "exec: library-version shadowed (struct accessor != amiga.ffi's)"
         (not (eq (sym "AMIGA.RAW.EXEC" "library-version") 'amiga.ffi:library-version)))
    (chk "exec: NewMinList (V45, clashes on MorphOS) is AmigaOS-only"
         (fn-row-p "AMIGA.RAW.EXEC" "new-min-list" :guard :not-morphos :min-version 45))
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
         (fn-row-p "AMIGA.RAW.DOS" "error-output" :guard :not-morphos))
    (chk "graphics: rastport spelling, *rastport-size* 100"
         (eql (sym-value "AMIGA.RAW.GRAPHICS" "*rastport-size*") 100))
    (chk "graphics: blt-bitmap (11 registers) via call-library"
         (and (fbound "AMIGA.RAW.GRAPHICS" "blt-bitmap")
              (not (stub-info "AMIGA.RAW.GRAPHICS" "blt-bitmap"))))
    (chk "graphics: read-pixel ULONG -> :unsigned, write-pixel LONG -> :signed, set-a-pen :void"
         (and (fn-row-p "AMIGA.RAW.GRAPHICS" "read-pixel" :lvo -318 :regs '(:a1 :d0 :d1) :result :unsigned)
              (fn-row-p "AMIGA.RAW.GRAPHICS" "write-pixel" :lvo -324 :regs '(:a1 :d0 :d1) :result :signed)
              (fn-row-p "AMIGA.RAW.GRAPHICS" "set-a-pen" :lvo -342 :regs '(:a1 :d0) :result :void)))
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
    ;; muimaster: generated from the MUI 3.8 developer kit (primary) + the
    ;; MorphOS SDK (twin) + libraries/mui.h
    (chk "muimaster: sources = MUI 3.8 SDK + MorphOS SDK + libraries/mui.h; 3.8 vectors unguarded"
         (and (probe-file (raw-file "muimaster"))
              (file-contains "muimaster" ";;;   MUI 3.8 SDK muimaster_lib.fd + clib/muimaster_protos.h (via fd2sfd)")
              (file-contains "muimaster" ";;;   MorphOS SDK muimaster_lib.fd + clib/muimaster_protos.h (via fd2sfd)")
              (file-contains "muimaster" ";;;   libraries/mui.h")
              (fn-row-p "AMIGA.RAW.MUIMASTER" "mui-new-object-a" :lvo -30 :regs '(:a0 :a1) :result :pointer)
              (fn-row-unguarded-p "AMIGA.RAW.MUIMASTER" "mui-new-object-a")
              (fbound "AMIGA.RAW.MUIMASTER" "mui-new-object-a")
              (fn-row-p "AMIGA.RAW.MUIMASTER" "mui-request-a" :lvo -42 :regs '(:d0 :d1 :d2 :a0 :a1 :a2 :a3) :result :signed)
              (fn-row-p "AMIGA.RAW.MUIMASTER" "mui-make-object-a" :lvo -120 :regs '(:d0 :a0) :result :pointer)
              (fn-row-p "AMIGA.RAW.MUIMASTER" "mui-obtain-pen" :lvo -156 :regs '(:a0 :a1 :d0) :result :signed)
              (fn-row-p "AMIGA.RAW.MUIMASTER" "mui-end-refresh" :lvo -198 :regs '(:a0 :d0) :result :void)
              (fn-row-unguarded-p "AMIGA.RAW.MUIMASTER" "mui-end-refresh")))
    (chk "muimaster: MUI_GetRGBColor (-690) is MorphOS-only"
         (and (fn-row-p "AMIGA.RAW.MUIMASTER" "mui-get-rgb-color" :lvo -690 :guard :morphos)
              (eq *mos* (fbound "AMIGA.RAW.MUIMASTER" "mui-get-rgb-color"))))
    (chk "muimaster: ~880 entries incl. 79 string constants, table < 40 KB"
         (let ((info (clamiga::%binding-table-info "AMIGA.RAW.MUIMASTER")))
           (and (> (getf info :entries) 850) (< (getf info :entries) 1000)
                (< (getf info :bytes) 40000)
                (= 79 (count-if (lambda (r) (and (eq (first r) :const) (stringp (third r))))
                                (table-rows "AMIGA.RAW.MUIMASTER"))))))
    (chk "muimaster: MUIC_* class names and MUIX_* text styles are strings"
         (and (equal (sym-value "AMIGA.RAW.MUIMASTER" "+muic-application+") "Application.mui")
              (equal (sym-value "AMIGA.RAW.MUIMASTER" "+muic-window+") "Window.mui")
              (equal (sym-value "AMIGA.RAW.MUIMASTER" "+muic-group+") "Group.mui")
              (equal (sym-value "AMIGA.RAW.MUIMASTER" "+muic-text+") "Text.mui")
              (equal (sym-value "AMIGA.RAW.MUIMASTER" "+muimaster-name+") "muimaster.library")
              (equal (sym-value "AMIGA.RAW.MUIMASTER" "+muix-c+") (format nil "~Cc" (code-char 27)))
              (equal (sym-value "AMIGA.RAW.MUIMASTER" "+muix-b+") (format nil "~Cb" (code-char 27)))
              (constantp (sym "AMIGA.RAW.MUIMASTER" "+muic-window+"))))
    (chk "muimaster: tags, methods and values of mui.h"
         (and (eql (sym-value "AMIGA.RAW.MUIMASTER" "+muimaster-vmin+") 11)
              (eql (sym-value "AMIGA.RAW.MUIMASTER" "+muia-application-title+") #x804281B8)
              (eql (sym-value "AMIGA.RAW.MUIMASTER" "+muia-window-close-request+") #x8042E86E)
              (eql (sym-value "AMIGA.RAW.MUIMASTER" "+muia-window-root-object+") #x8042CBA5)
              (eql (sym-value "AMIGA.RAW.MUIMASTER" "+muia-group-child+") #x804226E6)
              (eql (sym-value "AMIGA.RAW.MUIMASTER" "+muia-text-contents+") #x8042F8DC)
              (eql (sym-value "AMIGA.RAW.MUIMASTER" "+muia-pressed+") #x80423535)
              (eql (sym-value "AMIGA.RAW.MUIMASTER" "+muim-notify+") #x8042C9CB)
              (eql (sym-value "AMIGA.RAW.MUIMASTER" "+muim-application-new-input+") #x80423BA6)
              (eql (sym-value "AMIGA.RAW.MUIMASTER" "+muim-application-return-id+") #x804276EF)
              (eql (sym-value "AMIGA.RAW.MUIMASTER" "+muiv-application-return-id-quit+") -1)
              (eql (sym-value "AMIGA.RAW.MUIMASTER" "+muiv-notify-application+") 3)
              (eql (sym-value "AMIGA.RAW.MUIMASTER" "+muiv-trigger-value+") #x49893131)
              (eql (sym-value "AMIGA.RAW.MUIMASTER" "+muiv-every-time+") #x49893131)
              (eql (sym-value "AMIGA.RAW.MUIMASTER" "+muii-window-back+") 0)
              (eql (sym-value "AMIGA.RAW.MUIMASTER" "+muio-button+") 2)
              (eql (sym-value "AMIGA.RAW.MUIMASTER" "+mui-maxmax+") 10000)
              (eql (sym-value "AMIGA.RAW.MUIMASTER" "+mc-template-id+") #xFFFFFFFF)
              (eql (sym-value "AMIGA.RAW.MUIMASTER" "+muikey-release+") -2)
              ;; the header's own #define MUI_OBSOLETE keeps these live
              (eql (sym-value "AMIGA.RAW.MUIMASTER" "+muim-application-input+") #x8042D0F5)))
    ;; ReAction class libraries: under gadgets/ images/ classes/, with the
    ;; tags that exist only in the NDK's C headers
    (chk "class libs: no top-level button / bevel / window modules"
         (and (not (probe-file (raw-file "button"))) (not (probe-file (raw-file "bevel")))
              (not (probe-file (raw-file "window"))) (not (probe-file (raw-file "layout")))))
    ;; (the class functions are V40+ guarded, so they are unbound on the host)
    (chk "gadgets/button: BUTTON_GetClass + tags (BUTTON_Dummy TAG_USER+0x04000000)"
         (and (fn-row-p "AMIGA.RAW.GADGETS.BUTTON" "button-get-class" :lvo -30 :regs '() :result :pointer)
              (file-contains "gadgets/button" "(amiga.ffi:open-library-or-die \"gadgets/button.gadget\" 0)")
              (eql (sym-value "AMIGA.RAW.GADGETS.BUTTON" "+button-dummy+") #x84000000)
              (eql (sym-value "AMIGA.RAW.GADGETS.BUTTON" "+button-justification+") #x84000010)
              ;; BUTTON_RenderImage is an alias of GA_Image
              (eql (sym-value "AMIGA.RAW.GADGETS.BUTTON" "+button-render-image+")
                   (sym-value "AMIGA.RAW.INTUITION" "+ga-image+"))))
    (chk "gadgets/layout: LAYOUT_AddChild, RethinkLayout"
         (and (eql (sym-value "AMIGA.RAW.GADGETS.LAYOUT" "+layout-add-child+") #x85007014)
              (fn-row-p "AMIGA.RAW.GADGETS.LAYOUT" "rethink-layout" :lvo -48 :regs '(:a0 :a1 :a2 :d0))))
    (chk "images/bevel: opened as images/bevel.image, BEVEL_Dummy"
         (and (file-contains "images/bevel" "(amiga.ffi:open-library-or-die \"images/bevel.image\" 0)")
              (eql (sym-value "AMIGA.RAW.IMAGES.BEVEL" "+bevel-dummy+") #x85016000)))
    (chk "classes/window: window.class, WINDOW_Position, WMHI_CLOSEWINDOW (1<<16), WMHI_IGNORE (~0L)"
         (and (file-contains "classes/window" "(amiga.ffi:open-library-or-die \"window.class\" 0)")
              (fn-row-p "AMIGA.RAW.CLASSES.WINDOW" "window-get-class" :lvo -30 :regs '() :result :pointer)
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
;; An error between checks (not inside a CHK) must not leave a green-looking
;; "fail=0" summary behind with the remaining checks unrun.
(handler-case
    (cond ((string= *mode* "fixture") (fixture-checks))
          ((string= *mode* "committed") (committed-checks))
          (t (error "unknown BINDGEN_CHECK mode ~S" *mode*)))
  (error (e)
    (incf *fail*)
    (format t "FAIL checks aborted by an error outside a check -- ERROR: ~A~%"
            (substitute #\Space #\Newline (princ-to-string e)))))

(format t "~%BINDGEN-RESULT mode=~A morphos=~A pass=~D fail=~D~%" *mode* *mos* *pass* *fail*)
