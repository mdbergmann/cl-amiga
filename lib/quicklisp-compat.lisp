;;; quicklisp-compat.lisp — Wire CL-Amiga networking into quicklisp
;;;
;;; Load AFTER quicklisp setup.lisp:
;;;   (load "lib/asdf.lisp")
;;;   (load "/path/to/quicklisp/setup.lisp")
;;;   (load "lib/quicklisp-compat.lisp")
;;;
;;; This provides the TCP networking that quicklisp needs to download
;;; archives, and works around CL-Amiga compiler limitations.

;; Load Gray Streams implementation (needed by trivial-gray-streams)
(load (merge-pathnames "lib/gray-streams.lisp" *default-pathname-defaults*))

(in-package #:ql-impl)

;; Register CL-Amiga as a known implementation so *implementation* is non-NIL.
;; The base 'lisp' class provides fallback methods for read-octets, write-octets,
;; close-connection, and call-with-connection — all of which work via standard CL
;; stream operations (read-sequence, write-sequence, close).
(unless *implementation*
  (setf *implementation* (make-instance 'lisp)))

(in-package #:ql-network)

;; Override open-connection to use CL-Amiga's ext:open-tcp-stream.
(defun open-connection (host port)
  (ext:open-tcp-stream host port))

;; Direct implementations of definterface functions.
;; The definterface GF dispatch via *implementation* has issues in CL-Amiga,
;; so we override the public entry points directly with the "t" fallback behavior.
(defun read-octets (buffer connection)
  (read-sequence buffer connection))

(defun write-octets (buffer connection)
  (write-sequence buffer connection)
  (force-output connection))

(defun close-connection (connection)
  (ignore-errors (close connection)))

(defun call-with-connection (host port fun)
  (let (connection)
    (unwind-protect
         (progn
           (setf connection (open-connection host port))
           (funcall fun connection))
      (when connection
        (close-connection connection)))))

(in-package #:ql-http)

;; Workaround: make-broadcast-stream not yet implemented.
;; Quicklisp uses (make-broadcast-stream) for :quietly t in http-fetch.
;; Provide a minimal version that discards all output.
;; Workaround: make-broadcast-stream not yet implemented in CL-Amiga.
;; Quicklisp uses (make-broadcast-stream) for :quietly t in http-fetch.
;; The symbol got interned in ql-http (since it wasn't in CL when quicklisp loaded).
;; Define it there, and also in CL for general use.
(in-package "COMMON-LISP")
(defun make-broadcast-stream (&rest streams)
  (declare (ignore streams))
  (make-string-output-stream))
(export 'make-broadcast-stream)

;; Also define on the ql-http-interned symbol
(in-package "QL-HTTP")
(defun make-broadcast-stream (&rest streams)
  (declare (ignore streams))
  (make-string-output-stream))

(in-package "COMMON-LISP-USER")
