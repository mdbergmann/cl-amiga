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

(in-package #:cl-user)
