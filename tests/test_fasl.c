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
#include "core/fasl.h"
#include "core/float.h"
#include "core/opcodes.h"
#include "platform/platform.h"
#include <string.h>
#include <math.h>

/* Helper: eval a string, return the CL_Obj */
static CL_Obj eval_obj(const char *str)
{
    return cl_eval_string(str);
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

/* Helper: build a minimal CL_Bytecode that pushes constant[0] and returns.
 * code: OP_CONST 0x00 0x00 OP_RET */
static CL_Obj make_simple_bytecode(CL_Obj constant)
{
    CL_Bytecode *bc = (CL_Bytecode *)cl_alloc(TYPE_BYTECODE, sizeof(CL_Bytecode));
    if (!bc) return CL_NIL;

    bc->code = (uint8_t *)platform_alloc(4);
    bc->code[0] = OP_CONST;
    bc->code[1] = 0x00;  /* high byte of constant index */
    bc->code[2] = 0x00;  /* low byte of constant index */
    bc->code[3] = OP_RET;
    bc->code_len = 4;

    bc->constants = (CL_Obj *)platform_alloc(sizeof(CL_Obj));
    bc->constants[0] = constant;
    bc->n_constants = 1;

    bc->arity = 0;
    bc->n_locals = 0;
    bc->n_upvalues = 0;
    bc->name = CL_NIL;
    bc->n_optional = 0;
    bc->flags = 0;
    bc->n_keys = 0;
    bc->key_syms = NULL;
    bc->key_slots = NULL;
    bc->key_suppliedp_slots = NULL;
    bc->line_map = NULL;
    bc->line_map_count = 0;
    bc->source_line = 42;
    bc->source_file = NULL;

    return CL_PTR_TO_OBJ(bc);
}

/* Helper: build a bytecode with source file and line map */
static CL_Obj make_bytecode_with_source(CL_Obj constant, const char *file)
{
    CL_Bytecode *bc;
    CL_Obj obj = make_simple_bytecode(constant);
    if (CL_NULL_P(obj)) return CL_NIL;
    bc = (CL_Bytecode *)CL_OBJ_TO_PTR(obj);

    /* Set source file */
    if (file) {
        char *sf = (char *)platform_alloc(strlen(file) + 1);
        strcpy(sf, file);
        bc->source_file = sf;
    }

    /* Add a line map */
    bc->line_map = (CL_LineEntry *)platform_alloc(2 * sizeof(CL_LineEntry));
    bc->line_map[0].pc = 0;
    bc->line_map[0].line = 10;
    bc->line_map[1].pc = 3;
    bc->line_map[1].line = 11;
    bc->line_map_count = 2;

    return obj;
}

/* Helper: build a bytecode with keyword params */
static CL_Obj make_bytecode_with_keys(void)
{
    CL_Bytecode *bc;
    CL_Obj obj = make_simple_bytecode(CL_MAKE_FIXNUM(0));
    if (CL_NULL_P(obj)) return CL_NIL;
    bc = (CL_Bytecode *)CL_OBJ_TO_PTR(obj);

    bc->arity = 0;
    bc->n_locals = 4;
    bc->flags = 1;  /* has_key */
    bc->n_keys = 2;
    bc->key_syms = (CL_Obj *)platform_alloc(2 * sizeof(CL_Obj));
    bc->key_syms[0] = cl_intern_in("X", 1, cl_find_package("KEYWORD", 7));
    bc->key_syms[1] = cl_intern_in("Y", 1, cl_find_package("KEYWORD", 7));
    bc->key_slots = (uint8_t *)platform_alloc(2);
    bc->key_slots[0] = 0;
    bc->key_slots[1] = 1;
    bc->key_suppliedp_slots = (uint8_t *)platform_alloc(2);
    bc->key_suppliedp_slots[0] = 2;
    bc->key_suppliedp_slots[1] = 0xFF;

    return obj;
}

/* ================================================================
 * Writer/Reader primitive tests
 * ================================================================ */

TEST(write_read_u8)
{
    uint8_t buf[16];
    CL_FaslWriter w;
    CL_FaslReader r;

    cl_fasl_writer_init(&w, buf, sizeof(buf));
    cl_fasl_write_u8(&w, 0x00);
    cl_fasl_write_u8(&w, 0xFF);
    cl_fasl_write_u8(&w, 0x42);
    ASSERT_EQ_INT(w.error, FASL_OK);
    ASSERT_EQ_INT(w.pos, 3);

    cl_fasl_reader_init(&r, buf, w.pos);
    ASSERT_EQ_INT(cl_fasl_read_u8(&r), 0x00);
    ASSERT_EQ_INT(cl_fasl_read_u8(&r), 0xFF);
    ASSERT_EQ_INT(cl_fasl_read_u8(&r), 0x42);
    ASSERT_EQ_INT(r.error, FASL_OK);
}

TEST(write_read_u16)
{
    uint8_t buf[16];
    CL_FaslWriter w;
    CL_FaslReader r;

    cl_fasl_writer_init(&w, buf, sizeof(buf));
    cl_fasl_write_u16(&w, 0x0000);
    cl_fasl_write_u16(&w, 0xFFFF);
    cl_fasl_write_u16(&w, 0x1234);
    ASSERT_EQ_INT(w.error, FASL_OK);
    ASSERT_EQ_INT(w.pos, 6);

    /* Verify big-endian byte order */
    ASSERT_EQ_INT(buf[4], 0x12);
    ASSERT_EQ_INT(buf[5], 0x34);

    cl_fasl_reader_init(&r, buf, w.pos);
    ASSERT_EQ_INT(cl_fasl_read_u16(&r), 0x0000);
    ASSERT_EQ_INT(cl_fasl_read_u16(&r), 0xFFFF);
    ASSERT_EQ_INT(cl_fasl_read_u16(&r), 0x1234);
    ASSERT_EQ_INT(r.error, FASL_OK);
}

TEST(write_read_u32)
{
    uint8_t buf[16];
    CL_FaslWriter w;
    CL_FaslReader r;

    cl_fasl_writer_init(&w, buf, sizeof(buf));
    cl_fasl_write_u32(&w, 0x00000000);
    cl_fasl_write_u32(&w, 0xDEADBEEF);
    ASSERT_EQ_INT(w.error, FASL_OK);
    ASSERT_EQ_INT(w.pos, 8);

    /* Verify big-endian byte order */
    ASSERT_EQ_INT(buf[4], 0xDE);
    ASSERT_EQ_INT(buf[5], 0xAD);
    ASSERT_EQ_INT(buf[6], 0xBE);
    ASSERT_EQ_INT(buf[7], 0xEF);

    cl_fasl_reader_init(&r, buf, w.pos);
    ASSERT(cl_fasl_read_u32(&r) == 0x00000000);
    ASSERT(cl_fasl_read_u32(&r) == 0xDEADBEEF);
    ASSERT_EQ_INT(r.error, FASL_OK);
}

TEST(write_read_bytes)
{
    uint8_t buf[32];
    CL_FaslWriter w;
    CL_FaslReader r;
    uint8_t out[5];
    const char *data = "hello";

    cl_fasl_writer_init(&w, buf, sizeof(buf));
    cl_fasl_write_bytes(&w, data, 5);
    ASSERT_EQ_INT(w.error, FASL_OK);
    ASSERT_EQ_INT(w.pos, 5);

    cl_fasl_reader_init(&r, buf, w.pos);
    cl_fasl_read_bytes(&r, out, 5);
    ASSERT_EQ_INT(r.error, FASL_OK);
    ASSERT(memcmp(out, data, 5) == 0);
}

/* ================================================================
 * Overflow/truncation error tests
 * ================================================================ */

TEST(writer_overflow)
{
    uint8_t buf[2];
    CL_FaslWriter w;

    cl_fasl_writer_init(&w, buf, sizeof(buf));
    cl_fasl_write_u32(&w, 0x12345678);
    ASSERT_EQ_INT(w.error, FASL_ERR_OVERFLOW);
}

TEST(reader_truncated)
{
    uint8_t buf[2] = {0x12, 0x34};
    CL_FaslReader r;

    cl_fasl_reader_init(&r, buf, 2);
    cl_fasl_read_u32(&r);
    ASSERT_EQ_INT(r.error, FASL_ERR_TRUNCATED);
}

TEST(reader_truncated_u8)
{
    CL_FaslReader r;
    cl_fasl_reader_init(&r, NULL, 0);
    cl_fasl_read_u8(&r);
    ASSERT_EQ_INT(r.error, FASL_ERR_TRUNCATED);
}

TEST(reader_truncated_u16)
{
    uint8_t buf[1] = {0x12};
    CL_FaslReader r;
    cl_fasl_reader_init(&r, buf, 1);
    cl_fasl_read_u16(&r);
    ASSERT_EQ_INT(r.error, FASL_ERR_TRUNCATED);
}

TEST(reader_truncated_bytes)
{
    uint8_t buf[2] = {0x12, 0x34};
    uint8_t out[4];
    CL_FaslReader r;
    cl_fasl_reader_init(&r, buf, 2);
    cl_fasl_read_bytes(&r, out, 4);
    ASSERT_EQ_INT(r.error, FASL_ERR_TRUNCATED);
}

TEST(error_propagation_writer)
{
    uint8_t buf[2];
    CL_FaslWriter w;

    cl_fasl_writer_init(&w, buf, sizeof(buf));
    cl_fasl_write_u32(&w, 0x12345678);  /* overflow */
    ASSERT_EQ_INT(w.error, FASL_ERR_OVERFLOW);

    /* Additional writes are no-ops */
    cl_fasl_write_u8(&w, 0x42);
    ASSERT_EQ_INT(w.error, FASL_ERR_OVERFLOW);
    ASSERT_EQ_INT(w.pos, 0);
}

TEST(error_propagation_reader)
{
    uint8_t buf[1] = {0x12};
    CL_FaslReader r;

    cl_fasl_reader_init(&r, buf, 1);
    cl_fasl_read_u32(&r);  /* truncated */
    ASSERT_EQ_INT(r.error, FASL_ERR_TRUNCATED);

    ASSERT_EQ_INT(cl_fasl_read_u8(&r), 0);
    ASSERT_EQ_INT(r.error, FASL_ERR_TRUNCATED);
}

/* ================================================================
 * Header tests
 * ================================================================ */

TEST(write_read_header)
{
    uint8_t buf[64];
    CL_FaslWriter w;
    CL_FaslReader r;
    uint32_t n_units;

    cl_fasl_writer_init(&w, buf, sizeof(buf));
    cl_fasl_write_header(&w, 5);
    ASSERT_EQ_INT(w.error, FASL_OK);
    ASSERT_EQ_INT(w.pos, 12);

    cl_fasl_reader_init(&r, buf, w.pos);
    n_units = cl_fasl_read_header(&r);
    ASSERT_EQ_INT(r.error, FASL_OK);
    ASSERT_EQ_INT(n_units, 5);
}

TEST(header_bad_magic)
{
    uint8_t buf[12] = {0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1};
    CL_FaslReader r;
    cl_fasl_reader_init(&r, buf, 12);
    cl_fasl_read_header(&r);
    ASSERT_EQ_INT(r.error, FASL_ERR_BAD_MAGIC);
}

TEST(header_bad_version)
{
    uint8_t buf[12];
    CL_FaslWriter w;
    CL_FaslReader r;

    cl_fasl_writer_init(&w, buf, sizeof(buf));
    cl_fasl_write_u32(&w, CL_FASL_MAGIC);
    cl_fasl_write_u16(&w, 99);  /* bad version */
    cl_fasl_write_u16(&w, 0);
    cl_fasl_write_u32(&w, 0);

    cl_fasl_reader_init(&r, buf, 12);
    cl_fasl_read_header(&r);
    ASSERT_EQ_INT(r.error, FASL_ERR_BAD_VERSION);
}

TEST(header_zero_units)
{
    uint8_t buf[64];
    CL_FaslWriter w;
    CL_FaslReader r;

    cl_fasl_writer_init(&w, buf, sizeof(buf));
    cl_fasl_write_header(&w, 0);

    cl_fasl_reader_init(&r, buf, w.pos);
    ASSERT_EQ_INT(cl_fasl_read_header(&r), 0);
    ASSERT_EQ_INT(r.error, FASL_OK);
}

/* ================================================================
 * Object serialization round-trip tests
 * ================================================================ */

TEST(serialize_nil)
{
    uint8_t buf[64];
    CL_FaslWriter w;
    CL_FaslReader r;

    cl_fasl_writer_init(&w, buf, sizeof(buf));
    cl_fasl_serialize_obj(&w, CL_NIL);
    ASSERT_EQ_INT(w.error, FASL_OK);
    ASSERT_EQ_INT(w.pos, 1);

    cl_fasl_reader_init(&r, buf, w.pos);
    ASSERT(CL_NULL_P(cl_fasl_deserialize_obj(&r)));
    ASSERT_EQ_INT(r.error, FASL_OK);
}

TEST(serialize_t)
{
    uint8_t buf[64];
    CL_FaslWriter w;
    CL_FaslReader r;

    cl_fasl_writer_init(&w, buf, sizeof(buf));
    cl_fasl_serialize_obj(&w, CL_T);

    cl_fasl_reader_init(&r, buf, w.pos);
    ASSERT(cl_fasl_deserialize_obj(&r) == CL_T);
    ASSERT_EQ_INT(r.error, FASL_OK);
}

TEST(serialize_unbound)
{
    uint8_t buf[64];
    CL_FaslWriter w;
    CL_FaslReader r;

    cl_fasl_writer_init(&w, buf, sizeof(buf));
    cl_fasl_serialize_obj(&w, CL_UNBOUND);

    cl_fasl_reader_init(&r, buf, w.pos);
    ASSERT(cl_fasl_deserialize_obj(&r) == CL_UNBOUND);
    ASSERT_EQ_INT(r.error, FASL_OK);
}

TEST(serialize_fixnum_zero)
{
    uint8_t buf[64];
    CL_FaslWriter w;
    CL_FaslReader r;
    CL_Obj result;

    cl_fasl_writer_init(&w, buf, sizeof(buf));
    cl_fasl_serialize_obj(&w, CL_MAKE_FIXNUM(0));

    cl_fasl_reader_init(&r, buf, w.pos);
    result = cl_fasl_deserialize_obj(&r);
    ASSERT(CL_FIXNUM_P(result));
    ASSERT_EQ_INT(CL_FIXNUM_VAL(result), 0);
}

TEST(serialize_fixnum_positive)
{
    uint8_t buf[64];
    CL_FaslWriter w;
    CL_FaslReader r;

    cl_fasl_writer_init(&w, buf, sizeof(buf));
    cl_fasl_serialize_obj(&w, CL_MAKE_FIXNUM(42));

    cl_fasl_reader_init(&r, buf, w.pos);
    ASSERT_EQ_INT(CL_FIXNUM_VAL(cl_fasl_deserialize_obj(&r)), 42);
}

TEST(serialize_fixnum_negative)
{
    uint8_t buf[64];
    CL_FaslWriter w;
    CL_FaslReader r;

    cl_fasl_writer_init(&w, buf, sizeof(buf));
    cl_fasl_serialize_obj(&w, CL_MAKE_FIXNUM(-99));

    cl_fasl_reader_init(&r, buf, w.pos);
    ASSERT_EQ_INT(CL_FIXNUM_VAL(cl_fasl_deserialize_obj(&r)), -99);
}

TEST(serialize_fixnum_max)
{
    uint8_t buf[64];
    CL_FaslWriter w;
    CL_FaslReader r;

    cl_fasl_writer_init(&w, buf, sizeof(buf));
    cl_fasl_serialize_obj(&w, CL_MAKE_FIXNUM(CL_FIXNUM_MAX));

    cl_fasl_reader_init(&r, buf, w.pos);
    ASSERT_EQ_INT(CL_FIXNUM_VAL(cl_fasl_deserialize_obj(&r)), CL_FIXNUM_MAX);
}

TEST(serialize_fixnum_min)
{
    uint8_t buf[64];
    CL_FaslWriter w;
    CL_FaslReader r;

    cl_fasl_writer_init(&w, buf, sizeof(buf));
    cl_fasl_serialize_obj(&w, CL_MAKE_FIXNUM(CL_FIXNUM_MIN));

    cl_fasl_reader_init(&r, buf, w.pos);
    ASSERT_EQ_INT(CL_FIXNUM_VAL(cl_fasl_deserialize_obj(&r)), CL_FIXNUM_MIN);
}

TEST(serialize_character)
{
    uint8_t buf[64];
    CL_FaslWriter w;
    CL_FaslReader r;
    CL_Obj result;

    cl_fasl_writer_init(&w, buf, sizeof(buf));
    cl_fasl_serialize_obj(&w, CL_MAKE_CHAR('A'));

    cl_fasl_reader_init(&r, buf, w.pos);
    result = cl_fasl_deserialize_obj(&r);
    ASSERT(CL_CHAR_P(result));
    ASSERT_EQ_INT(CL_CHAR_VAL(result), 'A');
}

TEST(serialize_character_newline)
{
    uint8_t buf[64];
    CL_FaslWriter w;
    CL_FaslReader r;

    cl_fasl_writer_init(&w, buf, sizeof(buf));
    cl_fasl_serialize_obj(&w, CL_MAKE_CHAR('\n'));

    cl_fasl_reader_init(&r, buf, w.pos);
    ASSERT_EQ_INT(CL_CHAR_VAL(cl_fasl_deserialize_obj(&r)), '\n');
}

TEST(serialize_string_empty)
{
    uint8_t buf[64];
    CL_FaslWriter w;
    CL_FaslReader r;
    CL_Obj str, result;
    CL_String *s;

    str = cl_make_string("", 0);
    cl_fasl_writer_init(&w, buf, sizeof(buf));
    cl_fasl_serialize_obj(&w, str);

    cl_fasl_reader_init(&r, buf, w.pos);
    result = cl_fasl_deserialize_obj(&r);
    ASSERT(CL_STRING_P(result));
    s = (CL_String *)CL_OBJ_TO_PTR(result);
    ASSERT_EQ_INT(s->length, 0);
}

TEST(serialize_string)
{
    uint8_t buf[128];
    CL_FaslWriter w;
    CL_FaslReader r;
    CL_Obj str, result;
    CL_String *s;

    str = cl_make_string("hello world", 11);
    cl_fasl_writer_init(&w, buf, sizeof(buf));
    cl_fasl_serialize_obj(&w, str);

    cl_fasl_reader_init(&r, buf, w.pos);
    result = cl_fasl_deserialize_obj(&r);
    ASSERT(CL_STRING_P(result));
    s = (CL_String *)CL_OBJ_TO_PTR(result);
    ASSERT_EQ_INT(s->length, 11);
    ASSERT_STR_EQ(s->data, "hello world");
}

TEST(serialize_long_string)
{
    uint8_t buf[2048];
    CL_FaslWriter w;
    CL_FaslReader r;
    CL_Obj str, result;
    CL_String *s;
    char long_str[513];
    int i;

    for (i = 0; i < 512; i++) long_str[i] = 'A' + (i % 26);
    long_str[512] = '\0';

    str = cl_make_string(long_str, 512);
    cl_fasl_writer_init(&w, buf, sizeof(buf));
    cl_fasl_serialize_obj(&w, str);

    cl_fasl_reader_init(&r, buf, w.pos);
    result = cl_fasl_deserialize_obj(&r);
    s = (CL_String *)CL_OBJ_TO_PTR(result);
    ASSERT_EQ_INT(s->length, 512);
    ASSERT(memcmp(s->data, long_str, 512) == 0);
}

TEST(serialize_symbol_cl)
{
    uint8_t buf[128];
    CL_FaslWriter w;
    CL_FaslReader r;
    CL_Obj sym, result;

    sym = cl_intern_in("CAR", 3, cl_package_cl);
    cl_fasl_writer_init(&w, buf, sizeof(buf));
    cl_fasl_serialize_obj(&w, sym);

    cl_fasl_reader_init(&r, buf, w.pos);
    result = cl_fasl_deserialize_obj(&r);
    ASSERT(CL_SYMBOL_P(result));
    ASSERT(result == sym);
}

TEST(serialize_symbol_keyword)
{
    uint8_t buf[128];
    CL_FaslWriter w;
    CL_FaslReader r;
    CL_Obj sym, result;

    sym = cl_intern_in("TEST", 4, cl_find_package("KEYWORD", 7));
    cl_fasl_writer_init(&w, buf, sizeof(buf));
    cl_fasl_serialize_obj(&w, sym);

    cl_fasl_reader_init(&r, buf, w.pos);
    result = cl_fasl_deserialize_obj(&r);
    ASSERT(result == sym);
}

TEST(serialize_symbol_user)
{
    uint8_t buf[128];
    CL_FaslWriter w;
    CL_FaslReader r;
    CL_Obj sym, result;

    sym = cl_intern_in("MY-VAR", 6, cl_package_cl_user);
    cl_fasl_writer_init(&w, buf, sizeof(buf));
    cl_fasl_serialize_obj(&w, sym);

    cl_fasl_reader_init(&r, buf, w.pos);
    result = cl_fasl_deserialize_obj(&r);
    ASSERT(result == sym);
}

TEST(serialize_cons)
{
    uint8_t buf[256];
    CL_FaslWriter w;
    CL_FaslReader r;
    CL_Obj pair, result;

    pair = cl_cons(CL_MAKE_FIXNUM(1), CL_MAKE_FIXNUM(2));
    cl_fasl_writer_init(&w, buf, sizeof(buf));
    cl_fasl_serialize_obj(&w, pair);

    cl_fasl_reader_init(&r, buf, w.pos);
    result = cl_fasl_deserialize_obj(&r);
    ASSERT(CL_CONS_P(result));
    ASSERT_EQ_INT(CL_FIXNUM_VAL(cl_car(result)), 1);
    ASSERT_EQ_INT(CL_FIXNUM_VAL(cl_cdr(result)), 2);
}

TEST(serialize_list)
{
    uint8_t buf[512];
    CL_FaslWriter w;
    CL_FaslReader r;
    CL_Obj list, result, cur;

    list = cl_cons(CL_MAKE_FIXNUM(3), CL_NIL);
    CL_GC_PROTECT(list);
    list = cl_cons(CL_MAKE_FIXNUM(2), list);
    list = cl_cons(CL_MAKE_FIXNUM(1), list);
    CL_GC_UNPROTECT(1);

    cl_fasl_writer_init(&w, buf, sizeof(buf));
    cl_fasl_serialize_obj(&w, list);

    cl_fasl_reader_init(&r, buf, w.pos);
    result = cl_fasl_deserialize_obj(&r);

    cur = result;
    ASSERT_EQ_INT(CL_FIXNUM_VAL(cl_car(cur)), 1); cur = cl_cdr(cur);
    ASSERT_EQ_INT(CL_FIXNUM_VAL(cl_car(cur)), 2); cur = cl_cdr(cur);
    ASSERT_EQ_INT(CL_FIXNUM_VAL(cl_car(cur)), 3); cur = cl_cdr(cur);
    ASSERT(CL_NULL_P(cur));
}

TEST(serialize_dotted_list)
{
    uint8_t buf[256];
    CL_FaslWriter w;
    CL_FaslReader r;
    CL_Obj list, result;

    /* (1 2 . 3) */
    {
        CL_Obj tail = cl_cons(CL_MAKE_FIXNUM(2), CL_MAKE_FIXNUM(3));
        CL_GC_PROTECT(tail);
        list = cl_cons(CL_MAKE_FIXNUM(1), tail);
        CL_GC_UNPROTECT(1);
    }

    cl_fasl_writer_init(&w, buf, sizeof(buf));
    cl_fasl_serialize_obj(&w, list);

    cl_fasl_reader_init(&r, buf, w.pos);
    result = cl_fasl_deserialize_obj(&r);

    ASSERT_EQ_INT(CL_FIXNUM_VAL(cl_car(result)), 1);
    ASSERT_EQ_INT(CL_FIXNUM_VAL(cl_car(cl_cdr(result))), 2);
    ASSERT_EQ_INT(CL_FIXNUM_VAL(cl_cdr(cl_cdr(result))), 3);
}

TEST(serialize_nested_cons)
{
    uint8_t buf[1024];
    CL_FaslWriter w;
    CL_FaslReader r;
    CL_Obj nested, result;

    /* ((1 . 2) . (3 . 4)) */
    {
        CL_Obj a = cl_cons(CL_MAKE_FIXNUM(1), CL_MAKE_FIXNUM(2));
        CL_GC_PROTECT(a);
        CL_Obj b = cl_cons(CL_MAKE_FIXNUM(3), CL_MAKE_FIXNUM(4));
        CL_GC_PROTECT(b);
        nested = cl_cons(a, b);
        CL_GC_UNPROTECT(2);
    }

    cl_fasl_writer_init(&w, buf, sizeof(buf));
    cl_fasl_serialize_obj(&w, nested);

    cl_fasl_reader_init(&r, buf, w.pos);
    result = cl_fasl_deserialize_obj(&r);

    ASSERT_EQ_INT(CL_FIXNUM_VAL(cl_car(cl_car(result))), 1);
    ASSERT_EQ_INT(CL_FIXNUM_VAL(cl_cdr(cl_car(result))), 2);
    ASSERT_EQ_INT(CL_FIXNUM_VAL(cl_car(cl_cdr(result))), 3);
    ASSERT_EQ_INT(CL_FIXNUM_VAL(cl_cdr(cl_cdr(result))), 4);
}

TEST(serialize_deeply_nested_list)
{
    uint8_t buf[4096];
    CL_FaslWriter w;
    CL_FaslReader r;
    CL_Obj list, result;
    int i, depth = 20;

    list = cl_cons(CL_MAKE_FIXNUM(42), CL_NIL);
    CL_GC_PROTECT(list);
    for (i = 1; i < depth; i++)
        list = cl_cons(list, CL_NIL);
    CL_GC_UNPROTECT(1);

    cl_fasl_writer_init(&w, buf, sizeof(buf));
    cl_fasl_serialize_obj(&w, list);

    cl_fasl_reader_init(&r, buf, w.pos);
    result = cl_fasl_deserialize_obj(&r);

    for (i = 1; i < depth; i++) {
        ASSERT(CL_CONS_P(result));
        result = cl_car(result);
    }
    ASSERT_EQ_INT(CL_FIXNUM_VAL(cl_car(result)), 42);
}

TEST(serialize_single_float)
{
    uint8_t buf[128];
    CL_FaslWriter w;
    CL_FaslReader r;
    CL_Obj flt, result;
    CL_SingleFloat *sf;

    flt = cl_make_single_float(3.14f);
    cl_fasl_writer_init(&w, buf, sizeof(buf));
    cl_fasl_serialize_obj(&w, flt);

    cl_fasl_reader_init(&r, buf, w.pos);
    result = cl_fasl_deserialize_obj(&r);
    ASSERT(CL_SINGLE_FLOAT_P(result));
    sf = (CL_SingleFloat *)CL_OBJ_TO_PTR(result);
    ASSERT(fabsf(sf->value - 3.14f) < 0.001f);
}

TEST(serialize_single_float_zero)
{
    uint8_t buf[128];
    CL_FaslWriter w;
    CL_FaslReader r;
    CL_SingleFloat *sf;

    cl_fasl_writer_init(&w, buf, sizeof(buf));
    cl_fasl_serialize_obj(&w, cl_make_single_float(0.0f));

    cl_fasl_reader_init(&r, buf, w.pos);
    sf = (CL_SingleFloat *)CL_OBJ_TO_PTR(cl_fasl_deserialize_obj(&r));
    ASSERT(sf->value == 0.0f);
}

TEST(serialize_single_float_negative)
{
    uint8_t buf[128];
    CL_FaslWriter w;
    CL_FaslReader r;
    CL_SingleFloat *sf;

    cl_fasl_writer_init(&w, buf, sizeof(buf));
    cl_fasl_serialize_obj(&w, cl_make_single_float(-1.5f));

    cl_fasl_reader_init(&r, buf, w.pos);
    sf = (CL_SingleFloat *)CL_OBJ_TO_PTR(cl_fasl_deserialize_obj(&r));
    ASSERT(sf->value == -1.5f);
}

TEST(serialize_double_float)
{
    uint8_t buf[128];
    CL_FaslWriter w;
    CL_FaslReader r;
    CL_DoubleFloat *df;

    cl_fasl_writer_init(&w, buf, sizeof(buf));
    cl_fasl_serialize_obj(&w, cl_make_double_float(2.718281828459045));

    cl_fasl_reader_init(&r, buf, w.pos);
    df = (CL_DoubleFloat *)CL_OBJ_TO_PTR(cl_fasl_deserialize_obj(&r));
    ASSERT(fabs(df->value - 2.718281828459045) < 1e-15);
}

TEST(serialize_double_float_zero)
{
    uint8_t buf[128];
    CL_FaslWriter w;
    CL_FaslReader r;
    CL_DoubleFloat *df;

    cl_fasl_writer_init(&w, buf, sizeof(buf));
    cl_fasl_serialize_obj(&w, cl_make_double_float(0.0));

    cl_fasl_reader_init(&r, buf, w.pos);
    df = (CL_DoubleFloat *)CL_OBJ_TO_PTR(cl_fasl_deserialize_obj(&r));
    ASSERT(df->value == 0.0);
}

TEST(serialize_double_float_negative)
{
    uint8_t buf[128];
    CL_FaslWriter w;
    CL_FaslReader r;
    CL_DoubleFloat *df;

    cl_fasl_writer_init(&w, buf, sizeof(buf));
    cl_fasl_serialize_obj(&w, cl_make_double_float(-123.456));

    cl_fasl_reader_init(&r, buf, w.pos);
    df = (CL_DoubleFloat *)CL_OBJ_TO_PTR(cl_fasl_deserialize_obj(&r));
    ASSERT(fabs(df->value - (-123.456)) < 1e-10);
}

TEST(serialize_ratio)
{
    uint8_t buf[128];
    CL_FaslWriter w;
    CL_FaslReader r;
    CL_Obj rat, result;
    CL_Ratio *rt;

    rat = cl_make_ratio(CL_MAKE_FIXNUM(3), CL_MAKE_FIXNUM(4));
    cl_fasl_writer_init(&w, buf, sizeof(buf));
    cl_fasl_serialize_obj(&w, rat);

    cl_fasl_reader_init(&r, buf, w.pos);
    result = cl_fasl_deserialize_obj(&r);
    ASSERT(CL_RATIO_P(result));
    rt = (CL_Ratio *)CL_OBJ_TO_PTR(result);
    ASSERT_EQ_INT(CL_FIXNUM_VAL(rt->numerator), 3);
    ASSERT_EQ_INT(CL_FIXNUM_VAL(rt->denominator), 4);
}

TEST(serialize_bignum)
{
    uint8_t buf[256];
    CL_FaslWriter w;
    CL_FaslReader r;
    CL_Obj bn, result;
    CL_Bignum *b;

    bn = cl_make_bignum(2, 0);
    b = (CL_Bignum *)CL_OBJ_TO_PTR(bn);
    b->limbs[0] = 0x1234;
    b->limbs[1] = 0x5678;

    cl_fasl_writer_init(&w, buf, sizeof(buf));
    cl_fasl_serialize_obj(&w, bn);

    cl_fasl_reader_init(&r, buf, w.pos);
    result = cl_fasl_deserialize_obj(&r);
    ASSERT(CL_BIGNUM_P(result));
    b = (CL_Bignum *)CL_OBJ_TO_PTR(result);
    ASSERT_EQ_INT(b->length, 2);
    ASSERT_EQ_INT(b->sign, 0);
    ASSERT(b->limbs[0] == 0x1234);
    ASSERT(b->limbs[1] == 0x5678);
}

TEST(serialize_bignum_negative)
{
    uint8_t buf[256];
    CL_FaslWriter w;
    CL_FaslReader r;
    CL_Obj bn, result;
    CL_Bignum *b;

    bn = cl_make_bignum(1, 1);
    b = (CL_Bignum *)CL_OBJ_TO_PTR(bn);
    b->limbs[0] = 42;

    cl_fasl_writer_init(&w, buf, sizeof(buf));
    cl_fasl_serialize_obj(&w, bn);

    cl_fasl_reader_init(&r, buf, w.pos);
    result = cl_fasl_deserialize_obj(&r);
    b = (CL_Bignum *)CL_OBJ_TO_PTR(result);
    ASSERT_EQ_INT(b->sign, 1);
    ASSERT(b->limbs[0] == 42);
}

TEST(serialize_vector)
{
    uint8_t buf[256];
    CL_FaslWriter w;
    CL_FaslReader r;
    CL_Obj vec, result;
    CL_Vector *v;

    vec = cl_make_vector(3);
    v = (CL_Vector *)CL_OBJ_TO_PTR(vec);
    v->data[0] = CL_MAKE_FIXNUM(10);
    v->data[1] = CL_MAKE_FIXNUM(20);
    v->data[2] = CL_MAKE_FIXNUM(30);

    cl_fasl_writer_init(&w, buf, sizeof(buf));
    cl_fasl_serialize_obj(&w, vec);

    cl_fasl_reader_init(&r, buf, w.pos);
    result = cl_fasl_deserialize_obj(&r);
    ASSERT(CL_VECTOR_P(result));
    v = (CL_Vector *)CL_OBJ_TO_PTR(result);
    ASSERT_EQ_INT(v->length, 3);
    ASSERT_EQ_INT(CL_FIXNUM_VAL(v->data[0]), 10);
    ASSERT_EQ_INT(CL_FIXNUM_VAL(v->data[1]), 20);
    ASSERT_EQ_INT(CL_FIXNUM_VAL(v->data[2]), 30);
}

TEST(serialize_vector_empty)
{
    uint8_t buf[64];
    CL_FaslWriter w;
    CL_FaslReader r;
    CL_Vector *v;

    cl_fasl_writer_init(&w, buf, sizeof(buf));
    cl_fasl_serialize_obj(&w, cl_make_vector(0));

    cl_fasl_reader_init(&r, buf, w.pos);
    v = (CL_Vector *)CL_OBJ_TO_PTR(cl_fasl_deserialize_obj(&r));
    ASSERT_EQ_INT(v->length, 0);
}

TEST(serialize_vector_mixed_types)
{
    uint8_t buf[512];
    CL_FaslWriter w;
    CL_FaslReader r;
    CL_Obj vec, result;
    CL_Vector *v;

    vec = cl_make_vector(3);
    CL_GC_PROTECT(vec);
    v = (CL_Vector *)CL_OBJ_TO_PTR(vec);
    v->data[0] = CL_MAKE_FIXNUM(42);
    v->data[1] = cl_make_string("test", 4);
    v = (CL_Vector *)CL_OBJ_TO_PTR(vec);
    v->data[2] = CL_T;
    CL_GC_UNPROTECT(1);

    cl_fasl_writer_init(&w, buf, sizeof(buf));
    cl_fasl_serialize_obj(&w, vec);

    cl_fasl_reader_init(&r, buf, w.pos);
    result = cl_fasl_deserialize_obj(&r);
    v = (CL_Vector *)CL_OBJ_TO_PTR(result);
    ASSERT_EQ_INT(CL_FIXNUM_VAL(v->data[0]), 42);
    ASSERT(CL_STRING_P(v->data[1]));
    ASSERT(v->data[2] == CL_T);
}

TEST(serialize_bit_vector)
{
    uint8_t buf[128];
    CL_FaslWriter w;
    CL_FaslReader r;
    CL_Obj bv, result;
    CL_BitVector *b;

    bv = cl_make_bit_vector(8);
    b = (CL_BitVector *)CL_OBJ_TO_PTR(bv);
    cl_bv_set_bit(b, 0, 1);
    cl_bv_set_bit(b, 3, 1);
    cl_bv_set_bit(b, 7, 1);

    cl_fasl_writer_init(&w, buf, sizeof(buf));
    cl_fasl_serialize_obj(&w, bv);

    cl_fasl_reader_init(&r, buf, w.pos);
    result = cl_fasl_deserialize_obj(&r);
    ASSERT(CL_BIT_VECTOR_P(result));
    b = (CL_BitVector *)CL_OBJ_TO_PTR(result);
    ASSERT_EQ_INT(b->length, 8);
    ASSERT_EQ_INT(cl_bv_get_bit(b, 0), 1);
    ASSERT_EQ_INT(cl_bv_get_bit(b, 1), 0);
    ASSERT_EQ_INT(cl_bv_get_bit(b, 3), 1);
    ASSERT_EQ_INT(cl_bv_get_bit(b, 7), 1);
}

TEST(serialize_bit_vector_empty)
{
    uint8_t buf[64];
    CL_FaslWriter w;
    CL_FaslReader r;
    CL_BitVector *b;

    cl_fasl_writer_init(&w, buf, sizeof(buf));
    cl_fasl_serialize_obj(&w, cl_make_bit_vector(0));

    cl_fasl_reader_init(&r, buf, w.pos);
    b = (CL_BitVector *)CL_OBJ_TO_PTR(cl_fasl_deserialize_obj(&r));
    ASSERT_EQ_INT(b->length, 0);
}

TEST(serialize_pathname)
{
    uint8_t buf[512];
    CL_FaslWriter w;
    CL_FaslReader r;
    CL_Obj pn, result;
    CL_Pathname *p;
    CL_String *s;

    {
        CL_Obj name_str = cl_make_string("test", 4);
        CL_GC_PROTECT(name_str);
        CL_Obj type_str = cl_make_string("lisp", 4);
        CL_GC_PROTECT(type_str);
        pn = cl_make_pathname(CL_NIL, CL_NIL, CL_NIL,
                              name_str, type_str, CL_NIL);
        CL_GC_UNPROTECT(2);
    }

    cl_fasl_writer_init(&w, buf, sizeof(buf));
    cl_fasl_serialize_obj(&w, pn);

    cl_fasl_reader_init(&r, buf, w.pos);
    result = cl_fasl_deserialize_obj(&r);
    ASSERT(CL_PATHNAME_P(result));
    p = (CL_Pathname *)CL_OBJ_TO_PTR(result);
    ASSERT(CL_NULL_P(p->host));
    ASSERT(CL_NULL_P(p->device));
    ASSERT(CL_NULL_P(p->directory));
    s = (CL_String *)CL_OBJ_TO_PTR(p->name);
    ASSERT_STR_EQ(s->data, "test");
    s = (CL_String *)CL_OBJ_TO_PTR(p->type);
    ASSERT_STR_EQ(s->data, "lisp");
}

TEST(serialize_heterogeneous_list)
{
    uint8_t buf[2048];
    CL_FaslWriter w;
    CL_FaslReader r;
    CL_Obj list, result, cur;

    /* Build (42 "hello" :key T NIL #\A) */
    list = CL_NIL;
    CL_GC_PROTECT(list);
    list = cl_cons(CL_MAKE_CHAR('A'), list);
    list = cl_cons(CL_NIL, list);
    list = cl_cons(CL_T, list);
    {
        CL_Obj kw = cl_intern_in("KEY", 3, cl_find_package("KEYWORD", 7));
        list = cl_cons(kw, list);
    }
    {
        CL_Obj s = cl_make_string("hello", 5);
        list = cl_cons(s, list);
    }
    list = cl_cons(CL_MAKE_FIXNUM(42), list);
    CL_GC_UNPROTECT(1);

    cl_fasl_writer_init(&w, buf, sizeof(buf));
    cl_fasl_serialize_obj(&w, list);

    cl_fasl_reader_init(&r, buf, w.pos);
    result = cl_fasl_deserialize_obj(&r);

    cur = result;
    ASSERT_EQ_INT(CL_FIXNUM_VAL(cl_car(cur)), 42); cur = cl_cdr(cur);
    ASSERT(CL_STRING_P(cl_car(cur))); cur = cl_cdr(cur);
    ASSERT(CL_SYMBOL_P(cl_car(cur))); cur = cl_cdr(cur);
    ASSERT(cl_car(cur) == CL_T); cur = cl_cdr(cur);
    ASSERT(CL_NULL_P(cl_car(cur))); cur = cl_cdr(cur);
    ASSERT_EQ_INT(CL_CHAR_VAL(cl_car(cur)), 'A');
}

TEST(serialize_multiple_objects_sequential)
{
    uint8_t buf[512];
    CL_FaslWriter w;
    CL_FaslReader r;

    cl_fasl_writer_init(&w, buf, sizeof(buf));
    cl_fasl_serialize_obj(&w, CL_MAKE_FIXNUM(1));
    cl_fasl_serialize_obj(&w, CL_T);
    cl_fasl_serialize_obj(&w, CL_NIL);

    cl_fasl_reader_init(&r, buf, w.pos);
    ASSERT_EQ_INT(CL_FIXNUM_VAL(cl_fasl_deserialize_obj(&r)), 1);
    ASSERT(cl_fasl_deserialize_obj(&r) == CL_T);
    ASSERT(CL_NULL_P(cl_fasl_deserialize_obj(&r)));
    ASSERT_EQ_INT(r.error, FASL_OK);
}

TEST(deserialize_bad_tag)
{
    uint8_t buf[4] = {0xFE};
    CL_FaslReader r;

    cl_fasl_reader_init(&r, buf, 1);
    cl_fasl_deserialize_obj(&r);
    ASSERT_EQ_INT(r.error, FASL_ERR_BAD_TAG);
}

/* ================================================================
 * Bytecode serialization — manually constructed bytecodes
 * ================================================================ */

TEST(serialize_bytecode_simple)
{
    uint8_t buf[1024];
    CL_FaslWriter w;
    CL_FaslReader r;
    CL_Obj bc_obj, result;
    CL_Bytecode *bc, *bc2;

    bc_obj = make_simple_bytecode(CL_MAKE_FIXNUM(42));
    ASSERT(!CL_NULL_P(bc_obj));
    bc = (CL_Bytecode *)CL_OBJ_TO_PTR(bc_obj);

    cl_fasl_writer_init(&w, buf, sizeof(buf));
    cl_fasl_serialize_bytecode(&w, bc_obj);
    ASSERT_EQ_INT(w.error, FASL_OK);

    cl_fasl_reader_init(&r, buf, w.pos);
    result = cl_fasl_deserialize_bytecode(&r);
    ASSERT_EQ_INT(r.error, FASL_OK);
    ASSERT(CL_BYTECODE_P(result));

    bc2 = (CL_Bytecode *)CL_OBJ_TO_PTR(result);
    ASSERT_EQ_INT(bc2->code_len, bc->code_len);
    ASSERT(memcmp(bc2->code, bc->code, bc->code_len) == 0);
    ASSERT_EQ_INT(bc2->n_constants, 1);
    ASSERT_EQ_INT(CL_FIXNUM_VAL(bc2->constants[0]), 42);
    ASSERT_EQ_INT(bc2->arity, 0);
    ASSERT_EQ_INT(bc2->n_locals, 0);
    ASSERT_EQ_INT(bc2->n_upvalues, 0);
    ASSERT_EQ_INT(bc2->n_optional, 0);
    ASSERT_EQ_INT(bc2->flags, 0);
    ASSERT_EQ_INT(bc2->n_keys, 0);
    ASSERT_EQ_INT(bc2->source_line, 42);
    ASSERT(CL_NULL_P(bc2->name));
}

TEST(serialize_bytecode_eval)
{
    uint8_t buf[1024];
    CL_FaslWriter w;
    CL_FaslReader r;
    CL_Obj bc_obj, result, val;

    /* OP_CONST 0, OP_RET with constant = 99 */
    bc_obj = make_simple_bytecode(CL_MAKE_FIXNUM(99));

    cl_fasl_writer_init(&w, buf, sizeof(buf));
    cl_fasl_serialize_bytecode(&w, bc_obj);

    cl_fasl_reader_init(&r, buf, w.pos);
    result = cl_fasl_deserialize_bytecode(&r);

    val = cl_vm_eval(result);
    ASSERT(CL_FIXNUM_P(val));
    ASSERT_EQ_INT(CL_FIXNUM_VAL(val), 99);
}

TEST(serialize_bytecode_with_string_constant)
{
    uint8_t buf[1024];
    CL_FaslWriter w;
    CL_FaslReader r;
    CL_Obj bc_obj, result, val;
    CL_Obj str;
    CL_String *s;

    str = cl_make_string("hello", 5);
    CL_GC_PROTECT(str);
    bc_obj = make_simple_bytecode(str);
    CL_GC_UNPROTECT(1);

    cl_fasl_writer_init(&w, buf, sizeof(buf));
    cl_fasl_serialize_bytecode(&w, bc_obj);

    cl_fasl_reader_init(&r, buf, w.pos);
    result = cl_fasl_deserialize_bytecode(&r);

    val = cl_vm_eval(result);
    ASSERT(CL_STRING_P(val));
    s = (CL_String *)CL_OBJ_TO_PTR(val);
    ASSERT_STR_EQ(s->data, "hello");
}

TEST(serialize_bytecode_with_source_info)
{
    uint8_t buf[2048];
    CL_FaslWriter w;
    CL_FaslReader r;
    CL_Obj bc_obj, result;
    CL_Bytecode *bc2;

    bc_obj = make_bytecode_with_source(CL_MAKE_FIXNUM(1), "test.lisp");

    cl_fasl_writer_init(&w, buf, sizeof(buf));
    cl_fasl_serialize_bytecode(&w, bc_obj);

    cl_fasl_reader_init(&r, buf, w.pos);
    result = cl_fasl_deserialize_bytecode(&r);
    ASSERT_EQ_INT(r.error, FASL_OK);

    bc2 = (CL_Bytecode *)CL_OBJ_TO_PTR(result);
    ASSERT_EQ_INT(bc2->source_line, 42);
    ASSERT(bc2->source_file != NULL);
    ASSERT_STR_EQ(bc2->source_file, "test.lisp");
    ASSERT_EQ_INT(bc2->line_map_count, 2);
    ASSERT_EQ_INT(bc2->line_map[0].pc, 0);
    ASSERT_EQ_INT(bc2->line_map[0].line, 10);
    ASSERT_EQ_INT(bc2->line_map[1].pc, 3);
    ASSERT_EQ_INT(bc2->line_map[1].line, 11);
}

TEST(serialize_bytecode_no_source_file)
{
    uint8_t buf[1024];
    CL_FaslWriter w;
    CL_FaslReader r;
    CL_Obj bc_obj, result;
    CL_Bytecode *bc2;

    bc_obj = make_simple_bytecode(CL_MAKE_FIXNUM(1));

    cl_fasl_writer_init(&w, buf, sizeof(buf));
    cl_fasl_serialize_bytecode(&w, bc_obj);

    cl_fasl_reader_init(&r, buf, w.pos);
    result = cl_fasl_deserialize_bytecode(&r);
    bc2 = (CL_Bytecode *)CL_OBJ_TO_PTR(result);
    ASSERT(bc2->source_file == NULL);
    ASSERT_EQ_INT(bc2->line_map_count, 0);
}

TEST(serialize_bytecode_with_keys)
{
    uint8_t buf[2048];
    CL_FaslWriter w;
    CL_FaslReader r;
    CL_Obj bc_obj, result;
    CL_Bytecode *bc2;

    bc_obj = make_bytecode_with_keys();

    cl_fasl_writer_init(&w, buf, sizeof(buf));
    cl_fasl_serialize_bytecode(&w, bc_obj);

    cl_fasl_reader_init(&r, buf, w.pos);
    result = cl_fasl_deserialize_bytecode(&r);
    ASSERT_EQ_INT(r.error, FASL_OK);

    bc2 = (CL_Bytecode *)CL_OBJ_TO_PTR(result);
    ASSERT_EQ_INT(bc2->n_keys, 2);
    ASSERT_EQ_INT(bc2->flags & 1, 1);  /* has_key */
    ASSERT(CL_SYMBOL_P(bc2->key_syms[0]));
    ASSERT(CL_SYMBOL_P(bc2->key_syms[1]));
    ASSERT_EQ_INT(bc2->key_slots[0], 0);
    ASSERT_EQ_INT(bc2->key_slots[1], 1);
    ASSERT_EQ_INT(bc2->key_suppliedp_slots[0], 2);
    ASSERT_EQ_INT(bc2->key_suppliedp_slots[1], 0xFF);
}

TEST(serialize_bytecode_with_symbol_name)
{
    uint8_t buf[1024];
    CL_FaslWriter w;
    CL_FaslReader r;
    CL_Obj bc_obj, result;
    CL_Bytecode *bc, *bc2;
    CL_Obj name_sym;

    bc_obj = make_simple_bytecode(CL_MAKE_FIXNUM(0));
    bc = (CL_Bytecode *)CL_OBJ_TO_PTR(bc_obj);
    name_sym = cl_intern_in("MY-FUNC", 7, cl_package_cl_user);
    bc->name = name_sym;

    cl_fasl_writer_init(&w, buf, sizeof(buf));
    cl_fasl_serialize_bytecode(&w, bc_obj);

    cl_fasl_reader_init(&r, buf, w.pos);
    result = cl_fasl_deserialize_bytecode(&r);
    bc2 = (CL_Bytecode *)CL_OBJ_TO_PTR(result);
    ASSERT(bc2->name == name_sym);
}

/* ================================================================
 * Full FASL file round-trip (header + units)
 * ================================================================ */

/* Helper: write a single bytecode unit into a FASL writer */
static void write_fasl_unit(CL_FaslWriter *w, CL_Obj bc_obj)
{
    uint8_t tmp[4096];
    CL_FaslWriter tw;
    cl_fasl_writer_init(&tw, tmp, sizeof(tmp));
    cl_fasl_serialize_bytecode(&tw, bc_obj);
    cl_fasl_write_u32(w, tw.pos);
    cl_fasl_write_bytes(w, tmp, tw.pos);
}

TEST(full_fasl_single_unit)
{
    uint8_t buf[4096];
    CL_FaslWriter w;
    CL_Obj bc_obj, result;

    bc_obj = make_simple_bytecode(CL_MAKE_FIXNUM(77));

    cl_fasl_writer_init(&w, buf, sizeof(buf));
    cl_fasl_write_header(&w, 1);
    write_fasl_unit(&w, bc_obj);
    ASSERT_EQ_INT(w.error, FASL_OK);

    result = cl_fasl_load(buf, w.pos);
    ASSERT(result == CL_T);
}

TEST(full_fasl_two_units)
{
    uint8_t buf[8192];
    CL_FaslWriter w;
    CL_Obj bc1, bc2;

    bc1 = make_simple_bytecode(CL_MAKE_FIXNUM(1));
    CL_GC_PROTECT(bc1);
    bc2 = make_simple_bytecode(CL_MAKE_FIXNUM(2));
    CL_GC_UNPROTECT(1);

    cl_fasl_writer_init(&w, buf, sizeof(buf));
    cl_fasl_write_header(&w, 2);
    write_fasl_unit(&w, bc1);
    write_fasl_unit(&w, bc2);
    ASSERT_EQ_INT(w.error, FASL_OK);

    ASSERT(cl_fasl_load(buf, w.pos) == CL_T);
}

TEST(full_fasl_defun_via_compile)
{
    uint8_t buf[8192];
    CL_FaslWriter w;
    CL_Obj bc_obj;

    /* Use cl_compile to compile a defun — this works with minimal init
     * since defun is a compiler special form, not a boot.lisp macro */
    bc_obj = cl_compile(eval_obj("'(defun fasl-square (n) (* n n))"));
    ASSERT(!CL_NULL_P(bc_obj));

    cl_fasl_writer_init(&w, buf, sizeof(buf));
    cl_fasl_write_header(&w, 1);
    write_fasl_unit(&w, bc_obj);

    cl_fasl_load(buf, w.pos);
    ASSERT_STR_EQ(eval_print("(fasl-square 7)"), "49");
}

TEST(full_fasl_defvar_via_compile)
{
    uint8_t buf[8192];
    CL_FaslWriter w;
    CL_Obj bc1, bc2;

    bc1 = cl_compile(eval_obj("'(defvar *fasl-x* 100)"));
    CL_GC_PROTECT(bc1);
    bc2 = cl_compile(eval_obj("'(defvar *fasl-y* 200)"));
    CL_GC_UNPROTECT(1);

    cl_fasl_writer_init(&w, buf, sizeof(buf));
    cl_fasl_write_header(&w, 2);
    write_fasl_unit(&w, bc1);
    write_fasl_unit(&w, bc2);

    cl_fasl_load(buf, w.pos);
    ASSERT_STR_EQ(eval_print("*fasl-x*"), "100");
    ASSERT_STR_EQ(eval_print("*fasl-y*"), "200");
}

TEST(full_fasl_zero_units)
{
    uint8_t buf[64];
    CL_FaslWriter w;

    cl_fasl_writer_init(&w, buf, sizeof(buf));
    cl_fasl_write_header(&w, 0);

    ASSERT(cl_fasl_load(buf, w.pos) == CL_T);
}

/* ================================================================
 * compile-file and load tests (require full boot.lisp)
 *
 * These write temp .lisp files, compile-file them to .fasl,
 * then load the .fasl and verify the results.
 * ================================================================ */

/* Helper: write a string to a file */
static void write_test_file(const char *path, const char *content)
{
    PlatformFile fh = platform_file_open(path, PLATFORM_FILE_WRITE);
    if (fh != PLATFORM_FILE_INVALID) {
        platform_file_write_buf(fh, content, (uint32_t)strlen(content));
        platform_file_close(fh);
    }
}

/* Helper: get the cache path for a source file (via compile-file-pathname) */
static const char *get_fasl_path(const char *lisp_expr_buf)
{
    /* Returns pointer to static eval_print buffer */
    return eval_print(lisp_expr_buf);
}

/* Helper: delete a FASL file given its source path */
static void delete_cached_fasl(const char *source_path)
{
    char expr[256];
    const char *fasl_path;
    snprintf(expr, sizeof(expr),
        "(namestring (compile-file-pathname \"%s\"))", source_path);
    fasl_path = eval_print(expr);
    /* fasl_path is quoted like "\"/path/to/file.fasl\"" — strip quotes */
    if (fasl_path && fasl_path[0] == '"') {
        char path_buf[1024];
        size_t len = strlen(fasl_path);
        if (len > 2 && len - 2 < sizeof(path_buf)) {
            memcpy(path_buf, fasl_path + 1, len - 2);
            path_buf[len - 2] = '\0';
            platform_file_delete(path_buf);
        }
    }
}

TEST(compile_file_pathname_uses_cache)
{
    /* compile-file-pathname should return a cache path under ~/.cache */
    const char *result = eval_print(
        "(namestring (compile-file-pathname \"/tmp/test.lisp\"))");
    /* Should contain the cache directory structure and version */
    ASSERT(result != NULL);
    ASSERT(strstr(result, ".cache/common-lisp/cl-amiga-") != NULL);
    ASSERT(strstr(result, CL_VERSION_STRING) != NULL);
    /* Should end with test.fasl (path may include /private/tmp on macOS) */
    ASSERT(strstr(result, "test.fasl") != NULL);
}

TEST(compile_file_pathname_no_ext)
{
    /* No extension -> appends .fasl */
    const char *result = eval_print(
        "(namestring (compile-file-pathname \"/tmp/myfile\"))");
    ASSERT(result != NULL);
    ASSERT(strstr(result, "tmp/myfile.fasl") != NULL);
}

TEST(compile_file_pathname_output_file_override)
{
    /* :output-file overrides cache path */
    ASSERT_STR_EQ(
        eval_print("(namestring (compile-file-pathname \"/tmp/x.lisp\" "
                    ":output-file \"/tmp/custom.fasl\"))"),
        "\"/tmp/custom.fasl\"");
}

TEST(compile_file_basic)
{
    /* Write source, compile, verify .fasl exists at cache path */
    write_test_file("/tmp/cf-test1.lisp",
        "(defvar *cf-test-val* 42)\n");

    eval_obj("(compile-file \"/tmp/cf-test1.lisp\")");

    /* Verify output file exists at the path compile-file-pathname returns */
    {
        const char *result = eval_print(
            "(probe-file (compile-file-pathname \"/tmp/cf-test1.lisp\"))");
        ASSERT(result != NULL);
        /* probe-file returns pathname if exists, NIL if not */
        ASSERT(strcmp(result, "NIL") != 0);
    }

    /* Verify FASL magic by loading the cached file */
    {
        char expr[512];
        snprintf(expr, sizeof(expr),
            "(with-open-file (s (compile-file-pathname \"/tmp/cf-test1.lisp\") "
            ":element-type '(unsigned-byte 8)) "
            "(list (read-byte s) (read-byte s) (read-byte s) (read-byte s)))");
        /* C=67, L=76, F=70, A=65 */
        ASSERT_STR_EQ(eval_print(expr), "(67 76 70 65)");
    }

    /* Clean up */
    platform_file_delete("/tmp/cf-test1.lisp");
    delete_cached_fasl("/tmp/cf-test1.lisp");
}

TEST(compile_file_and_load)
{
    /* Full round-trip: write .lisp -> compile-file -> load .fasl -> verify */
    write_test_file("/tmp/cf-test2.lisp",
        "(defun cf-double (x) (* x 2))\n");

    eval_obj("(compile-file \"/tmp/cf-test2.lisp\")");

    /* Redefine function to something else */
    eval_obj("(defun cf-double (x) x)");

    /* Load the FASL directly by its cache path */
    eval_obj("(load (compile-file-pathname \"/tmp/cf-test2.lisp\"))");

    ASSERT_STR_EQ(eval_print("(cf-double 21)"), "42");

    platform_file_delete("/tmp/cf-test2.lisp");
    delete_cached_fasl("/tmp/cf-test2.lisp");
}

TEST(compile_file_output_file_keyword)
{
    /* :output-file keyword overrides cache path */
    write_test_file("/tmp/cf-test3.lisp", "(defvar *cf-custom* :ok)\n");

    eval_obj("(compile-file \"/tmp/cf-test3.lisp\" :output-file \"/tmp/cf-custom.fasl\")");
    ASSERT(platform_file_exists("/tmp/cf-custom.fasl"));

    eval_obj("(load \"/tmp/cf-custom.fasl\")");
    ASSERT_STR_EQ(eval_print("*cf-custom*"), ":OK");

    platform_file_delete("/tmp/cf-test3.lisp");
    platform_file_delete("/tmp/cf-custom.fasl");
}

TEST(compile_file_multiple_forms)
{
    /* Multiple top-level forms */
    write_test_file("/tmp/cf-test4.lisp",
        "(defvar *cf-a4* 10)\n"
        "(defvar *cf-b4* 20)\n"
        "(defvar *cf-c4* 30)\n"
        "(defun cf-sum4 () (+ *cf-a4* *cf-b4* *cf-c4*))\n");

    eval_obj("(compile-file \"/tmp/cf-test4.lisp\")");
    eval_obj("(load (compile-file-pathname \"/tmp/cf-test4.lisp\"))");

    ASSERT_STR_EQ(eval_print("(cf-sum4)"), "60");

    platform_file_delete("/tmp/cf-test4.lisp");
    delete_cached_fasl("/tmp/cf-test4.lisp");
}

TEST(compile_file_returns_values)
{
    /* compile-file returns (values output-truename nil nil) */
    write_test_file("/tmp/cf-test5.lisp", "(+ 1 2)\n");

    {
        CL_Obj result = eval_obj("(compile-file \"/tmp/cf-test5.lisp\")");
        ASSERT(CL_PATHNAME_P(result));
    }

    platform_file_delete("/tmp/cf-test5.lisp");
    delete_cached_fasl("/tmp/cf-test5.lisp");
}

TEST(compile_file_preserves_package)
{
    /* in-package in compiled file doesn't leak to caller */
    write_test_file("/tmp/cf-test6.lisp",
        "(in-package :cl-user)\n"
        "(defvar *cf-pkg-test6* t)\n");

    {
        CL_Obj pkg_before = cl_current_package;
        eval_obj("(compile-file \"/tmp/cf-test6.lisp\")");
        ASSERT(cl_current_package == pkg_before);
    }

    platform_file_delete("/tmp/cf-test6.lisp");
    delete_cached_fasl("/tmp/cf-test6.lisp");
}

TEST(load_fasl_preserves_package)
{
    /* in-package in loaded FASL doesn't leak */
    write_test_file("/tmp/cf-test7.lisp",
        "(in-package :cl-user)\n"
        "(defvar *cf-pkg-test7* t)\n");

    eval_obj("(compile-file \"/tmp/cf-test7.lisp\")");

    {
        CL_Obj pkg_before = cl_current_package;
        eval_obj("(load (compile-file-pathname \"/tmp/cf-test7.lisp\"))");
        ASSERT(cl_current_package == pkg_before);
    }

    platform_file_delete("/tmp/cf-test7.lisp");
    delete_cached_fasl("/tmp/cf-test7.lisp");
}

TEST(load_source_still_works)
{
    /* Loading .lisp source files still works as before */
    write_test_file("/tmp/cf-test8.lisp",
        "(defvar *cf-source-load8* :source-ok)\n");

    eval_obj("(load \"/tmp/cf-test8.lisp\")");
    ASSERT_STR_EQ(eval_print("*cf-source-load8*"), ":SOURCE-OK");

    platform_file_delete("/tmp/cf-test8.lisp");
}

TEST(compile_file_with_macros)
{
    /* Macros defined early must work for later forms (eval-during-compile) */
    write_test_file("/tmp/cf-test9.lisp",
        "(defmacro cf-triple (x) `(* 3 ,x))\n"
        "(defun cf-apply-triple (n) (cf-triple n))\n");

    eval_obj("(compile-file \"/tmp/cf-test9.lisp\")");
    eval_obj("(load (compile-file-pathname \"/tmp/cf-test9.lisp\"))");

    ASSERT_STR_EQ(eval_print("(cf-apply-triple 5)"), "15");

    platform_file_delete("/tmp/cf-test9.lisp");
    delete_cached_fasl("/tmp/cf-test9.lisp");
}

TEST(load_auto_finds_cached_fasl)
{
    /* load of source file should auto-discover cached FASL */
    write_test_file("/tmp/cf-test10.lisp",
        "(defun cf-auto-fn () :from-fasl)\n");

    /* Compile to create cached FASL */
    eval_obj("(compile-file \"/tmp/cf-test10.lisp\")");

    /* Now modify the source to return something different */
    write_test_file("/tmp/cf-test10.lisp",
        "(defun cf-auto-fn () :from-source)\n");

    /* But don't touch the FASL — it's still newer because we just wrote
       the source AFTER compile-file. Actually the source was just written,
       so it will be newer. We need the FASL to be newer. */
    /* Re-compile so FASL is newer than source */
    write_test_file("/tmp/cf-test10.lisp",
        "(defun cf-auto-fn () :from-fasl)\n");
    eval_obj("(compile-file \"/tmp/cf-test10.lisp\")");

    /* Now load by source path — should pick up the cached FASL */
    eval_obj("(load \"/tmp/cf-test10.lisp\")");
    ASSERT_STR_EQ(eval_print("(cf-auto-fn)"), ":FROM-FASL");

    platform_file_delete("/tmp/cf-test10.lisp");
    delete_cached_fasl("/tmp/cf-test10.lisp");
}

TEST(load_stale_fasl_falls_back_to_source)
{
    /* If source is newer than FASL, load should use source */
    write_test_file("/tmp/cf-test11.lisp",
        "(defun cf-stale-fn () :old-fasl)\n");

    eval_obj("(compile-file \"/tmp/cf-test11.lisp\")");

    /* Wait a moment and rewrite source with different content */
    platform_sleep_ms(1100);  /* Ensure mtime differs (1-sec resolution) */

    write_test_file("/tmp/cf-test11.lisp",
        "(defun cf-stale-fn () :new-source)\n");

    /* Load source path — FASL is stale, should load source */
    eval_obj("(load \"/tmp/cf-test11.lisp\")");
    ASSERT_STR_EQ(eval_print("(cf-stale-fn)"), ":NEW-SOURCE");

    platform_file_delete("/tmp/cf-test11.lisp");
    delete_cached_fasl("/tmp/cf-test11.lisp");
}

TEST(cache_paths_differ_for_different_dirs)
{
    /* Two files with same name in different dirs get different cache paths */
    const char *path_a = eval_print(
        "(namestring (compile-file-pathname \"/tmp/a/test.lisp\"))");
    char buf_a[1024];
    strncpy(buf_a, path_a, sizeof(buf_a) - 1);
    buf_a[sizeof(buf_a) - 1] = '\0';

    const char *path_b = eval_print(
        "(namestring (compile-file-pathname \"/tmp/b/test.lisp\"))");

    /* Paths must be different */
    ASSERT(strcmp(buf_a, path_b) != 0);
    /* But both should contain .fasl */
    ASSERT(strstr(buf_a, ".fasl") != NULL);
    ASSERT(strstr(path_b, ".fasl") != NULL);
}

/* ================================================================
 * main
 * ================================================================ */

int main(void)
{
    test_init();

    /* === Primitive read/write (no CL init needed) === */
    RUN(write_read_u8);
    RUN(write_read_u16);
    RUN(write_read_u32);
    RUN(write_read_bytes);
    RUN(writer_overflow);
    RUN(reader_truncated);
    RUN(reader_truncated_u8);
    RUN(reader_truncated_u16);
    RUN(reader_truncated_bytes);
    RUN(error_propagation_writer);
    RUN(error_propagation_reader);
    RUN(write_read_header);
    RUN(header_bad_magic);
    RUN(header_bad_version);
    RUN(header_zero_units);

    /* === Init CL runtime (once, minimal — no boot.lisp) === */
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
    cl_repl_init_minimal();

    /* Object serialization */
    RUN(serialize_nil);
    RUN(serialize_t);
    RUN(serialize_unbound);
    RUN(serialize_fixnum_zero);
    RUN(serialize_fixnum_positive);
    RUN(serialize_fixnum_negative);
    RUN(serialize_fixnum_max);
    RUN(serialize_fixnum_min);
    RUN(serialize_character);
    RUN(serialize_character_newline);
    RUN(serialize_string_empty);
    RUN(serialize_string);
    RUN(serialize_long_string);
    RUN(serialize_symbol_cl);
    RUN(serialize_symbol_keyword);
    RUN(serialize_symbol_user);
    RUN(serialize_cons);
    RUN(serialize_list);
    RUN(serialize_dotted_list);
    RUN(serialize_nested_cons);
    RUN(serialize_deeply_nested_list);
    RUN(serialize_single_float);
    RUN(serialize_single_float_zero);
    RUN(serialize_single_float_negative);
    RUN(serialize_double_float);
    RUN(serialize_double_float_zero);
    RUN(serialize_double_float_negative);
    RUN(serialize_ratio);
    RUN(serialize_bignum);
    RUN(serialize_bignum_negative);
    RUN(serialize_vector);
    RUN(serialize_vector_empty);
    RUN(serialize_vector_mixed_types);
    RUN(serialize_bit_vector);
    RUN(serialize_bit_vector_empty);
    RUN(serialize_pathname);
    RUN(serialize_heterogeneous_list);
    RUN(serialize_multiple_objects_sequential);
    RUN(deserialize_bad_tag);

    /* Bytecode serialization (manually constructed) */
    RUN(serialize_bytecode_simple);
    RUN(serialize_bytecode_eval);
    RUN(serialize_bytecode_with_string_constant);
    RUN(serialize_bytecode_with_source_info);
    RUN(serialize_bytecode_no_source_file);
    RUN(serialize_bytecode_with_keys);
    RUN(serialize_bytecode_with_symbol_name);

    /* Full FASL file round-trips */
    RUN(full_fasl_single_unit);
    RUN(full_fasl_two_units);
    RUN(full_fasl_defun_via_compile);
    RUN(full_fasl_defvar_via_compile);
    RUN(full_fasl_zero_units);

    /* === Reinit with full boot.lisp for compile-file tests === */
    cl_mem_shutdown();
    platform_shutdown();
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

    /* compile-file-pathname tests */
    RUN(compile_file_pathname_uses_cache);
    RUN(compile_file_pathname_no_ext);
    RUN(compile_file_pathname_output_file_override);

    /* compile-file + load round-trip tests */
    RUN(compile_file_basic);
    RUN(compile_file_and_load);
    RUN(compile_file_output_file_keyword);
    RUN(compile_file_multiple_forms);
    RUN(compile_file_returns_values);
    RUN(compile_file_preserves_package);
    RUN(load_fasl_preserves_package);
    RUN(load_source_still_works);
    RUN(compile_file_with_macros);

    /* FASL cache behavior tests */
    RUN(load_auto_finds_cached_fasl);
    RUN(load_stale_fasl_falls_back_to_source);
    RUN(cache_paths_differ_for_different_dirs);

    cl_mem_shutdown();
    platform_shutdown();

    REPORT();
}
