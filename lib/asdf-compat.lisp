;;; asdf-compat.lisp — CL-Amiga workarounds for ASDF
;;;
;;; Load AFTER lib/asdf.lisp.
;;; Provides null-safe methods for ALL ASDF session accessors and ensures
;;; *asdf-session* is pre-initialized.
;;;
;;; When *asdf-session* is NIL (e.g. before first with-asdf-session, or
;;; during define-op compilation), toplevel-asdf-session returns NIL.
;;; ASDF then calls session accessors on NIL — without these methods,
;;; CLOS dispatch fails with corrupted-pointer errors because it tries
;;; to read struct slots from NIL (arena offset 0).

(in-package #:asdf/session)

;; Pre-initialize session so code that accesses it directly works.
(unless *asdf-session*
  (setf *asdf-session* (make-instance *asdf-session-class*)))

;; Null-safe accessor methods for ALL session slots.
;; Share a single empty hash table for hash-table-returning accessors.
(let ((empty-ht (make-hash-table :test 'equal)))
  ;; Readers
  (defmethod session-ancestor ((obj (eql nil))) nil)
  (defmethod session-cache ((obj (eql nil))) empty-ht)
  (defmethod session-operate-level ((obj (eql nil))) 0)
  (defmethod asdf-upgraded-p ((obj (eql nil))) nil)
  (defmethod forcing ((obj (eql nil))) nil)
  (defmethod visited-actions ((obj (eql nil))) empty-ht)
  (defmethod visiting-action-set ((obj (eql nil))) empty-ht)
  (defmethod visiting-action-list ((obj (eql nil))) nil)
  (defmethod total-action-count ((obj (eql nil))) 0)
  (defmethod planned-action-count ((obj (eql nil))) 0)
  (defmethod planned-output-action-count ((obj (eql nil))) 0)
  ;; Setters (no-op when session is NIL)
  (defmethod (setf session-operate-level) (val (obj (eql nil))) val)
  (defmethod (setf asdf-upgraded-p) (val (obj (eql nil))) val)
  (defmethod (setf forcing) (val (obj (eql nil))) val)
  (defmethod (setf visited-actions) (val (obj (eql nil))) val)
  (defmethod (setf visiting-action-set) (val (obj (eql nil))) val)
  (defmethod (setf visiting-action-list) (val (obj (eql nil))) val)
  (defmethod (setf total-action-count) (val (obj (eql nil))) val)
  (defmethod (setf planned-action-count) (val (obj (eql nil))) val)
  (defmethod (setf planned-output-action-count) (val (obj (eql nil))) val))

;;; CL-Amiga source-loading mode for ASDF
;;;
;;; CL-Amiga's LOAD already compiles each form to bytecode, so the
;;; compile-file → FASL → load-fasl round-trip adds complexity without
;;; benefit. Override ASDF's compilation and FASL loading to use direct
;;; source loading instead. This avoids the tmpize-pathname / staging
;;; machinery and eliminates FASL-related crashes.
;;;
;;; compile-op: load the source (compiles at load time), write a
;;;   marker FASL so ASDF doesn't re-trigger compilation.
;;; load-op: prefer loading the source file directly.

(in-package #:asdf/lisp-action)

(defun perform-lisp-compilation (o c)
  "CL-Amiga override: load source form-by-form instead of compile-file.
CL-Amiga compiles each form at load time, so source loading achieves
the same result as compile-file without the FASL round-trip.
Errors in individual forms (e.g. bytecode-too-large) are reported as
warnings but do not abort the file."
  (let* ((input-file (first (input-files o c)))
         (outputs (output-files o c))
         (output-file (first outputs)))
    (handler-case
      (call-with-around-compile-hook
       c #'(lambda (&rest flags)
             (declare (ignore flags))
             (let ((*package* *package*)
                   (*readtable* *readtable*))
               (with-open-file (stream input-file :direction :input)
                 (let ((eof (gensym "EOF"))
                       (nerrors 0))
                   (loop
                     (let ((form (handler-case (read stream nil eof)
                                   (error (e)
                                     (format t "~&;; [CL-Amiga] read error in ~A: ~A~%"
                                             input-file e)
                                     (return)))))
                       (when (eq form eof) (return))
                       (handler-case (eval form)
                         (error (e)
                           (incf nerrors)
                           (format t "~&;; [CL-Amiga] WARNING in ~A: ~A~%"
                                   input-file e)))))
                   (when (> nerrors 0)
                     (format t "~&;; [CL-Amiga] ~A: ~D form~:P skipped~%"
                             input-file nerrors)))))))
      (error (e)
        (format t "~&;; [CL-Amiga] ERROR loading ~A: ~A (continuing)~%"
                input-file e)))
    ;; Create a marker FASL so ASDF considers the file compiled.
    ;; Without this, ASDF would re-compile on every load.
    (when output-file
      (ensure-directories-exist output-file)
      (with-open-file (s output-file :direction :output
                                      :if-exists :supersede)
        (format s ";; CL-Amiga marker FASL for ~A~%" input-file)))
    (values output-file nil nil)))

(defun perform-lisp-load-fasl (o c)
  "CL-Amiga override: no-op since perform-lisp-compilation already loaded
the source (which compiles and evaluates all forms).  Loading again would
redundantly re-compile everything and can crash on large functions."
  (declare (ignore o c))
  nil)

(in-package #:cl-user)
