;;; load-libs-ql.lisp — make the load-and-test dependencies
;;; resolvable through ASDF, with quicklisp kept entirely on this side.
;;;
;;; The sibling trunk/load-and-test-*.lisp scripts deliberately contain
;;; NO quicklisp references — they only ever call (asdf:load-system ...).
;;; That works because each of them loads THIS file first, and this file:
;;;
;;;   1. probes for quicklisp (~/quicklisp/setup.lisp on host,
;;;      S:quicklisp/setup.lisp on Amiga) and auto-installs it via
;;;      lib/quicklisp-install.lisp when missing;
;;;   2. loads setup.lisp + lib/quicklisp-compat.lisp once, which
;;;      registers quicklisp's dist + local-projects searchers with
;;;      ASDF — that is what lets (asdf:load-system :fiveam) find a
;;;      quicklisp-managed system at all;
;;;   3. defines ENSURE-QL-LIB, which a test script calls for the ONE
;;;      system it tests. It fetches that system from the quicklisp dist
;;;      into the local repo only if it is not there already, then leaves
;;;      the actual load to the script's own (asdf:load-system ...).
;;;
;;; We deliberately do NOT fetch every dependency up front. Each test
;;; script is meant to run from a cold boot (fresh image, and typically a
;;; cleared FASL cache), exercising the compile+load of just its own
;;; library — pre-fetching the others would touch the network for systems
;;; the run never uses. Fetching is idempotent at the local-repo level:
;;; once a release is on disk it is never re-downloaded, so cold-boot
;;; runs stay offline-fast.
;;;
;;; Run paths are relative to the repo root, matching how the scripts
;;; are invoked, e.g.:
;;;
;;;   ./build/host/clamiga --heap 24M --load trunk/load-and-test-5am.lisp
;;;
;;; Shim systems (closer-mop, trivial-cltl2, trivial-garbage) need no
;;; ENSURE-QL-LIB call — they live under contrib/shims/ and are symlinked
;;; into quicklisp's local-projects tree by `make install-shims`, so
;;; quicklisp's local-projects searcher resolves them once setup runs.

(require "asdf")

;; Host: ~/quicklisp/setup.lisp, Amiga: S:quicklisp/setup.lisp
(defvar *ql-setup*
  #+amigaos #P"S:quicklisp/setup.lisp"
  #-amigaos (merge-pathnames "quicklisp/setup.lisp" (user-homedir-pathname)))

;; Install quicklisp if missing. Use FIND-SYMBOL to invoke the entry
;; point because the CL-AMIGA-QL package does not exist at read time.
(unless (probe-file *ql-setup*)
  (load "lib/quicklisp-install.lisp")
  (funcall (find-symbol "INSTALL" "CL-AMIGA-QL")))

;; Skip the SETUP load when CL-AMIGA-QL:INSTALL has just initialised
;; quicklisp in this image (re-loading SETUP.LISP triggers a second
;; ASDF:OOS on the QL client, which redefines its CLOS classes and
;; corrupts generic-function dispatch). Same goes for QUICKLISP-COMPAT:
;; re-loading it re-wraps gray-streams and breaks dispatch.
(unless (member :quicklisp *features*)
  (load *ql-setup*))

(unless (member :quicklisp-compat *features*)
  (load "lib/quicklisp-compat.lisp"))

(defun ensure-ql-lib (name)
  "Make system NAME loadable from the local quicklisp repo, fetching it
from the dist only when it is not there yet.

If ASDF can already locate NAME (an already-downloaded dist release, or a
contrib/shims system on quicklisp's local-projects path) we do nothing —
no network, no re-download. Otherwise we pull the matching dist release to
disk via QL-DIST:ENSURE-INSTALLED. Either way the system is NOT loaded
here; the caller does that with (asdf:load-system name)."
  (let ((sname (string-downcase (string name))))
    (unless (asdf:find-system sname nil)   ; located locally? then leave it
      (let ((sys (ignore-errors
                   (funcall (find-symbol "FIND-SYSTEM" "QL-DIST") sname))))
        (when sys
          (format t "~&;; load-libs-ql: fetching ~A from quicklisp dist~%" sname)
          (funcall (find-symbol "ENSURE-INSTALLED" "QL-DIST") sys))))
    sname))
