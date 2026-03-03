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

;; --- String stream macros ---

(defmacro with-output-to-string (spec &body body)
  (let ((var (car spec)))
    `(let ((,var (make-string-output-stream)))
       (unwind-protect
         (progn ,@body (get-output-stream-string ,var))
         (close ,var)))))

(defmacro with-input-from-string (spec &body body)
  (let ((var (car spec))
        (string (cadr spec)))
    `(let ((,var (make-string-input-stream ,string)))
       (unwind-protect
         (progn ,@body)
         (close ,var)))))

;; with-open-file — open a file stream, execute body, ensure close
(defmacro with-open-file (spec &body body)
  (let ((var (car spec))
        (open-args (cdr spec)))
    `(let ((,var (open ,@open-args)))
       (unwind-protect
         (progn ,@body)
         (when ,var (close ,var))))))

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

(defun write-to-string (object)
  (with-output-to-string (s) (prin1 object s)))

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

;; --- Pathname functions (Step 10) ---
;; Pathnames are represented as strings (namestrings).
;; Supports both POSIX (/) and Amiga (:) path separators.

(defun namestring (pathname)
  "Return the namestring for PATHNAME (identity for strings)."
  pathname)

(defun truename (pathname)
  "Return the true name of PATHNAME (identity for now)."
  pathname)

(defun pathname-name (pathname)
  "Extract the name component (without extension) from PATHNAME."
  (let* ((file (file-namestring pathname))
         (dot-pos (position #\. file :from-end t)))
    (if dot-pos
        (subseq file 0 dot-pos)
        file)))

(defun pathname-type (pathname)
  "Extract the type (extension) from PATHNAME."
  (let* ((file (file-namestring pathname))
         (dot-pos (position #\. file :from-end t)))
    (if dot-pos
        (subseq file (+ dot-pos 1))
        nil)))

(defun pathname-directory (pathname)
  "Extract the directory component from PATHNAME as a list."
  (let ((dir (directory-namestring pathname)))
    (if (string= dir "")
        nil
        (list :absolute dir))))

(defun make-pathname (&key directory name type)
  "Construct a pathname string from components."
  (let ((result ""))
    (when directory
      (if (and (consp directory) (eq (car directory) :absolute))
          (setq result (cadr directory))
          (when (stringp directory) (setq result directory))))
    (when name
      (setq result (concatenate 'string result name)))
    (when type
      (setq result (concatenate 'string result "." type)))
    result))

(defun merge-pathnames (pathname &optional (defaults "") default-version)
  "Merge PATHNAME with DEFAULTS."
  (let ((dir (directory-namestring pathname))
        (file (file-namestring pathname)))
    (if (string= dir "")
        ;; No directory in pathname — use defaults' directory
        (let ((def-dir (directory-namestring defaults)))
          (concatenate 'string def-dir file))
        pathname)))

(defun enough-namestring (pathname &optional (defaults ""))
  "Return a relative pathname string sufficient to identify PATHNAME given DEFAULTS."
  (let ((def-dir (directory-namestring defaults))
        (path-dir (directory-namestring pathname))
        (file (file-namestring pathname)))
    (if (and (> (length def-dir) 0)
             (>= (length path-dir) (length def-dir))
             (string= (subseq path-dir 0 (length def-dir)) def-dir))
        ;; Strip the common prefix
        (concatenate 'string (subseq path-dir (length def-dir)) file)
        pathname)))

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
