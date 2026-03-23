/*
 * builtins_describe.c — DESCRIBE function
 *
 * (describe object &optional stream) — prints descriptive information
 * about an object to a stream (default *standard-output*), returns no values.
 */

#include "builtins.h"
#include "symbol.h"
#include "package.h"
#include "mem.h"
#include "error.h"
#include "printer.h"
#include "stream.h"
#include "vm.h"
#include "float.h"
#include "../platform/platform.h"
#include <string.h>
#include <stdio.h>

/* Helper to register a builtin */
static void defun(const char *name, CL_CFunc func, int min, int max)
{
    CL_Obj sym = cl_intern_in(name, (uint32_t)strlen(name), cl_package_cl);
    CL_Obj fn = cl_make_function(func, sym, min, max);
    CL_Symbol *s = (CL_Symbol *)CL_OBJ_TO_PTR(sym);
    s->function = fn;
    s->value = fn;
}

/* --- Helpers --- */

static void write_str(CL_Obj stream, const char *str)
{
    cl_stream_write_string(stream, str, (uint32_t)strlen(str));
}

static void write_nl(CL_Obj stream)
{
    cl_stream_write_char(stream, '\n');
}

static void write_line(CL_Obj stream, const char *str)
{
    write_str(stream, str);
    write_nl(stream);
}

/* Print obj representation (prin1 style) to stream */
static void write_obj(CL_Obj stream, CL_Obj obj)
{
    char buf[256];
    cl_prin1_to_string(obj, buf, sizeof(buf));
    write_str(stream, buf);
}


static CL_Obj resolve_stream(CL_Obj *args, int n)
{
    if (n >= 2 && !CL_NULL_P(args[1])) {
        if (args[1] == CL_T)
            return cl_symbol_value(SYM_STANDARD_OUTPUT);
        return args[1];
    }
    return cl_symbol_value(SYM_STANDARD_OUTPUT);
}

/* Count length of a proper list. Returns -1 if dotted/circular. */
static int list_length(CL_Obj obj)
{
    int len = 0;
    CL_Obj slow = obj, fast = obj;
    while (!CL_NULL_P(fast)) {
        if (!CL_CONS_P(fast)) return -1;
        fast = cl_cdr(fast);
        len++;
        if (CL_NULL_P(fast)) break;
        if (!CL_CONS_P(fast)) return -1;
        fast = cl_cdr(fast);
        len++;
        slow = cl_cdr(slow);
        if (slow == fast) return -1; /* circular */
    }
    return len;
}

/* --- Type-specific describers --- */

static void describe_nil(CL_Obj stream)
{
    write_line(stream, "NIL is the symbol NIL");
    write_line(stream, "  It is also the empty list and the boolean false.");
    write_line(stream, "  Value: NIL");
    write_line(stream, "  Type: NULL");
}

static void describe_fixnum(CL_Obj obj, CL_Obj stream)
{
    char buf[64];
    int32_t val = CL_FIXNUM_VAL(obj);
    write_obj(stream, obj);
    write_line(stream, " is a FIXNUM");
    snprintf(buf, sizeof(buf), "  Value: %d", (int)val);
    write_line(stream, buf);
}

static void describe_character(CL_Obj obj, CL_Obj stream)
{
    char buf[64];
    int ch = CL_CHAR_VAL(obj);
    write_obj(stream, obj);
    write_line(stream, " is a CHARACTER");
    if (ch >= 32 && ch < 127) {
        snprintf(buf, sizeof(buf), "  Char: '%c'", (char)ch);
        write_line(stream, buf);
    }
    snprintf(buf, sizeof(buf), "  Char-code: %d", ch);
    write_line(stream, buf);
}

static void describe_symbol(CL_Obj obj, CL_Obj stream)
{
    CL_Symbol *sym = (CL_Symbol *)CL_OBJ_TO_PTR(obj);
    CL_String *name_str;

    write_obj(stream, obj);
    write_line(stream, " is a SYMBOL");

    name_str = (CL_String *)CL_OBJ_TO_PTR(sym->name);
    write_str(stream, "  Name: \"");
    cl_stream_write_string(stream, name_str->data, name_str->length);
    write_line(stream, "\"");

    if (!CL_NULL_P(sym->package)) {
        write_str(stream, "  Package: ");
        {
            CL_Package *pkg = (CL_Package *)CL_OBJ_TO_PTR(sym->package);
            write_obj(stream, pkg->name);
        }
        write_nl(stream);
    } else {
        write_line(stream, "  Package: NIL (uninterned)");
    }

    {
        CL_Obj sv = cl_symbol_value(obj);
        if (sv != CL_UNBOUND) {
            write_str(stream, "  Value: ");
            write_obj(stream, sv);
            write_nl(stream);
        } else {
            write_line(stream, "  Value: <unbound>");
        }
    }

    if (sym->function != CL_UNBOUND) {
        write_str(stream, "  Function: ");
        write_obj(stream, sym->function);
        write_nl(stream);
    }

    if (!CL_NULL_P(sym->plist)) {
        write_str(stream, "  Plist: ");
        write_obj(stream, sym->plist);
        write_nl(stream);
    }

    /* Flags */
    if (sym->flags) {
        write_str(stream, "  Flags:");
        if (sym->flags & CL_SYM_SPECIAL)  write_str(stream, " SPECIAL");
        if (sym->flags & CL_SYM_CONSTANT) write_str(stream, " CONSTANT");
        if (sym->flags & CL_SYM_EXPORTED) write_str(stream, " EXPORTED");
        if (sym->flags & CL_SYM_TRACED)   write_str(stream, " TRACED");
        if (sym->flags & CL_SYM_INLINE)   write_str(stream, " INLINE");
        write_nl(stream);
    }
}

static void describe_cons(CL_Obj obj, CL_Obj stream)
{
    int len;

    write_obj(stream, obj);
    write_line(stream, " is a CONS");

    len = list_length(obj);
    if (len >= 0) {
        char buf[64];
        snprintf(buf, sizeof(buf), "  Length: %d", len);
        write_line(stream, buf);
    } else {
        write_line(stream, "  (dotted or circular list)");
    }

    write_str(stream, "  Car: ");
    write_obj(stream, cl_car(obj));
    write_nl(stream);
    write_str(stream, "  Cdr: ");
    write_obj(stream, cl_cdr(obj));
    write_nl(stream);
}

static void describe_string(CL_Obj obj, CL_Obj stream)
{
    CL_String *str = (CL_String *)CL_OBJ_TO_PTR(obj);
    char buf[64];

    write_obj(stream, obj);
    write_line(stream, " is a STRING");
    snprintf(buf, sizeof(buf), "  Length: %u", (unsigned)str->length);
    write_line(stream, buf);
}

static void describe_function(CL_Obj obj, CL_Obj stream)
{
    CL_Function *fn = (CL_Function *)CL_OBJ_TO_PTR(obj);
    char buf[64];

    write_obj(stream, obj);
    write_line(stream, " is a FUNCTION");
    write_line(stream, "  Type: built-in");

    write_str(stream, "  Name: ");
    write_obj(stream, fn->name);
    write_nl(stream);

    snprintf(buf, sizeof(buf), "  Min-args: %d", fn->min_args);
    write_line(stream, buf);
    if (fn->max_args < 0) {
        write_line(stream, "  Max-args: variable");
    } else {
        snprintf(buf, sizeof(buf), "  Max-args: %d", fn->max_args);
        write_line(stream, buf);
    }
}

static void describe_closure(CL_Obj obj, CL_Obj stream)
{
    CL_Closure *cl = (CL_Closure *)CL_OBJ_TO_PTR(obj);
    CL_Bytecode *bc;
    char buf[64];

    write_obj(stream, obj);
    write_line(stream, " is a COMPILED-FUNCTION");

    if (CL_BYTECODE_P(cl->bytecode)) {
        bc = (CL_Bytecode *)CL_OBJ_TO_PTR(cl->bytecode);
        if (!CL_NULL_P(bc->name)) {
            write_str(stream, "  Name: ");
            write_obj(stream, bc->name);
            write_nl(stream);
        }
        snprintf(buf, sizeof(buf), "  Arity: %d", (int)(bc->arity & 0x7FFF));
        write_line(stream, buf);
        if (bc->arity & 0x8000)
            write_line(stream, "  Has &rest parameter");
        if (bc->n_upvalues > 0) {
            snprintf(buf, sizeof(buf), "  Upvalues: %u", (unsigned)bc->n_upvalues);
            write_line(stream, buf);
        }
    }
}

static void describe_vector(CL_Obj obj, CL_Obj stream)
{
    CL_Vector *vec = (CL_Vector *)CL_OBJ_TO_PTR(obj);
    char buf[64];
    uint32_t active_len, show, i;
    CL_Obj *data;

    write_obj(stream, obj);
    write_line(stream, " is a VECTOR");
    snprintf(buf, sizeof(buf), "  Length: %u", (unsigned)vec->length);
    write_line(stream, buf);

    if (vec->fill_pointer != CL_NO_FILL_POINTER) {
        snprintf(buf, sizeof(buf), "  Fill-pointer: %u", (unsigned)vec->fill_pointer);
        write_line(stream, buf);
    }
    if (vec->flags & CL_VEC_FLAG_ADJUSTABLE)
        write_line(stream, "  Adjustable: T");

    active_len = cl_vector_active_length(vec);
    data = cl_vector_data(vec);
    show = active_len > 5 ? 5 : active_len;
    if (show > 0) {
        write_str(stream, "  Elements:");
        for (i = 0; i < show; i++) {
            write_str(stream, " ");
            write_obj(stream, data[i]);
        }
        if (active_len > 5)
            write_str(stream, " ...");
        write_nl(stream);
    }
}

static void describe_package(CL_Obj obj, CL_Obj stream)
{
    CL_Package *pkg = (CL_Package *)CL_OBJ_TO_PTR(obj);
    char buf[64];

    write_obj(stream, obj);
    write_line(stream, " is a PACKAGE");

    write_str(stream, "  Name: ");
    write_obj(stream, pkg->name);
    write_nl(stream);

    if (!CL_NULL_P(pkg->nicknames)) {
        write_str(stream, "  Nicknames: ");
        write_obj(stream, pkg->nicknames);
        write_nl(stream);
    }

    if (!CL_NULL_P(pkg->use_list)) {
        write_str(stream, "  Use-list: ");
        write_obj(stream, pkg->use_list);
        write_nl(stream);
    }

    snprintf(buf, sizeof(buf), "  Symbol-count: %u", (unsigned)pkg->sym_count);
    write_line(stream, buf);
}

static void describe_hashtable(CL_Obj obj, CL_Obj stream)
{
    CL_Hashtable *ht = (CL_Hashtable *)CL_OBJ_TO_PTR(obj);
    char buf[64];
    const char *test_name;

    write_obj(stream, obj);
    write_line(stream, " is a HASH-TABLE");

    switch (ht->test) {
        case CL_HT_TEST_EQ:     test_name = "EQ";     break;
        case CL_HT_TEST_EQL:    test_name = "EQL";    break;
        case CL_HT_TEST_EQUAL:  test_name = "EQUAL";  break;
        case CL_HT_TEST_EQUALP: test_name = "EQUALP"; break;
        default:                test_name = "?";     break;
    }
    snprintf(buf, sizeof(buf), "  Test: %s", test_name);
    write_line(stream, buf);
    snprintf(buf, sizeof(buf), "  Count: %u", (unsigned)ht->count);
    write_line(stream, buf);
    snprintf(buf, sizeof(buf), "  Size: %u", (unsigned)ht->bucket_count);
    write_line(stream, buf);
}

static void describe_struct(CL_Obj obj, CL_Obj stream)
{
    CL_Struct *st = (CL_Struct *)CL_OBJ_TO_PTR(obj);
    char buf[64];

    extern CL_Obj cl_struct_slot_names(CL_Obj type_name);

    write_obj(stream, obj);
    write_line(stream, " is a STRUCTURE");

    write_str(stream, "  Type: ");
    write_obj(stream, st->type_desc);
    write_nl(stream);

    snprintf(buf, sizeof(buf), "  Slots: %u", (unsigned)st->n_slots);
    write_line(stream, buf);

    /* Show slot names and values */
    {
        CL_Obj names = cl_struct_slot_names(st->type_desc);
        uint32_t i = 0;
        while (!CL_NULL_P(names) && i < st->n_slots) {
            write_str(stream, "  ");
            write_obj(stream, cl_car(names));
            write_str(stream, ": ");
            write_obj(stream, st->slots[i]);
            write_nl(stream);
            names = cl_cdr(names);
            i++;
        }
    }
}

static void describe_condition(CL_Obj obj, CL_Obj stream)
{
    CL_Condition *cond = (CL_Condition *)CL_OBJ_TO_PTR(obj);

    write_obj(stream, obj);
    write_line(stream, " is a CONDITION");

    write_str(stream, "  Type: ");
    write_obj(stream, cond->type_name);
    write_nl(stream);

    if (!CL_NULL_P(cond->report_string)) {
        write_str(stream, "  Report: ");
        write_obj(stream, cond->report_string);
        write_nl(stream);
    }

    if (!CL_NULL_P(cond->slots)) {
        write_str(stream, "  Slots: ");
        write_obj(stream, cond->slots);
        write_nl(stream);
    }
}

static void describe_bignum(CL_Obj obj, CL_Obj stream)
{
    CL_Bignum *bn = (CL_Bignum *)CL_OBJ_TO_PTR(obj);
    char buf[64];

    write_obj(stream, obj);
    write_line(stream, " is a BIGNUM");

    snprintf(buf, sizeof(buf), "  Sign: %s", bn->sign ? "negative" : "positive");
    write_line(stream, buf);
    snprintf(buf, sizeof(buf), "  Limbs: %u", (unsigned)bn->length);
    write_line(stream, buf);
}

static void describe_single_float(CL_Obj obj, CL_Obj stream)
{
    write_obj(stream, obj);
    write_line(stream, " is a SINGLE-FLOAT");
}

static void describe_double_float(CL_Obj obj, CL_Obj stream)
{
    write_obj(stream, obj);
    write_line(stream, " is a DOUBLE-FLOAT");
}

static void describe_ratio(CL_Obj obj, CL_Obj stream)
{
    CL_Ratio *r = (CL_Ratio *)CL_OBJ_TO_PTR(obj);

    write_obj(stream, obj);
    write_line(stream, " is a RATIO");

    write_str(stream, "  Numerator: ");
    write_obj(stream, r->numerator);
    write_nl(stream);
    write_str(stream, "  Denominator: ");
    write_obj(stream, r->denominator);
    write_nl(stream);
}

static void describe_stream(CL_Obj obj, CL_Obj stream)
{
    CL_Stream *st = (CL_Stream *)CL_OBJ_TO_PTR(obj);
    const char *dir;
    const char *stype;

    write_obj(stream, obj);
    write_line(stream, " is a STREAM");

    switch (st->direction) {
        case CL_STREAM_INPUT:  dir = "INPUT";  break;
        case CL_STREAM_OUTPUT: dir = "OUTPUT"; break;
        case CL_STREAM_IO:     dir = "IO";     break;
        default:               dir = "?";      break;
    }
    write_str(stream, "  Direction: ");
    write_line(stream, dir);

    switch (st->stream_type) {
        case CL_STREAM_CONSOLE: stype = "CONSOLE"; break;
        case CL_STREAM_FILE:    stype = "FILE";    break;
        case CL_STREAM_STRING:  stype = "STRING";  break;
        default:                stype = "?";       break;
    }
    write_str(stream, "  Type: ");
    write_line(stream, stype);

    write_str(stream, "  Open: ");
    write_line(stream, (st->flags & CL_STREAM_FLAG_OPEN) ? "T" : "NIL");
}

static void describe_random_state(CL_Obj obj, CL_Obj stream)
{
    write_obj(stream, obj);
    write_line(stream, " is a RANDOM-STATE");
}

static void describe_bit_vector(CL_Obj obj, CL_Obj stream)
{
    CL_BitVector *bv = (CL_BitVector *)CL_OBJ_TO_PTR(obj);
    char buf[64];

    write_obj(stream, obj);
    write_line(stream, " is a BIT-VECTOR");

    snprintf(buf, sizeof(buf), "  Length: %u", (unsigned)bv->length);
    write_line(stream, buf);
}

static void describe_pathname(CL_Obj obj, CL_Obj stream)
{
    CL_Pathname *pn = (CL_Pathname *)CL_OBJ_TO_PTR(obj);

    write_obj(stream, obj);
    write_line(stream, " is a PATHNAME");

    write_str(stream, "  Host: ");
    write_obj(stream, pn->host);
    write_nl(stream);
    write_str(stream, "  Device: ");
    write_obj(stream, pn->device);
    write_nl(stream);
    write_str(stream, "  Directory: ");
    write_obj(stream, pn->directory);
    write_nl(stream);
    write_str(stream, "  Name: ");
    write_obj(stream, pn->name);
    write_nl(stream);
    write_str(stream, "  Type: ");
    write_obj(stream, pn->type);
    write_nl(stream);
    write_str(stream, "  Version: ");
    write_obj(stream, pn->version);
    write_nl(stream);
}

/* --- Main dispatch --- */

void cl_describe_to_stream(CL_Obj obj, CL_Obj stream)
{
    /* NIL */
    if (CL_NULL_P(obj)) {
        describe_nil(stream);
        return;
    }

    /* Fixnum */
    if (CL_FIXNUM_P(obj)) {
        describe_fixnum(obj, stream);
        return;
    }

    /* Character */
    if (CL_CHAR_P(obj)) {
        describe_character(obj, stream);
        return;
    }

    /* Heap objects — use if-else to avoid m68k-amigaos-gcc LTO
     * jump table bug (undefined .L labels during link) */
    if (CL_SYMBOL_P(obj))       { describe_symbol(obj, stream);       return; }
    if (CL_CONS_P(obj))         { describe_cons(obj, stream);         return; }
    if (CL_STRING_P(obj))       { describe_string(obj, stream);       return; }
    if (CL_FUNCTION_P(obj))     { describe_function(obj, stream);     return; }
    if (CL_CLOSURE_P(obj))      { describe_closure(obj, stream);      return; }
    if (CL_VECTOR_P(obj))       { describe_vector(obj, stream);       return; }
    if (CL_PACKAGE_P(obj))      { describe_package(obj, stream);      return; }
    if (CL_HASHTABLE_P(obj))    { describe_hashtable(obj, stream);    return; }
    if (CL_STRUCT_P(obj))       { describe_struct(obj, stream);       return; }
    if (CL_CONDITION_P(obj))    { describe_condition(obj, stream);    return; }
    if (CL_BIGNUM_P(obj))       { describe_bignum(obj, stream);       return; }
    if (CL_SINGLE_FLOAT_P(obj)) { describe_single_float(obj, stream); return; }
    if (CL_DOUBLE_FLOAT_P(obj)) { describe_double_float(obj, stream); return; }
    if (CL_RATIO_P(obj))        { describe_ratio(obj, stream);        return; }
    if (CL_STREAM_P(obj))       { describe_stream(obj, stream);       return; }
    if (CL_RANDOM_STATE_P(obj)) { describe_random_state(obj, stream); return; }
    if (CL_BIT_VECTOR_P(obj))   { describe_bit_vector(obj, stream);   return; }
    if (CL_PATHNAME_P(obj))     { describe_pathname(obj, stream);     return; }

    /* Fallback */
    write_obj(stream, obj);
    write_line(stream, " is an unknown object");
}

/* (describe object &optional stream) */
static CL_Obj bi_describe(CL_Obj *args, int n)
{
    CL_Obj obj = args[0];
    CL_Obj stream = resolve_stream(args, n);

    cl_describe_to_stream(obj, stream);

    /* describe returns no values */
    cl_mv_count = 0;
    return CL_NIL;
}

/* (room &optional verbosity)
   verbosity: :default (or omitted), :minimal, :full
   Prints heap usage, GC stats, and allocation info. */
static CL_Obj bi_room(CL_Obj *args, int n)
{
    char buf[256];
    uint32_t used = cl_heap.total_allocated;
    uint32_t total = cl_heap.arena_size;
    uint32_t free_bytes = total - used;
    uint32_t pct = (used * 100) / total;

    (void)args; (void)n;

    platform_write_string("Heap:\n");
    sprintf(buf, "  %lu / %lu bytes used (%lu%%)\n",
            (unsigned long)used, (unsigned long)total, (unsigned long)pct);
    platform_write_string(buf);
    sprintf(buf, "  %lu bytes free\n", (unsigned long)free_bytes);
    platform_write_string(buf);
    sprintf(buf, "  %lu bytes consed (total ever allocated)\n",
            (unsigned long)cl_heap.total_consed);
    platform_write_string(buf);
    sprintf(buf, "GC:\n  %lu collections\n  %lu compactions\n",
            (unsigned long)cl_heap.gc_count,
            (unsigned long)cl_heap.compact_count);
    platform_write_string(buf);

    cl_mv_count = 0;
    return CL_NIL;
}

/* --- Registration --- */

void cl_builtins_describe_init(void)
{
    defun("DESCRIBE", bi_describe, 1, 2);
    defun("ROOM", bi_room, 0, 1);
}
