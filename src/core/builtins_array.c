#include "builtins.h"
#include "symbol.h"
#include "package.h"
#include "mem.h"
#include "error.h"
#include "string_utils.h"
#include "thread.h"
#include "../platform/platform.h"
#include <string.h>

/* Helper to register a builtin */
static void defun(const char *name, CL_CFunc func, int min, int max)
{
    CL_Obj sym = cl_intern_in(name, (uint32_t)strlen(name), cl_package_cl);
    CL_Obj fn = cl_make_function(func, sym, min, max);
    CL_Symbol *s = (CL_Symbol *)CL_OBJ_TO_PTR(sym);
    s->function = fn;
}

/* Helper: get element from any 1D sequence (list, string, vector) */
static CL_Obj seq_elt(CL_Obj seq, uint32_t index)
{
    if (CL_ANY_STRING_P(seq)) {
        return CL_MAKE_CHAR(cl_string_char_at(seq, index));
    }
    if (CL_VECTOR_P(seq)) {
        CL_Vector *v = (CL_Vector *)CL_OBJ_TO_PTR(seq);
        return cl_vector_data(v)[index];
    }
    /* List: iterate to index (slow, but correct) */
    {
        uint32_t i;
        CL_Obj cur = seq;
        for (i = 0; i < index && CL_CONS_P(cur); i++)
            cur = cl_cdr(cur);
        return CL_CONS_P(cur) ? cl_car(cur) : CL_NIL;
    }
}

static uint32_t seq_length(CL_Obj seq)
{
    if (CL_ANY_STRING_P(seq)) {
        return cl_string_length(seq);
    }
    if (CL_VECTOR_P(seq)) {
        CL_Vector *v = (CL_Vector *)CL_OBJ_TO_PTR(seq);
        return cl_vector_active_length(v);
    }
    /* List: count */
    {
        uint32_t len = 0;
        CL_Obj cur = seq;
        while (CL_CONS_P(cur)) { len++; cur = cl_cdr(cur); }
        return len;
    }
}

/* --- Pre-interned keyword symbols --- */

static CL_Obj KW_INITIAL_ELEMENT = CL_NIL;
static CL_Obj KW_INITIAL_CONTENTS = CL_NIL;
static CL_Obj KW_FILL_POINTER = CL_NIL;
static CL_Obj KW_ADJUSTABLE = CL_NIL;
static CL_Obj KW_ELEMENT_TYPE = CL_NIL;
static CL_Obj KW_DISPLACED_TO = CL_NIL;
static CL_Obj KW_DISPLACED_INDEX_OFFSET = CL_NIL;

/* Helper: recursively flatten nested lists into array elements */
static void flatten_contents(CL_Obj contents, CL_Obj *elts, uint32_t total, uint32_t *j)
{
    while (!CL_NULL_P(contents) && *j < total) {
        CL_Obj elem = cl_car(contents);
        if (CL_CONS_P(elem)) {
            flatten_contents(elem, elts, total, j);
        } else {
            elts[(*j)++] = elem;
        }
        contents = cl_cdr(contents);
    }
}

/* ======================================================= */
/* MAKE-ARRAY                                              */
/* ======================================================= */

static CL_Obj bi_make_array(CL_Obj *args, int n)
{
    CL_Obj dim_arg = args[0];
    CL_Obj initial_element = CL_NIL;
    CL_Obj initial_contents = CL_NIL;
    CL_Obj element_type = CL_NIL;
    CL_Obj displaced_to = CL_NIL;
    int has_initial_element = 0;
    int has_initial_contents = 0;
    int has_displaced_to = 0;
    int element_type_bit = 0;
    int element_type_char = 0;
    uint32_t fill_ptr = CL_NO_FILL_POINTER;
    uint32_t displaced_offset = 0;
    int fill_pointer_is_t = 0;
    uint8_t flags = 0;
    int i;

    /* Parse keyword args */
    for (i = 1; i + 1 < n; i += 2) {
        if (args[i] == KW_INITIAL_ELEMENT) {
            initial_element = args[i + 1];
            has_initial_element = 1;
        } else if (args[i] == KW_INITIAL_CONTENTS) {
            initial_contents = args[i + 1];
            has_initial_contents = 1;
        } else if (args[i] == KW_FILL_POINTER) {
            /* :fill-pointer T means start at array size (CLHS), integer means that value */
            if (args[i + 1] == SYM_T) {
                fill_pointer_is_t = 1;
                flags |= CL_VEC_FLAG_FILL_POINTER;
            } else if (CL_FIXNUM_P(args[i + 1])) {
                fill_ptr = (uint32_t)CL_FIXNUM_VAL(args[i + 1]);
                flags |= CL_VEC_FLAG_FILL_POINTER;
            }
            /* NIL = no fill pointer (default) */
        } else if (args[i] == KW_ADJUSTABLE) {
            if (!CL_NULL_P(args[i + 1]))
                flags |= CL_VEC_FLAG_ADJUSTABLE;
        } else if (args[i] == KW_ELEMENT_TYPE) {
            element_type = args[i + 1];
            if (CL_SYMBOL_P(element_type)) {
                const char *ename = cl_symbol_name(element_type);
                if (strcmp(ename, "BIT") == 0)
                    element_type_bit = 1;
                else if (strcmp(ename, "CHARACTER") == 0 ||
                         strcmp(ename, "BASE-CHAR") == 0 ||
                         strcmp(ename, "STANDARD-CHAR") == 0)
                    element_type_char = 1;
            }
        } else if (args[i] == KW_DISPLACED_TO) {
            displaced_to = args[i + 1];
            if (!CL_NULL_P(displaced_to))
                has_displaced_to = 1;
        } else if (args[i] == KW_DISPLACED_INDEX_OFFSET) {
            if (CL_FIXNUM_P(args[i + 1]))
                displaced_offset = (uint32_t)CL_FIXNUM_VAL(args[i + 1]);
        }
    }

    if (has_initial_element && has_initial_contents)
        cl_error(CL_ERR_ARGS, "MAKE-ARRAY: cannot specify both :initial-element and :initial-contents");

    if (has_displaced_to && (has_initial_element || has_initial_contents))
        cl_error(CL_ERR_ARGS, "MAKE-ARRAY: cannot specify :displaced-to with :initial-element or :initial-contents");

    /* --- 1D case: dim_arg is a fixnum --- */
    if (CL_FIXNUM_P(dim_arg)) {
        uint32_t length = (uint32_t)CL_FIXNUM_VAL(dim_arg);
        CL_Obj result;
        CL_Vector *v;

        /* :fill-pointer T means fill-pointer = array size (CLHS) */
        if (fill_pointer_is_t)
            fill_ptr = length;

        /* --- :displaced-to handling --- */
        if (has_displaced_to) {
            /* Displaced to a string: copy substring (TYPE_STRING uses packed
               bytes, incompatible with CL_Obj element storage) */
            if (CL_ANY_STRING_P(displaced_to)) {
                uint32_t slen = cl_string_length(displaced_to);
                if (displaced_offset + length > slen)
                    cl_error(CL_ERR_ARGS, "MAKE-ARRAY: displaced bounds exceed target string length");
                /* Create new string with copied characters */
                {
                    CL_Obj str = cl_make_string(NULL, length);
                    uint32_t j;
                    for (j = 0; j < length; j++)
                        cl_string_set_char_at(str, j,
                            cl_string_char_at(displaced_to, displaced_offset + j));
                    return str;
                }
            }
            /* Displaced to a character vector (CL_VEC_FLAG_STRING) */
            if (CL_VECTOR_P(displaced_to)) {
                CL_Vector *target = (CL_Vector *)CL_OBJ_TO_PTR(displaced_to);
                uint32_t target_total = target->length;
                uint32_t n_data;

                if (displaced_offset + length > target_total)
                    cl_error(CL_ERR_ARGS, "MAKE-ARRAY: displaced bounds exceed target array");

                /* Allocate vector with 2 data slots: backing ref + offset */
                n_data = 2;
                {
                    uint32_t alloc_size = sizeof(CL_Vector) + n_data * sizeof(CL_Obj);
                    CL_Vector *dv;
                    uint8_t dflags = flags | CL_VEC_FLAG_DISPLACED;

                    /* Preserve string flag from target */
                    if (target->flags & CL_VEC_FLAG_STRING)
                        dflags |= CL_VEC_FLAG_STRING;

                    CL_GC_PROTECT(displaced_to);
                    result = CL_PTR_TO_OBJ((CL_Vector *)cl_alloc(TYPE_VECTOR, alloc_size));
                    CL_GC_UNPROTECT(1);

                    dv = (CL_Vector *)CL_OBJ_TO_PTR(result);
                    dv->length = length;
                    dv->fill_pointer = fill_ptr;
                    dv->flags = dflags;
                    dv->rank = 0;
                    dv->_reserved = 0;
                    dv->data[0] = displaced_to;
                    dv->data[1] = CL_MAKE_FIXNUM((int32_t)displaced_offset);
                    return result;
                }
            }
            /* Displaced to a bit vector.  Like the string path above, this
             * does NOT install a true displaced view (the bit-vector heap
             * type has no displacement field) — it copies the requested
             * window into a fresh bit-vector.  Mutations of the target
             * therefore won't propagate; full displacement semantics are a
             * TODO for the array-chapter conformance work. */
            if (CL_BIT_VECTOR_P(displaced_to)) {
                CL_BitVector *src = (CL_BitVector *)CL_OBJ_TO_PTR(displaced_to);
                uint32_t src_total = src->length;
                CL_Obj bv_obj;
                CL_BitVector *dst;
                uint32_t j;

                if (displaced_offset + length > src_total)
                    cl_error(CL_ERR_ARGS, "MAKE-ARRAY: displaced bounds exceed target bit-vector");

                CL_GC_PROTECT(displaced_to);
                bv_obj = cl_make_bit_vector(length);
                CL_GC_UNPROTECT(1);
                /* Re-fetch src after potential GC. */
                src = (CL_BitVector *)CL_OBJ_TO_PTR(displaced_to);
                dst = (CL_BitVector *)CL_OBJ_TO_PTR(bv_obj);
                dst->flags = flags;
                dst->fill_pointer = fill_ptr;
                for (j = 0; j < length; j++) {
                    if (cl_bv_get_bit(src, displaced_offset + j))
                        dst->data[j / 32] |= (1u << (j % 32));
                }
                return bv_obj;
            }
            cl_error(CL_ERR_TYPE, "MAKE-ARRAY: :displaced-to target must be an array");
        }

        /* Bit vector path */
        if (element_type_bit) {
            CL_BitVector *bv;
            result = cl_make_bit_vector(length);
            bv = (CL_BitVector *)CL_OBJ_TO_PTR(result);
            bv->flags = flags;
            bv->fill_pointer = fill_ptr;
            if (has_initial_element) {
                if (!CL_FIXNUM_P(initial_element) ||
                    (CL_FIXNUM_VAL(initial_element) != 0 && CL_FIXNUM_VAL(initial_element) != 1))
                    cl_error(CL_ERR_TYPE, "MAKE-ARRAY: :initial-element for bit array must be 0 or 1");
                if (CL_FIXNUM_VAL(initial_element) == 1) {
                    uint32_t nw = CL_BV_WORDS(length), wi;
                    for (wi = 0; wi < nw; wi++)
                        bv->data[wi] = 0xFFFFFFFFu;
                    /* Mask trailing */
                    if (length % 32 != 0)
                        bv->data[nw - 1] &= (1u << (length % 32)) - 1;
                }
            } else if (has_initial_contents) {
                uint32_t j;
                if (CL_VECTOR_P(initial_contents)) {
                    /* Vector initial-contents */
                    CL_Vector *iv = (CL_Vector *)CL_OBJ_TO_PTR(initial_contents);
                    CL_Obj *idata = cl_vector_data(iv);
                    uint32_t ilen = iv->length < length ? iv->length : length;
                    for (j = 0; j < ilen; j++) {
                        if (CL_FIXNUM_P(idata[j]) && CL_FIXNUM_VAL(idata[j]) == 1)
                            bv->data[j / 32] |= (1u << (j % 32));
                    }
                } else {
                    /* List initial-contents */
                    CL_Obj cur = initial_contents;
                    for (j = 0; j < length && !CL_NULL_P(cur); j++) {
                        CL_Obj elem = cl_car(cur);
                        if (CL_FIXNUM_P(elem) && CL_FIXNUM_VAL(elem) == 1)
                            bv->data[j / 32] |= (1u << (j % 32));
                        cur = cl_cdr(cur);
                    }
                }
            }
            return result;
        }

        /* Character element-type */
        if (element_type_char) {
            /* If fill-pointer or adjustable, use CL_Vector with STRING flag */
            if (flags & (CL_VEC_FLAG_FILL_POINTER | CL_VEC_FLAG_ADJUSTABLE)) {
                CL_Obj init_char = CL_MAKE_CHAR(' ');
                uint32_t j;
                if (has_initial_element) {
                    if (!CL_CHAR_P(initial_element))
                        cl_error(CL_ERR_TYPE, "MAKE-ARRAY: :initial-element for character array must be a character");
                    init_char = initial_element;
                }
                result = cl_make_array(length, 0, NULL, flags | CL_VEC_FLAG_STRING, fill_ptr);
                v = (CL_Vector *)CL_OBJ_TO_PTR(result);
                for (j = 0; j < length; j++)
                    cl_vector_data(v)[j] = init_char;
                if (has_initial_contents) {
                    uint32_t ic_len = seq_length(initial_contents);
                    if (ic_len > length) ic_len = length;
                    for (j = 0; j < ic_len; j++)
                        cl_vector_data(v)[j] = seq_elt(initial_contents, j);
                }
                return result;
            }
            /* Simple string: use TYPE_STRING */
            {
                char init_char = ' ';
                CL_Obj str;
                if (has_initial_element) {
                    if (!CL_CHAR_P(initial_element))
                        cl_error(CL_ERR_TYPE, "MAKE-ARRAY: :initial-element for character array must be a character");
                    init_char = (char)CL_CHAR_VAL(initial_element);
                }
                str = cl_make_string(NULL, length);
                {
                    uint32_t j;
                    for (j = 0; j < length; j++)
                        cl_string_set_char_at(str, j, init_char);
                }
                if (has_initial_contents) {
                    uint32_t j;
                    uint32_t ic_len = seq_length(initial_contents);
                    if (ic_len > length) ic_len = length;
                    for (j = 0; j < ic_len; j++) {
                        CL_Obj elem = seq_elt(initial_contents, j);
                        if (CL_CHAR_P(elem))
                            cl_string_set_char_at(str, j, CL_CHAR_VAL(elem));
                    }
                }
                return str;
            }
        }

        /* For numeric element types, default initial-element to 0 */
        if (!has_initial_element && !has_initial_contents && !CL_NULL_P(element_type)) {
            if (!element_type_bit && !element_type_char) {
                initial_element = CL_MAKE_FIXNUM(0);
                has_initial_element = 1;
            }
        }

        if (flags == 0 && fill_ptr == CL_NO_FILL_POINTER && !has_initial_element && !has_initial_contents) {
            /* Fast path: simple vector */
            return cl_make_vector(length);
        }

        result = cl_make_array(length, 0, NULL, flags, fill_ptr);
        v = (CL_Vector *)CL_OBJ_TO_PTR(result);

        if (has_initial_element) {
            uint32_t j;
            CL_Obj *elts = cl_vector_data(v);
            for (j = 0; j < length; j++)
                elts[j] = initial_element;
        } else if (has_initial_contents) {
            /* :initial-contents can be any sequence for 1D */
            uint32_t j;
            uint32_t ic_len = seq_length(initial_contents);
            CL_Obj *elts = cl_vector_data(v);
            if (ic_len > length) ic_len = length;
            for (j = 0; j < ic_len; j++)
                elts[j] = seq_elt(initial_contents, j);
        }
        return result;
    }

    /* --- Multi-dim case: dim_arg is a list of fixnums --- */
    if (CL_CONS_P(dim_arg) || CL_NULL_P(dim_arg)) {
        uint32_t dims[16];
        uint8_t rank = 0;
        uint32_t total = 1;
        CL_Obj cur = dim_arg;
        CL_Obj result;
        CL_Vector *v;

        /* Count rank and compute total size */
        while (!CL_NULL_P(cur) && rank < 16) {
            if (!CL_FIXNUM_P(cl_car(cur)))
                cl_error(CL_ERR_TYPE, "MAKE-ARRAY: dimension must be a fixnum");
            dims[rank] = (uint32_t)CL_FIXNUM_VAL(cl_car(cur));
            total *= dims[rank];
            rank++;
            cur = cl_cdr(cur);
        }

        /* :fill-pointer T means fill-pointer = array size (CLHS) */
        if (fill_pointer_is_t && rank == 1)
            fill_ptr = dims[0];
        else if (fill_pointer_is_t && rank == 0)
            fill_ptr = 1;

        if (rank == 0) {
            /* 0-dimensional array: single element */
            return cl_make_array(1, 0, NULL, flags, fill_ptr);
        }
        if (rank == 1 && element_type_char) {
            /* 1D character array from list dimension: create string */
            char init_char = ' ';
            CL_Obj str;
            if (has_initial_element) {
                if (!CL_CHAR_P(initial_element))
                    cl_error(CL_ERR_TYPE, "MAKE-ARRAY: :initial-element for character array must be a character");
                init_char = (char)CL_CHAR_VAL(initial_element);
            }
            str = cl_make_string(NULL, dims[0]);
            {
                uint32_t j;
                for (j = 0; j < dims[0]; j++)
                    cl_string_set_char_at(str, j, init_char);
            }
            if (has_initial_contents) {
                uint32_t j;
                uint32_t ic_len = seq_length(initial_contents);
                if (ic_len > dims[0]) ic_len = dims[0];
                for (j = 0; j < ic_len; j++) {
                    CL_Obj elem = seq_elt(initial_contents, j);
                    if (CL_CHAR_P(elem))
                        cl_string_set_char_at(str, j, CL_CHAR_VAL(elem));
                }
            }
            return str;
        }
        if (rank == 1) {
            /* List of one element = 1D array */
            result = cl_make_array(dims[0], 0, NULL, flags, fill_ptr);
        } else {
            /* True multi-dim */
            if (fill_ptr != CL_NO_FILL_POINTER)
                cl_error(CL_ERR_ARGS, "MAKE-ARRAY: fill pointer only valid for vectors (1D)");
            flags |= CL_VEC_FLAG_MULTIDIM;
            result = cl_make_array(total, rank, dims, flags, CL_NO_FILL_POINTER);
        }

        v = (CL_Vector *)CL_OBJ_TO_PTR(result);

        if (has_initial_element) {
            uint32_t j;
            CL_Obj *elts = cl_vector_data(v);
            for (j = 0; j < total; j++)
                elts[j] = initial_element;
        } else if (has_initial_contents) {
            /* For multi-dim: flatten nested lists into row-major order */
            CL_Obj *elts = cl_vector_data(v);
            if (rank <= 1) {
                uint32_t j = 0;
                cur = initial_contents;
                while (!CL_NULL_P(cur) && j < total) {
                    elts[j++] = cl_car(cur);
                    cur = cl_cdr(cur);
                }
            } else {
                /* Recursive flatten: walk nested structure to arbitrary depth */
                uint32_t j = 0;
                flatten_contents(initial_contents, elts, total, &j);
            }
        }
        return result;
    }

    cl_error(CL_ERR_TYPE, "MAKE-ARRAY: dimensions must be a fixnum or list of fixnums");
    return CL_NIL;
}

/* ======================================================= */
/* AREF / SVREF                                            */
/* ======================================================= */

static CL_Obj bi_aref(CL_Obj *args, int n)
{
    CL_Vector *vec;
    if (CL_BIT_VECTOR_P(args[0])) {
        CL_BitVector *bv = (CL_BitVector *)CL_OBJ_TO_PTR(args[0]);
        int32_t idx;
        if (n < 2)
            cl_error(CL_ERR_ARGS, "AREF: too few arguments");
        if (!CL_FIXNUM_P(args[1]))
            cl_error(CL_ERR_TYPE, "AREF: index must be a fixnum");
        idx = CL_FIXNUM_VAL(args[1]);
        if (idx < 0 || (uint32_t)idx >= cl_bv_active_length(bv))
            cl_error(CL_ERR_ARGS, "AREF: index %d out of range", (int)idx);
        return CL_MAKE_FIXNUM(cl_bv_get_bit(bv, (uint32_t)idx));
    }
    if (CL_ANY_STRING_P(args[0])) {
        int32_t idx;
        if (n < 2)
            cl_error(CL_ERR_ARGS, "AREF: too few arguments");
        if (!CL_FIXNUM_P(args[1]))
            cl_error(CL_ERR_TYPE, "AREF: index must be a fixnum");
        idx = CL_FIXNUM_VAL(args[1]);
        if (idx < 0 || (uint32_t)idx >= cl_string_length(args[0]))
            cl_error(CL_ERR_ARGS, "AREF: index %d out of range", (int)idx);
        return CL_MAKE_CHAR(cl_string_char_at(args[0], (uint32_t)idx));
    }
    if (!CL_VECTOR_P(args[0]))
        cl_error(CL_ERR_TYPE, "AREF: not an array");
    vec = (CL_Vector *)CL_OBJ_TO_PTR(args[0]);

    if (vec->rank > 1) {
        /* Multi-dimensional: compute row-major index */
        uint8_t rank = vec->rank;
        uint32_t row_major = 0;
        int d;
        if (n - 1 != (int)rank)
            cl_error(CL_ERR_ARGS, "AREF: expected %d indices, got %d",
                     (int)rank, n - 1);
        for (d = 0; d < (int)rank; d++) {
            uint32_t dim_size;
            int32_t idx;
            if (!CL_FIXNUM_P(args[d + 1]))
                cl_error(CL_ERR_TYPE, "AREF: index must be a fixnum");
            idx = CL_FIXNUM_VAL(args[d + 1]);
            dim_size = (uint32_t)CL_FIXNUM_VAL(vec->data[d]);
            if (idx < 0 || (uint32_t)idx >= dim_size)
                cl_error(CL_ERR_ARGS, "AREF: index %d out of range for dimension %d (size %d)",
                         (int)idx, d, (int)dim_size);
            row_major = row_major * dim_size + (uint32_t)idx;
        }
        return cl_vector_data(vec)[row_major];
    }

    /* 1D case */
    {
        int32_t idx;
        if (n < 2)
            cl_error(CL_ERR_ARGS, "AREF: too few arguments");
        if (!CL_FIXNUM_P(args[1]))
            cl_error(CL_ERR_TYPE, "AREF: index must be a fixnum");
        idx = CL_FIXNUM_VAL(args[1]);
        if (idx < 0 || (uint32_t)idx >= vec->length)
            cl_error(CL_ERR_ARGS, "AREF: index %d out of range", (int)idx);
        return cl_vector_data(vec)[idx];
    }
}

static CL_Obj bi_svref(CL_Obj *args, int n)
{
    CL_Vector *vec;
    int32_t idx;
    CL_UNUSED(n);
    if (!CL_VECTOR_P(args[0]))
        cl_error(CL_ERR_TYPE, "SVREF: not a simple vector");
    vec = (CL_Vector *)CL_OBJ_TO_PTR(args[0]);
    if (vec->rank > 1 || vec->flags != 0)
        cl_error(CL_ERR_TYPE, "SVREF: not a simple vector");
    if (!CL_FIXNUM_P(args[1]))
        cl_error(CL_ERR_TYPE, "SVREF: index must be a fixnum");
    idx = CL_FIXNUM_VAL(args[1]);
    if (idx < 0 || (uint32_t)idx >= vec->length)
        cl_error(CL_ERR_ARGS, "SVREF: index %d out of range", (int)idx);
    return cl_vector_data(vec)[idx];
}

/* ======================================================= */
/* %SETF-AREF (multi-dimensional setter)                   */
/* ======================================================= */

/* (%SETF-AREF array value idx1 idx2 ...) → value */
static CL_Obj bi_setf_aref(CL_Obj *args, int n)
{
    CL_Vector *vec;
    CL_Obj array = args[0];
    CL_Obj value = args[1];
    int nindices = n - 2;
    uint32_t row_major = 0;
    int d;

    if (CL_BIT_VECTOR_P(array)) {
        CL_BitVector *bv = (CL_BitVector *)CL_OBJ_TO_PTR(array);
        int32_t idx, val;
        if (nindices != 1)
            cl_error(CL_ERR_ARGS, "%SETF-AREF: expected 1 index for bit vector");
        if (!CL_FIXNUM_P(args[2]))
            cl_error(CL_ERR_TYPE, "%SETF-AREF: index must be a fixnum");
        idx = CL_FIXNUM_VAL(args[2]);
        if (idx < 0 || (uint32_t)idx >= cl_bv_active_length(bv))
            cl_error(CL_ERR_ARGS, "%SETF-AREF: index %d out of range", (int)idx);
        if (!CL_FIXNUM_P(value))
            cl_error(CL_ERR_TYPE, "%SETF-AREF: value must be 0 or 1 for bit vector");
        val = CL_FIXNUM_VAL(value);
        if (val != 0 && val != 1)
            cl_error(CL_ERR_TYPE, "%SETF-AREF: value must be 0 or 1");
        cl_bv_set_bit(bv, (uint32_t)idx, val);
        return value;
    }

    if (CL_ANY_STRING_P(array)) {
        int32_t idx;
        if (nindices != 1)
            cl_error(CL_ERR_ARGS, "%SETF-AREF: expected 1 index for string");
        if (!CL_FIXNUM_P(args[2]))
            cl_error(CL_ERR_TYPE, "%SETF-AREF: index must be a fixnum");
        idx = CL_FIXNUM_VAL(args[2]);
        if (idx < 0 || (uint32_t)idx >= cl_string_length(array))
            cl_error(CL_ERR_ARGS, "%SETF-AREF: index %d out of range", (int)idx);
        if (!CL_CHAR_P(value))
            cl_error(CL_ERR_TYPE, "%SETF-AREF: value must be a character for string");
        cl_string_set_char_at(array, (uint32_t)idx, CL_CHAR_VAL(value));
        return value;
    }

    if (!CL_VECTOR_P(array))
        cl_error(CL_ERR_TYPE, "%SETF-AREF: not an array");
    vec = (CL_Vector *)CL_OBJ_TO_PTR(array);

    if (vec->rank > 1) {
        /* Multi-dimensional */
        if (nindices != (int)vec->rank)
            cl_error(CL_ERR_ARGS, "%SETF-AREF: expected %d indices, got %d",
                     (int)vec->rank, nindices);
        for (d = 0; d < nindices; d++) {
            uint32_t dim_size;
            int32_t idx;
            if (!CL_FIXNUM_P(args[d + 2]))
                cl_error(CL_ERR_TYPE, "%SETF-AREF: index must be a fixnum");
            idx = CL_FIXNUM_VAL(args[d + 2]);
            dim_size = (uint32_t)CL_FIXNUM_VAL(vec->data[d]);
            if (idx < 0 || (uint32_t)idx >= dim_size)
                cl_error(CL_ERR_ARGS, "%SETF-AREF: index %d out of range for dimension %d (size %d)",
                         (int)idx, d, (int)dim_size);
            row_major = row_major * dim_size + (uint32_t)idx;
        }
    } else {
        /* 1D fallback */
        int32_t idx;
        if (nindices != 1)
            cl_error(CL_ERR_ARGS, "%SETF-AREF: expected 1 index, got %d", nindices);
        if (!CL_FIXNUM_P(args[2]))
            cl_error(CL_ERR_TYPE, "%SETF-AREF: index must be a fixnum");
        idx = CL_FIXNUM_VAL(args[2]);
        if (idx < 0 || (uint32_t)idx >= vec->length)
            cl_error(CL_ERR_ARGS, "%SETF-AREF: index %d out of range", (int)idx);
        row_major = (uint32_t)idx;
    }

    cl_vector_data(vec)[row_major] = value;
    return value;
}

/* ======================================================= */
/* VECTOR / VECTORP / ARRAYP / SIMPLE-VECTOR-P /           */
/* ADJUSTABLE-ARRAY-P                                      */
/* ======================================================= */

static CL_Obj bi_vector(CL_Obj *args, int n)
{
    CL_Obj v;
    CL_Vector *vec;
    int i;
    v = cl_make_vector((uint32_t)n);
    vec = (CL_Vector *)CL_OBJ_TO_PTR(v);
    for (i = 0; i < n; i++)
        cl_vector_data(vec)[i] = args[i];
    return v;
}

static CL_Obj bi_vectorp(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    /* Strings and bit-vectors are vectors per CL spec */
    if (CL_ANY_STRING_P(args[0])) return SYM_T;
    if (CL_BIT_VECTOR_P(args[0])) return SYM_T;
    if (!CL_VECTOR_P(args[0])) return CL_NIL;
    /* vectorp is false for multi-dim arrays */
    {
        CL_Vector *v = (CL_Vector *)CL_OBJ_TO_PTR(args[0]);
        return (v->rank <= 1) ? SYM_T : CL_NIL;
    }
}

/* (arrayp obj) — true for any array: vectors, multi-dim arrays, strings, bit-vectors */
static CL_Obj bi_arrayp(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    return (CL_VECTOR_P(args[0]) || CL_ANY_STRING_P(args[0]) || CL_BIT_VECTOR_P(args[0])) ? SYM_T : CL_NIL;
}

/* (simple-vector-p obj) — true for 1D, element-type T, no fill-pointer, not adjustable */
static CL_Obj bi_simple_vector_p(CL_Obj *args, int n)
{
    CL_Vector *v;
    CL_UNUSED(n);
    if (!CL_VECTOR_P(args[0])) return CL_NIL;
    v = (CL_Vector *)CL_OBJ_TO_PTR(args[0]);
    return (v->rank <= 1 && v->flags == 0) ? SYM_T : CL_NIL;
}

/* (adjustable-array-p array) — true if array was created with :adjustable t */
static CL_Obj bi_adjustable_array_p(CL_Obj *args, int n)
{
    CL_Vector *v;
    CL_UNUSED(n);
    if (CL_BIT_VECTOR_P(args[0])) {
        CL_BitVector *bv = (CL_BitVector *)CL_OBJ_TO_PTR(args[0]);
        return (bv->flags & CL_VEC_FLAG_ADJUSTABLE) ? SYM_T : CL_NIL;
    }
    if (!CL_VECTOR_P(args[0]) && !CL_ANY_STRING_P(args[0]))
        cl_error(CL_ERR_TYPE, "ADJUSTABLE-ARRAY-P: not an array");
    if (CL_ANY_STRING_P(args[0])) return CL_NIL;  /* strings are never adjustable */
    v = (CL_Vector *)CL_OBJ_TO_PTR(args[0]);
    return (v->flags & CL_VEC_FLAG_ADJUSTABLE) ? SYM_T : CL_NIL;
}

/* ======================================================= */
/* ARRAY-DISPLACEMENT                                      */
/* ======================================================= */

/* (array-displacement array) → displaced-to, displaced-index-offset
   Returns two values: the array this one is displaced to (or NIL),
   and the displaced-index-offset (or 0). */
static CL_Obj bi_array_displacement(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    if (CL_VECTOR_P(args[0])) {
        CL_Vector *v = (CL_Vector *)CL_OBJ_TO_PTR(args[0]);
        if (v->flags & CL_VEC_FLAG_DISPLACED) {
            CL_Obj backing = v->data[0];
            uint32_t offset = 0;
            if (CL_FIXNUM_P(v->data[1]))
                offset = (uint32_t)CL_FIXNUM_VAL(v->data[1]);
            cl_mv_count = 2;
            cl_mv_values[0] = backing;
            cl_mv_values[1] = CL_MAKE_FIXNUM((int32_t)offset);
            return backing;
        }
    }
    /* Not displaced: return NIL, 0 */
    cl_mv_count = 2;
    cl_mv_values[0] = CL_NIL;
    cl_mv_values[1] = CL_MAKE_FIXNUM(0);
    return CL_NIL;
}

/* ======================================================= */
/* ARRAY-DIMENSIONS / ARRAY-RANK                           */
/* ======================================================= */

static CL_Obj bi_array_dimensions(CL_Obj *args, int n)
{
    CL_Vector *vec;
    CL_UNUSED(n);
    if (CL_BIT_VECTOR_P(args[0])) {
        CL_BitVector *bv = (CL_BitVector *)CL_OBJ_TO_PTR(args[0]);
        return cl_cons(CL_MAKE_FIXNUM(bv->length), CL_NIL);
    }
    if (CL_ANY_STRING_P(args[0]))
        return cl_cons(CL_MAKE_FIXNUM((int32_t)cl_string_length(args[0])), CL_NIL);
    if (!CL_VECTOR_P(args[0]))
        cl_error(CL_ERR_TYPE, "ARRAY-DIMENSIONS: not an array");
    vec = (CL_Vector *)CL_OBJ_TO_PTR(args[0]);
    if (vec->rank > 1) {
        /* Build list from stored dims */
        CL_Obj result = CL_NIL;
        CL_Obj arr = args[0];
        int i;
        CL_GC_PROTECT(result);
        CL_GC_PROTECT(arr);
        i = (int)vec->rank;
        while (i > 0) {
            i--;
            vec = (CL_Vector *)CL_OBJ_TO_PTR(arr);  /* re-deref after GC */
            result = cl_cons(vec->data[i], result);  /* dims are already fixnums */
        }
        CL_GC_UNPROTECT(2);
        return result;
    }
    return cl_cons(CL_MAKE_FIXNUM(vec->length), CL_NIL);
}

static CL_Obj bi_array_rank(CL_Obj *args, int n)
{
    CL_Vector *vec;
    CL_UNUSED(n);
    if (CL_BIT_VECTOR_P(args[0])) return CL_MAKE_FIXNUM(1);
    if (CL_ANY_STRING_P(args[0])) return CL_MAKE_FIXNUM(1);
    if (!CL_VECTOR_P(args[0]))
        cl_error(CL_ERR_TYPE, "ARRAY-RANK: not an array");
    vec = (CL_Vector *)CL_OBJ_TO_PTR(args[0]);
    return CL_MAKE_FIXNUM(vec->rank > 1 ? vec->rank : 1);
}

/* ======================================================= */
/* ARRAY-DIMENSION / ARRAY-TOTAL-SIZE                      */
/* ======================================================= */

/* (array-dimension array axis-number) → dimension */
static CL_Obj bi_array_dimension(CL_Obj *args, int n)
{
    CL_Vector *vec;
    int32_t axis;
    CL_UNUSED(n);
    if (CL_BIT_VECTOR_P(args[0])) {
        CL_BitVector *bv = (CL_BitVector *)CL_OBJ_TO_PTR(args[0]);
        if (!CL_FIXNUM_P(args[1]) || CL_FIXNUM_VAL(args[1]) != 0)
            cl_error(CL_ERR_ARGS, "ARRAY-DIMENSION: axis must be 0 for bit vector");
        return CL_MAKE_FIXNUM(bv->length);
    }
    if (CL_ANY_STRING_P(args[0])) {
        if (!CL_FIXNUM_P(args[1]) || CL_FIXNUM_VAL(args[1]) != 0)
            cl_error(CL_ERR_ARGS, "ARRAY-DIMENSION: axis must be 0 for string");
        return CL_MAKE_FIXNUM((int32_t)cl_string_length(args[0]));
    }
    if (!CL_VECTOR_P(args[0]))
        cl_error(CL_ERR_TYPE, "ARRAY-DIMENSION: not an array");
    if (!CL_FIXNUM_P(args[1]))
        cl_error(CL_ERR_TYPE, "ARRAY-DIMENSION: axis must be a fixnum");
    vec = (CL_Vector *)CL_OBJ_TO_PTR(args[0]);
    axis = CL_FIXNUM_VAL(args[1]);
    if (vec->rank > 1) {
        if (axis < 0 || axis >= (int32_t)vec->rank)
            cl_error(CL_ERR_ARGS, "ARRAY-DIMENSION: axis %d out of range (rank %d)",
                     (int)axis, (int)vec->rank);
        return vec->data[axis];  /* dims stored as fixnums */
    }
    /* 1D: only axis 0 is valid */
    if (axis != 0)
        cl_error(CL_ERR_ARGS, "ARRAY-DIMENSION: axis %d out of range (rank 1)", (int)axis);
    return CL_MAKE_FIXNUM(vec->length);
}

/* (array-total-size array) → total number of elements */
static CL_Obj bi_array_total_size(CL_Obj *args, int n)
{
    CL_Vector *vec;
    CL_UNUSED(n);
    if (CL_BIT_VECTOR_P(args[0])) {
        CL_BitVector *bv = (CL_BitVector *)CL_OBJ_TO_PTR(args[0]);
        return CL_MAKE_FIXNUM(bv->length);
    }
    if (CL_ANY_STRING_P(args[0]))
        return CL_MAKE_FIXNUM((int32_t)cl_string_length(args[0]));
    if (!CL_VECTOR_P(args[0]))
        cl_error(CL_ERR_TYPE, "ARRAY-TOTAL-SIZE: not an array");
    vec = (CL_Vector *)CL_OBJ_TO_PTR(args[0]);
    return CL_MAKE_FIXNUM(vec->length);
}

/* ======================================================= */
/* ARRAY-ELEMENT-TYPE                                      */
/* ======================================================= */

/* (array-element-type array) → typespec
 * Returns the element type of the array.
 * CL-Amiga: strings → CHARACTER, bit-vectors → BIT, all others → T. */
static CL_Obj bi_array_element_type(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    if (CL_ANY_STRING_P(args[0]))
        return cl_intern("CHARACTER", 9);
    if (CL_BIT_VECTOR_P(args[0]))
        return cl_intern("BIT", 3);
    if (CL_VECTOR_P(args[0])) {
        CL_Vector *v = (CL_Vector *)CL_OBJ_TO_PTR(args[0]);
        if (v->flags & CL_VEC_FLAG_STRING)
            return cl_intern("CHARACTER", 9);
        return SYM_T;
    }
    cl_error(CL_ERR_TYPE, "ARRAY-ELEMENT-TYPE: not an array");
    return CL_NIL;
}

/* ======================================================= */
/* ARRAY-ROW-MAJOR-INDEX                                   */
/* ======================================================= */

/* (array-row-major-index array &rest subscripts) → index */
static CL_Obj bi_array_row_major_index(CL_Obj *args, int n)
{
    CL_Vector *vec;
    int nindices = n - 1;
    uint32_t row_major = 0;
    int d;

    if (CL_BIT_VECTOR_P(args[0])) {
        int32_t idx;
        if (nindices != 1)
            cl_error(CL_ERR_ARGS, "ARRAY-ROW-MAJOR-INDEX: expected 1 subscript for bit vector");
        if (!CL_FIXNUM_P(args[1]))
            cl_error(CL_ERR_TYPE, "ARRAY-ROW-MAJOR-INDEX: subscript must be a fixnum");
        idx = CL_FIXNUM_VAL(args[1]);
        if (idx < 0 || (uint32_t)idx >= ((CL_BitVector *)CL_OBJ_TO_PTR(args[0]))->length)
            cl_error(CL_ERR_ARGS, "ARRAY-ROW-MAJOR-INDEX: subscript out of range");
        return args[1];
    }

    if (CL_ANY_STRING_P(args[0])) {
        int32_t idx;
        if (nindices != 1)
            cl_error(CL_ERR_ARGS, "ARRAY-ROW-MAJOR-INDEX: expected 1 subscript for string");
        if (!CL_FIXNUM_P(args[1]))
            cl_error(CL_ERR_TYPE, "ARRAY-ROW-MAJOR-INDEX: subscript must be a fixnum");
        idx = CL_FIXNUM_VAL(args[1]);
        if (idx < 0 || (uint32_t)idx >= cl_string_length(args[0]))
            cl_error(CL_ERR_ARGS, "ARRAY-ROW-MAJOR-INDEX: subscript out of range");
        return args[1];
    }

    if (!CL_VECTOR_P(args[0]))
        cl_error(CL_ERR_TYPE, "ARRAY-ROW-MAJOR-INDEX: not an array");
    vec = (CL_Vector *)CL_OBJ_TO_PTR(args[0]);

    if (vec->rank > 1) {
        if (nindices != (int)vec->rank)
            cl_error(CL_ERR_ARGS, "ARRAY-ROW-MAJOR-INDEX: expected %d subscripts, got %d",
                     (int)vec->rank, nindices);
        for (d = 0; d < nindices; d++) {
            uint32_t dim_size;
            int32_t idx;
            if (!CL_FIXNUM_P(args[d + 1]))
                cl_error(CL_ERR_TYPE, "ARRAY-ROW-MAJOR-INDEX: subscript must be a fixnum");
            idx = CL_FIXNUM_VAL(args[d + 1]);
            dim_size = (uint32_t)CL_FIXNUM_VAL(vec->data[d]);
            if (idx < 0 || (uint32_t)idx >= dim_size)
                cl_error(CL_ERR_ARGS, "ARRAY-ROW-MAJOR-INDEX: subscript %d out of range for dimension %d",
                         (int)idx, d);
            row_major = row_major * dim_size + (uint32_t)idx;
        }
    } else {
        int32_t idx;
        if (nindices != 1)
            cl_error(CL_ERR_ARGS, "ARRAY-ROW-MAJOR-INDEX: expected 1 subscript, got %d", nindices);
        if (!CL_FIXNUM_P(args[1]))
            cl_error(CL_ERR_TYPE, "ARRAY-ROW-MAJOR-INDEX: subscript must be a fixnum");
        idx = CL_FIXNUM_VAL(args[1]);
        if (idx < 0 || (uint32_t)idx >= vec->length)
            cl_error(CL_ERR_ARGS, "ARRAY-ROW-MAJOR-INDEX: subscript %d out of range", (int)idx);
        row_major = (uint32_t)idx;
    }
    return CL_MAKE_FIXNUM(row_major);
}

/* ======================================================= */
/* ROW-MAJOR-AREF / (SETF ROW-MAJOR-AREF)                 */
/* ======================================================= */

/* (row-major-aref array index) → element */
static CL_Obj bi_row_major_aref(CL_Obj *args, int n)
{
    CL_Vector *vec;
    int32_t idx;
    CL_UNUSED(n);
    if (CL_BIT_VECTOR_P(args[0])) {
        CL_BitVector *bv = (CL_BitVector *)CL_OBJ_TO_PTR(args[0]);
        if (!CL_FIXNUM_P(args[1]))
            cl_error(CL_ERR_TYPE, "ROW-MAJOR-AREF: index must be a fixnum");
        idx = CL_FIXNUM_VAL(args[1]);
        if (idx < 0 || (uint32_t)idx >= bv->length)
            cl_error(CL_ERR_ARGS, "ROW-MAJOR-AREF: index out of range");
        return CL_MAKE_FIXNUM(cl_bv_get_bit(bv, (uint32_t)idx));
    }
    if (CL_ANY_STRING_P(args[0])) {
        if (!CL_FIXNUM_P(args[1]))
            cl_error(CL_ERR_TYPE, "ROW-MAJOR-AREF: index must be a fixnum");
        idx = CL_FIXNUM_VAL(args[1]);
        if (idx < 0 || (uint32_t)idx >= cl_string_length(args[0]))
            cl_error(CL_ERR_ARGS, "ROW-MAJOR-AREF: index %d out of range", (int)idx);
        return CL_MAKE_CHAR(cl_string_char_at(args[0], (uint32_t)idx));
    }
    if (!CL_VECTOR_P(args[0]))
        cl_error(CL_ERR_TYPE, "ROW-MAJOR-AREF: not an array");
    if (!CL_FIXNUM_P(args[1]))
        cl_error(CL_ERR_TYPE, "ROW-MAJOR-AREF: index must be a fixnum");
    vec = (CL_Vector *)CL_OBJ_TO_PTR(args[0]);
    idx = CL_FIXNUM_VAL(args[1]);
    if (idx < 0 || (uint32_t)idx >= vec->length)
        cl_error(CL_ERR_ARGS, "ROW-MAJOR-AREF: index %d out of range (size %d)",
                 (int)idx, (int)vec->length);
    return cl_vector_data(vec)[idx];
}

/* (%SETF-ROW-MAJOR-AREF array index value) → value */
static CL_Obj bi_setf_row_major_aref(CL_Obj *args, int n)
{
    CL_Vector *vec;
    int32_t idx;
    CL_UNUSED(n);
    if (CL_BIT_VECTOR_P(args[0])) {
        CL_BitVector *bv = (CL_BitVector *)CL_OBJ_TO_PTR(args[0]);
        int32_t val;
        if (!CL_FIXNUM_P(args[1]))
            cl_error(CL_ERR_TYPE, "%SETF-ROW-MAJOR-AREF: index must be a fixnum");
        idx = CL_FIXNUM_VAL(args[1]);
        if (idx < 0 || (uint32_t)idx >= bv->length)
            cl_error(CL_ERR_ARGS, "%SETF-ROW-MAJOR-AREF: index out of range");
        if (!CL_FIXNUM_P(args[2]))
            cl_error(CL_ERR_TYPE, "%SETF-ROW-MAJOR-AREF: value must be 0 or 1 for bit vector");
        val = CL_FIXNUM_VAL(args[2]);
        if (val != 0 && val != 1)
            cl_error(CL_ERR_TYPE, "%SETF-ROW-MAJOR-AREF: value must be 0 or 1");
        cl_bv_set_bit(bv, (uint32_t)idx, val);
        return args[2];
    }
    if (CL_ANY_STRING_P(args[0])) {
        if (!CL_FIXNUM_P(args[1]))
            cl_error(CL_ERR_TYPE, "%SETF-ROW-MAJOR-AREF: index must be a fixnum");
        idx = CL_FIXNUM_VAL(args[1]);
        if (idx < 0 || (uint32_t)idx >= cl_string_length(args[0]))
            cl_error(CL_ERR_ARGS, "%SETF-ROW-MAJOR-AREF: index %d out of range", (int)idx);
        if (!CL_CHAR_P(args[2]))
            cl_error(CL_ERR_TYPE, "%SETF-ROW-MAJOR-AREF: value must be a character for string");
        cl_string_set_char_at(args[0], (uint32_t)idx, CL_CHAR_VAL(args[2]));
        return args[2];
    }
    if (!CL_VECTOR_P(args[0]))
        cl_error(CL_ERR_TYPE, "%SETF-ROW-MAJOR-AREF: not an array");
    if (!CL_FIXNUM_P(args[1]))
        cl_error(CL_ERR_TYPE, "%SETF-ROW-MAJOR-AREF: index must be a fixnum");
    vec = (CL_Vector *)CL_OBJ_TO_PTR(args[0]);
    idx = CL_FIXNUM_VAL(args[1]);
    if (idx < 0 || (uint32_t)idx >= vec->length)
        cl_error(CL_ERR_ARGS, "%SETF-ROW-MAJOR-AREF: index %d out of range (size %d)",
                 (int)idx, (int)vec->length);
    cl_vector_data(vec)[idx] = args[2];
    return args[2];
}

/* ======================================================= */
/* FILL-POINTER / (SETF FILL-POINTER) / HAS-FILL-POINTER  */
/* ======================================================= */

/* (fill-pointer vector) → fixnum */
static CL_Obj bi_fill_pointer(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    if (CL_BIT_VECTOR_P(args[0])) {
        CL_BitVector *bv = (CL_BitVector *)CL_OBJ_TO_PTR(args[0]);
        if (bv->fill_pointer == CL_NO_FILL_POINTER)
            cl_error(CL_ERR_TYPE, "FILL-POINTER: bit vector has no fill pointer");
        return CL_MAKE_FIXNUM((int32_t)bv->fill_pointer);
    }
    {
    CL_Vector *vec;
    if (!CL_VECTOR_P(args[0]))
        cl_error(CL_ERR_TYPE, "FILL-POINTER: not a vector");
    vec = (CL_Vector *)CL_OBJ_TO_PTR(args[0]);
    if (vec->fill_pointer == CL_NO_FILL_POINTER)
        cl_error(CL_ERR_TYPE, "FILL-POINTER: vector has no fill pointer");
    return CL_MAKE_FIXNUM((int32_t)vec->fill_pointer);
    }
}

/* (%SETF-FILL-POINTER vector new-fp) → new-fp */
static CL_Obj bi_setf_fill_pointer(CL_Obj *args, int n)
{
    CL_Vector *vec;
    int32_t new_fp;
    CL_UNUSED(n);
    if (!CL_VECTOR_P(args[0]))
        cl_error(CL_ERR_TYPE, "(SETF FILL-POINTER): not a vector");
    vec = (CL_Vector *)CL_OBJ_TO_PTR(args[0]);
    if (vec->fill_pointer == CL_NO_FILL_POINTER)
        cl_error(CL_ERR_TYPE, "(SETF FILL-POINTER): vector has no fill pointer");
    if (!CL_FIXNUM_P(args[1]))
        cl_error(CL_ERR_TYPE, "(SETF FILL-POINTER): new value must be a fixnum");
    new_fp = CL_FIXNUM_VAL(args[1]);
    if (new_fp < 0 || (uint32_t)new_fp > vec->length)
        cl_error(CL_ERR_ARGS, "(SETF FILL-POINTER): %d out of range (0-%d)",
                 (int)new_fp, (int)vec->length);
    vec->fill_pointer = (uint32_t)new_fp;
    return args[1];
}

/* (array-has-fill-pointer-p array) → boolean */
static CL_Obj bi_array_has_fill_pointer_p(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    if (CL_BIT_VECTOR_P(args[0])) {
        CL_BitVector *bv = (CL_BitVector *)CL_OBJ_TO_PTR(args[0]);
        return (bv->fill_pointer != CL_NO_FILL_POINTER) ? SYM_T : CL_NIL;
    }
    if (CL_ANY_STRING_P(args[0])) return CL_NIL;
    {
    CL_Vector *vec;
    if (!CL_VECTOR_P(args[0]))
        cl_error(CL_ERR_TYPE, "ARRAY-HAS-FILL-POINTER-P: not an array");
    vec = (CL_Vector *)CL_OBJ_TO_PTR(args[0]);
    return (vec->fill_pointer != CL_NO_FILL_POINTER) ? SYM_T : CL_NIL;
    }
}

/* ======================================================= */
/* VECTOR-PUSH / VECTOR-PUSH-EXTEND                        */
/* ======================================================= */

/* (vector-push new-element vector) → index or NIL */
static CL_Obj bi_vector_push(CL_Obj *args, int n)
{
    CL_Vector *vec;
    uint32_t fp;
    CL_UNUSED(n);
    if (!CL_VECTOR_P(args[1]))
        cl_error(CL_ERR_TYPE, "VECTOR-PUSH: not a vector");
    vec = (CL_Vector *)CL_OBJ_TO_PTR(args[1]);
    if (vec->fill_pointer == CL_NO_FILL_POINTER)
        cl_error(CL_ERR_TYPE, "VECTOR-PUSH: vector has no fill pointer");
    fp = vec->fill_pointer;
    if (fp >= vec->length)
        return CL_NIL;  /* full — return NIL */
    cl_vector_data(vec)[fp] = args[0];
    vec->fill_pointer = fp + 1;
    return CL_MAKE_FIXNUM((int32_t)fp);
}

/* (vector-push-extend new-element vector &optional extension) → index
   If room: stores element at fill-pointer, increments fp, returns old fp.
   If no room: extends the vector (allocates new backing storage, copies data).
   Note: arena allocator cannot resize in-place, so the old vector object
   is patched with the new capacity by copying back over the old allocation
   if the new size fits, otherwise errors. Pre-allocate enough capacity. */
static CL_Obj bi_vector_push_extend(CL_Obj *args, int n)
{
    CL_Vector *vec;
    uint32_t fp, new_cap, old_len, i;
    CL_Obj new_arr;
    CL_Vector *new_vec;
    CL_Obj *old_data, *new_data;

    if (!CL_VECTOR_P(args[1]))
        cl_error(CL_ERR_TYPE, "VECTOR-PUSH-EXTEND: not a vector");
    vec = (CL_Vector *)CL_OBJ_TO_PTR(args[1]);
    if (vec->fill_pointer == CL_NO_FILL_POINTER)
        cl_error(CL_ERR_TYPE, "VECTOR-PUSH-EXTEND: vector has no fill pointer");
    if (!(vec->flags & CL_VEC_FLAG_ADJUSTABLE))
        cl_error(CL_ERR_TYPE, "VECTOR-PUSH-EXTEND: vector is not adjustable");

    fp = vec->fill_pointer;
    if (fp < vec->length) {
        /* Room available — same as vector-push */
        cl_vector_data(vec)[fp] = args[0];
        vec->fill_pointer = fp + 1;
        return CL_MAKE_FIXNUM((int32_t)fp);
    }

    /* Need to extend: compute new capacity */
    old_len = vec->length;
    new_cap = old_len * 2;
    if (new_cap < 4) new_cap = 4;
    /* Honor optional extension argument */
    if (n >= 3 && CL_FIXNUM_P(args[2])) {
        uint32_t ext = (uint32_t)CL_FIXNUM_VAL(args[2]);
        if (new_cap < old_len + ext)
            new_cap = old_len + ext;
    }

    /* Allocate new backing vector (GC may fire) */
    CL_GC_PROTECT(args[0]);
    CL_GC_PROTECT(args[1]);
    new_arr = cl_make_array(new_cap, 0, NULL,
                            CL_VEC_FLAG_ADJUSTABLE, CL_NO_FILL_POINTER);
    CL_GC_UNPROTECT(2);

    /* Re-fetch after potential GC */
    vec = (CL_Vector *)CL_OBJ_TO_PTR(args[1]);
    new_vec = (CL_Vector *)CL_OBJ_TO_PTR(new_arr);

    /* Copy old data to new backing vector */
    old_data = cl_vector_data(vec);
    new_data = cl_vector_data(new_vec);
    for (i = 0; i < old_len; i++)
        new_data[i] = old_data[i];

    /* Store new element */
    new_data[fp] = args[0];

    /* Displace old vector to new backing vector */
    vec->data[0] = new_arr;
    vec->data[1] = CL_MAKE_FIXNUM(0);  /* displacement offset = 0 */
    vec->flags |= CL_VEC_FLAG_DISPLACED;
    vec->length = new_cap;
    vec->fill_pointer = fp + 1;

    return CL_MAKE_FIXNUM((int32_t)fp);
}

/* (vector-pop vector) → element
   Decrements fill pointer and returns the element at the new position. */
static CL_Obj bi_vector_pop(CL_Obj *args, int n)
{
    CL_Vector *vec;
    uint32_t fp;
    CL_UNUSED(n);
    if (!CL_VECTOR_P(args[0]))
        cl_error(CL_ERR_TYPE, "VECTOR-POP: not a vector");
    vec = (CL_Vector *)CL_OBJ_TO_PTR(args[0]);
    if (vec->fill_pointer == CL_NO_FILL_POINTER)
        cl_error(CL_ERR_TYPE, "VECTOR-POP: vector has no fill pointer");
    fp = vec->fill_pointer;
    if (fp == 0)
        cl_error(CL_ERR_GENERAL, "VECTOR-POP: fill pointer is zero");
    vec->fill_pointer = fp - 1;
    return cl_vector_data(vec)[fp - 1];
}

/* ======================================================= */
/* ADJUST-ARRAY                                            */
/* ======================================================= */

/* (adjust-array array new-dimensions &key :initial-element :fill-pointer) → new-array */
static CL_Obj bi_adjust_array(CL_Obj *args, int n)
{
    CL_Vector *old_vec;
    CL_Vector *new_vec;
    CL_Obj new_arr;
    uint32_t new_len, old_len, copy_len, i;
    CL_Obj initial_element = CL_NIL;
    int has_ie = 0;
    uint32_t new_fp = CL_NO_FILL_POINTER;
    int has_fp = 0;

    if (!CL_VECTOR_P(args[0]))
        cl_error(CL_ERR_TYPE, "ADJUST-ARRAY: not an array");
    old_vec = (CL_Vector *)CL_OBJ_TO_PTR(args[0]);
    if (old_vec->rank > 1)
        cl_error(CL_ERR_GENERAL, "ADJUST-ARRAY: multi-dimensional arrays not yet supported");

    /* Parse new-dimensions (fixnum for 1D) */
    if (CL_FIXNUM_P(args[1])) {
        new_len = (uint32_t)CL_FIXNUM_VAL(args[1]);
    } else if (CL_CONS_P(args[1])) {
        if (!CL_FIXNUM_P(cl_car(args[1])))
            cl_error(CL_ERR_TYPE, "ADJUST-ARRAY: dimension must be a fixnum");
        new_len = (uint32_t)CL_FIXNUM_VAL(cl_car(args[1]));
    } else {
        cl_error(CL_ERR_TYPE, "ADJUST-ARRAY: invalid dimensions");
        return CL_NIL;
    }

    /* Parse keyword args */
    for (i = 2; i + 1 < (uint32_t)n; i += 2) {
        if (args[i] == KW_INITIAL_ELEMENT) {
            initial_element = args[i + 1];
            has_ie = 1;
        } else if (args[i] == KW_FILL_POINTER) {
            if (args[i + 1] == SYM_T) {
                /* :fill-pointer T means set to total size (CLHS) */
                new_fp = new_len;
                has_fp = 1;
            } else if (CL_FIXNUM_P(args[i + 1])) {
                new_fp = (uint32_t)CL_FIXNUM_VAL(args[i + 1]);
                has_fp = 1;
            }
        }
    }

    /* Determine fill pointer for new array */
    if (!has_fp && old_vec->fill_pointer != CL_NO_FILL_POINTER) {
        /* Preserve existing fill pointer */
        new_fp = old_vec->fill_pointer;
        if (new_fp > new_len) new_fp = new_len;
    }

    old_len = old_vec->length;
    copy_len = old_len < new_len ? old_len : new_len;

    /* Allocate new backing array */
    CL_GC_PROTECT(args[0]);
    new_arr = cl_make_array(new_len, 0, NULL,
                            old_vec->flags | CL_VEC_FLAG_ADJUSTABLE,
                            new_fp);
    CL_GC_UNPROTECT(1);

    /* Re-fetch after potential GC */
    old_vec = (CL_Vector *)CL_OBJ_TO_PTR(args[0]);
    new_vec = (CL_Vector *)CL_OBJ_TO_PTR(new_arr);

    /* Copy old data */
    {
        CL_Obj *src = cl_vector_data(old_vec);
        CL_Obj *dst = cl_vector_data(new_vec);
        for (i = 0; i < copy_len; i++)
            dst[i] = src[i];

        /* Initialize new elements */
        if (has_ie && new_len > old_len) {
            for (i = old_len; i < new_len; i++)
                dst[i] = initial_element;
        }
    }

    if (old_vec->flags & CL_VEC_FLAG_ADJUSTABLE) {
        /* Adjustable: modify in place via displacement (CL spec identity) */
        old_vec->data[0] = new_arr;
        old_vec->data[1] = CL_MAKE_FIXNUM(0);  /* displacement offset = 0 */
        old_vec->flags |= CL_VEC_FLAG_DISPLACED;
        old_vec->length = new_len;
        old_vec->fill_pointer = new_fp;
        return args[0];  /* Same identity */
    }

    return new_arr;
}

/* ======================================================= */
/* Registration                                            */
/* ======================================================= */

void cl_builtins_array_init(void)
{
    /* Pre-intern keyword symbols */
    KW_INITIAL_ELEMENT  = cl_intern_keyword("INITIAL-ELEMENT", 15);
    KW_INITIAL_CONTENTS = cl_intern_keyword("INITIAL-CONTENTS", 16);
    KW_FILL_POINTER     = cl_intern_keyword("FILL-POINTER", 12);
    KW_ADJUSTABLE       = cl_intern_keyword("ADJUSTABLE", 10);
    KW_ELEMENT_TYPE     = cl_intern_keyword("ELEMENT-TYPE", 12);
    KW_DISPLACED_TO     = cl_intern_keyword("DISPLACED-TO", 12);
    KW_DISPLACED_INDEX_OFFSET = cl_intern_keyword("DISPLACED-INDEX-OFFSET", 22);

    /* Array construction */
    defun("MAKE-ARRAY", bi_make_array, 1, -1);
    defun("VECTOR", bi_vector, 0, -1);

    /* Access */
    defun("AREF", bi_aref, 2, -1);
    defun("SVREF", bi_svref, 2, 2);
    cl_register_builtin("%SETF-AREF", bi_setf_aref, 3, -1, cl_package_clamiga);

    /* Predicates */
    defun("VECTORP", bi_vectorp, 1, 1);
    defun("ARRAYP", bi_arrayp, 1, 1);
    defun("SIMPLE-VECTOR-P", bi_simple_vector_p, 1, 1);
    defun("ADJUSTABLE-ARRAY-P", bi_adjustable_array_p, 1, 1);
    defun("ARRAY-DISPLACEMENT", bi_array_displacement, 1, 1);

    /* Query */
    defun("ARRAY-DIMENSIONS", bi_array_dimensions, 1, 1);
    defun("ARRAY-RANK", bi_array_rank, 1, 1);
    defun("ARRAY-DIMENSION", bi_array_dimension, 2, 2);
    defun("ARRAY-TOTAL-SIZE", bi_array_total_size, 1, 1);
    defun("ARRAY-ELEMENT-TYPE", bi_array_element_type, 1, 1);
    defun("ARRAY-ROW-MAJOR-INDEX", bi_array_row_major_index, 2, -1);
    defun("ROW-MAJOR-AREF", bi_row_major_aref, 2, 2);
    cl_register_builtin("%SETF-ROW-MAJOR-AREF", bi_setf_row_major_aref, 3, 3, cl_package_clamiga);

    /* Fill pointer */
    defun("FILL-POINTER", bi_fill_pointer, 1, 1);
    cl_register_builtin("%SETF-FILL-POINTER", bi_setf_fill_pointer, 2, 2, cl_package_clamiga);
    defun("ARRAY-HAS-FILL-POINTER-P", bi_array_has_fill_pointer_p, 1, 1);
    defun("VECTOR-PUSH", bi_vector_push, 2, 2);
    defun("VECTOR-PUSH-EXTEND", bi_vector_push_extend, 2, 3);
    defun("VECTOR-POP", bi_vector_pop, 1, 1);
    defun("ADJUST-ARRAY", bi_adjust_array, 2, -1);

    /* Register cached symbols for GC compaction forwarding */
    cl_gc_register_root(&KW_INITIAL_ELEMENT);
    cl_gc_register_root(&KW_INITIAL_CONTENTS);
    cl_gc_register_root(&KW_FILL_POINTER);
    cl_gc_register_root(&KW_ADJUSTABLE);
    cl_gc_register_root(&KW_ELEMENT_TYPE);
    cl_gc_register_root(&KW_DISPLACED_TO);
    cl_gc_register_root(&KW_DISPLACED_INDEX_OFFSET);
}
