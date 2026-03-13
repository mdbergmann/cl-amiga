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

/* Helper: read from string */
static CL_Obj reads(const char *str)
{
    CL_ReadStream stream;
    stream.buf = str;
    stream.pos = 0;
    stream.len = (int)strlen(str);
    stream.line = 1;
    return cl_read_from_string(&stream);
}

/* Helper: read and print to buffer for comparison */
static const char *read_print(const char *str)
{
    static char buf[256];
    CL_Obj obj = reads(str);
    cl_prin1_to_string(obj, buf, sizeof(buf));
    return buf;
}

/* --- Integer reading --- */

TEST(read_integer)
{
    CL_Obj obj = reads("42");
    ASSERT(CL_FIXNUM_P(obj));
    ASSERT_EQ_INT(CL_FIXNUM_VAL(obj), 42);
}

TEST(read_negative_integer)
{
    CL_Obj obj = reads("-7");
    ASSERT(CL_FIXNUM_P(obj));
    ASSERT_EQ_INT(CL_FIXNUM_VAL(obj), -7);
}

TEST(read_zero)
{
    CL_Obj obj = reads("0");
    ASSERT(CL_FIXNUM_P(obj));
    ASSERT_EQ_INT(CL_FIXNUM_VAL(obj), 0);
}

/* --- Symbol reading --- */

TEST(read_symbol)
{
    CL_Obj obj = reads("foo");
    ASSERT(CL_SYMBOL_P(obj));
    ASSERT_STR_EQ(cl_symbol_name(obj), "FOO");
}

TEST(read_nil)
{
    CL_Obj obj = reads("nil");
    ASSERT(CL_NULL_P(obj));
}

TEST(read_t)
{
    CL_Obj obj = reads("t");
    ASSERT(CL_SYMBOL_P(obj));
    ASSERT_EQ(obj, SYM_T);
}

/* --- String reading --- */

TEST(read_string)
{
    CL_Obj obj = reads("\"hello\"");
    CL_String *s;
    ASSERT(CL_STRING_P(obj));
    s = (CL_String *)CL_OBJ_TO_PTR(obj);
    ASSERT_STR_EQ(s->data, "hello");
}

TEST(read_string_escape)
{
    CL_Obj obj = reads("\"a\\nb\"");
    CL_String *s;
    ASSERT(CL_STRING_P(obj));
    s = (CL_String *)CL_OBJ_TO_PTR(obj);
    ASSERT_EQ_INT((int)s->length, 3);
    ASSERT_EQ_INT(s->data[1], '\n');
}

/* --- List reading --- */

TEST(read_empty_list)
{
    CL_Obj obj = reads("()");
    ASSERT(CL_NULL_P(obj));
}

TEST(read_list)
{
    CL_Obj obj = reads("(1 2 3)");
    ASSERT(CL_CONS_P(obj));
    ASSERT_EQ_INT(CL_FIXNUM_VAL(cl_car(obj)), 1);
    ASSERT_EQ_INT(CL_FIXNUM_VAL(cl_car(cl_cdr(obj))), 2);
    ASSERT_EQ_INT(CL_FIXNUM_VAL(cl_car(cl_cdr(cl_cdr(obj)))), 3);
    ASSERT(CL_NULL_P(cl_cdr(cl_cdr(cl_cdr(obj)))));
}

TEST(read_nested_list)
{
    CL_Obj obj = reads("(1 (2 3) 4)");
    CL_Obj inner;
    ASSERT(CL_CONS_P(obj));
    ASSERT_EQ_INT(CL_FIXNUM_VAL(cl_car(obj)), 1);
    inner = cl_car(cl_cdr(obj));
    ASSERT(CL_CONS_P(inner));
    ASSERT_EQ_INT(CL_FIXNUM_VAL(cl_car(inner)), 2);
    ASSERT_EQ_INT(CL_FIXNUM_VAL(cl_car(cl_cdr(inner))), 3);
    ASSERT_EQ_INT(CL_FIXNUM_VAL(cl_car(cl_cdr(cl_cdr(obj)))), 4);
}

TEST(read_dotted_pair)
{
    CL_Obj obj = reads("(1 . 2)");
    ASSERT(CL_CONS_P(obj));
    ASSERT_EQ_INT(CL_FIXNUM_VAL(cl_car(obj)), 1);
    ASSERT_EQ_INT(CL_FIXNUM_VAL(cl_cdr(obj)), 2);
}

/* --- Quote --- */

TEST(read_quote)
{
    ASSERT_STR_EQ(read_print("'foo"), "(QUOTE FOO)");
}

TEST(read_function_shorthand)
{
    ASSERT_STR_EQ(read_print("#'foo"), "(FUNCTION FOO)");
}

/* --- Character literal --- */

TEST(read_char_literal)
{
    CL_Obj obj = reads("#\\A");
    ASSERT(CL_CHAR_P(obj));
    ASSERT_EQ_INT(CL_CHAR_VAL(obj), 'A');
}

TEST(read_char_space)
{
    CL_Obj obj = reads("#\\Space");
    ASSERT(CL_CHAR_P(obj));
    ASSERT_EQ_INT(CL_CHAR_VAL(obj), ' ');
}

TEST(read_char_newline)
{
    CL_Obj obj = reads("#\\Newline");
    ASSERT(CL_CHAR_P(obj));
    ASSERT_EQ_INT(CL_CHAR_VAL(obj), '\n');
}

/* --- Keyword --- */

TEST(read_keyword)
{
    CL_Obj obj = reads(":test");
    CL_Symbol *sym;
    ASSERT(CL_SYMBOL_P(obj));
    sym = (CL_Symbol *)CL_OBJ_TO_PTR(obj);
    ASSERT_STR_EQ(cl_symbol_name(obj), "TEST");
    /* Keywords are self-evaluating */
    ASSERT_EQ(sym->value, obj);
}

/* --- Comments --- */

TEST(read_with_comment)
{
    CL_Obj obj = reads("; this is a comment\n42");
    ASSERT(CL_FIXNUM_P(obj));
    ASSERT_EQ_INT(CL_FIXNUM_VAL(obj), 42);
}

/* --- Round-trip (read-print) --- */

TEST(roundtrip_integer)
{
    ASSERT_STR_EQ(read_print("42"), "42");
    ASSERT_STR_EQ(read_print("-7"), "-7");
    ASSERT_STR_EQ(read_print("0"), "0");
}

TEST(roundtrip_list)
{
    ASSERT_STR_EQ(read_print("(1 2 3)"), "(1 2 3)");
    ASSERT_STR_EQ(read_print("(a b c)"), "(A B C)");
}

TEST(roundtrip_string)
{
    ASSERT_STR_EQ(read_print("\"hello\""), "\"hello\"");
}

TEST(roundtrip_nil)
{
    ASSERT_STR_EQ(read_print("nil"), "NIL");
    ASSERT_STR_EQ(read_print("()"), "NIL");
}

TEST(roundtrip_dotted)
{
    ASSERT_STR_EQ(read_print("(a . b)"), "(A . B)");
}

/* --- Feature conditionals (#+ / #-) --- */

TEST(feature_plus_present)
{
    /* :CL-AMIGA is in *features*, so #+cl-amiga should include the form */
    CL_Obj obj = reads("#+cl-amiga 42");
    ASSERT(CL_FIXNUM_P(obj));
    ASSERT_EQ_INT(CL_FIXNUM_VAL(obj), 42);
}

TEST(feature_plus_absent)
{
    /* :NONEXISTENT is not in *features*, so form is skipped, next is read */
    CL_Obj obj = reads("#+nonexistent 42 99");
    ASSERT(CL_FIXNUM_P(obj));
    ASSERT_EQ_INT(CL_FIXNUM_VAL(obj), 99);
}

TEST(feature_minus_present)
{
    /* #-cl-amiga: feature IS present, so form is SKIPPED */
    CL_Obj obj = reads("#-cl-amiga 42 99");
    ASSERT(CL_FIXNUM_P(obj));
    ASSERT_EQ_INT(CL_FIXNUM_VAL(obj), 99);
}

TEST(feature_minus_absent)
{
    /* #-nonexistent: feature is NOT present, so form is INCLUDED */
    CL_Obj obj = reads("#-nonexistent 42");
    ASSERT(CL_FIXNUM_P(obj));
    ASSERT_EQ_INT(CL_FIXNUM_VAL(obj), 42);
}

TEST(feature_plus_posix)
{
    /* On host, :POSIX should be in *features* */
    CL_Obj obj = reads("#+posix :yes");
    ASSERT(CL_SYMBOL_P(obj));
    ASSERT_STR_EQ(cl_symbol_name(obj), "YES");
}

TEST(feature_plus_common_lisp)
{
    CL_Obj obj = reads("#+common-lisp :yes");
    ASSERT(CL_SYMBOL_P(obj));
    ASSERT_STR_EQ(cl_symbol_name(obj), "YES");
}

TEST(feature_in_list)
{
    /* Feature conditionals inside a list */
    ASSERT_STR_EQ(read_print("(1 #+cl-amiga 2 3)"), "(1 2 3)");
}

TEST(feature_skip_in_list)
{
    /* Skipped feature conditional inside a list */
    ASSERT_STR_EQ(read_print("(1 #+nonexistent 2 3)"), "(1 3)");
}

TEST(feature_skip_compound_form)
{
    /* Skipping a compound form (list), not just atom */
    ASSERT_STR_EQ(read_print("(1 #+nonexistent (a b c) 3)"), "(1 3)");
}

TEST(feature_and_expr)
{
    /* (:and :cl-amiga :posix) — both present on host */
    CL_Obj obj = reads("#+(and cl-amiga posix) 42 99");
    ASSERT(CL_FIXNUM_P(obj));
    ASSERT_EQ_INT(CL_FIXNUM_VAL(obj), 42);
}

TEST(feature_and_expr_fail)
{
    /* (:and :cl-amiga :nonexistent) — one missing */
    CL_Obj obj = reads("#+(and cl-amiga nonexistent) 42 99");
    ASSERT(CL_FIXNUM_P(obj));
    ASSERT_EQ_INT(CL_FIXNUM_VAL(obj), 99);
}

TEST(feature_or_expr)
{
    /* (:or :nonexistent :cl-amiga) — one present */
    CL_Obj obj = reads("#+(or nonexistent cl-amiga) 42 99");
    ASSERT(CL_FIXNUM_P(obj));
    ASSERT_EQ_INT(CL_FIXNUM_VAL(obj), 42);
}

TEST(feature_or_expr_fail)
{
    /* (:or :nonexistent :also-nonexistent) — none present */
    CL_Obj obj = reads("#+(or nonexistent also-nonexistent) 42 99");
    ASSERT(CL_FIXNUM_P(obj));
    ASSERT_EQ_INT(CL_FIXNUM_VAL(obj), 99);
}

TEST(feature_not_expr)
{
    /* (:not :nonexistent) — not present, so true */
    CL_Obj obj = reads("#+(not nonexistent) 42 99");
    ASSERT(CL_FIXNUM_P(obj));
    ASSERT_EQ_INT(CL_FIXNUM_VAL(obj), 42);
}

TEST(feature_not_expr_fail)
{
    /* (:not :cl-amiga) — present, so false */
    CL_Obj obj = reads("#+(not cl-amiga) 42 99");
    ASSERT(CL_FIXNUM_P(obj));
    ASSERT_EQ_INT(CL_FIXNUM_VAL(obj), 99);
}

TEST(features_is_list)
{
    /* *FEATURES* should be a non-nil list */
    CL_Symbol *s = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_STAR_FEATURES);
    ASSERT(!CL_NULL_P(s->value));
    ASSERT(CL_CONS_P(s->value));
}

/* --- Read-suppress: skipped feature conditionals suppress errors --- */

TEST(feature_suppress_unknown_package)
{
    /* #+nonexistent should suppress "Package not found" error */
    CL_Obj obj = reads("#+nonexistent (unknown-pkg:symbol) 42");
    ASSERT(CL_FIXNUM_P(obj));
    ASSERT_EQ_INT(CL_FIXNUM_VAL(obj), 42);
}

TEST(feature_suppress_internal_symbol)
{
    /* #+nonexistent should suppress pkg::internal access errors */
    CL_Obj obj = reads("#+nonexistent badpkg::internal 42");
    ASSERT(CL_FIXNUM_P(obj));
    ASSERT_EQ_INT(CL_FIXNUM_VAL(obj), 42);
}

TEST(feature_suppress_unknown_char_name)
{
    /* #+nonexistent should suppress unknown character name errors */
    CL_Obj obj = reads("#+nonexistent #\\UnknownCharName 42");
    ASSERT(CL_FIXNUM_P(obj));
    ASSERT_EQ_INT(CL_FIXNUM_VAL(obj), 42);
}

TEST(feature_suppress_nested)
{
    /* Nested feature conditionals: both skipped, read final form */
    CL_Obj obj = reads("#+nonexistent #+also-nonexistent foo 42");
    ASSERT(CL_FIXNUM_P(obj));
    ASSERT_EQ_INT(CL_FIXNUM_VAL(obj), 42);
}

TEST(feature_suppress_unknown_dispatch)
{
    /* #+nonexistent should suppress unknown dispatch macro */
    CL_Obj obj = reads("#+nonexistent #! 42");
    ASSERT(CL_FIXNUM_P(obj));
    ASSERT_EQ_INT(CL_FIXNUM_VAL(obj), 42);
}

TEST(feature_suppress_in_list)
{
    /* read-suppress inside a list with unknown package */
    ASSERT_STR_EQ(read_print("(1 #+nonexistent unknown-pkg:sym 3)"), "(1 3)");
}

/* --- Read-time eval (#.) --- */

TEST(read_time_eval_arithmetic)
{
    /* #.(+ 1 2) should read as 3 */
    CL_Obj obj = reads("#.(+ 1 2)");
    ASSERT(CL_FIXNUM_P(obj));
    ASSERT_EQ_INT(CL_FIXNUM_VAL(obj), 3);
}

TEST(read_time_eval_list)
{
    /* #.(list 1 2) should read as (1 2) */
    ASSERT_STR_EQ(read_print("#.(list 1 2)"), "(1 2)");
}

TEST(read_time_eval_in_list)
{
    /* #. inside a list */
    ASSERT_STR_EQ(read_print("(a #.(+ 10 20) b)"), "(A 30 B)");
}

/* --- #nA multi-dimensional array reader --- */

TEST(read_2d_array)
{
    /* #2A((1 2) (3 4)) => 2x2 array */
    CL_Obj obj = reads("#2A((1 2) (3 4))");
    ASSERT(CL_VECTOR_P(obj));
    {
        CL_Vector *v = (CL_Vector *)CL_OBJ_TO_PTR(obj);
        ASSERT_EQ_INT(v->rank, 2);
        ASSERT_EQ_INT(v->length, 4);
        /* dims stored in data[0..rank-1] */
        ASSERT_EQ_INT(CL_FIXNUM_VAL(v->data[0]), 2);
        ASSERT_EQ_INT(CL_FIXNUM_VAL(v->data[1]), 2);
        /* elements at data[rank..] */
        ASSERT_EQ_INT(CL_FIXNUM_VAL(v->data[2]), 1);
        ASSERT_EQ_INT(CL_FIXNUM_VAL(v->data[3]), 2);
        ASSERT_EQ_INT(CL_FIXNUM_VAL(v->data[4]), 3);
        ASSERT_EQ_INT(CL_FIXNUM_VAL(v->data[5]), 4);
    }
}

TEST(read_2d_array_print)
{
    /* Verify printed form round-trips */
    ASSERT_STR_EQ(read_print("#2A((1 2) (3 4))"), "#2A((1 2) (3 4))");
}

TEST(read_2d_array_3x2)
{
    /* #2A((1 2) (3 4) (5 6)) => 3x2 array */
    CL_Obj obj = reads("#2A((1 2) (3 4) (5 6))");
    CL_Vector *v = (CL_Vector *)CL_OBJ_TO_PTR(obj);
    ASSERT_EQ_INT(v->rank, 2);
    ASSERT_EQ_INT(CL_FIXNUM_VAL(v->data[0]), 3);  /* dim 0 */
    ASSERT_EQ_INT(CL_FIXNUM_VAL(v->data[1]), 2);  /* dim 1 */
    ASSERT_EQ_INT(CL_FIXNUM_VAL(v->data[2]), 1);
    ASSERT_EQ_INT(CL_FIXNUM_VAL(v->data[7]), 6);
}

TEST(read_1d_array_reader)
{
    /* #1A(1 2 3) => same as #(1 2 3) */
    CL_Obj obj = reads("#1A(1 2 3)");
    ASSERT(CL_VECTOR_P(obj));
    {
        CL_Vector *v = (CL_Vector *)CL_OBJ_TO_PTR(obj);
        ASSERT_EQ_INT(v->length, 3);
    }
}

TEST(read_0d_array)
{
    /* #0A 42 => 0-dimensional array containing 42 */
    CL_Obj obj = reads("#0A 42");
    ASSERT(CL_VECTOR_P(obj));
    {
        CL_Vector *v = (CL_Vector *)CL_OBJ_TO_PTR(obj);
        ASSERT_EQ_INT(v->length, 1);
        ASSERT_EQ_INT(CL_FIXNUM_VAL(cl_vector_data(v)[0]), 42);
    }
}

TEST(read_2d_array_lowercase)
{
    /* #2a also works (lowercase) */
    CL_Obj obj = reads("#2a((10 20) (30 40))");
    ASSERT(CL_VECTOR_P(obj));
    {
        CL_Vector *v = (CL_Vector *)CL_OBJ_TO_PTR(obj);
        ASSERT_EQ_INT(v->rank, 2);
        ASSERT_EQ_INT(CL_FIXNUM_VAL(v->data[2]), 10);
        ASSERT_EQ_INT(CL_FIXNUM_VAL(v->data[5]), 40);
    }
}

int main(void)
{
    test_init();
    setup();

    RUN(read_integer);
    RUN(read_negative_integer);
    RUN(read_zero);
    RUN(read_symbol);
    RUN(read_nil);
    RUN(read_t);
    RUN(read_string);
    RUN(read_string_escape);
    RUN(read_empty_list);
    RUN(read_list);
    RUN(read_nested_list);
    RUN(read_dotted_pair);
    RUN(read_quote);
    RUN(read_function_shorthand);
    RUN(read_char_literal);
    RUN(read_char_space);
    RUN(read_char_newline);
    RUN(read_keyword);
    RUN(read_with_comment);
    RUN(roundtrip_integer);
    RUN(roundtrip_list);
    RUN(roundtrip_string);
    RUN(roundtrip_nil);
    RUN(roundtrip_dotted);

    /* Feature conditionals */
    RUN(feature_plus_present);
    RUN(feature_plus_absent);
    RUN(feature_minus_present);
    RUN(feature_minus_absent);
    RUN(feature_plus_posix);
    RUN(feature_plus_common_lisp);
    RUN(feature_in_list);
    RUN(feature_skip_in_list);
    RUN(feature_skip_compound_form);
    RUN(feature_and_expr);
    RUN(feature_and_expr_fail);
    RUN(feature_or_expr);
    RUN(feature_or_expr_fail);
    RUN(feature_not_expr);
    RUN(feature_not_expr_fail);
    RUN(features_is_list);

    /* Read-suppress tests */
    RUN(feature_suppress_unknown_package);
    RUN(feature_suppress_internal_symbol);
    RUN(feature_suppress_unknown_char_name);
    RUN(feature_suppress_nested);
    RUN(feature_suppress_unknown_dispatch);
    RUN(feature_suppress_in_list);

    /* Read-time eval */
    RUN(read_time_eval_arithmetic);
    RUN(read_time_eval_list);
    RUN(read_time_eval_in_list);

    /* #nA multi-dimensional array reader */
    RUN(read_2d_array);
    RUN(read_2d_array_print);
    RUN(read_2d_array_3x2);
    RUN(read_1d_array_reader);
    RUN(read_0d_array);
    RUN(read_2d_array_lowercase);

    teardown();
    REPORT();
}
