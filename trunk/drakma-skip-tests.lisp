;; drakma-skip-server-tests.lisp — loaded by load-and-test-drakma.lisp AFTER
;; the :drakma-test system is loaded (so the DRAKMA-TEST and FIVEAM packages
;; exist and the original tests are defined).
;;
;; cl-amiga runs drakma as an HTTP/HTTPS *client* over the usocket cl-amiga
;; backend + cl+ssl.  The tests left running here exercise exactly that and
;; pass reliably: plain HTTP and HTTPS, GET and POST (google.com), streamed
;; responses (STREAM / FORCE-BINARY), cl+ssl certificate verification (the
;; VERIFY.* badssl.com tests), AND — as of 2026-06-12 — the form-POST/PUT
;; tests that stand up a LOCAL hunchentoot server (post/put-x-www-form,
;; post/put-multipart-form).  Hunchentoot now runs as a server over the
;; usocket cl-amiga backend; see trunk/hunchentoot-clamiga.lisp.
;;
;; The only tests still redefined below as fiveam SKIPs depend on httpbin.org,
;; which is ORTHOGONAL to the client+server-on-cl-amiga goal:
;;
;;   httpbin.org dependency — these assert on httpbin.org responses, but
;;   httpbin rate-limits aggressively (returns 503) and sometimes stalls,
;;   which makes them flaky/hang in unattended runs regardless of cl-amiga.
;;   gzip-content/deflate-content also exercise chipz streaming decode
;;   (now supported, see below) but httpbin remains their real blocker.
;;
;; NOTE: chipz streaming decompression (drakma's :decode-content t, which wraps
;; the reply in chipz:make-decompressing-stream) IS now supported on cl-amiga
;; via the mdbergmann/chipz fork's #+cl-amiga gray-stream branch, so the
;; google.com gzip-decode tests (get-google-gzip / get-google-gzip-no-close)
;; run here and pass.  Only the httpbin-backed decode tests stay skipped, for
;; the httpbin flakiness above — not for any chipz limitation.
;;
;; Remove the relevant SKIPs (and the underlying support) to re-enable a test.

(in-package :drakma-test)

(5am:in-suite :drakma)

(macrolet ((skip-tests (reason &rest names)
             `(progn ,@(mapcar (lambda (n) `(5am:test ,n (5am:skip ,reason)))
                               names))))
  ;; Assert on httpbin.org, which rate-limits (503) and stalls.
  ;; gzip-content/deflate-content also stream-gunzip via chipz (now supported),
  ;; but httpbin — not chipz — is what makes them flaky here.
  (skip-tests "cl-amiga: depends on httpbin.org which rate-limits (503) / stalls — flaky"
              gzip-content deflate-content
              gzip-content-undecoded deflate-content-undecoded))
