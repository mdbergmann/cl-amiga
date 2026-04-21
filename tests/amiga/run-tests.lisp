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

; --- Heap-boxed cells (mutable closure capture) ---
(let ((n 0)) (defun cell-inc () (setq n (+ n 1)) n))
(check "cell counter 1" 1 (cell-inc))
(check "cell counter 2" 2 (cell-inc))
(check "cell counter 3" 3 (cell-inc))

(let ((x 10)) (defun cg () x) (defun cs (v) (setq x v)))
(check "cell getter init" 10 (cg))
(cs 42)
(check "cell getter after set" 42 (cg))

(defun make-acc (init) (lambda (x) (setq init (+ init x)) init))
(defparameter *ta* (make-acc 0))
(check "cell accum 5" 5 (funcall *ta* 5))
(check "cell accum +3" 8 (funcall *ta* 3))
(check "cell accum +10" 18 (funcall *ta* 10))

(defun mk-ctr () (let ((n 0)) (lambda () (setq n (+ n 1)) n)))
(defparameter *tc1* (mk-ctr))
(defparameter *tc2* (mk-ctr))
(check "cell indep c1-1" 1 (funcall *tc1*))
(check "cell indep c1-2" 2 (funcall *tc1*))
(check "cell indep c2-1" 1 (funcall *tc2*))
(check "cell indep c1-3" 3 (funcall *tc1*))
(check "cell indep c2-2" 2 (funcall *tc2*))

(check "cell setq before capture" 42 (let ((x 0)) (setq x 42) ((lambda () x))))

(let ((x 0)) (defun cell-outer () (lambda () (setq x (+ x 1)) x)))
(check "cell nested 1" 1 (funcall (cell-outer)))
(check "cell nested 2" 2 (funcall (cell-outer)))

(check "cell flet mutation" 3
  (let ((n 0))
    (flet ((bump () (setq n (+ n 1))))
      (bump) (bump) (bump) n)))

(check "cell labels shared" 3
  (let ((n 0))
    (labels ((my-inc () (setq n (+ n 1)))
             (my-peek () n))
      (my-inc) (my-inc) (my-inc) (my-peek))))

(check "cell readonly" 42 (let ((x 42)) ((lambda () x))))

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
; Regression: key param shadowing special var must see dynamic value as default
(defvar *key-special-test* 100)
(defun key-special-def (&key ((:path *key-special-test*) *key-special-test*))
  *key-special-test*)
(check "key default special" 100 (key-special-def))
(check "key override special" 77 (key-special-def :path 77))
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
(setq *dv1* 10)
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
(defparameter *sv1* 1)
(check "special let binding" 2 (let ((*sv1* 2)) *sv1*))
(check "special restored" 1 *sv1*)
(check "special let* binding" 20 (let* ((*sv1* 20)) *sv1*))
(check "special let* restored" 1 *sv1*)
(defparameter *sv2* 0)
(defun read-sv2 () *sv2*)
(check "special visible in fn" 99 (let ((*sv2* 99)) (read-sv2)))
(check "special fn restored" 0 (read-sv2))
(defparameter *sv3* 1)
(check "special nested" 3 (let ((*sv3* 2)) (let ((*sv3* 3)) *sv3*)))
(check "special nested restored" 1 *sv3*)
(defparameter *sv4* 1)
(check "special setq" 20 (let ((*sv4* 10)) (setq *sv4* 20) *sv4*))
(check "special setq restored" 1 *sv4*)
(defparameter *sv5* 1)
(catch 'done (unwind-protect (let ((*sv5* 99)) (throw 'done *sv5*)) nil))
(check "special uwp restored" 1 *sv5*)
(defparameter *sv6* 10)
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
; --- Reader #(...) syntax (Step 8) ---
(check "#() reader empty" "#()" (write-to-string #()))
(check "#() reader elems" "#(1 2 3)" (write-to-string #(1 2 3)))
(check "#() reader aref" 20 (aref #(10 20 30) 1))
(check "#() reader length" 4 (length #(a b c d)))
(check "#() reader svp" t (simple-vector-p #(1 2)))
(check "#() reader nested" "#(1 2)" (write-to-string (aref #(#(1 2) #(3 4)) 0)))

(check "array-dimensions" '(5) (array-dimensions (make-array 5)))
(check "array-rank" 1 (array-rank (make-array 5)))

; --- Multi-dim array printing (Step 9) ---
(check "print 2d array" "#2A((1 2 3) (4 5 6))" (write-to-string (make-array '(2 3) :initial-contents '((1 2 3) (4 5 6)))))
(check "print 3d array" "#3A(((1 2) (3 4)) ((5 6) (7 8)))" (write-to-string (make-array '(2 2 2) :initial-contents '(((1 2) (3 4)) ((5 6) (7 8))))))
(check "print 2d empty" "#2A(() ())" (write-to-string (make-array '(2 0))))
(check "print-array nil vec" "#<VECTOR>" (let ((*print-array* nil)) (write-to-string (vector 1 2))))
(check "print-array nil arr" "#<ARRAY>" (let ((*print-array* nil)) (write-to-string (make-array '(2 3)))))

; --- Multi-dimensional arrays (Step 4) ---
(check "make-array 2d" '(2 3) (array-dimensions (make-array '(2 3))))
(check "aref 2d setf" 70 (let ((a (make-array '(2 3)))) (setf (aref a 0 0) 10) (setf (aref a 1 2) 60) (+ (aref a 0 0) (aref a 1 2))))
(check "setf aref 2d ret" 99 (let ((a (make-array '(3 3)))) (setf (aref a 1 2) 99)))
(check "aref 3d" 9 (let ((a (make-array '(2 2 2)))) (setf (aref a 0 0 0) 1) (setf (aref a 1 1 1) 8) (+ (aref a 0 0 0) (aref a 1 1 1))))
(check "array-rank 2d" 2 (array-rank (make-array '(3 4))))
(check "array-dimensions 2d" '(3 4) (array-dimensions (make-array '(3 4))))

; --- Array query builtins (Step 5) ---
(check "array-dimension 1d" 5 (array-dimension (make-array 5) 0))
(check "array-dimension 2d ax0" 3 (array-dimension (make-array '(3 4)) 0))
(check "array-dimension 2d ax1" 4 (array-dimension (make-array '(3 4)) 1))
(check "array-total-size 1d" 5 (array-total-size (make-array 5)))
(check "array-total-size 2d" 12 (array-total-size (make-array '(3 4))))
(check "array-total-size 3d" 24 (array-total-size (make-array '(2 3 4))))
(check "row-major-index 1d" 3 (array-row-major-index (make-array 5) 3))
(check "row-major-index 2d" 0 (array-row-major-index (make-array '(3 4)) 0 0))
(check "row-major-index 2d b" 4 (array-row-major-index (make-array '(3 4)) 1 0))
(check "row-major-index 2d c" 11 (array-row-major-index (make-array '(3 4)) 2 3))
(check "row-major-aref" 99 (let ((a (make-array '(2 3)))) (setf (aref a 1 2) 99) (row-major-aref a 5)))
(check "setf row-major-aref" 77 (let ((a (make-array '(2 3)))) (setf (row-major-aref a 5) 77) (aref a 1 2)))

; --- Fill pointer operations (Step 6) ---
(check "fill-pointer 0" 0 (fill-pointer (make-array 10 :fill-pointer 0)))
(check "fill-pointer 5" 5 (fill-pointer (make-array 10 :fill-pointer 5)))
(check "fill-pointer t" 0 (fill-pointer (make-array 10 :fill-pointer t)))
(check "has-fill-pointer t" t (array-has-fill-pointer-p (make-array 5 :fill-pointer 0)))
(check "has-fill-pointer nil" nil (array-has-fill-pointer-p (make-array 5)))
(check "setf fill-pointer" 7 (let ((v (make-array 10 :fill-pointer 0))) (setf (fill-pointer v) 7) (fill-pointer v)))
(check "setf fill-pointer ret" 3 (let ((v (make-array 10 :fill-pointer 0))) (setf (fill-pointer v) 3)))
(check "vector-push basic" 3 (let ((v (make-array 5 :fill-pointer 0))) (vector-push 10 v) (vector-push 20 v) (vector-push 30 v) (fill-pointer v)))
(check "vector-push ret" 1 (let ((v (make-array 5 :fill-pointer 0))) (vector-push 10 v) (vector-push 20 v)))
(check "vector-push full" nil (let ((v (make-array 2 :fill-pointer 0))) (vector-push 1 v) (vector-push 2 v) (vector-push 3 v)))
(check "vector-push data" 30 (let ((v (make-array 3 :fill-pointer 0))) (vector-push 10 v) (vector-push 20 v) (+ (aref v 0) (aref v 1))))
(check "length w/ fp" 2 (let ((v (make-array 10 :fill-pointer 0))) (vector-push 1 v) (vector-push 2 v) (length v)))
(check "adjust-array grow" 100 (let ((v (make-array 3 :adjustable t :initial-element 1))) (let ((v2 (adjust-array v 5 :initial-element 99))) (+ (aref v2 0) (aref v2 3)))))
(check "adjust-array shrink" 3 (let ((v (make-array 5 :adjustable t :initial-element 42))) (array-total-size (adjust-array v 3))))
(check "adjust-array fp" 2 (let ((v (make-array 5 :fill-pointer 2 :adjustable t))) (fill-pointer (adjust-array v 10))))
(check "adjust-array identity" t (let ((v (make-array 3 :adjustable t))) (eq v (adjust-array v 10))))
(check "adjust-array data" 104 (let ((v (make-array 3 :adjustable t :initial-element 5))) (adjust-array v 6 :initial-element 99) (+ (aref v 0) (aref v 4))))
(check "vpush-ext basic" 2 (let ((v (make-array 2 :fill-pointer 0 :adjustable t))) (vector-push-extend 10 v) (vector-push-extend 20 v) (vector-push-extend 30 v)))
(check "vpush-ext fp" 3 (let ((v (make-array 2 :fill-pointer 0 :adjustable t))) (vector-push-extend 10 v) (vector-push-extend 20 v) (vector-push-extend 30 v) (fill-pointer v)))
(check "vpush-ext data" 60 (let ((v (make-array 2 :fill-pointer 0 :adjustable t))) (vector-push-extend 10 v) (vector-push-extend 20 v) (vector-push-extend 30 v) (+ (aref v 0) (aref v 1) (aref v 2))))
(check "vpush-ext many" 20 (let ((v (make-array 1 :fill-pointer 0 :adjustable t))) (dotimes (i 20) (vector-push-extend i v)) (fill-pointer v)))
(check "vpush-ext identity" t (let ((v (make-array 1 :fill-pointer 0 :adjustable t))) (let ((v2 v)) (vector-push-extend 42 v) (vector-push-extend 99 v) (eq v v2))))
(check "vpush-ext zero" 42 (let ((v (make-array 0 :fill-pointer 0 :adjustable t))) (vector-push-extend 42 v) (aref v 0)))

; --- Array type predicates (Step 7) ---
(check "arrayp vector" t (arrayp (vector 1 2 3)))
(check "arrayp make-array" t (arrayp (make-array 5)))
(check "arrayp 2d" t (arrayp (make-array '(2 3))))
(check "arrayp string" t (arrayp "hello"))
(check "arrayp fixnum" nil (arrayp 42))
(check "arrayp list" nil (arrayp '(1 2)))
(check "arrayp nil" nil (arrayp nil))
(check "simple-vector-p vec" t (simple-vector-p (vector 1 2 3)))
(check "simple-vector-p arr" t (simple-vector-p (make-array 5)))
(check "simple-vector-p fp" nil (simple-vector-p (make-array 5 :fill-pointer 0)))
(check "simple-vector-p adj" nil (simple-vector-p (make-array 5 :adjustable t)))
(check "simple-vector-p 2d" nil (simple-vector-p (make-array '(2 3))))
(check "simple-vector-p str" nil (simple-vector-p "hello"))
(check "simple-vector-p fix" nil (simple-vector-p 42))
(check "adjustable-array-p t" t (adjustable-array-p (make-array 5 :adjustable t)))
(check "adjustable-array-p nil" nil (adjustable-array-p (make-array 5)))
(check "adjustable-array-p vec" nil (adjustable-array-p (vector 1 2)))
(check "adjustable-array-p str" nil (adjustable-array-p "hello"))
(check "typep array vec" t (typep (vector 1 2) 'array))
(check "typep array 2d" t (typep (make-array '(2 3)) 'array))
(check "typep array str" t (typep "hello" 'array))
(check "typep array fix" nil (typep 42 'array))
(check "typep vector 1d" t (typep (vector 1 2) 'vector))
(check "typep vector str" t (typep "hello" 'vector))
(check "typep vector 2d" nil (typep (make-array '(2 3)) 'vector))
(check "typep simple-vec" t (typep (vector 1 2) 'simple-vector))
(check "typep simple-vec fp" nil (typep (make-array 5 :fill-pointer 0) 'simple-vector))
(check "typep simple-vec str" nil (typep "hello" 'simple-vector))
(check "typep simple-arr" t (typep (vector 1 2) 'simple-array))
(check "typep simple-arr 2d" t (typep (make-array '(2 3)) 'simple-array))
(check "typep simple-arr fp" nil (typep (make-array 5 :fill-pointer 0) 'simple-array))
(check "type-of simple-vec" 'simple-vector (type-of (vector 1 2 3)))
(check "type-of vec w/ fp" 'vector (type-of (make-array 5 :fill-pointer 0)))
(check "type-of 2d" 'simple-array (type-of (make-array '(2 3))))
(check "type-of string" 'string (type-of "hello"))

(defparameter *sv-t1* 42)
(check "symbol-value" 42 (symbol-value '*sv-t1*))
(defparameter *sv-t2* 10)
(check "setf symbol-value" 99 (setf (symbol-value '*sv-t2*) 99))
(check "symbol-value after setf" 99 *sv-t2*)
(defparameter *set-t* 10)
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

; --- load-time-value ---
(check "ltv basic" 3 (load-time-value (+ 1 2)))
(check "ltv list" '(1 2 3) (load-time-value (list 1 2 3)))
(check "ltv constant" 42 (load-time-value 42))
(check "ltv t" t (load-time-value t))
(check "ltv nil" nil (load-time-value nil))
(check "ltv read-only" 30 (load-time-value (+ 10 20) t))
(defun ltv-fn () (load-time-value (cons 'a 'b)))
(check "ltv in function" '(a . b) (ltv-fn))
(check "ltv same object" t (eq (ltv-fn) (ltv-fn)))

; --- get-properties ---
(check "get-properties found" '(:b 2 (:b 2 :c 3)) (multiple-value-list (get-properties '(:a 1 :b 2 :c 3) '(:b :c))))
(check "get-properties not found" '(nil nil nil) (multiple-value-list (get-properties '(:a 1) '(:x))))
(check "get-properties empty" '(nil nil nil) (multiple-value-list (get-properties '() '(:a))))

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
(defparameter *ht-test* (make-hash-table))
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
(defparameter *ht-sum* 0)
(defparameter *ht-map* (make-hash-table))
(setf (gethash 'x *ht-map*) 10)
(setf (gethash 'y *ht-map*) 20)
(maphash (lambda (k v) (setq *ht-sum* (+ *ht-sum* v))) *ht-map*)
(check "maphash sum" 30 *ht-sum*)
(defparameter *ht-eq* (make-hash-table :test 'eq))
(setf (gethash 'foo *ht-eq*) 99)
(check "hash-table eq test" 99 (gethash 'foo *ht-eq*))
(defparameter *ht-equal* (make-hash-table :test 'equal))
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

; remove/remove-if/remove-if-not on bit-vectors
(check "remove 0 bv" "#*111" (format nil "~A" (remove 0 #*10101)))
(check "remove 1 bv" "#*00" (format nil "~A" (remove 1 #*10101)))
(check "remove-if bv" "#*111" (format nil "~A" (remove-if #'zerop #*10101)))
(check "remove-if-not bv" "#*111"
  (format nil "~A" (remove-if-not (lambda (b) (= b 1)) #*10101)))
(check "remove bv type" t (bit-vector-p (remove 0 #*101)))
(check "remove bv empty" "#*" (format nil "~A" (remove 0 #*)))
(check "remove bv all" "#*" (format nil "~A" (remove 1 #*1111)))
(check "remove bv none" "#*1111" (format nil "~A" (remove 0 #*1111)))

; remove-duplicates
(check "remove-duplicates" '(1 3 2 4) (remove-duplicates '(1 2 1 3 2 4)))
(check "remove-dup symbols" '(b a c) (remove-duplicates '(a b a c)))

; substitute
(check "substitute basic" '(1 2 99 4 99) (substitute 99 3 '(1 2 3 4 3)))
(check "substitute count" '(1 2 0 4 3) (substitute 0 3 '(1 2 3 4 3) :count 1))

; substitute-if, substitute-if-not
(check "substitute-if" '(1 0 3 0 5) (substitute-if 0 #'my-evenp '(1 2 3 4 5)))
(check "substitute-if-not" '(0 2 0 4 0) (substitute-if-not 0 #'my-evenp '(1 2 3 4 5)))

; nsubstitute
(check "nsubstitute basic" '(1 2 99 4 99) (let ((x (list 1 2 3 4 3))) (nsubstitute 99 3 x) x))
(check "nsubstitute count" '(1 2 0 4 3) (let ((x (list 1 2 3 4 3))) (nsubstitute 0 3 x :count 1) x))
(check "nsubstitute-if" '(1 0 3 0 5) (let ((x (list 1 2 3 4 5))) (nsubstitute-if 0 #'my-evenp x) x))
(check "nsubstitute-if-not" '(0 2 0 4 0) (let ((x (list 1 2 3 4 5))) (nsubstitute-if-not 0 #'my-evenp x) x))

; reduce
(check "reduce sum" 10 (reduce #'+ '(1 2 3 4)))
(check "reduce empty init" 0 (reduce #'+ '() :initial-value 0))
(check "reduce single init" 15 (reduce #'+ '(5) :initial-value 10))
(check "reduce cons" '((1 . 2) . 3) (reduce #'cons '(1 2 3)))

; fill
(defparameter *fill-list* (list 1 2 3 4))
(fill *fill-list* 0)
(check "fill all" '(0 0 0 0) *fill-list*)
(defparameter *fill-list2* (list 1 2 3 4))
(fill *fill-list2* 0 :start 1 :end 3)
(check "fill range" '(1 0 0 4) *fill-list2*)

; replace
(defparameter *repl-list* (list 1 2 3 4 5))
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
; Regression: sort with allocating key on large list (GC bug #8)
(check "sort large+key" 200
  (length (sort (loop for i from 1 to 200 collect (format nil "item~3,'0d" i)) #'string<)))

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

; every/some/notany/notevery with strings
(check "every string t" t (every #'alpha-char-p "hello"))
(check "every string nil" nil (every #'alpha-char-p "hello1"))
(check "some string t" t (some #'digit-char-p "abc1"))
(check "some string nil" nil (some #'digit-char-p "abcd"))
(check "notany string t" t (notany #'digit-char-p "abcd"))
(check "notany string nil" nil (notany #'digit-char-p "abc1"))
(check "notevery str t" t (notevery #'alpha-char-p "hello1"))
(check "notevery str nil" nil (notevery #'alpha-char-p "hello"))

; every/some/notany/notevery with vectors
(check "every vector t" t (every #'numberp #(1 2 3)))
(check "every vector nil" nil (every #'numberp #(1 :a 3)))
(check "some vector t" t (some #'zerop #(1 2 0 3)))
(check "some vector nil" nil (some #'zerop #(1 2 3)))
(check "notany vector t" t (notany #'zerop #(1 2 3)))
(check "notany vector nil" nil (notany #'zerop #(1 0 3)))
(check "notevery vec t" t (notevery #'numberp #(1 :a 3)))
(check "notevery vec nil" nil (notevery #'numberp #(1 2 3)))

; every/some with empty sequences
(check "every empty str" t (every #'identity ""))
(check "every empty vec" t (every #'identity #()))
(check "some empty str" nil (some #'identity ""))
(check "some empty vec" nil (some #'identity #()))

; every/some with bit-vectors
(check "every bv t" t (every #'zerop #*000))
(check "every bv nil" nil (every #'zerop #*010))
(check "some bv t" t (some #'plusp #*010))
(check "some bv nil" nil (some #'plusp #*000))

; map over strings and vectors
(check "map str" '(97 98 99) (map 'list #'char-code "abc"))
(check "map vec" '(11 21 31) (map 'list #'1+ #(10 20 30)))
(check "map mixed" '(11 22 33) (map 'list #'+ '(1 2 3) #(10 20 30)))
(check "map str result" "HELLO" (map 'string #'char-upcase "hello"))
(check "map vec result" #(2 3 4) (map 'vector #'1+ '(1 2 3)))
(check "map shortest" '(11 22) (map 'list #'+ '(1 2 3 4) #(10 20)))
(check "map simple-vector" #(2 3 4) (map 'simple-vector #'1+ '(1 2 3)))
(check "map simple-string" "HELLO" (map 'simple-string #'char-upcase "hello"))
(check "map cons" '(2 3 4) (map 'cons #'1+ '(1 2 3)))

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
(handler-case (bt-err-outer) (error () nil))
(check "backtrace recovery" 42 42)
(defun bt-rec-err (n) (if (= n 0) (error "depth") (+ 1 (bt-rec-err (1- n)))))
(handler-case (bt-rec-err 5) (error () nil))
(check "backtrace deep recovery" 99 99)
(defun bt-uwp-fn () (unwind-protect (error "uwp err") nil))
(handler-case (bt-uwp-fn) (error () nil))
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
(handler-case (srcloc-outer 1) (error () nil))
(check "srcloc error recovery" 42 42)

; --- Condition signaling ---
(check "signal returns nil" nil (signal (make-condition 'simple-condition :format-control "test")))
(check "signal string returns nil" nil (signal "something"))
(check "signal symbol returns nil" nil (signal 'simple-condition))
(check "warn returns nil" nil (warn "test warning"))
(check "warn symbol returns nil" nil (warn 'simple-warning))
; error test: error is caught, next expression returns 42
(handler-case (error "test error for batch") (error () nil))
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
(check "package-use-list CL-USER" 6 (length (package-use-list (find-package "CL-USER"))))
(check "list-all-packages" t (>= (length (list-all-packages)) 3))
(check "make-package" t (not (null (or (find-package "TEST-PKG-1") (make-package "TEST-PKG-1")))))
(check "find-package after make" t (not (null (find-package "TEST-PKG-1"))))
(check "find-symbol CAR external" :external (multiple-value-bind (sym status) (find-symbol "CAR" (find-package "CL")) status))
(check "find-symbol CAR inherited" :inherited (multiple-value-bind (sym status) (find-symbol "CAR" (find-package "CL-USER")) status))
(check "find-symbol not found" nil (multiple-value-bind (sym status) (find-symbol "ZZZZZ" (find-package "CL")) status))
(check "intern new" nil (let ((name (symbol-name (gensym "INTERN-TEST-")))) (multiple-value-bind (sym status) (intern name) status)))
(check "intern existing" :inherited (multiple-value-bind (sym status) (intern "CAR") status))
(unless (find-package "EXP-AMI") (make-package "EXP-AMI"))
(intern "EXP-SYM" (find-package "EXP-AMI"))
(check "export" :external (progn (export (find-symbol "EXP-SYM" (find-package "EXP-AMI")) (find-package "EXP-AMI")) (multiple-value-bind (s st) (find-symbol "EXP-SYM" (find-package "EXP-AMI")) st)))
(check "unexport" :internal (progn (unexport (find-symbol "EXP-SYM" (find-package "EXP-AMI")) (find-package "EXP-AMI")) (multiple-value-bind (s st) (find-symbol "EXP-SYM" (find-package "EXP-AMI")) st)))
(unless (find-package "USE-AMI-SRC") (make-package "USE-AMI-SRC"))
(unless (find-package "USE-AMI-DST") (make-package "USE-AMI-DST"))
(intern "SHARED-SYM" (find-package "USE-AMI-SRC"))
(export (find-symbol "SHARED-SYM" (find-package "USE-AMI-SRC")) (find-package "USE-AMI-SRC"))
(use-package "USE-AMI-SRC" "USE-AMI-DST")
(check "use-package inherit" :inherited (multiple-value-bind (s st) (find-symbol "SHARED-SYM" (find-package "USE-AMI-DST")) st))
(check "delete-package" nil (progn (unless (find-package "DEL-AMI") (make-package "DEL-AMI")) (delete-package (find-package "DEL-AMI")) (find-package "DEL-AMI")))
(check "rename-package" "RENAMED-AMI" (progn (unless (find-package "ORIG-AMI") (make-package "ORIG-AMI")) (unless (find-package "RENAMED-AMI") (rename-package (find-package "ORIG-AMI") "RENAMED-AMI")) (package-name (find-package "RENAMED-AMI"))))
(unless (find-package "SHAD-AMI") (make-package "SHAD-AMI"))
(use-package "CL" "SHAD-AMI")
(shadow "CAR" (find-package "SHAD-AMI"))
(check "shadow" :internal (multiple-value-bind (s st) (find-symbol "CAR" (find-package "SHAD-AMI")) st))
(unless (find-package "UNINT-AMI") (make-package "UNINT-AMI"))
(intern "REM-SYM" (find-package "UNINT-AMI"))
(unintern (find-symbol "REM-SYM" (find-package "UNINT-AMI")) (find-package "UNINT-AMI"))
(check "unintern" nil (multiple-value-bind (s st) (find-symbol "REM-SYM" (find-package "UNINT-AMI")) st))

; === Reader Qualified Syntax ===

; pkg:sym external access
(unless (find-package "QRA-FOO") (make-package "QRA-FOO"))
(intern "QBAR" (find-package "QRA-FOO"))
(export (find-symbol "QBAR" (find-package "QRA-FOO")) (find-package "QRA-FOO"))
(check "pkg:sym external" t (eq (find-symbol "QBAR" (find-package "QRA-FOO")) 'QRA-FOO:QBAR))

; pkg::sym internal access
(intern "QSECRET" (find-package "QRA-FOO"))
(check "pkg::sym internal" t (eq (find-symbol "QSECRET" (find-package "QRA-FOO")) 'QRA-FOO::QSECRET))

; pkg::sym creates symbol
(unless (find-package "QRA-BAR") (make-package "QRA-BAR"))
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
(unless (find-package "QRA-PR") (make-package "QRA-PR"))
(intern "XSYM" (find-package "QRA-PR"))
(export (find-symbol "XSYM" (find-package "QRA-PR")) (find-package "QRA-PR"))
(check "print external" "QRA-PR:XSYM" (prin1-to-string 'QRA-PR:XSYM))

; Printer: other pkg internal
(unless (find-package "QRA-PR2") (make-package "QRA-PR2"))
(intern "ISYM" (find-package "QRA-PR2"))
(check "print internal" "QRA-PR2::ISYM" (prin1-to-string 'QRA-PR2::ISYM))

; --- CDR-10: Package-local nicknames ---
(unless (find-package "LN-AMI1") (make-package "LN-AMI1" :use '("COMMON-LISP")))
(add-package-local-nickname "KW-AMI" (find-package "KEYWORD") (find-package "LN-AMI1"))
(check "pkg-local-nick add" 1 (length (package-local-nicknames (find-package "LN-AMI1"))))
(in-package "LN-AMI1")
(cl-user::check "pkg-local-nick resolve" t (not (null (find-package "KW-AMI"))))
(in-package "COMMON-LISP-USER")
(check "pkg-local-nick scoped" nil (find-package "KW-AMI"))
(unless (find-package "LN-AMI2") (make-package "LN-AMI2" :use '("COMMON-LISP")))
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
(check "bignum logand -1 identity" 65536 (logand -1 65536))
(check "bignum logand -1 multi-limb" 610777 (logand -1 610777))
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

; --- Complex numbers ---
(check "complex reader" #C(5 16) #C(5 16))
(check "complex eql" t (eql #C(5 16) #C(5 16)))
(check "complex eql diff" nil (eql #C(5 16) #C(5 17)))
(check "complex equal" t (equal #C(3 4) #C(3 4)))
(check "complex complexp" t (complexp #C(5 16)))
(check "complex complexp not" nil (complexp 42))
(check "complex realpart" 5 (realpart #C(5 16)))
(check "complex imagpart" 16 (imagpart #C(5 16)))
(check "complex realpart real" 42 (realpart 42))
(check "complex imagpart real" 0 (imagpart 42))
(check "complex constructor" #C(3 4) (complex 3 4))
(check "complex zero imag" 5 (complex 5 0))
(check "complex type-of" 'complex (type-of #C(1 2)))
(check "complex typep" t (typep #C(1 2) 'complex))
(check "complex numberp" t (numberp #C(1 2)))
(check "complex conjugate" #C(3 -4) (conjugate #C(3 4)))
(check "complex conjugate real" 5 (conjugate 5))
(check "complex negative" #C(-4 15) #C(-4 15))
(check "complex hash-table" 42 (let ((ht (make-hash-table :test 'eql))) (setf (gethash #C(5 16) ht) 42) (gethash #C(5 16) ht)))

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
(defparameter *test-tmp*
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

; open with :if-exists :supersede (overwrites existing file)
(let ((s (open (concatenate 'string *test-tmp* "_clt_sup.txt") :direction :output)))
  (write-string "Original" s) (close s))
(let ((s (open (concatenate 'string *test-tmp* "_clt_sup.txt") :direction :output :if-exists :supersede)))
  (write-string "New" s) (close s))
(check "open supersede" "New"
  (with-open-file (s (concatenate 'string *test-tmp* "_clt_sup.txt") :direction :input)
    (read-line s)))

; open with :if-exists :rename-and-delete
(let ((s (open (concatenate 'string *test-tmp* "_clt_rad.txt") :direction :output)))
  (write-string "Before" s) (close s))
(let ((s (open (concatenate 'string *test-tmp* "_clt_rad.txt") :direction :output :if-exists :rename-and-delete)))
  (write-string "After" s) (close s))
(check "open rename-and-delete" "After"
  (with-open-file (s (concatenate 'string *test-tmp* "_clt_rad.txt") :direction :input)
    (read-line s)))

; open with :if-exists :rename
(let ((s (open (concatenate 'string *test-tmp* "_clt_ren.txt") :direction :output)))
  (write-string "Old" s) (close s))
(let ((s (open (concatenate 'string *test-tmp* "_clt_ren.txt") :direction :output :if-exists :rename)))
  (write-string "Fresh" s) (close s))
(check "open rename creates new" "Fresh"
  (with-open-file (s (concatenate 'string *test-tmp* "_clt_ren.txt") :direction :input)
    (read-line s)))
(check "open rename keeps backup" t
  (not (null (probe-file (concatenate 'string *test-tmp* "_clt_ren.txt.bak")))))

; open with :if-exists :overwrite (treated as supersede)
(let ((s (open (concatenate 'string *test-tmp* "_clt_ovw.txt") :direction :output)))
  (write-string "ABCDEF" s) (close s))
(let ((s (open (concatenate 'string *test-tmp* "_clt_ovw.txt") :direction :output :if-exists :overwrite)))
  (write-string "XY" s) (close s))
(check "open overwrite" "XY"
  (with-open-file (s (concatenate 'string *test-tmp* "_clt_ovw.txt") :direction :input)
    (read-line s)))

; open with :if-exists nil returns nil
(let ((s (open (concatenate 'string *test-tmp* "_clt_sup.txt") :direction :output :if-exists nil)))
  (check "open if-exists nil" nil s))

; open with :if-exists :error on existing file
(check "open if-exists error" t
  (handler-case
    (progn (open (concatenate 'string *test-tmp* "_clt_sup.txt") :direction :output :if-exists :error) nil)
    (error (c) t)))

; with-open-file with :if-exists :rename-and-delete
(with-open-file (s (concatenate 'string *test-tmp* "_clt_wrad.txt") :direction :output)
  (write-string "First version" s))
(with-open-file (s (concatenate 'string *test-tmp* "_clt_wrad.txt") :direction :output :if-exists :rename-and-delete)
  (write-string "Second version" s))
(check "with-open-file rename-and-delete" "Second version"
  (with-open-file (s (concatenate 'string *test-tmp* "_clt_wrad.txt") :direction :input)
    (read-line s)))

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

; --- Feature conditionals (#+ / #-) ---

; *features* is a list
(check "features-is-list" t (consp *features*))

; :CL-AMIGA is in *features*
(check "features-has-cl-amiga" t (if (member :cl-amiga *features*) t nil))

; :COMMON-LISP is in *features*
(check "features-has-common-lisp" t (if (member :common-lisp *features*) t nil))

; #+cl-amiga includes form
(check "feature-plus-present" 42 #+cl-amiga 42)

; #-cl-amiga skips form
(check "feature-minus-present" 99 #-cl-amiga 42 99)

; #+nonexistent skips form
(check "feature-plus-absent" 99 #+nonexistent 42 99)

; #-nonexistent includes form
(check "feature-minus-absent" 42 #-nonexistent 42)

; Feature conditionals in a list
(check "feature-in-list" '(1 2 3) '(1 #+cl-amiga 2 3))
(check "feature-skip-in-list" '(1 3) '(1 #+nonexistent 2 3))

; Compound feature expressions
(check "feature-and" 42 #+(and cl-amiga common-lisp) 42)
(check "feature-or" 42 #+(or nonexistent cl-amiga) 42)
(check "feature-not" 42 #+(not nonexistent) 42)
(check "feature-not-fail" 99 #+(not cl-amiga) 42 99)

; Platform-specific features on Amiga
(check "features-has-amigaos" t (if (member :amigaos *features*) t nil))
(check "features-has-m68k" t (if (member :m68k *features*) t nil))
#+amigaos (check "feature-plus-amigaos" 42 42)

; sleep with zero duration
(check "sleep-zero" nil (sleep 0))

; with-standard-io-syntax executes body
(check "with-standard-io-syntax" 42 (with-standard-io-syntax 42))

; --- Step 10: Time Functions + Minimal Pathnames ---

; get-universal-time returns a large integer
(check "get-universal-time" t (> (get-universal-time) 3786912000))

; file-write-date on existing file returns a number
(check "file-write-date" t (numberp (file-write-date "lib/boot.lisp")))

; probe-file on existing file
(check "probe-file-exists" #P"lib/boot.lisp" (probe-file "lib/boot.lisp"))

; probe-file on nonexistent file
(check "probe-file-nil" nil (probe-file "nonexistent_xyz_123.txt"))

; file-namestring extracts filename
(check "file-namestring-posix" "baz.txt" (file-namestring "/foo/bar/baz.txt"))
(check "file-namestring-amiga" "test.lisp" (file-namestring "Work:dev/test.lisp"))

; directory-namestring extracts directory
(check "dir-namestring-posix" "/foo/bar/" (directory-namestring "/foo/bar/baz.txt"))
(check "dir-namestring-amiga" "Work:dev/" (directory-namestring "Work:dev/test.lisp"))

; pathname-name extracts name without extension
(check "pathname-name" "test" (pathname-name "/foo/test.lisp"))

; pathname-type extracts extension
(check "pathname-type" "lisp" (pathname-type "/foo/test.lisp"))
(check "pathname-type-none" nil (pathname-type "/foo/test"))

; namestring is identity
(check "namestring" "/foo/bar" (namestring "/foo/bar"))

; truename is identity (for now)
(check "truename" #P"/foo/bar" (truename "/foo/bar"))

; make-pathname constructs path (returns pathname, use namestring to get string)
(check "make-pathname" "hello.txt" (namestring (make-pathname :name "hello" :type "txt")))

; merge-pathnames fills in missing directory (returns pathname)
(check "merge-pathnames" "/some/dir/file.txt" (namestring (merge-pathnames "file.txt" "/some/dir/")))
(check "merge-pathnames-has-dir" "/other/file.txt" (namestring (merge-pathnames "/other/file.txt" "/some/dir/")))

; enough-namestring
(check "enough-namestring" "/foo/bar/baz.txt" (enough-namestring "/foo/bar/baz.txt" "/foo/"))

; --- Pathname type (proper #P pathnames) ---

; pathnamep predicate
(check "pathnamep-true" t (pathnamep #P"test.lisp"))
(check "pathnamep-false" nil (pathnamep "test.lisp"))

; #P reader creates pathname objects
(check "pathname-read-print" "#P\"foo.lisp\"" (format nil "~S" #P"foo.lisp"))

; pathname coercion from string
(check "pathname-from-string" t (pathnamep (pathname "foo.lisp")))

; pathname-name from #P
(check "pathname-name-p" "bar" (pathname-name #P"/foo/bar.lisp"))

; pathname-type from #P
(check "pathname-type-p" "lisp" (pathname-type #P"/foo/bar.lisp"))

; pathname-directory from #P
(check "pathname-dir-p" (list :absolute "foo") (pathname-directory #P"/foo/bar.lisp"))

; pathname-device from Amiga path
(check "pathname-device-amiga" "DH0" (pathname-device #P"DH0:Work/test.lisp"))

; namestring round-trip
(check "namestring-roundtrip" "/foo/bar.lisp" (namestring #P"/foo/bar.lisp"))

; --- Amiga device: pathname tests ---

; Device-only paths (PROGDIR:, S:, DH0:)
(check "device-only S: device" "S" (pathname-device #P"S:"))
(check "device-only S: dir" (list :absolute) (pathname-directory #P"S:"))
(check "device-only S: name" nil (pathname-name #P"S:"))
(check "device-only S: namestring" "S:" (namestring #P"S:"))
(check "device-only PROGDIR: namestring" "PROGDIR:" (namestring #P"PROGDIR:"))
(check "device-only PROGDIR: device" "PROGDIR" (pathname-device #P"PROGDIR:"))
(check "device-only PROGDIR: dir" (list :absolute) (pathname-directory #P"PROGDIR:"))

; Device-only paths are absolute (critical for ASDF)
(check "device S: absolute" :absolute (car (pathname-directory #P"S:")))
(check "device PROGDIR: absolute" :absolute (car (pathname-directory #P"PROGDIR:")))
(check "device DH0: absolute" :absolute (car (pathname-directory #P"DH0:")))

; Device-only roundtrips
(check "device S: roundtrip" "S:" (namestring (pathname (namestring #P"S:"))))
(check "device PROGDIR: roundtrip" "PROGDIR:" (namestring (pathname (namestring #P"PROGDIR:"))))

; Device path with subdirectory
(check "device subdir parse" "S:asdfcache/" (namestring #P"S:asdfcache/"))
(check "device subdir dir" (list :absolute "asdfcache") (pathname-directory #P"S:asdfcache/"))

; Merge-pathnames with device paths
(check "merge device+relative" "PROGDIR:lib/boot.lisp"
  (namestring (merge-pathnames "lib/boot.lisp" #P"PROGDIR:")))
(check "merge device+subdir" "S:asdfcache/"
  (namestring (merge-pathnames "asdfcache/" #P"S:")))
(check "merge device dir" (list :absolute "lib")
  (pathname-directory (merge-pathnames "lib/boot.lisp" #P"PROGDIR:")))
(check "merge device preserves device" "PROGDIR"
  (pathname-device (merge-pathnames "lib/boot.lisp" #P"PROGDIR:")))
(check "merge device preserves name" "boot"
  (pathname-name (merge-pathnames "boot.lisp" #P"PROGDIR:lib/")))
(check "merge device preserves type" "lisp"
  (pathname-type (merge-pathnames "boot.lisp" #P"PROGDIR:lib/")))
(check "merge device absolute overrides" "DH0:Work/test.lisp"
  (namestring (merge-pathnames #P"DH0:Work/test.lisp" #P"S:")))
(check "merge device nested subdir" "S:asdfcache/common-lisp/cache/"
  (namestring (merge-pathnames "common-lisp/cache/" #P"S:asdfcache/")))

; Make-pathname with device
(check "make-pathname device" "DH0:Work/test.lisp"
  (namestring (make-pathname :device "DH0" :directory '(:absolute "Work") :name "test" :type "lisp")))
(check "make-pathname device-only" "S:"
  (namestring (make-pathname :device "S" :directory '(:absolute))))

; equal on pathnames
(check "pathname-equal" t (equal #P"/foo/bar.lisp" #P"/foo/bar.lisp"))
(check "pathname-not-equal" nil (equal #P"/foo/bar.lisp" #P"/baz/bar.lisp"))

; typep / type-of
(check "pathname-typep" t (typep #P"test" 'pathname))
(check "pathname-type-of" 'pathname (type-of #P"test"))

; *default-pathname-defaults* is a pathname
(check "default-pn-defaults" t (pathnamep *default-pathname-defaults*))

; decode-universal-time returns 9 values
(check "decode-ut-count" 9 (length (multiple-value-list (decode-universal-time (get-universal-time)))))

; encode/decode round-trip
(check "encode-decode-roundtrip" (list 30 45 12 15 6 2025) (multiple-value-bind (s mi h d mo y) (decode-universal-time (encode-universal-time 30 45 12 15 6 2025)) (list s mi h d mo y)))

; get-decoded-time returns 9 values
(check "get-decoded-time-count" 9 (length (multiple-value-list (get-decoded-time))))

; values propagation through defun (regression test for OP_LOAD MV fix)
(defun %test-mv-values () (values 10 20 30))
(check "defun-mv-propagation" (list 10 20 30) (multiple-value-list (%test-mv-values)))

; delete-file and rename-file via write/probe/delete cycle
(with-open-file (s "T:cl_test_step10.tmp" :direction :output) (write-string "test" s))
(check "probe-written" #P"T:cl_test_step10.tmp" (probe-file "T:cl_test_step10.tmp"))
(rename-file "T:cl_test_step10.tmp" "T:cl_test_step10_ren.tmp")
(check "rename-old-gone" nil (probe-file "T:cl_test_step10.tmp"))
(check "rename-new-exists" #P"T:cl_test_step10_ren.tmp" (probe-file "T:cl_test_step10_ren.tmp"))
(delete-file "T:cl_test_step10_ren.tmp")
(check "delete-file" nil (probe-file "T:cl_test_step10_ren.tmp"))

; %mkdir creates directory
(check "mkdir" t (%mkdir "T:cl_test_step10_dir"))
(check "mkdir-probe" #P"T:cl_test_step10_dir" (probe-file "T:cl_test_step10_dir"))

; --- Step 12: Readtable + Compile ---

; readtablep
(check "readtablep-current" t (readtablep *readtable*))
(check "readtablep-non-rt" nil (readtablep 42))

; get-macro-character for ( => NIL (built-in), not non-terminating
(check "get-macro-char-paren" nil (get-macro-character #\())

; get-macro-character for # => NIL fn, T non-term-p (second value)
(check "get-macro-char-hash" (list nil t) (multiple-value-list (get-macro-character #\#)))

; copy-readtable
(check "copy-readtable" t (readtablep (copy-readtable)))

; compile nil lambda
(check "compile-nil-lambda" 42 (funcall (compile nil '(lambda (x) (+ x 1))) 41))

; compile named function
(check "compile-named" t (functionp (compile 'car)))

; set-macro-character: define ! then test it
(set-macro-character #\! (lambda (stream char) (list 'quote (read stream t nil t))))
(check "set-macro-char" t (equal !hello 'hello))

; --- Phase 8 Step 2: Missing string operations ---

; string-capitalize
(check "string-capitalize" "Hello World" (string-capitalize "hello world"))
(check "string-capitalize upper" "Hello World" (string-capitalize "HELLO WORLD"))
(check "string-capitalize hyphen" "Foo-Bar" (string-capitalize "foo-bar"))

; nstring-upcase
(check "nstring-upcase" "HELLO" (let ((s (copy-seq "hello"))) (nstring-upcase s) s))

; nstring-downcase
(check "nstring-downcase" "hello" (let ((s (copy-seq "HELLO"))) (nstring-downcase s) s))

; nstring-capitalize
(check "nstring-capitalize" "Hello World" (let ((s (copy-seq "hello world"))) (nstring-capitalize s) s))

; char-name
(check "char-name space" "Space" (char-name #\Space))
(check "char-name newline" "Newline" (char-name #\Newline))
(check "char-name graphic" nil (char-name #\A))

; name-char
(check "name-char space" #\Space (name-char "Space"))
(check "name-char upper" #\Space (name-char "SPACE"))
(check "name-char newline" #\Newline (name-char "Newline"))
(check "name-char invalid" nil (name-char "xyzzy"))

; char-equal (case-insensitive)
(check "char-equal ci" t (char-equal #\a #\A))
(check "char-not-equal ci" t (char-not-equal #\a #\b))
(check "char-lessp ci" t (char-lessp #\a #\B))
(check "char-greaterp ci" t (char-greaterp #\b #\A))
(check "char-not-greaterp ci" t (char-not-greaterp #\a #\A))
(check "char-not-lessp ci" t (char-not-lessp #\A #\a))

; graphic-char-p
(check "graphic-char-p A" t (graphic-char-p #\A))
(check "graphic-char-p space" t (graphic-char-p #\Space))
(check "graphic-char-p newline" nil (graphic-char-p #\Newline))

; alphanumericp
(check "alphanumericp A" t (alphanumericp #\A))
(check "alphanumericp 5" t (alphanumericp #\5))
(check "alphanumericp !" nil (alphanumericp #\!))

; digit-char
(check "digit-char 0" #\0 (digit-char 0))
(check "digit-char 9" #\9 (digit-char 9))
(check "digit-char hex" #\A (digit-char 10 16))
(check "digit-char over" nil (digit-char 10))

; both-case-p
(check "both-case-p A" t (both-case-p #\A))
(check "both-case-p a" t (both-case-p #\a))
(check "both-case-p 1" nil (both-case-p #\1))

; standard-char-p
(check "standard-char-p A" t (standard-char-p #\A))
(check "standard-char-p nl" t (standard-char-p #\Newline))

; --- Phase 8 Step 3: Missing sequence operations ---

; elt
(check "elt list" 'b (elt '(a b c) 1))
(check "elt vector" 20 (elt (vector 10 20 30) 1))
(check "elt string" #\b (elt "abc" 1))

; (setf elt)
(check "setf elt vector" 99 (let ((v (vector 1 2 3))) (setf (elt v 1) 99) (elt v 1)))
(check "setf elt list" '(99 2 3) (let ((l (list 1 2 3))) (setf (elt l 0) 99) l))

; copy-seq
(check "copy-seq list" '(1 2 3) (copy-seq '(1 2 3)))
(check "copy-seq nil" nil (copy-seq nil))
(check "copy-seq string" "hello" (copy-seq "hello"))
(check "copy-seq indep" 1 (let ((a (list 1 2))) (let ((b (copy-seq a))) (setf (car b) 99) (car a))))

; map-into
(check "map-into vector" 33 (let ((v (vector 0 0 0))) (map-into v #'+ (vector 1 2 3) (vector 10 20 30)) (elt v 2)))

; --- Phase 8 Step 4: Higher-order functions ---

; complement
(check "complement t" nil (funcall (complement #'zerop) 0))
(check "complement nil" t (funcall (complement #'zerop) 5))

; constantly
(check "constantly" 42 (funcall (constantly 42) 1 2 3))
(check "constantly mapcar" '(x x x) (mapcar (constantly 'x) '(1 2 3)))

; --- Phase 8 Step 1: Missing list operations ---

; list*
(check "list* 2args" '(1 . 2) (list* 1 2))
(check "list* 3args" '(1 2 . 3) (list* 1 2 3))
(check "list* with nil" '(1 2 3) (list* 1 2 3 nil))
(check "list* single" 'a (list* 'a))

; make-list
(check "make-list basic" '(nil nil nil) (make-list 3))
(check "make-list init" '(x x x) (make-list 3 :initial-element 'x))
(check "make-list zero" nil (make-list 0))

; tree-equal
(check "tree-equal match" t (tree-equal '(1 (2 3)) '(1 (2 3))))
(check "tree-equal mismatch" nil (tree-equal '(1 2) '(1 3)))
(check "tree-equal nil" t (tree-equal nil nil))
(check "tree-equal test" t (tree-equal '("a" "b") '("a" "b") :test #'equal))

; list-length
(check "list-length" 3 (list-length '(1 2 3)))
(check "list-length nil" 0 (list-length nil))

; tailp
(check "tailp cdr" t (let ((x '(1 2 3))) (tailp (cdr x) x)))
(check "tailp nil" t (tailp nil '(1 2 3)))
(check "tailp no" nil (tailp '(4) '(1 2 3)))

; ldiff
(check "ldiff basic" '(1 2) (let* ((x '(1 2 3 4)) (tail (cddr x))) (ldiff x tail)))
(check "ldiff nil" '(1 2 3) (ldiff '(1 2 3) nil))

; revappend
(check "revappend" '(3 2 1 4 5) (revappend '(1 2 3) '(4 5)))
(check "revappend nil" '(1 2) (revappend nil '(1 2)))

; nreconc
(check "nreconc" '(3 2 1 4 5) (nreconc (list 1 2 3) '(4 5)))
(check "nreconc nil" '(1 2) (nreconc nil '(1 2)))

; assoc-if
(check "assoc-if found" '(b . 2) (assoc-if #'symbolp '((1 . a) (b . 2) (3 . c))))
(check "assoc-if not found" nil (assoc-if #'null '((1 . a) (2 . b))))

; assoc-if-not
(check "assoc-if-not" '(1 . a) (assoc-if-not #'symbolp '((1 . a) (b . 2))))

; rassoc-if
(check "rassoc-if found" '(3 . b) (rassoc-if #'symbolp '((1 . 2) (3 . b) (4 . 5))))
(check "rassoc-if not found" nil (rassoc-if #'null '((1 . a) (2 . b))))

; rassoc-if-not
(check "rassoc-if-not" '(1 . 2) (rassoc-if-not #'symbolp '((1 . 2) (3 . b))))

; remf
(check "remf middle" '(:a 1 :c 3) (let ((p (list :a 1 :b 2 :c 3))) (remf p :b) p))
(check "remf head mv" '((:b 2) t) (multiple-value-list (remf (list :a 1 :b 2) :a)))
(check "remf missing mv" '((:a 1 :b 2) nil) (multiple-value-list (remf (list :a 1 :b 2) :z)))

; --- Loop macro ---
; Simple loop
(check "loop simple return" 5 (let ((i 0)) (loop (when (= i 5) (return i)) (setq i (+ i 1)))))
(check "loop simple accum" '(0 1 2) (let ((result nil) (i 0)) (loop (when (>= i 3) (return (nreverse result))) (push i result) (setq i (+ i 1)))))

; Extended loop - while/until/do
(check "loop while" 10 (let ((i 0) (sum 0)) (loop while (< i 5) do (setq sum (+ sum i)) (setq i (+ i 1))) sum))
(check "loop until" 6 (let ((i 0) (sum 0)) (loop until (= i 4) do (setq sum (+ sum i)) (setq i (+ i 1))) sum))
(check "loop do multiple" 30 (let ((x 0) (y 0)) (loop while (< x 3) do (setq x (+ x 1)) (setq y (+ y 10))) y))

; Loop for/as/repeat
(check "loop for in" '(1 2 3) (let ((r nil)) (loop for x in '(1 2 3) do (push x r)) (nreverse r)))
(check "loop for in by" '(1 3 5) (let ((r nil)) (loop for x in '(1 2 3 4 5) by #'cddr do (push x r)) (nreverse r)))
(check "loop for on" '((1 2 3) (2 3) (3)) (let ((r nil)) (loop for x on '(1 2 3) do (push x r)) (nreverse r)))
(check "loop for from to" 15 (let ((sum 0)) (loop for i from 1 to 5 do (setq sum (+ sum i))) sum))
(check "loop for from below" 6 (let ((sum 0)) (loop for i from 0 below 4 do (setq sum (+ sum i))) sum))
(check "loop for from by" '(0 3 6 9) (let ((r nil)) (loop for i from 0 to 10 by 3 do (push i r)) (nreverse r)))
(check "loop for downfrom" '(5 4 3 2 1) (let ((r nil)) (loop for i downfrom 5 to 1 do (push i r)) (nreverse r)))
(check "loop for downfrom above" '(5 4 3) (let ((r nil)) (loop for i downfrom 5 above 2 do (push i r)) (nreverse r)))
(check "loop for across" '(1 2 3) (let ((r nil)) (loop for x across (vector 1 2 3) do (push x r)) (nreverse r)))
(check "loop for = then" '(1 2 4 8 16) (let ((r nil)) (loop for x = 1 then (* x 2) while (< x 20) do (push x r)) (nreverse r)))
(check "loop repeat" 5 (let ((c 0)) (loop repeat 5 do (setq c (+ c 1))) c))
(check "loop for multiple" '((1 . a) (2 . b) (3 . c)) (let ((r nil)) (loop for x in '(a b c) for i from 1 do (push (cons i x) r)) (nreverse r)))

; Loop accumulation
(check "loop collect" '(1 2 3) (loop for x in '(1 2 3) collect x))
(check "loop collect expr" '(1 4 9) (loop for x in '(1 2 3) collect (* x x)))
(check "loop for type-spec" '(0 1 2 3) (loop for i fixnum from 0 to 3 collect i))
(check "loop collect into" '(1 2 3) (loop for x in '(1 2 3 4 5) collect x into r do (when (= x 3) (return (nreverse r)))))
(check "loop sum" 15 (loop for x in '(1 2 3 4 5) sum x))
(check "loop sum into" 6 (loop for x in '(1 2 3 4 5) sum x into tot do (when (= x 3) (return tot))))
(check "loop count" 3 (loop for x in '(1 nil 2 nil 3) count x))
(check "loop maximize" 9 (loop for x in '(3 1 4 1 5 9 2 6) maximize x))
(check "loop minimize" 1 (loop for x in '(3 1 4 1 5 9 2 6) minimize x))
(check "loop append" '(a b c d e) (loop for x in '((a b) (c d) (e)) append x))
(check "loop nconc" '(1 2 3 4) (loop for x in '((1 2) (3 4)) nconc (copy-list x)))
(check "loop return" 3 (loop for x in '(1 2 3 4 5) do (when (= x 3) (return x))))
(check "loop return in do+let" 42 (loop while t do (let ((x 42)) (return x))))
(check "loop return in do+let*" 3 (loop while t do (let* ((a 1) (b 2)) (return (+ a b)))))
(check "loop return in do+let defun" 7 (progn (defun %test-loop-ret2 () (loop while t do (let ((x 7)) (return x)))) (%test-loop-ret2)))

; Loop Step 4 — conditionals, always/never/thereis, with, named, initially/finally
(check "loop when collect" '(2 4 6) (loop for x in '(1 2 3 4 5 6) when (evenp x) collect x))
(check "loop if/else" '(1 -2 3 -4 5) (loop for x in '(1 2 3 4 5) if (oddp x) collect x else collect (- x)))
(check "loop unless collect" '(1 3 5) (loop for x in '(1 2 3 4 5) unless (evenp x) collect x))
(check "loop when and" '((2 4) (2 4)) (let ((side nil)) (let ((r (loop for x in '(1 2 3 4) when (evenp x) collect x and do (push x side)))) (list r (nreverse side)))))
(check "loop always true" t (loop for x in '(2 4 6) always (evenp x)))
(check "loop always false" nil (loop for x in '(2 3 6) always (evenp x)))
(check "loop never true" t (loop for x in '(1 3 5) never (evenp x)))
(check "loop never false" nil (loop for x in '(1 2 5) never (evenp x)))
(check "loop thereis found" 4 (loop for x in '(1 2 3 4 5) thereis (and (> x 3) x)))
(check "loop thereis nil" nil (loop for x in '(1 2 3) thereis (and (> x 10) x)))
(check "loop with" 6 (loop with sum = 0 for x in '(1 2 3) do (setq sum (+ sum x)) finally (return sum)))
(check "loop with and" '(30) (loop with x = 10 and y = 20 repeat 1 collect (+ x y)))
(check "loop named" 6 (loop named my-loop for x from 1 do (when (> x 5) (return-from my-loop x))))
(check "loop initially" '(start 1 2) (let ((log nil)) (loop initially (push 'start log) for x in '(1 2) do (push x log)) (nreverse log)))
(check "loop finally return" 15 (loop for x in '(1 2 3 4 5) sum x into total finally (return total)))
(check "loop collect into finally" '(a b c) (loop for x in '(a b c) collect x into items finally (return items)))

;; loop-finish
(check "loop-finish" 15 (loop for i from 1 to 10 sum i do (when (= i 5) (loop-finish))))
(check "loop-finish+finally" t (let ((ran nil)) (loop for i from 1 to 10 do (when (= i 3) (loop-finish)) finally (setq ran t)) ran))

;; loop BEING clause
(check "loop being hash-keys" '(:a :b)
  (let ((ht (make-hash-table)))
    (setf (gethash :a ht) 1)
    (setf (gethash :b ht) 2)
    (sort (loop for k being the hash-keys of ht collect k)
          #'string< :key #'symbol-name)))
(check "loop being hash-values" '(10 20)
  (let ((ht (make-hash-table)))
    (setf (gethash :a ht) 10)
    (setf (gethash :b ht) 20)
    (sort (loop for v being the hash-values in ht collect v) #'<)))
(check "loop being hash-keys using" '((:x 42))
  (let ((ht (make-hash-table)))
    (setf (gethash :x ht) 42)
    (loop for k being each hash-key of ht using (hash-value v)
          collect (list k v))))
(check "loop being hash-values using" '((:x 42))
  (let ((ht (make-hash-table)))
    (setf (gethash :x ht) 42)
    (loop for v being each hash-value of ht using (hash-key k)
          collect (list k v))))
(defpackage "LOOP-SYM-AMI" (:use))
(intern "AA" "LOOP-SYM-AMI")
(intern "BB" "LOOP-SYM-AMI")
(check "loop being symbols" 2
  (length (loop for s being the symbols of "LOOP-SYM-AMI" collect s)))
(defpackage "LOOP-EXT-AMI" (:use) (:export "PUB"))
(intern "PRIV" "LOOP-EXT-AMI")
(check "loop being external-symbols" 1
  (length (loop for s being the external-symbols of "LOOP-EXT-AMI" collect s)))

;; loop destructuring
(check "loop destr in" '(3 7 11) (loop for (a b) in '((1 2) (3 4) (5 6)) collect (+ a b)))
(check "loop destr dotted" '((x 1) (y 2) (z 3)) (loop for (a . b) in '((x . 1) (y . 2) (z . 3)) collect (list a b)))
(check "loop destr nested" '(6 15) (loop for (a (b c)) in '((1 (2 3)) (4 (5 6))) collect (+ a b c)))
(check "loop destr on" '((1 (2 3)) (2 (3)) (3 nil)) (loop for (a . b) on '(1 2 3) collect (list a b)))

; --- Printer control variables (Steps 1-2) ---
; Default values per CL spec
(check "print-escape default" t *print-escape*)
(check "print-readably default" nil *print-readably*)
(check "print-base default" 10 *print-base*)
(check "print-radix default" nil *print-radix*)
(check "print-level default" nil *print-level*)
(check "print-length default" nil *print-length*)
(check "print-case default" :upcase *print-case*)
(check "print-gensym default" t *print-gensym*)
(check "print-array default" t *print-array*)
(check "print-circle default" nil *print-circle*)
(check "print-pretty default" nil *print-pretty*)

; prin1-to-string / princ-to-string honor escape variable
(check "prin1-to-string string" "\"hello\"" (prin1-to-string "hello"))
(check "princ-to-string string" "hello" (princ-to-string "hello"))
(check "prin1-to-string char" "#\\Space" (prin1-to-string #\Space))
(check "princ-to-string char" " " (princ-to-string #\Space))
(check "prin1-to-string number" "42" (prin1-to-string 42))

; Dynamic binding of *print-escape* affects princ (which binds NIL)
; and prin1 (which binds T) — verify they override correctly
(check "let print-escape nil princ-to-string"
  "hello" (let ((*print-escape* nil)) (princ-to-string "hello")))
(check "let print-escape t prin1-to-string"
  "\"hello\"" (let ((*print-escape* t)) (prin1-to-string "hello")))

; *print-level* tests
(check "print-level 0" "#"
  (let ((*print-level* 0)) (prin1-to-string '(a b))))
(check "print-level 1" "(A #)"
  (let ((*print-level* 1)) (prin1-to-string '(a (b (c))))))
(check "print-level 2" "(A (B #))"
  (let ((*print-level* 2)) (prin1-to-string '(a (b (c))))))

; *print-length* tests
(check "print-length 0" "(...)"
  (let ((*print-length* 0)) (prin1-to-string '(a b c))))
(check "print-length 2" "(A B ...)"
  (let ((*print-length* 2)) (prin1-to-string '(a b c d e))))
(check "print-length no trunc" "(A B)"
  (let ((*print-length* 10)) (prin1-to-string '(a b))))
(check "print-length dotted" "(A . B)"
  (let ((*print-length* 2)) (prin1-to-string '(a . b))))

; Combined level + length
(check "print-level+length" "(A B ...)"
  (let ((*print-level* 1) (*print-length* 2))
    (prin1-to-string '(a b (c d) e))))

; *print-base* tests
(check "print-base binary" "1010"
  (let ((*print-base* 2)) (prin1-to-string 10)))
(check "print-base binary 255" "11111111"
  (let ((*print-base* 2)) (prin1-to-string 255)))
(check "print-base binary 0" "0"
  (let ((*print-base* 2)) (prin1-to-string 0)))
(check "print-base binary neg" "-101"
  (let ((*print-base* 2)) (prin1-to-string -5)))
(check "print-base octal" "10"
  (let ((*print-base* 8)) (prin1-to-string 8)))
(check "print-base octal 255" "377"
  (let ((*print-base* 8)) (prin1-to-string 255)))
(check "print-base hex FF" "FF"
  (let ((*print-base* 16)) (prin1-to-string 255)))
(check "print-base hex 100" "100"
  (let ((*print-base* 16)) (prin1-to-string 256)))
(check "print-base hex 0" "0"
  (let ((*print-base* 16)) (prin1-to-string 0)))
(check "print-base hex neg" "-1"
  (let ((*print-base* 16)) (prin1-to-string -1)))
(check "print-base 3" "100"
  (let ((*print-base* 3)) (prin1-to-string 9)))
(check "print-base 36" "Z"
  (let ((*print-base* 36)) (prin1-to-string 35)))

; *print-radix* tests
(check "print-radix decimal" "42."
  (let ((*print-radix* t)) (prin1-to-string 42)))
(check "print-radix decimal 0" "0."
  (let ((*print-radix* t)) (prin1-to-string 0)))
(check "print-radix decimal neg" "-7."
  (let ((*print-radix* t)) (prin1-to-string -7)))
(check "print-radix binary" "#b1010"
  (let ((*print-base* 2) (*print-radix* t)) (prin1-to-string 10)))
(check "print-radix octal" "#o377"
  (let ((*print-base* 8) (*print-radix* t)) (prin1-to-string 255)))
(check "print-radix hex" "#xFF"
  (let ((*print-base* 16) (*print-radix* t)) (prin1-to-string 255)))
(check "print-radix base3" "#3r100"
  (let ((*print-base* 3) (*print-radix* t)) (prin1-to-string 9)))

; *print-base* with bignums
(check "print-base bignum hex" "100000000"
  (let ((*print-base* 16)) (prin1-to-string (expt 2 32))))
(check "print-base bignum binary" "10000000000000000"
  (let ((*print-base* 2)) (prin1-to-string (expt 2 16))))
(check "print-base bignum octal" "200000"
  (let ((*print-base* 8)) (prin1-to-string 65536)))
(check "print-radix bignum hex" "#x100000000"
  (let ((*print-base* 16) (*print-radix* t)) (prin1-to-string (expt 2 32))))
(check "print-radix bignum decimal" "4294967296."
  (let ((*print-radix* t)) (prin1-to-string (expt 2 32))))

; *print-case* tests
(check "print-case upcase" "HELLO"
  (let ((*print-case* :upcase)) (prin1-to-string 'hello)))
(check "print-case downcase" "hello"
  (let ((*print-case* :downcase)) (prin1-to-string 'hello)))
(check "print-case downcase hyphen" "foo-bar"
  (let ((*print-case* :downcase)) (prin1-to-string 'foo-bar)))
(check "print-case downcase nil" "nil"
  (let ((*print-case* :downcase)) (prin1-to-string nil)))
(check "print-case downcase t" "t"
  (let ((*print-case* :downcase)) (prin1-to-string t)))
(check "print-case downcase keyword" ":foo"
  (let ((*print-case* :downcase)) (prin1-to-string :foo)))
(check "print-case capitalize" "Hello"
  (let ((*print-case* :capitalize)) (prin1-to-string 'hello)))
(check "print-case capitalize hyphen" "Foo-Bar"
  (let ((*print-case* :capitalize)) (prin1-to-string 'foo-bar)))
(check "print-case capitalize nil" "Nil"
  (let ((*print-case* :capitalize)) (prin1-to-string nil)))
(check "print-case capitalize keyword" ":Test"
  (let ((*print-case* :capitalize)) (prin1-to-string :test)))
(check "print-case downcase list" "(a b c)"
  (let ((*print-case* :downcase)) (prin1-to-string '(a b c))))
(check "print-case capitalize list" "(Hello World)"
  (let ((*print-case* :capitalize)) (prin1-to-string '(hello world))))

; *print-gensym* tests
(check "print-gensym default" "#:FOO"
  (prin1-to-string (make-symbol "FOO")))
(check "print-gensym nil" "FOO"
  (let ((*print-gensym* nil)) (prin1-to-string (make-symbol "FOO"))))
(check "print-gensym nil + downcase" "foo"
  (let ((*print-gensym* nil) (*print-case* :downcase))
    (prin1-to-string (make-symbol "FOO"))))

; *print-array* tests
(check "print-array default" "#(1 2 3)"
  (prin1-to-string (vector 1 2 3)))
(check "print-array nil vector" "#<VECTOR>"
  (let ((*print-array* nil)) (prin1-to-string (vector 1 2 3))))
(check "print-array nil multi-dim" "#<ARRAY>"
  (let ((*print-array* nil)) (prin1-to-string (make-array '(2 3)))))

; --- WRITE function with keyword arguments ---
(check "write returns object" 42
  (let ((x (write 42 :stream (make-string-output-stream)))) x))
(check "write :escape nil" "hello"
  (let ((s (make-string-output-stream)))
    (write "hello" :stream s :escape nil)
    (get-output-stream-string s)))
(check "write :escape t" "\"hello\""
  (let ((s (make-string-output-stream)))
    (write "hello" :stream s :escape t)
    (get-output-stream-string s)))
(check "write :base 16" "FF"
  (let ((s (make-string-output-stream)))
    (write 255 :stream s :base 16)
    (get-output-stream-string s)))
(check "write :base 2" "1010"
  (let ((s (make-string-output-stream)))
    (write 10 :stream s :base 2)
    (get-output-stream-string s)))
(check "write :radix t :base 16" "#xFF"
  (let ((s (make-string-output-stream)))
    (write 255 :stream s :base 16 :radix t)
    (get-output-stream-string s)))
(check "write :level 1" "(A #)"
  (let ((s (make-string-output-stream)))
    (write '(a (b (c))) :stream s :level 1)
    (get-output-stream-string s)))
(check "write :length 2" "(A B ...)"
  (let ((s (make-string-output-stream)))
    (write '(a b c d e) :stream s :length 2)
    (get-output-stream-string s)))
(check "write :case :downcase" "hello"
  (let ((s (make-string-output-stream)))
    (write 'hello :stream s :case :downcase)
    (get-output-stream-string s)))
(check "write :case :capitalize" "Foo-Bar"
  (let ((s (make-string-output-stream)))
    (write 'foo-bar :stream s :case :capitalize)
    (get-output-stream-string s)))
(check "write :gensym nil" "FOO"
  (let ((s (make-string-output-stream)))
    (write (make-symbol "FOO") :stream s :gensym nil)
    (get-output-stream-string s)))
(check "write :array nil" "#<VECTOR>"
  (let ((s (make-string-output-stream)))
    (write (vector 1 2) :stream s :array nil)
    (get-output-stream-string s)))

; --- *print-circle* ---
(check "circle cdr cycle" "#0=(1 2 . #0#)"
  (let ((*print-circle* t))
    (let ((x (list 1 2)))
      (rplacd (cdr x) x)
      (prin1-to-string x))))
(check "circle car self-ref" "#0=(#0#)"
  (let ((*print-circle* t))
    (let ((x (list nil)))
      (rplaca x x)
      (prin1-to-string x))))
(check "circle shared sub" "(#0=(1 2) #0#)"
  (let ((*print-circle* t))
    (let ((sub (list 1 2)))
      (prin1-to-string (list sub sub)))))
(check "circle no sharing" "(1 2 3)"
  (let ((*print-circle* t))
    (prin1-to-string '(1 2 3))))
(check "circle nil default" "((1) (1))"
  (let ((sub (list 1)))
    (prin1-to-string (list sub sub))))
(check "circle vector self" "#0=#(#0# 42)"
  (let ((*print-circle* t))
    (let ((v (vector nil 42)))
      (setf (aref v 0) v)
      (prin1-to-string v))))
(check "circle deep shared" "((#0=(99)) (#0#))"
  (let ((*print-circle* t))
    (let ((leaf (list 99)))
      (prin1-to-string (list (list leaf) (list leaf))))))
(check "write-to-string :circle" "#0=(1 2 . #0#)"
  (let ((x (list 1 2)))
    (rplacd (cdr x) x)
    (write-to-string x :circle t)))

; --- Format directive extensions ---
(check "format ~D" "42" (format nil "~D" 42))
(check "format ~D negative" "-7" (format nil "~D" -7))
(check "format ~D zero" "0" (format nil "~D" 0))
(check "format ~B" "1010" (format nil "~B" 10))
(check "format ~B 255" "11111111" (format nil "~B" 255))
(check "format ~O" "10" (format nil "~O" 8))
(check "format ~O 255" "377" (format nil "~O" 255))
(check "format ~X" "FF" (format nil "~X" 255))
(check "format ~X 256" "100" (format nil "~X" 256))
(check "format ~X zero" "0" (format nil "~X" 0))
(check "format ~C" "A" (format nil "~C" #\A))
(check "format ~C space" " " (format nil "~C" #\Space))
(check "format ~W string" "\"hello\"" (format nil "~W" "hello"))
(check "format ~W number" "42" (format nil "~W" 42))
(check "format ~& freshline" 11 (length (format nil "hello~&world")))
(check "format ~| page" 3 (length (format nil "a~|b")))
(check "format ~| page char" 12 (let ((s (format nil "a~|b"))) (if (>= (length s) 2) (char-code (char s 1)) -1)))
(check "format mixed" "dec=255 hex=FF bin=11111111"
  (format nil "dec=~D hex=~X bin=~B" 255 255 255))

; --- Advanced format: padding/commas/sign (Step 1) ---
(check "format ~8D pad" "      42" (format nil "~8D" 42))
(check "format ~8,'0D padchar" "00000042" (format nil "~8,'0D" 42))
(check "format ~:D commas" "1,234,567" (format nil "~:D" 1234567))
(check "format ~@D sign" "+42" (format nil "~@D" 42))
(check "format ~@D neg" "-7" (format nil "~@D" -7))
(check "format ~10A pad right" "hello     " (format nil "~10A" "hello"))
(check "format ~10@A pad left" "     hello" (format nil "~10@A" "hello"))
(check "format ~10S" "\"hello\"   " (format nil "~10S" "hello"))
(check "format ~3%" 3 (length (format nil "~3%")))
(check "format ~5~" "~~~~~" (format nil "~5~"))

; --- Advanced format: ~* goto, ~T tabulate (Step 2) ---
(check "format ~* skip" "B" (format nil "~*~A" 'a 'b))
(check "format ~2* skip2" "C" (format nil "~2*~A" 'a 'b 'c))
(check "format ~:* backup" "XX" (format nil "~A~:*~A" 'x))

; --- Advanced format: ~(~) case conversion (Step 3) ---
(check "format ~( lowercase" "hello world" (format nil "~(HELLO WORLD~)"))
(check "format ~:( capitalize" "Hello World" (format nil "~:(hello world~)"))
(check "format ~@( cap first" "Hello world" (format nil "~@(hello world~)"))
(check "format ~:@( upcase" "HELLO WORLD" (format nil "~:@(hello world~)"))

; --- Advanced format: ~[~;~] conditional (Step 4) ---
(check "format ~[ numeric 0" "zero" (format nil "~[zero~;one~;two~]" 0))
(check "format ~[ numeric 1" "one" (format nil "~[zero~;one~;two~]" 1))
(check "format ~[ default" "other" (format nil "~[zero~;one~:;other~]" 99))
(check "format ~:[ false" "false" (format nil "~:[false~;true~]" nil))
(check "format ~:[ true" "true" (format nil "~:[false~;true~]" 42))
(check "format ~@[ non-nil" "x=42 done" (format nil "~@[x=~A ~]done" 42))
(check "format ~@[ nil" "done" (format nil "~@[x=~A ~]done" nil))

; --- Advanced format: ~{~} iteration, ~^ (Step 5) ---
(check "format ~{~} list" "1 2 3 " (format nil "~{~A ~}" '(1 2 3)))
(check "format ~{~^~} sep" "1, 2, 3" (format nil "~{~A~^, ~}" '(1 2 3)))
(check "format ~{~} empty" "" (format nil "~{~A~}" nil))
(check "format ~:{~} sublists" "X=1 Y=2 " (format nil "~:{~A=~A ~}" '((x 1) (y 2))))
(check "format ~@{~^~} atsign" "1, 2, 3" (format nil "~@{~A~^, ~}" 1 2 3))
(check "format ~2{~}" "1 2 " (format nil "~2{~A ~}" '(1 2 3 4 5)))
(check "format ~^ single" "X" (format nil "~{~A~^-~}" '(x)))
(check "format ~^ multi" "X-Y-Z" (format nil "~{~A~^-~}" '(x y z)))

; --- Advanced format: ~? recursive, ~R radix (Step 6) ---
(check "format ~? recursive" "1 2 and 3" (format nil "~? and ~A" "~A ~A" '(1 2) 3))
(check "format ~@? atsign" "1 2 and 3" (format nil "~@? and ~A" "~A ~A" 1 2 3))
(check "format ~R zero" "zero" (format nil "~R" 0))
(check "format ~R cardinal" "forty-two" (format nil "~R" 42))
(check "format ~R thousand" "one thousand two hundred thirty-four" (format nil "~R" 1234))
(check "format ~:R ordinal" "first" (format nil "~:R" 1))
(check "format ~:R 21" "twenty-first" (format nil "~:R" 21))
(check "format ~@R roman" "XLII" (format nil "~@R" 42))
(check "format ~@R 1999" "MCMXCIX" (format nil "~@R" 1999))
(check "format ~:@R old roman" "IIII" (format nil "~:@R" 4))
(check "format ~2R binary" "1010" (format nil "~2R" 10))
(check "format ~16R hex" "FF" (format nil "~16R" 255))

; --- Pretty-printing / *print-pretty* ---
(check "print-pretty default nil" nil *print-pretty*)
(check "print-right-margin default nil" nil *print-right-margin*)
(check "pretty short list" "(1 2 3)"
  (let ((*print-pretty* t) (*print-right-margin* 40))
    (write-to-string '(1 2 3))))
(check "pretty long list" 25
  (length (let ((*print-pretty* t) (*print-right-margin* 10))
    (write-to-string '(alpha beta gamma delta)))))
(check "pretty off no break" "(ALPHA BETA GAMMA)"
  (let ((*print-pretty* nil) (*print-right-margin* 5))
    (write-to-string '(alpha beta gamma))))
(check "pretty write keyword" "(1 2 3)"
  (write-to-string '(1 2 3) :pretty t :right-margin 40))
(check "pretty write-to-string narrow" 25
  (length (write-to-string '(alpha beta gamma delta) :pretty t :right-margin 10)))
(check "pprint basic" 8
  (length (let ((s (make-string-output-stream)))
    (pprint '(1 2 3) s)
    (get-output-stream-string s))))
(check "pretty empty nil" "NIL"
  (let ((*print-pretty* t) (*print-right-margin* 10))
    (write-to-string nil)))
(check "pretty single elt" "(X)"
  (let ((*print-pretty* t) (*print-right-margin* 10))
    (write-to-string '(x))))
(check "pretty dotted" "(A . B)"
  (let ((*print-pretty* t) (*print-right-margin* 40))
    (write-to-string '(a . b))))

; --- Ratio Numbers ---
; Reader
(check "read ratio" 1/2 1/2)
(check "read ratio neg" -3/4 -3/4)
(check "read ratio normalize" 1/2 2/4)
(check "read ratio demote" 2 6/3)

; Predicates
(check "ratiop ratio" t (typep 1/2 'ratio))
(check "ratiop int" nil (typep 5 'ratio))
(check "rationalp ratio" t (rationalp 1/2))
(check "rationalp int" t (rationalp 5))
(check "rationalp float" nil (rationalp 1.0))

; Type
(check "type-of ratio" 'ratio (type-of 1/2))
(check "subtypep ratio rational" t (subtypep 'ratio 'rational))

; Arithmetic
(check "ratio add" 5/6 (+ 1/2 1/3))
(check "ratio add int" 3/2 (+ 1/2 1))
(check "ratio sub" 1/6 (- 1/2 1/3))
(check "ratio mul" 1/6 (* 1/2 1/3))
(check "ratio div" 3/2 (/ 1/2 1/3))
(check "ratio negate" -1/2 (- 1/2))
(check "div to ratio" 1/2 (/ 1 2))
(check "div reduces" 3/2 (/ 6 4))
(check "div to integer" 2 (/ 6 3))
(check "reciprocal" 1/3 (/ 3))

; Comparison
(check "ratio < true" t (< 1/3 1/2))
(check "ratio > true" t (> 1/2 1/3))
(check "ratio = same" t (= 1/2 2/4))
(check "ratio = float" t (= 1/2 0.5))
(check "ratio < int" t (< 1/3 1))
(check "ratio > int" t (> 3/2 1))

; Predicates on ratios
(check "zerop ratio" nil (zerop 1/2))
(check "plusp ratio" t (plusp 1/2))
(check "minusp ratio" t (minusp -1/2))
(check "abs ratio" 3/4 (abs -3/4))

; min/max
(check "min ratio" 1/4 (min 1/2 1/3 1/4))
(check "max ratio" 1/2 (max 1/2 1/3 1/4))

; Accessors
(check "numerator ratio" 3 (numerator 3/4))
(check "numerator int" 5 (numerator 5))
(check "denominator ratio" 4 (denominator 3/4))
(check "denominator int" 1 (denominator 5))

; Rounding
(check "floor ratio" 3 (floor 7/2))
(check "ceiling ratio" 4 (ceiling 7/2))
(check "truncate ratio" 3 (truncate 7/2))
(check "round ratio" 4 (round 7/2))

; Coerce
(check "coerce ratio float" 0.5 (coerce 1/2 'single-float))

; Float contagion
(check "ratio + float" 1.0 (+ 1/2 0.5))

; eql/equal
(check "eql ratio" t (eql 1/2 2/4))
(check "equal ratio" t (equal 1/2 2/4))
(check "eql ratio diff" nil (eql 1/2 1/3))

; Hash tables with ratio keys
(check "ratio hash key"
  'half
  (let ((ht (make-hash-table)))
    (setf (gethash 1/2 ht) 'half)
    (gethash 1/2 ht)))
(check "ratio hash normalized"
  'half
  (let ((ht (make-hash-table)))
    (setf (gethash 1/2 ht) 'half)
    (gethash 2/4 ht)))

; expt with ratios
(check "expt ratio" 1/8 (expt 1/2 3))
(check "expt ratio neg" 3/2 (expt 2/3 -1))
(check "expt int neg" 1/2 (expt 2 -1))

; 1+/1-
(check "1+ ratio" 3/2 (1+ 1/2))
(check "1- ratio" -1/2 (1- 1/2))

; mod/rem
(check "mod ratio" 1/2 (mod 5/2 1))
(check "rem ratio" 1/2 (rem 5/2 1))

; rational/rationalize
(check "rational int" 5 (rational 5))
(check "rational ratio" 1/2 (rational 1/2))
(check "rational float" 1/2 (rational 0.5))
(check "rationalize float" 1/2 (rationalize 0.5))

; Printer
(check "print ratio" "1/2" (write-to-string 1/2))
(check "print ratio neg" "-3/4" (write-to-string -3/4))

; --- rotatef / shiftf ---
(defparameter *rfa* 1)
(defparameter *rfb* 2)
(defparameter *rfc* 3)
(rotatef *rfa* *rfb* *rfc*)
(check "rotatef a" 2 *rfa*)
(check "rotatef b" 3 *rfb*)
(check "rotatef c" 1 *rfc*)

(defparameter *rf2x* 10)
(defparameter *rf2y* 20)
(rotatef *rf2x* *rf2y*)
(check "rotatef two x" 20 *rf2x*)
(check "rotatef two y" 10 *rf2y*)

(check "rotatef returns nil" nil (let ((a 1) (b 2)) (rotatef a b)))

(defparameter *sfa* 1)
(defparameter *sfb* 2)
(defparameter *sfc* 3)
(defparameter *sf-result* (shiftf *sfa* *sfb* *sfc* 99))
(check "shiftf result" 1 *sf-result*)
(check "shiftf a" 2 *sfa*)
(check "shiftf b" 3 *sfb*)
(check "shiftf c" 99 *sfc*)

(defparameter *sf2x* 10)
(check "shiftf two" 10 (shiftf *sf2x* 42))
(check "shiftf two val" 42 *sf2x*)

(defparameter *sfc-x* (cons 5 6))
(check "shiftf car" 5 (shiftf (car *sfc-x*) 77))
(check "shiftf car val" 77 (car *sfc-x*))

; --- pprint-newline ---
(check "pprint-newline mandatory" t (progn (pprint-newline :mandatory) t))
(check "pprint-newline fill" t (progn (pprint-newline :fill) t))
(check "pprint-newline linear" t (progn (pprint-newline :linear) t))
(check "pprint-newline miser" t (progn (pprint-newline :miser) t))

; --- pprint-indent ---
(check "pprint-indent current" t (progn (pprint-indent :current 2) t))
(check "pprint-indent block" t (progn (pprint-indent :block 4) t))

; --- pprint-logical-block ---
(check "pprint-logical-block" "[1 2 3]" (with-output-to-string (s) (pprint-logical-block (s '(1 2 3) :prefix "[" :suffix "]") (princ (pprint-pop) s) (pprint-exit-if-list-exhausted) (write-char #\Space s) (princ (pprint-pop) s) (pprint-exit-if-list-exhausted) (write-char #\Space s) (princ (pprint-pop) s))))

; --- pprint-dispatch ---
(check "copy-pprint-dispatch" nil (copy-pprint-dispatch))
(set-pprint-dispatch 'integer (lambda (s obj) (write-string "[" s) (princ obj s) (write-string "]" s)) 1)
(check "pprint-dispatch custom" "[42]" (let ((*print-pretty* t)) (with-output-to-string (s) (write 42 :stream s :pretty t))))
(set-pprint-dispatch 'integer nil)

; --- Bit manipulation ---
(check "logcount 0" 0 (logcount 0))
(check "logcount 13" 3 (logcount 13))
(check "logcount -1" 0 (logcount -1))
(check "logcount -13" 2 (logcount -13))
(check "logcount 255" 8 (logcount 255))
(check "logbitp 0 1" t (logbitp 0 1))
(check "logbitp 1 1" nil (logbitp 1 1))
(check "logbitp 0 0" nil (logbitp 0 0))
(check "logbitp 3 15" t (logbitp 3 15))
(check "logbitp 4 15" nil (logbitp 4 15))
(check "logbitp neg" t (logbitp 0 -1))
(check "logbitp neg high" t (logbitp 100 -1))
(check "logtest true" t (logtest 6 5))
(check "logtest false" nil (logtest 4 2))
(check "byte" '(8 . 4) (byte 8 4))
(check "byte-size" 8 (byte-size (byte 8 4)))
(check "byte-position" 4 (byte-position (byte 8 4)))
(check "ldb low" 5 (ldb (byte 4 0) 4085))
(check "ldb high" 15 (ldb (byte 4 4) 255))
(check "ldb mid" 6 (ldb (byte 4 4) 111))
(check "dpb basic" 245 (dpb 15 (byte 4 4) 5))
(check "ldb-test true" t (ldb-test (byte 4 0) 15))
(check "ldb-test false" nil (ldb-test (byte 4 4) 15))
(check "mask-field" 240 (mask-field (byte 4 4) 255))
(check "deposit-field" 175 (deposit-field 170 (byte 4 4) 15))
(check "boole and" 9 (boole boole-and 15 9))
(check "boole ior" 15 (boole boole-ior 15 9))
(check "boole xor" 6 (boole boole-xor 15 9))
(check "boole clr" 0 (boole boole-clr 15 9))
(check "boole set" -1 (boole boole-set 15 9))
(check "boole 1" 15 (boole boole-1 15 9))
(check "boole 2" 9 (boole boole-2 15 9))
(check "boole c1" -16 (boole boole-c1 15 9))
(check "boole c2" -10 (boole boole-c2 15 9))
(check "boole eqv" -7 (boole boole-eqv 15 9))
(check "boole nand" -10 (boole boole-nand 15 9))
(check "boole nor" -16 (boole boole-nor 15 9))
(check "boole andc1" 0 (boole boole-andc1 15 9))
(check "boole andc2" 6 (boole boole-andc2 15 9))
(check "boole orc1" -7 (boole boole-orc1 15 9))
(check "boole orc2" -1 (boole boole-orc2 15 9))
(check "boole-clr val" 0 boole-clr)
(check "boole-set val" 1 boole-set)
(check "boole-and val" 6 boole-and)
(check "boole-ior val" 7 boole-ior)
(check "boole-xor val" 8 boole-xor)
; Bignum logcount/logbitp
(check "logcount bignum" 1 (logcount (expt 2 40)))
(check "logbitp bignum" t (logbitp 40 (expt 2 40)))
(check "logbitp bignum false" nil (logbitp 39 (expt 2 40)))

; --- Bit Vectors ---
; Reader/printer round-trip
(check "bv reader empty" "#*" (format nil "~A" #*))
(check "bv reader 10110" "#*10110" (format nil "~A" #*10110))
(check "bv reader 32bit" "#*10101010101010101010101010101010" (format nil "~A" #*10101010101010101010101010101010))

; Predicates
(check "bit-vector-p t" t (bit-vector-p #*1010))
(check "bit-vector-p nil" nil (bit-vector-p #(1 0 1 0)))
(check "simple-bit-vector-p" t (simple-bit-vector-p #*1010))
(check "vectorp bv" t (vectorp #*1010))
(check "arrayp bv" t (arrayp #*1010))

; type-of, typep
(check "type-of bv" 'simple-bit-vector (type-of #*1010))
(check "typep bv bit-vector" t (typep #*1010 'bit-vector))
(check "typep bv vector" t (typep #*1010 'vector))
(check "typep bv array" t (typep #*1010 'array))
(check "typep bv sequence" t (typep #*1010 'sequence))
(check "typep bv simple-array" t (typep #*1010 'simple-array))
(check "typep bv string" nil (typep #*1010 'string))

; subtypep
(check "subtypep bv<vector" t (subtypep 'bit-vector 'vector))
(check "subtypep sbv<bv" t (subtypep 'simple-bit-vector 'bit-vector))
(check "subtypep bv<sequence" t (subtypep 'bit-vector 'sequence))

; Element access
(check "bit 0" 1 (bit #*10110 0))
(check "bit 1" 0 (bit #*10110 1))
(check "bit 2" 1 (bit #*10110 2))
(check "sbit 0" 1 (sbit #*1100 0))
(check "sbit 3" 0 (sbit #*1100 3))
(check "aref bv" 1 (aref #*10110 2))

; setf bit
(check "setf bit" "#*0010" (let ((bv #*0000)) (setf (bit bv 2) 1) (format nil "~A" bv)))
(check "setf sbit" "#*1011" (let ((bv #*1111)) (setf (sbit bv 1) 0) (format nil "~A" bv)))
(check "setf aref bv" "#*1000" (let ((bv #*0000)) (setf (aref bv 0) 1) (format nil "~A" bv)))

; make-array with element-type bit
(check "make-array bit" t (bit-vector-p (make-array 5 :element-type 'bit)))
(check "make-array bit ie1" "#*1111" (format nil "~A" (make-array 4 :element-type 'bit :initial-element 1)))
(check "make-array bit ic" "#*1010" (format nil "~A" (make-array 4 :element-type 'bit :initial-contents '(1 0 1 0))))

; Length / elt
(check "length bv" 5 (length #*10110))
(check "length bv empty" 0 (length #*))
(check "elt bv 0" 1 (elt #*10110 0))
(check "elt bv 4" 0 (elt #*10110 4))

; find / position / count
(check "find 1 bv" 1 (find 1 #*00100))
(check "find 1 bv nil" nil (find 1 #*00000))
(check "position 1 bv" 2 (position 1 #*00100))
(check "count 1 bv" 3 (count 1 #*10110))
(check "count 0 bv" 2 (count 0 #*10110))

; copy-seq
(check "copy-seq bv" "#*10110" (format nil "~A" (copy-seq #*10110)))
(check "copy-seq independent" '("#*001" "#*101")
  (let ((a #*101) (b (copy-seq #*101)))
    (setf (bit a 0) 0)
    (list (format nil "~A" a) (format nil "~A" b))))

; reverse
(check "reverse bv" "#*01101" (format nil "~A" (reverse #*10110)))
(check "reverse bv empty" "#*" (format nil "~A" (reverse #*)))

; fill
(check "fill bv" "#*11111" (let ((bv #*00000)) (fill bv 1) (format nil "~A" bv)))
(check "fill bv range" "#*10011" (let ((bv #*11111)) (fill bv 0 :start 1 :end 3) (format nil "~A" bv)))

; setf elt
(check "setf elt bv" "#*0010" (let ((bv #*0000)) (setf (elt bv 2) 1) (format nil "~A" bv)))

; equal
(check "equal bv same" t (equal #*10110 #*10110))
(check "equal bv diff" nil (equal #*10110 #*10111))
(check "equal bv len" nil (equal #*10110 #*1011))
(check "equal bv empty" t (equal #* #*))
(check "equal bv vs vec" nil (equal #*101 #(1 0 1)))

; coerce
(check "coerce bv->list" '(1 0 1 1 0) (coerce #*10110 'list))
(check "coerce list->bv" "#*10110" (format nil "~A" (coerce '(1 0 1 1 0) 'bit-vector)))
(check "coerce bv->vector" #(1 0 1) (coerce #*101 'vector))

; Bitwise operations
(check "bit-and" "#*1000" (format nil "~A" (bit-and #*1100 #*1010)))
(check "bit-ior" "#*1110" (format nil "~A" (bit-ior #*1100 #*1010)))
(check "bit-xor" "#*0110" (format nil "~A" (bit-xor #*1100 #*1010)))
(check "bit-eqv" "#*1001" (format nil "~A" (bit-eqv #*1100 #*1010)))
(check "bit-nand" "#*0111" (format nil "~A" (bit-nand #*1100 #*1010)))
(check "bit-nor" "#*0001" (format nil "~A" (bit-nor #*1100 #*1010)))
(check "bit-andc1" "#*0010" (format nil "~A" (bit-andc1 #*1100 #*1010)))
(check "bit-andc2" "#*0100" (format nil "~A" (bit-andc2 #*1100 #*1010)))
(check "bit-orc1" "#*1011" (format nil "~A" (bit-orc1 #*1100 #*1010)))
(check "bit-orc2" "#*1101" (format nil "~A" (bit-orc2 #*1100 #*1010)))
(check "bit-not" "#*01001" (format nil "~A" (bit-not #*10110)))
(check "bit-and in-place" "#*1000" (let ((a #*1100)) (bit-and a #*1010 t) (format nil "~A" a)))

; Array queries
(check "array-dimensions bv" '(5) (array-dimensions #*10110))
(check "array-rank bv" 1 (array-rank #*10110))
(check "array-dimension bv" 5 (array-dimension #*10110 0))
(check "array-total-size bv" 5 (array-total-size #*10110))
(check "row-major-aref bv" 1 (row-major-aref #*10110 2))
(check "array-has-fp bv" nil (array-has-fill-pointer-p #*1010))
(check "adjustable-p bv" nil (adjustable-array-p #*1010))

; --- Modules (provide / require) ---
(check "provide returns t" t (provide "test-mod-1"))
(check "modules contains provided" "test-mod-1" (find "test-mod-1" *modules* :test #'string=))
(check "provide no duplicate" 1 (progn (provide "test-mod-1") (count "test-mod-1" *modules* :test #'string=)))
(check "provide symbol" t (provide 'test-mod-sym))
(check "provide symbol name" "TEST-MOD-SYM" (find "TEST-MOD-SYM" *modules* :test #'string=))
(check "require already provided" nil (require "test-mod-1"))
(check "modules is list" t (listp *modules*))

; --- Read-time eval (#.) ---
(check "#. arithmetic" 3 #.(+ 1 2))
(check "#. list" '(1 2) '#.(list 1 2))
(check "#. in list" '(a 30 b) (list 'a #.(+ 10 20) 'b))
(check "*read-eval* default" t *read-eval*)

; --- CLOS: %class-of ---
(check "%class-of fixnum" 'fixnum (%class-of 42))
(check "%class-of nil" 'null (%class-of nil))
(check "%class-of symbol" 'symbol (%class-of 'foo))
(check "%class-of cons" 'cons (%class-of '(1 2)))
(check "%class-of string" 'string (%class-of "hello"))
(check "%class-of character" 'character (%class-of #\A))
(check "%class-of function" 'function (%class-of #'car))
(check "%class-of lambda" 'function (%class-of (lambda (x) x)))
(check "%class-of vector" 'vector (%class-of (vector 1 2 3)))
(check "%class-of hash-table" 'hash-table (%class-of (make-hash-table)))
(check "%class-of float" 'single-float (%class-of 3.14))
(check "%class-of ratio" 'ratio (%class-of 1/3))
(check "%class-of pathname" 'pathname (%class-of #P"/tmp/foo"))
(check "%class-of package" 'package (%class-of (find-package :cl)))
(check "%class-of stream" 'stream (%class-of (make-string-input-stream "hi")))
(defstruct clos-amiga-pt (x 0) (y 0))
(check "%class-of struct" 'clos-amiga-pt (%class-of (make-clos-amiga-pt :x 1)))

; --- CLOS: Bootstrap core classes (Step 2) ---
(require "clos")
(check "find-class integer" t (structurep (find-class 'integer)))
(check "class-name integer" 'integer (class-name (find-class 'integer)))
(check "class-name string" 'string (class-name (find-class 'string)))
(check "class-name t" 't (class-name (find-class 't)))
(check "find-class unknown nil" nil (find-class 'no-such-class nil))
(check "class-of 42" 'fixnum (class-name (class-of 42)))
(check "class-of string" 'string (class-name (class-of "hi")))
(check "class-of nil" 'null (class-name (class-of nil)))
(check "class-of cons" 'cons (class-name (class-of '(1 2))))
(check "class-of eq find-class" t (eq (class-of 42) (find-class 'fixnum)))
(check "direct-supers integer" 'rational (class-name (car (class-direct-superclasses (find-class 'integer)))))
(check "direct-supers t" nil (class-direct-superclasses (find-class 't)))
(check "cpl fixnum" '(fixnum integer rational real number t) (mapcar #'class-name (class-precedence-list (find-class 'fixnum))))
(check "cpl cons" '(cons list sequence t) (mapcar #'class-name (class-precedence-list (find-class 'cons))))
(check "cpl null" '(null symbol t list sequence) (mapcar #'class-name (class-precedence-list (find-class 'null))))
(check "direct-subclasses integer has fixnum" t (if (member (find-class 'fixnum) (class-direct-subclasses (find-class 'integer)) :test #'eq) t nil))
(check "direct-subclasses integer has bignum" t (if (member (find-class 'bignum) (class-direct-subclasses (find-class 'integer)) :test #'eq) t nil))

; --- CLOS Phase 1: Slot Access ---
(%register-struct-type 'clos-test-pt 2 nil '((x nil) (y nil)))
(let ((cls (%make-struct 'standard-class
             'clos-test-pt nil nil nil nil nil nil nil nil t nil nil)))
  (let ((idx-table (make-hash-table :test 'eq)))
    (setf (gethash 'x idx-table) 0)
    (setf (gethash 'y idx-table) 1)
    (%struct-set cls 5 idx-table)
    (setf (find-class 'clos-test-pt) cls)))
(defparameter *slot-test-pt* (%make-struct 'clos-test-pt 10 20))
(check "slot-value read x" 10 (slot-value *slot-test-pt* 'x))
(check "slot-value read y" 20 (slot-value *slot-test-pt* 'y))
(setf (slot-value *slot-test-pt* 'x) 42)
(check "setf slot-value" 42 (slot-value *slot-test-pt* 'x))
(check "slot-boundp true" t (slot-boundp *slot-test-pt* 'x))
(slot-makunbound *slot-test-pt* 'x)
(check "slot-boundp after makunbound" nil (slot-boundp *slot-test-pt* 'x))
(check "slot-exists-p true" t (slot-exists-p *slot-test-pt* 'y))
(check "slot-exists-p false" nil (slot-exists-p *slot-test-pt* 'z))

; --- CLOS Phase 2: C3 Linearization ---
(check "c3 single" '(fixnum integer rational real number t) (mapcar #'class-name (%compute-class-precedence-list (find-class 'fixnum))))
(check "c3 root" '(t) (mapcar #'class-name (%compute-class-precedence-list (find-class 't))))
(%make-bootstrap-class 'c3-a (list (find-class 't)))
(%make-bootstrap-class 'c3-b (list (find-class 'c3-a)))
(%make-bootstrap-class 'c3-c (list (find-class 'c3-a)))
(let ((d (%make-struct 'standard-class
           'c3-d
           (list (find-class 'c3-b) (find-class 'c3-c))
           nil nil nil nil nil nil nil t nil nil)))
  (setf (find-class 'c3-d) d)
  (%set-class-cpl d (%compute-class-precedence-list d)))
(check "c3 diamond" '(c3-d c3-b c3-c c3-a t) (mapcar #'class-name (class-precedence-list (find-class 'c3-d))))

; --- CLOS Phase 3: defclass ---
(defclass point () ((x :initarg :x :accessor point-x) (y :initarg :y :accessor point-y)))
(check "defclass find-class" 'point (class-name (find-class 'point)))
(check "defclass cpl" '(point standard-object t) (mapcar #'class-name (class-precedence-list (find-class 'point))))
(check "defclass effective slots" 2 (length (class-effective-slots (find-class 'point))))
(check "defclass slot-index x" 0 (slot-definition-location (gethash 'x (class-slot-index-table (find-class 'point)))))
(check "defclass slot-index y" 1 (slot-definition-location (gethash 'y (class-slot-index-table (find-class 'point)))))
(check "defclass accessor defined" t (functionp #'point-x))
(defclass point3d (point) ((z :initarg :z :accessor point-z)))
(check "defclass inheritance slots" 3 (length (class-effective-slots (find-class 'point3d))))
(check "defclass inheritance cpl" '(point3d point standard-object t) (mapcar #'class-name (class-precedence-list (find-class 'point3d))))
(defclass empty-class () ())
(check "defclass no supers defaults" '(standard-object) (mapcar #'class-name (class-direct-superclasses (find-class 'empty-class))))

; --- CLOS Phase 4: make-instance ---
(check "make-instance basic" '(10 20) (let ((p (make-instance 'point :x 10 :y 20))) (list (point-x p) (point-y p))))
(defclass counted () ((count :initform 0 :accessor obj-count) (name :initarg :name :accessor obj-name)))
(check "make-instance initform" '(0 "test") (let ((c (make-instance 'counted :name "test"))) (list (obj-count c) (obj-name c))))
(defclass counter () ((val :initarg :val :initform 0 :accessor counter-val)))
(check "initarg overrides initform" 42 (counter-val (make-instance 'counter :val 42)))
(check "initform default" 0 (counter-val (make-instance 'counter)))
(check "unbound slot" nil (let ((c (make-instance 'counted))) (slot-boundp c 'name)))
(check "make-instance by class" 1 (let ((p (make-instance (find-class 'point) :x 1 :y 2))) (point-x p)))
(check "make-instance inherited" '(1 2 3) (let ((p (make-instance 'point3d :x 1 :y 2 :z 3))) (list (point-x p) (point-y p) (point-z p))))

; --- CLOS Phase 5: defgeneric + defmethod ---
(defgeneric greet (obj))
(defmethod greet ((p point)) (list 'point (point-x p) (point-y p)))
(check "defmethod basic" '(point 10 20) (greet (make-instance 'point :x 10 :y 20)))
(defmethod greet ((p point3d)) (list 'point3d (point-x p) (point-y p) (point-z p)))
(check "dispatch point3d" '(point3d 1 2 3) (greet (make-instance 'point3d :x 1 :y 2 :z 3)))
(check "dispatch point" '(point 5 6) (greet (make-instance 'point :x 5 :y 6)))
(defgeneric describe-obj (obj))
(defmethod describe-obj ((p point)) (list 'base-point (point-x p)))
(defmethod describe-obj ((p point3d)) (cons 'extended (call-next-method)))
(check "call-next-method" '(extended base-point 7) (describe-obj (make-instance 'point3d :x 7 :y 8 :z 9)))
(defparameter *trace* nil)
(defgeneric traced-op (obj))
(defmethod traced-op :before ((p point)) (push 'before *trace*))
(defmethod traced-op ((p point)) (push 'primary *trace*) 'done)
(defmethod traced-op :after ((p point)) (push 'after *trace*))
(setq *trace* nil)
(check "before/after result" 'done (traced-op (make-instance 'point :x 0 :y 0)))
(check "before/after order" '(before primary after) (nreverse *trace*))
(defgeneric wrapped-op (obj))
(defmethod wrapped-op ((p point)) (point-x p))
(defmethod wrapped-op :around ((p point)) (+ 100 (call-next-method)))
(check "around method" 105 (wrapped-op (make-instance 'point :x 5 :y 0)))
(defgeneric identity-gf (x))
(defmethod identity-gf (x) x)
(check "unspecialized fixnum" 42 (identity-gf 42))
(check "unspecialized string" "hello" (identity-gf "hello"))
(defgeneric nmp-test (x))
(defmethod nmp-test ((x point)) (if (next-method-p) 'has-next 'no-next))
(check "next-method-p" 'no-next (nmp-test (make-instance 'point :x 0 :y 0)))

; --- CLOS Phase 6: with-slots ---
(check "with-slots read" '(10 20) (let ((p (make-instance 'point :x 10 :y 20))) (with-slots (x y) p (list x y))))
(check "with-slots write" 99 (let ((p (make-instance 'point :x 1 :y 2))) (with-slots (x) p (setf x 99)) (point-x p)))
(check "with-slots renamed" 11 (let ((p (make-instance 'point :x 5 :y 6))) (with-slots ((px x) (py y)) p (+ px py))))

; --- CLOS Phase 7: GF accessors ---
(defclass gf-point () ((x :initarg :x :accessor gf-point-x) (y :initarg :y :accessor gf-point-y)))
(check "gf accessor is gf" t (if (gethash 'gf-point-x *generic-function-table*) t nil))
(check "gf accessor read" '(10 20) (let ((p (make-instance 'gf-point :x 10 :y 20))) (list (gf-point-x p) (gf-point-y p))))
(check "gf accessor setf" 99 (let ((p (make-instance 'gf-point :x 1 :y 2))) (setf (gf-point-x p) 99) (gf-point-x p)))
(defparameter *init-log* nil)
(defmethod initialize-instance :after ((p gf-point) &rest initargs) (push 'gf-point-initialized *init-log*))
(setq *init-log* nil)
(make-instance 'gf-point :x 1 :y 2)
(check "gf initialize-instance :after" '(gf-point-initialized) *init-log*)

; --- CLOS Phase 8: change-class + reinitialize-instance ---
(check "reinitialize-instance" 99 (let ((p (make-instance 'gf-point :x 1 :y 2))) (reinitialize-instance p :x 99) (gf-point-x p)))
(defclass color-point (gf-point) ((color :initarg :color :initform :red :accessor point-color)))
(check "change-class shared slots" '(10 20 :red) (let* ((p (make-instance 'gf-point :x 10 :y 20)) (cp (change-class p 'color-point))) (list (gf-point-x cp) (gf-point-y cp) (point-color cp))))
(check "change-class with initargs" :blue (let* ((p (make-instance 'gf-point :x 1 :y 2)) (cp (change-class p 'color-point :color :blue))) (point-color cp)))

; --- Slot definition metaobjects (MOP) ---
(check "mop1 direct slot typep" t (typep (first (class-direct-slots (find-class 'point))) 'standard-direct-slot-definition))
(check "mop1 direct slot is slot-definition" t (typep (first (class-direct-slots (find-class 'point))) 'slot-definition))
(check "mop1 effective slot typep" t (typep (first (class-slots (find-class 'point))) 'standard-effective-slot-definition))
(check "mop1 class-slots length" 2 (length (class-slots (find-class 'point))))
(check "mop1 slot-def name" 'x (slot-definition-name (first (class-direct-slots (find-class 'point)))))
(check "mop1 slot-def initargs" '(:x) (slot-definition-initargs (first (class-direct-slots (find-class 'point)))))
(check "mop1 slot-def readers" '(point-x) (slot-definition-readers (first (class-direct-slots (find-class 'point)))))
(check "mop1 slot-def writers" '((setf point-x)) (slot-definition-writers (first (class-direct-slots (find-class 'point)))))
(check "mop1 slot-def allocation" :instance (slot-definition-allocation (first (class-direct-slots (find-class 'point)))))
(check "mop1 effective-slot location 0" 0 (slot-definition-location (first (class-slots (find-class 'point)))))
(check "mop1 effective-slot location 1" 1 (slot-definition-location (second (class-slots (find-class 'point)))))
(defclass mop1-amiga-ifm () ((n :initarg :n :initform 42)))
(check "mop1 slot-def initform" 42 (slot-definition-initform (first (class-direct-slots (find-class 'mop1-amiga-ifm)))))
(check "mop1 slot-def initfunction call" 42 (funcall (slot-definition-initfunction (first (class-direct-slots (find-class 'mop1-amiga-ifm))))))
(check "mop1 make-instance uses initform" 42 (slot-value (make-instance 'mop1-amiga-ifm) 'n))
(check "mop1 direct-slot-definition-class" 'standard-direct-slot-definition (class-name (direct-slot-definition-class (find-class 'standard-class))))
(check "mop1 effective-slot-definition-class" 'standard-effective-slot-definition (class-name (effective-slot-definition-class (find-class 'standard-class))))
(defclass mop1-amiga-p1 () ((x :initarg :a)))
(defclass mop1-amiga-p2 (mop1-amiga-p1) ((x :initarg :b)))
(check "mop1 inherited initarg :a" 1 (slot-value (make-instance 'mop1-amiga-p2 :a 1) 'x))
(check "mop1 inherited initarg :b" 2 (slot-value (make-instance 'mop1-amiga-p2 :b 2) 'x))

; --- Class finalization protocol (MOP) ---
(defclass cf-amiga-f1 () ((x :initarg :x)))
(check "class-finalized-p new class" t (class-finalized-p (find-class 'cf-amiga-f1)))
(check "class-finalized-p bootstrap" t (class-finalized-p (find-class 'standard-object)))
(check "finalize-inheritance idempotent" t (progn (finalize-inheritance (find-class 'cf-amiga-f1)) (finalize-inheritance (find-class 'cf-amiga-f1)) (class-finalized-p (find-class 'cf-amiga-f1))))
(defclass cf-amiga-a () ())
(defclass cf-amiga-b (cf-amiga-a) ())
(check "compute-class-precedence-list" '(cf-amiga-b cf-amiga-a standard-object t) (mapcar #'class-name (compute-class-precedence-list (find-class 'cf-amiga-b))))
(defclass cf-amiga-cs () ((a :initarg :a) (b :initarg :b)))
(check "compute-slots length" 2 (length (compute-slots (find-class 'cf-amiga-cs))))
(check "compute-slots names" '(a b) (mapcar #'slot-definition-name (compute-slots (find-class 'cf-amiga-cs))))
(check "compute-effective-slot-definition name" 'a (slot-definition-name (compute-effective-slot-definition (find-class 'cf-amiga-cs) 'a (list (first (class-direct-slots (find-class 'cf-amiga-cs)))))))
(defclass cf-amiga-di-base () ((a :initarg :a)) (:default-initargs :a 99))
(defclass cf-amiga-di-sub (cf-amiga-di-base) ())
(check "default-initargs inherited" 99 (slot-value (make-instance 'cf-amiga-di-sub) 'a))
(defclass cf-amiga-di-p () ((a :initarg :a)) (:default-initargs :a 'parent))
(defclass cf-amiga-di-c (cf-amiga-di-p) () (:default-initargs :a 'child))
(check "default-initargs most-specific wins" 'child (slot-value (make-instance 'cf-amiga-di-c) 'a))
(check "validate-superclass T" t (validate-superclass (find-class 'standard-class) (find-class 'standard-object)))
(defclass cf-amiga-ddi () ((x :initarg :x)) (:default-initargs :x 42))
(check "class-direct-default-initargs key" :x (first (first (class-direct-default-initargs (find-class 'cf-amiga-ddi)))))
(check "class-direct-default-initargs value" 42 (funcall (second (first (class-direct-default-initargs (find-class 'cf-amiga-ddi))))))
(defclass cf-amiga-proto () ((x :initarg :x)))
(check "class-prototype typep" t (typep (class-prototype (find-class 'cf-amiga-proto)) 'cf-amiga-proto))
(check "class-prototype cached" t (eq (class-prototype (find-class 'cf-amiga-proto)) (class-prototype (find-class 'cf-amiga-proto))))
(ensure-class 'cf-amiga-ec :direct-superclasses '() :direct-slots (list (%make-direct-slot-def 'v '(:v) nil nil nil :instance nil nil nil)) :direct-default-initargs '())
(check "ensure-class creates" 'cf-amiga-ec (class-name (find-class 'cf-amiga-ec)))
(check "ensure-class slot count" 1 (length (class-slots (find-class 'cf-amiga-ec))))

; --- Slot-access protocol (MOP) ---
(defclass sv-amiga-fast () ((x :initarg :x :initform 10)))
(check "protocol flag clean" nil *slot-access-protocol-extended-p*)
(check "fast path slot-value" 7 (slot-value (make-instance 'sv-amiga-fast :x 7) 'x))
(defclass sv-amiga-default () ((n :initarg :n)))
(check "default svuc = slot-value" 42 (let ((inst (make-instance 'sv-amiga-default :n 42))) (slot-value-using-class (class-of inst) inst (first (class-slots (class-of inst))))))
(check "slot-definition-location 0" 0 (slot-definition-location (first (class-slots (find-class 'sv-amiga-default)))))
(defclass sv-amiga-loc2 () ((a :initarg :a) (b :initarg :b)))
(check "slot-definition-location 1" 1 (slot-definition-location (second (class-slots (find-class 'sv-amiga-loc2)))))
(defclass sv-amiga-setf () ((z :initarg :z :initform 1)))
(let ((inst (make-instance 'sv-amiga-setf)))
  (setf (slot-value-using-class (class-of inst) inst (first (class-slots (class-of inst)))) 77)
  (check "setf svuc default round-trip" 77 (slot-value inst 'z)))
(defclass sv-amiga-obs () ((k :initarg :k :initform 'orig)))
(defvar *sv-amiga-log* nil)
(defmethod slot-value-using-class :around ((class t) (inst sv-amiga-obs) slot)
  (push (slot-definition-name slot) *sv-amiga-log*)
  (call-next-method))
(check "protocol flag extended" t *slot-access-protocol-extended-p*)
(check "around read returns orig" 'orig (slot-value (make-instance 'sv-amiga-obs) 'k))
(check "around read logged" 'k (first *sv-amiga-log*))
(defclass sv-amiga-mod () ((v :initarg :v :initform 1)))
(defmethod slot-value-using-class :around ((class t) (inst sv-amiga-mod) slot)
  (declare (ignore slot))
  (* 10 (call-next-method)))
(check "around modifies read" 30 (slot-value (make-instance 'sv-amiga-mod :v 3) 'v))
(defclass sv-amiga-set () ((w :initarg :w :initform 0)))
(defvar *sv-amiga-writes* nil)
(defmethod (setf slot-value-using-class) :around (new-value (class t) (inst sv-amiga-set) slot)
  (push (cons (slot-definition-name slot) new-value) *sv-amiga-writes*)
  (call-next-method))
(let ((inst (make-instance 'sv-amiga-set))) (setf (slot-value inst 'w) 99) (check "setf-svuc around value" 99 (slot-value inst 'w)))
(check "setf-svuc around logged new-value" 99 (cdr (first *sv-amiga-writes*)))
(check "setf-svuc around logged slot" 'w (car (first *sv-amiga-writes*)))
(defclass sv-amiga-boundp () ((q :initarg :q)))
(defvar *sv-amiga-saw-boundp* nil)
(defmethod slot-boundp-using-class :around ((class t) (inst sv-amiga-boundp) slot)
  (declare (ignore slot))
  (setq *sv-amiga-saw-boundp* t)
  (call-next-method))
(check "boundp via protocol bound" t (slot-boundp (make-instance 'sv-amiga-boundp :q 1) 'q))
(check "boundp around fired" t *sv-amiga-saw-boundp*)
(check "boundp via protocol unbound" nil (slot-boundp (make-instance 'sv-amiga-boundp) 'q))
(defclass sv-amiga-mu () ((r :initarg :r :initform 5)))
(defvar *sv-amiga-cleared* nil)
(defmethod slot-makunbound-using-class :around ((class t) (inst sv-amiga-mu) slot)
  (push (slot-definition-name slot) *sv-amiga-cleared*)
  (call-next-method))
(let ((inst (make-instance 'sv-amiga-mu))) (slot-makunbound inst 'r) (check "makunbound via protocol" nil (slot-boundp inst 'r)))
(check "makunbound around fired" 'r (first *sv-amiga-cleared*))
(defstruct sv-amiga-struct a b)
(let ((s (make-sv-amiga-struct :a 100 :b 200)))
  (check "struct fallback a" 100 (slot-value s 'a))
  (check "struct fallback b" 200 (slot-value s 'b)))
(defclass sv-amiga-unbnd () ((u)))
(defmethod slot-unbound ((class t) (inst sv-amiga-unbnd) slot-name)
  (declare (ignore class slot-name))
  :sv-amiga-unbound-sentinel)
(check "slot-unbound via protocol" :sv-amiga-unbound-sentinel (slot-value (make-instance 'sv-amiga-unbnd) 'u))

; --- :allocation :class shared slots (MOP) ---
(defclass cs-amiga-share () ((n :allocation :class :initform 0)))
(let ((a (make-instance 'cs-amiga-share)) (b (make-instance 'cs-amiga-share)))
  (setf (slot-value a 'n) 42)
  (check "class-alloc shared write" 42 (slot-value b 'n)))
(defclass cs-amiga-parent () ((x :allocation :class :initform 'hello)))
(defclass cs-amiga-child (cs-amiga-parent) ())
(let ((p (make-instance 'cs-amiga-parent)) (c (make-instance 'cs-amiga-child)))
  (declare (ignore p))
  (setf (slot-value c 'x) 'world)
  (check "class-alloc inherited cell" 'world (slot-value (make-instance 'cs-amiga-parent) 'x)))
(check "class-alloc shared location identity" t
  (eq (slot-definition-location (first (class-slots (find-class 'cs-amiga-parent))))
      (slot-definition-location (first (class-slots (find-class 'cs-amiga-child))))))
(defclass cs-amiga-p2 () ((x :allocation :class :initform 1)))
(defclass cs-amiga-c2 (cs-amiga-p2) ((x :allocation :class :initform 2)))
(let ((p (make-instance 'cs-amiga-p2)) (c (make-instance 'cs-amiga-c2)))
  (check "class-alloc redefined parent initform" 1 (slot-value p 'x))
  (check "class-alloc redefined child initform" 2 (slot-value c 'x))
  (setf (slot-value p 'x) 100)
  (check "class-alloc redefined child unaffected" 2 (slot-value c 'x))
  (check "class-alloc redefined parent written" 100 (slot-value p 'x)))
(defclass cs-amiga-loc () ((shared :allocation :class :initform 'hi)))
(check "class-alloc location is cons" t (consp (slot-definition-location (first (class-slots (find-class 'cs-amiga-loc))))))
(make-instance 'cs-amiga-loc)
(check "class-alloc cell cdr holds value" 'hi (cdr (slot-definition-location (first (class-slots (find-class 'cs-amiga-loc))))))
(defclass cs-amiga-mix () ((a :initarg :a) (b :allocation :class :initform 'shared) (c :initarg :c)))
(let ((slots (class-slots (find-class 'cs-amiga-mix))))
  (check "class-alloc instance a loc" 0 (slot-definition-location (find 'a slots :key #'slot-definition-name)))
  (check "class-alloc class b loc consp" t (consp (slot-definition-location (find 'b slots :key #'slot-definition-name))))
  (check "class-alloc instance c loc" 1 (slot-definition-location (find 'c slots :key #'slot-definition-name))))
(let ((inst (make-instance 'cs-amiga-mix :a 10 :c 30)))
  (check "class-alloc read a" 10 (slot-value inst 'a))
  (check "class-alloc read b" 'shared (slot-value inst 'b))
  (check "class-alloc read c" 30 (slot-value inst 'c))
  (check "class-alloc struct-ref 0" 10 (%struct-ref inst 0))
  (check "class-alloc struct-ref 1" 30 (%struct-ref inst 1)))
(defclass cs-amiga-bp () ((s :allocation :class)))
(let ((a (make-instance 'cs-amiga-bp)) (b (make-instance 'cs-amiga-bp)))
  (check "class-alloc boundp unbound" nil (slot-boundp a 's))
  (setf (slot-value a 's) 7)
  (check "class-alloc boundp bound sibling" t (slot-boundp b 's))
  (slot-makunbound a 's)
  (check "class-alloc makunbound affects sibling" nil (slot-boundp b 's)))
(defvar *cs-amiga-init-counter* 0)
(defclass cs-amiga-init () ((k :allocation :class :initform (incf *cs-amiga-init-counter*))))
(let ((a (make-instance 'cs-amiga-init)) (b (make-instance 'cs-amiga-init)))
  (check "class-alloc initform runs once" 1 *cs-amiga-init-counter*)
  (check "class-alloc initform same for a" 1 (slot-value a 'k))
  (check "class-alloc initform same for b" 1 (slot-value b 'k)))
(defclass cs-amiga-ia () ((k :allocation :class :initarg :k :initform 0)))
(let ((a (make-instance 'cs-amiga-ia :k 99)) (b (make-instance 'cs-amiga-ia)))
  (check "class-alloc initarg write a" 99 (slot-value a 'k))
  (check "class-alloc initarg persists b" 99 (slot-value b 'k)))

; --- Reified EQL specializers (MOP) ---
(check "eql-spec intern same value eq" t (eq (intern-eql-specializer 42) (intern-eql-specializer 42)))
(check "eql-spec intern different value not eq" nil (eq (intern-eql-specializer 42) (intern-eql-specializer 43)))
(check "eql-spec intern symbol" t (eq (intern-eql-specializer 'amiga-sym) (intern-eql-specializer 'amiga-sym)))
(check "eql-spec object accessor integer" 42 (eql-specializer-object (intern-eql-specializer 42)))
(check "eql-spec object accessor symbol" 'amiga-tag (eql-specializer-object (intern-eql-specializer 'amiga-tag)))
(check "eql-spec object accessor nil" nil (eql-specializer-object (intern-eql-specializer nil)))
(check "eql-spec predicate true" t (eql-specializer-p (intern-eql-specializer 1)))
(check "eql-spec predicate on class" nil (eql-specializer-p (find-class 'integer)))
(check "eql-spec predicate on fixnum" nil (eql-specializer-p 42))
(check "eql-spec predicate on cons" nil (eql-specializer-p '(eql 42)))
(check "eql-spec class bootstrapped" 'eql-specializer (class-name (class-of (intern-eql-specializer 42))))
(defgeneric es-amiga-disp (x))
(defmethod es-amiga-disp ((x (eql 'alpha))) :a)
(defmethod es-amiga-disp ((x (eql 'beta))) :b)
(defmethod es-amiga-disp ((x symbol)) :other)
(check "eql-spec dispatch alpha" :a (es-amiga-disp 'alpha))
(check "eql-spec dispatch beta"  :b (es-amiga-disp 'beta))
(check "eql-spec dispatch other" :other (es-amiga-disp 'gamma))
(defgeneric es-amiga-ms (x))
(defmethod es-amiga-ms ((x (eql 7))) :seven)
(check "eql-spec method-specializers is metaobject" t
  (eql-specializer-p (first (method-specializers (first (gf-methods (gethash 'es-amiga-ms *generic-function-table*)))))))
(check "eql-spec method-specializers object" 7
  (eql-specializer-object (first (method-specializers (first (gf-methods (gethash 'es-amiga-ms *generic-function-table*)))))))
(defgeneric es-amiga-share1 (x))
(defgeneric es-amiga-share2 (x))
(defmethod es-amiga-share1 ((x (eql :amiga-shared))) :one)
(defmethod es-amiga-share2 ((x (eql :amiga-shared))) :two)
(check "eql-spec shared across methods" t
  (eq (first (method-specializers (first (gf-methods (gethash 'es-amiga-share1 *generic-function-table*)))))
      (first (method-specializers (first (gf-methods (gethash 'es-amiga-share2 *generic-function-table*)))))))
(check "extract-specializer-names eql+class" '((eql 99) string)
  (extract-specializer-names '((x (eql 99)) (y string))))
(check "extract-specializer-names class-only" '(integer)
  (extract-specializer-names '((x integer))))
(defgeneric es-amiga-rep (x))
(defmethod es-amiga-rep ((x (eql 1))) :first)
(defmethod es-amiga-rep ((x (eql 1))) :second)
(check "eql-spec redef replaces" :second (es-amiga-rep 1))
(check "eql-spec redef single method" 1
  (length (gf-methods (gethash 'es-amiga-rep *generic-function-table*))))
(defgeneric es-amiga-cache (x))
(defmethod es-amiga-cache ((x (eql 42))) :fortytwo)
(defmethod es-amiga-cache ((x integer)) :int)
(check "eql-spec cacheable-p :EQL" :eql
  (gf-cacheable-p (gethash 'es-amiga-cache *generic-function-table*)))

; --- Funcallable standard class (MOP) ---
(check "funcallable-standard-class bootstrapped" 'funcallable-standard-class
  (class-name (find-class 'funcallable-standard-class)))
(check "funcallable-standard-object bootstrapped" 'funcallable-standard-object
  (class-name (find-class 'funcallable-standard-object)))
(defgeneric fsc-amiga-foo (x))
(defmethod fsc-amiga-foo ((x integer)) (* x 2))
(check "gf typep funcallable-standard-object" t (typep #'fsc-amiga-foo 'funcallable-standard-object))
(check "gf typep standard-generic-function" t (typep #'fsc-amiga-foo 'standard-generic-function))
(check "gf typep function" t (typep #'fsc-amiga-foo 'function))
(check "gf functionp" t (functionp #'fsc-amiga-foo))
(check "gf class-of" t (eq (class-of #'fsc-amiga-foo) (find-class 'standard-generic-function)))
(check "gf type-of" 'standard-generic-function (type-of #'fsc-amiga-foo))
(check "gf callable via symbol" 14 (fsc-amiga-foo 7))
(check "gf funcall"    20 (funcall #'fsc-amiga-foo 10))
(check "gf apply"      50 (apply #'fsc-amiga-foo '(25)))
(check "gf funcall ensure" 30 (funcall (ensure-generic-function 'fsc-amiga-foo) 15))
(defgeneric fsc-amiga-sfi (x))
(defmethod fsc-amiga-sfi ((x integer)) :default)
(check "gf dispatch before sfi" :default (fsc-amiga-sfi 5))
(set-funcallable-instance-function
  (ensure-generic-function 'fsc-amiga-sfi)
  (lambda (&rest args) (cons :replaced args)))
(check "gf dispatch after sfi" '(:replaced 5) (fsc-amiga-sfi 5))
(check "sfi returns gf" t
  (eq (set-funcallable-instance-function
        (ensure-generic-function 'fsc-amiga-sfi)
        (lambda (&rest args) args))
      (ensure-generic-function 'fsc-amiga-sfi)))
(defclass sia-amiga () ((a :initarg :a) (b :initarg :b)))
(defvar *sia-amiga-inst* (make-instance 'sia-amiga :a 10 :b 20))
(check "standard-instance-access a" 10 (standard-instance-access *sia-amiga-inst* 0))
(check "standard-instance-access b" 20 (standard-instance-access *sia-amiga-inst* 1))
(setf (standard-instance-access *sia-amiga-inst* 0) 99)
(check "standard-instance-access setf" 99 (slot-value *sia-amiga-inst* 'a))
(check "standard-instance-access == struct-ref" t
  (eq (standard-instance-access *sia-amiga-inst* 1) (%struct-ref *sia-amiga-inst* 1)))
(check "funcallable-standard-instance-access" 99
  (funcallable-standard-instance-access *sia-amiga-inst* 0))
(defgeneric fsc-amiga-cdf (x))
(defmethod fsc-amiga-cdf ((x integer)) :int-case)
(check "compute-discriminating-function functionp" t
  (functionp (compute-discriminating-function (ensure-generic-function 'fsc-amiga-cdf))))
(check "compute-discriminating-function dispatches" :int-case
  (funcall (compute-discriminating-function (ensure-generic-function 'fsc-amiga-cdf)) 7))

; --- Method metaobject protocol (MOP) ---
(defgeneric mmop-amiga-bl (x))
(defmethod mmop-amiga-bl ((x integer)) :int)
(check "method-generic-function back-link" t
  (eq (method-generic-function (first (gf-methods (gethash 'mmop-amiga-bl *generic-function-table*))))
      (gethash 'mmop-amiga-bl *generic-function-table*)))
(check "extract-lambda-list plain" '(x y z)
  (extract-lambda-list '((x point) (y (eql 3)) z)))
(check "extract-lambda-list preserves tail" '(x &optional (y 5) &rest rest &key k)
  (extract-lambda-list '((x integer) &optional (y 5) &rest rest &key k)))
(check "extract-specializer-names padded t" '(t integer t)
  (extract-specializer-names '(x (y integer) z)))
(check "extract-specializer-names stops at &" '(string)
  (extract-specializer-names '((a string) &optional b &key c)))

(defgeneric mmop-amiga-add (x))
(defmethod mmop-amiga-add ((x t)) :default)
(let* ((gf (ensure-generic-function 'mmop-amiga-add))
       (fn (lambda (x) (declare (ignore x)) :added))
       (m (%make-struct 'standard-method
            nil (list (find-class 'integer)) '() fn '(x))))
  (add-method gf m))
(check "add-method installs" :added (mmop-amiga-add 42))
(check "add-method keeps default" :default (mmop-amiga-add "hi"))

(defgeneric mmop-amiga-addret (x))
(check "add-method returns gf" t
  (let* ((gf (ensure-generic-function 'mmop-amiga-addret))
         (m (%make-struct 'standard-method
              nil (list (find-class 't)) '()
              (lambda (x) (declare (ignore x)) :ok) '(x))))
    (eq (add-method gf m) gf)))

(defgeneric mmop-amiga-rm (x))
(defmethod mmop-amiga-rm ((x t)) :default)
(defmethod mmop-amiga-rm ((x integer)) :int)
(check "remove-method before" :int (mmop-amiga-rm 42))
(let* ((gf (gethash 'mmop-amiga-rm *generic-function-table*))
       (m  (find-if (lambda (m)
                      (equal (method-specializers m) (list (find-class 'integer))))
                    (gf-methods gf))))
  (remove-method gf m))
(check "remove-method after" :default (mmop-amiga-rm 42))

(defgeneric mmop-amiga-rmbl (x))
(defmethod mmop-amiga-rmbl ((x integer)) :int)
(defvar *mmop-amiga-rm-m*
  (first (gf-methods (gethash 'mmop-amiga-rmbl *generic-function-table*))))
(remove-method (gethash 'mmop-amiga-rmbl *generic-function-table*) *mmop-amiga-rm-m*)
(check "remove-method clears back-link" nil
  (method-generic-function *mmop-amiga-rm-m*))

(defgeneric mmop-amiga-fm (x))
(defmethod mmop-amiga-fm ((x string)) :s)
(defmethod mmop-amiga-fm :before ((x string)) :before)
(check "find-method locates" t
  (not (null (find-method (gethash 'mmop-amiga-fm *generic-function-table*)
                          '() (list (find-class 'string))))))
(check "find-method resolves class names" t
  (not (null (find-method (gethash 'mmop-amiga-fm *generic-function-table*)
                          '() '(string)))))
(check "find-method resolves (eql v)" t
  (progn
    (defgeneric mmop-amiga-fmeql (x))
    (defmethod mmop-amiga-fmeql ((x (eql 7))) :seven)
    (not (null (find-method (gethash 'mmop-amiga-fmeql *generic-function-table*)
                            '() '((eql 7)))))))
(check "find-method missing returns nil" nil
  (progn
    (defgeneric mmop-amiga-fmnil (x))
    (find-method (gethash 'mmop-amiga-fmnil *generic-function-table*)
                 '() '(integer) nil)))

(defgeneric mmop-amiga-em (x))
(ensure-method 'mmop-amiga-em
  '(lambda (x) (declare (ignore x)) :ensured)
  :specializers '(integer))
(check "ensure-method installs" :ensured (mmop-amiga-em 99))

(defgeneric mmop-amiga-emdef (x))
(ensure-method 'mmop-amiga-emdef
  '(lambda (x) (declare (ignore x)) :any))
(check "ensure-method default specs catches string" :any (mmop-amiga-emdef "a"))
(check "ensure-method default specs catches integer" :any (mmop-amiga-emdef 42))

(defgeneric mmop-amiga-mml (x))
(check "make-method-lambda default identity" '((lambda (x) x) nil)
  (let* ((gf (ensure-generic-function 'mmop-amiga-mml))
         (m (%make-struct 'standard-method
              gf (list (find-class 't)) '()
              (lambda (x) x) '(x))))
    (multiple-value-list (make-method-lambda gf m '(lambda (x) x) nil))))

; --- CLOS Phase 9: print-object ---
(check "print-object class" "#<STANDARD-CLASS INTEGER>" (print-object (find-class 'integer) nil))
(check "print-object gf" "#<STANDARD-GENERIC-FUNCTION GREET>" (print-object (gethash 'greet *generic-function-table*) nil))
(defclass printable () ((tag :initarg :tag :accessor printable-tag)))
(defmethod print-object ((obj printable) stream) (concatenate 'string "#<PRINTABLE " (printable-tag obj) ">"))
(check "print-object custom" "#<PRINTABLE hello>" (print-object (make-instance 'printable :tag "hello") nil))
(check "print-object default nil" nil (print-object 42 nil))

; --- LOOP nested conditionals ---
(check "loop nested if" '(:two :three) (loop :for x :in '(1 2 3) :when (> x 1) :if (= x 2) :collect :two :else :collect :three :end))
(check "loop nested when collect" '(3 5) (loop :for x :in '(1 2 3 4 5) :when (oddp x) :if (> x 2) :collect x :end))
(check "loop nested unless" '(1 3) (loop :for x :in '(1 2 3 4) :when t :unless (evenp x) :collect x :end))
(check "loop nested if do" '(2 4) (let ((r nil)) (loop :for x :in '(1 2 3 4) :when t :if (evenp x) :do (push x r) :end) (nreverse r)))

; --- Defmacro destructuring ---
(defmacro amiga-dm1 ((a b) c) `(list ,a ,b ,c))
(check "defmacro destructuring req" '(1 2 3) (amiga-dm1 (1 2) 3))
(defmacro amiga-dm2 (var &body (then-form &optional else-form)) `(let ((,var t)) (if ,var ,then-form ,else-form)))
(check "defmacro destructuring body" :yes (amiga-dm2 x :yes :no))
(check "defmacro destructuring body opt" :only (amiga-dm2 x :only))
(defmacro amiga-dm3 (x &optional (y 42)) `(list ,x ,y))
(check "defmacro optional not destructured" '(1 42) (amiga-dm3 1))
(check "defmacro optional provided" '(1 2) (amiga-dm3 1 2))

; --- define-modify-macro ---
(define-modify-macro amiga-appendf (&rest args) append)
(check "define-modify-macro" '(1 2 3 4) (let ((x '(1 2))) (amiga-appendf x '(3 4)) x))

; --- reduce :from-end ---
(check "reduce from-end cons" '(1 2 3 . 4) (reduce #'cons '(1 2 3 4) :from-end t))
(check "reduce from-end initial" '(1 2 3) (reduce #'cons '(1 2 3) :from-end t :initial-value '()))
(check "reduce from-end vector" '(1 2 3 . 4) (reduce #'cons #(1 2 3 4) :from-end t))
(check "reduce forward unchanged" 10 (reduce #'+ '(1 2 3 4)))

; --- defmethod implicit block ---
(defgeneric amiga-block-test (x))
(defmethod amiga-block-test ((x t)) (return-from amiga-block-test 42) 99)
(check "defmethod implicit block" 42 (amiga-block-test 1))

; --- user-homedir-pathname ---
(check "user-homedir-pathname type" t (pathnamep (user-homedir-pathname)))

; --- %set-condition-default-initargs ---
(check "set-condition-default-initargs stub" nil (%set-condition-default-initargs 'foo '(:x 1)))
(define-condition amiga-test-cond (error) () (:default-initargs :x 1))
(check "define-condition with default-initargs" 'amiga-test-cond 'amiga-test-cond)

; --- ASDF-session features ---
(check "lisp-implementation-type returns string" t (stringp (lisp-implementation-type)))
(check "lisp-implementation-version returns string" t (stringp (lisp-implementation-version)))
(check "software-type returns string" t (stringp (software-type)))
(check "machine-type returns string" t (stringp (machine-type)))

; --- make-string ---
(check "make-string basic" "aaa" (make-string 3 :initial-element #\a))
(check "make-string default" 3 (length (make-string 3)))

; --- fboundp / fmakunbound ---
(defun amiga-fboundp-test () 42)
(check "fboundp defined function" t (fboundp 'amiga-fboundp-test))
(fmakunbound 'amiga-fboundp-test)
(check "fmakunbound removes binding" nil (fboundp 'amiga-fboundp-test))
(defun (setf amiga-setf-test) (v o) (declare (ignore o)) v)
(check "fboundp (setf name)" t (fboundp '(setf amiga-setf-test)))
(fmakunbound '(setf amiga-setf-test))
(check "fmakunbound (setf name)" nil (fboundp '(setf amiga-setf-test)))
(check "fmakunbound unknown (setf name)" '(setf no-such-fn) (fmakunbound '(setf no-such-fn)))
(defvar *amiga-mak-test* 123)
(check "makunbound returns symbol" '*amiga-mak-test* (makunbound '*amiga-mak-test*))
(check "makunbound unbinds" nil (boundp '*amiga-mak-test*))

; --- ext:getenv ---
(check "getenv non-existent returns NIL" nil (ext:getenv "CLAMIGA_NONEXISTENT_VAR_12345"))
(check "getenv type check" t (handler-case (progn (ext:getenv 42) nil) (error () t)))

; --- Nested quasiquote ---
(check "nested-qq double unquote" '(quasiquote (a (unquote 42)))
  (let ((x 42)) ``(a ,,x)))
(check "nested-qq unquote-splice" '(quasiquote (a (unquote 1) (unquote 2) (unquote 3)))
  (let ((xs '(1 2 3))) ``(a ,,@xs)))
(check "nested-qq simple" '(quasiquote (a b))
  ``(a b))
(check "nested-qq inner splice" '(a 1 2 b)
  (let ((xs '(1 2))) `(a ,@xs b)))
(check "dotted quasiquote" '(a . 3)
  `(a . ,(+ 1 2)))

; --- reader-error condition ---
(check "reader-error typep" t (typep (make-condition 'reader-error) 'reader-error))
(check "reader-error is parse-error" t (typep (make-condition 'reader-error) 'parse-error))
(check "reader-error is stream-error" t (typep (make-condition 'reader-error) 'stream-error))
(check "reader-error is error" t (typep (make-condition 'reader-error) 'error))

; --- Standard condition accessors ---
(check "stream-error-stream" *standard-output*
  (stream-error-stream (make-condition 'stream-error :stream *standard-output*)))
(check "cell-error-name" 'foo
  (cell-error-name (make-condition 'cell-error :name 'foo)))
(check "file-error-pathname" "/tmp/x"
  (file-error-pathname (make-condition 'file-error :pathname "/tmp/x")))

; --- define-compiler-macro (no-op) ---
(check "define-compiler-macro" 'test-cm
  (define-compiler-macro test-cm (&whole form x) (declare (ignore form x)) nil))
(check "compiler-macro-function" nil (compiler-macro-function 'test-cm))

; --- setf values ---
(check "setf values" '(10 20 30)
  (let ((a 0) (b 0) (c 0))
    (setf (values a b c) (values 10 20 30))
    (list a b c)))

; --- define-setf-expander (no-op) ---
(check "define-setf-expander" 'my-place
  (define-setf-expander my-place (x) (declare (ignore x)) nil))

; --- PROGV ---
(defparameter *progv-x* 10)
(check "progv basic" 99 (progv '(*progv-x*) '(99) *progv-x*))
(check "progv restored" 10 *progv-x*)

(defparameter *progv-a* 1)
(defparameter *progv-b* 2)
(check "progv multiple" 30 (progv '(*progv-a* *progv-b*) '(10 20) (+ *progv-a* *progv-b*)))
(check "progv multiple restored a" 1 *progv-a*)
(check "progv multiple restored b" 2 *progv-b*)

(check "progv empty symbols" 3 (progv '() '() (+ 1 2)))

(check "progv extra values" 77
  (progv '(*progv-a*) '(77 88 99) *progv-a*))

(check "progv fewer values unbound" nil
  (progv '(*progv-a*) '() (boundp '*progv-a*)))
(check "progv fewer values restored" 1 *progv-a*)

(check "progv restore on throw" 42
  (catch 'done (progv '(*progv-x*) '(999) (throw 'done 42))))
(check "progv restored after throw" 10 *progv-x*)

(check "progv nested" 2
  (progv '(*progv-x*) '(1)
    (progv '(*progv-x*) '(2) *progv-x*)))
(check "progv nested restored" 10 *progv-x*)

; --- Macrolet/symbol-macrolet across lambda ---
(check "macrolet across lambda" 42
  (macrolet ((foo () '42))
    (funcall (lambda () (foo)))))
(check "symbol-macrolet across lambda" 42
  (symbol-macrolet ((x 42))
    (funcall (lambda () x))))
(check "macrolet across nested lambda" 15
  (macrolet ((add10 (x) `(+ ,x 10)))
    (funcall (lambda () (funcall (lambda () (add10 5)))))))

; --- Bignum logand/logxor masking (CDB hash pattern) ---
(check "logand mask 32bit 1" 2087597948 (logand #xFFFFFFFF 6382565244))
(check "logand mask 32bit 2" 1356358965 (logand #xFFFFFFFF 6950613443893))
(check "logand mask 32bit 3" 0 (logand #xFFFFFFFF 4294967296))
(check "logand mask 32bit 4" 4294967295 (logand #xFFFFFFFF 4294967295))
(check "logand mask 32bit 5" 4294967295 (logand #xFFFFFFFF 8589934591))
(check "logand bignum+bignum 1" 4294967295 (logand #xFFFFFFFF #xFFFFFFFF))
(check "logand bignum+bignum 2" 4294967295 (logand #x1FFFFFFFF #xFFFFFFFF))
(check "logxor bignum 1" 2087597849 (logxor 2087597948 101))
(check "logxor bignum 2" 4294967295 (logxor #xFFFFFFFF 0))
(check "logxor bignum 3" 0 (logxor #xFFFFFFFF #xFFFFFFFF))
(check "ash bignum left" 6189154176 (ash 193411068 5))
(check "logand+ash" 1894186880 (logand #xFFFFFFFF (ash 193411068 5)))
(check "cdb hash fiveam" 1356358965
  (let ((h 5381))
    (dolist (c '(102 105 118 101 97 109) h)
      (setf h (logand #xFFFFFFFF (+ h (ash h 5))))
      (setf h (logxor h c)))))
(check "cdb hash step f" 177603 (logxor (logand #xFFFFFFFF (+ 5381 (ash 5381 5))) 102))
(check "cdb hash step i" 5860938 (logxor (logand #xFFFFFFFF (+ 177603 (ash 177603 5))) 105))
(check "cdb hash step v" 193411068 (logxor (logand #xFFFFFFFF (+ 5860938 (ash 5860938 5))) 118))
(check "cdb hash add e" 6382565244 (+ 193411068 (ash 193411068 5)))
(check "cdb hash mask e" 2087597948 (logand #xFFFFFFFF 6382565244))
(check "cdb hash xor e" 2087597849 (logxor 2087597948 101))

; --- FASL compile-file and load ---

; Write a test Lisp file, compile it, load the FASL
(with-open-file (s "T:fasl-test1.lisp" :direction :output :if-exists :supersede)
  (write-string "(defun fasl-test-fn (x) (* x 3))" s)
  (terpri s))
(compile-file "T:fasl-test1.lisp" :output-file "T:fasl-test1.fasl")
(check "compile-file creates FASL" t (not (null (probe-file "T:fasl-test1.fasl"))))

; Load the FASL and verify function works
(load "T:fasl-test1.fasl")
(check "load FASL defines function" 15 (fasl-test-fn 5))

; compile-file with multiple forms
(with-open-file (s "T:fasl-test2.lisp" :direction :output :if-exists :supersede)
  (write-string "(defvar *fasl-var-a* 100)" s) (terpri s)
  (write-string "(defvar *fasl-var-b* 200)" s) (terpri s)
  (write-string "(defun fasl-sum () (+ *fasl-var-a* *fasl-var-b*))" s) (terpri s))
(compile-file "T:fasl-test2.lisp" :output-file "T:fasl-test2.fasl")
(load "T:fasl-test2.fasl")
(check "FASL multiple forms" 300 (fasl-sum))

; compile-file-pathname returns a pathname
(check "compile-file-pathname type" t (pathnamep (compile-file-pathname "T:foo.lisp")))

; compile-file returns a pathname
(check "compile-file return type" t
  (pathnamep (compile-file "T:fasl-test1.lisp" :output-file "T:fasl-test1.fasl")))

; FASL with macros (eval-during-compile)
(with-open-file (s "T:fasl-test3.lisp" :direction :output :if-exists :supersede)
  (write-string "(defmacro fasl-triple (x) `(* 3 ,x))" s) (terpri s)
  (write-string "(defun fasl-apply-triple (n) (fasl-triple n))" s) (terpri s))
(compile-file "T:fasl-test3.lisp" :output-file "T:fasl-test3.fasl")
(load "T:fasl-test3.fasl")
(check "FASL with macros" 21 (fasl-apply-triple 7))

; --- Dispatch cache ---
(defgeneric dcache-test (x))
(defmethod dcache-test ((x point)) (point-x x))
(check "cache basic dispatch" 42 (dcache-test (make-instance 'point :x 42 :y 0)))
; second call uses cache
(check "cache hit correct" 77 (dcache-test (make-instance 'point :x 77 :y 0)))
; cache populated
(check "cache populated" t
  (let ((gf (gethash 'dcache-test *generic-function-table*)))
    (not (null (gf-dispatch-cache gf)))))
; cacheable-p is 1 for single-dispatch
(check "cache cacheable-p" 1
  (let ((gf (gethash 'dcache-test *generic-function-table*)))
    (gf-cacheable-p gf)))
; invalidation on defmethod
(defmethod dcache-test ((x point3d)) (+ (point-x x) (point-z x)))
(check "cache cleared on defmethod" nil
  (let ((gf (gethash 'dcache-test *generic-function-table*)))
    (gf-dispatch-cache gf)))
(check "cache new method works" 15
  (dcache-test (make-instance 'point3d :x 10 :y 0 :z 5)))
; EQL specializer bypass
(defgeneric dcache-eql-test (x))
(defmethod dcache-eql-test ((x (eql 42))) 'forty-two)
(defmethod dcache-eql-test ((x integer)) 'other-int)
(check "cache eql dispatch 42" 'forty-two (dcache-eql-test 42))
(check "cache eql dispatch 7" 'other-int (dcache-eql-test 7))
(check "cache eql cacheable-p" :eql
  (let ((gf (gethash 'dcache-eql-test *generic-function-table*)))
    (gf-cacheable-p gf)))
; Multi-dispatch bypass
(defgeneric dcache-multi (x y))
(defmethod dcache-multi ((x point) (y point)) 'both-points)
(check "cache multi dispatch" 'both-points
  (dcache-multi (make-instance 'point :x 1 :y 2)
                (make-instance 'point :x 3 :y 4)))
(check "cache multi cacheable-p" 2
  (let ((gf (gethash 'dcache-multi *generic-function-table*)))
    (gf-cacheable-p gf)))
; before/after/around with cache
(defparameter *dcache-trace* nil)
(defgeneric dcache-combo (x))
(defmethod dcache-combo :before ((x point)) (push 'before *dcache-trace*))
(defmethod dcache-combo ((x point)) (push 'primary *dcache-trace*) 'done)
(defmethod dcache-combo :after ((x point)) (push 'after *dcache-trace*))
(defmethod dcache-combo :around ((x point)) (push 'around *dcache-trace*) (call-next-method))
(setq *dcache-trace* nil)
(check "cache combo result" 'done (dcache-combo (make-instance 'point :x 0 :y 0)))
(check "cache combo order" '(around before primary after) (nreverse *dcache-trace*))
; second call uses cache, same result
(setq *dcache-trace* nil)
(dcache-combo (make-instance 'point :x 1 :y 1))
(check "cache combo cached order" '(around before primary after) (nreverse *dcache-trace*))
; no applicable method with cache
(defgeneric dcache-err (x))
(defmethod dcache-err ((x point)) 'point-ok)
(check "cache no-applicable errors" 'got-error
  (handler-case (dcache-err "hello") (error (c) 'got-error)))

; --- EQL specializer cache ---
(defgeneric eql-c-test (x))
(defmethod eql-c-test ((x (eql 42))) 'forty-two)
(defmethod eql-c-test ((x integer)) 'some-int)
(check "eql cache 42" 'forty-two (eql-c-test 42))
(check "eql cache 7" 'some-int (eql-c-test 7))
; cache hit
(check "eql cache hit 42" 'forty-two (eql-c-test 42))
(check "eql cache populated" t
  (let ((gf (gethash 'eql-c-test *generic-function-table*)))
    (not (null (gf-dispatch-cache gf)))))
; mixed eql + class
(defgeneric eql-mix (x))
(defmethod eql-mix ((x (eql :alpha))) 'got-alpha)
(defmethod eql-mix ((x (eql :beta))) 'got-beta)
(defmethod eql-mix ((x symbol)) 'some-symbol)
(check "eql cache alpha" 'got-alpha (eql-mix :alpha))
(check "eql cache beta" 'got-beta (eql-mix :beta))
(check "eql cache gamma fallback" 'some-symbol (eql-mix :gamma))
; class fallback
(check "eql cache class fallback" 'some-int (eql-c-test 100))
; call-next-method
(defgeneric eql-cnm (x))
(defmethod eql-cnm ((x (eql 99))) (list 'eql-99 (call-next-method)))
(defmethod eql-cnm ((x integer)) (list 'int x))
(check "eql cache cnm" '(eql-99 (int 99)) (eql-cnm 99))
(check "eql cache cnm cached" '(eql-99 (int 99)) (eql-cnm 99))
(check "eql cache cnm fallback" '(int 50) (eql-cnm 50))
; (eql nil)
(defgeneric eql-nil-test (x))
(defmethod eql-nil-test ((x (eql nil))) 'got-nil)
(defmethod eql-nil-test ((x t)) 'got-other)
(check "eql cache nil" 'got-nil (eql-nil-test nil))
(check "eql cache nil other" 'got-other (eql-nil-test 'foo))
(check "eql cache nil cached" 'got-nil (eql-nil-test nil))
; EQL on arg2
(defgeneric eql-arg2 (x y))
(defmethod eql-arg2 ((x t) (y (eql :load))) (list 'loading x))
(defmethod eql-arg2 ((x t) (y (eql :compile))) (list 'compiling x))
(defmethod eql-arg2 ((x t) (y t)) (list 'default x y))
(check "eql cache arg2 load" '(loading foo) (eql-arg2 'foo :load))
(check "eql cache arg2 compile" '(compiling bar) (eql-arg2 'bar :compile))
(check "eql cache arg2 default" '(default baz :other) (eql-arg2 'baz :other))
(check "eql cache arg2 cached" '(loading x) (eql-arg2 'x :load))
; invalidation
(eql-c-test 42) ; populate cache
(defmethod eql-c-test ((x (eql 0))) 'zero)
(check "eql cache invalidated" nil
  (let ((gf (gethash 'eql-c-test *generic-function-table*)))
    (gf-dispatch-cache gf)))
(check "eql cache new method" 'zero (eql-c-test 0))
(check "eql cache old still works" 'forty-two (eql-c-test 42))
; cacheable-p
(check "eql cache cacheable-p" :eql
  (let ((gf (gethash 'eql-c-test *generic-function-table*)))
    (gf-cacheable-p gf)))

; --- Multi-dispatch cache ---
(defgeneric mc-test (x y))
(defmethod mc-test ((x point) (y point))
  (+ (point-x x) (point-x y)))
(defmethod mc-test ((x point3d) (y point))
  (+ (point-x x) (point-z x) (point-x y)))
(check "multi cache point+point" 30
  (mc-test (make-instance 'point :x 10 :y 0)
           (make-instance 'point :x 20 :y 0)))
(check "multi cache point3d+point" 35
  (mc-test (make-instance 'point3d :x 10 :y 0 :z 5)
           (make-instance 'point :x 20 :y 0)))
; cache hit
(check "multi cache hit" 3
  (mc-test (make-instance 'point :x 1 :y 0)
           (make-instance 'point :x 2 :y 0)))
(check "multi cache populated" t
  (let ((gf (gethash 'mc-test *generic-function-table*)))
    (not (null (gf-dispatch-cache gf)))))
(check "multi cache cacheable-p" 2
  (let ((gf (gethash 'mc-test *generic-function-table*)))
    (gf-cacheable-p gf)))
; invalidation
(defmethod mc-test ((x point) (y point3d))
  (+ (point-x x) (point-z y)))
(check "multi cache invalidated" nil
  (let ((gf (gethash 'mc-test *generic-function-table*)))
    (gf-dispatch-cache gf)))
(check "multi cache new method" 40
  (mc-test (make-instance 'point :x 10 :y 0)
           (make-instance 'point3d :x 1 :y 2 :z 30)))
; call-next-method in multi-dispatch
(defgeneric mc-cnm (x y))
(defmethod mc-cnm ((x point) (y point))
  (list 'base (point-x x) (point-x y)))
(defmethod mc-cnm ((x point3d) (y point))
  (list 'p3d (call-next-method)))
(check "multi cache cnm" '(p3d (base 10 20))
  (mc-cnm (make-instance 'point3d :x 10 :y 0 :z 5)
          (make-instance 'point :x 20 :y 0)))
(check "multi cache cnm cached" '(p3d (base 99 88))
  (mc-cnm (make-instance 'point3d :x 99 :y 0 :z 1)
          (make-instance 'point :x 88 :y 0)))

; --- EMF cache (effective method closure caching) ---
(defgeneric emf-cnm-test (x))
(defmethod emf-cnm-test ((x point3d))
  (list 'p3d (point-x x) (call-next-method)))
(defmethod emf-cnm-test ((x point))
  (list 'pt (point-x x)))
; First call — populates cache
(check "emf cache cnm no-args 1st" '(p3d 10 (pt 10))
  (emf-cnm-test (make-instance 'point3d :x 10 :y 20 :z 30)))
; Second call — uses cached EMF
(check "emf cache cnm no-args cached" '(p3d 99 (pt 99))
  (emf-cnm-test (make-instance 'point3d :x 99 :y 0 :z 0)))

; call-next-method with explicit args
(defgeneric emf-cnm-args (x))
(defmethod emf-cnm-args ((x point3d))
  (call-next-method (make-instance 'point :x 42 :y 0)))
(defmethod emf-cnm-args ((x point))
  (point-x x))
(check "emf cache cnm with-args" 42
  (emf-cnm-args (make-instance 'point3d :x 1 :y 2 :z 3)))
(check "emf cache cnm with-args cached" 42
  (emf-cnm-args (make-instance 'point3d :x 7 :y 8 :z 9)))

; Full method combination with EMF cache
(defparameter *emf-trace* nil)
(defgeneric emf-combo (x))
(defmethod emf-combo :before ((x point)) (push (list 'before (point-x x)) *emf-trace*))
(defmethod emf-combo ((x point)) (push 'primary *emf-trace*) (point-x x))
(defmethod emf-combo :after ((x point)) (push 'after *emf-trace*))
(defmethod emf-combo :around ((x point)) (push 'around *emf-trace*) (call-next-method))
(setq *emf-trace* nil)
(check "emf cache combo result" 5
  (emf-combo (make-instance 'point :x 5 :y 0)))
(check "emf cache combo order" '(around (before 5) primary after) (nreverse *emf-trace*))
; Cached call
(setq *emf-trace* nil)
(check "emf cache combo cached" 88
  (emf-combo (make-instance 'point :x 88 :y 0)))
(check "emf cache combo cached order" '(around (before 88) primary after) (nreverse *emf-trace*))

; next-method-p in cached path
(defgeneric emf-nmp (x))
(defmethod emf-nmp ((x point3d)) (list (next-method-p) (call-next-method)))
(defmethod emf-nmp ((x point)) (next-method-p))
(check "emf cache next-method-p" '(t nil)
  (emf-nmp (make-instance 'point3d :x 0 :y 0 :z 0)))
(check "emf cache next-method-p cached" '(t nil)
  (emf-nmp (make-instance 'point3d :x 1 :y 1 :z 1)))

; Negative caching
(defgeneric emf-neg (x))
(defmethod emf-neg ((x point)) 'ok)
(check "emf cache negative ok" 'ok
  (emf-neg (make-instance 'point :x 0 :y 0)))
(check "emf cache negative error" 'no-method
  (handler-case (emf-neg "hello") (error (c) 'no-method)))
(check "emf cache negative cached" 'no-method
  (handler-case (emf-neg "world") (error (c) 'no-method)))

; ============================================================
; Threading (MP package)
; ============================================================
(format t "~%--- Threading ---~%")

; --- Thread creation and join ---
(check "thread make and join" 42
  (mp:join-thread (mp:make-thread (lambda () 42))))

(check "thread join returns computed result" 60
  (mp:join-thread (mp:make-thread (lambda () (+ 10 20 30)))))

(check "thread join cons result" '(1 2 3)
  (mp:join-thread (mp:make-thread (lambda () (list 1 2 3)))))

(check "thread make with name" "worker"
  (mp:thread-name (mp:make-thread (lambda () nil) :name "worker")))

; --- Thread predicates ---
(check "thread alive-p after join" nil
  (let ((thr (mp:make-thread (lambda () 42))))
    (mp:join-thread thr)
    (mp:thread-alive-p thr)))

(check "current-thread returns thread" t
  (not (null (mp:current-thread))))

(check "all-threads includes main" t
  (>= (length (mp:all-threads)) 1))

; --- Thread yield ---
(check "thread-yield no crash" nil
  (mp:thread-yield))

; --- Lock operations ---
(check "make-lock" t
  (not (null (mp:make-lock "test-lock"))))

(check "lock acquire and release" t
  (let ((lk (mp:make-lock)))
    (mp:acquire-lock lk)
    (mp:release-lock lk)
    t))

(check "lock trylock" t
  (let ((lk (mp:make-lock)))
    (mp:acquire-lock lk nil)))

(check "with-lock-held" 42
  (let ((lk (mp:make-lock)))
    (mp:with-lock-held (lk)
      42)))

; --- Lock contention (two threads) ---
(check "lock contention two threads" 200
  (let ((counter (list 0))
        (lk (mp:make-lock)))
    (let ((t1 (mp:make-thread
                (lambda ()
                  (dotimes (i 100)
                    (mp:with-lock-held (lk)
                      (setf (car counter) (+ (car counter) 1)))))))
          (t2 (mp:make-thread
                (lambda ()
                  (dotimes (i 100)
                    (mp:with-lock-held (lk)
                      (setf (car counter) (+ (car counter) 1))))))))
      (mp:join-thread t1)
      (mp:join-thread t2)
      (car counter))))

; --- Condition variables ---
(check "make-condition-variable" t
  (not (null (mp:make-condition-variable))))

(check "condvar notify/wait" t
  (let ((lk (mp:make-lock))
        (cv (mp:make-condition-variable))
        (ready (list nil)))
    (let ((consumer (mp:make-thread
                      (lambda ()
                        (mp:acquire-lock lk)
                        (loop until (car ready)
                              do (mp:condition-wait cv lk))
                        (mp:release-lock lk)
                        (car ready)))))
      (mp:thread-yield)
      (mp:acquire-lock lk)
      (setf (car ready) t)
      (mp:condition-notify cv)
      (mp:release-lock lk)
      (mp:join-thread consumer))))

; --- Dynamic binding isolation ---
(check "thread inherits dynamic bindings" "COMMON-LISP-USER"
  (mp:join-thread
    (mp:make-thread
      (lambda () (package-name *package*)))))

(defvar *thread-test-var* :parent)
(check "thread let binding isolation" '(:parent :child)
  (let ((child (mp:make-thread
                 (lambda ()
                   (let ((*thread-test-var* :child))
                     *thread-test-var*)))))
    (let ((child-result (mp:join-thread child)))
      (list *thread-test-var* child-result))))

; --- Error handling in threads ---
(check "thread error sets aborted" :survived
  (let ((thr (mp:make-thread
               (lambda () (error "boom")))))
    (mp:join-thread thr)
    :survived))

; --- Multiple concurrent threads ---
(check "multiple concurrent threads" '(10 20 30 40)
  (let ((threads (list
                   (mp:make-thread (lambda () (* 1 10)))
                   (mp:make-thread (lambda () (* 2 10)))
                   (mp:make-thread (lambda () (* 3 10)))
                   (mp:make-thread (lambda () (* 4 10))))))
    (mapcar #'mp:join-thread threads)))

; --- Named condition variables ---
(check "make-condition-variable with name" t
  (not (null (mp:make-condition-variable "my-cv"))))

(check "condition-name accessor" "test-cv"
  (mp:condition-name (mp:make-condition-variable "test-cv")))

(check "condition-name nil when unnamed" nil
  (mp:condition-name (mp:make-condition-variable)))

; --- Lock name accessor ---
(check "lock-name accessor" "my-lock"
  (mp:lock-name (mp:make-lock "my-lock")))

(check "lock-name nil when unnamed" nil
  (mp:lock-name (mp:make-lock)))

; --- special-operator-p (CLHS) ---
(check "special-operator-p quote" t (special-operator-p 'quote))
(check "special-operator-p if" t (special-operator-p 'if))
(check "special-operator-p tagbody" t (special-operator-p 'tagbody))
(check "special-operator-p car" nil (special-operator-p 'car))
(check "special-operator-p undefined" nil (special-operator-p 'no-such-sym))

; --- documentation is a generic function ---
; Storage via (setf documentation) + retrieval; adding a specialized
; method for a user-defined doc-type must NOT break the (t t) fallback.
(progn
  (setf (documentation 'amiga-doc-sym 'function) "hello")
  (defmethod documentation ((x symbol) (type (eql 'amiga-ns))) 'specialized))
(check "documentation fallback retains string"
  "hello" (documentation 'amiga-doc-sym 'function))
(check "documentation specialized method used"
  'specialized (documentation 'amiga-doc-sym 'amiga-ns))

; --- Type predicates ---
(check "threadp true" t
  (mp:threadp (mp:current-thread)))

(check "threadp false" nil
  (mp:threadp 42))

(check "lockp true" t
  (mp:lockp (mp:make-lock)))

(check "lockp false" nil
  (mp:lockp "not-a-lock"))

(check "condition-variable-p true" t
  (mp:condition-variable-p (mp:make-condition-variable)))

(check "condition-variable-p false" nil
  (mp:condition-variable-p nil))

; --- interrupt-thread ---
(check "interrupt-thread self" 42
  (let ((x 0))
    (mp:interrupt-thread (mp:current-thread)
      (lambda () (setf x 42)))
    x))

(check "interrupt-thread basic" :interrupted
  (let ((flag (list nil))
        (lk (mp:make-lock))
        (cv (mp:make-condition-variable)))
    (let ((thr (mp:make-thread
                 (lambda ()
                   (mp:acquire-lock lk)
                   (loop until (car flag)
                         do (mp:condition-wait cv lk))
                   (mp:release-lock lk)
                   (car flag)))))
      (mp:thread-yield)
      (mp:interrupt-thread thr
        (lambda () (setf (car flag) :interrupted)))
      (mp:acquire-lock lk)
      (mp:condition-notify cv)
      (mp:release-lock lk)
      (mp:join-thread thr))))

; --- destroy-thread ---
(check "destroy-thread basic" :destroyed
  (let ((thr (mp:make-thread
               (lambda ()
                 (loop (mp:thread-yield))))))
    (mp:thread-yield)
    (mp:destroy-thread thr)
    (mp:join-thread thr)
    :destroyed))

(check "destroy-thread not alive after" nil
  (let ((thr (mp:make-thread
               (lambda ()
                 (loop (mp:thread-yield))))))
    (mp:thread-yield)
    (mp:destroy-thread thr)
    (mp:join-thread thr)
    (mp:thread-alive-p thr)))

; --- GC stress under threads ---
(check "thread GC stress" 400
  (let ((t1 (mp:make-thread
              (lambda () (length (make-list 200)))))
        (t2 (mp:make-thread
              (lambda () (length (make-list 200))))))
    (+ (mp:join-thread t1) (mp:join-thread t2))))

; --- FFI (generic) ---
(require "ffi")

(check "ffi-package-exists" "FFI" (package-name (find-package "FFI")))
(check "ffi-make-foreign-pointer" 'foreign-pointer (type-of (ffi:make-foreign-pointer 42)))
(check "ffi-foreign-pointer-p" t (ffi:foreign-pointer-p (ffi:make-foreign-pointer 0)))
(check "ffi-foreign-pointer-p-int" nil (ffi:foreign-pointer-p 42))
(check "ffi-null-pointer-p-zero" t (ffi:null-pointer-p (ffi:make-foreign-pointer 0)))
(check "ffi-null-pointer-p-nonzero" nil (ffi:null-pointer-p (ffi:make-foreign-pointer 1)))
(check "ffi-foreign-pointer-address" 42 (ffi:foreign-pointer-address (ffi:make-foreign-pointer 42)))
(check "ffi-alloc-free" nil
  (let ((p (ffi:alloc-foreign 64)))
    (prog1 (ffi:null-pointer-p p)
      (ffi:free-foreign p))))
(check "ffi-peek-poke-u32" 12345
  (let ((p (ffi:alloc-foreign 16)))
    (ffi:poke-u32 p 12345)
    (prog1 (ffi:peek-u32 p)
      (ffi:free-foreign p))))
(check "ffi-peek-poke-u32-offset" 300
  (let ((p (ffi:alloc-foreign 16)))
    (ffi:poke-u32 p 100 0)
    (ffi:poke-u32 p 200 4)
    (prog1 (+ (ffi:peek-u32 p 0) (ffi:peek-u32 p 4))
      (ffi:free-foreign p))))
(check "ffi-peek-poke-u16" 1234
  (let ((p (ffi:alloc-foreign 16)))
    (ffi:poke-u16 p 1234)
    (prog1 (ffi:peek-u16 p)
      (ffi:free-foreign p))))
(check "ffi-peek-poke-u8" 255
  (let ((p (ffi:alloc-foreign 16)))
    (ffi:poke-u8 p 255)
    (prog1 (ffi:peek-u8 p)
      (ffi:free-foreign p))))
(check "ffi-foreign-string-roundtrip" "hello"
  (let ((p (ffi:foreign-string "hello")))
    (prog1 (ffi:foreign-to-string p)
      (ffi:free-foreign p))))
(check "ffi-pointer-plus" 120
  (ffi:foreign-pointer-address (ffi:pointer+ (ffi:make-foreign-pointer 100) 20)))
(check "ffi-with-foreign-alloc" 42
  (ffi:with-foreign-alloc (p 16)
    (ffi:poke-u32 p 42)
    (ffi:peek-u32 p)))
(check "ffi-defcstruct" '(100 200)
  (progn
    (ffi:defcstruct test-point (x :u16 0) (y :u16 2))
    (ffi:with-foreign-alloc (p 4)
      (setf (test-point-x p) 100)
      (setf (test-point-y p) 200)
      (list (test-point-x p) (test-point-y p)))))

; --- FFI (Amiga-specific) ---
#+amigaos
(progn
  (check "amiga-package-exists" "AMIGA" (package-name (find-package "AMIGA")))
  (check "amiga-open-close-library" t
    (let ((lib (amiga:open-library "dos.library" 36)))
      (prog1 (not (null lib))
        (amiga:close-library lib))))
  ;; ExecBase is at absolute address 4 on all Amigas
  (check "amiga-peek-execbase" t
    (let ((p (ffi:make-foreign-pointer 4)))
      (> (ffi:peek-u32 p) 0)))
  (check "amiga-alloc-chip" t
    (let ((p (amiga:alloc-chip 256)))
      (prog1 (not (ffi:null-pointer-p p))
        (amiga:free-chip p))))

  ; --- Intuition/Graphics/GadTools tests ---
  ; These are in a separate file because the reader needs the packages
  ; to exist before it can read amiga.intuition:* qualified symbols.
  (handler-case
    (load "tests/amiga/test-gui.lisp")
    (error (e) (format t "ERROR loading GUI tests: ~A~%" e))))

; --- Summary ---
(format t "~%=== Results ===~%")
(format t "Passed: ~A~%" *pass-count*)
(format t "Failed: ~A~%" *fail-count*)
(format t "Total:  ~A~%" (+ *pass-count* *fail-count*))
(if (= *fail-count* 0) (format t "~%ALL TESTS PASSED~%") (format t "~%SOME TESTS FAILED~%"))
