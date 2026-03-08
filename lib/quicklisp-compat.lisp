;;; quicklisp-compat.lisp — Wire CL-Amiga networking into quicklisp
;;;
;;; Load AFTER quicklisp setup.lisp:
;;;   (load "lib/asdf.lisp")
;;;   (load "/path/to/quicklisp/setup.lisp")
;;;   (load "lib/quicklisp-compat.lisp")
;;;
;;; This provides the TCP networking that quicklisp needs to download
;;; archives, and works around CL-Amiga compiler limitations.

(in-package #:ql-impl)

;; Register CL-Amiga as a known implementation so *implementation* is non-NIL.
;; The base 'lisp' class provides fallback methods for read-octets, write-octets,
;; close-connection, and call-with-connection — all of which work via standard CL
;; stream operations (read-sequence, write-sequence, close).
(unless *implementation*
  (setf *implementation* (make-instance 'lisp)))

(in-package #:ql-network)

;; Override open-connection to use CL-Amiga's ext:open-tcp-stream.
;; Returns a bidirectional stream connected to host:port,
;; similar to LispWorks' comm:open-tcp-stream or CLISP's socket:socket-connect.
(defun open-connection (host port)
  (ext:open-tcp-stream host port))

(in-package #:ql-http)

;; Workaround: CL-Amiga compiler bug with let* variable shadowing + closure capture.
;; The original parse-urlstring shadows `pos` in let*, then mutates it in labels closures.
;; Our compiler captures the wrong binding for the mutated variable.
;; Fix: rename the second binding from `pos` to `cur` to avoid shadowing.
(defun parse-urlstring (urlstring)
  (setf urlstring (string-trim " " urlstring))
  (let* ((colon-pos (position #\: urlstring))
         (scheme (or (and colon-pos (subseq urlstring 0 colon-pos)) "http"))
         (cur (mismatch urlstring "://" :test 'char-equal :start1 colon-pos))
         (mark cur)
         (url (make-instance 'url)))
    (setf (scheme url) scheme)
    (labels ((save ()
               (subseq urlstring mark cur))
             (mark ()
               (setf mark cur))
             (finish ()
               (return-from parse-urlstring url))
             (hostname-char-p (char)
               (position char "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_."
                         :test 'char-equal))
             (at-start (char)
               (case char
                 (#\/
                  (setf (port url) nil)
                  (mark)
                  #'in-path)
                 (t
                  #'in-host)))
             (in-host (char)
               (case char
                 ((#\/ :end)
                  (setf (hostname url) (save))
                  (mark)
                  #'in-path)
                 (#\:
                  (setf (hostname url) (save))
                  (mark)
                  #'in-port)
                 (t
                  (unless (hostname-char-p char)
                    (error "~S is not a valid URL" urlstring))
                  #'in-host)))
             (in-port (char)
               (case char
                 ((#\/ :end)
                  (setf (port url)
                        (parse-integer urlstring
                                       :start (1+ mark)
                                       :end cur))
                  (mark)
                  #'in-path)
                 (t
                  (unless (digit-char-p char)
                    (error "Bad port in URL ~S" urlstring))
                  #'in-port)))
             (in-path (char)
               (case char
                 ((#\# :end)
                  (setf (path url) (save))
                  (finish)))
               #'in-path))
      (let ((state #'at-start))
        (loop
         (when (<= (length urlstring) cur)
           (funcall state :end)
           (finish))
         (setf state (funcall state (aref urlstring cur)))
         (incf cur))))))

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
