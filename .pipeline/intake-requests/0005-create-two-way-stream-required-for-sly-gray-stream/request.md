---
id: 0005-create-two-way-stream-required-for-sly-gray-stream
type: feature
status: implementing
title: create two-way-stream (required for Sly - gray streams)
---

# create two-way-stream (required for Sly - gray streams)

The following should be possible:

;;;; Minimal repro: MAKE-TWO-WAY-STREAM is not implemented in clamiga.
       2 ;;;;
       3 ;;;; Run with:
       4 ;;;;   clamiga --no-userinit --script /tmp/make-two-way-stream-missing.lisp
       5 ;;;;
       6 ;;;; CL spec: (make-two-way-stream input-stream output-stream) returns a
       7 ;;;; bidirectional stream that reads from INPUT-STREAM and writes to
       8 ;;;; OUTPUT-STREAM.  See also TWO-WAY-STREAM-INPUT-STREAM / -OUTPUT-STREAM.
       9
      10 (in-package :cl-user)
      11
      12 (let ((in  (make-string-input-stream "hello"))
      13       (out (make-string-output-stream)))
      14
      15   ;; These building blocks already work in clamiga:
      16   (format t "make-string-input-stream  => ~S~%" in)
      17   (format t "make-string-output-stream => ~S~%" out)
      18   (format t "fboundp 'make-string-input-stream  = ~S~%" (fboundp 'make-string-input-stream))
      19
      20   ;; ...but the two-way-stream constructor/accessors are not defined:
      21   (format t "fboundp 'make-two-way-stream          = ~S~%" (fboundp 'make-two-way-stream))
      22   (format t "fboundp 'two-way-stream-input-stream  = ~S~%" (fboundp 'two-way-stream-input-stream))
      23   (format t "fboundp 'two-way-stream-output-stream = ~S~%" (fboundp 'two-way-stream-output-stream))
      24
      25   ;; The actual call SLY makes when building a REPL listener:
      26   (format t "~%calling (make-two-way-stream in out) ...~%")
      27   (let ((tw (make-two-way-stream in out)))
      28     (format t "  got: ~S~%" tw)))
