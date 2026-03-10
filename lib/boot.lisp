;;; boot.lisp — Bootstrap functions for CL-Amiga
;;; Loaded at startup to provide common CL conveniences.

;; Standard CL variables (set early, before any macro expansion)
(setq *macroexpand-hook* #'funcall)

;; Composite CAR/CDR accessors (all 28 from CL spec)
(defun cadr (x) (car (cdr x)))
(defun caar (x) (car (car x)))
(defun cdar (x) (cdr (car x)))
(defun cddr (x) (cdr (cdr x)))
(defun caaar (x) (car (car (car x))))
(defun cdaar (x) (cdr (car (car x))))
(defun cddar (x) (cdr (cdr (car x))))
(defun caddr (x) (car (cdr (cdr x))))
(defun cadar (x) (car (cdr (car x))))
(defun caadr (x) (car (car (cdr x))))
(defun cdadr (x) (cdr (car (cdr x))))
(defun cdddr (x) (cdr (cdr (cdr x))))
(defun caaaar (x) (car (car (car (car x)))))
(defun cadaar (x) (car (cdr (car (car x)))))
(defun caddar (x) (car (cdr (cdr (car x)))))
(defun cadddr (x) (car (cdr (cdr (cdr x)))))
(defun cdaaar (x) (cdr (car (car (car x)))))
(defun cddaar (x) (cdr (cdr (car (car x)))))
(defun cdddar (x) (cdr (cdr (cdr (car x)))))
(defun cddddr (x) (cdr (cdr (cdr (cdr x)))))
(defun caadar (x) (car (car (cdr (car x)))))
(defun caaddr (x) (car (car (cdr (cdr x)))))
(defun cdadar (x) (cdr (car (cdr (car x)))))
(defun cdaddr (x) (cdr (car (cdr (cdr x)))))
(defun caaadr (x) (car (car (car (cdr x)))))
(defun cadadr (x) (car (cdr (car (cdr x)))))
(defun cdaadr (x) (cdr (car (car (cdr x)))))
(defun cddadr (x) (cdr (cdr (car (cdr x)))))

;; Utility functions
(defun identity (x) x)
(defun endp (x) (null x))

;; prog1 / prog2
(defmacro prog1 (first &rest body) (let ((g (gensym))) `(let ((,g ,first)) ,@body ,g)))
(defmacro prog2 (first second &rest body) (let ((g (gensym))) `(progn ,first (let ((,g ,second)) ,@body ,g))))

;; setf modify macros
(defmacro push (item place) `(setf ,place (cons ,item ,place)))
(defmacro pop (place) (let ((g (gensym))) `(let ((,g (car ,place))) (setf ,place (cdr ,place)) ,g)))
(defmacro incf (place &optional (delta 1)) `(setf ,place (+ ,place ,delta)))
(defmacro decf (place &optional (delta 1)) `(setf ,place (- ,place ,delta)))
(defsetf elt %setf-elt)

;; rotatef — rotate values among places
(defmacro rotatef (&rest places)
  (if (< (length places) 2)
      nil
      (let ((temps (mapcar (lambda (x) (declare (ignore x)) (gensym)) places)))
        `(let ,(mapcar #'list temps places)
           ,@(mapcar (lambda (p v) `(setf ,p ,v))
                     places
                     (append (cdr temps) (list (car temps))))
           nil))))

;; shiftf — shift values among places, return first
(defmacro shiftf (&rest places-and-newval)
  (let* ((places (butlast places-and-newval))
         (newval (car (last places-and-newval)))
         (temps (mapcar (lambda (x) (declare (ignore x)) (gensym)) places))
         (tval (gensym)))
    `(let (,@(mapcar #'list temps places)
           (,tval ,newval))
       ,@(mapcar (lambda (p v) `(setf ,p ,v))
                 places
                 (append (cdr temps) (list tval)))
       ,(car temps))))

;; define-modify-macro — define a macro that modifies a place
(defmacro define-modify-macro (name lambda-list function &optional documentation)
  (let ((place-var (gensym "PLACE"))
        (has-rest (member '&rest lambda-list))
        (plain-args (remove '&rest (remove '&optional lambda-list))))
    `(defmacro ,name (,place-var ,@lambda-list)
       ,@(when documentation (list documentation))
       (list 'setf ,place-var
             ,(if has-rest
                  `(list* ',function ,place-var ,@plain-args)
                  `(list ',function ,place-var ,@plain-args))))))

;; Compiler macros (no-op — CL-Amiga doesn't optimize via compiler macros)
(defmacro define-compiler-macro (name lambda-list &body body)
  (declare (ignore lambda-list body))
  `',name)

(defun compiler-macro-function (name &optional env)
  (declare (ignore name env))
  nil)

;; Setf expanders (stub — CL-Amiga uses defsetf for setf places)
(defmacro define-setf-expander (access-fn lambda-list &body body)
  (declare (ignore lambda-list body))
  `',access-fn)

;; List searching
(defun member (item list &key (test #'eql))
  (do ((l list (cdr l)))
      ((null l) nil)
    (when (funcall test item (car l))
      (return-from member l))))

;; pushnew — push only if not already a member
(defmacro pushnew (item place &key test)
  (let ((g (gensym)))
    (if test
        `(let ((,g ,item))
           (unless (member ,g ,place :test ,test)
             (setf ,place (cons ,g ,place)))
           ,place)
        `(let ((,g ,item))
           (unless (member ,g ,place)
             (setf ,place (cons ,g ,place)))
           ,place))))

;; Set operations
(defun intersection (list1 list2 &key (test #'eql))
  (let ((result nil))
    (dolist (x list1 (nreverse result))
      (when (member x list2 :test test)
        (push x result)))))

(defun union (list1 list2 &key (test #'eql))
  (let ((result (copy-list list1)))
    (dolist (x list2 (nreverse result))
      (unless (member x list1 :test test)
        (push x result)))))

(defun set-difference (list1 list2 &key (test #'eql))
  (let ((result nil))
    (dolist (x list1 (nreverse result))
      (unless (member x list2 :test test)
        (push x result)))))

(defun subsetp (list1 list2 &key (test #'eql))
  (dolist (x list1 t)
    (unless (member x list2 :test test)
      (return-from subsetp nil))))

;; documentation — CL standard documentation function
(defvar *documentation-table* (make-hash-table :test 'equal))

(defun documentation (obj doc-type)
  (gethash (cons obj doc-type) *documentation-table*))

(defun %set-documentation (obj doc-type string)
  (setf (gethash (cons obj doc-type) *documentation-table*) string))

(defsetf documentation %set-documentation)

;; with-open-file — defined early so compile-file can use it.
;; Dependencies: open, close (C builtins), unwind-protect/locally/when (special forms).
(defmacro with-open-file (spec &body body)
  (let ((var (car spec))
        (open-args (cdr spec)))
    `(let ((,var (open ,@open-args)))
       (unwind-protect
         (locally ,@body)
         (when ,var (close ,var))))))

;; compile-file and compile-file-pathname are C builtins (builtins_io.c).
;; compile-file produces real FASL binary files that load without reparsing.

(defmacro with-compilation-unit ((&rest options) &body body)
  (declare (ignore options))
  `(progn ,@body))

(defmacro with-standard-io-syntax (&body body)
  `(let ((*package* (find-package :common-lisp-user))
         (*print-base* 10)
         (*print-radix* nil)
         (*print-circle* nil)
         (*print-escape* t)
         (*print-readably* nil)
         (*read-base* 10)
         (*read-eval* t))
     ,@body))

(defmacro print-unreadable-object ((object stream &key type identity) &body body)
  (let ((obj-var (gensym)))
    `(let ((,obj-var ,object))
       (write-string "#<" ,stream)
       ,@(when type `((write-string (symbol-name (type-of ,obj-var)) ,stream)
                       ,(when body `(write-char #\Space ,stream))))
       ,@body
       (write-string ">" ,stream)
       nil)))

;; handler-case — run clause bodies after unwinding
;; Uses catch/throw + cons box because:
;;   - handlers run in separate VM context (cl_vm_apply), so return-from can't
;;     reach the establishing block
;;   - closures use value capture, so setq on outer variables doesn't propagate;
;;     rplaca on a shared cons cell does propagate
(defmacro handler-case (form &rest clauses)
  (let ((tag (gensym "HC"))
        (box (gensym "BOX")))
    `(let ((,box (cons nil nil)))
       (let ((result (catch ',tag
                       (handler-bind
                         ,(mapcar (lambda (clause)
                                    `(,(car clause) (lambda (c)
                                                      (rplaca ,box c)
                                                      (throw ',tag ',tag))))
                                  clauses)
                         ,form))))
         (if (eq result ',tag)
             (typecase (car ,box)
               ,@(mapcar (lambda (clause)
                           (let ((type (car clause))
                                 (arglist (cadr clause))
                                 (body (cddr clause)))
                             `(,type (let ((,(if arglist (car arglist) (gensym)) (car ,box)))
                                       ,@body))))
                         clauses))
             result)))))

;; ignore-errors — catch errors, return (values nil condition)
(defmacro ignore-errors (&rest body)
  `(handler-case (progn ,@body)
     (error (c) (values nil c))))

;; with-simple-restart — establish a named restart that returns (values nil t)
(defmacro with-simple-restart (restart-spec &rest body)
  (let ((name (car restart-spec))
        (format-control (cadr restart-spec)))
    `(restart-case (progn ,@body)
       (,name () :report ,format-control (values nil t)))))

;; cerror — continuable error: establish CONTINUE restart around error
(defun cerror (format-control datum &rest args)
  (restart-case (apply #'error datum args)
    (continue () :report format-control nil)))

;; %set-condition-default-initargs — stub (default initargs not yet used)
(defun %set-condition-default-initargs (name initargs)
  (declare (ignore name initargs))
  nil)

;; define-condition — define a user condition type with slots and readers
(defmacro define-condition (name parent-types slot-specs &rest options)
  (let ((parent (cond ((consp parent-types) (car parent-types))
                      ((null parent-types) 'condition)
                      (t parent-types)))
        (parents-list (cond ((consp parent-types) parent-types)
                            ((null parent-types) '(condition))
                            (t (list parent-types))))
        (slot-pairs (mapcar (lambda (spec) (cons (car spec) (getf (cdr spec) :initarg))) slot-specs))
        (report nil)
        (default-initargs nil)
        (clos-available (fboundp '%add-method-to-gf)))
    ;; Parse options
    (dolist (opt options)
      (case (car opt)
        (:report (setq report (cadr opt)))
        (:default-initargs (setq default-initargs (cdr opt)))))
    `(progn
       (%register-condition-type ',name ',parent ',slot-pairs)
       (when (fboundp '%register-condition-class)
         (%register-condition-class ',name ',parents-list))
       ,@(mapcan (lambda (slot-spec)
                   (let* ((slot-name (car slot-spec))
                          (opts (cdr slot-spec))
                          (reader (or (getf opts :reader)
                                      (getf opts :accessor)))
                          (accessor (getf opts :accessor)))
                     (append
                       (when reader
                         (if clos-available
                             (list `(defmethod ,reader ((c ,name)) (condition-slot-value c ',slot-name)))
                             (list `(defun ,reader (c) (condition-slot-value c ',slot-name)))))
                       (when accessor
                         (let ((setter-name (intern (concatenate 'string
                                                     "%SETF-" (symbol-name accessor))
                                                   (or (symbol-package accessor) *package*))))
                           (list `(defun ,setter-name (val c)
                                    (%set-condition-slot-value c ',slot-name val))))))))
                 slot-specs)
       ,@(when report
           (if (stringp report)
             `((defmethod print-object ((c ,name) stream)
                 (write-string ,report stream)))
             `((defmethod print-object ((c ,name) stream)
                 (funcall ,report c stream)))))
       ,@(when default-initargs
           `((%set-condition-default-initargs ',name ',default-initargs)))
       ',name)))

;; check-type — signal type-error if place is not of type
(defmacro check-type (place type &optional type-string)
  (let ((val (gensym)))
    `(let ((,val ,place))
       (unless (typep ,val ',type)
         (error 'type-error :datum ,val :expected-type ',type)))))

;; assert — signal error if test-form is false
(defmacro assert (test-form &optional places string &rest args)
  `(unless ,test-form
     (error 'simple-error
            :format-control ,(or string "Assertion failed: ~S")
            :format-arguments (list ',test-form))))

;; defpackage — define a package with :use, :export, :nicknames, :local-nicknames,
;;   :import-from, :shadow, :shadowing-import-from, :intern options
(defmacro defpackage (name &rest options)
  (let ((pkg-name (if (symbolp name) (symbol-name name) name))
        (uses nil)
        (exports nil)
        (nicknames nil)
        (local-nicks nil)
        (import-froms nil)
        (shadows nil)
        (shadowing-import-froms nil)
        (interns nil))
    ;; Parse options
    (dolist (opt options)
      (case (car opt)
        (:use (setq uses (append uses (cdr opt))))
        (:export (setq exports (append exports (cdr opt))))
        (:nicknames (setq nicknames (append nicknames (cdr opt))))
        (:local-nicknames (setq local-nicks (cdr opt)))
        (:import-from (setq import-froms (cons (cdr opt) import-froms)))
        (:shadow (setq shadows (append shadows (cdr opt))))
        (:shadowing-import-from (setq shadowing-import-froms (cons (cdr opt) shadowing-import-froms)))
        (:intern (setq interns (append interns (cdr opt))))))
    `(progn
       (let ((pkg (or (find-package ,pkg-name)
                      (make-package ,pkg-name :nicknames ',nicknames))))
         ,@(when shadows
             `((dolist (s ',shadows)
                 (shadow (if (symbolp s) (symbol-name s) s) pkg))))
         ,@(when uses
             `((dolist (u ',uses)
                 (use-package (or (find-package (if (symbolp u) (symbol-name u) u))
                                  (error "Package ~A not found" u))
                              pkg))))
         ,@(mapcar (lambda (import-spec)
                     (let ((from-pkg (car import-spec))
                           (syms (cdr import-spec)))
                       `(let ((src-pkg (or (find-package ,(if (symbolp from-pkg)
                                                             (symbol-name from-pkg)
                                                             from-pkg))
                                           (error "Package ~A not found" ',from-pkg))))
                          (dolist (s ',syms)
                            (let ((sym (find-symbol (if (symbolp s) (symbol-name s) s) src-pkg)))
                              (when sym (import sym pkg)))))))
                   import-froms)
         ,@(mapcar (lambda (import-spec)
                     (let ((from-pkg (car import-spec))
                           (syms (cdr import-spec)))
                       `(let ((src-pkg (or (find-package ,(if (symbolp from-pkg)
                                                             (symbol-name from-pkg)
                                                             from-pkg))
                                           (error "Package ~A not found" ',from-pkg))))
                          (dolist (s ',syms)
                            (let ((sym (find-symbol (if (symbolp s) (symbol-name s) s) src-pkg)))
                              (when sym (shadowing-import sym pkg)))))))
                   shadowing-import-froms)
         ,@(when interns
             `((dolist (s ',interns)
                 (intern (if (symbolp s) (symbol-name s) s) pkg))))
         ,@(when exports
             `((dolist (e ',exports)
                 (export (intern (if (symbolp e) (symbol-name e) e) pkg) pkg))))
         ,@(when local-nicks
             `((dolist (ln ',local-nicks)
                 (add-package-local-nickname
                  (car ln)
                  (or (find-package (if (symbolp (cadr ln)) (symbol-name (cadr ln)) (cadr ln)))
                      (error "Package ~A not found for local nickname" (cadr ln)))
                  pkg))))
         pkg))))

;; do-symbols — iterate over all symbols in a package
(defmacro do-symbols (spec &body body)
  (let ((var (car spec))
        (package (cadr spec))
        (result (caddr spec))
        (pkg (gensym)) (syms (gensym)) (s (gensym)))
    `(let* ((,pkg ,(if package `(find-package ,package) '*package*))
            (,syms (%package-symbols ,pkg)))
       (dolist (,s ,syms ,result)
         (let ((,var ,s))
           ,@body)))))

;; do-external-symbols — iterate over exported symbols in a package
(defmacro do-external-symbols (spec &body body)
  (let ((var (car spec))
        (package (cadr spec))
        (result (caddr spec))
        (pkg (gensym)) (syms (gensym)) (s (gensym)))
    `(let* ((,pkg ,(if package `(find-package ,package) '*package*))
            (,syms (%package-external-symbols ,pkg)))
       (dolist (,s ,syms ,result)
         (let ((,var ,s))
           ,@body)))))

;; defstruct — define a named structure type
;; Supports options: :conc-name, :constructor, :predicate, :copier, :include
(defun %defstruct-parse-slot (spec)
  "Parse a slot spec into (name default). Accepts NAME or (NAME DEFAULT)."
  (if (consp spec)
      (list (car spec) (cadr spec))
      (list spec nil)))

(defmacro defstruct (name-and-options &rest slot-specs)
  (let* ((name (if (consp name-and-options) (car name-and-options) name-and-options))
         (options (if (consp name-and-options) (cdr name-and-options) nil))
         (name-str (symbol-name name))
         ;; Parse options
         (conc-name-opt nil) (conc-name-set nil)
         (constructor-opt nil) (constructor-set nil)
         (predicate-opt nil) (predicate-set nil)
         (copier-opt nil) (copier-set nil)
         (include-name nil) (include-slots nil))
    ;; Process options
    (dolist (opt options)
      (cond
        ((and (consp opt) (eq (car opt) :conc-name))
         (setq conc-name-set t)
         (setq conc-name-opt (cadr opt)))
        ((eq opt :conc-name)
         (setq conc-name-set t)
         (setq conc-name-opt nil))
        ((and (consp opt) (eq (car opt) :constructor))
         (setq constructor-set t)
         (setq constructor-opt (cadr opt)))
        ((eq opt :constructor)
         (setq constructor-set t)
         (setq constructor-opt nil))
        ((and (consp opt) (eq (car opt) :predicate))
         (setq predicate-set t)
         (setq predicate-opt (cadr opt)))
        ((eq opt :predicate)
         (setq predicate-set t)
         (setq predicate-opt nil))
        ((and (consp opt) (eq (car opt) :copier))
         (setq copier-set t)
         (setq copier-opt (cadr opt)))
        ((eq opt :copier)
         (setq copier-set t)
         (setq copier-opt nil))
        ((and (consp opt) (eq (car opt) :include))
         (setq include-name (cadr opt)))))
    ;; Compute inherited slots from :include (with defaults)
    (when include-name
      (let ((parent-specs (%struct-slot-specs include-name)))
        (dolist (spec parent-specs)
          (push spec include-slots))
        (setq include-slots (reverse include-slots))))
    ;; Build full slot list: inherited + own
    (let* ((own-parsed (mapcar #'%defstruct-parse-slot slot-specs))
           (all-slots (append include-slots own-parsed))
           (n-slots (length all-slots))
           (slot-names (mapcar #'car all-slots))
           ;; Compute conc-name prefix (user supplies the full prefix incl. separator)
           (prefix (cond
                     ((not conc-name-set) (concatenate 'string name-str "-"))
                     ((null conc-name-opt) "")
                     (t (symbol-name conc-name-opt))))
           ;; Constructor name
           (ctor-name (cond
                        ((not constructor-set) (intern (concatenate 'string "MAKE-" name-str)))
                        ((null constructor-opt) nil)
                        (t constructor-opt)))
           ;; Predicate name
           (pred-name (cond
                        ((not predicate-set) (intern (concatenate 'string name-str "-P")))
                        ((null predicate-opt) nil)
                        (t predicate-opt)))
           ;; Copier name
           (copy-name (cond
                        ((not copier-set) (intern (concatenate 'string "COPY-" name-str)))
                        ((null copier-opt) nil)
                        (t copier-opt)))
           (forms nil))
      ;; Register the struct type (store slot specs with defaults for :include)
      (push `(%register-struct-type ',name ,n-slots
               ,(if include-name `',include-name nil)
               ',all-slots)
            forms)
      ;; Constructor
      (when ctor-name
        (let ((key-params (mapcar (lambda (s)
                                    (list (car s) (cadr s)))
                                  all-slots)))
          (push `(defun ,ctor-name (&key ,@key-params)
                   (%make-struct ',name ,@(mapcar #'car all-slots)))
                forms)))
      ;; Predicate
      (when pred-name
        (push `(defun ,pred-name (obj) (typep obj ',name))
              forms))
      ;; Copier
      (when copy-name
        (push `(defun ,copy-name (obj) (%copy-struct obj))
              forms))
      ;; Accessors and setf writers
      (let ((idx 0))
        (dolist (sname slot-names)
          (let* ((acc-name (intern (concatenate 'string prefix (symbol-name sname))))
                 (setter-name (intern (concatenate 'string "%SET-" (symbol-name acc-name)))))
            (push `(defun ,acc-name (obj) (%struct-ref obj ,idx)) forms)
            (push `(defun ,setter-name (obj val) (%struct-set obj ,idx val)) forms)
            (push `(defsetf ,acc-name ,setter-name) forms))
          (setq idx (+ idx 1))))
      ;; Register struct as CLOS class if CLOS is loaded
      (push `(when (fboundp '%make-bootstrap-class)
               (%make-bootstrap-class ',name
                 (list (find-class ',(or include-name 'structure-object)))))
            forms)
      `(progn ,@(reverse forms) ',name))))

;; psetq — parallel setq: evaluate all values, then assign
(defmacro psetq (&rest pairs)
  (let ((temps nil) (sets nil) (p pairs))
    (do () ((null p))
      (let ((var (car p))
            (val (cadr p))
            (tmp (gensym)))
        (push (list tmp val) temps)
        (push `(setq ,var ,tmp) sets)
        (setq p (cddr p))))
    `(let ,(reverse temps) ,@(reverse sets) nil)))

;; psetf — parallel setf: evaluate all values, then assign
(defmacro psetf (&rest pairs)
  (let ((temps nil) (sets nil) (p pairs))
    (do () ((null p))
      (let ((place (car p))
            (val (cadr p))
            (tmp (gensym)))
        (push (list tmp val) temps)
        (push `(setf ,place ,tmp) sets)
        (setq p (cddr p))))
    `(let ,(reverse temps) ,@(reverse sets) nil)))

;; read-from-string — read an S-expression from a string
(defun read-from-string (string &optional eof-error-p eof-value)
  (let ((s (make-string-input-stream string)))
    (read s eof-error-p eof-value)))

;; read-preserving-whitespace — same as read for our implementation
(defun read-preserving-whitespace (&optional stream (eof-error-p t) eof-value recursive-p)
  (declare (ignore recursive-p))
  (read stream eof-error-p eof-value))

;; break — enter debugger with CONTINUE restart
(defun break (&optional (format-control "Break"))
  (restart-case
    (invoke-debugger (make-condition 'simple-condition
                                     :format-control format-control))
    (continue () nil)))

;; --- String stream macros ---

(defmacro with-output-to-string (spec &body body)
  (let ((var (car spec)))
    `(let ((,var (make-string-output-stream)))
       (unwind-protect
         (locally ,@body (get-output-stream-string ,var))
         (close ,var)))))

(defmacro with-input-from-string (spec &body body)
  (let ((var (car spec))
        (string (cadr spec)))
    `(let ((,var (make-string-input-stream ,string)))
       (unwind-protect
         (locally ,@body)
         (close ,var)))))

;; with-open-file — open a file stream, execute body, ensure close
;; with-open-file defined earlier in boot.lisp (before compile-file)

;; --- Stream output control ---
;; No-ops: console/file output is unbuffered, socket send() is immediate.

(defun finish-output (&optional (stream *standard-output*))
  (declare (ignore stream))
  nil)

(defun force-output (&optional (stream *standard-output*))
  (declare (ignore stream))
  nil)

(defun clear-output (&optional (stream *standard-output*))
  (declare (ignore stream))
  nil)

;; --- Byte and sequence I/O ---

(defun write-byte (byte &optional stream)
  (write-char (code-char byte) stream)
  byte)

(defun read-byte (stream &optional (eof-error-p t) eof-value)
  (let ((ch (read-char stream eof-error-p nil)))
    (if ch (char-code ch) (if eof-error-p (error "End of file on ~A" stream) eof-value))))

(defun write-sequence (sequence stream &key (start 0) end)
  (let ((e (or end (length sequence))))
    (do ((i start (1+ i)))
        ((>= i e) sequence)
      (let ((elt (elt sequence i)))
        (if (characterp elt)
            (write-char elt stream)
            (write-byte elt stream))))))

(defun read-sequence (sequence stream &key (start 0) end)
  (let ((e (or end (length sequence)))
        (i start))
    ;; Cannot use loop here — loop macro is defined later in boot.lisp
    (block nil
      (tagbody
       loop-top
       (when (>= i e) (go loop-end))
       (let ((b (read-byte stream nil nil)))
         (if b
             (progn (setf (aref sequence i) b) (setf i (1+ i)))
             (return i)))
       (go loop-top)
       loop-end))
    i))

;; with-standard-io-syntax — bind I/O variables to standard values
;; Minimal implementation: only binds *package* to CL-USER for now.
;; Future: bind *readtable*, *print-readably*, etc. when implemented.
(defmacro with-standard-io-syntax (&body body)
  `(let ((*package* (find-package "CL-USER")))
     ,@body))

;; --- Printer-to-string functions ---

(defun prin1-to-string (object)
  (with-output-to-string (s) (prin1 object s)))

(defun princ-to-string (object)
  (with-output-to-string (s) (princ object s)))


;; --- Time functions (Step 10) ---

(defun decode-universal-time (universal-time &optional time-zone)
  "Decode universal time into 9 values: sec min hour date month year dow dst tz"
  (let* ((tz (or time-zone 0))
         (adjusted (- universal-time (* tz 3600)))
         ;; Seconds within the day
         (day-seconds (mod adjusted 86400))
         (total-days (truncate adjusted 86400))
         ;; Time components
         (second (mod day-seconds 60))
         (minute (mod (truncate day-seconds 60) 60))
         (hour (truncate day-seconds 3600))
         ;; Date computation — days since 1900-01-01 (Monday)
         ;; Day of week: 0=Monday, 6=Sunday
         (dow (mod total-days 7))
         ;; Year calculation with leap year handling
         (remaining-days total-days)
         (year 1900)
         (leap nil)
         (month 1)
         (date 1))
    ;; Find year using tagbody/go
    (block find-year
      (tagbody
       year-loop
        (setq leap (and (zerop (mod year 4))
                        (or (not (zerop (mod year 100)))
                            (zerop (mod year 400)))))
        (let ((days-in-year (if leap 366 365)))
          (when (< remaining-days days-in-year)
            (return-from find-year))
          (setq remaining-days (- remaining-days days-in-year))
          (setq year (+ year 1)))
        (go year-loop)))
    ;; Find month (remaining-days is 0-based day within year)
    (let ((month-days (if leap
                          '(31 29 31 30 31 30 31 31 30 31 30 31)
                          '(31 28 31 30 31 30 31 31 30 31 30 31))))
      (dolist (md month-days)
        (when (< remaining-days md)
          (setq date (+ remaining-days 1))
          (return))
        (setq remaining-days (- remaining-days md))
        (setq month (+ month 1))))
    (values second minute hour date month year dow nil tz)))

(defun encode-universal-time (second minute hour date month year &optional time-zone)
  "Encode time components into universal time."
  (let* ((tz (or time-zone 0))
         ;; Count days from 1900-01-01 to year-01-01
         (y (- year 1900))
         (total-days (+ (* y 365)
                        (truncate (+ y 3) 4)        ; leap years (every 4)
                        (- (truncate (+ y 99) 100)) ; minus century years
                        (truncate (+ y 399) 400)))   ; plus 400-year cycles
         ;; Add days for months within the year
         (leap (and (zerop (mod year 4))
                    (or (not (zerop (mod year 100)))
                        (zerop (mod year 400)))))
         (month-days (if leap
                         '(0 31 60 91 121 152 182 213 244 274 305 335)
                         '(0 31 59 90 120 151 181 212 243 273 304 334))))
    (setq total-days (+ total-days (nth (- month 1) month-days) (- date 1)))
    (+ (* total-days 86400) (* hour 3600) (* minute 60) second (* tz 3600))))

(defun get-decoded-time ()
  "Return the current time decoded into 9 values."
  (decode-universal-time (get-universal-time)))

;; --- Phase 8 Step 4: Higher-order functions ---
(defun complement (fn)
  "Return a function that returns the opposite of FN."
  (lambda (&rest args) (not (apply fn args))))

(defun constantly (value)
  "Return a function that always returns VALUE."
  (lambda (&rest args) value))

;; --- Phase 8 Step 1: Missing list operations ---

(defun tree-equal (a b &key (test #'eql))
  "Compare two trees recursively using TEST."
  (cond
    ((funcall test a b) t)
    ((and (consp a) (consp b))
     (and (tree-equal (car a) (car b) :test test)
          (tree-equal (cdr a) (cdr b) :test test)))
    (t nil)))

(defun list-length (list)
  "Return the length of LIST, or NIL if circular (tortoise-and-hare)."
  (do ((n 0 (+ n 2))
       (slow list (cddr slow))
       (fast list))
      (nil)
    (when (null fast) (return n))
    (setq fast (cdr fast))
    (when (null fast) (return (+ n 1)))
    (when (eq slow fast) (return nil))
    (setq fast (cdr fast))))

(defun tailp (object list)
  "Return true if OBJECT is EQL to LIST or any CDR of LIST."
  (do ((l list (cdr l)))
      ((atom l) (eql object l))
    (when (eql object l) (return t))))

(defun ldiff (list object)
  "Return a copy of LIST up to but not including the tail OBJECT."
  (let ((result nil))
    (do ((l list (cdr l)))
        ((or (atom l) (eql l object)) (nreverse result))
      (push (car l) result))))

(defun revappend (list tail)
  "Non-destructively reverse LIST and append TAIL."
  (do ((l list (cdr l))
       (result tail (cons (car l) result)))
      ((null l) result)))

(defun nreconc (list tail)
  "Destructively reverse LIST and append TAIL."
  (do ((l list)
       (next nil))
      ((null l) tail)
    (setq next (cdr l))
    (rplacd l tail)
    (setq tail l)
    (setq l next)))

(defun assoc-if (predicate alist &key key)
  "Return first pair in ALIST where PREDICATE is true of the key."
  (dolist (pair alist nil)
    (when (consp pair)
      (when (funcall predicate (if key (funcall key (car pair)) (car pair)))
        (return pair)))))

(defun assoc-if-not (predicate alist &key key)
  "Return first pair in ALIST where PREDICATE is false of the key."
  (dolist (pair alist nil)
    (when (consp pair)
      (unless (funcall predicate (if key (funcall key (car pair)) (car pair)))
        (return pair)))))

(defun rassoc-if (predicate alist &key key)
  "Return first pair in ALIST where PREDICATE is true of the value."
  (dolist (pair alist nil)
    (when (consp pair)
      (when (funcall predicate (if key (funcall key (cdr pair)) (cdr pair)))
        (return pair)))))

(defun rassoc-if-not (predicate alist &key key)
  "Return first pair in ALIST where PREDICATE is false of the value."
  (dolist (pair alist nil)
    (when (consp pair)
      (unless (funcall predicate (if key (funcall key (cdr pair)) (cdr pair)))
        (return pair)))))

;; --- make-sequence ---
(defun make-sequence (type size &key (initial-element nil ie-p))
  "Create a sequence of the given TYPE and SIZE."
  (let ((tname (if (symbolp type) (symbol-name type) "")))
    (cond
      ((or (string= tname "LIST"))
       (if ie-p
           (make-list size :initial-element initial-element)
           (make-list size)))
      ((or (string= tname "VECTOR")
           (string= tname "SIMPLE-VECTOR")
           (string= tname "ARRAY"))
       (if ie-p
           (make-array size :initial-element initial-element)
           (make-array size)))
      ((string= tname "STRING")
       (if ie-p
           (make-string size :initial-element initial-element)
           (make-string size)))
      ;; Handle (vector element-type) and (simple-array element-type (size)) compound specs
      ((and (consp type)
            (let ((head (symbol-name (car type))))
              (or (string= head "VECTOR") (string= head "SIMPLE-VECTOR")
                  (string= head "ARRAY") (string= head "SIMPLE-ARRAY"))))
       (if ie-p
           (make-array size :initial-element initial-element)
           (make-array size)))
      (t (error "MAKE-SEQUENCE: unsupported type ~S" type)))))

;; --- Pathname functions (Step 10) ---
;; Core pathname functions (pathname, pathnamep, parse-namestring, namestring,
;; make-pathname, merge-pathnames, pathname-host/device/directory/name/type/version,
;; file-namestring, directory-namestring, enough-namestring) are C builtins.
;; Only truename remains in boot.lisp for now.

(defun truename (pathname)
  "Return the true name of PATHNAME."
  (pathname (namestring pathname)))

(defun %ensure-dirs-helper (dir pos len created)
  "Helper for ensure-directories-exist. Walks dir string creating directories."
  (if (>= pos len)
      created
      (let ((next-sep nil))
        (do ((i pos (+ i 1)))
            ((>= i len))
          (when (or (char= (char dir i) #\/)
                    (char= (char dir i) #\:))
            (setq next-sep (+ i 1))
            (return)))
        (if next-sep
            (progn
              (when (char= (char dir (- next-sep 1)) #\/)
                (let ((sub (subseq dir 0 next-sep)))
                  (when (%mkdir sub) (setq created t))))
              (%ensure-dirs-helper dir next-sep len created))
            created))))

(defun ensure-directories-exist (pathname)
  "Create all directories in the path of PATHNAME. Returns pathname and created-p."
  (let* ((dir (directory-namestring pathname))
         (created (%ensure-dirs-helper dir 0 (length dir) nil)))
    (values pathname created)))

;; --- LOOP macro (Phase 8) ---

(defun %loop-keyword-p (sym name)
  "Check if SYM is a symbol whose name matches NAME."
  (and (symbolp sym)
       (string= (symbol-name sym) name)))

(defun %loop-keyword-sym-p (sym)
  "Check if SYM is any recognized loop keyword."
  (and (symbolp sym)
       (let ((n (symbol-name sym)))
         (or (string= n "FOR") (string= n "AS")
             (string= n "WITH") (string= n "DO") (string= n "DOING")
             (string= n "COLLECT") (string= n "COLLECTING")
             (string= n "APPEND") (string= n "APPENDING")
             (string= n "NCONC") (string= n "NCONCING")
             (string= n "SUM") (string= n "SUMMING")
             (string= n "COUNT") (string= n "COUNTING")
             (string= n "MAXIMIZE") (string= n "MAXIMIZING")
             (string= n "MINIMIZE") (string= n "MINIMIZING")
             (string= n "WHILE") (string= n "UNTIL")
             (string= n "REPEAT")
             (string= n "ALWAYS") (string= n "NEVER") (string= n "THEREIS")
             (string= n "WHEN") (string= n "IF") (string= n "UNLESS")
             (string= n "RETURN")
             (string= n "NAMED") (string= n "INITIALLY") (string= n "FINALLY")
             (string= n "IN") (string= n "ON") (string= n "BY")
             (string= n "FROM") (string= n "DOWNFROM") (string= n "UPFROM")
             (string= n "TO") (string= n "BELOW") (string= n "ABOVE")
             (string= n "DOWNTO") (string= n "UPTO")
             (string= n "ACROSS") (string= n "THEN")
             (string= n "INTO") (string= n "BEING") (string= n "EACH")
             (string= n "THE") (string= n "OF") (string= n "USING")
             (string= n "HASH-KEY") (string= n "HASH-KEYS")
             (string= n "HASH-VALUE") (string= n "HASH-VALUES")
             (string= n "SYMBOL") (string= n "SYMBOLS")
             (string= n "PRESENT-SYMBOL") (string= n "PRESENT-SYMBOLS")
             (string= n "EXTERNAL-SYMBOL") (string= n "EXTERNAL-SYMBOLS")
             (string= n "AND") (string= n "ELSE") (string= n "END")))))

(defun %loop-accum-body (kn expr accum-var)
  "Generate the body form for accumulation kind KN."
  (cond
    ((or (string= kn "COLLECT") (string= kn "COLLECTING"))
     `(push ,expr ,accum-var))
    ((or (string= kn "SUM") (string= kn "SUMMING"))
     `(setq ,accum-var (+ ,accum-var ,expr)))
    ((or (string= kn "COUNT") (string= kn "COUNTING"))
     `(when ,expr (setq ,accum-var (+ ,accum-var 1))))
    ((or (string= kn "MAXIMIZE") (string= kn "MAXIMIZING"))
     (let ((tmp (gensym "V")))
       `(let ((,tmp ,expr))
          (when (or (null ,accum-var) (> ,tmp ,accum-var))
            (setq ,accum-var ,tmp)))))
    ((or (string= kn "MINIMIZE") (string= kn "MINIMIZING"))
     (let ((tmp (gensym "V")))
       `(let ((,tmp ,expr))
          (when (or (null ,accum-var) (< ,tmp ,accum-var))
            (setq ,accum-var ,tmp)))))
    ((or (string= kn "APPEND") (string= kn "APPENDING"))
     `(setq ,accum-var (append ,accum-var ,expr)))
    (t
     `(setq ,accum-var (nconc ,accum-var ,expr)))))

(defun %loop-accum-keyword-p (sym)
  "Check if SYM is an accumulation keyword."
  (and (symbolp sym)
       (let ((n (symbol-name sym)))
         (or (string= n "COLLECT") (string= n "COLLECTING")
             (string= n "APPEND") (string= n "APPENDING")
             (string= n "NCONC") (string= n "NCONCING")
             (string= n "SUM") (string= n "SUMMING")
             (string= n "COUNT") (string= n "COUNTING")
             (string= n "MAXIMIZE") (string= n "MAXIMIZING")
             (string= n "MINIMIZE") (string= n "MINIMIZING")))))

(defun %expand-simple-loop (forms)
  "Expand simple (loop body...) into block/tagbody."
  (let ((tag (gensym "LOOP")))
    `(block nil
       (tagbody
         ,tag
         ,@forms
         (go ,tag)))))

(defun %loop-destructure-vars (pattern)
  "Extract all variable names from a destructuring pattern."
  (cond
    ((null pattern) nil)
    ((symbolp pattern) (list pattern))
    ((consp pattern)
     (append (%loop-destructure-vars (car pattern))
             (%loop-destructure-vars (cdr pattern))))
    (t nil)))

(defun %loop-destructure-assigns (pattern source)
  "Generate setq forms to destructure SOURCE into PATTERN variables."
  (cond
    ((null pattern) nil)
    ((symbolp pattern) (list `(setq ,pattern ,source)))
    ((consp pattern)
     (append (%loop-destructure-assigns (car pattern) `(car ,source))
             (%loop-destructure-assigns (cdr pattern) `(cdr ,source))))
    (t nil)))

(defun %loop-destructure-bindings (pattern source)
  "Generate (var init) binding forms to destructure SOURCE into PATTERN variables."
  (cond
    ((null pattern) nil)
    ((symbolp pattern) (list (list pattern source)))
    ((consp pattern)
     (append (%loop-destructure-bindings (car pattern) `(car ,source))
             (%loop-destructure-bindings (cdr pattern) `(cdr ,source))))
    (t nil)))

(defun %loop-parse-for (rest var sub-kw end-tag)
  "Parse a FOR/AS clause. Returns (new-rest bindings end-tests pre-body steps)."
  (cond
    ;; FOR var IN list [BY step-fn]
    ((%loop-keyword-p sub-kw "IN")
     (let ((list-expr (car rest))
           (iter-gs (gensym "ITER"))
           (by-fn nil))
       (setq rest (cdr rest))
       (when (and rest (%loop-keyword-p (car rest) "BY"))
         (setq rest (cdr rest))
         (setq by-fn (car rest))
         (setq rest (cdr rest)))
       (list rest
             (list (list iter-gs list-expr) (list var nil))
             (list `(when (endp ,iter-gs) (go ,end-tag)))
             (list `(setq ,var (car ,iter-gs)))
             (list (if by-fn
                       `(setq ,iter-gs (funcall ,by-fn ,iter-gs))
                       `(setq ,iter-gs (cdr ,iter-gs)))))))
    ;; FOR var ON list [BY step-fn]
    ((%loop-keyword-p sub-kw "ON")
     (let ((list-expr (car rest))
           (by-fn nil))
       (setq rest (cdr rest))
       (when (and rest (%loop-keyword-p (car rest) "BY"))
         (setq rest (cdr rest))
         (setq by-fn (car rest))
         (setq rest (cdr rest)))
       (list rest
             (list (list var list-expr))
             (list `(when (endp ,var) (go ,end-tag)))
             nil
             (list (if by-fn
                       `(setq ,var (funcall ,by-fn ,var))
                       `(setq ,var (cdr ,var)))))))
    ;; FOR var ACROSS vector
    ((%loop-keyword-p sub-kw "ACROSS")
     (let ((vec-expr (car rest))
           (vec-gs (gensym "VEC"))
           (len-gs (gensym "LEN"))
           (idx-gs (gensym "IDX")))
       (setq rest (cdr rest))
       (list rest
             (list (list vec-gs vec-expr) (list len-gs `(length ,vec-gs))
                   (list idx-gs 0) (list var nil))
             (list `(when (>= ,idx-gs ,len-gs) (go ,end-tag)))
             (list `(setq ,var (elt ,vec-gs ,idx-gs)))
             (list `(setq ,idx-gs (+ ,idx-gs 1))))))
    ;; FOR var = expr [THEN step-expr]
    ((and (symbolp sub-kw) (string= (symbol-name sub-kw) "="))
     (let ((init-expr (car rest))
           (step-expr nil))
       (setq rest (cdr rest))
       (when (and rest (%loop-keyword-p (car rest) "THEN"))
         (setq rest (cdr rest))
         (setq step-expr (car rest))
         (setq rest (cdr rest)))
       (if step-expr
           ;; WITH THEN: first iteration uses init-expr, subsequent use step-expr
           (let ((flag (gensym "FIRST")))
             (list rest
                   (list (list var nil) (list flag t))
                   nil
                   (list `(if ,flag
                              (progn (setq ,var ,init-expr) (setq ,flag nil))
                              (setq ,var ,step-expr)))
                   nil))
           ;; WITHOUT THEN: re-evaluate expr each iteration
           (list rest
                 (list (list var nil))
                 nil
                 (list `(setq ,var ,init-expr))
                 nil))))
    ;; FOR var FROM/UPFROM/DOWNFROM start ...
    ((or (%loop-keyword-p sub-kw "FROM")
         (%loop-keyword-p sub-kw "UPFROM")
         (%loop-keyword-p sub-kw "DOWNFROM"))
     (let ((start-expr (car rest))
           (dir (if (%loop-keyword-p sub-kw "DOWNFROM") :down :up))
           (end-expr nil) (end-gs nil) (cmp-fn nil)
           (step-val 1))
       (setq rest (cdr rest))
       (block parse-num-opts
         (tagbody
           num-opt-next
           (when (null rest) (return-from parse-num-opts))
           (cond
             ((or (%loop-keyword-p (car rest) "TO")
                  (%loop-keyword-p (car rest) "UPTO"))
              (setq rest (cdr rest))
              (setq end-expr (car rest))
              (setq rest (cdr rest))
              (setq cmp-fn (if (eq dir :up) '> '<)))
             ((%loop-keyword-p (car rest) "DOWNTO")
              (setq rest (cdr rest))
              (setq end-expr (car rest))
              (setq rest (cdr rest))
              (setq dir :down)
              (setq cmp-fn '<))
             ((%loop-keyword-p (car rest) "BELOW")
              (setq rest (cdr rest))
              (setq end-expr (car rest))
              (setq rest (cdr rest))
              (setq cmp-fn '>=))
             ((%loop-keyword-p (car rest) "ABOVE")
              (setq rest (cdr rest))
              (setq end-expr (car rest))
              (setq rest (cdr rest))
              (setq dir :down)
              (setq cmp-fn '<=))
             ((%loop-keyword-p (car rest) "BY")
              (setq rest (cdr rest))
              (setq step-val (car rest))
              (setq rest (cdr rest)))
             (t (return-from parse-num-opts)))
           (go num-opt-next)))
       (let ((binds (list (list var start-expr)))
             (tests nil))
         (when end-expr
           (setq end-gs (gensym "END"))
           (push (list end-gs end-expr) binds)
           (setq tests (list `(when (,cmp-fn ,var ,end-gs) (go ,end-tag)))))
         (list rest binds tests nil
               (list (if (eq dir :down)
                         `(setq ,var (- ,var ,step-val))
                         `(setq ,var (+ ,var ,step-val))))))))
    ;; FOR var BEING {the|each} {hash-key[s]|hash-value[s]} {of|in} ht [using (...)]
    ;; FOR var BEING {the|each} {symbol[s]|present-symbol[s]|external-symbol[s]} {of|in} pkg
    ((%loop-keyword-p sub-kw "BEING")
     (let ((what nil)
           (using-var nil))
       ;; Skip optional the/each
       (when (and rest (symbolp (car rest))
                  (let ((n (symbol-name (car rest))))
                    (or (string= n "THE") (string= n "EACH"))))
         (setq rest (cdr rest)))
       ;; Parse iteration kind
       (when (null rest) (error "LOOP: missing BEING iteration kind"))
       (let ((n (if (symbolp (car rest)) (symbol-name (car rest)) "")))
         (cond
           ((or (string= n "HASH-KEY") (string= n "HASH-KEYS"))
            (setq what "HASH-KEYS"))
           ((or (string= n "HASH-VALUE") (string= n "HASH-VALUES"))
            (setq what "HASH-VALUES"))
           ((or (string= n "SYMBOL") (string= n "SYMBOLS"))
            (setq what "SYMBOLS"))
           ((or (string= n "PRESENT-SYMBOL") (string= n "PRESENT-SYMBOLS"))
            (setq what "PRESENT-SYMBOLS"))
           ((or (string= n "EXTERNAL-SYMBOL") (string= n "EXTERNAL-SYMBOLS"))
            (setq what "EXTERNAL-SYMBOLS"))
           (t (error "LOOP: unrecognized BEING type ~S" (car rest)))))
       (setq rest (cdr rest))
       ;; Skip of/in
       (when (and rest (symbolp (car rest))
                  (let ((n (symbol-name (car rest))))
                    (or (string= n "OF") (string= n "IN"))))
         (setq rest (cdr rest)))
       ;; Source expression
       (let ((src-expr (car rest)))
         (setq rest (cdr rest))
         ;; Check for USING clause (hash tables only)
         (when (and rest (%loop-keyword-p (car rest) "USING"))
           (setq rest (cdr rest))
           (let ((spec (car rest)))
             (setq rest (cdr rest))
             (setq using-var (cadr spec))))
         ;; Generate expansion
         (cond
           ;; Hash table iteration
           ((or (string= what "HASH-KEYS") (string= what "HASH-VALUES"))
            (let* ((iter-gs (gensym "ITER"))
                   (is-keys (string= what "HASH-KEYS"))
                   (binds (list (list iter-gs `(%hash-table-pairs ,src-expr))
                                (list var nil)))
                   (pre (list `(setq ,var (,(if is-keys 'caar 'cdar) ,iter-gs)))))
              (when using-var
                (setq binds (append binds (list (list using-var nil))))
                (setq pre (append pre
                                  (list `(setq ,using-var
                                               (,(if is-keys 'cdar 'caar) ,iter-gs))))))
              (list rest binds
                    (list `(when (endp ,iter-gs) (go ,end-tag)))
                    pre
                    (list `(setq ,iter-gs (cdr ,iter-gs))))))
           ;; Package symbol iteration
           (t
            (let* ((iter-gs (gensym "ITER"))
                   (fn (if (string= what "EXTERNAL-SYMBOLS")
                           '%package-external-symbols
                           '%package-symbols)))
              (list rest
                    (list (list iter-gs `(,fn (find-package ,src-expr)))
                          (list var nil))
                    (list `(when (endp ,iter-gs) (go ,end-tag)))
                    (list `(setq ,var (car ,iter-gs)))
                    (list `(setq ,iter-gs (cdr ,iter-gs))))))))))
    ;; FOR var BELOW limit — shorthand for FROM 0 BELOW limit
    ((%loop-keyword-p sub-kw "BELOW")
     (let ((end-expr (car rest))
           (end-gs (gensym "END")))
       (setq rest (cdr rest))
       (let ((step-val 1))
         (when (and rest (%loop-keyword-p (car rest) "BY"))
           (setq rest (cdr rest))
           (setq step-val (car rest))
           (setq rest (cdr rest)))
         (list rest
               (list (list var 0) (list end-gs end-expr))
               (list `(when (>= ,var ,end-gs) (go ,end-tag)))
               nil
               (list `(setq ,var (+ ,var ,step-val)))))))
    ;; FOR var ABOVE limit — shorthand for downward from some start
    ((%loop-keyword-p sub-kw "ABOVE")
     (let ((end-expr (car rest))
           (end-gs (gensym "END")))
       (setq rest (cdr rest))
       (let ((step-val 1))
         (when (and rest (%loop-keyword-p (car rest) "BY"))
           (setq rest (cdr rest))
           (setq step-val (car rest))
           (setq rest (cdr rest)))
         (list rest
               (list (list var 0) (list end-gs end-expr))
               (list `(when (<= ,var ,end-gs) (go ,end-tag)))
               nil
               (list `(setq ,var (- ,var ,step-val)))))))
    ;; FOR var OF-TYPE type-spec <sub-clause> — skip type declaration, recurse
    ((%loop-keyword-p sub-kw "OF-TYPE")
     ;; rest = (type-spec real-sub-kw . rest-of-clause)
     ;; Skip the type-spec, delegate to real sub-clause
     (let ((real-sub-kw (cadr rest))
           (real-rest (cddr rest)))
       (%loop-parse-for real-rest var real-sub-kw end-tag)))
    (t
     (error "LOOP: unrecognized FOR sub-clause ~S" sub-kw))))

(defun %loop-parse-cond-subclause (sub rest block-name
                                    bindings into-vars default-accum
                                    result-form epilogue)
  "Parse one conditional sub-clause. Returns (form rest bindings into-vars
   default-accum result-form epilogue)."
  (let ((form nil))
    (cond
      ;; DO — consume forms until keyword
      ((or (%loop-keyword-p sub "DO") (%loop-keyword-p sub "DOING"))
       (let ((do-forms nil))
         (block consume-cond-do
           (tagbody
             cond-do-next
             (when (null rest) (return-from consume-cond-do))
             (when (%loop-keyword-sym-p (car rest)) (return-from consume-cond-do))
             (push (car rest) do-forms)
             (setq rest (cdr rest))
             (go cond-do-next)))
         (setq form `(progn ,@(nreverse do-forms)))))
      ;; RETURN
      ((%loop-keyword-p sub "RETURN")
       (setq form `(return-from ,block-name ,(car rest)))
       (setq rest (cdr rest)))
      ;; Accumulation
      ((%loop-accum-keyword-p sub)
       (let* ((kn (symbol-name sub))
              (is-collect (or (string= kn "COLLECT") (string= kn "COLLECTING")))
              (is-sum (or (string= kn "SUM") (string= kn "SUMMING")))
              (is-count (or (string= kn "COUNT") (string= kn "COUNTING")))
              (init-val (if (or is-sum is-count) 0 nil))
              (expr (car rest))
              (accum-var nil))
         (setq rest (cdr rest))
         (if (and rest (%loop-keyword-p (car rest) "INTO"))
             (progn
               (setq rest (cdr rest))
               (setq accum-var (car rest))
               (setq rest (cdr rest))
               (unless (member accum-var into-vars)
                 (push accum-var into-vars)
                 (push (list accum-var init-val) bindings)
                 (when is-collect
                   (push `(setq ,accum-var (nreverse ,accum-var)) epilogue))))
             (progn
               (unless default-accum
                 (setq default-accum (gensym "ACC"))
                 (push (list default-accum init-val) bindings)
                 (setq result-form (if is-collect `(nreverse ,default-accum)
                                       default-accum)))
               (setq accum-var default-accum)))
         (setq form (%loop-accum-body kn expr accum-var))))
      ;; Nested IF/WHEN/UNLESS as sub-clause (standard LOOP syntax)
      ((or (%loop-keyword-p sub "IF") (%loop-keyword-p sub "WHEN")
           (%loop-keyword-p sub "UNLESS"))
       (let* ((inner-test (car rest))
              (negate (%loop-keyword-p sub "UNLESS"))
              (then-forms nil)
              (else-forms nil))
         (setq rest (cdr rest))
         ;; Parse then-branch sub-clause(s) connected by AND
         (block parse-inner-then
           (tagbody
             inner-then-sub
             (let* ((r (%loop-parse-cond-subclause
                        (car rest) (cdr rest) block-name
                        bindings into-vars default-accum
                        result-form epilogue)))
               (push (car r) then-forms)
               (setq rest (cadr r))
               (setq bindings (caddr r))
               (setq into-vars (car (cdddr r)))
               (setq default-accum (cadr (cdddr r)))
               (setq result-form (caddr (cdddr r)))
               (setq epilogue (car (cdddr (cdddr r)))))
             (when (and rest (%loop-keyword-p (car rest) "AND"))
               (setq rest (cdr rest))
               (go inner-then-sub))))
         ;; Check for ELSE branch
         (when (and rest (%loop-keyword-p (car rest) "ELSE"))
           (setq rest (cdr rest))
           (block parse-inner-else
             (tagbody
               inner-else-sub
               (let* ((r (%loop-parse-cond-subclause
                          (car rest) (cdr rest) block-name
                          bindings into-vars default-accum
                          result-form epilogue)))
                 (push (car r) else-forms)
                 (setq rest (cadr r))
                 (setq bindings (caddr r))
                 (setq into-vars (car (cdddr r)))
                 (setq default-accum (cadr (cdddr r)))
                 (setq result-form (caddr (cdddr r)))
                 (setq epilogue (car (cdddr (cdddr r)))))
               (when (and rest (%loop-keyword-p (car rest) "AND"))
                 (setq rest (cdr rest))
                 (go inner-else-sub)))))
         ;; Consume optional END
         (when (and rest (%loop-keyword-p (car rest) "END"))
           (setq rest (cdr rest)))
         ;; Build the form
         (when negate (setq inner-test `(not ,inner-test)))
         (let ((then-body (if (cdr then-forms)
                              `(progn ,@(nreverse then-forms))
                              (car then-forms)))
               (else-body (when else-forms
                            (if (cdr else-forms)
                                `(progn ,@(nreverse else-forms))
                                (car else-forms)))))
           (setq form (if else-body
                          `(if ,inner-test ,then-body ,else-body)
                          `(when ,inner-test ,then-body))))))
      (t (error "LOOP: invalid WHEN/IF sub-clause ~S" sub)))
    (list form rest bindings into-vars default-accum result-form epilogue)))

(defun %expand-extended-loop (forms)
  "Expand extended loop with keywords into block/let*/tagbody."
  (let* ((rest forms)
         (block-name nil)
         (bindings nil)
         (preamble nil)
         (body nil)
         (steps nil)
         (epilogue nil)
         (finally-forms nil)
         (initially-forms nil)
         (result-form nil)
         (default-accum nil)
         (into-vars nil)
         (loop-tag (gensym "LOOP"))
         (end-tag (gensym "END")))
    ;; Parser loop
    (block parse-done
      (tagbody
        parse-next
        (when (null rest) (return-from parse-done))
        (let ((kw (car rest)))
          (setq rest (cdr rest))
          (cond
            ;; WHILE
            ((%loop-keyword-p kw "WHILE")
             (push `(unless ,(car rest) (go ,end-tag)) preamble)
             (setq rest (cdr rest)))
            ;; UNTIL
            ((%loop-keyword-p kw "UNTIL")
             (push `(when ,(car rest) (go ,end-tag)) preamble)
             (setq rest (cdr rest)))
            ;; DO / DOING
            ((or (%loop-keyword-p kw "DO") (%loop-keyword-p kw "DOING"))
             (block consume-do
               (tagbody
                 do-next
                 (when (null rest) (return-from consume-do))
                 (when (%loop-keyword-sym-p (car rest)) (return-from consume-do))
                 (push (car rest) body)
                 (setq rest (cdr rest))
                 (go do-next))))
            ;; REPEAT
            ((%loop-keyword-p kw "REPEAT")
             (let ((ctr (gensym "REP")))
               (push (list ctr (car rest)) bindings)
               (setq rest (cdr rest))
               (push `(when (<= ,ctr 0) (go ,end-tag)) preamble)
               (push `(setq ,ctr (- ,ctr 1)) steps)))
            ;; FOR / AS — delegated
            ((or (%loop-keyword-p kw "FOR") (%loop-keyword-p kw "AS"))
             (let* ((raw-var (car rest))
                    (destructuring (consp raw-var))
                    (var (if destructuring (gensym "DVAR") raw-var))
                    (sub-kw (cadr rest))
                    (r (%loop-parse-for (cddr rest) var sub-kw end-tag)))
               (setq rest (car r))
               (dolist (b (cadr r)) (push b bindings))
               (when destructuring
                 (dolist (v (%loop-destructure-vars raw-var))
                   (push (list v nil) bindings)))
               (dolist (e (caddr r)) (push e preamble))
               (dolist (p (car (cdddr r))) (push p preamble))
               (when destructuring
                 (dolist (a (%loop-destructure-assigns raw-var var))
                   (push a preamble)))
               (dolist (s (cadr (cdddr r))) (push s steps))))
            ;; RETURN
            ((%loop-keyword-p kw "RETURN")
             (push `(return-from ,block-name ,(car rest)) body)
             (setq rest (cdr rest)))
            ;; Accumulation (unified)
            ((%loop-accum-keyword-p kw)
             (let* ((kn (symbol-name kw))
                    (is-collect (or (string= kn "COLLECT") (string= kn "COLLECTING")))
                    (is-sum (or (string= kn "SUM") (string= kn "SUMMING")))
                    (is-count (or (string= kn "COUNT") (string= kn "COUNTING")))
                    (init-val (if (or is-sum is-count) 0 nil))
                    (expr (car rest))
                    (accum-var nil))
               (setq rest (cdr rest))
               (if (and rest (%loop-keyword-p (car rest) "INTO"))
                   (progn
                     (setq rest (cdr rest))
                     (setq accum-var (car rest))
                     (setq rest (cdr rest))
                     (unless (member accum-var into-vars)
                       (push accum-var into-vars)
                       (push (list accum-var init-val) bindings)
                       (when is-collect
                         (push `(setq ,accum-var (nreverse ,accum-var)) epilogue))))
                   (progn
                     (unless default-accum
                       (setq default-accum (gensym "ACC"))
                       (push (list default-accum init-val) bindings)
                       (setq result-form (if is-collect `(nreverse ,default-accum)
                                             default-accum)))
                     (setq accum-var default-accum)))
               (push (%loop-accum-body kn expr accum-var) body)))
            ;; NAMED
            ((%loop-keyword-p kw "NAMED")
             (setq block-name (car rest))
             (setq rest (cdr rest)))
            ;; WITH var [= expr] {AND var [= expr]}*
            ((%loop-keyword-p kw "WITH")
             (block parse-with
               (tagbody
                 with-next
                 (let* ((raw-wvar (car rest))
                        (destructuring-w (consp raw-wvar))
                        (wvar (if destructuring-w (gensym "WDVAR") raw-wvar)))
                   (setq rest (cdr rest))
                   ;; Skip OF-TYPE type-spec if present
                   (when (and rest (%loop-keyword-p (car rest) "OF-TYPE"))
                     (setq rest (cddr rest)))
                   (if (and rest (symbolp (car rest))
                            (string= (symbol-name (car rest)) "="))
                       (progn
                         (setq rest (cdr rest))
                         (push (list wvar (car rest)) bindings)
                         (setq rest (cdr rest)))
                       (push (list wvar nil) bindings))
                   (when destructuring-w
                     (dolist (b (%loop-destructure-bindings raw-wvar wvar))
                       (push b bindings))))
                 (when (and rest (%loop-keyword-p (car rest) "AND"))
                   (setq rest (cdr rest))
                   (go with-next)))))
            ;; INITIALLY
            ((%loop-keyword-p kw "INITIALLY")
             (block consume-ini
               (tagbody
                 ini-next
                 (when (null rest) (return-from consume-ini))
                 (when (%loop-keyword-sym-p (car rest)) (return-from consume-ini))
                 (push (car rest) initially-forms)
                 (setq rest (cdr rest))
                 (go ini-next))))
            ;; FINALLY
            ((%loop-keyword-p kw "FINALLY")
             (block consume-fin
               (tagbody
                 fin-next
                 (when (null rest) (return-from consume-fin))
                 (when (%loop-keyword-sym-p (car rest)) (return-from consume-fin))
                 (push (car rest) finally-forms)
                 (setq rest (cdr rest))
                 (go fin-next))))
            ;; ALWAYS
            ((%loop-keyword-p kw "ALWAYS")
             (push `(unless ,(car rest) (return-from ,block-name nil)) body)
             (setq rest (cdr rest))
             (unless result-form (setq result-form t)))
            ;; NEVER
            ((%loop-keyword-p kw "NEVER")
             (push `(when ,(car rest) (return-from ,block-name nil)) body)
             (setq rest (cdr rest))
             (unless result-form (setq result-form t)))
            ;; THEREIS
            ((%loop-keyword-p kw "THEREIS")
             (let ((tmp (gensym "V")))
               (push `(let ((,tmp ,(car rest)))
                        (when ,tmp (return-from ,block-name ,tmp)))
                     body)
               (setq rest (cdr rest))))
            ;; WHEN / IF / UNLESS — conditional with sub-clauses
            ;; Handles flat :when ... :else :when ... chains iteratively
            ((or (%loop-keyword-p kw "WHEN") (%loop-keyword-p kw "IF")
                 (%loop-keyword-p kw "UNLESS"))
             (let ((cond-clauses nil)  ;; list of (test . then-forms) -- built in reverse
                   (final-else nil)
                   (cur-test (car rest))
                   (cur-negate (%loop-keyword-p kw "UNLESS"))
                   (cur-then nil))
               (setq rest (cdr rest))
               (block parse-cond-chain
                 (tagbody
                   parse-cond-sub
                   ;; Parse one sub-clause
                   (let* ((r (%loop-parse-cond-subclause
                              (car rest) (cdr rest) block-name
                              bindings into-vars default-accum
                              result-form epilogue)))
                     (push (car r) cur-then)
                     (setq rest (cadr r))
                     (setq bindings (caddr r))
                     (setq into-vars (car (cdddr r)))
                     (setq default-accum (cadr (cdddr r)))
                     (setq result-form (caddr (cdddr r)))
                     (setq epilogue (car (cdddr (cdddr r)))))
                   (cond
                     ;; AND — more sub-clauses for same branch
                     ((and rest (%loop-keyword-p (car rest) "AND"))
                      (setq rest (cdr rest))
                      (go parse-cond-sub))
                     ;; ELSE followed by WHEN/IF/UNLESS — new cond clause (iterative)
                     ((and rest (%loop-keyword-p (car rest) "ELSE")
                           (cdr rest)
                           (or (%loop-keyword-p (cadr rest) "WHEN")
                               (%loop-keyword-p (cadr rest) "IF")
                               (%loop-keyword-p (cadr rest) "UNLESS")))
                      ;; Save current clause
                      (when cur-negate (setq cur-test `(not ,cur-test)))
                      (push (cons cur-test (nreverse cur-then)) cond-clauses)
                      ;; Start new clause
                      (setq rest (cdr rest))  ;; skip :else
                      (setq cur-negate (%loop-keyword-p (car rest) "UNLESS"))
                      (setq rest (cdr rest))  ;; skip :when/:if/:unless
                      (setq cur-test (car rest))
                      (setq rest (cdr rest))  ;; skip test-expr
                      (setq cur-then nil)
                      (go parse-cond-sub))
                     ;; ELSE followed by non-conditional — final else branch
                     ((and rest (%loop-keyword-p (car rest) "ELSE"))
                      (setq rest (cdr rest))
                      ;; Save current clause
                      (when cur-negate (setq cur-test `(not ,cur-test)))
                      (push (cons cur-test (nreverse cur-then)) cond-clauses)
                      ;; Parse else sub-clauses
                      (setq cur-then nil)
                      (block parse-final-else
                        (tagbody
                          parse-else-sub
                          (let* ((r (%loop-parse-cond-subclause
                                     (car rest) (cdr rest) block-name
                                     bindings into-vars default-accum
                                     result-form epilogue)))
                            (push (car r) final-else)
                            (setq rest (cadr r))
                            (setq bindings (caddr r))
                            (setq into-vars (car (cdddr r)))
                            (setq default-accum (cadr (cdddr r)))
                            (setq result-form (caddr (cdddr r)))
                            (setq epilogue (car (cdddr (cdddr r)))))
                          (when (and rest (%loop-keyword-p (car rest) "AND"))
                            (setq rest (cdr rest))
                            (go parse-else-sub))))
                      (when (and rest (%loop-keyword-p (car rest) "END"))
                        (setq rest (cdr rest))))
                     ;; END — done
                     ((and rest (%loop-keyword-p (car rest) "END"))
                      (setq rest (cdr rest))
                      ;; Save current clause
                      (when cur-negate (setq cur-test `(not ,cur-test)))
                      (push (cons cur-test (nreverse cur-then)) cond-clauses))
                     ;; No more conditional tokens — done
                     (t
                      (when cur-negate (setq cur-test `(not ,cur-test)))
                      (push (cons cur-test (nreverse cur-then)) cond-clauses)))))
               ;; Build nested if/else from cond-clauses (reverse order) + final-else
               (let ((result (if final-else
                                 `(progn ,@(nreverse final-else))
                                 nil)))
                 (dolist (clause cond-clauses)
                   (let ((test (car clause))
                         (forms (cdr clause)))
                     (if result
                         (setq result `(if ,test (progn ,@forms) ,result))
                         (setq result `(when ,test ,@forms)))))
                 (push result body))))
            (t
             (error "LOOP: unrecognized clause ~S" kw))))
        (go parse-next)))
    ;; Build the expansion
    `(block ,block-name
       (let* ,(nreverse bindings)
         ,@(nreverse initially-forms)
         (macrolet ((loop-finish () (list 'go ',end-tag)))
           (tagbody
             ,loop-tag
             ,@(nreverse preamble)
             ,@(nreverse body)
             ,@(nreverse steps)
             (go ,loop-tag)
             ,end-tag))
         ,@(nreverse epilogue)
         ,@(nreverse finally-forms)
         ,result-form))))

(defmacro loop (&rest forms)
  "CL LOOP macro — supports simple and extended forms."
  (if (or (null forms)
          (%loop-keyword-sym-p (car forms)))
      ;; Extended loop (starts with keyword or is empty)
      (%expand-extended-loop forms)
      ;; Simple loop (body forms only)
      (%expand-simple-loop forms)))

;; pprint-logical-block — structured pretty printing
(defmacro pprint-logical-block (header &body body)
  (let* ((stream-sym (car header))
         (list-form (cadr header))
         (keys (cddr header))
         (prefix (getf keys :prefix))
         (per-line-prefix (getf keys :per-line-prefix))
         (suffix (getf keys :suffix))
         (pfx (or prefix per-line-prefix))
         (glist (gensym "LIST")))
    `(let ((,glist ,list-form))
       (%pp-push-block)
       ,(when pfx `(write-string ,pfx ,stream-sym))
       (macrolet ((pprint-pop ()
                    (list 'prog1 (list 'car ',glist)
                          (list 'setq ',glist (list 'cdr ',glist))))
                  (pprint-exit-if-list-exhausted ()
                    (list 'when (list 'null ',glist) '(return nil))))
         (block nil ,@body))
       ,(when suffix `(write-string ,suffix ,stream-sym))
       (%pp-pop-block)
       nil)))

;; pprint-fill — print list elements separated by spaces (fill style)
(defun pprint-fill (stream list &optional (colon-p t) atsign-p)
  (declare (ignore atsign-p))
  (let ((s (or stream *standard-output*)))
    (when colon-p (write-char #\( s))
    (loop for tail on list
          do (write (car tail) :stream s)
          when (cdr tail) do (write-char #\Space s))
    (when colon-p (write-char #\) s))))

;; pprint-linear — print list elements separated by newlines (linear style)
(defun pprint-linear (stream list &optional (colon-p t) atsign-p)
  (declare (ignore atsign-p))
  (let ((s (or stream *standard-output*)))
    (when colon-p (write-char #\( s))
    (loop for tail on list
          do (write (car tail) :stream s)
          when (cdr tail) do (terpri s))
    (when colon-p (write-char #\) s))))
