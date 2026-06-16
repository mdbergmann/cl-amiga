;; Minimal repro for the chipi-api integration-test flakiness:
;;   DRAKMA:SYNTAX-ERROR "No space in status line """ / "No status line"
;; Hypothesis: bodyless responses (204 / 304 / HEAD) framing race on the
;; cl-amiga loopback socket layer.  Stand up an easy-acceptor whose handler
;; returns a bare 204 (no body) and a sibling that returns a 200 WITH a body,
;; then hammer each N times with drakma and count how many reads fail.

(setq *load-verbose* nil)
(require "asdf")
(load "trunk/load-libs-ql.lisp")
(pushnew :hunchentoot-no-ssl *features*)
(dolist (sys '(:trivial-features :alexandria :babel :cffi
               :cl-base64 :chunga :flexi-streams :cl-ppcre
               :md5 :rfc2388 :trivial-backtrace
               :usocket :bordeaux-threads :cl-fad :puri
               :cl-who :drakma :hunchentoot))
  (ensure-ql-lib sys))
(asdf:load-system :hunchentoot)
(load "trunk/hunchentoot-clamiga.lisp")
(asdf:load-system :drakma)

(in-package :cl-user)

(defparameter *port* 4243)

(hunchentoot:define-easy-handler (h204 :uri "/no-content") ()
  ;; Read the POST body like the real handler, and allocate heavily to drive
  ;; GC pressure (compaction) during the request — mirrors the real snooze
  ;; route which conses a lot (json + log4cl) per request.
  (hunchentoot:raw-post-data :force-binary t)
  (let ((junk nil))
    (dotimes (i 4000) (push (format nil "garbage-~D-~D" i (* i i)) junk))
    (setf junk nil))
  (setf (hunchentoot:return-code*) hunchentoot:+http-no-content+)
  ;; Return a NON-empty body that hunchentoot must discard for the 204 — this
  ;; matches the real snooze route (whose 204 carries a discarded body).
  "{\"discarded\":\"body-for-204\"}")

(hunchentoot:define-easy-handler (h200 :uri "/with-body") ()
  (setf (hunchentoot:content-type*) "application/json")
  "{\"value\":\"baz\"}")

(defun hammer (uri n)
  (let ((fails 0) (ok 0) (last-err nil))
    (dotimes (i n)
      (handler-case
          (multiple-value-bind (body status)
              (drakma:http-request (format nil "http://localhost:~A~A" *port* uri)
                                   :method :post
                                   :content "{\"value\":\"baz\"}"
                                   :content-type "application/json"
                                   :accept "application/json")
            (declare (ignore body))
            (if (or (= status 204) (= status 200))
                (incf ok)
                (progn (incf fails) (setf last-err (format nil "status ~A" status)))))
        (error (e)
          (incf fails)
          (setf last-err (princ-to-string e))
          (format t "~&!!! FAIL #~D on ~A: ~A~%" i uri e)
          (ignore-errors (ext:backtrace)))))
    (format t "~&~A: ~D ok, ~D FAIL~@[  last-err: ~A~]~%" uri ok fails last-err)
    fails))

(let ((server (hunchentoot:start
               (make-instance 'hunchentoot:easy-acceptor
                              :port *port*
                              :message-log-destination nil
                              :access-log-destination nil))))
  (unwind-protect
       (progn
         (sleep 2)
         (format t "~%=== hammering /no-content (bare 204) x256 ===~%")
         (hammer "/no-content" 256))
    (ignore-errors (hunchentoot:stop server))))

(format t "~&DONE~%")
