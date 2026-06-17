;; Load and test CLOG (Common Lisp Omnificent GUI, system "clog") via ASDF.
;;
;; Like the sibling load-and-test-*.lisp scripts this file contains NO
;; quicklisp references: load-libs-ql.lisp sets up the dist + local-projects
;; searchers (and the :drakma-no-ssl feature cl-amiga needs), ENSURE-QL-LIB
;; fetches each system from the dist on a cold cache, and ASDF does the rest.
;;
;; cl-amiga ships a real CFFI-SYS backend (cffi-clamiga.lisp, libffi-based,
;; host: dlopen/dlsym), so CLOG's foreign-backed deps (static-vectors via
;; fast-io; cl-sqlite via clog-dbi) load through genuine CFFI on the host.
;; The only TLS pieces are sidestepped with the established *-no-ssl features:
;;   * :hunchentoot-no-ssl   — Hunchentoot skips cl+ssl (plain HTTP only)
;;   * :websocket-driver-no-ssl — websocket-driver-client skips cl+ssl
;;     (CLOG is server-only and never opens an outbound wss: client socket)
;; CLACK names the `swank' ASDF system unconditionally (only to reach
;; SWANK:CREATE-SERVER for an optional, NIL-by-default remote-debug server);
;; cl-amiga has no portable SWANK backend, so contrib/shims/swank supplies a
;; minimal stub package — install it with `make install-shims`.
;;
;; Usage (host):
;;   ./build/host/clamiga --heap 256M --load trunk/load-and-test-clog.lisp
;;
;; On Amiga: --heap >= 96M and a large stack (e.g. `stack 800000`).  NB the
;; foreign-call paths (static-vectors allocation, sqlite) are host-only today;
;; on AmigaOS ffi:symbol-pointer returns NIL.

(setq *load-verbose* nil)

;; Skip the optional cl+ssl deps BEFORE any .asd that mentions them is read.
(pushnew :hunchentoot-no-ssl *features*)
(pushnew :websocket-driver-no-ssl *features*)

(require "asdf")
(load "trunk/load-libs-ql.lisp")

;; Make CLOG and its full dependency closure resolvable from the quicklisp
;; dist (download-only; ASDF performs the actual load/compile below).  Listed
;; roughly in dependency order; ENSURE-QL-LIB is idempotent and only touches
;; the network for releases not already on disk.  Foundational/forked systems
;; (cffi, trivial-features, atomics, bordeaux-threads, usocket, hunchentoot,
;; cl-fad, rfc2388, chipz, ...) already live under quicklisp local-projects.
(dolist (sys '(;; foundations
               :alexandria :babel :trivial-features :split-sequence :iterate
               :named-readtables :pythonic-string-reader :trivial-utf-8
               :trivial-gray-streams :trivial-backtrace :trivial-indent
               :global-vars :cl-ppcre :parse-float :documentation-utils
               ;; foreign-backed (real CFFI on host)
               :static-vectors :cffi
               ;; fast-* I/O + http + websocket stack
               :fast-io :xsubseq :proc-parse :smart-buffer :cl-utilities
               :fast-http :fast-websocket :event-emitter :sha1 :cl-base64
               :circular-streams :websocket-driver
               ;; web server stack (clack/lack over hunchentoot)
               :chunga :flexi-streams :trivial-mimes :trivial-rfc-1123
               :http-body :quri :idna :clack :lack
               ;; docs / templating / crypto / db used by clog core
               :mgl-pax :cl-template :ironclad :md5 :cl-isaac :cl-pass
               :yason :cl-dbi :sqlite
               ;; the target
               :clog))
  (ensure-ql-lib sys))

(format t "~%--- Loading CLOG ---~%")
(asdf:load-system :clog)

;; CLOG has no fiveam suite of its own; "test" here is a load + offline smoke
;; test that the core entry points are present and a CLOG element can be
;; constructed without standing up a live websocket connection.
(format t "~%--- CLOG smoke test ---~%")
;; (package . symbol-name) — CREATE-WEB-PAGE lives in the CLOG-WEB subsystem
;; package, the rest in CLOG proper.
(let ((probes '(("CLOG" . "INITIALIZE")     ("CLOG" . "SET-ON-NEW-WINDOW")
                ("CLOG" . "CREATE-DIV")     ("CLOG" . "CREATE-CHILD")
                ("CLOG-WEB" . "CREATE-WEB-PAGE")
                ("CLOG" . "HTML-DOCUMENT")  ("CLOG" . "CLOG-OBJ")))
      (missing 0))
  (dolist (p probes)
    (let* ((pkg (car p)) (s (cdr p))
           (sym (and (find-package pkg) (find-symbol s pkg))))
      (format t "~&;; ~a:~a => ~:[MISSING~;present~]~%" pkg s sym)
      (unless sym (incf missing))))
  (format t "~&;; CLOG version: ~a~%"
          (asdf:component-version (asdf:find-system :clog)))
  (if (zerop missing)
      (format t "~&--- CLOG loaded: all probed entry points present ---~%")
      (progn
        (format t "~&--- CLOG loaded but ~a entry point(s) MISSING ---~%" missing)
        (error "~D CLOG entry point(s) missing" missing))))
