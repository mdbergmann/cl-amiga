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
    cl_vm_init(0, 0);
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
        cl_vm.sp = 0;
        cl_vm.fp = 0;
        return buf;
    }
}

/* --- Basic defstruct --- */

TEST(struct_basic_define)
{
    ASSERT_STR_EQ(eval_print("(defstruct point (x 0) (y 0))"), "POINT");
}

TEST(struct_constructor)
{
    eval_print("(defstruct point2 (x 0) (y 0))");
    /* Default values */
    ASSERT_STR_EQ(eval_print("(point2-x (make-point2))"), "0");
    ASSERT_STR_EQ(eval_print("(point2-y (make-point2))"), "0");
    /* Keyword arguments */
    ASSERT_STR_EQ(eval_print("(point2-x (make-point2 :x 10 :y 20))"), "10");
    ASSERT_STR_EQ(eval_print("(point2-y (make-point2 :x 10 :y 20))"), "20");
}

TEST(struct_predicate)
{
    eval_print("(defstruct dog3 (name \"Rex\"))");
    ASSERT_STR_EQ(eval_print("(dog3-p (make-dog3))"), "T");
    ASSERT_STR_EQ(eval_print("(dog3-p 42)"), "NIL");
    ASSERT_STR_EQ(eval_print("(dog3-p nil)"), "NIL");
}

TEST(struct_setf_accessor)
{
    eval_print("(defstruct box4 (width 0) (height 0))");
    ASSERT_STR_EQ(eval_print(
        "(let ((b (make-box4 :width 5 :height 10)))"
        "  (setf (box4-width b) 99)"
        "  (box4-width b))"), "99");
}

TEST(struct_copier)
{
    eval_print("(defstruct item5 (val 0))");
    ASSERT_STR_EQ(eval_print(
        "(let ((a (make-item5 :val 42)))"
        "  (let ((b (copy-item5 a)))"
        "    (setf (item5-val b) 99)"
        "    (item5-val a)))"), "42");
}

TEST(struct_typep)
{
    eval_print("(defstruct thing6 (data nil))");
    ASSERT_STR_EQ(eval_print("(typep (make-thing6) 'thing6)"), "T");
    ASSERT_STR_EQ(eval_print("(typep 42 'thing6)"), "NIL");
}

TEST(struct_type_of)
{
    eval_print("(defstruct widget7 (label \"x\"))");
    ASSERT_STR_EQ(eval_print("(type-of (make-widget7))"), "WIDGET7");
}

TEST(struct_printer)
{
    eval_print("(defstruct pt8 (x 0) (y 0))");
    ASSERT_STR_EQ(eval_print("(make-pt8 :x 1 :y 2)"), "#S(PT8 :X 1 :Y 2)");
}

TEST(struct_structurep)
{
    eval_print("(defstruct s9 (a 1))");
    ASSERT_STR_EQ(eval_print("(structurep (make-s9))"), "T");
    ASSERT_STR_EQ(eval_print("(structurep 42)"), "NIL");
    ASSERT_STR_EQ(eval_print("(structurep nil)"), "NIL");
}

/* --- Options --- */

TEST(struct_conc_name)
{
    eval_print("(defstruct (color10 (:conc-name c-)) (r 0) (g 0) (b 0))");
    ASSERT_STR_EQ(eval_print("(c-r (make-color10 :r 255))"), "255");
    ASSERT_STR_EQ(eval_print("(c-g (make-color10 :g 128))"), "128");
}

TEST(struct_conc_name_nil)
{
    /* nil conc-name means no prefix */
    eval_print("(defstruct (bare11 (:conc-name nil)) (field 99))");
    ASSERT_STR_EQ(eval_print("(field (make-bare11))"), "99");
}

TEST(struct_constructor_name)
{
    eval_print("(defstruct (vec12 (:constructor new-vec)) (x 0) (y 0))");
    ASSERT_STR_EQ(eval_print("(vec12-x (new-vec :x 7))"), "7");
}

TEST(struct_constructor_nil)
{
    /* No constructor generated */
    eval_print("(defstruct (noc13 (:constructor nil)) (a 1))");
    /* make-noc13 should not exist */
    ASSERT(strncmp(eval_print("(make-noc13)"), "ERROR:", 6) == 0);
}

TEST(struct_predicate_name)
{
    eval_print("(defstruct (pred14 (:predicate is-pred14)) (x 0))");
    ASSERT_STR_EQ(eval_print("(is-pred14 (make-pred14))"), "T");
}

TEST(struct_predicate_nil)
{
    eval_print("(defstruct (nop15 (:predicate nil)) (x 0))");
    /* nop15-p should not exist */
    ASSERT(strncmp(eval_print("(nop15-p (make-nop15))"), "ERROR:", 6) == 0);
}

TEST(struct_copier_name)
{
    eval_print("(defstruct (cp16 (:copier clone-cp16)) (x 0))");
    ASSERT_STR_EQ(eval_print("(cp16-x (clone-cp16 (make-cp16 :x 42)))"), "42");
}

TEST(struct_copier_nil)
{
    eval_print("(defstruct (nocp17 (:copier nil)) (x 0))");
    /* copy-nocp17 should not exist */
    ASSERT(strncmp(eval_print("(copy-nocp17 (make-nocp17))"), "ERROR:", 6) == 0);
}

/* --- Inheritance (:include) --- */

TEST(struct_include_basic)
{
    eval_print("(defstruct parent18 (a 1) (b 2))");
    eval_print("(defstruct (child18 (:include parent18)) (c 3))");
    ASSERT_STR_EQ(eval_print("(child18-a (make-child18 :a 10 :c 30))"), "10");
    ASSERT_STR_EQ(eval_print("(child18-b (make-child18))"), "2");
    ASSERT_STR_EQ(eval_print("(child18-c (make-child18 :c 30))"), "30");
}

TEST(struct_include_typep)
{
    eval_print("(defstruct parent19 (x 0))");
    eval_print("(defstruct (child19 (:include parent19)) (y 0))");
    ASSERT_STR_EQ(eval_print("(typep (make-child19) 'child19)"), "T");
    ASSERT_STR_EQ(eval_print("(typep (make-child19) 'parent19)"), "T");
    /* Parent is not a child */
    ASSERT_STR_EQ(eval_print("(typep (make-parent19) 'child19)"), "NIL");
}

TEST(struct_include_predicate)
{
    eval_print("(defstruct base20 (x 0))");
    eval_print("(defstruct (derived20 (:include base20)) (y 0))");
    /* Derived predicate works */
    ASSERT_STR_EQ(eval_print("(derived20-p (make-derived20))"), "T");
    /* Parent predicate also matches derived */
    ASSERT_STR_EQ(eval_print("(base20-p (make-derived20))"), "T");
}

/* --- Multiple slots / defaults --- */

TEST(struct_no_slots)
{
    eval_print("(defstruct empty21)");
    ASSERT_STR_EQ(eval_print("(empty21-p (make-empty21))"), "T");
    ASSERT_STR_EQ(eval_print("(type-of (make-empty21))"), "EMPTY21");
}

TEST(struct_slot_no_default)
{
    /* Slot with no default gets NIL */
    eval_print("(defstruct nd22 x y)");
    ASSERT_STR_EQ(eval_print("(nd22-x (make-nd22))"), "NIL");
    ASSERT_STR_EQ(eval_print("(nd22-y (make-nd22))"), "NIL");
    ASSERT_STR_EQ(eval_print("(nd22-x (make-nd22 :x 7))"), "7");
}

TEST(struct_boa_constructor)
{
    /* BOA constructor with positional args */
    eval_print("(defstruct (boa1 (:constructor make-boa1 (x y))) x y)");
    ASSERT_STR_EQ(eval_print("(boa1-x (make-boa1 10 20))"), "10");
    ASSERT_STR_EQ(eval_print("(boa1-y (make-boa1 10 20))"), "20");
    /* BOA with &optional — bare param inherits slot default (CL spec 3.4.6) */
    eval_print("(defstruct (boa2 (:constructor make-boa2 (a &optional b)))"
               "  (a nil) (b t))");
    ASSERT_STR_EQ(eval_print("(boa2-b (make-boa2 1))"), "T");
    ASSERT_STR_EQ(eval_print("(boa2-b (make-boa2 1 42))"), "42");
    /* BOA with &optional explicit default overrides slot default */
    eval_print("(defstruct (boa3 (:constructor make-boa3 (a &optional (b 99))))"
               "  (a nil) (b t))");
    ASSERT_STR_EQ(eval_print("(boa3-b (make-boa3 1))"), "99");
}

int main(void)
{
    test_init();
    setup();

    RUN(struct_basic_define);
    RUN(struct_constructor);
    RUN(struct_predicate);
    RUN(struct_setf_accessor);
    RUN(struct_copier);
    RUN(struct_typep);
    RUN(struct_type_of);
    RUN(struct_printer);
    RUN(struct_structurep);

    /* Options */
    RUN(struct_conc_name);
    RUN(struct_conc_name_nil);
    RUN(struct_constructor_name);
    RUN(struct_constructor_nil);
    RUN(struct_predicate_name);
    RUN(struct_predicate_nil);
    RUN(struct_copier_name);
    RUN(struct_copier_nil);

    /* Inheritance */
    RUN(struct_include_basic);
    RUN(struct_include_typep);
    RUN(struct_include_predicate);

    /* Multiple slots / defaults */
    RUN(struct_no_slots);
    RUN(struct_slot_no_default);
    RUN(struct_boa_constructor);

    teardown();
    REPORT();
}
