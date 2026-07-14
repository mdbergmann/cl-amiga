;; verify/linux-arm64/eta-boot.lisp — container adaptation of
;; ~/Development/MySources/cl-eta/eta-boot.lisp for clamiga on Debian arm64.
;;
;; Differences from the original (SBCL / Raspberry Pi) eta-boot.lisp:
;;   * quicklisp dist + local-projects searchers are registered via
;;     trunk/load-libs-ql.lisp (on SBCL ~/.sbclrc does this)
;;   * slynk comes from the SLY fork that carries the clamiga backend
;;     (mounted at /work/sly), not from an elpa install
;;   * the quicklisp-dist dependency closure is pre-fetched with
;;     ENSURE-QL-LIB — plain (asdf:load-system ...) never downloads
;;   * after a successful load the image shuts down cleanly and exits,
;;     unless ETA_STAY=1 is set (then it stays up like the original,
;;     with slynk reachable on port 4005)
;;
;; Run via verify/linux-arm64/run.sh; cwd is the /tmp/cl-amiga build tree.

(setq *load-verbose* nil)

;; chipi's log:info lines print whole ITEM structs, and item <-> binding
;; structures reference each other — unbounded printing walks that graph
;; for MINUTES per log line (the printer's depth hard-cap is 64, and the
;; print-object hook costs two O(n) type-table walks + a CLOS dispatch per
;; visited node, all while holding the log lock).  Bound it hard; threads
;; inherit the global value.
(setq *print-level* 3
      *print-length* 10)

;; Skip the optional cl+ssl legs BEFORE any .asd that mentions them is read
;; (same as trunk/load-and-test-chipi-ui.lisp; clog/hunchentoot are
;; server-only here, and drakma-no-ssl comes from load-libs-ql.lisp).
(pushnew :hunchentoot-no-ssl *features*)
(pushnew :websocket-driver-no-ssl *features*)

(require "asdf")
(load "trunk/load-libs-ql.lisp")

;; babel is normally pulled in via the cffi/cl+ssl chain; with SSL skipped
;; it must be fetched + loaded explicitly (pure Lisp).
(ensure-ql-lib :babel)
(asdf:load-system :babel)

;; Pre-fetch the quicklisp-dist closure of eta-hab (chipi + chipi-api +
;; chipi-ui/clog + knx + cl-eta).  Local checkouts (chipi, knx-conn, sento,
;; cl-eta) and the cl-amiga forks in quicklisp local-projects resolve ahead
;; of the dist, so ENSURE-QL-LIB is a no-op for those.  The list is the
;; transitive closure computed from the dist's systems.txt (one system per
;; release; fetch-only, ASDF decides what actually loads).
(dolist (sys '(:alexandria :atomics :babel :binding-arrows :bit-smasher
               :blackbird :bordeaux-threads :cffi :chipz :chunga
               :circular-streams :cl+ssl :cl-base58 :cl-base64 :cl-change-case
               :cl-cron :cl-fad :cl-isaac :cl-pass :cl-ppcre
               :cl-speedy-queue :cl-template :cl-unicode :cl-utilities :clack
               :clog :closer-mop :com.inuoe.jzon :cserial-port :dbi
               :documentation-utils :drakma :dref :esrap :event-emitter
               :fast-http :fast-io :fast-websocket :fiveam :flexi-streams
               :float-features :global-vars :http-body :hunchentoot :idna
               :ironclad :iterate :lack :local-time :local-time-duration
               :log4cl :marshal :md5 :named-readtables :net.didierverna.asdf-flv
               :osicat :parse-float :proc-parse :puri :pythonic-string-reader
               :quri :rfc2388 :sha1 :smart-buffer :snooze
               ;; (static-vectors omitted: its .asd errors on non-listed
               ;; implementations, and fast-io only wants it #+fast-io-sv,
               ;; which clamiga is not)
               ;; (swank omitted: clack's swank dep is satisfied by the
               ;; local-projects stub — fetching would pull real slime,
               ;; whose loader breaks on clamiga)
               :split-sequence :sqlite :str
               :timer-wheel :trivial-backtrace :trivial-features :trivial-garbage
               :trivial-gray-streams :trivial-indent :trivial-mimes
               :trivial-rfc-1123 :trivial-utf-8 :trivial-with-current-source-form
               :usocket :vom :websocket-driver :xsubseq :yason))
  ;; one failing ensure (bad .asd read, network hiccup) must not abort the
  ;; rest of the closure — report it and let the eta-hab load surface it.
  (handler-case (ensure-ql-lib sys)
    (error (e)
      (format t "~&;; WARN: ensure-ql-lib ~a failed: ~a~%" sys e))))

;; --- mirrors the original eta-boot.lisp from here on ---

(format t "~%--- Loading slynk (SLY fork, clamiga backend) ---~%")
(asdf:load-asd "/work/sly/slynk/slynk.asd")
(asdf:load-system :slynk)

(format t "~%--- Loading eta-hab ---~%")
;; Pin binding-knx to the local chipi checkout — without this it resolves
;; through the quicklisp dist searcher to the (installed) chipi release.
(asdf:load-asd "/work/cl-hab/bindings/knx/binding-knx.asd")

(asdf:load-asd "/tmp/cl-eta/eta-hab.asd")

;; The KNX gateway is usually unreachable from the container (its tunnel
;; slot is held by the production instance, and the NAT'd container address
;; in the HPAI keeps responses from routing back), and a failing KNX-INIT
;; inside DEFCONFIG would abort the load before any item/rule definitions
;; run.  Tolerate the failure so the rest of eta-hab is exercised; set
;; ETA_STRICT_KNX=1 to keep the production fail-fast behaviour.
;;
;; Ordering matters: eta-hab.asd's EVAL-WHEN re-registers binding-knx via
;; LOAD-ASD (invalidating an earlier load), so binding-knx must be loaded
;; and wrapped only AFTER that .asd was read — otherwise the eta-hab plan
;; reloads it and the fresh DEFUN clobbers the wrapper.
(unless (uiop:getenv "ETA_STRICT_KNX")
  (asdf:load-system :binding-knx)
  ;; FIND-SYMBOL at run time: the package only exists after the LOAD-SYSTEM
  ;; above evaluates, which is later than this form is READ.
  (let* ((sym (or (find-symbol "KNX-INIT" "CHIPI.BINDING.KNX")
                  (error "knx-init not found after loading binding-knx")))
         (orig (fdefinition sym)))
    (setf (fdefinition sym)
          (lambda (&rest args)
            (handler-case (apply orig args)
              (error (e)
                (format t "~&;; WARN: knx-init failed (tolerated in verify): ~a~%" e)))))
    (format t ";; knx-init failure tolerance installed~%")))
(asdf:load-system :eta-hab)           ; runs the defconfig and starts everything

;; clamiga's --load reports an unhandled toplevel error and continues with
;; the next form, so reaching this point does NOT mean the load succeeded —
;; ask ASDF whether the whole plan completed.
(if (asdf:component-loaded-p :eta-hab)
    (format t "~%=== ETA-HAB LOADED OK ===~%")
    (format t "~%=== ETA-HAB LOAD FAILED (see errors above) ===~%"))

(slynk:create-server :interface "0.0.0.0" :port 4005 :dont-close t)
(format t "=== SLYNK SERVER ON PORT 4005 ===~%")

(if (uiop:getenv "ETA_STAY")
    ;; keep the image alive (original behaviour)
    (loop (sleep 60))
    ;; verification run: shut down cleanly — this releases the KNX tunnel
    ;; slot on the gateway if the connect succeeded — and exit.
    (progn
      (format t "=== SHUTTING DOWN (set ETA_STAY=1 to keep running) ===~%")
      (ignore-errors (funcall (find-symbol "SHUTDOWN" "CHIPI.HAB")))
      (format t "=== DONE ===~%")))
