(defun ltv-gen-fn () (list 4 5 6))
(defun ltv-get-fn () (load-time-value (ltv-gen-fn)))
