;;; quicklisp-compat.lisp — Wire CL-Amiga networking into quicklisp
;;;
;;; Load AFTER quicklisp setup.lisp:
;;;   (load "lib/asdf.lisp")
;;;   (load "/path/to/quicklisp/setup.lisp")
;;;   (load "lib/quicklisp-compat.lisp")
;;;
;;; This provides the TCP networking that quicklisp needs to download
;;; archives, and works around CL-Amiga compiler limitations.

;; Load Gray Streams implementation (needed by trivial-gray-streams).
;; REQUIRE resolves lib/ against the CWD and falls back to $CLAMIGA_HOME,
;; so this works when clamiga is launched outside the source root (editor,
;; ICL) — a cwd-relative LOAD here would not.
(require "gray-streams")

;; NOTE: a handful of systems (closer-mop, trivial-cltl2,
;; introspect-environment, trivial-garbage) carry first-class CL-Amiga
;; support in maintained forks that live in quicklisp's own
;; local-projects tree (cloned in; see README's Quicklisp section).
;; The `swank` stub under `contrib/shims/` is symlinked in by
;; `make install-shims`.  Quicklisp picks all of them up through its
;; normal local-projects search — no extra registration here.  Long-term
;; the fork branches should merge upstream so stock quicklisp just works
;; on CL-Amiga.

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

;; MAKE-BROADCAST-STREAM is a real builtin now (CLHS 21.2); quicklisp's
;; (make-broadcast-stream) for :quietly t works directly.  The historical
;; string-output-stream workaround that lived here silently shadowed the
;; builtin — keep this file free of CL redefinitions.

(in-package #:ql-impl-util)

;; ql-impl-util:directory-entries has no :implementation t fallback, so
;; ql:quickload's local-projects scan fails with "No applicable primary
;; method" on CL-Amiga. Override with the ccl/sbcl-style implementation.
;; CL-Amiga's DIRECTORY uses GLOB_MARK, which appends "/" to subdirs —
;; that's exactly what quicklisp's DIRECTORYP fallback recognizes.
(defun directory-entries (directory)
  (when (directoryp directory)
    (directory (merge-pathnames *wild-entry* directory))))

(in-package "COMMON-LISP-USER")

;; Marker so callers can tell whether the shim is already active.
;; Reloading this file in the same image re-wraps gray-streams etc.
;; and can corrupt CLOS dispatch — callers should check this feature
;; before a second LOAD.
(pushnew :quicklisp-compat *features*)
