#include "builtins.h"
#include "symbol.h"
#include "package.h"
#include "mem.h"
#include "error.h"
#include "../platform/platform.h"
#include <string.h>

/* Helper to register a builtin */
static void defun(const char *name, CL_CFunc func, int min, int max)
{
    CL_Obj sym = cl_intern_in(name, (uint32_t)strlen(name), cl_package_cl);
    CL_Obj fn = cl_make_function(func, sym, min, max);
    CL_Symbol *s = (CL_Symbol *)CL_OBJ_TO_PTR(sym);
    s->function = fn;
    s->value = fn;
}

/* --- Pre-interned keyword symbols --- */

static CL_Obj KW_INITIAL_ELEMENT = CL_NIL;
static CL_Obj KW_INITIAL_CONTENTS = CL_NIL;
static CL_Obj KW_FILL_POINTER = CL_NIL;
static CL_Obj KW_ADJUSTABLE = CL_NIL;
static CL_Obj KW_ELEMENT_TYPE = CL_NIL;

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
    int has_initial_element = 0;
    int has_initial_contents = 0;
    int element_type_bit = 0;
    uint32_t fill_ptr = CL_NO_FILL_POINTER;
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
            /* :fill-pointer T means start at 0, integer means that value */
            if (args[i + 1] == SYM_T) {
                fill_ptr = 0;
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
            if (CL_SYMBOL_P(element_type) &&
                strcmp(cl_symbol_name(element_type), "BIT") == 0)
                element_type_bit = 1;
        }
    }

    if (has_initial_element && has_initial_contents)
        cl_error(CL_ERR_ARGS, "MAKE-ARRAY: cannot specify both :initial-element and :initial-contents");

    /* --- 1D case: dim_arg is a fixnum --- */
    if (CL_FIXNUM_P(dim_arg)) {
        uint32_t length = (uint32_t)CL_FIXNUM_VAL(dim_arg);
        CL_Obj result;
        CL_Vector *v;

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
                CL_Obj cur = initial_contents;
                uint32_t j;
                for (j = 0; j < length && !CL_NULL_P(cur); j++) {
                    CL_Obj elem = cl_car(cur);
                    if (CL_FIXNUM_P(elem) && CL_FIXNUM_VAL(elem) == 1)
                        bv->data[j / 32] |= (1u << (j % 32));
                    cur = cl_cdr(cur);
                }
            }
            return result;
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
            /* :initial-contents is a list for 1D */
            uint32_t j = 0;
            CL_Obj cur = initial_contents;
            CL_Obj *elts = cl_vector_data(v);
            while (!CL_NULL_P(cur) && j < length) {
                elts[j++] = cl_car(cur);
                cur = cl_cdr(cur);
            }
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

        if (rank == 0) {
            /* 0-dimensional array: single element */
            return cl_make_array(1, 0, NULL, flags, fill_ptr);
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
    return (CL_VECTOR_P(args[0]) || CL_STRING_P(args[0]) || CL_BIT_VECTOR_P(args[0])) ? SYM_T : CL_NIL;
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
    if (!CL_VECTOR_P(args[0]) && !CL_STRING_P(args[0]))
        cl_error(CL_ERR_TYPE, "ADJUSTABLE-ARRAY-P: not an array");
    if (CL_STRING_P(args[0])) return CL_NIL;  /* strings are never adjustable */
    v = (CL_Vector *)CL_OBJ_TO_PTR(args[0]);
    return (v->flags & CL_VEC_FLAG_ADJUSTABLE) ? SYM_T : CL_NIL;
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
    if (!CL_VECTOR_P(args[0]))
        cl_error(CL_ERR_TYPE, "ARRAY-DIMENSIONS: not an array");
    vec = (CL_Vector *)CL_OBJ_TO_PTR(args[0]);
    if (vec->rank > 1) {
        /* Build list from stored dims */
        CL_Obj result = CL_NIL;
        int i = (int)vec->rank;
        while (i > 0) {
            i--;
            result = cl_cons(vec->data[i], result);  /* dims are already fixnums */
        }
        return result;
    }
    return cl_cons(CL_MAKE_FIXNUM(vec->length), CL_NIL);
}

static CL_Obj bi_array_rank(CL_Obj *args, int n)
{
    CL_Vector *vec;
    CL_UNUSED(n);
    if (CL_BIT_VECTOR_P(args[0])) return CL_MAKE_FIXNUM(1);
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
    if (!CL_VECTOR_P(args[0]))
        cl_error(CL_ERR_TYPE, "ARRAY-TOTAL-SIZE: not an array");
    vec = (CL_Vector *)CL_OBJ_TO_PTR(args[0]);
    return CL_MAKE_FIXNUM(vec->length);
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
    uint32_t fp;
    CL_UNUSED(n);

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

    cl_error(CL_ERR_GENERAL,
             "VECTOR-PUSH-EXTEND: vector full (capacity %d); "
             "use adjust-array to grow, then retry",
             (int)vec->length);
    return CL_NIL;  /* unreachable */
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
                new_fp = (old_vec->fill_pointer != CL_NO_FILL_POINTER)
                         ? old_vec->fill_pointer : new_len;
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

    /* Allocate new array */
    CL_GC_PROTECT(args[0]);
    new_arr = cl_make_array(new_len, 0, NULL,
                            old_vec->flags | CL_VEC_FLAG_ADJUSTABLE,
                            new_fp);
    CL_GC_UNPROTECT(1);

    /* Re-fetch after potential GC */
    old_vec = (CL_Vector *)CL_OBJ_TO_PTR(args[0]);
    new_vec = (CL_Vector *)CL_OBJ_TO_PTR(new_arr);

    /* Copy old data */
    for (i = 0; i < copy_len; i++)
        cl_vector_data(new_vec)[i] = cl_vector_data(old_vec)[i];

    /* Initialize new elements */
    if (has_ie && new_len > old_len) {
        for (i = old_len; i < new_len; i++)
            cl_vector_data(new_vec)[i] = initial_element;
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

    /* Array construction */
    defun("MAKE-ARRAY", bi_make_array, 1, -1);
    defun("VECTOR", bi_vector, 0, -1);

    /* Access */
    defun("AREF", bi_aref, 2, -1);
    defun("SVREF", bi_svref, 2, 2);
    defun("%SETF-AREF", bi_setf_aref, 3, -1);

    /* Predicates */
    defun("VECTORP", bi_vectorp, 1, 1);
    defun("ARRAYP", bi_arrayp, 1, 1);
    defun("SIMPLE-VECTOR-P", bi_simple_vector_p, 1, 1);
    defun("ADJUSTABLE-ARRAY-P", bi_adjustable_array_p, 1, 1);

    /* Query */
    defun("ARRAY-DIMENSIONS", bi_array_dimensions, 1, 1);
    defun("ARRAY-RANK", bi_array_rank, 1, 1);
    defun("ARRAY-DIMENSION", bi_array_dimension, 2, 2);
    defun("ARRAY-TOTAL-SIZE", bi_array_total_size, 1, 1);
    defun("ARRAY-ROW-MAJOR-INDEX", bi_array_row_major_index, 2, -1);
    defun("ROW-MAJOR-AREF", bi_row_major_aref, 2, 2);
    defun("%SETF-ROW-MAJOR-AREF", bi_setf_row_major_aref, 3, 3);

    /* Fill pointer */
    defun("FILL-POINTER", bi_fill_pointer, 1, 1);
    defun("%SETF-FILL-POINTER", bi_setf_fill_pointer, 2, 2);
    defun("ARRAY-HAS-FILL-POINTER-P", bi_array_has_fill_pointer_p, 1, 1);
    defun("VECTOR-PUSH", bi_vector_push, 2, 2);
    defun("VECTOR-PUSH-EXTEND", bi_vector_push_extend, 2, 3);
    defun("ADJUST-ARRAY", bi_adjust_array, 2, -1);
}
