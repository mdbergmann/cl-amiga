(macrolet ((emit () '(defmacro cf-ml-mac (x) (list 'list :ml x)))) (emit))
(defparameter *cf-ml-mac* (cf-ml-mac 7))
