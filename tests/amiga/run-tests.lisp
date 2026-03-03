; CL-Amiga Test Suite — runs in batch mode on AmigaOS
; Multi-line expressions supported (paren-balanced accumulation)

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

; --- Phase 4 Tier 2: tagbody/go ---
(check "tagbody basic" 2 (let ((x 0)) (tagbody (setq x 1) (setq x (+ x 1))) x))
(check "tagbody forward go" 0 (let ((x 0)) (tagbody (go end) (setq x 99) end) x))
(check "tagbody backward go" 5 (let ((x 0)) (tagbody loop (setq x (+ x 1)) (if (< x 5) (go loop))) x))
(check "tagbody fixnum tags" 42 (let ((x 0)) (tagbody (go 2) 1 (setq x 99) 2 (setq x 42)) x))
(check "tagbody go from if" 3 (let ((x 0)) (tagbody start (setq x (+ x 1)) (if (< x 3) (go start))) x))

; --- Phase 4 Tier 2: catch/throw ---
(check "catch basic" 42 (catch 'done (throw 'done 42)))
(check "catch normal" 3 (catch 'done (+ 1 2)))
(defun throw-helper-t () (throw 'bail 99))
(check "catch across call" 99 (catch 'bail (+ 1 (throw-helper-t))))
(check "catch nested inner" 15 (catch 'outer (+ 10 (catch 'inner (throw 'inner 5)))))
(check "catch nested outer" 42 (catch 'outer (catch 'inner (throw 'outer 42))))
(check "throw no value" nil (catch 'done (throw 'done)))

; --- Phase 4 Tier 2: unwind-protect ---
(check "uwp normal" '(42 t) (let ((log nil)) (let ((r (unwind-protect 42 (setq log t)))) (list r log))))
(check "uwp throw cleanup" t (let ((cleanup nil)) (catch 'done (unwind-protect (throw 'done 1) (setq cleanup t))) cleanup))
(check "uwp throw value" 42 (catch 'done (unwind-protect (throw 'done 42) (+ 1 2))))
(check "uwp nested" '(outer inner) (let ((log nil)) (catch 'done (unwind-protect (unwind-protect (throw 'done 1) (setq log (cons 'inner log))) (setq log (cons 'outer log)))) log))

; --- Phase 4 Tier 2: Multiple Values ---
(check "values none" nil (values))
(check "values one" 1 (values 1))
(check "values primary" 1 (values 1 2 3))
(check "mvb basic" '(1 2 3) (multiple-value-bind (a b c) (values 1 2 3) (list a b c)))
(check "mvb fewer values" '(1 nil nil) (multiple-value-bind (a b c) (values 1) (list a b c)))
(check "mvb fewer vars" 10 (multiple-value-bind (a) (values 10 20 30) a))
(check "mv-list values" '(1 2 3) (multiple-value-list (values 1 2 3)))
(check "mv-list none" nil (multiple-value-list (values)))
(check "mv-list single" '(3) (multiple-value-list (+ 1 2)))
(check "nth-value 0" 10 (nth-value 0 (values 10 20 30)))
(check "nth-value 1" 20 (nth-value 1 (values 10 20 30)))
(check "nth-value 2" 30 (nth-value 2 (values 10 20 30)))
(check "nth-value oob" nil (nth-value 5 (values 1 2 3)))
(check "values-list" '(1 2 3) (multiple-value-list (values-list '(1 2 3))))
(check "values-list primary" 42 (values-list '(42)))
(check "mv progn" '(2 3 4) (multiple-value-list (progn 1 (values 2 3 4))))
(check "mv if" '(1 2) (multiple-value-list (if t (values 1 2))))
(check "mv let" '(1 2 3) (multiple-value-list (let ((x 1)) (values x 2 3))))
(check "mvp1" '(1 2 3) (multiple-value-list (multiple-value-prog1 (values 1 2 3) (+ 4 5))))

; --- Dynamic (special) variables ---
(defvar *dv1* 10)
(check "defvar basic" 10 *dv1*)
(defvar *dv1* 99)
(check "defvar no overwrite" 10 *dv1*)
(defparameter *dp1* 10)
(check "defparameter basic" 10 *dp1*)
(defparameter *dp1* 20)
(check "defparameter overwrite" 20 *dp1*)
(defconstant +dc-test+ 42)
(check "defconstant basic" 42 +dc-test+)
(check "defconstant set error" 42
       (handler-case (set '+dc-test+ 0)
         (error (c) 42)))
(check "defconstant value preserved" 42 +dc-test+)
(defvar *sv1* 1)
(check "special let binding" 2 (let ((*sv1* 2)) *sv1*))
(check "special restored" 1 *sv1*)
(check "special let* binding" 20 (let* ((*sv1* 20)) *sv1*))
(check "special let* restored" 1 *sv1*)
(defvar *sv2* 0)
(defun read-sv2 () *sv2*)
(check "special visible in fn" 99 (let ((*sv2* 99)) (read-sv2)))
(check "special fn restored" 0 (read-sv2))
(defvar *sv3* 1)
(check "special nested" 3 (let ((*sv3* 2)) (let ((*sv3* 3)) *sv3*)))
(check "special nested restored" 1 *sv3*)
(defvar *sv4* 1)
(check "special setq" 20 (let ((*sv4* 10)) (setq *sv4* 20) *sv4*))
(check "special setq restored" 1 *sv4*)
(defvar *sv5* 1)
(catch 'done (unwind-protect (let ((*sv5* 99)) (throw 'done *sv5*)) nil))
(check "special uwp restored" 1 *sv5*)
(defvar *sv6* 10)
(check "special mixed let" 23 (let ((x 1) (*sv6* 20) (y 2)) (+ x *sv6* y)))
(check "special mixed restored" 10 *sv6*)
(check "boundp bound" t (boundp '*sv6*))
(check "boundp unbound" nil (boundp (gensym)))

; --- Mutation / setf ---
(check "rplaca" '(10 . 2) (let ((x (cons 1 2))) (rplaca x 10)))
(check "rplacd" '(1 . 20) (let ((x (cons 1 2))) (rplacd x 20)))
(check "setf car" 10 (let ((x (list 1 2 3))) (setf (car x) 10) (car x)))
(check "setf cdr" '(20 30) (let ((x (list 1 2 3))) (setf (cdr x) (list 20 30)) (cdr x)))
(check "setf first" 10 (let ((x (list 1 2 3))) (setf (first x) 10) (first x)))
(check "setf rest" '(20 30) (let ((x (list 1 2 3))) (setf (rest x) (list 20 30)) (rest x)))
(check "setf nth 0" 10 (let ((x (list 1 2 3))) (setf (nth 0 x) 10) (nth 0 x)))
(check "setf nth 1" 20 (let ((x (list 1 2 3))) (setf (nth 1 x) 20) (nth 1 x)))
(check "setf variable" 42 (let ((x 1)) (setf x 42) x))
(check "setf multiple" 2 (let ((a 0) (b 0)) (setf a 1 b 2)))
(check "setf returns val" 99 (let ((x (list 1 2))) (setf (car x) 99)))
(check "make-array" t (vectorp (make-array 3)))
(check "vectorp nil" nil (vectorp 42))
(check "aref/setf aref" 60 (let ((v (make-array 3))) (setf (aref v 0) 10) (setf (aref v 1) 20) (setf (aref v 2) 30) (+ (aref v 0) (aref v 1) (aref v 2))))
(check "svref/setf svref" 12 (let ((v (make-array 2))) (setf (svref v 0) 5) (setf (svref v 1) 7) (+ (svref v 0) (svref v 1))))
(check "vector" "#(1 2 3)" (write-to-string (vector 1 2 3)))
(check "vector empty" "#()" (write-to-string (vector)))
(check "vector aref" 20 (aref (vector 10 20 30) 1))
(check "array-dimensions" '(5) (array-dimensions (make-array 5)))
(check "array-rank" 1 (array-rank (make-array 5)))
(defvar *sv-t1* 42)
(check "symbol-value" 42 (symbol-value '*sv-t1*))
(defvar *sv-t2* 10)
(check "setf symbol-value" 99 (setf (symbol-value '*sv-t2*) 99))
(check "symbol-value after setf" 99 *sv-t2*)
(defvar *set-t* 10)
(check "set" 77 (set '*set-t* 77))
(check "set result" 77 *set-t*)
(check "push" '(1 2 3) (let ((x (list 2 3))) (push 1 x) x))
(check "pop" 1 (let ((x (list 1 2 3))) (pop x)))
(check "pop side effect" '(2 3) (let ((x (list 1 2 3))) (pop x) x))
(check "incf" 11 (let ((x 10)) (incf x)))
(check "incf delta" 15 (let ((x 10)) (incf x 5)))
(check "decf" 9 (let ((x 10)) (decf x)))
(check "decf delta" 5 (let ((x 10)) (decf x 5)))

; --- member & pushnew ---
(check "member found" '(2 3) (member 2 '(1 2 3)))
(check "member not found" nil (member 4 '(1 2 3)))
(check "member with test" '("a" "b") (member "a" (list "a" "b") :test #'equal))
(check "pushnew new item" '(3 1 2) (let ((x '(1 2))) (pushnew 3 x) x))
(check "pushnew existing" '(1 2) (let ((x '(1 2))) (pushnew 1 x) x))
(check "pushnew with test" '("a" "b") (let ((x (list "a" "b"))) (pushnew "a" x :test #'equal) x))

; --- eval-when ---
(check "eval-when execute" 3 (eval-when (:execute) (+ 1 2)))
(check "eval-when multiple" 30 (eval-when (:compile-toplevel :load-toplevel :execute) (+ 10 20)))
(check "eval-when body" 3 (eval-when (:execute) 1 2 3))

; --- destructuring-bind ---
(check "d-bind simple" '(1 2 3) (destructuring-bind (a b c) '(1 2 3) (list a b c)))
(check "d-bind nested" '(1 2 3 (4 5)) (destructuring-bind (a (b c) &rest d) '(1 (2 3) 4 5) (list a b c d)))
(check "d-bind &rest" '(1 (2 3)) (destructuring-bind (a &rest b) '(1 2 3) (list a b)))
(check "d-bind &optional default" '(1 10) (destructuring-bind (a &optional (b 10)) '(1) (list a b)))
(check "d-bind &optional provided" '(1 2) (destructuring-bind (a &optional (b 10)) '(1 2) (list a b)))
(check "d-bind &body" '(1 (2 3)) (destructuring-bind (a &body b) '(1 2 3) (list a b)))

; --- defsetf ---
(defun my-get-t (vec i) (aref vec i))
(defun my-put-t (vec i val) (setf (aref vec i) val) val)
(defsetf my-get-t my-put-t)
(check "defsetf short" 42 (let ((v (make-array 3))) (setf (my-get-t v 0) 42) (my-get-t v 0)))
(defun set-cadr-t (l v) (rplaca (cdr l) v) v)
(defsetf cadr set-cadr-t)
(check "defsetf cadr" '(1 99 3) (let ((x (list 1 2 3))) (setf (cadr x) 99) x))

; --- defun implicit block ---
(defun find-test (item list) (do ((l list (cdr l))) ((null l) nil) (when (eql item (car l)) (return-from find-test l))))
(check "defun return-from found" '(2 3) (find-test 2 '(1 2 3)))
(check "defun return-from nil" nil (find-test 4 '(1 2 3)))

; --- Phase 5 Tier 1: Character functions ---
(check "characterp char" t (characterp #\A))
(check "characterp non-char" nil (characterp 65))
(check "char= same" t (char= #\A #\A))
(check "char= diff" nil (char= #\A #\B))
(check "char/= diff" t (char/= #\A #\B))
(check "char/= same" nil (char/= #\A #\A))
(check "char< true" t (char< #\A #\B))
(check "char< false" nil (char< #\B #\A))
(check "char> true" t (char> #\B #\A))
(check "char> false" nil (char> #\A #\B))
(check "char<= equal" t (char<= #\A #\A))
(check "char<= less" t (char<= #\A #\B))
(check "char>= equal" t (char>= #\B #\B))
(check "char>= greater" t (char>= #\B #\A))
(check "char-code A" 65 (char-code #\A))
(check "char-code a" 97 (char-code #\a))
(check "code-char 65" #\A (code-char 65))
(check "char-code roundtrip" 97 (char-code (code-char 97)))
(check "char-upcase a" #\A (char-upcase #\a))
(check "char-upcase A" #\A (char-upcase #\A))
(check "char-downcase A" #\a (char-downcase #\A))
(check "char-downcase a" #\a (char-downcase #\a))
(check "upper-case-p A" t (upper-case-p #\A))
(check "upper-case-p a" nil (upper-case-p #\a))
(check "lower-case-p a" t (lower-case-p #\a))
(check "lower-case-p A" nil (lower-case-p #\A))
(check "alpha-char-p A" t (alpha-char-p #\A))
(check "alpha-char-p 1" nil (alpha-char-p #\1))
(check "digit-char-p 5" t (digit-char-p #\5))
(check "digit-char-p A" nil (digit-char-p #\A))

; --- Phase 5 Tier 1: Symbol functions ---
(check "symbol-name foo" "FOO" (symbol-name 'foo))
(check "symbol-name nil" "NIL" (symbol-name nil))
(check "fboundp +" t (fboundp '+))
(check "fboundp gensym-result" nil (fboundp (gensym)))
(check "fdefinition +" t (functionp (fdefinition '+)))
(check "make-symbol" t (symbolp (make-symbol "TEST")))
(check "make-symbol name" "HELLO" (symbol-name (make-symbol "HELLO")))
(check "keywordp keyword" t (keywordp :foo))
(check "keywordp symbol" nil (keywordp 'foo))
(check "keywordp number" nil (keywordp 42))

; --- Phase 5 Tier 1: Fixed builtins ---
(check "length vector" 5 (length (make-array 5)))
(check "length vector 0" 0 (length (make-array 0)))
(check "mapcar multi-list" '(11 22 33) (mapcar #'+ '(1 2 3) '(10 20 30)))
(check "mapcar multi-list fn" '((a 1) (b 2)) (mapcar #'list '(a b) '(1 2)))
(check "mapcar multi uneven" '(11 22) (mapcar #'+ '(1 2 3) '(10 20)))

; --- Phase 5 Tier 2: String functions ---
(check "string= same" t (string= "hello" "hello"))
(check "string= diff" nil (string= "hello" "HELLO"))
(check "string-equal ci" t (string-equal "hello" "HELLO"))
(check "string-equal diff" nil (string-equal "hello" "world"))
(check "string< true" t (string< "abc" "abd"))
(check "string< false" nil (string< "abd" "abc"))
(check "string> true" t (string> "abd" "abc"))
(check "string<= equal" t (string<= "abc" "abc"))
(check "string>= equal" t (string>= "abc" "abc"))
(check "string< shorter" t (string< "ab" "abc"))
(check "string-upcase" "HELLO" (string-upcase "hello"))
(check "string-downcase" "hello" (string-downcase "HELLO"))
(check "string-trim" "hello" (string-trim " " "  hello  "))
(check "string-left-trim" "hello  " (string-left-trim " " "  hello  "))
(check "string-right-trim" "  hello" (string-right-trim " " "  hello  "))
(check "subseq string" "el" (subseq "hello" 1 3))
(check "subseq string end" "llo" (subseq "hello" 2))
(check "subseq list" '(b c) (subseq '(a b c d) 1 3))
(check "concatenate" "hello world" (concatenate 'string "hello" " " "world"))
(check "char access" #\h (char "hello" 0))
(check "schar access" #\b (schar "abc" 1))
(check "string from sym" "FOO" (string 'foo))
(check "string from str" "hello" (string "hello"))
(check "string from char" "A" (string #\A))
(check "parse-integer" 42 (parse-integer "42"))
(check "parse-integer neg" -7 (parse-integer "-7"))
(check "parse-integer hex" 255 (parse-integer "FF" :radix 16))
(check "parse-integer ws" 123 (parse-integer "  123  "))
(check "write-to-string" "42" (write-to-string 42))
(check "princ-to-string" "hello" (princ-to-string "hello"))
(check "princ-to-string num" "42" (princ-to-string 42))

; --- Phase 5 Tier 3: List utilities ---
(check "nthcdr 0" '(1 2 3) (nthcdr 0 '(1 2 3)))
(check "nthcdr 2" '(3) (nthcdr 2 '(1 2 3)))
(check "nthcdr past end" nil (nthcdr 5 '(1 2 3)))
(check "last" '(3) (last '(1 2 3)))
(check "last 2" '(2 3) (last '(1 2 3) 2))
(check "last nil" nil (last nil))
(check "acons" '((a . 1)) (acons 'a 1 nil))
(check "copy-list" '(1 2 3) (copy-list '(1 2 3)))
(check "copy-list nil" nil (copy-list nil))
(check "pairlis" '((a . 1) (b . 2)) (pairlis '(a b) '(1 2)))
(check "pairlis with alist" '((a . 1) (b . 2) (c . 3)) (pairlis '(a b) '(1 2) '((c . 3))))
(check "assoc found" '(b . 2) (assoc 'b '((a . 1) (b . 2) (c . 3))))
(check "assoc not found" nil (assoc 'd '((a . 1) (b . 2))))
(check "assoc with test" '("b" . 2) (assoc "b" (list (cons "a" 1) (cons "b" 2)) :test #'equal))
(check "rassoc found" '(b . 2) (rassoc 2 '((a . 1) (b . 2) (c . 3))))
(check "rassoc not found" nil (rassoc 9 '((a . 1) (b . 2))))
(check "getf found" 2 (getf '(:a 1 :b 2) :b))
(check "getf not found" nil (getf '(:a 1) :z))
(check "getf default" 99 (getf '(:a 1) :z 99))
(check "subst" '(a x (c x)) (subst 'x 'b '(a b (c b))))
(check "sublis" '(1 2 c) (sublis '((a . 1) (b . 2)) '(a b c)))
(check "adjoin new" '(1 2 3) (adjoin 1 '(2 3)))
(check "adjoin existing" '(1 2 3) (adjoin 2 '(1 2 3)))
(check "nconc" '(1 2 3 4) (nconc (list 1 2) (list 3 4)))
(check "nconc nil" '(1 2) (nconc nil (list 1 2)))
(check "nreverse" '(3 2 1) (nreverse (list 1 2 3)))
(check "nreverse nil" nil (nreverse nil))
(check "delete" '(1 3 4) (delete 2 (list 1 2 3 2 4)))
(check "delete not found" '(1 2 3) (delete 5 (list 1 2 3)))
(check "delete-if" '(1 2 3) (delete-if #'zerop (list 0 1 0 2 0 3)))
(check "nsubst" '(a x (c x)) (let ((x (list 'a 'b (list 'c 'b)))) (nsubst 'x 'b x)))
(check "butlast" '(1 2) (butlast '(1 2 3)))
(check "butlast 2" '(1) (butlast '(1 2 3) 2))
(check "butlast all" nil (butlast '(1 2 3) 5))
(check "copy-tree" '(1 (2 3) 4) (copy-tree '(1 (2 3) 4)))
(check "mapc returns list" '(1 2 3) (mapc (lambda (x) x) '(1 2 3)))
(check "mapcan" '(1 2 3) (mapcan (lambda (x) (if (numberp x) (list x) nil)) '(a 1 b 2 c 3)))
(check "maplist" '(4 3 2 1) (maplist #'length '(1 2 3 4)))
(check "mapl returns list" '(a b c) (mapl (lambda (l) l) '(a b c)))
(check "mapcon" '(3 2 1) (mapcon (lambda (l) (list (length l))) '(a b c)))
(check "intersection" '(2 4) (intersection '(1 2 3 4) '(2 4 6)))
(check "intersection empty" nil (intersection '(1 2) '(3 4)))
(check "union" '(3 2 1 4) (union '(1 2 3) '(2 3 4)))
(check "set-difference" '(1 3) (set-difference '(1 2 3 4) '(2 4)))
(check "set-difference empty" nil (set-difference '(1 2) '(1 2)))
(check "subsetp true" t (subsetp '(1 2) '(1 2 3)))
(check "subsetp false" nil (subsetp '(1 4) '(1 2 3)))
(check "subsetp empty" t (subsetp nil '(1 2 3)))

; --- Hash tables ---
(check "make-hash-table" t (hash-table-p (make-hash-table)))
(check "hash-table-p nil" nil (hash-table-p nil))
(check "hash-table-p number" nil (hash-table-p 42))
(check "type-of hash-table" 'hash-table (type-of (make-hash-table)))
(defvar *ht-test* (make-hash-table))
(setf (gethash 'a *ht-test*) 1)
(setf (gethash 'b *ht-test*) 2)
(check "gethash found" 1 (gethash 'a *ht-test*))
(check "gethash found 2" 2 (gethash 'b *ht-test*))
(check "hash-table-count" 2 (hash-table-count *ht-test*))
(check "gethash not found" nil (gethash 'z *ht-test*))
(check "gethash default" 99 (gethash 'z *ht-test* 99))
(setf (gethash 'a *ht-test*) 10)
(check "gethash overwrite" 10 (gethash 'a *ht-test*))
(check "hash-table-count after overwrite" 2 (hash-table-count *ht-test*))
(check "remhash found" t (remhash 'a *ht-test*))
(check "gethash after remhash" nil (gethash 'a *ht-test*))
(check "hash-table-count after remhash" 1 (hash-table-count *ht-test*))
(check "remhash not found" nil (remhash 'z *ht-test*))
(clrhash *ht-test*)
(check "hash-table-count after clrhash" 0 (hash-table-count *ht-test*))
(defvar *ht-sum* 0)
(defvar *ht-map* (make-hash-table))
(setf (gethash 'x *ht-map*) 10)
(setf (gethash 'y *ht-map*) 20)
(maphash (lambda (k v) (setq *ht-sum* (+ *ht-sum* v))) *ht-map*)
(check "maphash sum" 30 *ht-sum*)
(defvar *ht-eq* (make-hash-table :test 'eq))
(setf (gethash 'foo *ht-eq*) 99)
(check "hash-table eq test" 99 (gethash 'foo *ht-eq*))
(defvar *ht-equal* (make-hash-table :test 'equal))
(setf (gethash "hello" *ht-equal*) 42)
(check "hash-table equal test string" 42 (gethash "hello" *ht-equal*))
(check "gethash mv present" t (multiple-value-bind (v p) (gethash 'x *ht-map*) p))
(check "gethash mv absent" nil (multiple-value-bind (v p) (gethash 'missing *ht-map*) p))

; --- Sequence functions ---
(defun my-evenp (x) (= (mod x 2) 0))

; find
(check "find basic" 3 (find 3 '(1 2 3 4 5)))
(check "find not found" nil (find 9 '(1 2 3)))
(check "find with start" 2 (find 2 '(1 2 3 2 1) :start 2))
(check "find with test" "b" (find "b" '("a" "b" "c") :test #'equal))
(check "find from-end" 3 (find 3 '(1 3 2 3 4) :from-end t))

; find-if, find-if-not
(check "find-if" 2 (find-if #'my-evenp '(1 2 3 4)))
(check "find-if not found" nil (find-if #'my-evenp '(1 3 5)))
(check "find-if-not" 5 (find-if-not #'my-evenp '(2 4 5 6)))

; position
(check "position basic" 2 (position 3 '(1 2 3 4)))
(check "position not found" nil (position 9 '(1 2 3)))
(check "position from-end" 2 (position 1 '(1 2 1 2) :from-end t))

; position-if, position-if-not
(check "position-if" 2 (position-if #'my-evenp '(1 3 4 5)))
(check "position-if-not" 2 (position-if-not #'my-evenp '(2 4 5)))

; count
(check "count basic" 3 (count 1 '(1 2 1 3 1)))
(check "count none" 0 (count 9 '(1 2 3)))
(check "count with start" 2 (count 1 '(1 2 1 3 1) :start 1))

; count-if, count-if-not
(check "count-if" 3 (count-if #'my-evenp '(1 2 3 4 5 6)))
(check "count-if-not" 3 (count-if-not #'my-evenp '(1 2 3 4 5)))

; remove
(check "remove basic" '(1 2 4 5) (remove 3 '(1 2 3 4 3 5)))
(check "remove count" '(1 2 4 3 5) (remove 3 '(1 2 3 4 3 5) :count 1))
(check "remove not found" '(1 2 3) (remove 9 '(1 2 3)))
(check "remove from-end count" '(1 2 3 4 5) (remove 3 '(1 2 3 4 3 5) :count 1 :from-end t))

; remove-if, remove-if-not
(check "remove-if" '(1 3 5) (remove-if #'my-evenp '(1 2 3 4 5)))
(check "remove-if-not" '(2 4) (remove-if-not #'my-evenp '(1 2 3 4 5)))

; remove-duplicates
(check "remove-duplicates" '(1 3 2 4) (remove-duplicates '(1 2 1 3 2 4)))
(check "remove-dup symbols" '(b a c) (remove-duplicates '(a b a c)))

; substitute
(check "substitute basic" '(1 2 99 4 99) (substitute 99 3 '(1 2 3 4 3)))
(check "substitute count" '(1 2 0 4 3) (substitute 0 3 '(1 2 3 4 3) :count 1))

; substitute-if, substitute-if-not
(check "substitute-if" '(1 0 3 0 5) (substitute-if 0 #'my-evenp '(1 2 3 4 5)))
(check "substitute-if-not" '(0 2 0 4 0) (substitute-if-not 0 #'my-evenp '(1 2 3 4 5)))

; reduce
(check "reduce sum" 10 (reduce #'+ '(1 2 3 4)))
(check "reduce empty init" 0 (reduce #'+ '() :initial-value 0))
(check "reduce single init" 15 (reduce #'+ '(5) :initial-value 10))
(check "reduce cons" '((1 . 2) . 3) (reduce #'cons '(1 2 3)))

; fill
(defvar *fill-list* (list 1 2 3 4))
(fill *fill-list* 0)
(check "fill all" '(0 0 0 0) *fill-list*)
(defvar *fill-list2* (list 1 2 3 4))
(fill *fill-list2* 0 :start 1 :end 3)
(check "fill range" '(1 0 0 4) *fill-list2*)

; replace
(defvar *repl-list* (list 1 2 3 4 5))
(replace *repl-list* '(a b c) :start1 1)
(check "replace" '(1 a b c 5) *repl-list*)

; every, some, notany, notevery
(check "every true" t (every #'my-evenp '(2 4 6)))
(check "every false" nil (every #'my-evenp '(2 3 6)))
(check "every empty" t (every #'my-evenp '()))
(check "some true" t (some #'my-evenp '(1 3 4)))
(check "some false" nil (some #'my-evenp '(1 3 5)))
(check "notany true" t (notany #'my-evenp '(1 3 5)))
(check "notany false" nil (notany #'my-evenp '(1 2 3)))
(check "notevery true" t (notevery #'my-evenp '(2 4 5)))
(check "notevery false" nil (notevery #'my-evenp '(2 4 6)))

; map
(check "map list" '(2 3 4) (map 'list #'1+ '(1 2 3)))

; mismatch
(check "mismatch none" nil (mismatch '(1 2 3) '(1 2 3)))
(check "mismatch at 2" 2 (mismatch '(1 2 3) '(1 2 4)))
(check "mismatch length" 2 (mismatch '(1 2) '(1 2 3)))

; search
(check "search found" 1 (search '(2 3) '(1 2 3 4)))
(check "search not found" nil (search '(9) '(1 2 3)))
(check "search empty" 0 (search '() '(1 2 3)))

; sort
(check "sort asc" '(1 1 3 4 5) (sort (list 3 1 4 1 5) #'<))
(check "sort desc" '(5 4 3 2 1) (sort (list 1 2 3 4 5) #'>))
(check "sort single" '(1) (sort (list 1) #'<))
(check "sort empty" nil (sort '() #'<))
(check "sort with key" '(1 -2 -3 4) (sort (list -3 1 -2 4) #'< :key #'abs))

; stable-sort
(check "stable-sort" '(1 2 3) (stable-sort (list 3 1 2) #'<))

; find/position with :key
(check "find with key" '(2 b) (find 2 '((1 a) (2 b) (3 c)) :key #'car))
(check "position with key" 1 (position 2 '((1 a) (2 b) (3 c)) :key #'car))
(check "count with key" 2 (count 1 '((1 a) (2 b) (1 c)) :key #'car))

; remove with :key
(check "remove with key" '((2 b) (3 c)) (remove 1 '((1 a) (2 b) (3 c)) :key #'car))

; every/some with multiple lists
(check "every 2 lists" t (every #'< '(1 2 3) '(2 3 4)))
(check "some 2 lists" t (some #'< '(3 2 1) '(1 2 3)))

; reduce with key
(check "reduce with key" 6 (reduce #'+ '((1 a) (2 b) (3 c)) :key #'car))

; --- Type system: typep ---
(check "typep integer" t (typep 42 'integer))
(check "typep fixnum" t (typep 42 'fixnum))
(check "typep number" t (typep 42 'number))
(check "typep not string" nil (typep 42 'string))
(check "typep string" t (typep "hello" 'string))
(check "typep character" t (typep #\A 'character))
(check "typep null" t (typep nil 'null))
(check "typep nil symbol" t (typep nil 'symbol))
(check "typep nil list" t (typep nil 'list))
(check "typep cons" t (typep '(1 2) 'cons))
(check "typep list" t (typep '(1 2) 'list))
(check "typep atom int" t (typep 42 'atom))
(check "typep atom cons" nil (typep '(1) 'atom))
(check "typep keyword" t (typep :foo 'keyword))
(check "typep not keyword" nil (typep 'bar 'keyword))
(check "typep function" t (typep #'+ 'function))
(check "typep hash-table" t (typep (make-hash-table) 'hash-table))
(check "typep t" t (typep 42 't))
(check "typep nil-type" nil (typep 42 'nil))
(check "typep sequence list" t (typep '(1 2) 'sequence))
(check "typep sequence vec" t (typep (vector 1 2) 'sequence))
(check "typep vector" t (typep (vector 1 2) 'vector))
(check "typep compiled-function" t (typep (lambda (x) x) 'compiled-function))

; --- Type system: coerce ---
(check "coerce t" 42 (coerce 42 't))
(check "coerce int->char" #\A (coerce 65 'character))
(check "coerce char->int" 65 (coerce #\A 'integer))
(check "coerce char identity" #\A (coerce #\A 'character))
(check "coerce int identity" 42 (coerce 42 'integer))
(check "coerce sym->string" "FOO" (coerce 'foo 'string))
(check "coerce char->string" "A" (coerce #\A 'string))
(check "coerce string identity" "hello" (coerce "hello" 'string))
(check "coerce list->vector" t (equal (coerce '(1 2 3) 'vector) (vector 1 2 3)))
(check "coerce vector->list" '(1 2 3) (coerce (vector 1 2 3) 'list))
(check "coerce nil->list" nil (coerce nil 'list))
(check "coerce nil->vector" t (equal (coerce nil 'vector) (vector)))

; --- Type system: compound typep ---
(check "typep or match int" t (typep 42 '(or integer string)))
(check "typep or match str" t (typep "hi" '(or integer string)))
(check "typep or no match" nil (typep #\A '(or integer string)))
(check "typep and match" t (typep nil '(and symbol list)))
(check "typep and no match" nil (typep 42 '(and number string)))
(check "typep not match" t (typep 42 '(not string)))
(check "typep not no match" nil (typep "hi" '(not string)))
(check "typep member match" t (typep 1 '(member 1 2 3)))
(check "typep member no match" nil (typep 4 '(member 1 2 3)))
(check "typep eql match" t (typep 42 '(eql 42)))
(check "typep eql no match" nil (typep 43 '(eql 42)))
(check "typep satisfies match" t (typep 42 '(satisfies numberp)))
(check "typep satisfies no" nil (typep "hi" '(satisfies numberp)))
(check "typep nested compound" t (typep 42 '(or (and integer atom) string)))

; --- Type system: deftype ---
(deftype my-num () 'number)
(check "deftype basic match" t (typep 42 'my-num))
(check "deftype basic no" nil (typep "hi" 'my-num))
(deftype str-or-num () '(or string number))
(check "deftype compound match int" t (typep 42 'str-or-num))
(check "deftype compound match str" t (typep "hi" 'str-or-num))
(check "deftype compound no" nil (typep #\A 'str-or-num))

; --- Type system: subtypep ---
(check "subtypep fixnum<number" t (subtypep 'fixnum 'number))
(check "subtypep fixnum<integer" t (subtypep 'fixnum 'integer))
(check "subtypep integer<number" t (subtypep 'integer 'number))
(check "subtypep cons<list" t (subtypep 'cons 'list))
(check "subtypep null<list" t (subtypep 'null 'list))
(check "subtypep list<sequence" t (subtypep 'list 'sequence))
(check "subtypep keyword<symbol" t (subtypep 'keyword 'symbol))
(check "subtypep compiled-fn<fn" t (subtypep 'compiled-function 'function))
(check "subtypep same type" t (subtypep 'integer 'integer))
(check "subtypep not subtype" nil (subtypep 'string 'number))
(check "subtypep nil<anything" t (subtypep 'nil 'number))
(check "subtypep anything<t" t (subtypep 'number 't))
(check "subtypep mv second val" '(t t) (multiple-value-list (subtypep 'fixnum 'number)))
(check "subtypep mv not sub" '(nil t) (multiple-value-list (subtypep 'string 'number)))

; --- Type system: typecase with compound ---
(check "typecase or int" "match" (typecase 42 ((or integer string) "match") (t "no")))
(check "typecase or str" "match" (typecase "hi" ((or integer string) "match") (t "no")))
(check "typecase or no" "no" (typecase #\A ((or integer string) "match") (t "no")))
(check "typecase list nil" "list" (typecase nil (list "list") (t "other")))

; --- Disassemble ---
(defun disasm-test-fn (x) (+ x 1))
(check "disassemble defun" nil (disassemble 'disasm-test-fn))
(check "disassemble closure" nil (disassemble (lambda (x) (+ x 1))))
(check "disassemble builtin" nil (disassemble 'cons))

; --- Declarations: declare, declaim, proclaim, locally ---
(declaim (special *dcl-x*))
(setq *dcl-x* 10)
(defun get-dcl-x () *dcl-x*)
(check "declaim special" 99 (let ((*dcl-x* 99)) (get-dcl-x)))

(setq *dcl-ds* 10)
(defun get-dcl-ds () *dcl-ds*)
(check "declare special let" 42 (let ((*dcl-ds* 42)) (declare (special *dcl-ds*)) (get-dcl-ds)))

(check "declare ignore" 42 (let ((x 1)) (declare (ignore x)) 42))
(check "declare type" 1 (let ((x 1)) (declare (type fixnum x)) x))
(check "declaim optimize" nil (declaim (optimize (speed 3))))
(check "declaim inline" nil (declaim (inline cons)))

(proclaim '(special *dcl-p*))
(setq *dcl-p* 100)
(defun get-dcl-p () *dcl-p*)
(check "proclaim special" 200 (let ((*dcl-p* 200)) (get-dcl-p)))

(check "locally basic" 3 (locally 1 2 3))
(check "locally declare" 5 (locally (declare (special *dcl-lv*)) (setq *dcl-lv* 5) *dcl-lv*))

(check "declare in lambda" 42 ((lambda (x) (declare (ignore x)) 42) 99))

(declaim (special *dcl-m1*) (special *dcl-m2*))
(setq *dcl-m1* 1)
(setq *dcl-m2* 2)
(defun get-dcl-m1 () *dcl-m1*)
(defun get-dcl-m2 () *dcl-m2*)
(check "declaim multi 1" 10 (let ((*dcl-m1* 10)) (get-dcl-m1)))
(check "declaim multi 2" 20 (let ((*dcl-m2* 20)) (get-dcl-m2)))

; --- Trace/Untrace ---
(defun tr-fact (n) (if (<= n 1) 1 (* n (tr-fact (1- n)))))
(trace tr-fact)
(check "trace result" 120 (tr-fact 5))
(untrace tr-fact)
(check "untrace result" 6 (tr-fact 3))
(defun tr-bar () 42)
(check "trace returns list" '(tr-bar) (trace tr-bar))
(check "trace query" '(tr-bar) (trace))
(check "untrace all" nil (untrace))
(check "trace query empty" nil (trace))
(trace cons)
(check "trace builtin" '(1 . 2) (cons 1 2))
(untrace cons)
(defun tr-p2 (x) (* x x))
(defun tr-q2 (x) (+ x 1))
(trace tr-p2 tr-q2)
(check "trace multiple" 16 (tr-p2 (tr-q2 3)))
(untrace)

; --- Backtrace (error recovery) ---
; Test that errors in nested calls don't break subsequent evaluation
(defun bt-err-inner () (error "test error"))
(defun bt-err-outer () (+ 1 (bt-err-inner)))
(bt-err-outer)
(check "backtrace recovery" 42 42)
(defun bt-rec-err (n) (if (= n 0) (error "depth") (+ 1 (bt-rec-err (1- n)))))
(bt-rec-err 5)
(check "backtrace deep recovery" 99 99)
(defun bt-uwp-fn () (unwind-protect (error "uwp err") nil))
(bt-uwp-fn)
(check "backtrace uwp recovery" 7 (+ 3 4))

; --- Time ---
(check "time returns value" 3 (time (+ 1 2)))
(check "time nested" 22 (+ 10 (time (* 3 4))))
(defun time-sq (x) (* x x))
(check "time defun" 25 (time (time-sq 5)))
(check "get-internal-real-time" t (integerp (get-internal-real-time)))

; --- Source location tracking ---
; Reader line tracking is implicitly tested by batch mode itself
; The fact that errors show proper backtraces validates source locations
(defun srcloc-inner (x) (/ x 0))
(defun srcloc-outer (x) (let ((r (srcloc-inner x))) r))
(srcloc-outer 1)
(check "srcloc error recovery" 42 42)

; --- Condition signaling ---
(check "signal returns nil" nil (signal (make-condition 'simple-condition :format-control "test")))
(check "signal string returns nil" nil (signal "something"))
(check "signal symbol returns nil" nil (signal 'simple-condition))
(check "warn returns nil" nil (warn "test warning"))
(check "warn symbol returns nil" nil (warn 'simple-warning))
; error test: error is caught by batch REPL, next expression returns 42
(error "test error for batch")
(check "error recovery" 42 42)

; --- handler-bind ---
(check "handler-bind basic" 'simple-condition (catch 'test-tag (handler-bind ((simple-condition (lambda (c) (throw 'test-tag (condition-type-name c))))) (signal (make-condition 'simple-condition :format-control "test")))))
(check "handler-bind error match" 42 (catch 'test-tag (handler-bind ((error (lambda (c) (throw 'test-tag 42)))) (signal (make-condition 'simple-error :format-control "boom")))))
(check "handler-bind no match" :untouched (catch 'test-tag (handler-bind ((type-error (lambda (c) (throw 'test-tag :touched)))) (signal (make-condition 'simple-warning :format-control "w")) :untouched)))
(check "handler-bind body value" 3 (handler-bind ((error (lambda (c) nil))) (+ 1 2)))

; --- handler-case ---
(check "handler-case catches error" 42 (handler-case (error "boom") (error (c) 42)))
(check "handler-case no error" 3 (handler-case (+ 1 2) (error (c) 99)))
(check "handler-case type dispatch" 42 (handler-case (error 'type-error :datum 42 :expected-type 'string) (type-error (c) (type-error-datum c)) (error (c) :generic)))

; --- ignore-errors ---
(check "ignore-errors catches" nil (ignore-errors (error "boom")))
(check "ignore-errors no error" 30 (ignore-errors (+ 10 20)))

; --- restart-case ---
(check "restart-case basic" 42 (restart-case (invoke-restart 'use-value 42) (use-value (v) v)))
(check "restart-case normal exit" 3 (restart-case (+ 1 2) (abort () 99)))
(check "restart-case no params" 42 (restart-case (invoke-restart 'abort) (abort () 42)))
(check "restart-case multiple" :continued (restart-case (invoke-restart 'continue) (abort () :aborted) (continue () :continued)))
(check "find-restart found" :found (restart-case (if (find-restart 'continue) :found :not-found) (continue () nil)))
(check "find-restart missing" nil (find-restart 'continue))
(check "compute-restarts count" 2 (restart-case (length (compute-restarts)) (abort () nil) (continue () nil)))
(check "restart with handler" 99 (handler-bind ((error (lambda (c) (invoke-restart 'continue)))) (restart-case (error "boom") (continue () 99))))
(check "with-simple-restart invoke" nil (with-simple-restart (abort "Abort") (invoke-restart 'abort)))
(check "with-simple-restart normal" 42 (with-simple-restart (abort "Abort") 42))
(check "cerror continue" :after (handler-bind ((error (lambda (c) (invoke-restart 'continue)))) (cerror "Continue" "bad") :after))

; --- define-condition ---
(check "define-condition basic" 'my-error (define-condition my-error (error) ()))
(check "define-condition conditionp" t (conditionp (make-condition 'my-error)))
(define-condition file-err (error) ((pathname :initarg :pathname :reader file-err-pathname)))
(check "define-condition reader" "/tmp" (file-err-pathname (make-condition 'file-err :pathname "/tmp")))
(check "define-condition typep parent" t (typep (make-condition 'my-error) 'error))
(check "define-condition typep condition" t (typep (make-condition 'my-error) 'condition))
(check "define-condition typep not warning" nil (typep (make-condition 'my-error) 'warning))
(define-condition app-err (error) ())
(check "define-condition handler-case" :caught (handler-case (error 'app-err) (app-err (c) :caught)))

; --- check-type ---
(check "check-type pass" :ok (let ((x 42)) (check-type x integer) :ok))
(check "check-type fail" "hello" (handler-case (let ((x "hello")) (check-type x integer)) (type-error (c) (type-error-datum c))))

; --- assert ---
(check "assert pass" :ok (progn (assert (= 1 1)) :ok))
(check "assert fail" :failed (handler-case (assert (= 1 2)) (simple-error (c) :failed)))

; --- Package System ---
(check "find-package CL" t (not (null (find-package "COMMON-LISP"))))
(check "find-package nickname CL" t (not (null (find-package "CL"))))
(check "find-package CL-USER" t (not (null (find-package "CL-USER"))))
(check "find-package KEYWORD" t (not (null (find-package "KEYWORD"))))
(check "find-package nil" nil (find-package "NONEXISTENT"))
(check "package-name CL" "COMMON-LISP" (package-name (find-package "CL")))
(check "package-nicknames CL" "CL" (car (package-nicknames (find-package "CL"))))
(check "package-use-list CL-USER" 1 (length (package-use-list (find-package "CL-USER"))))
(check "list-all-packages" t (>= (length (list-all-packages)) 3))
(check "make-package" t (not (null (make-package "TEST-PKG-1"))))
(check "find-package after make" t (not (null (find-package "TEST-PKG-1"))))
(check "find-symbol CAR external" :external (multiple-value-bind (sym status) (find-symbol "CAR" (find-package "CL")) status))
(check "find-symbol CAR inherited" :inherited (multiple-value-bind (sym status) (find-symbol "CAR" (find-package "CL-USER")) status))
(check "find-symbol not found" nil (multiple-value-bind (sym status) (find-symbol "ZZZZZ" (find-package "CL")) status))
(check "intern new" nil (multiple-value-bind (sym status) (intern "NEW-AMIGA-SYM") status))
(check "intern existing" :inherited (multiple-value-bind (sym status) (intern "CAR") status))
(make-package "EXP-AMI")
(intern "EXP-SYM" (find-package "EXP-AMI"))
(check "export" :external (progn (export (find-symbol "EXP-SYM" (find-package "EXP-AMI")) (find-package "EXP-AMI")) (multiple-value-bind (s st) (find-symbol "EXP-SYM" (find-package "EXP-AMI")) st)))
(check "unexport" :internal (progn (unexport (find-symbol "EXP-SYM" (find-package "EXP-AMI")) (find-package "EXP-AMI")) (multiple-value-bind (s st) (find-symbol "EXP-SYM" (find-package "EXP-AMI")) st)))
(make-package "USE-AMI-SRC")
(make-package "USE-AMI-DST")
(intern "SHARED-SYM" (find-package "USE-AMI-SRC"))
(export (find-symbol "SHARED-SYM" (find-package "USE-AMI-SRC")) (find-package "USE-AMI-SRC"))
(use-package "USE-AMI-SRC" "USE-AMI-DST")
(check "use-package inherit" :inherited (multiple-value-bind (s st) (find-symbol "SHARED-SYM" (find-package "USE-AMI-DST")) st))
(check "delete-package" nil (progn (make-package "DEL-AMI") (delete-package (find-package "DEL-AMI")) (find-package "DEL-AMI")))
(check "rename-package" "RENAMED-AMI" (progn (make-package "ORIG-AMI") (rename-package (find-package "ORIG-AMI") "RENAMED-AMI") (package-name (find-package "RENAMED-AMI"))))
(make-package "SHAD-AMI")
(use-package "CL" "SHAD-AMI")
(shadow "CAR" (find-package "SHAD-AMI"))
(check "shadow" :internal (multiple-value-bind (s st) (find-symbol "CAR" (find-package "SHAD-AMI")) st))
(make-package "UNINT-AMI")
(intern "REM-SYM" (find-package "UNINT-AMI"))
(unintern (find-symbol "REM-SYM" (find-package "UNINT-AMI")) (find-package "UNINT-AMI"))
(check "unintern" nil (multiple-value-bind (s st) (find-symbol "REM-SYM" (find-package "UNINT-AMI")) st))

; === Reader Qualified Syntax ===

; pkg:sym external access
(make-package "QRA-FOO")
(intern "QBAR" (find-package "QRA-FOO"))
(export (find-symbol "QBAR" (find-package "QRA-FOO")) (find-package "QRA-FOO"))
(check "pkg:sym external" t (eq (find-symbol "QBAR" (find-package "QRA-FOO")) 'QRA-FOO:QBAR))

; pkg::sym internal access
(intern "QSECRET" (find-package "QRA-FOO"))
(check "pkg::sym internal" t (eq (find-symbol "QSECRET" (find-package "QRA-FOO")) 'QRA-FOO::QSECRET))

; pkg::sym creates symbol
(make-package "QRA-BAR")
(check "pkg::sym creates" t (symbolp 'QRA-BAR::NEWSYM))
(check "pkg::sym status" :internal (multiple-value-bind (s st) (find-symbol "NEWSYM" (find-package "QRA-BAR")) st))

; CL:CAR = CAR
(check "cl:sym" t (eq 'CL:CAR 'CAR))

; KEYWORD:TEST = :TEST
(check "keyword:sym" t (eq 'KEYWORD:TEST :TEST))

; #:sym uninterned
(check "uninterned symbolp" t (symbolp '#:TEMP))
(check "uninterned no pkg" t (null (symbol-package '#:TEMP)))
(check "uninterned unique" nil (eq '#:X '#:X))

; Printer: uninterned
(check "print uninterned" "#:HELLO" (prin1-to-string '#:HELLO))

; Printer: keyword unchanged
(check "print keyword" ":FOO" (prin1-to-string :FOO))

; Printer: current pkg no prefix
(check "print current pkg" "CAR" (prin1-to-string 'CAR))

; Printer: other pkg external
(make-package "QRA-PR")
(intern "XSYM" (find-package "QRA-PR"))
(export (find-symbol "XSYM" (find-package "QRA-PR")) (find-package "QRA-PR"))
(check "print external" "QRA-PR:XSYM" (prin1-to-string 'QRA-PR:XSYM))

; Printer: other pkg internal
(make-package "QRA-PR2")
(intern "ISYM" (find-package "QRA-PR2"))
(check "print internal" "QRA-PR2::ISYM" (prin1-to-string 'QRA-PR2::ISYM))

; --- CDR-10: Package-local nicknames ---
(make-package "LN-AMI1" :use '("COMMON-LISP"))
(add-package-local-nickname "KW-AMI" (find-package "KEYWORD") (find-package "LN-AMI1"))
(check "pkg-local-nick add" 1 (length (package-local-nicknames (find-package "LN-AMI1"))))
(in-package "LN-AMI1")
(cl-user::check "pkg-local-nick resolve" t (not (null (find-package "KW-AMI"))))
(in-package "COMMON-LISP-USER")
(check "pkg-local-nick scoped" nil (find-package "KW-AMI"))
(make-package "LN-AMI2" :use '("COMMON-LISP"))
(add-package-local-nickname "RK" (find-package "KEYWORD") (find-package "LN-AMI2"))
(remove-package-local-nickname "RK" (find-package "LN-AMI2"))
(check "pkg-local-nick remove" 0 (length (package-local-nicknames (find-package "LN-AMI2"))))
(defpackage "LN-AMI3" (:use "COMMON-LISP") (:local-nicknames ("KD" "KEYWORD")))
(in-package "LN-AMI3")
(cl-user::check "defpackage local-nick" t (not (null (find-package "KD"))))
(cl-user::check "local-nick reader" t (eq 'KD:TEST :TEST))
(in-package "COMMON-LISP-USER")

; --- Macrolet ---
(check "macrolet basic" 25 (macrolet ((square (x) `(* ,x ,x))) (square 5)))
(check "macrolet nested" 15 (macrolet ((m (x) `(+ ,x 1))) (macrolet ((m (x) `(+ ,x 10))) (m 5))))
(check "macrolet body" 14 (macrolet ((double (x) `(* 2 ,x))) (double 3) (double 7)))

; --- Symbol-macrolet ---
(check "symbol-macrolet basic" 42 (symbol-macrolet ((x 42)) x))
(check "symbol-macrolet expr" 3 (symbol-macrolet ((x (+ 1 2))) x))
(check "symbol-macrolet setq" '(99 . 2) (let ((a (cons 1 2))) (symbol-macrolet ((x (car a))) (setq x 99) a)))
(check "symbol-macrolet nested" 20 (symbol-macrolet ((x 10)) (symbol-macrolet ((x 20)) x)))
(check "symbol-macrolet multi" 30 (symbol-macrolet ((x 10) (y 20)) (+ x y)))

; --- Debugger ---
(check "invoke-debugger exists" t (functionp #'invoke-debugger))
(check "*debugger-hook* initial" nil *debugger-hook*)
(check "break exists" t (functionp #'break))

; --- Multi-line expressions ---
(defun multiline-fact (n)
  (if (<= n 1)
    1
    (* n (multiline-fact (1- n)))))
(check "multiline defun" 120 (multiline-fact 5))
(check "multiline nested"
  15
  (let ((x 10)
        (y 5))
    (+ x y)))

; --- defstruct ---
(check "defstruct basic" 'point (defstruct point (x 0) (y 0)))
(check "defstruct make default" 0 (point-x (make-point)))
(check "defstruct make keyword" 10 (point-x (make-point :x 10 :y 20)))
(check "defstruct accessor y" 20 (point-y (make-point :x 10 :y 20)))
(check "defstruct predicate t" t (point-p (make-point)))
(check "defstruct predicate nil" nil (point-p 42))
(check "defstruct setf" 99 (let ((p (make-point :x 1))) (setf (point-x p) 99) (point-x p)))
(check "defstruct copier" 1 (let ((a (make-point :x 1))) (let ((b (copy-point a))) (setf (point-x b) 99) (point-x a))))
(check "defstruct typep" t (typep (make-point) 'point))
(check "defstruct typep nil" nil (typep 42 'point))
(check "defstruct type-of" 'point (type-of (make-point)))
(check "defstruct structurep" t (structurep (make-point)))
(check "defstruct structurep nil" nil (structurep 42))
(check "defstruct printer" "#S(POINT :X 1 :Y 2)" (prin1-to-string (make-point :x 1 :y 2)))
(check "defstruct no-slot define" 'empty-st (defstruct empty-st))
(check "defstruct no-slot make" t (empty-st-p (make-empty-st)))
(defstruct (cst (:conc-name c-)) (r 0) (g 0))
(check "defstruct conc-name" 255 (c-r (make-cst :r 255)))
(defstruct (nst (:constructor new-nst)) (val 0))
(check "defstruct custom ctor" 7 (nst-val (new-nst :val 7)))
(defstruct base-st (a 1) (b 2))
(defstruct (child-st (:include base-st)) (c 3))
(check "defstruct include a" 10 (child-st-a (make-child-st :a 10 :c 30)))
(check "defstruct include b" 2 (child-st-b (make-child-st)))
(check "defstruct include c" 30 (child-st-c (make-child-st :c 30)))
(check "defstruct include typep parent" t (typep (make-child-st) 'base-st))
(check "defstruct include typep child" t (typep (make-child-st) 'child-st))
(check "defstruct include typep neg" nil (typep (make-base-st) 'child-st))

; --- the ---
(check "the fixnum" 42 (the fixnum 42))
(check "the string" "hello" (the string "hello"))
(check "the fixnum expr" 3 (the fixnum (+ 1 2)))
(check "the symbol" 'foo (the symbol 'foo))
(check "the list" '(1 2) (the list '(1 2)))
(check "the null" nil (the null nil))
(check "the or fixnum null 1" 42 (the (or fixnum null) 42))
(check "the or fixnum null 2" nil (the (or fixnum null) nil))
(check "the nested" 3 (the fixnum (the fixnum (+ 1 2))))
(check "the type-error" t (handler-case (progn (the fixnum "oops") nil) (type-error () t)))
(declaim (optimize (safety 0)))
(check "the safety 0" "oops" (the fixnum "oops"))
(declaim (optimize (safety 1)))

; --- Bignums ---
(check "bignum add overflow" 1073741824 (+ 1073741823 1))
(check "bignum sub overflow" -1073741825 (+ -1073741824 -1))
(check "bignum mul overflow" 1000000000000 (* 1000000 1000000))
(check "bignum demotion" 1073741823 (- (+ 1073741823 1) 1))
(check "bignum read large" 1073741824 1073741824)
(check "bignum expt 2^32" 4294967296 (expt 2 32))
(defun bn-fact (n) (if (<= n 1) 1 (* n (bn-fact (1- n)))))
(check "bignum factorial 10" 3628800 (bn-fact 10))
(check "bignum factorial 20" 2432902008176640000 (bn-fact 20))
(check "bignum compare eq" t (= (expt 2 64) (expt 2 64)))
(check "bignum compare lt" t (< (expt 2 64) (expt 2 65)))
(check "bignum compare gt" t (> (expt 2 65) (expt 2 64)))
(check "bignum compare mixed" t (> (expt 2 64) 42))
(check "bignum negate" t (minusp (- (expt 2 64))))
(check "bignum abs" t (plusp (abs (- (expt 2 64)))))
(check "bignum div" 1125899906842624 (/ (expt 2 100) (expt 2 50)))
(check "bignum mod" 1 (mod 10 3))
(check "bignum gcd" 4 (gcd 12 8))
(check "bignum gcd large" 1125899906842624 (gcd (expt 2 100) (expt 2 50)))
(check "bignum lcm" 12 (lcm 4 6))
(check "bignum evenp" t (evenp (expt 2 64)))
(check "bignum oddp" t (oddp (+ (expt 2 64) 1)))
(check "bignum eql" t (eql (expt 2 64) (expt 2 64)))
(check "bignum logand" 9 (logand 15 9))
(check "bignum logior" 15 (logior 15 9))
(check "bignum logxor" 6 (logxor 15 9))
(check "bignum lognot" -1 (lognot 0))
(check "bignum ash left" 1024 (ash 1 10))
(check "bignum ash right" 1 (ash 1024 -10))
(check "bignum integer-length" 8 (integer-length 255))
(check "bignum isqrt" 1000 (isqrt 1000000))
(check "bignum typep fixnum" t (typep 42 'fixnum))
(check "bignum typep bignum" t (typep (expt 2 64) 'bignum))
(check "bignum typep integer" t (typep (expt 2 64) 'integer))
(check "bignum typep number" t (typep (expt 2 64) 'number))
(check "bignum type-of" 'bignum (type-of (expt 2 64)))
(check "bignum numberp" t (numberp (expt 2 64)))
(check "bignum integerp" t (integerp (expt 2 64)))
(check "most-positive-fixnum" 1073741823 most-positive-fixnum)
(check "most-negative-fixnum" -1073741824 most-negative-fixnum)
(check "subtypep bignum<integer" t (subtypep 'bignum 'integer))
(check "subtypep bignum<number" t (subtypep 'bignum 'number))

; --- Float basics (Steps 1-8) ---
(check "float read single" 3.14 3.14)
(check "float read double" 1.0d0 1.0d0)
(check "float add" 5.0 (+ 2.0 3.0))
(check "float sub" 1.5 (- 3.5 2.0))
(check "float mul" 6.0 (* 2.0 3.0))
(check "float div" 2.5 (/ 5.0 2.0))
(check "float negate" -3.5 (- 3.5))
(check "float compare =" t (= 3.14 3.14))
(check "float compare <" t (< 1.0 2.0))
(check "float compare >" t (> 3.0 2.0))
(check "float zerop" t (zerop 0.0))
(check "float plusp" t (plusp 1.0))
(check "float minusp" t (minusp -1.0))
(check "float abs" 3.5 (abs -3.5))
(check "floatp yes" t (floatp 1.0))
(check "floatp no" nil (floatp 1))
(check "float numberp" t (numberp 1.0))
(check "float typep single" t (typep 1.0 'single-float))
(check "float typep double" t (typep 1.0d0 'double-float))
(check "float typep float" t (typep 1.0 'float))
(check "float eql same" t (eql 1.0 1.0))
(check "float eql diff type" nil (eql 1.0 1.0d0))
(check "float int add" 6.0 (+ 3.0 3))
(check "float contagion" t (typep (+ 1 1.0d0) 'double-float))

; --- Float-specific functions (Step 9) ---
(check "float convert int" 5.0 (float 5))
(check "float convert proto double" 5.0d0 (float 5 1.0d0))
(check "float convert proto single" 5.0 (float 5 1.0))
(check "float passthrough" 3.14 (float 3.14))
(check "float-digits single" 24 (float-digits 1.0))
(check "float-digits double" 53 (float-digits 1.0d0))
(check "float-radix" 2 (float-radix 1.0))
(check "float-sign positive" 1.0 (float-sign 3.5))
(check "float-sign negative" -1.0 (float-sign -3.5))
(check "float-sign two args" -2.5 (float-sign -1.0 2.5))
(check "decode-float 1.5" (list 0.75 1 1.0) (multiple-value-list (decode-float 1.5)))
(check "decode-float -1.5" (list 0.75 1 -1.0) (multiple-value-list (decode-float -1.5)))
(check "decode-float 0.0" (list 0.0 0 1.0) (multiple-value-list (decode-float 0.0)))
(check "decode-float 8.0" (list 0.5 4 1.0) (multiple-value-list (decode-float 8.0)))
(check "integer-decode-float 1.5" (list 12582912 -23 1) (multiple-value-list (integer-decode-float 1.5)))
(check "integer-decode-float 0.0" (list 0 0 1) (multiple-value-list (integer-decode-float 0.0)))
(check "scale-float up" 8.0 (scale-float 1.0 3))
(check "scale-float down" 1.0 (scale-float 8.0 -3))
(check "scale-float double" 8.0d0 (scale-float 1.0d0 3))
(check "decode-float reconstruct" 3.14 (multiple-value-bind (s e g) (decode-float 3.14) (* (scale-float s e) g)))

; --- Rounding functions (Step 10) ---
(check "truncate float 1arg" 2 (truncate 2.7))
(check "truncate float neg" -2 (truncate -2.7))
(check "truncate mv" (list 3 1) (multiple-value-list (truncate 7 2)))
(check "truncate mv neg" (list -3 -1) (multiple-value-list (truncate -7 2)))
(check "truncate mv float" (list 2 0.5) (multiple-value-list (truncate 2.5)))
(check "floor int" 3 (floor 7 2))
(check "floor int neg" -4 (floor -7 2))
(check "floor float 1arg" 2 (floor 2.7))
(check "floor float neg" -3 (floor -2.7))
(check "floor mv" (list 3 1) (multiple-value-list (floor 7 2)))
(check "floor mv neg" (list -4 1) (multiple-value-list (floor -7 2)))
(check "floor mv float" (list 2 0.5) (multiple-value-list (floor 2.5)))
(check "ceiling int" 4 (ceiling 7 2))
(check "ceiling int neg" -3 (ceiling -7 2))
(check "ceiling float 1arg" 3 (ceiling 2.3))
(check "ceiling float neg" -2 (ceiling -2.3))
(check "ceiling mv" (list 4 -1) (multiple-value-list (ceiling 7 2)))
(check "ceiling mv neg" (list -3 -1) (multiple-value-list (ceiling -7 2)))
(check "round banker even" 2 (round 2.5))
(check "round banker odd" 4 (round 3.5))
(check "round down" 2 (round 2.3))
(check "round up" 3 (round 2.7))
(check "round neg" -2 (round -2.5))
(check "round int" 4 (round 7 2))
(check "round int even" 2 (round 5 2))
(check "ffloor float" 2.0 (ffloor 2.7))
(check "ffloor neg" -3.0 (ffloor -2.7))
(check "ffloor int" 3.0 (ffloor 7 2))
(check "fceiling float" 3.0 (fceiling 2.3))
(check "ftruncate float" 2.0 (ftruncate 2.7))
(check "ftruncate neg" -2.0 (ftruncate -2.7))
(check "fround even" 2.0 (fround 2.5))
(check "fround odd" 4.0 (fround 3.5))
(check "ffloor double" 2.0d0 (ffloor 2.7d0))
(check "mod float" 1.0 (mod 10.0 3))
(check "mod float neg" 2.0 (mod -10.0 3))
(check "mod float neg div" -2.0 (mod 10.0 -3))
(check "rem float" 1.0 (rem 10.0 3))
(check "rem float neg" -1.0 (rem -10.0 3))
(check "rem float neg div" 1.0 (rem 10.0 -3))

; --- Streams (Phase 7 Step 1) ---
(check "streamp on stream" t (streamp (%make-test-stream 1 0)))
(check "streamp on fixnum" nil (streamp 42))
(check "streamp on string" nil (streamp "hello"))
(check "streamp on nil" nil (streamp nil))
(check "type-of stream" 'stream (type-of (%make-test-stream 1 0)))
(check "typep stream" t (typep (%make-test-stream 2 0) 'stream))
(check "input-stream-p input" t (input-stream-p (%make-test-stream 1 0)))
(check "input-stream-p output" nil (input-stream-p (%make-test-stream 2 0)))
(check "input-stream-p io" t (input-stream-p (%make-test-stream 3 0)))
(check "output-stream-p output" t (output-stream-p (%make-test-stream 2 0)))
(check "output-stream-p input" nil (output-stream-p (%make-test-stream 1 0)))
(check "output-stream-p io" t (output-stream-p (%make-test-stream 3 0)))
(check "stream console type" t (streamp (%make-test-stream 1 0)))
(check "stream file type" t (streamp (%make-test-stream 1 1)))
(check "stream string type" t (streamp (%make-test-stream 1 2)))

; --- Streams (Phase 7 Step 2) ---
; Standard stream variables are bound to streams
(check "standard-input bound" t (streamp *standard-input*))
(check "standard-output bound" t (streamp *standard-output*))
(check "error-output bound" t (streamp *error-output*))
(check "trace-output bound" t (streamp *trace-output*))
(check "debug-io bound" t (streamp *debug-io*))
(check "query-io bound" t (streamp *query-io*))
(check "terminal-io bound" t (streamp *terminal-io*))
; Direction checks
(check "standard-input is input" t (input-stream-p *standard-input*))
(check "standard-input not output" nil (output-stream-p *standard-input*))
(check "standard-output is output" t (output-stream-p *standard-output*))
(check "standard-output not input" nil (input-stream-p *standard-output*))
(check "error-output is output" t (output-stream-p *error-output*))
; interactive-stream-p
(check "interactive-stream-p console" t (interactive-stream-p *standard-output*))
(check "interactive-stream-p stdin" t (interactive-stream-p *standard-input*))
(check "interactive-stream-p file" nil (interactive-stream-p (%make-test-stream 1 1)))
(check "interactive-stream-p string" nil (interactive-stream-p (%make-test-stream 1 2)))

; --- Streams (Phase 7 Step 3) ---
; String output streams
(check "make-string-output-stream" t (streamp (make-string-output-stream)))
(check "write-char to string stream" "A" (let ((s (make-string-output-stream))) (write-char #\A s) (get-output-stream-string s)))
(check "write-string to string stream" "Hello" (let ((s (make-string-output-stream))) (write-string "Hello" s) (get-output-stream-string s)))
(check "write-line to string stream" (concatenate 'string "Hello" (string #\Newline)) (let ((s (make-string-output-stream))) (write-line "Hello" s) (get-output-stream-string s)))
(check "get-output-stream-string resets" "second" (let ((s (make-string-output-stream))) (write-string "first" s) (get-output-stream-string s) (write-string "second" s) (get-output-stream-string s)))
(check "multiple write-char" "ABC" (let ((s (make-string-output-stream))) (write-char #\A s) (write-char #\B s) (write-char #\C s) (get-output-stream-string s)))

; String input streams
(check "make-string-input-stream" t (streamp (make-string-input-stream "hello")))
(check "read-char from string" #\H (read-char (make-string-input-stream "Hello")))
(check "read-char eof error" t (handler-case (progn (read-char (make-string-input-stream "")) nil) (error (c) t)))
(check "read-char eof no error" :eof (read-char (make-string-input-stream "") nil :eof))
(check "read-line from string" "Hello" (read-line (make-string-input-stream "Hello")))
(check "read-line with newline" "Hello" (read-line (make-string-input-stream (concatenate 'string "Hello" (string #\Newline) "World"))))
(check "read-line missing-newline-p" t (nth-value 1 (read-line (make-string-input-stream "Hello"))))
(check "read-line has newline" nil (nth-value 1 (read-line (make-string-input-stream (concatenate 'string "Hello" (string #\Newline))))))
(check "string-input-stream start end" #\W (read-char (make-string-input-stream "Hello World" 6)))

; peek-char
(check "peek-char nil" #\A (peek-char nil (make-string-input-stream "ABC")))
(check "peek-char no consume" #\A (let ((s (make-string-input-stream "ABC"))) (peek-char nil s) (read-char s)))
(check "peek-char t skip ws" #\A (peek-char t (make-string-input-stream "  A")))
(check "peek-char char skip" #\B (peek-char #\B (make-string-input-stream "xxByz")))

; unread-char
(check "unread-char" #\H (let ((s (make-string-input-stream "Hi"))) (let ((c (read-char s))) (unread-char c s) (read-char s))))

; write-string :start :end
(check "write-string start end" "llo" (let ((s (make-string-output-stream))) (write-string "Hello" s :start 2 :end 5) (get-output-stream-string s)))

; open-stream-p / close
(check "open-stream-p open" t (open-stream-p (make-string-output-stream)))
(check "close returns t" t (close (make-string-output-stream)))
(check "open-stream-p closed" nil (let ((s (make-string-output-stream))) (close s) (open-stream-p s)))

; terpri with stream
(check "terpri to stream" (string #\Newline) (let ((s (make-string-output-stream))) (terpri s) (get-output-stream-string s)))

; fresh-line
(check "fresh-line at BOL" nil (let ((s (make-string-output-stream))) (fresh-line s)))
(check "fresh-line not at BOL" t (let ((s (make-string-output-stream))) (write-char #\X s) (fresh-line s)))
(check "fresh-line writes newline" (concatenate 'string "X" (string #\Newline)) (let ((s (make-string-output-stream))) (write-char #\X s) (fresh-line s) (get-output-stream-string s)))

; finish-output / force-output / clear-output (no error)
(check "finish-output no error" nil (finish-output))
(check "force-output no error" nil (force-output))
(check "clear-output no error" nil (clear-output))

; write-char returns character
(check "write-char returns char" #\X (write-char #\X (make-string-output-stream)))

; --- Read from stream ---
(check "read from string-input-stream list" '(+ 1 2) (read (make-string-input-stream "(+ 1 2)")))
(check "read from string-input-stream integer" 42 (read (make-string-input-stream "42")))
(check "read from string-input-stream symbol" 'hello (read (make-string-input-stream "hello")))
(check "read from string-input-stream string" "hello" (read (make-string-input-stream "\"hello\"")))
(check "read eof-value" :eof (read (make-string-input-stream "") nil :eof))
(check "read-from-string list" '(hello world) (read-from-string "(hello world)"))
(check "read-from-string integer" 42 (read-from-string "42"))
(check "read-from-string eof-value" :eof (read-from-string "" nil :eof))

; --- Printer + stream integration (Step 7) ---

; prin1/princ/print with optional stream arg
(check "prin1 to stream" "42" (let ((s (make-string-output-stream))) (prin1 42 s) (get-output-stream-string s)))
(check "princ to stream" "hello" (let ((s (make-string-output-stream))) (princ "hello" s) (get-output-stream-string s)))
(check "prin1 string escaped" "\"hello\"" (let ((s (make-string-output-stream))) (prin1 "hello" s) (get-output-stream-string s)))
(check "princ string unescaped" "hello" (let ((s (make-string-output-stream))) (princ "hello" s) (get-output-stream-string s)))
(check "prin1 list to stream" "(1 2 3)" (let ((s (make-string-output-stream))) (prin1 '(1 2 3) s) (get-output-stream-string s)))
(check "prin1 nil to stream" "NIL" (let ((s (make-string-output-stream))) (prin1 nil s) (get-output-stream-string s)))
(check "prin1 char to stream" "#\\A" (let ((s (make-string-output-stream))) (prin1 #\A s) (get-output-stream-string s)))
(check "princ char to stream" "A" (let ((s (make-string-output-stream))) (princ #\A s) (get-output-stream-string s)))
(check "multiple prints to stream" "1+2=3" (let ((s (make-string-output-stream))) (princ 1 s) (write-char #\+ s) (princ 2 s) (write-char #\= s) (princ 3 s) (get-output-stream-string s)))
(check "prin1 returns object" 42 (prin1 42 (make-string-output-stream)))
(check "princ returns object" "hi" (princ "hi" (make-string-output-stream)))

; format with destination
(check "format nil returns string" "hello 42" (format nil "hello ~A" 42))
(check "format nil multi args" "1 + 2 = 3" (format nil "~A + ~A = ~A" 1 2 3))
(check "format nil ~S" "\"hi\"" (format nil "~S" "hi"))
(check "format nil tilde" "~" (format nil "~~"))
(check "format to stream" "val=99" (let ((s (make-string-output-stream))) (format s "val=~A" 99) (get-output-stream-string s)))
(check "format t returns nil" nil (format t ""))

; with-output-to-string macro
(check "with-output-to-string basic" "hello" (with-output-to-string (s) (write-string "hello" s)))
(check "with-output-to-string format" "1 + 2 = 3" (with-output-to-string (s) (format s "~A + ~A = ~A" 1 2 3)))
(check "with-output-to-string prin1" "42" (with-output-to-string (s) (prin1 42 s)))
(check "with-output-to-string multiple" "AB" (with-output-to-string (s) (princ "A" s) (princ "B" s)))

; with-input-from-string macro
(check "with-input-from-string read" '(+ 1 2) (with-input-from-string (s "(+ 1 2)") (read s)))
(check "with-input-from-string read-char" #\H (with-input-from-string (s "Hello") (read-char s)))

; prin1-to-string / princ-to-string / write-to-string
(check "prin1-to-string fixnum" "42" (prin1-to-string 42))
(check "prin1-to-string string" "\"hello\"" (prin1-to-string "hello"))
(check "prin1-to-string list" "(1 2 3)" (prin1-to-string '(1 2 3)))
(check "prin1-to-string nil" "NIL" (prin1-to-string nil))
(check "prin1-to-string char" "#\\A" (prin1-to-string #\A))
(check "princ-to-string string" "hello" (princ-to-string "hello"))
(check "princ-to-string fixnum" "42" (princ-to-string 42))
(check "write-to-string" "(1 2 3)" (write-to-string '(1 2 3)))

; --- File streams / open / with-open-file (Step 8) ---
; Use /tmp/ on POSIX, T: on Amiga — both are writable temp directories
; Detect by trying /tmp/ first (fails on Amiga), fall back to T:
(defvar *test-tmp*
  (handler-case
    (let ((s (open "/tmp/_cltest_probe" :direction :output)))
      (close s) "/tmp/")
    (error (c) "T:")))

; open for output and input
(check "open output creates stream" t (let ((s (open (concatenate 'string *test-tmp* "_clt_s8.txt") :direction :output))) (close s) (streamp s)))
(let ((s (open (concatenate 'string *test-tmp* "_clt_s8.txt") :direction :output))) (write-string "Test123" s) (close s))
(check "open input reads" "Test123" (let ((s (open (concatenate 'string *test-tmp* "_clt_s8.txt") :direction :input))) (let ((line (read-line s))) (close s) line)))

; open with :if-exists :append
(let ((s (open (concatenate 'string *test-tmp* "_clt_s8a.txt") :direction :output))) (write-string "First" s) (close s))
(let ((s (open (concatenate 'string *test-tmp* "_clt_s8a.txt") :direction :output :if-exists :append))) (write-string "Second" s) (close s))
(check "open append" "FirstSecond" (let ((s (open (concatenate 'string *test-tmp* "_clt_s8a.txt") :direction :input))) (let ((line (read-line s))) (close s) line)))

; open nonexistent for input -> error
(check "open nonexistent error" t (handler-case (progn (open (concatenate 'string *test-tmp* "_nonexist_xyz.txt") :direction :input) nil) (error (c) t)))

; open nonexistent for input with :if-does-not-exist nil -> nil
(check "open nonexistent nil" nil (open (concatenate 'string *test-tmp* "_nonexist_xyz.txt") :direction :input :if-does-not-exist nil))

; with-open-file write then read
(with-open-file (s (concatenate 'string *test-tmp* "_clt_wof.txt") :direction :output)
  (format s "Hello ~A" "WOF"))
(check "with-open-file read" "Hello WOF" (with-open-file (s (concatenate 'string *test-tmp* "_clt_wof.txt") :direction :input) (read-line s)))

; with-open-file returns body value
(check "with-open-file returns value" 42 (with-open-file (s (concatenate 'string *test-tmp* "_clt_wof.txt") :direction :input) (read-line s) 42))

; with-open-file close on error
(check "with-open-file closes on error" t (let ((stream nil)) (handler-case (with-open-file (s (concatenate 'string *test-tmp* "_clt_wof.txt") :direction :input) (setq stream s) (error "boom")) (error (c) (not (open-stream-p stream))))))

; format to file stream
(with-open-file (s (concatenate 'string *test-tmp* "_clt_fmt.txt") :direction :output)
  (format s "~A + ~A = ~A" 1 2 3))
(check "format to file" "1 + 2 = 3" (with-open-file (s (concatenate 'string *test-tmp* "_clt_fmt.txt") :direction :input) (read-line s)))

; prin1/princ to file stream
(with-open-file (s (concatenate 'string *test-tmp* "_clt_pr.txt") :direction :output)
  (prin1 42 s)
  (princ " " s)
  (princ "hello" s))
(check "prin1/princ to file" "42 hello" (with-open-file (s (concatenate 'string *test-tmp* "_clt_pr.txt") :direction :input) (read-line s)))

; --- Summary ---
(format t "~%=== Results ===~%")
(format t "Passed: ~A~%" *pass-count*)
(format t "Failed: ~A~%" *fail-count*)
(format t "Total:  ~A~%" (+ *pass-count* *fail-count*))
(if (= *fail-count* 0) (format t "~%ALL TESTS PASSED~%") (format t "~%SOME TESTS FAILED~%"))
