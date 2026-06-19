;; Load and test knx-conn (KNXnet/IP implementation, system "knx-conn")
;; via ASDF's test-op.
;;
;; knx-conn resolves through ASDF's source registry the same way the other
;; load-and-test-*.lisp scripts do — its .asd is symlinked into ~/common-lisp
;; on the host (and its two cl-amiga-relevant deps, usocket and sento, resolve
;; to the local forks: usocket from quicklisp's local-projects, sento from
;; ~/common-lisp).  Like the sibling scripts this file contains NO quicklisp
;; references: load-libs-ql.lisp sets up the dist + local-projects searchers,
;; ENSURE-QL-LIB fetches the remaining deps from the dist on a cold cache, and
;; ASDF does the rest.
;;
;; The test system's TEST-OP runs (fiveam:run! knx-conn.tests:test-suite).
;; The KNXC-TUNNEL-E2E-TESTS suite is deliberately NOT part of that suite (it
;; talks to a real KNX/IP gateway and is run manually on demand), so it is only
;; compiled here, never executed — no live gateway is required.
;;
;; Usage (host):
;;   ./build/host/clamiga --heap 192M --load trunk/load-and-test-knx-conn.lisp
;;
;; On Amiga: --heap >= 96M and a large stack (e.g. `stack 800000`).

(setq *load-verbose* nil)
(require "asdf")
(load "trunk/load-libs-ql.lisp")

;; babel is a direct knx-conn dependency (utils.lisp uses babel:string-to-octets
;; for the byte-level KNX wire encoding).  It is pure Lisp (alexandria +
;; trivial-features) so it loads fine here; fetch + load it explicitly, mirroring
;; the chipi script, so the load below never trips over a missing transitive dep.
(ensure-ql-lib :babel)
(asdf:load-system :babel)

;; Make knx-conn's remaining quicklisp-managed deps and its test-only deps
;; resolvable from the dist (download-only on a cold cache; ASDF performs the
;; actual compile/load below).  knx-conn itself, usocket and sento resolve to
;; local sources and need no fetch.
(ensure-ql-lib :log4cl)
(ensure-ql-lib :binding-arrows)
(ensure-ql-lib :local-time)
(ensure-ql-lib :knx-conn)
(ensure-ql-lib :fiveam)
(ensure-ql-lib :cl-mock)

;; Load knx-conn (and its sento + log4cl deps) before running the tests so the
;; log4cl warm-cache workaround below can run against a loaded log4cl.
(asdf:load-system :knx-conn)

;; --- log4cl warm-cache workaround ---
;; On a COLD FASL cache sento is compiled in this image, and macroexpanding its
;; (log:debug ...) forms creates the log4cl logger objects (including the parent
;; (SENTO) category) in the live logger tree, where they persist for the run.
;; On a WARM cache sento is loaded from FASL instead: log4cl embeds those logger
;; objects as literal constants in the compiled log statements and relies on
;; MAKE-LOAD-FORM to reconstruct them at load time — but clamiga's FASL writer
;; does not yet honor MAKE-LOAD-FORM, so the (SENTO) logger is never registered.
;; knx-client-test.lisp / knx-connect-test.lisp then call
;;   (log:config '(sento) :warn)
;; which log4cl rejects with "Logger named (SENTO) not found".  Pre-create the
;; (SENTO) logger here so that call succeeds; harmless on a cold cache where it
;; already exists.  read-from-string defers reading the LOG4CL:* symbols until
;; runtime, after log4cl has been loaded above.
(ignore-errors
  (eval (read-from-string "(log4cl:make-logger '(sento))")))

(format t "~%--- Running (asdf:test-system :knx-conn) ---~%")
(asdf:test-system :knx-conn)
