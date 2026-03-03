#include "builtins.h"
#include "bignum.h"
#include "symbol.h"
#include "package.h"
#include "mem.h"
#include "error.h"
#include "vm.h"
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

/* --- Hash function for CL objects --- */

static uint32_t hash_obj(CL_Obj obj, uint32_t test)
{
    /* For eq: hash by identity */
    if (test == CL_HT_TEST_EQ) {
        uint32_t h = obj;
        h ^= h >> 16;
        h *= 0x45d9f3bU;
        h ^= h >> 16;
        return h;
    }
    /* For eql: bignums need value-based hash */
    if (test == CL_HT_TEST_EQL) {
        if (CL_BIGNUM_P(obj)) return cl_bignum_hash(obj);
        {
            uint32_t h = obj;
            h ^= h >> 16;
            h *= 0x45d9f3bU;
            h ^= h >> 16;
            return h;
        }
    }

    /* For equal: structural hash */
    if (CL_NULL_P(obj)) return 0;
    if (CL_FIXNUM_P(obj)) return (uint32_t)obj;
    if (CL_CHAR_P(obj)) return (uint32_t)obj;
    if (CL_BIGNUM_P(obj)) return cl_bignum_hash(obj);

    if (CL_HEAP_P(obj)) {
        uint8_t type = CL_HDR_TYPE(CL_OBJ_TO_PTR(obj));
        if (type == TYPE_STRING) {
            CL_String *s = (CL_String *)CL_OBJ_TO_PTR(obj);
            return cl_hash_string(s->data, s->length);
        }
        if (type == TYPE_SYMBOL) {
            /* Symbols are eq-unique, hash by identity */
            uint32_t h = obj;
            h ^= h >> 16;
            h *= 0x45d9f3bU;
            h ^= h >> 16;
            return h;
        }
        if (type == TYPE_CONS) {
            /* Hash first element only (avoid deep recursion) */
            return hash_obj(cl_car(obj), test) * 31 + 1;
        }
    }

    /* Fallback: identity hash */
    {
        uint32_t h = obj;
        h ^= h >> 16;
        h *= 0x45d9f3bU;
        h ^= h >> 16;
        return h;
    }
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
        return 0;
    }

    /* CL_HT_TEST_EQUAL: structural equality */
    if (a == b) return 1;
    if (CL_FIXNUM_P(a) || CL_CHAR_P(a)) return a == b;
    if (CL_BIGNUM_P(a) && CL_BIGNUM_P(b))
        return cl_bignum_equal(a, b);

    if (CL_STRING_P(a) && CL_STRING_P(b)) {
        CL_String *sa = (CL_String *)CL_OBJ_TO_PTR(a);
        CL_String *sb = (CL_String *)CL_OBJ_TO_PTR(b);
        return sa->length == sb->length &&
               memcmp(sa->data, sb->data, sa->length) == 0;
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
            if (test_fn == SYM_EQ_HT)
                test = CL_HT_TEST_EQ;
            else if (test_fn == SYM_EQL_HT)
                test = CL_HT_TEST_EQL;
            else if (test_fn == SYM_EQUAL_HT)
                test = CL_HT_TEST_EQUAL;
            else
                cl_error(CL_ERR_ARGS, "MAKE-HASH-TABLE: :test must be EQ, EQL, or EQUAL");
        } else if (args[i] == KW_SIZE) {
            if (!CL_FIXNUM_P(args[i + 1]))
                cl_error(CL_ERR_TYPE, "MAKE-HASH-TABLE: :size must be a number");
            size = (uint32_t)CL_FIXNUM_VAL(args[i + 1]);
            if (size < 1) size = 1;
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
    bucket_idx = hash_obj(key, ht->test) % ht->bucket_count;
    chain = ht->buckets[bucket_idx];

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
    bucket_idx = hash_obj(key, ht->test) % ht->bucket_count;
    chain = ht->buckets[bucket_idx];

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

        pair = cl_cons(key, value);
        CL_GC_PROTECT(pair);

        new_chain = cl_cons(pair, chain);
        CL_GC_UNPROTECT(4);

        /* Re-dereference ht_obj after potential GC */
        ht = (CL_Hashtable *)CL_OBJ_TO_PTR(ht_obj);
        ht->buckets[bucket_idx] = new_chain;
        ht->count++;
    }

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
    bucket_idx = hash_obj(key, ht->test) % ht->bucket_count;

    prev = CL_NIL;
    cursor = ht->buckets[bucket_idx];

    while (!CL_NULL_P(cursor)) {
        CL_Obj pair = cl_car(cursor);
        if (keys_equal(cl_car(pair), key, ht->test)) {
            /* Remove from chain */
            if (CL_NULL_P(prev)) {
                ht->buckets[bucket_idx] = cl_cdr(cursor);
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
    CL_Obj func = args[0];
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
        CL_Obj chain = ht->buckets[i];
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
    for (i = 0; i < ht->bucket_count; i++)
        ht->buckets[i] = CL_NIL;
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

/* --- Registration --- */

void cl_builtins_hashtable_init(void)
{
    /* Intern keyword/symbols used by make-hash-table */
    KW_TEST     = cl_intern_keyword("TEST", 4);
    KW_SIZE     = cl_intern_keyword("SIZE", 4);
    SYM_EQ_HT   = cl_intern_in("EQ", 2, cl_package_cl);
    SYM_EQL_HT  = cl_intern_in("EQL", 3, cl_package_cl);
    SYM_EQUAL_HT = cl_intern_in("EQUAL", 5, cl_package_cl);

    defun("MAKE-HASH-TABLE", bi_make_hash_table, 0, -1);
    defun("GETHASH", bi_gethash, 2, 3);
    defun("REMHASH", bi_remhash, 2, 2);
    defun("MAPHASH", bi_maphash, 2, 2);
    defun("CLRHASH", bi_clrhash, 1, 1);
    defun("HASH-TABLE-COUNT", bi_hash_table_count, 1, 1);
    defun("HASH-TABLE-P", bi_hash_table_p, 1, 1);
    defun("%SETF-GETHASH", bi_setf_gethash, 3, 3);
}
