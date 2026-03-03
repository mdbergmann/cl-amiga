;;; boot.lisp — Bootstrap functions for CL-Amiga
;;; Loaded at startup to provide common CL conveniences.

;; Composite CAR/CDR accessors
(defun cadr (x) (car (cdr x)))
(defun caar (x) (car (car x)))
(defun cdar (x) (cdr (car x)))
(defun cddr (x) (cdr (cdr x)))
(defun caddr (x) (car (cdr (cdr x))))
(defun cadar (x) (car (cdr (car x))))

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

;; define-condition — define a user condition type with slots and readers
(defmacro define-condition (name parent-types slot-specs &rest options)
  (let ((parent (if (consp parent-types) (car parent-types) parent-types))
        (slot-pairs (mapcar (lambda (spec) (cons (car spec) (getf (cdr spec) :initarg))) slot-specs)))
    `(progn
       (%register-condition-type ',name ',parent ',slot-pairs)
       ,@(mapcan (lambda (slot-spec)
                   (let* ((slot-name (car slot-spec))
                          (opts (cdr slot-spec))
                          (reader (getf opts :reader)))
                     (when reader
                       (list `(defun ,reader (c) (condition-slot-value c ',slot-name))))))
                 slot-specs)
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

;; defpackage — define a package with :use, :export, :nicknames, :local-nicknames options
(defmacro defpackage (name &rest options)
  (let ((pkg-name (if (symbolp name) (symbol-name name) name))
        (uses nil)
        (exports nil)
        (nicknames nil)
        (local-nicks nil))
    ;; Parse options
    (dolist (opt options)
      (case (car opt)
        (:use (setq uses (cdr opt)))
        (:export (setq exports (cdr opt)))
        (:nicknames (setq nicknames (cdr opt)))
        (:local-nicknames (setq local-nicks (cdr opt)))))
    `(progn
       (let ((pkg (or (find-package ,pkg-name)
                      (make-package ,pkg-name :nicknames ',nicknames))))
         ,@(when uses
             `((dolist (u ',uses)
                 (use-package (or (find-package (if (symbolp u) (symbol-name u) u))
                                  (error "Package ~A not found" u))
                              pkg))))
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
      `(progn ,@(reverse forms) ',name))))

;; read-from-string — read an S-expression from a string
(defun read-from-string (string &optional eof-error-p eof-value)
  (let ((s (make-string-input-stream string)))
    (read s eof-error-p eof-value)))

;; break — enter debugger with CONTINUE restart
(defun break (&optional (format-control "Break"))
  (restart-case
    (invoke-debugger (make-condition 'simple-condition
                                     :format-control format-control))
    (continue () nil)))
