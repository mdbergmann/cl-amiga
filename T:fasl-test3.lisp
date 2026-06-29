(defmacro fasl-triple (x) `(* 3 ,x))
(defun fasl-apply-triple (n) (fasl-triple n))
