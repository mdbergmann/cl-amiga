#include "test.h"
#include "core/types.h"
#include "core/mem.h"
#include "core/error.h"
#include "core/package.h"
#include "core/symbol.h"
#include "platform/platform.h"

static void setup(void)
{
    platform_init();
    cl_error_init();
    cl_mem_init(CL_DEFAULT_HEAP_SIZE);
    cl_package_init();
    cl_symbol_init();
}

static void teardown(void)
{
    cl_mem_shutdown();
    platform_shutdown();
}

/* --- Fixnum tests --- */

TEST(fixnum_zero)
{
    CL_Obj obj = CL_MAKE_FIXNUM(0);
    ASSERT(CL_FIXNUM_P(obj));
    ASSERT_EQ_INT(CL_FIXNUM_VAL(obj), 0);
    ASSERT(!CL_NULL_P(obj));
    ASSERT(!CL_CHAR_P(obj));
    ASSERT(!CL_HEAP_P(obj));
}

TEST(fixnum_positive)
{
    CL_Obj obj = CL_MAKE_FIXNUM(42);
    ASSERT(CL_FIXNUM_P(obj));
    ASSERT_EQ_INT(CL_FIXNUM_VAL(obj), 42);
}

TEST(fixnum_negative)
{
    CL_Obj obj = CL_MAKE_FIXNUM(-100);
    ASSERT(CL_FIXNUM_P(obj));
    ASSERT_EQ_INT(CL_FIXNUM_VAL(obj), -100);
}

TEST(fixnum_max)
{
    CL_Obj obj = CL_MAKE_FIXNUM(CL_FIXNUM_MAX);
    ASSERT(CL_FIXNUM_P(obj));
    ASSERT_EQ_INT(CL_FIXNUM_VAL(obj), CL_FIXNUM_MAX);
}

TEST(fixnum_min)
{
    CL_Obj obj = CL_MAKE_FIXNUM(CL_FIXNUM_MIN);
    ASSERT(CL_FIXNUM_P(obj));
    ASSERT_EQ_INT(CL_FIXNUM_VAL(obj), CL_FIXNUM_MIN);
}

/* --- Character tests --- */

TEST(char_basic)
{
    CL_Obj obj = CL_MAKE_CHAR('A');
    ASSERT(CL_CHAR_P(obj));
    ASSERT_EQ_INT(CL_CHAR_VAL(obj), 'A');
    ASSERT(!CL_FIXNUM_P(obj));
    ASSERT(!CL_NULL_P(obj));
    ASSERT(!CL_HEAP_P(obj));
}

TEST(char_space)
{
    CL_Obj obj = CL_MAKE_CHAR(' ');
    ASSERT(CL_CHAR_P(obj));
    ASSERT_EQ_INT(CL_CHAR_VAL(obj), ' ');
}

TEST(char_newline)
{
    CL_Obj obj = CL_MAKE_CHAR('\n');
    ASSERT(CL_CHAR_P(obj));
    ASSERT_EQ_INT(CL_CHAR_VAL(obj), '\n');
}

/* --- NIL tests --- */

TEST(nil_is_zero)
{
    ASSERT_EQ(CL_NIL, (CL_Obj)0);
    ASSERT(CL_NULL_P(CL_NIL));
    ASSERT(!CL_FIXNUM_P(CL_NIL));
    ASSERT(!CL_CHAR_P(CL_NIL));
    ASSERT(!CL_HEAP_P(CL_NIL));
}

TEST(nil_car_cdr)
{
    ASSERT_EQ(cl_car(CL_NIL), CL_NIL);
    ASSERT_EQ(cl_cdr(CL_NIL), CL_NIL);
}

/* --- Cons tests --- */

TEST(cons_basic)
{
    CL_Obj c = cl_cons(CL_MAKE_FIXNUM(1), CL_MAKE_FIXNUM(2));
    ASSERT(CL_HEAP_P(c));
    ASSERT(CL_CONS_P(c));
    ASSERT_EQ_INT(CL_FIXNUM_VAL(cl_car(c)), 1);
    ASSERT_EQ_INT(CL_FIXNUM_VAL(cl_cdr(c)), 2);
}

TEST(cons_list)
{
    /* (1 2 3) = (cons 1 (cons 2 (cons 3 nil))) */
    CL_Obj list = cl_cons(CL_MAKE_FIXNUM(1),
                   cl_cons(CL_MAKE_FIXNUM(2),
                   cl_cons(CL_MAKE_FIXNUM(3), CL_NIL)));
    ASSERT(CL_CONS_P(list));
    ASSERT_EQ_INT(CL_FIXNUM_VAL(cl_car(list)), 1);
    ASSERT_EQ_INT(CL_FIXNUM_VAL(cl_car(cl_cdr(list))), 2);
    ASSERT_EQ_INT(CL_FIXNUM_VAL(cl_car(cl_cdr(cl_cdr(list)))), 3);
    ASSERT(CL_NULL_P(cl_cdr(cl_cdr(cl_cdr(list)))));
}

/* --- Symbol tests --- */

TEST(symbol_t)
{
    ASSERT(!CL_NULL_P(SYM_T));
    ASSERT(CL_SYMBOL_P(SYM_T));
    ASSERT_STR_EQ(cl_symbol_name(SYM_T), "T");
}

TEST(symbol_intern)
{
    CL_Obj s1 = cl_intern("FOO", 3);
    CL_Obj s2 = cl_intern("FOO", 3);
    ASSERT(CL_SYMBOL_P(s1));
    ASSERT_EQ(s1, s2);  /* Same symbol object */
}

TEST(symbol_different)
{
    CL_Obj s1 = cl_intern("AAA", 3);
    CL_Obj s2 = cl_intern("BBB", 3);
    ASSERT(s1 != s2);
}

/* --- String tests --- */

TEST(string_basic)
{
    CL_Obj s = cl_make_string("hello", 5);
    CL_String *str;
    ASSERT(CL_HEAP_P(s));
    ASSERT(CL_STRING_P(s));
    str = (CL_String *)CL_OBJ_TO_PTR(s);
    ASSERT_EQ_INT((int)str->length, 5);
    ASSERT_STR_EQ(str->data, "hello");
}

TEST(string_empty)
{
    CL_Obj s = cl_make_string("", 0);
    CL_String *str = (CL_String *)CL_OBJ_TO_PTR(s);
    ASSERT(CL_STRING_P(s));
    ASSERT_EQ_INT((int)str->length, 0);
}

/* --- Type name --- */

TEST(type_name)
{
    ASSERT_STR_EQ(cl_type_name(CL_NIL), "NULL");
    ASSERT_STR_EQ(cl_type_name(CL_MAKE_FIXNUM(1)), "FIXNUM");
    ASSERT_STR_EQ(cl_type_name(CL_MAKE_CHAR('a')), "CHARACTER");
    ASSERT_STR_EQ(cl_type_name(cl_cons(CL_NIL, CL_NIL)), "CONS");
    ASSERT_STR_EQ(cl_type_name(SYM_T), "SYMBOL");
    ASSERT_STR_EQ(cl_type_name(cl_make_string("x", 1)), "STRING");
}

int main(void)
{
    test_init();
    setup();

    RUN(fixnum_zero);
    RUN(fixnum_positive);
    RUN(fixnum_negative);
    RUN(fixnum_max);
    RUN(fixnum_min);
    RUN(char_basic);
    RUN(char_space);
    RUN(char_newline);
    RUN(nil_is_zero);
    RUN(nil_car_cdr);
    RUN(cons_basic);
    RUN(cons_list);
    RUN(symbol_t);
    RUN(symbol_intern);
    RUN(symbol_different);
    RUN(string_basic);
    RUN(string_empty);
    RUN(type_name);

    teardown();
    REPORT();
}
