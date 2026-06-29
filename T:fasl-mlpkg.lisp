(macrolet ((def-it () '(defpackage :cf-ml-pkg (:use :cl) (:export #:marker)))) (def-it))
(defun cf-ml-fn () (symbol-name 'cf-ml-pkg:marker))
