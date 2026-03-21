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

/* Component access functions from builtins_inspect.c */
extern int cl_inspect_component_count(CL_Obj obj);
extern CL_Obj cl_inspect_get_component(CL_Obj obj, int idx, const char **label);

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

/* Helper: evaluate expression and return CL_Obj */
static CL_Obj eval(const char *expr)
{
    return cl_eval_string(expr);
}

/* --- Component count tests --- */

TEST(inspect_count_nil)
{
    ASSERT_EQ(0, cl_inspect_component_count(CL_NIL));
}

TEST(inspect_count_fixnum)
{
    ASSERT_EQ(0, cl_inspect_component_count(CL_MAKE_FIXNUM(42)));
}

TEST(inspect_count_character)
{
    ASSERT_EQ(0, cl_inspect_component_count(CL_MAKE_CHAR('A')));
}

TEST(inspect_count_cons)
{
    CL_Obj c = eval("'(1 . 2)");
    ASSERT_EQ(2, cl_inspect_component_count(c));
}

TEST(inspect_count_symbol)
{
    CL_Obj s = eval("'car");
    ASSERT_EQ(5, cl_inspect_component_count(s));
}

TEST(inspect_count_string)
{
    CL_Obj s = eval("\"hello\"");
    ASSERT_EQ(5, cl_inspect_component_count(s));
}

TEST(inspect_count_vector)
{
    CL_Obj v = eval("(vector 10 20 30)");
    ASSERT_EQ(3, cl_inspect_component_count(v));
}

TEST(inspect_count_struct)
{
    eval("(defstruct itest-pt x y z)");
    CL_Obj s = eval("(make-itest-pt :x 1 :y 2 :z 3)");
    ASSERT_EQ(3, cl_inspect_component_count(s));
}

TEST(inspect_count_hashtable)
{
    CL_Obj h = eval("(let ((h (make-hash-table))) "
                     "  (setf (gethash 'a h) 1) "
                     "  (setf (gethash 'b h) 2) h)");
    ASSERT_EQ(2, cl_inspect_component_count(h));
}

TEST(inspect_count_pathname)
{
    CL_Obj p = eval("#P\"/tmp/foo.txt\"");
    ASSERT_EQ(6, cl_inspect_component_count(p));
}

TEST(inspect_count_ratio)
{
    CL_Obj r = eval("2/3");
    ASSERT_EQ(2, cl_inspect_component_count(r));
}

TEST(inspect_count_package)
{
    CL_Obj p = eval("(find-package :cl)");
    ASSERT_EQ(3, cl_inspect_component_count(p));
}

/* --- Component access tests --- */

TEST(inspect_cons_car)
{
    CL_Obj c = eval("'(42 . 99)");
    const char *label;
    CL_Obj comp = cl_inspect_get_component(c, 0, &label);
    ASSERT_EQ(42, CL_FIXNUM_VAL(comp));
    ASSERT(strcmp(label, "Car") == 0);
}

TEST(inspect_cons_cdr)
{
    CL_Obj c = eval("'(42 . 99)");
    const char *label;
    CL_Obj comp = cl_inspect_get_component(c, 1, &label);
    ASSERT_EQ(99, CL_FIXNUM_VAL(comp));
    ASSERT(strcmp(label, "Cdr") == 0);
}

TEST(inspect_symbol_name)
{
    CL_Obj s = eval("'car");
    const char *label;
    CL_Obj comp = cl_inspect_get_component(s, 0, &label);
    ASSERT(strcmp(label, "Name") == 0);
    ASSERT(CL_STRING_P(comp));
}

TEST(inspect_symbol_function)
{
    CL_Obj s = eval("'car");
    const char *label;
    CL_Obj comp = cl_inspect_get_component(s, 2, &label);
    ASSERT(strcmp(label, "Function") == 0);
    ASSERT(CL_FUNCTION_P(comp));
}

TEST(inspect_vector_elements)
{
    CL_Obj v = eval("(vector 10 20 30)");
    const char *label;
    CL_Obj c0 = cl_inspect_get_component(v, 0, &label);
    CL_Obj c1 = cl_inspect_get_component(v, 1, &label);
    CL_Obj c2 = cl_inspect_get_component(v, 2, &label);
    ASSERT_EQ(10, CL_FIXNUM_VAL(c0));
    ASSERT_EQ(20, CL_FIXNUM_VAL(c1));
    ASSERT_EQ(30, CL_FIXNUM_VAL(c2));
}

TEST(inspect_string_chars)
{
    CL_Obj s = eval("\"AB\"");
    const char *label;
    CL_Obj c0 = cl_inspect_get_component(s, 0, &label);
    CL_Obj c1 = cl_inspect_get_component(s, 1, &label);
    ASSERT(CL_CHAR_P(c0));
    ASSERT_EQ('A', CL_CHAR_VAL(c0));
    ASSERT(CL_CHAR_P(c1));
    ASSERT_EQ('B', CL_CHAR_VAL(c1));
}

TEST(inspect_ratio_parts)
{
    CL_Obj r = eval("2/3");
    const char *label;
    CL_Obj num = cl_inspect_get_component(r, 0, &label);
    ASSERT(strcmp(label, "Numerator") == 0);
    ASSERT_EQ(2, CL_FIXNUM_VAL(num));
    CL_Obj den = cl_inspect_get_component(r, 1, &label);
    ASSERT(strcmp(label, "Denominator") == 0);
    ASSERT_EQ(3, CL_FIXNUM_VAL(den));
}

TEST(inspect_pathname_components)
{
    CL_Obj p = eval("#P\"/tmp/foo.txt\"");
    const char *label;
    CL_Obj name = cl_inspect_get_component(p, 3, &label);
    ASSERT(strcmp(label, "Name") == 0);
    ASSERT(CL_STRING_P(name));
}

TEST(inspect_struct_slots)
{
    eval("(defstruct itest-pt2 a b)");
    CL_Obj s = eval("(make-itest-pt2 :a 77 :b 88)");
    const char *label;
    CL_Obj c0 = cl_inspect_get_component(s, 0, &label);
    CL_Obj c1 = cl_inspect_get_component(s, 1, &label);
    ASSERT_EQ(77, CL_FIXNUM_VAL(c0));
    ASSERT_EQ(88, CL_FIXNUM_VAL(c1));
}

TEST(inspect_out_of_range)
{
    CL_Obj c = eval("'(1 . 2)");
    const char *label;
    CL_Obj comp = cl_inspect_get_component(c, 5, &label);
    ASSERT(CL_NULL_P(comp));
}

TEST(inspect_is_bound)
{
    /* Verify inspect is a bound function */
    const char *r;
    static char buf[64];
    int err = CL_CATCH();
    if (err == CL_ERR_NONE) {
        CL_Obj result = cl_eval_string("(fboundp 'inspect)");
        cl_prin1_to_string(result, buf, sizeof(buf));
        r = buf;
        CL_UNCATCH();
    } else {
        CL_UNCATCH();
        r = "ERROR";
    }
    ASSERT(strcmp(r, "T") == 0);
}

int main(void)
{
    test_init();
    setup();

    /* Component count tests */
    RUN(inspect_count_nil);
    RUN(inspect_count_fixnum);
    RUN(inspect_count_character);
    RUN(inspect_count_cons);
    RUN(inspect_count_symbol);
    RUN(inspect_count_string);
    RUN(inspect_count_vector);
    RUN(inspect_count_struct);
    RUN(inspect_count_hashtable);
    RUN(inspect_count_pathname);
    RUN(inspect_count_ratio);
    RUN(inspect_count_package);

    /* Component access tests */
    RUN(inspect_cons_car);
    RUN(inspect_cons_cdr);
    RUN(inspect_symbol_name);
    RUN(inspect_symbol_function);
    RUN(inspect_vector_elements);
    RUN(inspect_string_chars);
    RUN(inspect_ratio_parts);
    RUN(inspect_pathname_components);
    RUN(inspect_struct_slots);
    RUN(inspect_out_of_range);
    RUN(inspect_is_bound);

    teardown();
    REPORT();
}
