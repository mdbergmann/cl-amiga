; Minimal repro for the MorphOS cross-closure NLX failure.
; Build clamiga with -DDEBUG_NLX and load this file; compare the [NLX]
; trace against the known-good host trace (see chat / commit message).
(defmacro tb-in-lambda (&body body) `(funcall (lambda () ,@body)))

(format t "=== tagbody/go through closure (expect 3) ===~%")
(format t "result: ~S~%"
  (let ((i 0))
    (tagbody start (setq i (+ i 1)) (tb-in-lambda (if (< i 3) (go start))))
    i))

(format t "=== block/return-from through closure (expect 42) ===~%")
(format t "result: ~S~%"
  (block blk (tb-in-lambda (return-from blk 42)) 99))
