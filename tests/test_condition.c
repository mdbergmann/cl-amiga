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
    cl_error_init();
    cl_mem_init(CL_DEFAULT_HEAP_SIZE);
    cl_package_init();
    cl_symbol_init();
    cl_reader_init();
    cl_printer_init();
    cl_compiler_init();
    cl_vm_init();
    cl_builtins_init();
    cl_repl_init();
}

static void teardown(void)
{
    cl_mem_shutdown();
    platform_shutdown();
}

/* Helper: eval a string, return printed result */
static const char *eval_print(const char *str)
{
    static char buf[512];
    int err;

    err = CL_CATCH();
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

/* --- C-level tests --- */

TEST(c_make_condition)
{
    CL_Obj cond = cl_make_condition(SYM_TYPE_ERROR, CL_NIL, CL_NIL);
    ASSERT(CL_CONDITION_P(cond));
    ASSERT(!CL_NULL_P(cond));

    {
        CL_Condition *c = (CL_Condition *)CL_OBJ_TO_PTR(cond);
        ASSERT_EQ(c->type_name, SYM_TYPE_ERROR);
        ASSERT_EQ(c->slots, CL_NIL);
        ASSERT_EQ(c->report_string, CL_NIL);
    }
}

TEST(c_condition_p)
{
    CL_Obj cond = cl_make_condition(SYM_SIMPLE_ERROR, CL_NIL, CL_NIL);
    ASSERT(CL_CONDITION_P(cond));

    /* Non-conditions */
    ASSERT(!CL_CONDITION_P(CL_NIL));
    ASSERT(!CL_CONDITION_P(CL_MAKE_FIXNUM(42)));
    ASSERT(!CL_CONDITION_P(SYM_T));
}

TEST(c_condition_with_slots)
{
    CL_Obj slots, pair1, pair2, cond;
    CL_Obj report;

    report = cl_make_string("test error", 10);
    CL_GC_PROTECT(report);

    pair1 = cl_cons(KW_DATUM, CL_MAKE_FIXNUM(42));
    CL_GC_PROTECT(pair1);

    pair2 = cl_cons(KW_EXPECTED_TYPE, SYM_T);
    CL_GC_PROTECT(pair2);

    slots = cl_cons(pair1, cl_cons(pair2, CL_NIL));
    CL_GC_PROTECT(slots);

    cond = cl_make_condition(SYM_TYPE_ERROR, slots, report);
    CL_GC_UNPROTECT(4);

    ASSERT(CL_CONDITION_P(cond));
    {
        CL_Condition *c = (CL_Condition *)CL_OBJ_TO_PTR(cond);
        ASSERT_EQ(c->type_name, SYM_TYPE_ERROR);
        ASSERT(!CL_NULL_P(c->slots));
        ASSERT_EQ(c->report_string, report);
    }
}

/* --- C-level hierarchy test --- */

TEST(c_hierarchy_matches)
{
    extern int cl_condition_type_matches(CL_Obj cond_type, CL_Obj handler_type);

    /* TYPE-ERROR matches itself */
    ASSERT(cl_condition_type_matches(SYM_TYPE_ERROR, SYM_TYPE_ERROR));

    /* TYPE-ERROR matches ERROR, SERIOUS-CONDITION, CONDITION */
    ASSERT(cl_condition_type_matches(SYM_TYPE_ERROR, SYM_ERROR_COND));
    ASSERT(cl_condition_type_matches(SYM_TYPE_ERROR, SYM_SERIOUS_CONDITION));
    ASSERT(cl_condition_type_matches(SYM_TYPE_ERROR, SYM_CONDITION));

    /* TYPE-ERROR does NOT match WARNING */
    ASSERT(!cl_condition_type_matches(SYM_TYPE_ERROR, SYM_WARNING));

    /* SIMPLE-ERROR matches both ERROR and SIMPLE-CONDITION */
    ASSERT(cl_condition_type_matches(SYM_SIMPLE_ERROR, SYM_ERROR_COND));
    ASSERT(cl_condition_type_matches(SYM_SIMPLE_ERROR, SYM_SIMPLE_CONDITION));
    ASSERT(cl_condition_type_matches(SYM_SIMPLE_ERROR, SYM_CONDITION));

    /* SIMPLE-WARNING matches both WARNING and SIMPLE-CONDITION */
    ASSERT(cl_condition_type_matches(SYM_SIMPLE_WARNING, SYM_WARNING));
    ASSERT(cl_condition_type_matches(SYM_SIMPLE_WARNING, SYM_SIMPLE_CONDITION));

    /* DIVISION-BY-ZERO matches ARITHMETIC-ERROR, ERROR, CONDITION */
    ASSERT(cl_condition_type_matches(SYM_DIVISION_BY_ZERO, SYM_ARITHMETIC_ERROR));
    ASSERT(cl_condition_type_matches(SYM_DIVISION_BY_ZERO, SYM_ERROR_COND));
    ASSERT(cl_condition_type_matches(SYM_DIVISION_BY_ZERO, SYM_CONDITION));

    /* WARNING does NOT match ERROR */
    ASSERT(!cl_condition_type_matches(SYM_WARNING, SYM_ERROR_COND));

    /* CONDITION does NOT match ERROR */
    ASSERT(!cl_condition_type_matches(SYM_CONDITION, SYM_ERROR_COND));
}

/* --- C-level signal tests --- */

TEST(c_signal_no_handlers)
{
    /* cl_signal_condition with empty handler stack returns NIL */
    CL_Obj cond = cl_make_condition(SYM_SIMPLE_CONDITION, CL_NIL, CL_NIL);
    CL_Obj result = cl_signal_condition(cond);
    ASSERT_EQ(result, CL_NIL);
}

TEST(c_error_creates_condition)
{
    /* Verify cl_error() still recovers via CL_CATCH (backward compat) */
    int err = CL_CATCH();
    if (err == CL_ERR_NONE) {
        cl_error(CL_ERR_GENERAL, "test backward compat");
        CL_UNCATCH();
        ASSERT(0);  /* Should not reach here */
    } else {
        CL_UNCATCH();
        ASSERT_EQ_INT(err, CL_ERR_GENERAL);
    }
}

TEST(c_create_condition_from_error)
{
    CL_Obj cond;
    CL_Condition *c;

    cond = cl_create_condition_from_error(CL_ERR_TYPE, "type mismatch");
    ASSERT(CL_CONDITION_P(cond));
    c = (CL_Condition *)CL_OBJ_TO_PTR(cond);
    ASSERT_EQ(c->type_name, SYM_TYPE_ERROR);
    ASSERT(!CL_NULL_P(c->report_string));

    cond = cl_create_condition_from_error(CL_ERR_DIVZERO, "div by zero");
    c = (CL_Condition *)CL_OBJ_TO_PTR(cond);
    ASSERT_EQ(c->type_name, SYM_DIVISION_BY_ZERO);

    cond = cl_create_condition_from_error(CL_ERR_UNBOUND, "unbound");
    c = (CL_Condition *)CL_OBJ_TO_PTR(cond);
    ASSERT_EQ(c->type_name, SYM_UNBOUND_VARIABLE_COND);

    cond = cl_create_condition_from_error(CL_ERR_UNDEFINED, "undef");
    c = (CL_Condition *)CL_OBJ_TO_PTR(cond);
    ASSERT_EQ(c->type_name, SYM_UNDEFINED_FUNCTION_COND);

    cond = cl_create_condition_from_error(CL_ERR_ARGS, "bad args");
    c = (CL_Condition *)CL_OBJ_TO_PTR(cond);
    ASSERT_EQ(c->type_name, SYM_PROGRAM_ERROR);

    cond = cl_create_condition_from_error(CL_ERR_GENERAL, "generic");
    c = (CL_Condition *)CL_OBJ_TO_PTR(cond);
    ASSERT_EQ(c->type_name, SYM_SIMPLE_ERROR);
}

TEST(c_handler_stack_nlx)
{
    /* Push a handler, set up NLX frame, verify handler_top restored after NLX */
    extern CL_HandlerBinding cl_handler_stack[];
    extern int cl_handler_top;

    cl_handler_top = 0;
    cl_handler_stack[0].type_name = SYM_CONDITION;
    cl_handler_stack[0].handler = CL_NIL;
    cl_handler_stack[0].handler_mark = 0;
    cl_handler_top = 1;

    {
        int err = CL_CATCH();
        if (err == CL_ERR_NONE) {
            /* handler_top should be 1 here */
            ASSERT_EQ_INT(cl_handler_top, 1);
            cl_error(CL_ERR_GENERAL, "test nlx");
            CL_UNCATCH();
        } else {
            CL_UNCATCH();
            /* After error recovery, handler_top should be reset to 0 */
            ASSERT_EQ_INT(cl_handler_top, 0);
        }
    }
}

/* --- Lisp-level tests --- */

TEST(lisp_conditionp)
{
    ASSERT_STR_EQ(eval_print("(conditionp (make-condition 'simple-error :format-control \"test\"))"), "T");
    ASSERT_STR_EQ(eval_print("(conditionp 42)"), "NIL");
    ASSERT_STR_EQ(eval_print("(conditionp nil)"), "NIL");
    ASSERT_STR_EQ(eval_print("(conditionp \"hello\")"), "NIL");
}

TEST(lisp_make_condition)
{
    ASSERT_STR_EQ(eval_print("(conditionp (make-condition 'type-error :datum 42 :expected-type 'string))"), "T");
    ASSERT_STR_EQ(eval_print("(conditionp (make-condition 'warning))"), "T");
    ASSERT_STR_EQ(eval_print("(conditionp (make-condition 'condition))"), "T");
}

TEST(lisp_condition_type_name)
{
    ASSERT_STR_EQ(eval_print("(condition-type-name (make-condition 'type-error))"), "TYPE-ERROR");
    ASSERT_STR_EQ(eval_print("(condition-type-name (make-condition 'simple-error))"), "SIMPLE-ERROR");
    ASSERT_STR_EQ(eval_print("(condition-type-name (make-condition 'warning))"), "WARNING");
}

TEST(lisp_type_error_accessors)
{
    ASSERT_STR_EQ(eval_print("(type-error-datum (make-condition 'type-error :datum 42))"), "42");
    ASSERT_STR_EQ(eval_print("(type-error-expected-type (make-condition 'type-error :expected-type 'string))"), "STRING");
    /* Missing slot returns NIL */
    ASSERT_STR_EQ(eval_print("(type-error-datum (make-condition 'type-error))"), "NIL");
}

TEST(lisp_simple_condition_accessors)
{
    ASSERT_STR_EQ(eval_print("(simple-condition-format-control (make-condition 'simple-error :format-control \"oops\"))"), "\"oops\"");
    ASSERT_STR_EQ(eval_print("(simple-condition-format-arguments (make-condition 'simple-error :format-arguments '(1 2)))"), "(1 2)");
}

TEST(lisp_type_of)
{
    ASSERT_STR_EQ(eval_print("(type-of (make-condition 'type-error))"), "TYPE-ERROR");
    ASSERT_STR_EQ(eval_print("(type-of (make-condition 'simple-error))"), "SIMPLE-ERROR");
    ASSERT_STR_EQ(eval_print("(type-of (make-condition 'warning))"), "WARNING");
}

TEST(lisp_typep_condition)
{
    /* Basic typep */
    ASSERT_STR_EQ(eval_print("(typep (make-condition 'type-error) 'condition)"), "T");
    ASSERT_STR_EQ(eval_print("(typep (make-condition 'type-error) 'error)"), "T");
    ASSERT_STR_EQ(eval_print("(typep (make-condition 'type-error) 'serious-condition)"), "T");
    ASSERT_STR_EQ(eval_print("(typep (make-condition 'type-error) 'type-error)"), "T");

    /* Negative cases */
    ASSERT_STR_EQ(eval_print("(typep (make-condition 'type-error) 'warning)"), "NIL");
    ASSERT_STR_EQ(eval_print("(typep (make-condition 'warning) 'error)"), "NIL");

    /* Warning hierarchy */
    ASSERT_STR_EQ(eval_print("(typep (make-condition 'warning) 'condition)"), "T");
    ASSERT_STR_EQ(eval_print("(typep (make-condition 'simple-warning) 'warning)"), "T");
    ASSERT_STR_EQ(eval_print("(typep (make-condition 'simple-warning) 'simple-condition)"), "T");

    /* Non-condition doesn't match */
    ASSERT_STR_EQ(eval_print("(typep 42 'condition)"), "NIL");
}

TEST(lisp_printer)
{
    /* Condition without report_string */
    ASSERT_STR_EQ(eval_print("(make-condition 'type-error :datum 42)"), "#<CONDITION TYPE-ERROR>");

    /* Condition with report_string */
    ASSERT_STR_EQ(eval_print("(make-condition 'simple-error :format-control \"bad thing\")"),
                  "#<CONDITION SIMPLE-ERROR: \"bad thing\">");
}

/* --- Lisp-level signal/warn/error tests --- */

TEST(lisp_signal_returns_nil)
{
    ASSERT_STR_EQ(eval_print("(signal (make-condition 'simple-condition :format-control \"test\"))"), "NIL");
}

TEST(lisp_signal_with_string)
{
    ASSERT_STR_EQ(eval_print("(signal \"something happened\")"), "NIL");
}

TEST(lisp_signal_with_symbol)
{
    ASSERT_STR_EQ(eval_print("(signal 'simple-condition)"), "NIL");
}

TEST(lisp_warn_returns_nil)
{
    ASSERT_STR_EQ(eval_print("(warn \"test warning\")"), "NIL");
}

TEST(lisp_warn_with_symbol)
{
    ASSERT_STR_EQ(eval_print("(warn 'simple-warning)"), "NIL");
}

TEST(lisp_error_still_caught)
{
    /* (error "test") caught by CL_CATCH in eval_print */
    const char *result = eval_print("(error \"test error\")");
    /* Should be caught as an error, not crash */
    ASSERT_STR_EQ(result, "ERROR:1");
}

TEST(lisp_error_with_symbol)
{
    /* (error 'type-error :datum 42) */
    const char *result = eval_print("(error 'type-error :datum 42)");
    ASSERT_STR_EQ(result, "ERROR:1");
}

int main(void)
{
    setup();

    /* C-level tests */
    RUN(c_make_condition);
    RUN(c_condition_p);
    RUN(c_condition_with_slots);
    RUN(c_hierarchy_matches);
    RUN(c_signal_no_handlers);
    RUN(c_error_creates_condition);
    RUN(c_create_condition_from_error);
    RUN(c_handler_stack_nlx);

    /* Lisp-level tests */
    RUN(lisp_conditionp);
    RUN(lisp_make_condition);
    RUN(lisp_condition_type_name);
    RUN(lisp_type_error_accessors);
    RUN(lisp_simple_condition_accessors);
    RUN(lisp_type_of);
    RUN(lisp_typep_condition);
    RUN(lisp_printer);

    /* Signal/warn/error tests */
    RUN(lisp_signal_returns_nil);
    RUN(lisp_signal_with_string);
    RUN(lisp_signal_with_symbol);
    RUN(lisp_warn_returns_nil);
    RUN(lisp_warn_with_symbol);
    RUN(lisp_error_still_caught);
    RUN(lisp_error_with_symbol);

    teardown();
    REPORT();
}
