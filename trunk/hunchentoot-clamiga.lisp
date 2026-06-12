;;;; hunchentoot-clamiga.lisp — cl-amiga portability shims for Hunchentoot.
;;;;
;;;; Hunchentoot has per-Lisp #+/#- conditionals for the few places it must
;;;; touch implementation internals.  cl-amiga is not (yet) one of the Lisps it
;;;; knows about, so those fall through to the generic "not implemented" error.
;;;; Load this file AFTER (asdf:load-system :hunchentoot) to patch them.
;;;;
;;;; This is loaded by the trunk scripts that run Hunchentoot as a SERVER over
;;;; the usocket cl-amiga backend (see trunk/load-and-test-drakma.lisp).  It is
;;;; deliberately a thin patch rather than a full fork: each shim is one small,
;;;; documented redefinition.

(in-package :hunchentoot)

;;; SET-TIMEOUTS.  Hunchentoot's set-timeouts.lisp sets the per-connection read
;;; and write timeouts through implementation-specific socket options
;;; (SO_RCVTIMEO / fd-stream-timeout / ...).  cl-amiga's sockets expose no
;;; Lisp-level timeout option (the EXT socket layer is a plain bidirectional
;;; stream), so the upstream definition reaches its #-(or ...) fallback and
;;; signals NOT-IMPLEMENTED — which, raised inside the acceptor's accept loop,
;;; kills the listener thread right after it accepts a connection.
;;;
;;; We make it a no-op: connections simply have no socket-level timeout, which
;;; matches what Hunchentoot already does on platforms that lack the option
;;; (e.g. :clasp only warns).  This is fine for the test server; a stalled peer
;;; would block its own handler thread but not the acceptor.
(defun set-timeouts (usocket read-timeout write-timeout)
  (declare (ignore usocket read-timeout write-timeout))
  nil)

;;; ---------------------------------------------------------------------------
;;; cl-fad (FAD) portability shims.
;;;
;;; cl-fad dispatches its filesystem primitives on the host implementation with
;;; #+(or :sbcl :lispworks ...) and falls through to (error "... not
;;; implemented") for any Lisp it doesn't know — which cl-amiga is.  Hunchentoot
;;; uses FAD:FILE-EXISTS-P in HANDLE-STATIC-FILE (serving the test's fz.jpg image
;;; and uploaded files) and FAD:LIST-DIRECTORY in the folder dispatcher, so
;;; without these the static-file / folder / upload handlers 500.
;;;
;;; cl-amiga's CL:PROBE-FILE already returns the truename of an existing file or
;;; directory (directory truename in directory form) and NIL otherwise — exactly
;;; FAD:FILE-EXISTS-P's contract and what the upstream :sbcl branch does.
(defun cl-fad:file-exists-p (pathspec)
  (probe-file pathspec))

;;; FAD:LIST-DIRECTORY enumerates the entries of a directory.  CL:DIRECTORY with
;;; a wild pathname over the directory does the same; mirror cl-fad's contract of
;;; returning directories in directory form (PATHNAME-AS-DIRECTORY).
(defun cl-fad:list-directory (dirname &key (follow-symlinks t))
  (declare (ignore follow-symlinks))
  (let ((dir (cl-fad:pathname-as-directory dirname)))
    (directory (make-pathname :name :wild :type :wild
                              :defaults dir))))
