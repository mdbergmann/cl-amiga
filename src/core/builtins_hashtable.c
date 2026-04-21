#include "builtins.h"
#include "bignum.h"
#include "float.h"
#include "ratio.h"
#include "symbol.h"
#include "package.h"
#include "mem.h"
#include "error.h"
#include "vm.h"
#include "string_utils.h"
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

/* --- Case-insensitive string hash for equalp --- */

static uint32_t hash_string_ci(const char *str, uint32_t len)
{
    uint32_t hash = 0;
    uint32_t i;
    for (i = 0; i < len; i++) {
        char ch = str[i];
        if (ch >= 'A' && ch <= 'Z') ch += 32;
        hash = ((hash << 5) | (hash >> 27)) ^ (uint8_t)ch;
    }
    return hash;
}

/* --- Bit mixer for identity/fixnum hashing --- */

static inline uint32_t hash_mix(uint32_t h)
{
    h ^= h >> 16;
    h *= 0x45d9f3bU;
    h ^= h >> 16;
    return h;
}

/* --- Bucket accessor: returns pointer to bucket array --- */

static inline CL_Obj *ht_get_buckets(CL_Hashtable *ht)
{
    if (!CL_NULL_P(ht->bucket_vec)) {
        CL_Vector *v = (CL_Vector *)CL_OBJ_TO_PTR(ht->bucket_vec);
        return v->data;
    }
    return ht->buckets;
}

/* --- Hash function for CL objects --- */

static uint32_t hash_obj(CL_Obj obj, uint32_t test)
{
    /* For eq: hash by identity */
    if (test == CL_HT_TEST_EQ)
        return hash_mix(obj);

    /* For eql: bignums, ratios, floats, and complex need value-based hash */
    if (test == CL_HT_TEST_EQL) {
        if (CL_BIGNUM_P(obj)) return cl_bignum_hash(obj);
        if (CL_RATIO_P(obj)) return cl_ratio_hash(obj);
        if (CL_COMPLEX_P(obj)) {
            CL_Complex *cx = (CL_Complex *)CL_OBJ_TO_PTR(obj);
            return hash_obj(cx->realpart, CL_HT_TEST_EQL) * 31 +
                   hash_obj(cx->imagpart, CL_HT_TEST_EQL);
        }
        if (CL_SINGLE_FLOAT_P(obj)) {
            union { float f; uint32_t u; } conv;
            conv.f = ((CL_SingleFloat *)CL_OBJ_TO_PTR(obj))->value;
            return conv.u * 2654435761u;
        }
        if (CL_DOUBLE_FLOAT_P(obj)) {
            union { double d; uint32_t u[2]; } conv;
            conv.d = ((CL_DoubleFloat *)CL_OBJ_TO_PTR(obj))->value;
            return (conv.u[0] ^ conv.u[1]) * 2654435761u;
        }
        return hash_mix(obj);
    }

    /* For equal/equalp: structural hash */
    if (CL_NULL_P(obj)) return 0;
    if (CL_FIXNUM_P(obj)) return hash_mix(obj);
    if (CL_CHAR_P(obj)) {
        if (test == CL_HT_TEST_EQUALP) {
            /* Case-insensitive char hash */
            char ch = (char)CL_CHAR_VAL(obj);
            if (ch >= 'A' && ch <= 'Z') ch += 32;
            return (uint32_t)CL_MAKE_CHAR(ch);
        }
        return (uint32_t)obj;
    }
    if (CL_BIGNUM_P(obj)) return cl_bignum_hash(obj);
    if (CL_RATIO_P(obj)) return cl_ratio_hash(obj);
    if (CL_COMPLEX_P(obj)) {
        CL_Complex *cx = (CL_Complex *)CL_OBJ_TO_PTR(obj);
        return hash_obj(cx->realpart, test) * 31 +
               hash_obj(cx->imagpart, test);
    }
    if (CL_SINGLE_FLOAT_P(obj)) {
        union { float f; uint32_t u; } conv;
        conv.f = ((CL_SingleFloat *)CL_OBJ_TO_PTR(obj))->value;
        return conv.u * 2654435761u;
    }
    if (CL_DOUBLE_FLOAT_P(obj)) {
        union { double d; uint32_t u[2]; } conv;
        conv.d = ((CL_DoubleFloat *)CL_OBJ_TO_PTR(obj))->value;
        return (conv.u[0] ^ conv.u[1]) * 2654435761u;
    }

    if (CL_HEAP_P(obj)) {
        uint8_t type = CL_HDR_TYPE(CL_OBJ_TO_PTR(obj));
        if (type == TYPE_STRING) {
            CL_String *s = (CL_String *)CL_OBJ_TO_PTR(obj);
            if (test == CL_HT_TEST_EQUALP)
                return hash_string_ci(s->data, s->length);
            return cl_hash_string(s->data, s->length);
        }
        if (type == TYPE_SYMBOL)
            return hash_mix(obj);
        if (type == TYPE_CONS) {
            /* Hash first element only (avoid deep recursion) */
            return hash_obj(cl_car(obj), test) * 31 + 1;
        }
    }

    /* Fallback: identity hash */
    return hash_mix(obj);
}

/* --- Key comparison --- */

/* Forward declaration of bi_equal from builtins.c */
static int keys_equal(CL_Obj a, CL_Obj b, uint32_t test)
{
    if (test == CL_HT_TEST_EQ)
        return a == b;

    if (test == CL_HT_TEST_EQL) {
        if (a == b) return 1;
        /* Value equality for bignums */
        if (CL_BIGNUM_P(a) && CL_BIGNUM_P(b))
            return cl_bignum_equal(a, b);
        if (CL_RATIO_P(a) && CL_RATIO_P(b))
            return cl_ratio_equal(a, b);
        if (CL_COMPLEX_P(a) && CL_COMPLEX_P(b)) {
            CL_Complex *ca_cx = (CL_Complex *)CL_OBJ_TO_PTR(a);
            CL_Complex *cb_cx = (CL_Complex *)CL_OBJ_TO_PTR(b);
            return keys_equal(ca_cx->realpart, cb_cx->realpart, CL_HT_TEST_EQL) &&
                   keys_equal(ca_cx->imagpart, cb_cx->imagpart, CL_HT_TEST_EQL);
        }
        /* Value equality for floats (same type required for eql) */
        if (CL_SINGLE_FLOAT_P(a) && CL_SINGLE_FLOAT_P(b))
            return ((CL_SingleFloat *)CL_OBJ_TO_PTR(a))->value ==
                   ((CL_SingleFloat *)CL_OBJ_TO_PTR(b))->value;
        if (CL_DOUBLE_FLOAT_P(a) && CL_DOUBLE_FLOAT_P(b))
            return ((CL_DoubleFloat *)CL_OBJ_TO_PTR(a))->value ==
                   ((CL_DoubleFloat *)CL_OBJ_TO_PTR(b))->value;
        return 0;
    }

    /* CL_HT_TEST_EQUAL or CL_HT_TEST_EQUALP: structural equality */
    if (a == b) return 1;
    if (CL_CHAR_P(a) && CL_CHAR_P(b)) {
        if (test == CL_HT_TEST_EQUALP) {
            char ca = (char)CL_CHAR_VAL(a);
            char cb = (char)CL_CHAR_VAL(b);
            if (ca >= 'A' && ca <= 'Z') ca += 32;
            if (cb >= 'A' && cb <= 'Z') cb += 32;
            return ca == cb;
        }
        return a == b;
    }
    if (CL_FIXNUM_P(a) || CL_CHAR_P(a)) return a == b;
    if (CL_BIGNUM_P(a) && CL_BIGNUM_P(b))
        return cl_bignum_equal(a, b);
    if (CL_RATIO_P(a) && CL_RATIO_P(b))
        return cl_ratio_equal(a, b);
    if (CL_COMPLEX_P(a) && CL_COMPLEX_P(b)) {
        CL_Complex *ca_cx = (CL_Complex *)CL_OBJ_TO_PTR(a);
        CL_Complex *cb_cx = (CL_Complex *)CL_OBJ_TO_PTR(b);
        return keys_equal(ca_cx->realpart, cb_cx->realpart, test) &&
               keys_equal(ca_cx->imagpart, cb_cx->imagpart, test);
    }
    /* Floats: equal is same as eql (same type, same value) */
    if (CL_SINGLE_FLOAT_P(a) && CL_SINGLE_FLOAT_P(b))
        return ((CL_SingleFloat *)CL_OBJ_TO_PTR(a))->value ==
               ((CL_SingleFloat *)CL_OBJ_TO_PTR(b))->value;
    if (CL_DOUBLE_FLOAT_P(a) && CL_DOUBLE_FLOAT_P(b))
        return ((CL_DoubleFloat *)CL_OBJ_TO_PTR(a))->value ==
               ((CL_DoubleFloat *)CL_OBJ_TO_PTR(b))->value;

    if (CL_ANY_STRING_P(a) && CL_ANY_STRING_P(b)) {
        uint32_t la = cl_string_length(a), lb = cl_string_length(b);
        uint32_t i;
        if (la != lb) return 0;
        if (test == CL_HT_TEST_EQUALP) {
            for (i = 0; i < la; i++) {
                int ca = cl_string_char_at(a, i), cb = cl_string_char_at(b, i);
                if (ca >= 'A' && ca <= 'Z') ca += 32;
                if (cb >= 'A' && cb <= 'Z') cb += 32;
                if (ca != cb) return 0;
            }
            return 1;
        }
        for (i = 0; i < la; i++) {
            if (cl_string_char_at(a, i) != cl_string_char_at(b, i)) return 0;
        }
        return 1;
    }

    if (CL_CONS_P(a) && CL_CONS_P(b)) {
        return keys_equal(cl_car(a), cl_car(b), test) &&
               keys_equal(cl_cdr(a), cl_cdr(b), test);
    }

    if (CL_VECTOR_P(a) && CL_VECTOR_P(b)) {
        CL_Vector *va = (CL_Vector *)CL_OBJ_TO_PTR(a);
        CL_Vector *vb = (CL_Vector *)CL_OBJ_TO_PTR(b);
        uint32_t i;
        if (va->length != vb->length) return 0;
        for (i = 0; i < va->length; i++) {
            if (!keys_equal(va->data[i], vb->data[i], test)) return 0;
        }
        return 1;
    }

    return 0;
}

/* Default bucket count */
#define CL_HT_DEFAULT_BUCKETS 16

/* --- Keyword symbols for :test and :size --- */
static CL_Obj KW_TEST = CL_NIL;
static CL_Obj KW_SIZE = CL_NIL;
static CL_Obj SYM_EQ_HT = CL_NIL;
static CL_Obj SYM_EQL_HT = CL_NIL;
static CL_Obj SYM_EQUAL_HT = CL_NIL;
static CL_Obj SYM_EQUALP_HT = CL_NIL;

/* --- Rehashing --- */

/* Grow the hash table when load factor > 75%.
 * Allocates a new CL_Vector for buckets, redistributes entries by relinking
 * existing cons cells (no new allocations during redistribution). */
static void ht_maybe_rehash(CL_Obj ht_obj)
{
    CL_Hashtable *ht = (CL_Hashtable *)CL_OBJ_TO_PTR(ht_obj);
    uint32_t old_count = ht->bucket_count;
    uint32_t new_count, i;
    CL_Obj new_vec;
    CL_Obj *old_bkts, *new_bkts;

    /* Check load factor: rehash if count > bucket_count * 3/4 */
    if (ht->count <= (old_count * 3) / 4)
        return;

    new_count = old_count * 2;

    /* Allocate new bucket vector — GC may fire */
    CL_GC_PROTECT(ht_obj);
    new_vec = cl_make_vector(new_count);
    CL_GC_UNPROTECT(1);

    if (CL_NULL_P(new_vec)) return;  /* allocation failed, skip rehash */

    /* Re-dereference after potential GC */
    ht = (CL_Hashtable *)CL_OBJ_TO_PTR(ht_obj);
    old_bkts = ht_get_buckets(ht);
    new_bkts = ((CL_Vector *)CL_OBJ_TO_PTR(new_vec))->data;

    /* Redistribute all entries — no allocations, just relinking cons cells */
    for (i = 0; i < old_count; i++) {
        CL_Obj chain = old_bkts[i];
        while (!CL_NULL_P(chain)) {
            CL_Obj next = cl_cdr(chain);
            CL_Obj pair = cl_car(chain);
            CL_Obj key = cl_car(pair);
            uint32_t new_idx = hash_obj(key, ht->test) & (new_count - 1);
            /* Splice this chain cell into the new bucket */
            ((CL_Cons *)CL_OBJ_TO_PTR(chain))->cdr = new_bkts[new_idx];
            new_bkts[new_idx] = chain;
            chain = next;
        }
    }

    /* Switch to new bucket storage */
    ht->bucket_vec = new_vec;
    ht->bucket_count = new_count;
}

/* --- Builtins --- */

static CL_Obj bi_make_hash_table(CL_Obj *args, int n)
{
    uint32_t test = CL_HT_TEST_EQL;  /* Default test is EQL */
    uint32_t size = CL_HT_DEFAULT_BUCKETS;
    int i;

    /* Parse keyword arguments */
    for (i = 0; i + 1 < n; i += 2) {
        if (args[i] == KW_TEST) {
            CL_Obj test_fn = args[i + 1];
            /* Compare by symbol name to handle cross-package references */
            if (CL_SYMBOL_P(test_fn)) {
                const char *name = cl_symbol_name(test_fn);
                if (strcmp(name, "EQ") == 0)
                    test = CL_HT_TEST_EQ;
                else if (strcmp(name, "EQL") == 0)
                    test = CL_HT_TEST_EQL;
                else if (strcmp(name, "EQUAL") == 0)
                    test = CL_HT_TEST_EQUAL;
                else if (strcmp(name, "EQUALP") == 0)
                    test = CL_HT_TEST_EQUALP;
                else
                    cl_error(CL_ERR_ARGS, "MAKE-HASH-TABLE: :test must be EQ, EQL, EQUAL, or EQUALP, got %s", name);
            } else if (CL_HEAP_P(test_fn) && CL_HDR_TYPE(CL_OBJ_TO_PTR(test_fn)) == TYPE_FUNCTION) {
                /* #'eq, #'eql, #'equal, #'equalp — check function name */
                CL_Function *fn = (CL_Function *)CL_OBJ_TO_PTR(test_fn);
                if (CL_SYMBOL_P(fn->name)) {
                    const char *name = cl_symbol_name(fn->name);
                    if (strcmp(name, "EQ") == 0)
                        test = CL_HT_TEST_EQ;
                    else if (strcmp(name, "EQL") == 0)
                        test = CL_HT_TEST_EQL;
                    else if (strcmp(name, "EQUAL") == 0)
                        test = CL_HT_TEST_EQUAL;
                    else if (strcmp(name, "EQUALP") == 0)
                        test = CL_HT_TEST_EQUALP;
                    else
                        cl_error(CL_ERR_ARGS, "MAKE-HASH-TABLE: :test must be EQ, EQL, EQUAL, or EQUALP");
                } else {
                    cl_error(CL_ERR_ARGS, "MAKE-HASH-TABLE: :test must be EQ, EQL, EQUAL, or EQUALP");
                }
            } else {
                cl_error(CL_ERR_ARGS, "MAKE-HASH-TABLE: :test must be EQ, EQL, EQUAL, or EQUALP");
            }
        } else if (args[i] == KW_SIZE) {
            uint32_t requested;
            if (!CL_FIXNUM_P(args[i + 1]))
                cl_error(CL_ERR_TYPE, "MAKE-HASH-TABLE: :size must be a number");
            requested = (uint32_t)CL_FIXNUM_VAL(args[i + 1]);
            if (requested < 1) requested = 1;
            /* :size is expected entry count; set bucket_count = ceil(size / 0.75)
             * so we don't immediately rehash after filling to :size entries */
            size = (requested * 4 + 2) / 3;
        }
    }

    return cl_make_hashtable(size, test);
}

static CL_Obj bi_gethash(CL_Obj *args, int n)
{
    CL_Obj key = args[0];
    CL_Obj ht_obj = args[1];
    CL_Obj default_val = (n >= 3) ? args[2] : CL_NIL;
    CL_Hashtable *ht;
    uint32_t bucket_idx;
    CL_Obj chain;

    if (!CL_HASHTABLE_P(ht_obj))
        cl_error(CL_ERR_TYPE, "GETHASH: not a hash table");

    ht = (CL_Hashtable *)CL_OBJ_TO_PTR(ht_obj);
    {
        CL_Obj *bkts = ht_get_buckets(ht);
        bucket_idx = hash_obj(key, ht->test) & (ht->bucket_count - 1);
        chain = bkts[bucket_idx];
    }

    while (!CL_NULL_P(chain)) {
        CL_Obj pair = cl_car(chain);
        if (keys_equal(cl_car(pair), key, ht->test)) {
            /* Found — return value, T as second value */
            cl_mv_count = 2;
            cl_mv_values[0] = cl_cdr(pair);
            cl_mv_values[1] = SYM_T;
            return cl_cdr(pair);
        }
        chain = cl_cdr(chain);
    }

    /* Not found — return default, NIL as second value */
    cl_mv_count = 2;
    cl_mv_values[0] = default_val;
    cl_mv_values[1] = CL_NIL;
    return default_val;
}

static CL_Obj bi_setf_gethash(CL_Obj *args, int n)
{
    /* (%SETF-GETHASH key hash-table value) — set and return value */
    CL_Obj key = args[0];
    CL_Obj ht_obj = args[1];
    CL_Obj value = args[2];
    CL_Hashtable *ht;
    uint32_t bucket_idx;
    CL_Obj chain;
    CL_UNUSED(n);

    if (!CL_HASHTABLE_P(ht_obj))
        cl_error(CL_ERR_TYPE, "(SETF GETHASH): not a hash table");

    ht = (CL_Hashtable *)CL_OBJ_TO_PTR(ht_obj);
    {
        CL_Obj *bkts = ht_get_buckets(ht);
        bucket_idx = hash_obj(key, ht->test) & (ht->bucket_count - 1);
        chain = bkts[bucket_idx];
    }

    /* Check if key already exists */
    {
        CL_Obj cursor = chain;
        while (!CL_NULL_P(cursor)) {
            CL_Obj pair = cl_car(cursor);
            if (keys_equal(cl_car(pair), key, ht->test)) {
                /* Update existing entry */
                ((CL_Cons *)CL_OBJ_TO_PTR(pair))->cdr = value;
                return value;
            }
            cursor = cl_cdr(cursor);
        }
    }

    /* New entry: cons (key . value) and prepend to bucket */
    {
        CL_Obj pair;
        CL_Obj new_chain;

        CL_GC_PROTECT(key);
        CL_GC_PROTECT(ht_obj);
        CL_GC_PROTECT(value);
        CL_GC_PROTECT(chain);

        pair = cl_cons(key, value);
        CL_GC_PROTECT(pair);

        new_chain = cl_cons(pair, chain);
        CL_GC_UNPROTECT(5);

        /* Re-dereference ht_obj after potential GC */
        ht = (CL_Hashtable *)CL_OBJ_TO_PTR(ht_obj);
        ht_get_buckets(ht)[bucket_idx] = new_chain;
        ht->count++;
    }

    /* Rehash if load factor exceeded */
    ht_maybe_rehash(ht_obj);

    return value;
}

static CL_Obj bi_remhash(CL_Obj *args, int n)
{
    CL_Obj key = args[0];
    CL_Obj ht_obj = args[1];
    CL_Hashtable *ht;
    uint32_t bucket_idx;
    CL_Obj prev, cursor;
    CL_UNUSED(n);

    if (!CL_HASHTABLE_P(ht_obj))
        cl_error(CL_ERR_TYPE, "REMHASH: not a hash table");

    ht = (CL_Hashtable *)CL_OBJ_TO_PTR(ht_obj);
    {
        CL_Obj *bkts = ht_get_buckets(ht);
        bucket_idx = hash_obj(key, ht->test) & (ht->bucket_count - 1);
        cursor = bkts[bucket_idx];
    }

    prev = CL_NIL;

    while (!CL_NULL_P(cursor)) {
        CL_Obj pair = cl_car(cursor);
        if (keys_equal(cl_car(pair), key, ht->test)) {
            /* Remove from chain */
            if (CL_NULL_P(prev)) {
                ht_get_buckets(ht)[bucket_idx] = cl_cdr(cursor);
            } else {
                ((CL_Cons *)CL_OBJ_TO_PTR(prev))->cdr = cl_cdr(cursor);
            }
            ht->count--;
            return SYM_T;
        }
        prev = cursor;
        cursor = cl_cdr(cursor);
    }

    return CL_NIL;
}

static CL_Obj bi_maphash(CL_Obj *args, int n)
{
    CL_Obj func = cl_coerce_funcdesig(args[0], "MAPHASH");
    CL_Obj ht_obj = args[1];
    CL_Hashtable *ht;
    uint32_t i;
    CL_UNUSED(n);

    if (!CL_HASHTABLE_P(ht_obj))
        cl_error(CL_ERR_TYPE, "MAPHASH: not a hash table");

    ht = (CL_Hashtable *)CL_OBJ_TO_PTR(ht_obj);

    CL_GC_PROTECT(func);
    CL_GC_PROTECT(ht_obj);

    for (i = 0; i < ht->bucket_count; i++) {
        CL_Obj chain = ht_get_buckets(ht)[i];
        while (!CL_NULL_P(chain)) {
            CL_Obj pair = cl_car(chain);
            CL_Obj call_args[2];
            call_args[0] = cl_car(pair);
            call_args[1] = cl_cdr(pair);

            if (CL_FUNCTION_P(func)) {
                CL_Function *f = (CL_Function *)CL_OBJ_TO_PTR(func);
                f->func(call_args, 2);
            } else if (CL_BYTECODE_P(func) || CL_CLOSURE_P(func)) {
                cl_vm_apply(func, call_args, 2);
            } else {
                cl_error(CL_ERR_TYPE, "MAPHASH: not a function");
            }

            /* Re-dereference after potential GC from function call */
            ht = (CL_Hashtable *)CL_OBJ_TO_PTR(ht_obj);
            chain = cl_cdr(chain);
        }
    }

    CL_GC_UNPROTECT(2);
    return CL_NIL;
}

static CL_Obj bi_clrhash(CL_Obj *args, int n)
{
    CL_Hashtable *ht;
    uint32_t i;
    CL_UNUSED(n);

    if (!CL_HASHTABLE_P(args[0]))
        cl_error(CL_ERR_TYPE, "CLRHASH: not a hash table");

    ht = (CL_Hashtable *)CL_OBJ_TO_PTR(args[0]);
    {
        CL_Obj *bkts = ht_get_buckets(ht);
        for (i = 0; i < ht->bucket_count; i++)
            bkts[i] = CL_NIL;
    }
    ht->count = 0;
    return args[0];
}

static CL_Obj bi_hash_table_count(CL_Obj *args, int n)
{
    CL_Hashtable *ht;
    CL_UNUSED(n);

    if (!CL_HASHTABLE_P(args[0]))
        cl_error(CL_ERR_TYPE, "HASH-TABLE-COUNT: not a hash table");

    ht = (CL_Hashtable *)CL_OBJ_TO_PTR(args[0]);
    return CL_MAKE_FIXNUM(ht->count);
}

static CL_Obj bi_hash_table_p(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    return CL_HASHTABLE_P(args[0]) ? SYM_T : CL_NIL;
}

/* CLHS: HASH-TABLE-SIZE — the "current size", i.e. the bucket capacity
   that the table was allocated with.  We return bucket_count, which is
   what we internally track. */
static CL_Obj bi_hash_table_size(CL_Obj *args, int n)
{
    CL_Hashtable *ht;
    CL_UNUSED(n);
    if (!CL_HASHTABLE_P(args[0]))
        cl_error(CL_ERR_TYPE, "HASH-TABLE-SIZE: not a hash table");
    ht = (CL_Hashtable *)CL_OBJ_TO_PTR(args[0]);
    return CL_MAKE_FIXNUM((int32_t)ht->bucket_count);
}

/* CLHS: HASH-TABLE-TEST — returns the test as a symbol for the four
   standard tests (eq/eql/equal/equalp). */
static CL_Obj bi_hash_table_test(CL_Obj *args, int n)
{
    CL_Hashtable *ht;
    CL_UNUSED(n);
    if (!CL_HASHTABLE_P(args[0]))
        cl_error(CL_ERR_TYPE, "HASH-TABLE-TEST: not a hash table");
    ht = (CL_Hashtable *)CL_OBJ_TO_PTR(args[0]);
    switch (ht->test) {
    case CL_HT_TEST_EQ:     return SYM_EQ_HT;
    case CL_HT_TEST_EQL:    return SYM_EQL_HT;
    case CL_HT_TEST_EQUAL:  return SYM_EQUAL_HT;
    case CL_HT_TEST_EQUALP: return SYM_EQUALP_HT;
    default:                return SYM_EQL_HT;
    }
}

/* CLHS: HASH-TABLE-REHASH-SIZE — positive number, how the table grows.
   We don't store this per-table; return a reasonable default (1.5). */
static CL_Obj bi_hash_table_rehash_size(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    if (!CL_HASHTABLE_P(args[0]))
        cl_error(CL_ERR_TYPE, "HASH-TABLE-REHASH-SIZE: not a hash table");
    return cl_make_single_float(1.5f);
}

/* CLHS: HASH-TABLE-REHASH-THRESHOLD — load factor, 0 < x <= 1.
   We don't store this per-table; return 0.75 which matches the load
   factor used internally by maybe_rehash. */
static CL_Obj bi_hash_table_rehash_threshold(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    if (!CL_HASHTABLE_P(args[0]))
        cl_error(CL_ERR_TYPE, "HASH-TABLE-REHASH-THRESHOLD: not a hash table");
    return cl_make_single_float(0.75f);
}

static CL_Obj bi_hash_table_pairs(CL_Obj *args, int n)
{
    CL_Obj ht_obj = args[0];
    CL_Hashtable *ht;
    CL_Obj result = CL_NIL;
    uint32_t i;
    CL_UNUSED(n);

    if (!CL_HASHTABLE_P(ht_obj))
        cl_error(CL_ERR_TYPE, "%HASH-TABLE-PAIRS: not a hash table");

    CL_GC_PROTECT(ht_obj);
    CL_GC_PROTECT(result);

    ht = (CL_Hashtable *)CL_OBJ_TO_PTR(ht_obj);
    for (i = 0; i < ht->bucket_count; i++) {
        CL_Obj chain = ht_get_buckets(ht)[i];
        while (!CL_NULL_P(chain)) {
            CL_Obj pair = cl_car(chain);
            result = cl_cons(pair, result);
            /* Re-dereference after potential GC from cl_cons */
            ht = (CL_Hashtable *)CL_OBJ_TO_PTR(ht_obj);
            chain = cl_cdr(chain);
        }
    }

    CL_GC_UNPROTECT(2);
    return result;
}

/* --- sxhash --- */

static CL_Obj bi_sxhash(CL_Obj *args, int n)
{
    (void)n;
    uint32_t h = hash_obj(args[0], CL_HT_TEST_EQUAL);
    /* Return as non-negative fixnum (mask to 30 bits) */
    return CL_MAKE_FIXNUM((int32_t)(h & 0x3FFFFFFFu));
}

/* --- Registration --- */

void cl_builtins_hashtable_init(void)
{
    /* Intern keyword/symbols used by make-hash-table */
    KW_TEST     = cl_intern_keyword("TEST", 4);
    KW_SIZE     = cl_intern_keyword("SIZE", 4);
    SYM_EQ_HT   = cl_intern_in("EQ", 2, cl_package_cl);
    SYM_EQL_HT  = cl_intern_in("EQL", 3, cl_package_cl);
    SYM_EQUAL_HT = cl_intern_in("EQUAL", 5, cl_package_cl);
    SYM_EQUALP_HT = cl_intern_in("EQUALP", 6, cl_package_cl);

    defun("MAKE-HASH-TABLE", bi_make_hash_table, 0, -1);
    defun("GETHASH", bi_gethash, 2, 3);
    defun("REMHASH", bi_remhash, 2, 2);
    defun("MAPHASH", bi_maphash, 2, 2);
    defun("CLRHASH", bi_clrhash, 1, 1);
    defun("HASH-TABLE-COUNT", bi_hash_table_count, 1, 1);
    defun("HASH-TABLE-P", bi_hash_table_p, 1, 1);
    defun("HASH-TABLE-SIZE", bi_hash_table_size, 1, 1);
    defun("HASH-TABLE-TEST", bi_hash_table_test, 1, 1);
    defun("HASH-TABLE-REHASH-SIZE", bi_hash_table_rehash_size, 1, 1);
    defun("HASH-TABLE-REHASH-THRESHOLD", bi_hash_table_rehash_threshold, 1, 1);
    cl_register_builtin("%SETF-GETHASH", bi_setf_gethash, 3, 3, cl_package_clamiga);
    cl_register_builtin("%HASH-TABLE-PAIRS", bi_hash_table_pairs, 1, 1, cl_package_clamiga);
    defun("SXHASH", bi_sxhash, 1, 1);

    /* Register cached symbols for GC compaction forwarding */
    cl_gc_register_root(&KW_TEST);
    cl_gc_register_root(&KW_SIZE);
    cl_gc_register_root(&SYM_EQ_HT);
    cl_gc_register_root(&SYM_EQL_HT);
    cl_gc_register_root(&SYM_EQUAL_HT);
    cl_gc_register_root(&SYM_EQUALP_HT);
}
