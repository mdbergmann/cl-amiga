(defvar *log* nil)
(let ((*log* nil))
  (restart-case
    (unwind-protect
      (progn (push 'before *log*) (invoke-restart 'abort))
      (push 'cleanup *log*))
    (abort () (push 'handler *log*) :done))
  (format t "Order: ~S~%" (reverse *log*)))
