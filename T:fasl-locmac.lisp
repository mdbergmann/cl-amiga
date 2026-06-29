(locally (declare (optimize speed)) (defmacro cf-loc-mac (x) (list 'list :loc x)))
(defparameter *cf-loc* (cf-loc-mac 9))
