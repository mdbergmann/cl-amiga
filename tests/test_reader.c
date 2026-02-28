#include "test.h"
#include "core/types.h"
#include "core/mem.h"
#include "core/error.h"
#include "core/package.h"
#include "core/symbol.h"
#include "core/reader.h"
#include "core/printer.h"
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

    teardown();
    REPORT();
}
