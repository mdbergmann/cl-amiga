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
