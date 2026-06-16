;; Faithful repro of the chipi-api ITEMS--POST-ITEM-VALUE--204--OK failure.
;; Loads the real chipi-api system, replicates the api-start-stop fixture
;; setup, then hammers the real POST /items/foo request in a loop.  On the
;; first drakma framing error it ALSO does one raw-socket request so we can
;; print the exact bytes the server puts on the wire.

(setq *load-verbose* nil)
(require "asdf")
(load "trunk/load-libs-ql.lisp")
(ensure-ql-lib :babel) (asdf:load-system :babel)
(dolist (s '(:chipi :ironclad :cl-base64 :marshal :snooze :fiveam :cl-mock :split-sequence))
  (ensure-ql-lib s))
(asdf:load-system :chipi-api/tests)
(load "trunk/hunchentoot-clamiga.lisp")

(in-package :cl-user)

(defun raw-post (item value apikey)
  "Send a POST via a raw usocket and return the exact response bytes as a string."
  (let* ((sock (usocket:socket-connect "localhost" 8765 :element-type '(unsigned-byte 8)))
         (stream (usocket:socket-stream sock)))
    (unwind-protect
         (let ((req (format nil "POST /items/~A HTTP/1.1~C~CHost: localhost:8765~C~CX-Api-Key: ~A~C~CContent-Type: application/json~C~CContent-Length: ~D~C~CConnection: close~C~C~C~C~A"
                            item #\Return #\Linefeed #\Return #\Linefeed apikey #\Return #\Linefeed
                            #\Return #\Linefeed (length value) #\Return #\Linefeed #\Return #\Linefeed
                            #\Return #\Linefeed value)))
           (loop for ch across req do (write-byte (char-code ch) stream))
           (finish-output stream)
           (let ((out (make-string-output-stream)))
             (handler-case
                 (loop for b = (read-byte stream nil nil) while b
                       do (write-char (code-char b) out))
               (error () nil))
             (get-output-stream-string out)))
      (ignore-errors (usocket:socket-close sock)))))

(defun run ()
  (hab:defconfig "chipi")
  (api-env:init :apikey-store apikey-store:*memory-backend*
                      :apikey-lifetime (ltd:duration :day 1))
  (api:start)
  (sleep 1)
  (let ((apikey (apikey-store:create-apikey :access-rights '(:update)))
        (fails 0))
    (hab:defitem 'foo "label1" 'string :initial-value "bar")
    (dotimes (i 120)
      (handler-case
          (multiple-value-bind (body status)
              (drakma:http-request "http://localhost:8765/items/foo"
                                   :method :post :content "{\"value\":\"baz\"}"
                                   :content-type "application/json" :accept "application/json")
            (declare (ignore body))
            (unless (= status 204)
              (incf fails) (format t "~&iter ~D: status ~A~%" i status)))
        (error (e)
          (incf fails)
          (format t "~&!!! iter ~D drakma error: ~A~%" i e)
          (when (= fails 1)
            (format t "~&--- RAW response bytes for the same request: ---~%")
            (let ((raw (raw-post "foo" "{\"value\":\"baz\"}" apikey)))
              (format t "~S~%(length ~D)~%" raw (length raw)))))))
    (format t "~&DONE: ~D/120 failed~%" fails)
    (api:stop)))

(run)
