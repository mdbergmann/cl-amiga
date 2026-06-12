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

;;; ---------------------------------------------------------------------------
;;; rfc2388 multipart upload: write the temp file as LATIN-1 (byte-faithful).
;;;
;;; A multipart/form-data file upload is binary, but rfc2388 parses the request
;;; over a CHARACTER stream (hunchentoot wraps the socket in a flexi-stream with
;;; :external-format :latin-1, so each request byte 0..255 arrives as the
;;; corresponding code-char).  PARSE-MIME copies those characters straight to a
;;; temp file via WITH-OPEN-FILE — and for byte-faithful output that file MUST
;;; be opened LATIN-1 too, or clamiga's default UTF-8 character stream encodes
;;; every code point > 127 as two bytes and the uploaded file is corrupted
;;; (its size inflates and the bytes no longer match what was sent).
;;;
;;; Upstream rfc2388 already opens the temp file :external-format :latin-1, but
;;; only inside a #+(or :sbcl :lispworks :allegro :openmcl :clisp) reader
;;; conditional — cl-amiga is none of those, so on cl-amiga the keyword is
;;; dropped and the file opens UTF-8.  We redefine the stream PARSE-MIME method
;;; to always pass :external-format :latin-1 (which clamiga's OPEN honours as
;;; the 8-bit-transparent path).  Identical to the upstream method otherwise.
(in-package :rfc2388)

(defmethod parse-mime ((input stream) boundary &key (write-content-to-file t))
  (unless (nth-value 1 (read-until-next-boundary input boundary t))
    (return-from parse-mime nil))
  (let ((result ()))
    (loop
      (let ((headers (loop
                       for header = (parse-header input)
                       while header
                       when (string-equal "CONTENT-TYPE" (header-name header))
                       do (setf (header-value header)
                                (parse-content-type (header-value header)))
                       collect header)))
        (let ((file-name (get-file-name headers)))
          (cond ((and write-content-to-file file-name)
                 (let ((temp-file (make-tmp-file-name)))
                   (multiple-value-bind (text more)
                       (with-open-file (out-file (ensure-directories-exist temp-file)
                                                 :direction :output
                                                 :external-format :latin-1)
                         (read-until-next-boundary input boundary nil out-file))
                     (declare (ignore text))
                     (when (and (stringp file-name) (plusp (length file-name)))
                       (push (make-mime-part temp-file headers) result))
                     (when (not more) (return)))))
                (t
                 (multiple-value-bind (text more)
                     (read-until-next-boundary input boundary)
                   (push (make-mime-part text headers) result)
                   (when (not more) (return))))))))
    (nreverse result)))

(in-package :hunchentoot)
