#include "fasl.h"
#include "mem.h"
#include "symbol.h"
#include "package.h"
#include "error.h"
#include "float.h"
#include "vm.h"
#include "compiler.h"
#include "../platform/platform.h"
#include <string.h>

/* ================================================================
 * Writer
 * ================================================================ */

void cl_fasl_writer_init(CL_FaslWriter *w, uint8_t *buf, uint32_t capacity)
{
    w->data = buf;
    w->capacity = capacity;
    w->pos = 0;
    w->error = FASL_OK;
    w->gensym_count = 0;
}

void cl_fasl_write_u8(CL_FaslWriter *w, uint8_t val)
{
    if (w->error) return;
    if (w->pos + 1 > w->capacity) { w->error = FASL_ERR_OVERFLOW; return; }
    w->data[w->pos++] = val;
}

void cl_fasl_write_u16(CL_FaslWriter *w, uint16_t val)
{
    if (w->error) return;
    if (w->pos + 2 > w->capacity) { w->error = FASL_ERR_OVERFLOW; return; }
    w->data[w->pos++] = (uint8_t)(val >> 8);
    w->data[w->pos++] = (uint8_t)(val);
}

void cl_fasl_write_u32(CL_FaslWriter *w, uint32_t val)
{
    if (w->error) return;
    if (w->pos + 4 > w->capacity) { w->error = FASL_ERR_OVERFLOW; return; }
    w->data[w->pos++] = (uint8_t)(val >> 24);
    w->data[w->pos++] = (uint8_t)(val >> 16);
    w->data[w->pos++] = (uint8_t)(val >> 8);
    w->data[w->pos++] = (uint8_t)(val);
}

void cl_fasl_write_bytes(CL_FaslWriter *w, const void *data, uint32_t len)
{
    if (w->error) return;
    if (w->pos + len > w->capacity) { w->error = FASL_ERR_OVERFLOW; return; }
    memcpy(w->data + w->pos, data, len);
    w->pos += len;
}

void cl_fasl_write_header(CL_FaslWriter *w, uint32_t n_units)
{
    cl_fasl_write_u32(w, CL_FASL_MAGIC);
    cl_fasl_write_u16(w, CL_FASL_VERSION);
    cl_fasl_write_u16(w, 0);  /* flags */
    cl_fasl_write_u32(w, n_units);
}

/* --- Serialize a CL_Obj constant --- */

void cl_fasl_serialize_obj(CL_FaslWriter *w, CL_Obj obj)
{
    if (w->error) return;

    /* NIL */
    if (CL_NULL_P(obj)) {
        cl_fasl_write_u8(w, FASL_TAG_NIL);
        return;
    }

    /* T */
    if (obj == CL_T) {
        cl_fasl_write_u8(w, FASL_TAG_T);
        return;
    }

    /* Unbound marker */
    if (obj == CL_UNBOUND) {
        cl_fasl_write_u8(w, FASL_TAG_UNBOUND);
        return;
    }

    /* Fixnum */
    if (CL_FIXNUM_P(obj)) {
        cl_fasl_write_u8(w, FASL_TAG_FIXNUM);
        cl_fasl_write_u32(w, obj);  /* raw tagged value */
        return;
    }

    /* Character */
    if (CL_CHAR_P(obj)) {
        cl_fasl_write_u8(w, FASL_TAG_CHARACTER);
        cl_fasl_write_u32(w, obj);  /* raw tagged value */
        return;
    }

    /* Heap objects */
    if (!CL_HEAP_P(obj)) {
        /* Unknown immediate — treat as fixnum fallback */
        cl_fasl_write_u8(w, FASL_TAG_FIXNUM);
        cl_fasl_write_u32(w, obj);
        return;
    }

    switch (CL_HDR_TYPE(CL_OBJ_TO_PTR(obj))) {
    case TYPE_SYMBOL: {
        CL_Symbol *sym = (CL_Symbol *)CL_OBJ_TO_PTR(obj);
        CL_String *name = (CL_String *)CL_OBJ_TO_PTR(sym->name);

        if (CL_NULL_P(sym->package)) {
            /* Uninterned symbol — check gensym dedup table */
            uint16_t gi;
            for (gi = 0; gi < w->gensym_count; gi++) {
                if (w->gensym_objs[gi] == obj) {
                    cl_fasl_write_u8(w, FASL_TAG_GENSYM_REF);
                    cl_fasl_write_u16(w, gi);
                    return;
                }
            }
            /* New gensym — register and emit definition */
            if (w->gensym_count < FASL_MAX_GENSYMS) {
                w->gensym_objs[w->gensym_count] = obj;
                cl_fasl_write_u8(w, FASL_TAG_GENSYM_DEF);
                cl_fasl_write_u16(w, w->gensym_count);
                w->gensym_count++;
            } else {
                /* Fallback: emit as plain uninterned symbol (no dedup) */
                cl_fasl_write_u8(w, FASL_TAG_SYMBOL);
                cl_fasl_write_u16(w, 0);
            }
            cl_fasl_write_u16(w, (uint16_t)name->length);
            cl_fasl_write_bytes(w, name->data, name->length);
            return;
        }

        cl_fasl_write_u8(w, FASL_TAG_SYMBOL);
        {
            CL_Package *pkg = (CL_Package *)CL_OBJ_TO_PTR(sym->package);
            CL_String *pkg_name = (CL_String *)CL_OBJ_TO_PTR(pkg->name);
            if (pkg == (CL_Package *)CL_OBJ_TO_PTR(
                    cl_find_package("KEYWORD", 7))) {
                /* Keyword: package_name_len = 0xFFFF */
                cl_fasl_write_u16(w, 0xFFFF);
            } else {
                cl_fasl_write_u16(w, (uint16_t)pkg_name->length);
                cl_fasl_write_bytes(w, pkg_name->data, pkg_name->length);
            }
        }
        cl_fasl_write_u16(w, (uint16_t)name->length);
        cl_fasl_write_bytes(w, name->data, name->length);
        return;
    }

    case TYPE_STRING: {
        CL_String *str = (CL_String *)CL_OBJ_TO_PTR(obj);
        cl_fasl_write_u8(w, FASL_TAG_STRING);
        cl_fasl_write_u32(w, str->length);
        cl_fasl_write_bytes(w, str->data, str->length);
        return;
    }

#ifdef CL_WIDE_STRINGS
    case TYPE_WIDE_STRING: {
        CL_WideString *ws = (CL_WideString *)CL_OBJ_TO_PTR(obj);
        uint32_t i;
        cl_fasl_write_u8(w, FASL_TAG_WIDE_STRING);
        cl_fasl_write_u32(w, ws->length);
        for (i = 0; i < ws->length; i++)
            cl_fasl_write_u32(w, ws->data[i]);
        return;
    }
#endif

    case TYPE_CONS: {
        cl_fasl_write_u8(w, FASL_TAG_CONS);
        cl_fasl_serialize_obj(w, cl_car(obj));
        cl_fasl_serialize_obj(w, cl_cdr(obj));
        return;
    }

    case TYPE_BYTECODE: {
        cl_fasl_write_u8(w, FASL_TAG_BYTECODE);
        cl_fasl_serialize_bytecode(w, obj);
        return;
    }

    case TYPE_CLOSURE: {
        CL_Closure *cl = (CL_Closure *)CL_OBJ_TO_PTR(obj);
        uint16_t n_upvalues = 0;
        uint16_t i;
        /* Count upvalues from the bytecode's n_upvalues field */
        if (!CL_NULL_P(cl->bytecode)) {
            CL_Bytecode *bc = (CL_Bytecode *)CL_OBJ_TO_PTR(cl->bytecode);
            n_upvalues = bc->n_upvalues;
        }
        cl_fasl_write_u8(w, FASL_TAG_CLOSURE);
        cl_fasl_serialize_obj(w, cl->bytecode);
        cl_fasl_write_u16(w, n_upvalues);
        for (i = 0; i < n_upvalues; i++)
            cl_fasl_serialize_obj(w, cl->upvalues[i]);
        return;
    }

    case TYPE_FUNCTION: {
        /* C builtin function — serialize by name for symbol lookup at load */
        CL_Function *fn = (CL_Function *)CL_OBJ_TO_PTR(obj);
        if (!CL_NULL_P(fn->name) && CL_HEAP_P(fn->name) &&
            CL_HDR_TYPE(CL_OBJ_TO_PTR(fn->name)) == TYPE_SYMBOL) {
            cl_fasl_write_u8(w, FASL_TAG_FUNCTION);
            cl_fasl_serialize_obj(w, fn->name);
        } else {
            cl_fasl_write_u8(w, FASL_TAG_NIL);
        }
        return;
    }

    case TYPE_SINGLE_FLOAT: {
        CL_SingleFloat *sf = (CL_SingleFloat *)CL_OBJ_TO_PTR(obj);
        uint32_t bits;
        cl_fasl_write_u8(w, FASL_TAG_SINGLE_FLOAT);
        memcpy(&bits, &sf->value, 4);
        cl_fasl_write_u32(w, bits);
        return;
    }

    case TYPE_DOUBLE_FLOAT: {
        CL_DoubleFloat *df = (CL_DoubleFloat *)CL_OBJ_TO_PTR(obj);
        uint32_t hi, lo;
        uint8_t bytes[8];
        cl_fasl_write_u8(w, FASL_TAG_DOUBLE_FLOAT);
        memcpy(bytes, &df->value, 8);
        /* Write in big-endian order regardless of host endianness */
        /* IEEE 754 double: check endianness */
        {
            union { double d; uint8_t b[8]; } u;
            u.d = 1.0;
            if (u.b[0] == 0x3F) {
                /* Already big-endian */
                cl_fasl_write_bytes(w, bytes, 8);
            } else {
                /* Little-endian host: reverse */
                uint8_t rev[8];
                int i;
                for (i = 0; i < 8; i++) rev[i] = bytes[7 - i];
                cl_fasl_write_bytes(w, rev, 8);
            }
        }
        (void)hi; (void)lo;
        return;
    }

    case TYPE_RATIO: {
        CL_Ratio *rat = (CL_Ratio *)CL_OBJ_TO_PTR(obj);
        cl_fasl_write_u8(w, FASL_TAG_RATIO);
        cl_fasl_serialize_obj(w, rat->numerator);
        cl_fasl_serialize_obj(w, rat->denominator);
        return;
    }

    case TYPE_COMPLEX: {
        CL_Complex *cx = (CL_Complex *)CL_OBJ_TO_PTR(obj);
        cl_fasl_write_u8(w, FASL_TAG_COMPLEX);
        cl_fasl_serialize_obj(w, cx->realpart);
        cl_fasl_serialize_obj(w, cx->imagpart);
        return;
    }

    case TYPE_BIGNUM: {
        CL_Bignum *bn = (CL_Bignum *)CL_OBJ_TO_PTR(obj);
        uint32_t i;
        cl_fasl_write_u8(w, FASL_TAG_BIGNUM);
        cl_fasl_write_u8(w, (uint8_t)bn->sign);
        cl_fasl_write_u32(w, bn->length);
        for (i = 0; i < bn->length; i++)
            cl_fasl_write_u16(w, bn->limbs[i]);
        return;
    }

    case TYPE_VECTOR: {
        CL_Vector *v = (CL_Vector *)CL_OBJ_TO_PTR(obj);
        CL_Obj *data = cl_vector_data(v);
        uint32_t len = v->length;
        uint32_t i;
        cl_fasl_write_u8(w, FASL_TAG_VECTOR);
        cl_fasl_write_u32(w, len);
        for (i = 0; i < len; i++)
            cl_fasl_serialize_obj(w, data[i]);
        return;
    }

    case TYPE_BIT_VECTOR: {
        CL_BitVector *bv = (CL_BitVector *)CL_OBJ_TO_PTR(obj);
        uint32_t n_words = CL_BV_WORDS(bv->length);
        uint32_t i;
        cl_fasl_write_u8(w, FASL_TAG_BIT_VECTOR);
        cl_fasl_write_u32(w, bv->length);
        for (i = 0; i < n_words; i++)
            cl_fasl_write_u32(w, bv->data[i]);
        return;
    }

    case TYPE_PACKAGE: {
        CL_Package *pkg = (CL_Package *)CL_OBJ_TO_PTR(obj);
        CL_String *pname = (CL_String *)CL_OBJ_TO_PTR(pkg->name);
        cl_fasl_write_u8(w, FASL_TAG_PACKAGE);
        cl_fasl_write_u16(w, (uint16_t)pname->length);
        cl_fasl_write_bytes(w, pname->data, pname->length);
        return;
    }

    case TYPE_PATHNAME: {
        CL_Pathname *pn = (CL_Pathname *)CL_OBJ_TO_PTR(obj);
        cl_fasl_write_u8(w, FASL_TAG_PATHNAME);
        cl_fasl_serialize_obj(w, pn->host);
        cl_fasl_serialize_obj(w, pn->device);
        cl_fasl_serialize_obj(w, pn->directory);
        cl_fasl_serialize_obj(w, pn->name);
        cl_fasl_serialize_obj(w, pn->type);
        cl_fasl_serialize_obj(w, pn->version);
        return;
    }

    default:
        /* Unsupported type — write NIL as fallback */
        cl_fasl_write_u8(w, FASL_TAG_NIL);
        return;
    }
}

/* --- Serialize a CL_Bytecode --- */

void cl_fasl_serialize_bytecode(CL_FaslWriter *w, CL_Obj bc_obj)
{
    CL_Bytecode *bc = (CL_Bytecode *)CL_OBJ_TO_PTR(bc_obj);
    uint16_t i;

    /* Code */
    cl_fasl_write_u32(w, bc->code_len);
    cl_fasl_write_bytes(w, bc->code, bc->code_len);

    /* Constants — deduplicate eq-identical objects (critical for gensym catch tags) */
    cl_fasl_write_u16(w, bc->n_constants);
    for (i = 0; i < bc->n_constants; i++) {
        uint16_t j;
        int found_dup = 0;
        /* Check if this constant is eq to an earlier one */
        if (CL_HEAP_P(bc->constants[i])) {
            for (j = 0; j < i; j++) {
                if (bc->constants[j] == bc->constants[i]) {
                    cl_fasl_write_u8(w, FASL_TAG_CONST_REF);
                    cl_fasl_write_u16(w, j);
                    found_dup = 1;
                    break;
                }
            }
        }
        if (!found_dup)
            cl_fasl_serialize_obj(w, bc->constants[i]);
    }

    /* Metadata */
    cl_fasl_write_u16(w, bc->arity);
    cl_fasl_write_u16(w, bc->n_locals);
    cl_fasl_write_u16(w, bc->n_upvalues);
    cl_fasl_write_u8(w, bc->n_optional);
    cl_fasl_write_u8(w, bc->flags);
    cl_fasl_write_u8(w, bc->n_keys);

    /* Key params */
    for (i = 0; i < bc->n_keys; i++)
        cl_fasl_serialize_obj(w, bc->key_syms[i]);
    for (i = 0; i < bc->n_keys; i++)
        cl_fasl_write_u8(w, bc->key_slots[i]);
    for (i = 0; i < bc->n_keys; i++)
        cl_fasl_write_u8(w, bc->key_suppliedp_slots[i]);

    /* Source info */
    cl_fasl_write_u16(w, bc->source_line);
    if (bc->source_file) {
        uint16_t slen = (uint16_t)strlen(bc->source_file);
        cl_fasl_write_u16(w, slen);
        cl_fasl_write_bytes(w, bc->source_file, slen);
    } else {
        cl_fasl_write_u16(w, 0);
    }

    /* Line map */
    cl_fasl_write_u16(w, bc->line_map_count);
    for (i = 0; i < bc->line_map_count; i++) {
        cl_fasl_write_u16(w, bc->line_map[i].pc);
        cl_fasl_write_u16(w, bc->line_map[i].line);
    }

    /* Name */
    cl_fasl_serialize_obj(w, bc->name);
}

/* ================================================================
 * Reader
 * ================================================================ */

void cl_fasl_reader_init(CL_FaslReader *r, const uint8_t *data, uint32_t size)
{
    r->data = data;
    r->size = size;
    r->pos = 0;
    r->error = FASL_OK;
    r->gensym_count = 0;
}

uint8_t cl_fasl_read_u8(CL_FaslReader *r)
{
    if (r->error) return 0;
    if (r->pos + 1 > r->size) { r->error = FASL_ERR_TRUNCATED; return 0; }
    return r->data[r->pos++];
}

uint16_t cl_fasl_read_u16(CL_FaslReader *r)
{
    uint16_t val;
    if (r->error) return 0;
    if (r->pos + 2 > r->size) { r->error = FASL_ERR_TRUNCATED; return 0; }
    val = (uint16_t)((r->data[r->pos] << 8) | r->data[r->pos + 1]);
    r->pos += 2;
    return val;
}

uint32_t cl_fasl_read_u32(CL_FaslReader *r)
{
    uint32_t val;
    if (r->error) return 0;
    if (r->pos + 4 > r->size) { r->error = FASL_ERR_TRUNCATED; return 0; }
    val = ((uint32_t)r->data[r->pos] << 24) |
          ((uint32_t)r->data[r->pos + 1] << 16) |
          ((uint32_t)r->data[r->pos + 2] << 8) |
          ((uint32_t)r->data[r->pos + 3]);
    r->pos += 4;
    return val;
}

void cl_fasl_read_bytes(CL_FaslReader *r, void *out, uint32_t len)
{
    if (r->error) return;
    if (r->pos + len > r->size) { r->error = FASL_ERR_TRUNCATED; return; }
    memcpy(out, r->data + r->pos, len);
    r->pos += len;
}

uint32_t cl_fasl_read_header(CL_FaslReader *r)
{
    uint32_t magic;
    uint16_t version;

    magic = cl_fasl_read_u32(r);
    if (r->error) return 0;
    if (magic != CL_FASL_MAGIC) { r->error = FASL_ERR_BAD_MAGIC; return 0; }

    version = cl_fasl_read_u16(r);
    if (r->error) return 0;
    if (version != CL_FASL_VERSION) { r->error = FASL_ERR_BAD_VERSION; return 0; }

    cl_fasl_read_u16(r);  /* flags — reserved */
    return cl_fasl_read_u32(r);
}

/* --- Deserialize a CL_Obj constant --- */

CL_Obj cl_fasl_deserialize_obj(CL_FaslReader *r)
{
    uint8_t tag;

    if (r->error) return CL_NIL;

    tag = cl_fasl_read_u8(r);
    if (r->error) return CL_NIL;

    switch (tag) {
    case FASL_TAG_NIL:
        return CL_NIL;

    case FASL_TAG_T:
        return CL_T;

    case FASL_TAG_UNBOUND:
        return CL_UNBOUND;

    case FASL_TAG_FIXNUM:
        return cl_fasl_read_u32(r);

    case FASL_TAG_CHARACTER:
        return cl_fasl_read_u32(r);

    case FASL_TAG_SYMBOL: {
        uint16_t pkg_len = cl_fasl_read_u16(r);
        char pkg_buf[256], sym_buf[256];
        uint16_t sym_len;
        CL_Obj pkg_obj;

        if (r->error) return CL_NIL;

        if (pkg_len == 0) {
            /* Uninterned symbol */
            sym_len = cl_fasl_read_u16(r);
            if (r->error || sym_len >= sizeof(sym_buf)) return CL_NIL;
            cl_fasl_read_bytes(r, sym_buf, sym_len);
            sym_buf[sym_len] = '\0';
            return cl_make_symbol(cl_make_string(sym_buf, sym_len));
        }

        if (pkg_len == 0xFFFF) {
            /* Keyword */
            sym_len = cl_fasl_read_u16(r);
            if (r->error || sym_len >= sizeof(sym_buf)) return CL_NIL;
            cl_fasl_read_bytes(r, sym_buf, sym_len);
            sym_buf[sym_len] = '\0';
            pkg_obj = cl_find_package("KEYWORD", 7);
            return cl_intern_in(sym_buf, sym_len, pkg_obj);
        }

        /* Named package */
        if (pkg_len >= sizeof(pkg_buf)) return CL_NIL;
        cl_fasl_read_bytes(r, pkg_buf, pkg_len);
        pkg_buf[pkg_len] = '\0';

        sym_len = cl_fasl_read_u16(r);
        if (r->error || sym_len >= sizeof(sym_buf)) return CL_NIL;
        cl_fasl_read_bytes(r, sym_buf, sym_len);
        sym_buf[sym_len] = '\0';

        pkg_obj = cl_find_package(pkg_buf, pkg_len);
        if (CL_NULL_P(pkg_obj)) {
            /* Package not found — intern in CL-USER as fallback */
            pkg_obj = cl_find_package("COMMON-LISP-USER", 16);
        }
        return cl_intern_in(sym_buf, sym_len, pkg_obj);
    }

    case FASL_TAG_GENSYM_DEF: {
        uint16_t id = cl_fasl_read_u16(r);
        uint16_t sym_len = cl_fasl_read_u16(r);
        char sym_buf[256];
        CL_Obj sym_obj;
        if (r->error || sym_len >= sizeof(sym_buf)) return CL_NIL;
        cl_fasl_read_bytes(r, sym_buf, sym_len);
        sym_buf[sym_len] = '\0';
        sym_obj = cl_make_symbol(cl_make_string(sym_buf, sym_len));
        if (id < FASL_MAX_GENSYMS) {
            r->gensym_objs[id] = sym_obj;
            if (id >= r->gensym_count)
                r->gensym_count = id + 1;
        }
        return sym_obj;
    }

    case FASL_TAG_GENSYM_REF: {
        uint16_t id = cl_fasl_read_u16(r);
        if (r->error || id >= r->gensym_count) return CL_NIL;
        return r->gensym_objs[id];
    }

    case FASL_TAG_STRING: {
        uint32_t len = cl_fasl_read_u32(r);
        char *tmp;
        CL_Obj result;
        if (r->error) return CL_NIL;
        tmp = (char *)platform_alloc(len + 1);
        if (!tmp) return CL_NIL;
        cl_fasl_read_bytes(r, tmp, len);
        tmp[len] = '\0';
        result = cl_make_string(tmp, len);
        platform_free(tmp);
        return result;
    }

#ifdef CL_WIDE_STRINGS
    case FASL_TAG_WIDE_STRING: {
        uint32_t len = cl_fasl_read_u32(r);
        uint32_t *tmp;
        uint32_t i;
        CL_Obj result;
        if (r->error) return CL_NIL;
        tmp = (uint32_t *)platform_alloc(len * sizeof(uint32_t));
        if (!tmp) return CL_NIL;
        for (i = 0; i < len; i++)
            tmp[i] = cl_fasl_read_u32(r);
        result = cl_make_wide_string(tmp, len);
        platform_free(tmp);
        return result;
    }
#endif

    case FASL_TAG_CONS: {
        CL_Obj car_val, cdr_val;
        car_val = cl_fasl_deserialize_obj(r);
        CL_GC_PROTECT(car_val);
        cdr_val = cl_fasl_deserialize_obj(r);
        CL_GC_PROTECT(cdr_val);
        {
            CL_Obj result = cl_cons(car_val, cdr_val);
            CL_GC_UNPROTECT(2);
            return result;
        }
    }

    case FASL_TAG_BYTECODE:
        return cl_fasl_deserialize_bytecode(r);

    case FASL_TAG_SINGLE_FLOAT: {
        uint32_t bits = cl_fasl_read_u32(r);
        float f;
        memcpy(&f, &bits, 4);
        return cl_make_single_float(f);
    }

    case FASL_TAG_DOUBLE_FLOAT: {
        uint8_t bytes[8];
        double d;
        cl_fasl_read_bytes(r, bytes, 8);
        if (r->error) return CL_NIL;
        /* Convert from big-endian to native */
        {
            union { double d; uint8_t b[8]; } u;
            u.d = 1.0;
            if (u.b[0] == 0x3F) {
                /* Big-endian host */
                memcpy(&d, bytes, 8);
            } else {
                /* Little-endian host: reverse */
                uint8_t rev[8];
                int i;
                for (i = 0; i < 8; i++) rev[i] = bytes[7 - i];
                memcpy(&d, rev, 8);
            }
        }
        return cl_make_double_float(d);
    }

    case FASL_TAG_RATIO: {
        CL_Obj num, den;
        num = cl_fasl_deserialize_obj(r);
        CL_GC_PROTECT(num);
        den = cl_fasl_deserialize_obj(r);
        CL_GC_PROTECT(den);
        {
            CL_Obj result = cl_make_ratio(num, den);
            CL_GC_UNPROTECT(2);
            return result;
        }
    }

    case FASL_TAG_COMPLEX: {
        CL_Obj real, imag;
        real = cl_fasl_deserialize_obj(r);
        CL_GC_PROTECT(real);
        imag = cl_fasl_deserialize_obj(r);
        CL_GC_PROTECT(imag);
        {
            CL_Obj result = cl_make_complex(real, imag);
            CL_GC_UNPROTECT(2);
            return result;
        }
    }

    case FASL_TAG_BIGNUM: {
        uint8_t sign = cl_fasl_read_u8(r);
        uint32_t n_limbs = cl_fasl_read_u32(r);
        CL_Obj result;
        CL_Bignum *bn;
        uint32_t i;
        if (r->error) return CL_NIL;
        result = cl_make_bignum(n_limbs, sign);
        bn = (CL_Bignum *)CL_OBJ_TO_PTR(result);
        for (i = 0; i < n_limbs; i++)
            bn->limbs[i] = cl_fasl_read_u16(r);
        return result;
    }

    case FASL_TAG_VECTOR: {
        uint32_t len = cl_fasl_read_u32(r);
        CL_Obj result;
        CL_Vector *v;
        uint32_t i;
        if (r->error) return CL_NIL;
        result = cl_make_vector(len);
        CL_GC_PROTECT(result);
        v = (CL_Vector *)CL_OBJ_TO_PTR(result);
        for (i = 0; i < len; i++) {
            CL_Obj elt = cl_fasl_deserialize_obj(r);
            /* Re-fetch v since GC may have occurred */
            v = (CL_Vector *)CL_OBJ_TO_PTR(result);
            v->data[i] = elt;
        }
        CL_GC_UNPROTECT(1);
        return result;
    }

    case FASL_TAG_BIT_VECTOR: {
        uint32_t nbits = cl_fasl_read_u32(r);
        uint32_t n_words = CL_BV_WORDS(nbits);
        CL_Obj result;
        CL_BitVector *bv;
        uint32_t i;
        if (r->error) return CL_NIL;
        result = cl_make_bit_vector(nbits);
        bv = (CL_BitVector *)CL_OBJ_TO_PTR(result);
        for (i = 0; i < n_words; i++)
            bv->data[i] = cl_fasl_read_u32(r);
        return result;
    }

    case FASL_TAG_PATHNAME: {
        CL_Obj components[6];
        int i;
        CL_Obj result;
        for (i = 0; i < 6; i++) {
            components[i] = cl_fasl_deserialize_obj(r);
            CL_GC_PROTECT(components[i]);
        }
        result = cl_make_pathname(components[0], components[1],
                                  components[2], components[3],
                                  components[4], components[5]);
        CL_GC_UNPROTECT(6);
        return result;
    }

    case FASL_TAG_CLOSURE: {
        CL_Obj bc_val = cl_fasl_deserialize_obj(r);
        uint16_t n_upvals = cl_fasl_read_u16(r);
        CL_Closure *cl;
        CL_Obj result;
        uint16_t i;
        if (r->error) { return CL_NIL; }
        CL_GC_PROTECT(bc_val);
        cl = (CL_Closure *)cl_alloc(TYPE_CLOSURE,
            sizeof(CL_Closure) + n_upvals * sizeof(CL_Obj));
        if (!cl) { CL_GC_UNPROTECT(1); return CL_NIL; }
        cl->bytecode = bc_val;
        result = CL_PTR_TO_OBJ(cl);
        CL_GC_PROTECT(result);
        for (i = 0; i < n_upvals; i++) {
            CL_Obj uv = cl_fasl_deserialize_obj(r);
            cl = (CL_Closure *)CL_OBJ_TO_PTR(result); /* refresh */
            cl->upvalues[i] = uv;
        }
        CL_GC_UNPROTECT(2);
        return result;
    }

    case FASL_TAG_PACKAGE: {
        uint16_t name_len = cl_fasl_read_u16(r);
        char pkg_buf[256];
        if (r->error || name_len >= sizeof(pkg_buf)) return CL_NIL;
        cl_fasl_read_bytes(r, pkg_buf, name_len);
        pkg_buf[name_len] = '\0';
        return cl_find_package(pkg_buf, name_len);
    }

    case FASL_TAG_FUNCTION: {
        /* C builtin — deserialize name symbol and look up its function */
        CL_Obj name_sym = cl_fasl_deserialize_obj(r);
        if (r->error || CL_NULL_P(name_sym)) return CL_NIL;
        if (CL_HEAP_P(name_sym) &&
            CL_HDR_TYPE(CL_OBJ_TO_PTR(name_sym)) == TYPE_SYMBOL) {
            CL_Symbol *sym = (CL_Symbol *)CL_OBJ_TO_PTR(name_sym);
            if (!CL_NULL_P(sym->function))
                return sym->function;
        }
        return CL_NIL;
    }

    default:
        r->error = FASL_ERR_BAD_TAG;
        return CL_NIL;
    }
}

/* --- Deserialize a CL_Bytecode --- */

CL_Obj cl_fasl_deserialize_bytecode(CL_FaslReader *r)
{
    CL_Bytecode *bc;
    CL_Obj bc_obj;
    uint32_t code_len;
    uint16_t n_consts, i;
    uint16_t source_file_len;

    if (r->error) return CL_NIL;

    /* Allocate bytecode on heap */
    bc = (CL_Bytecode *)cl_alloc(TYPE_BYTECODE, sizeof(CL_Bytecode));
    if (!bc) return CL_NIL;
    bc_obj = CL_PTR_TO_OBJ(bc);
    CL_GC_PROTECT(bc_obj);

    /* Zero out all fields */
    bc->code = NULL;
    bc->constants = NULL;
    bc->code_len = 0;
    bc->n_constants = 0;
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
    bc->source_line = 0;
    bc->source_file = NULL;

    /* Code */
    code_len = cl_fasl_read_u32(r);
    if (r->error) { CL_GC_UNPROTECT(1); return CL_NIL; }

    bc = (CL_Bytecode *)CL_OBJ_TO_PTR(bc_obj); /* refresh after potential GC */
    bc->code = (uint8_t *)platform_alloc(code_len);
    if (!bc->code) { CL_GC_UNPROTECT(1); return CL_NIL; }
    cl_fasl_read_bytes(r, bc->code, code_len);
    bc->code_len = code_len;

    /* Constants */
    n_consts = cl_fasl_read_u16(r);
    if (r->error) { CL_GC_UNPROTECT(1); return CL_NIL; }

    bc = (CL_Bytecode *)CL_OBJ_TO_PTR(bc_obj);
    if (n_consts > 0) {
        bc->constants = (CL_Obj *)platform_alloc(n_consts * sizeof(CL_Obj));
        if (!bc->constants) { CL_GC_UNPROTECT(1); return CL_NIL; }
        bc->n_constants = n_consts;
        /* Initialize to NIL so GC doesn't see garbage */
        for (i = 0; i < n_consts; i++)
            bc->constants[i] = CL_NIL;
    }

    for (i = 0; i < n_consts; i++) {
        CL_Obj val;
        /* Check for back-reference to earlier constant (dedup) */
        if (!r->error && r->pos < r->size &&
            r->data[r->pos] == FASL_TAG_CONST_REF) {
            uint16_t ref_idx;
            cl_fasl_read_u8(r);  /* consume tag */
            ref_idx = cl_fasl_read_u16(r);
            if (r->error) { CL_GC_UNPROTECT(1); return CL_NIL; }
            bc = (CL_Bytecode *)CL_OBJ_TO_PTR(bc_obj);
            bc->constants[i] = bc->constants[ref_idx];
            continue;
        }
        val = cl_fasl_deserialize_obj(r);
        if (r->error) { CL_GC_UNPROTECT(1); return CL_NIL; }
        bc = (CL_Bytecode *)CL_OBJ_TO_PTR(bc_obj); /* refresh */
        bc->constants[i] = val;
    }

    /* Metadata */
    bc = (CL_Bytecode *)CL_OBJ_TO_PTR(bc_obj);
    bc->arity = cl_fasl_read_u16(r);
    bc->n_locals = cl_fasl_read_u16(r);
    bc->n_upvalues = cl_fasl_read_u16(r);
    bc->n_optional = cl_fasl_read_u8(r);
    bc->flags = cl_fasl_read_u8(r);
    bc->n_keys = cl_fasl_read_u8(r);

    /* Key params */
    if (bc->n_keys > 0) {
        uint8_t nk = bc->n_keys;
        bc->key_syms = (CL_Obj *)platform_alloc(nk * sizeof(CL_Obj));
        bc->key_slots = (uint8_t *)platform_alloc(nk);
        bc->key_suppliedp_slots = (uint8_t *)platform_alloc(nk);
        if (!bc->key_syms || !bc->key_slots || !bc->key_suppliedp_slots) {
            CL_GC_UNPROTECT(1);
            return CL_NIL;
        }
        /* Initialize key_syms to NIL */
        for (i = 0; i < nk; i++)
            bc->key_syms[i] = CL_NIL;

        for (i = 0; i < nk; i++) {
            CL_Obj ksym = cl_fasl_deserialize_obj(r);
            bc = (CL_Bytecode *)CL_OBJ_TO_PTR(bc_obj);
            bc->key_syms[i] = ksym;
        }
        bc = (CL_Bytecode *)CL_OBJ_TO_PTR(bc_obj);
        for (i = 0; i < nk; i++)
            bc->key_slots[i] = cl_fasl_read_u8(r);
        for (i = 0; i < nk; i++)
            bc->key_suppliedp_slots[i] = cl_fasl_read_u8(r);
    }

    /* Source info */
    bc = (CL_Bytecode *)CL_OBJ_TO_PTR(bc_obj);
    bc->source_line = cl_fasl_read_u16(r);
    source_file_len = cl_fasl_read_u16(r);
    if (source_file_len > 0) {
        char *sf = (char *)platform_alloc(source_file_len + 1);
        if (sf) {
            cl_fasl_read_bytes(r, sf, source_file_len);
            sf[source_file_len] = '\0';
            bc = (CL_Bytecode *)CL_OBJ_TO_PTR(bc_obj);
            bc->source_file = sf;
        }
    }

    /* Line map */
    {
        uint16_t lm_count;
        bc = (CL_Bytecode *)CL_OBJ_TO_PTR(bc_obj);
        lm_count = cl_fasl_read_u16(r);
        if (lm_count > 0) {
            bc->line_map = (CL_LineEntry *)platform_alloc(
                lm_count * sizeof(CL_LineEntry));
            bc->line_map_count = lm_count;
            if (bc->line_map) {
                for (i = 0; i < lm_count; i++) {
                    bc->line_map[i].pc = cl_fasl_read_u16(r);
                    bc->line_map[i].line = cl_fasl_read_u16(r);
                }
            }
        }
    }

    /* Name */
    {
        CL_Obj name = cl_fasl_deserialize_obj(r);
        bc = (CL_Bytecode *)CL_OBJ_TO_PTR(bc_obj);
        bc->name = name;
    }

    CL_GC_UNPROTECT(1);
    return bc_obj;
}

/* ================================================================
 * High-level FASL load
 * ================================================================ */

CL_Obj cl_fasl_load(const uint8_t *data, uint32_t size)
{
    CL_FaslReader r;
    uint32_t n_units, i;

    cl_fasl_reader_init(&r, data, size);
    n_units = cl_fasl_read_header(&r);
    if (r.error) {
        switch (r.error) {
        case FASL_ERR_BAD_MAGIC:
            cl_error(CL_ERR_GENERAL, "FASL: invalid magic number");
            break;
        case FASL_ERR_BAD_VERSION:
            cl_error(CL_ERR_GENERAL, "FASL: unsupported version");
            break;
        default:
            cl_error(CL_ERR_GENERAL, "FASL: header read error");
            break;
        }
        return CL_NIL;
    }

    for (i = 0; i < n_units; i++) {
        CL_Obj bc_obj;
        cl_fasl_read_u32(&r); /* skip unit length */

        if (r.error) {
            cl_error(CL_ERR_GENERAL, "FASL: truncated unit header");
            return CL_NIL;
        }

        bc_obj = cl_fasl_deserialize_bytecode(&r);
        if (r.error) {
            cl_error(CL_ERR_GENERAL, "FASL: error deserializing unit");
            return CL_NIL;
        }

        if (!CL_NULL_P(bc_obj))
            cl_vm_eval(bc_obj);
    }

    return CL_T;
}
