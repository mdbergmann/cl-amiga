/* Tests for docstring capture, APROPOS and the DESCRIBE enrichment
 * (specs/documentation-introspection.md, phases 1-5).
 *
 * The compiler used to parse a docstring and drop it.  It now emits a
 * load-time (%SET-DOCUMENTATION 'name 'doc-type "...") call for every
 * documented DEFUN / DEFMACRO / DEFVAR / DEFPARAMETER / DEFCONSTANT /
 * DEFTYPE (compiler_extra.c emit_doc_call); DEFSTRUCT, DEFINE-CONDITION,
 * DEFCLASS, DEFGENERIC and DEFINE-METHOD-COMBINATION record theirs from
 * Lisp.  The FASL leg -- the record must survive COMPILE-FILE + LOAD in a
 * fresh session -- is in tests/test_dev_commands.sh (DESCRIBE over a
 * loaded FASL) and under GC stress in tests/test_gc_stress_regression.sh. */

#include "test.h"
#include "core/types.h"
#include "core/mem.h"
#include "core/error.h"
#include "core/package.h"
#include "core/symbol.h"
#include "core/reader.h"
#include "core/printer.h"
#include "core/compiler.h"
#include "core/vm.h"
#include "core/builtins.h"
#include "core/repl.h"
#include "platform/platform.h"
#include <string.h>

static void setup(void)
{
    platform_init();
    cl_thread_init();
    cl_error_init();
    cl_mem_init(CL_DEFAULT_HEAP_SIZE);
    cl_package_init();
    cl_symbol_init();
    cl_reader_init();
    cl_printer_init();
    cl_compiler_init();
    cl_vm_init(0, 0);
    cl_builtins_init();
    cl_repl_init();
}

static void teardown(void)
{
    cl_mem_shutdown();
    platform_shutdown();
}

/* Evaluate EXPR, return its printed value (or ERROR:n). */
static const char *eval_print(const char *expr)
{
    static char buf[2048];
    int err;

    CL_CATCH(err);
    if (err == CL_ERR_NONE) {
        CL_Obj result = cl_eval_string(expr);
        cl_prin1_to_string(result, buf, sizeof(buf));
        CL_UNCATCH();
        return buf;
    } else {
        CL_UNCATCH();
        snprintf(buf, sizeof(buf), "ERROR:%d", err);
        cl_vm.sp = 0;
        cl_vm.fp = 0;
        return buf;
    }
}

static int truthy(const char *expr)
{
    return cl_eval_string(expr) != CL_NIL;
}

/* --- Phase 1: functions and macros ---------------------------------- */

TEST(defun_docstring_recorded)
{
    eval_print("(defun doc-fn (x) \"Adds one.\" (+ x 1))");
    ASSERT_STR_EQ(eval_print("(documentation 'doc-fn 'function)"), "\"Adds one.\"");
    /* The body still runs: the docstring was not mistaken for code. */
    ASSERT_STR_EQ(eval_print("(doc-fn 1)"), "2");
}

TEST(defun_docstring_after_declare)
{
    /* CLHS 3.4.11: the docstring may sit among the declarations. */
    eval_print("(defun doc-decl (x) (declare (ignorable x)) \"After declare.\" x)");
    ASSERT_STR_EQ(eval_print("(documentation 'doc-decl 'function)"), "\"After declare.\"");
}

TEST(string_only_body_is_a_value_not_a_doc)
{
    /* CLHS 3.4.11: a string that is the whole body is the return value. */
    eval_print("(defun doc-str () \"just a string\")");
    ASSERT_STR_EQ(eval_print("(documentation 'doc-str 'function)"), "NIL");
    ASSERT_STR_EQ(eval_print("(doc-str)"), "\"just a string\"");
}

TEST(setf_function_docstring)
{
    eval_print("(defun (setf doc-place) (v x) \"Setter doc.\" (list v x))");
    ASSERT_STR_EQ(eval_print("(documentation '(setf doc-place) 'function)"),
                  "\"Setter doc.\"");
}

TEST(defmacro_docstring_recorded)
{
    eval_print("(defmacro doc-mac (a &body b) \"Macro doc.\" `(progn ,a ,@b))");
    /* CLHS: a macro's documentation is under the FUNCTION doc-type. */
    ASSERT_STR_EQ(eval_print("(documentation 'doc-mac 'function)"), "\"Macro doc.\"");
    ASSERT_STR_EQ(eval_print("(doc-mac 1 2 3)"), "3");
}

TEST(defmacro_keeps_written_lambda_list)
{
    /* (MACRO-FUNCTION 'x) is a (form env) wrapper; EXT:FUNCTION-ARGLIST on
     * it must still answer with the lambda list as written -- that is what
     * an editor's arglist display shows. */
    eval_print("(defmacro doc-ll (a (b c) &body body) `(list ,a ,b ,c ,@body))");
    ASSERT(truthy("(equal (ext:function-arglist (macro-function 'doc-ll))"
                  "        '(a (b c) &body body))"));
    /* &environment is the expander's business, not the caller's. */
    eval_print("(defmacro doc-env (x &environment env) (declare (ignore env)) x)");
    ASSERT(truthy("(equal (ext:function-arglist (macro-function 'doc-env)) '(x))"));
    /* And the destructuring still works after the rewrite. */
    ASSERT_STR_EQ(eval_print("(doc-ll 1 (2 3) 4)"), "(1 2 3 4)");
}

/* --- Phase 2: variables and types ----------------------------------- */

TEST(defvar_family_docstrings)
{
    eval_print("(defvar *doc-var* 1 \"Var doc.\")");
    eval_print("(defparameter *doc-param* 2 \"Param doc.\")");
    eval_print("(defconstant +doc-const+ 3 \"Const doc.\")");
    ASSERT_STR_EQ(eval_print("(documentation '*doc-var* 'variable)"), "\"Var doc.\"");
    ASSERT_STR_EQ(eval_print("(documentation '*doc-param* 'variable)"), "\"Param doc.\"");
    ASSERT_STR_EQ(eval_print("(documentation '+doc-const+ 'variable)"), "\"Const doc.\"");
    /* The values were not disturbed. */
    ASSERT_STR_EQ(eval_print("(list *doc-var* *doc-param* +doc-const+)"), "(1 2 3)");
    /* No third argument, no record. */
    eval_print("(defvar *doc-none* 4)");
    ASSERT_STR_EQ(eval_print("(documentation '*doc-none* 'variable)"), "NIL");
}

TEST(deftype_docstring)
{
    eval_print("(deftype doc-type () \"Type doc.\" 'integer)");
    ASSERT_STR_EQ(eval_print("(documentation 'doc-type 'type)"), "\"Type doc.\"");
    ASSERT_STR_EQ(eval_print("(typep 1 'doc-type)"), "T");
}

TEST(defstruct_docstring)
{
    eval_print("(defstruct doc-struct \"Struct doc.\" a b)");
    ASSERT_STR_EQ(eval_print("(documentation 'doc-struct 'structure)"), "\"Struct doc.\"");
    /* The string was not taken for a slot. */
    ASSERT_STR_EQ(eval_print("(doc-struct-a (make-doc-struct :a 1 :b 2))"), "1");
}

TEST(define_condition_documentation_option)
{
    eval_print("(define-condition doc-cond (error) ((a :initarg :a))"
               "  (:documentation \"Condition doc.\") (:report \"doc-cond\"))");
    ASSERT_STR_EQ(eval_print("(documentation 'doc-cond 'type)"), "\"Condition doc.\"");
}

/* --- Phase 3: CLOS doc-types ---------------------------------------- */

TEST(defclass_documentation_option)
{
    eval_print("(defclass doc-class () ((s :initarg :s)) (:documentation \"Class doc.\"))");
    ASSERT_STR_EQ(eval_print("(documentation 'doc-class 'type)"), "\"Class doc.\"");
}

TEST(defgeneric_documentation_option)
{
    eval_print("(defgeneric doc-gf (a) (:documentation \"GF doc.\"))");
    eval_print("(defmethod doc-gf ((a integer)) a)");
    ASSERT_STR_EQ(eval_print("(documentation 'doc-gf 'function)"), "\"GF doc.\"");
    ASSERT_STR_EQ(eval_print("(doc-gf 7)"), "7");
}

TEST(method_combination_documentation)
{
    eval_print("(define-method-combination doc-comb :documentation \"Comb doc.\" :operator list)");
    ASSERT_STR_EQ(eval_print("(documentation 'doc-comb 'method-combination)"),
                  "\"Comb doc.\"");
}

/* --- The switch ------------------------------------------------------ */

TEST(capture_switch_drops_docstrings)
{
    ASSERT_STR_EQ(eval_print("ext:*capture-documentation*"), "T");
    eval_print("(let ((ext:*capture-documentation* nil))"
               "  (eval '(defun doc-dropped (x) \"Gone.\" x))"
               "  (eval '(defvar *doc-dropped* 1 \"Gone too.\")))");
    ASSERT_STR_EQ(eval_print("(documentation 'doc-dropped 'function)"), "NIL");
    ASSERT_STR_EQ(eval_print("(documentation '*doc-dropped* 'variable)"), "NIL");
    /* The definitions themselves are intact. */
    ASSERT_STR_EQ(eval_print("(doc-dropped 5)"), "5");
}

/* --- Phase 4: APROPOS ------------------------------------------------ */

TEST(apropos_list_matches_substring_case_insensitively)
{
    ASSERT(truthy("(member 'mapcar (apropos-list \"mapc\" \"CL\"))"));
    ASSERT(truthy("(member 'mapcan (apropos-list \"MAPC\" \"CL\"))"));
    ASSERT(truthy("(not (member 'car (apropos-list \"mapc\" \"CL\")))"));
    /* Sorted by name, no duplicates. */
    ASSERT(truthy("(let ((l (apropos-list \"mapc\" \"CL\")))"
                  "  (and (equal l (sort (copy-list l) #'string< :key #'symbol-name))"
                  "       (= (length l) (length (remove-duplicates l)))))"));
}

TEST(apropos_list_sees_inherited_symbols)
{
    /* MAPCAR is not present in CL-USER, only accessible through CL. */
    ASSERT(truthy("(member 'mapcar (apropos-list \"mapcar\" \"CL-USER\"))"));
    eval_print("(defun apropos-own-fn () 1)");
    ASSERT(truthy("(member 'apropos-own-fn (apropos-list \"apropos-own\" \"CL-USER\"))"));
}

TEST(apropos_list_all_packages_and_string_designators)
{
    /* No package: every package.  A symbol designates its name. */
    ASSERT(truthy("(member 'mapcar (apropos-list 'mapcar))"));
    ASSERT_STR_EQ(eval_print("(handler-case (apropos-list \"x\" \"NO-SUCH-PKG-HERE\")"
                             "  (error () :error))"),
                  ":ERROR");
}

TEST(apropos_prints_and_returns_no_values)
{
    const char *out = eval_print(
        "(with-output-to-string (*standard-output*) (apropos \"make-hash-table\" \"CL\"))");
    ASSERT(strstr(out, "MAKE-HASH-TABLE") != NULL);
    ASSERT(strstr(out, "[function]") != NULL);
    ASSERT_STR_EQ(eval_print("(length (multiple-value-list (apropos \"zzz-nothing-here\" \"CL\")))"),
                  "0");
    /* No longer the "not yet implemented" stub. */
    ASSERT_STR_EQ(eval_print("(and (fboundp 'apropos) (fboundp 'apropos-list) t)"), "T");
}

/* --- Phase 5: DESCRIBE ----------------------------------------------- */

static const char *describe_output(const char *describe_expr)
{
    static char expr_buf[512];
    snprintf(expr_buf, sizeof(expr_buf),
             "(with-output-to-string (s) %s)", describe_expr);
    return eval_print(expr_buf);
}

TEST(describe_symbol_shows_lambda_list_and_documentation)
{
    const char *out;
    eval_print("(defun doc-desc (a &optional (b 2) &key c) \"Describe me.\" (list a b c))");
    out = describe_output("(describe 'doc-desc s)");
    ASSERT(strstr(out, "Lambda-list: (A &OPTIONAL (B 2) &KEY C)") != NULL);
    ASSERT(strstr(out, "Documentation: Describe me.") != NULL);
}

TEST(describe_symbol_shows_macro_and_variable)
{
    const char *out;
    eval_print("(defmacro doc-desc-mac (x &body b) \"Macro described.\" `(progn ,x ,@b))");
    out = describe_output("(describe 'doc-desc-mac s)");
    ASSERT(strstr(out, "Macro: ") != NULL);
    ASSERT(strstr(out, "Lambda-list: (X &BODY B)") != NULL);
    ASSERT(strstr(out, "Documentation: Macro described.") != NULL);

    eval_print("(defvar *doc-desc-var* 1 \"Variable described.\")");
    out = describe_output("(describe '*doc-desc-var* s)");
    ASSERT(strstr(out, "Variable documentation: Variable described.") != NULL);
}

TEST(describe_symbol_shows_generic_function_lambda_list)
{
    const char *out;
    eval_print("(defgeneric doc-desc-gf (a b) (:documentation \"GF described.\"))");
    out = describe_output("(describe 'doc-desc-gf s)");
    ASSERT(strstr(out, "Lambda-list: (A B)") != NULL);
    ASSERT(strstr(out, "Documentation: GF described.") != NULL);
}

TEST(describe_function_object_shows_lambda_list)
{
    const char *out;
    eval_print("(defun doc-desc-obj (x y) \"Object described.\" (+ x y))");
    out = describe_output("(describe #'doc-desc-obj s)");
    ASSERT(strstr(out, "COMPILED-FUNCTION") != NULL);
    ASSERT(strstr(out, "Lambda-list: (X Y)") != NULL);
    ASSERT(strstr(out, "Documentation: Object described.") != NULL);
    /* A builtin keeps its terse form. */
    out = describe_output("(describe 'car s)");
    ASSERT(strstr(out, "Lambda-list: (") != NULL);
    ASSERT(strstr(out, "Documentation:") == NULL);
}

TEST(describe_undocumented_symbol_has_no_doc_lines)
{
    const char *out;
    eval_print("(defun doc-desc-none (x) x)");
    out = describe_output("(describe 'doc-desc-none s)");
    ASSERT(strstr(out, "Lambda-list: (X)") != NULL);
    ASSERT(strstr(out, "Documentation") == NULL);
}

int main(void)
{
    test_init();
    setup();

    RUN(defun_docstring_recorded);
    RUN(defun_docstring_after_declare);
    RUN(string_only_body_is_a_value_not_a_doc);
    RUN(setf_function_docstring);
    RUN(defmacro_docstring_recorded);
    RUN(defmacro_keeps_written_lambda_list);
    RUN(defvar_family_docstrings);
    RUN(deftype_docstring);
    RUN(defstruct_docstring);
    RUN(define_condition_documentation_option);
    RUN(defclass_documentation_option);
    RUN(defgeneric_documentation_option);
    RUN(method_combination_documentation);
    RUN(capture_switch_drops_docstrings);
    RUN(apropos_list_matches_substring_case_insensitively);
    RUN(apropos_list_sees_inherited_symbols);
    RUN(apropos_list_all_packages_and_string_designators);
    RUN(apropos_prints_and_returns_no_values);
    RUN(describe_symbol_shows_lambda_list_and_documentation);
    RUN(describe_symbol_shows_macro_and_variable);
    RUN(describe_symbol_shows_generic_function_lambda_list);
    RUN(describe_function_object_shows_lambda_list);
    RUN(describe_undocumented_symbol_has_no_doc_lines);

    teardown();
    REPORT();
}
