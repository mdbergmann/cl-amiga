;; bench.lisp — Simple benchmarks for comparing CL implementations
;; Usage: clamiga --load bench.lisp
;;        sbcl --load bench.lisp
;;        ecl --load bench.lisp

(defun factorial (n)
  (let ((result 1))
    (dotimes (i n result)
      (setf result (* result (1+ i))))))

(defun fib-iter (n)
  (let ((a 0) (b 1))
    (dotimes (i n a)
      (psetf a b b (+ a b)))))

(defun tak (x y z)
  (if (not (< y x))
      z
      (tak (tak (1- x) y z)
           (tak (1- y) z x)
           (tak (1- z) x y))))

(defun bench-factorial (iterations)
  (dotimes (i iterations)
    (factorial 1000)))

(defun bench-fib (iterations)
  (dotimes (i iterations)
    (fib-iter 1000)))

(defun bench-tak (iterations)
  (dotimes (i iterations)
    (tak 18 12 6)))

(defun bench-list-ops (n)
  (let ((lst (loop for i from 0 below n collect i)))
    (dotimes (i 100)
      (reduce #'+ lst)
      (mapcar #'1+ lst)
      (remove-if #'evenp lst))))

(defun bench-hash (n)
  (let ((ht (make-hash-table :test #'eql)))
    (dotimes (i n)
      (setf (gethash i ht) (* i i)))
    (dotimes (i n)
      (gethash i ht))))

(defun run-bench (name fn &rest args)
  (let ((start (get-internal-real-time)))
    (apply fn args)
    (let* ((end (get-internal-real-time))
           (elapsed (/ (- end start)
                       (float internal-time-units-per-second))))
      (format t "  ~25A ~6,3F s~%" name elapsed))))

(defun run-all ()
  (format t "~%=== CL Benchmark Suite ===~%")
  (format t "  Implementation: ~A ~A~%"
          (lisp-implementation-type)
          (lisp-implementation-version))
  (format t "~%")
  (run-bench "factorial 1000 x5000"  #'bench-factorial 5000)
  (run-bench "fib-iter 1000 x5000"   #'bench-fib 5000)
  (run-bench "tak 18 12 6 x100"      #'bench-tak 100)
  (run-bench "list-ops 10000"         #'bench-list-ops 10000)
  (run-bench "hash 100000"            #'bench-hash 100000)
  (format t "~%=== Done ===~%"))

(run-all)
