/*
 * test_alexandria_regress.c — regression tests for the conformance bugs found
 * by ALEXANDRIA's test suite (trunk/load-and-test-alexandria.lisp):
 *
 *   - DELETE is destructive on lists (splices in place)            [deletef.1]
 *   - ROTATEF / SHIFTF / DEFINE-MODIFY-MACRO evaluate each place's
 *     subforms exactly once via GET-SETF-EXPANSION       [maxf.4/minf.2/shuffle]
 *   - EVERY returns a boolean T, not the last predicate value          [shuffle]
 *   - SUBTYPEP settles (or null cons)==list, (and symbol list)==null [type=.2/.3]
 *   - TYPEP: a multidimensional array is NOT a sequence     [proper-sequence.1]
 *   - READ-BYTE on an output-only stream signals an error
 *                                            [read-stream-content-into-bv.2]
 *   - WITH-STANDARD-IO-SYNTAX binds *PRINT-CASE* to :UPCASE
 *                                            [format-symbol.print-case-bound]
 */
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
    cl_repl_init();   /* loads boot.lisp — rotatef/define-modify-macro live there */
}

static void teardown(void)
{
    cl_mem_shutdown();
    platform_shutdown();
}

/* Eval one form, return its printed (prin1) representation. */
static const char *eval_print(const char *str)
{
    static char buf[512];
    int err;
    CL_CATCH(err);
    if (err == CL_ERR_NONE) {
        CL_Obj result = cl_eval_string(str);
        cl_prin1_to_string(result, buf, sizeof(buf));
        CL_UNCATCH();
        return buf;
    } else {
        CL_UNCATCH();
        snprintf(buf, sizeof(buf), "ERROR:%d", err);
        return buf;
    }
}

/* --- DELETE is destructive on lists --- */
TEST(delete_splices_list_in_place)
{
    /* Deleting the middle element must mutate the shared head cons, so the
     * alias x* observes the splice (CLHS allows DELETE to destroy its arg). */
    ASSERT_STR_EQ(eval_print("(let* ((x (list 1 2 3)) (x* x))"
                             "  (delete 2 x) x*)"),
                  "(1 3)");
    /* delete-if / delete-if-not likewise destructive on lists */
    ASSERT_STR_EQ(eval_print("(let* ((x (list 1 2 3 4)) (x* x))"
                             "  (delete-if #'evenp x) x*)"),
                  "(1 3)");
}

/* --- ROTATEF evaluates each place subform exactly once --- */
TEST(rotatef_evaluates_subforms_once)
{
    /* Each place index (incf p) must run once: p ends at 2, not 4. */
    ASSERT_STR_EQ(eval_print("(let ((p 0) (v (vector 10 20 30)))"
                             "  (rotatef (svref v (incf p)) (svref v (incf p))) p)"),
                  "2");
    ASSERT_STR_EQ(eval_print("(let ((v (vector 10 20 30)))"
                             "  (rotatef (aref v 0) (aref v 2)) v)"),
                  "#(30 20 10)");
}

/* --- SHIFTF: single subform eval, returns the first place's old value --- */
TEST(shiftf_evaluates_subforms_once)
{
    ASSERT_STR_EQ(eval_print("(let ((p 0) (v (vector 10 20 30)))"
                             "  (list (shiftf (svref v (incf p)) 99) p v))"),
                  "(20 1 #(10 99 30))");
}

/* --- DEFINE-MODIFY-MACRO expands through GET-SETF-EXPANSION --- */
TEST(define_modify_macro_evaluates_place_once)
{
    /* Define the macro and use it in separate top-level evals (the macro must
     * exist before the using form is compiled). */
    cl_eval_string("(define-modify-macro %t-maxf (&rest n) max)");
    ASSERT_STR_EQ(eval_print("(let ((xv (vector 0 0 0)) (p 0))"
                             "  (%t-maxf (svref xv (incf p)) (incf p))"
                             "  (list p xv))"),
                  "(2 #(0 2 0))");
}

/* --- EVERY returns a boolean T, not the last predicate value --- */
TEST(every_returns_boolean_t)
{
    /* Predicate returns MEMBER's (truthy) tail; EVERY must still yield T. */
    ASSERT_STR_EQ(eval_print("(every (lambda (x) (member x '(1 2 3))) '(1 2 3))"),
                  "T");
    ASSERT_STR_EQ(eval_print("(every (lambda (x) (member x '(1 2 3))) '(1 2 9))"),
                  "NIL");
}

/* --- SUBTYPEP settles exact compound identities --- */
TEST(subtypep_list_union_identity)
{
    ASSERT_STR_EQ(eval_print("(subtypep 'list '(or null cons))"), "T");
    ASSERT_STR_EQ(eval_print("(subtypep '(or null cons) 'list)"), "T");
    ASSERT_STR_EQ(eval_print("(subtypep 'null '(and symbol list))"), "T");
    ASSERT_STR_EQ(eval_print("(subtypep '(and symbol list) 'null)"), "T");
}

/* --- SUBTYPEP: an element-less (member) is the empty type (≡ NIL) --- */
TEST(subtypep_empty_member_is_bottom_type)
{
    /* CLHS 4.2.3 / member: `(member)` with zero elements denotes the empty
     * type, so it is type= to NIL.  Both directions of subtypep must be
     * certain T (the second value), not the "unknown" (NIL NIL). */
    ASSERT_STR_EQ(eval_print("(subtypep '(member) nil)"), "T");
    ASSERT_STR_EQ(eval_print("(subtypep 'nil '(member))"), "T");
    ASSERT_STR_EQ(eval_print("(subtypep '(member) '(member))"), "T");
    /* The empty type is a subtype of any type (NIL ⊆ everything). */
    ASSERT_STR_EQ(eval_print("(subtypep '(member) 'integer)"), "T");
    ASSERT_STR_EQ(eval_print("(subtypep 'nil 'integer)"), "T");
    /* A non-empty type is NOT a subtype of the empty (member). */
    ASSERT_STR_EQ(eval_print("(subtypep 'integer '(member))"), "NIL");
    /* Nothing is of the empty member type. */
    ASSERT_STR_EQ(eval_print("(typep :x '(member))"), "NIL");
    ASSERT_STR_EQ(eval_print("(typep nil '(member))"), "NIL");

    /* (or) with zero disjuncts is also the empty type (CLHS 4.4 / type-specifier OR).
     * tspec_is_empty must recognise both forms; the old code fell into the OR
     * compound handler producing (NIL T) for (subtypep 'nil '(or)). */
    ASSERT_STR_EQ(eval_print("(subtypep '(or) nil)"), "T");
    ASSERT_STR_EQ(eval_print("(subtypep 'nil '(or))"), "T");
    ASSERT_STR_EQ(eval_print("(subtypep '(or) '(or))"), "T");
    ASSERT_STR_EQ(eval_print("(subtypep '(or) 'integer)"), "T");
    ASSERT_STR_EQ(eval_print("(subtypep 'integer '(or))"), "NIL");
    /* (member) and (or) denote the same empty type — mutual subtype. */
    ASSERT_STR_EQ(eval_print("(subtypep '(member) '(or))"), "T");
    ASSERT_STR_EQ(eval_print("(subtypep '(or) '(member))"), "T");
}

/* --- TYPEP: multidimensional array is not a sequence --- */
TEST(typep_md_array_not_sequence)
{
    ASSERT_STR_EQ(eval_print("(typep #2a((1 2) (3 4)) 'sequence)"), "NIL");
    ASSERT_STR_EQ(eval_print("(typep #(1 2 3) 'sequence)"), "T");
    ASSERT_STR_EQ(eval_print("(typep '(1 2 3) 'sequence)"), "T");
}

/* --- READ-BYTE on an output-only stream errors (not EOF) --- */
TEST(read_byte_output_stream_errors)
{
    ASSERT_STR_EQ(eval_print("(handler-case (read-byte (make-broadcast-stream))"
                             "  (error () :err))"),
                  ":ERR");
}

/* --- WITH-STANDARD-IO-SYNTAX binds *PRINT-CASE* to :UPCASE --- */
TEST(with_standard_io_syntax_resets_print_case)
{
    /* Outer :DOWNCASE must not leak inside; FOO prints upcased. */
    ASSERT_STR_EQ(eval_print("(let ((*print-case* :downcase))"
                             "  (with-standard-io-syntax (princ-to-string 'foo)))"),
                  "\"FOO\"");
}

int main(void)
{
    test_init();
    setup();

    RUN(delete_splices_list_in_place);
    RUN(rotatef_evaluates_subforms_once);
    RUN(shiftf_evaluates_subforms_once);
    RUN(define_modify_macro_evaluates_place_once);
    RUN(every_returns_boolean_t);
    RUN(subtypep_list_union_identity);
    RUN(subtypep_empty_member_is_bottom_type);
    RUN(typep_md_array_not_sequence);
    RUN(read_byte_output_stream_errors);
    RUN(with_standard_io_syntax_resets_print_case);

    teardown();
    REPORT();
}
