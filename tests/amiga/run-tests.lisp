; CL-Amiga Test Suite — runs in batch mode on AmigaOS
; Each expression must be on a single line (REPL reads line-by-line)

; --- Test infrastructure ---
(setq *pass-count* 0)
(setq *fail-count* 0)
(defun check (name expected actual) (if (equal expected actual) (progn (setq *pass-count* (+ *pass-count* 1)) (format t "PASS: ~A~%" name)) (progn (setq *fail-count* (+ *fail-count* 1)) (format t "FAIL: ~A - expected ~S got ~S~%" name expected actual))))

(format t "~%=== CL-Amiga Test Suite ===~%~%")

; --- Arithmetic ---
(check "add two" 3 (+ 1 2))
(check "add many" 15 (+ 1 2 3 4 5))
(check "add zero args" 0 (+))
(check "sub two" 7 (- 10 3))
(check "sub many" 50 (- 100 30 20))
(check "sub negate" -5 (- 5))
(check "mul two" 20 (* 4 5))
(check "mul many" 24 (* 2 3 4))
(check "mul zero args" 1 (*))
(check "div" 5 (/ 10 2))
(check "div many" 2 (/ 100 10 5))
(check "mod" 1 (mod 10 3))
(check "mod even" 0 (mod 10 2))
(check "1+" 42 (1+ 41))
(check "1-" 42 (1- 43))

; --- Comparison ---
(check "= equal" t (= 5 5))
(check "= not equal" nil (= 1 2))
(check "= many" t (= 3 3 3))
(check "< true" t (< 1 2))
(check "< false" nil (< 2 1))
(check "> true" t (> 3 2))
(check "> false" nil (> 1 2))
(check "<= equal" t (<= 3 3))
(check "<= less" t (<= 2 3))
(check ">= equal" t (>= 3 3))
(check ">= greater" t (>= 5 3))

; --- Numeric predicates ---
(check "zerop zero" t (zerop 0))
(check "zerop nonzero" nil (zerop 1))
(check "plusp positive" t (plusp 5))
(check "plusp negative" nil (plusp -1))
(check "minusp negative" t (minusp -3))
(check "minusp positive" nil (minusp 3))
(check "abs positive" 42 (abs 42))
(check "abs negative" 42 (abs -42))
(check "max" 5 (max 1 5 3))
(check "min" 1 (min 1 5 3))

; --- Type predicates ---
(check "null nil" t (null nil))
(check "null non-nil" nil (null 42))
(check "numberp fixnum" t (numberp 42))
(check "numberp symbol" nil (numberp 'foo))
(check "symbolp" t (symbolp 'foo))
(check "symbolp nil" t (symbolp nil))
(check "stringp" t (stringp "hello"))
(check "stringp num" nil (stringp 42))
(check "consp" t (consp '(1 2)))
(check "consp atom" nil (consp 42))
(check "atom number" t (atom 42))
(check "atom cons" nil (atom '(1 2)))
(check "listp cons" t (listp '(1 2)))
(check "listp nil" t (listp nil))
(check "not nil" t (not nil))
(check "not t" nil (not t))
(check "integerp" t (integerp 42))

; --- Equality ---
(check "eq same symbol" t (eq 'a 'a))
(check "eq diff symbol" nil (eq 'a 'b))
(check "eql fixnum" t (eql 42 42))
(check "equal list" t (equal '(1 2 3) '(1 2 3)))
(check "equal string" t (equal "hello" "hello"))
(check "equal nested" t (equal '(1 (2 3)) '(1 (2 3))))

; --- List operations ---
(check "car" 1 (car '(1 2 3)))
(check "cdr" '(2 3) (cdr '(1 2 3)))
(check "first" 1 (first '(1 2 3)))
(check "rest" '(2 3) (rest '(1 2 3)))
(check "cons" '(1 2 3) (cons 1 '(2 3)))
(check "list" '(1 2 3) (list 1 2 3))
(check "length" 3 (length '(1 2 3)))
(check "length nil" 0 (length nil))
(check "append" '(1 2 3 4) (append '(1 2) '(3 4)))
(check "reverse" '(3 2 1) (reverse '(1 2 3)))
(check "nth 0" 10 (nth 0 '(10 20 30)))
(check "nth 2" 30 (nth 2 '(10 20 30)))

; --- String operations ---
(check "string length" 5 (length "hello"))
(check "string equal" t (equal "foo" "foo"))
(check "string not equal" nil (equal "foo" "bar"))

; --- Control flow ---
(check "if true" 1 (if t 1 2))
(check "if false" 2 (if nil 1 2))
(check "if no else" nil (if nil 1))
(check "progn" 3 (progn 1 2 3))
(check "let simple" 10 (let ((x 10)) x))
(check "let body" 8 (let ((a 5) (b 3)) (+ a b)))
(check "let* sequential" 2 (let* ((x 1) (y (+ x 1))) y))

; --- Defun and recursion ---
(defun double (x) (* x 2))
(check "defun simple" 42 (double 21))
(defun factorial (n) (if (<= n 1) 1 (* n (factorial (- n 1)))))
(check "factorial 1" 1 (factorial 1))
(check "factorial 5" 120 (factorial 5))
(check "factorial 10" 3628800 (factorial 10))
(defun fib (n) (if (< n 2) n (+ (fib (- n 1)) (fib (- n 2)))))
(check "fibonacci 0" 0 (fib 0))
(check "fibonacci 1" 1 (fib 1))
(check "fibonacci 10" 55 (fib 10))

; --- Lambda ---
(check "lambda call" 25 ((lambda (x) (* x x)) 5))
(check "funcall builtin" 7 (funcall #'+ 3 4))

; --- Setq ---
(setq *my-var* 100)
(check "setq global" 100 *my-var*)
(setq *my-var* 200)
(check "setq update" 200 *my-var*)

; --- Keywords ---
(check "keyword eq" t (eq :foo :foo))
(check "keyword symbolp" t (symbolp :bar))

; --- Mapcar (built-in functions only) ---
(check "mapcar 1+" '(2 3 4) (mapcar #'1+ '(1 2 3)))
(check "mapcar -" '(-1 -2 -3) (mapcar #'- '(1 2 3)))

; --- Quote ---
(check "quote symbol" 'foo (quote foo))
(check "quote list" '(1 2 3) (quote (1 2 3)))

; --- Apply ---
(check "apply +" 6 (apply #'+ '(1 2 3)))
(check "apply list" '(1 2 3) (apply #'list '(1 2 3)))

; --- Macros ---
(defmacro always-42 () 42)
(check "macro constant" 42 (always-42))
(defmacro inc (x) (list '+ x 1))
(check "macro inc 5" 6 (inc 5))
(check "macro inc 99" 100 (inc 99))
(defmacro my-when (test &rest body) (list 'if test (cons 'progn body)))
(check "macro when true" 42 (my-when t 1 2 42))
(check "macro when false" nil (my-when nil 1 2 42))
(defmacro my-unless (test &rest body) (list 'if test nil (cons 'progn body)))
(check "macro unless nil" 99 (my-unless nil 1 2 99))
(check "macro unless t" nil (my-unless t 1 2 99))
(defmacro quote-it (x) (list 'quote x))
(check "macro identity" '(+ 1 2) (quote-it (+ 1 2)))

; --- Closures with captured upvalues ---
(check "closure let capture" 15 (let ((x 10)) ((lambda (y) (+ x y)) 5)))
(check "closure no-arg capture" 100 (let ((a 100)) ((lambda () a))))
(defun make-adder (n) (lambda (x) (+ n x)))
(check "make-adder 10+5" 15 ((make-adder 10) 5))
(check "make-adder 100+42" 142 ((make-adder 100) 42))
(check "closure multi-capture" 6 (let ((a 1) (b 2) (c 3)) ((lambda () (+ a b c)))))
(check "closure with param" 60 (let ((x 10) (y 20)) ((lambda (z) (+ x y z)) 30)))
(check "closure nested" 15 (let ((x 10)) ((lambda () ((lambda () (+ x 5)))))))
(defun make-pair (x) (list (lambda () x) (lambda () (+ x 1))))
(check "closure shared-a" 10 (let ((p (make-pair 10))) ((car p))))
(check "closure shared-b" 11 (let ((p (make-pair 10))) ((car (cdr p)))))

; --- Type-of ---
(check "type-of fixnum" 'fixnum (type-of 42))
(check "type-of string" 'string (type-of "hello"))
(check "type-of cons" 'cons (type-of '(1 2)))

; --- AND / OR / COND ---
(check "and empty" t (and))
(check "and single" 42 (and 42))
(check "and all true" 3 (and 1 2 3))
(check "and short-circuit" nil (and 1 nil 2))
(check "and first nil" nil (and nil 1))
(check "or empty" nil (or))
(check "or single" 42 (or 42))
(check "or first true" 1 (or 1 2))
(check "or skip nil" 2 (or nil 2 3))
(check "or all nil" nil (or nil nil nil))
(check "cond t-clause" 42 (cond (t 42)))
(check "cond skip nil" 2 (cond (nil 1) (t 2)))
(check "cond no match" nil (cond (nil 1)))
(check "cond multi-body" 3 (cond (t 1 2 3)))
(check "cond expr-test" 20 (cond ((= 1 2) 10) ((= 1 1) 20)))
(check "cond empty" nil (cond))

; --- WHEN / UNLESS (bootstrap macros) ---
(check "when true" 42 (when t 1 2 42))
(check "when false" nil (when nil 1 2 42))
(check "unless nil" 99 (unless nil 1 2 99))
(check "unless t" nil (unless t 1 2 99))

; --- Mapcar with lambda/closure ---
(check "mapcar lambda" '(1 4 9) (mapcar (lambda (x) (* x x)) '(1 2 3)))
(defun make-multiplier (n) (lambda (x) (* n x)))
(check "mapcar closure" '(3 6 9) (mapcar (make-multiplier 3) '(1 2 3)))

; --- Funcall with lambda/closure ---
(check "funcall lambda" 30 (funcall (lambda (a b) (+ a b)) 10 20))
(check "funcall closure" 15 (funcall (make-adder 10) 5))

; --- Apply with lambda/closure ---
(check "apply lambda" 7 (apply (lambda (a b) (+ a b)) '(3 4)))
(check "apply closure" 57 (apply (make-adder 50) '(7)))

; --- DOLIST ---
(setq *dl-result* nil)
(dolist (x '(1 2 3)) (setq *dl-result* (cons x *dl-result*)))
(check "dolist collect" '(3 2 1) *dl-result*)
(setq *dl-count* 0)
(dolist (x nil) (setq *dl-count* (+ *dl-count* 1)))
(check "dolist empty" 0 *dl-count*)
(check "dolist result" 42 (dolist (x '(1 2 3) 42)))
(check "dolist nil result" nil (dolist (x '(1 2 3))))
(check "dolist var nil in result" nil (dolist (x '(1 2 3) x)))

; --- DOTIMES ---
(setq *dt-sum* 0)
(dotimes (i 5) (setq *dt-sum* (+ *dt-sum* i)))
(check "dotimes sum 0-4" 10 *dt-sum*)
(setq *dt-count* 0)
(dotimes (i 0) (setq *dt-count* (+ *dt-count* 1)))
(check "dotimes zero" 0 *dt-count*)
(check "dotimes result" 99 (dotimes (i 3 99)))
(check "dotimes nil result" nil (dotimes (i 3)))
(check "dotimes var in result" 5 (dotimes (i 5 i)))

; --- DO ---
(check "do countdown" 42 (do ((i 10 (- i 1))) ((= i 0) 42)))
(check "do multi-var" 10 (do ((i 0 (+ i 1)) (sum 0 (+ sum i))) ((= i 5) sum)))
(check "do no-step" 100 (do ((x 100) (i 0 (+ i 1))) ((= i 3) x)))
(setq *do-body* 0)
(do ((i 0 (+ i 1))) ((= i 3)) (setq *do-body* (+ *do-body* 1)))
(check "do body exec" 3 *do-body*)
(check "do multi-result" 30 (do ((i 0 (+ i 1))) ((= i 1) 10 20 30)))
(check "do nil result" nil (do ((i 0 (+ i 1))) ((= i 1))))

; --- Quasiquote ---
(check "qq atom" 42 `42)
(check "qq symbol" 'foo `foo)
(check "qq simple list" '(1 2 3) `(1 2 3))
(check "qq unquote" '(a 3 c) `(a ,(+ 1 2) c))
(check "qq unquote var" '(val 10) (let ((x 10)) `(val ,x)))
(check "qq splice" '(a 1 2 3 b) `(a ,@'(1 2 3) b))
(check "qq splice concat" '(1 2 3 4) `(,@'(1 2) ,@'(3 4)))
(check "qq nested list" '(a (b 3)) `(a (b ,(+ 1 2))))
(check "qq dotted" '(a . 3) `(a . ,(+ 1 2)))
(defmacro qq-inc (x) `(+ ,x 1))
(check "qq macro inc" 6 (qq-inc 5))
(defmacro qq-progn (&rest body) `(progn ,@body))
(check "qq macro splice" 42 (qq-progn 1 2 42))

; --- Gensym ---
(check "gensym symbolp" t (symbolp (gensym)))
(check "gensym unique" nil (eq (gensym) (gensym)))
(check "gensym prefix" t (symbolp (gensym "MY-")))

; --- Boot functions ---
(check "cadr" 2 (cadr '(1 2 3)))
(check "caar" 'a (caar '((a b) c)))
(check "cddr" '(3) (cddr '(1 2 3)))
(check "caddr" 3 (caddr '(1 2 3)))
(check "identity" 42 (identity 42))
(check "endp nil" t (endp nil))
(check "endp list" nil (endp '(1)))

; --- Format ---
(check "format nil" nil (format t ""))

; --- Phase 4: return / return-from ---
(check "block return" 42 (block nil 1 (return 42) 3))
(check "block return-from" 42 (block foo (return-from foo 42)))
(check "block normal" 3 (block nil 1 2 3))
(check "return in do" 42 (do ((i 0 (1+ i))) ((= i 10) 99) (if (= i 5) (return 42))))
(check "return in dolist" 4 (dolist (x '(1 3 4 7)) (if (= 0 (mod x 2)) (return x))))
(check "return in dotimes" 7 (dotimes (i 100) (if (= i 7) (return i))))

; --- Phase 4: prog1 / prog2 ---
(check "prog1" 1 (prog1 1 2 3))
(check "prog2" 2 (prog2 1 2 3))

; --- Phase 4: case / ecase ---
(check "case match" 'b (case 2 (1 'a) (2 'b) (3 'c)))
(check "case multi-key" 'other (case 5 ((1 2) 'low) ((3 4) 'high) (t 'other)))
(check "case no match" nil (case 99 (1 'a)))
(check "ecase match" 'b (ecase 2 (1 'a) (2 'b)))
(check "typecase int" 'num (typecase 42 (integer 'num) (string 'str)))
(check "typecase str" 'str (typecase "hi" (integer 'num) (string 'str)))

; --- Phase 4: flet / labels ---
(check "flet basic" 4 (flet ((f (x) (+ x 1))) (f 3)))
(check "flet closure" 15 (let ((x 10)) (flet ((f (y) (+ x y))) (f 5))))
(check "labels fact" 120 (labels ((fact (n) (if (<= n 1) 1 (* n (fact (- n 1)))))) (fact 5)))
(check "labels mutual" t (labels ((even2 (n) (if (= n 0) t (odd2 (- n 1)))) (odd2 (n) (if (= n 0) nil (even2 (- n 1))))) (even2 4)))

; --- Phase 4: &optional / &key ---
(defun opt-test (a &optional b) (list a b))
(check "optional nil" '(1 nil) (opt-test 1))
(check "optional val" '(1 2) (opt-test 1 2))
(defun opt-def (a &optional (b 10)) (+ a b))
(check "optional default" 11 (opt-def 1))
(check "optional override" 3 (opt-def 1 2))
(defun key-test (&key x y) (list x y))
(check "key both" '(1 2) (key-test :x 1 :y 2))
(check "key none" '(nil nil) (key-test))
(check "key partial" '(nil 5) (key-test :y 5))
(defun key-def (&key (x 0) (y 10)) (+ x y))
(check "key default" 10 (key-def))
(check "key override" 15 (key-def :x 5))
(check "lambda optional" 15 ((lambda (a &optional (b 5)) (+ a b)) 10))

; --- Phase 4: &allow-other-keys ---
(defun aok-test (&key x &allow-other-keys) x)
(check "aok accepts unknown" 5 (aok-test :x 5 :z 99))
(check "aok no known key" nil (aok-test :z 99))
(check "caller allow-other-keys" '(1 nil) (key-test :x 1 :z 99 :allow-other-keys t))

; --- Phase 4: eval ---
(check "eval simple" 3 (eval '(+ 1 2)))
(check "eval complex" 42 (eval '(* 6 7)))
(check "eval quote" 'foo (eval ''foo))

; --- Phase 4: macroexpand-1 / macroexpand ---
(defmacro me-test (x) `(+ ,x 1))
(check "macroexpand-1 macro" '(+ 5 1) (macroexpand-1 '(me-test 5)))
(check "macroexpand-1 non-macro" '(+ 1 2) (macroexpand-1 '(+ 1 2)))
(defmacro me-wrap (x) `(me-test ,x))
(check "macroexpand nested" '(+ 5 1) (macroexpand '(me-wrap 5)))
(check "macroexpand non-macro" '(+ 1 2) (macroexpand '(+ 1 2)))

; --- Summary ---
(format t "~%=== Results ===~%")
(format t "Passed: ~A~%" *pass-count*)
(format t "Failed: ~A~%" *fail-count*)
(format t "Total:  ~A~%" (+ *pass-count* *fail-count*))
(if (= *fail-count* 0) (format t "~%ALL TESTS PASSED~%") (format t "~%SOME TESTS FAILED~%"))
