/*
 * builtins_type.c — Type system: type-of, typep, coerce, subtypep
 *
 * Handles compound type specifiers: (or ...), (and ...), (not ...),
 * (member ...), (eql ...), (satisfies ...), user-defined types.
 */

#include "builtins.h"
#include "symbol.h"
#include "package.h"
#include "mem.h"
#include "float.h"
#include "ratio.h"
#include "bignum.h"
#include "printer.h"
#include "error.h"
#include "vm.h"
#include "compiler.h"
#include "readtable.h"
#include "string_utils.h"
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

/* Pre-interned compound type specifier head symbols */
static CL_Obj TYPE_SYM_OR = CL_NIL;
static CL_Obj TYPE_SYM_AND = CL_NIL;
static CL_Obj TYPE_SYM_NOT = CL_NIL;
static CL_Obj TYPE_SYM_MEMBER = CL_NIL;
static CL_Obj TYPE_SYM_EQL = CL_NIL;
static CL_Obj TYPE_SYM_SATISFIES = CL_NIL;
/* Numeric range type specifier head symbols */
static CL_Obj TYPE_SYM_INTEGER = CL_NIL;
static CL_Obj TYPE_SYM_RATIONAL = CL_NIL;
static CL_Obj TYPE_SYM_REAL = CL_NIL;
static CL_Obj TYPE_SYM_FLOAT = CL_NIL;
static CL_Obj TYPE_SYM_SINGLE_FLOAT = CL_NIL;
static CL_Obj TYPE_SYM_SHORT_FLOAT = CL_NIL;
static CL_Obj TYPE_SYM_DOUBLE_FLOAT = CL_NIL;
static CL_Obj TYPE_SYM_LONG_FLOAT = CL_NIL;
static CL_Obj TYPE_SYM_FIXNUM = CL_NIL;
static CL_Obj TYPE_SYM_BIGNUM = CL_NIL;
static CL_Obj TYPE_SYM_RATIO = CL_NIL;
static CL_Obj TYPE_SYM_NUMBER = CL_NIL;
static CL_Obj TYPE_SYM_STAR = CL_NIL; /* * for wildcard bounds in range types */
/* Compound vector/array type specifier head symbols */
static CL_Obj TYPE_SYM_SIMPLE_VECTOR = CL_NIL;
static CL_Obj TYPE_SYM_VECTOR = CL_NIL;
static CL_Obj TYPE_SYM_SIMPLE_ARRAY = CL_NIL;
static CL_Obj TYPE_SYM_ARRAY = CL_NIL;
static CL_Obj TYPE_SYM_STRING = CL_NIL;
static CL_Obj TYPE_SYM_SIMPLE_STRING = CL_NIL;
static CL_Obj TYPE_SYM_BASE_STRING = CL_NIL;
static CL_Obj TYPE_SYM_SIMPLE_BASE_STRING = CL_NIL;
static CL_Obj TYPE_SYM_BIT_VECTOR = CL_NIL;
static CL_Obj TYPE_SYM_SIMPLE_BIT_VECTOR = CL_NIL;

/* Forward declaration */
static int typep_check(CL_Obj obj, CL_Obj type_spec);

/* --- typep for simple symbol type specifiers --- */

static int typep_symbol(CL_Obj obj, CL_Obj type_sym)
{
    const char *tname = cl_symbol_name(type_sym);

    if (strcmp(tname, "T") == 0)              return 1;
    if (strcmp(tname, "NIL") == 0)            return 0;
    if (strcmp(tname, "NULL") == 0)           return CL_NULL_P(obj);
    if (strcmp(tname, "BOOLEAN") == 0)        return CL_NULL_P(obj) || obj == SYM_T;
    if (strcmp(tname, "SYMBOL") == 0)         return CL_NULL_P(obj) || CL_SYMBOL_P(obj);
    if (strcmp(tname, "KEYWORD") == 0) {
        if (!CL_NULL_P(obj) && CL_SYMBOL_P(obj)) {
            CL_Symbol *s = (CL_Symbol *)CL_OBJ_TO_PTR(obj);
            return s->package == cl_package_keyword;
        }
        return 0;
    }
    if (strcmp(tname, "CONS") == 0)           return CL_CONS_P(obj);
    if (strcmp(tname, "LIST") == 0)           return CL_NULL_P(obj) || CL_CONS_P(obj);
    if (strcmp(tname, "ATOM") == 0)           return !CL_CONS_P(obj);
    if (strcmp(tname, "FIXNUM") == 0)  return CL_FIXNUM_P(obj);
    if (strcmp(tname, "BIGNUM") == 0)  return CL_BIGNUM_P(obj);
    if (strcmp(tname, "INTEGER") == 0) return CL_INTEGER_P(obj);
    if (strcmp(tname, "RATIO") == 0)  return CL_RATIO_P(obj);
    if (strcmp(tname, "RATIONAL") == 0) return CL_RATIONAL_P(obj);
    if (strcmp(tname, "SINGLE-FLOAT") == 0 || strcmp(tname, "SHORT-FLOAT") == 0)
        return CL_SINGLE_FLOAT_P(obj);
    if (strcmp(tname, "DOUBLE-FLOAT") == 0 || strcmp(tname, "LONG-FLOAT") == 0)
        return CL_DOUBLE_FLOAT_P(obj);
    if (strcmp(tname, "FLOAT") == 0)  return CL_FLOATP(obj);
    if (strcmp(tname, "REAL") == 0)   return CL_REALP(obj);
    if (strcmp(tname, "COMPLEX") == 0) return CL_COMPLEX_P(obj);
    if (strcmp(tname, "NUMBER") == 0) return CL_NUMBER_P(obj);
    if (strcmp(tname, "CHARACTER") == 0)      return CL_CHAR_P(obj);
#ifdef CL_WIDE_STRINGS
    if (strcmp(tname, "BASE-CHAR") == 0)     return CL_CHAR_P(obj) && CL_CHAR_VAL(obj) <= 255;
    if (strcmp(tname, "STANDARD-CHAR") == 0) return CL_CHAR_P(obj) && CL_CHAR_VAL(obj) < 128;
    if (strcmp(tname, "EXTENDED-CHAR") == 0) return CL_CHAR_P(obj) && CL_CHAR_VAL(obj) > 255;
    if (strcmp(tname, "STRING") == 0)         return CL_ANY_STRING_P(obj) || CL_STRING_VECTOR_P(obj);
    if (strcmp(tname, "SIMPLE-STRING") == 0)  return CL_ANY_STRING_P(obj);
    if (strcmp(tname, "BASE-STRING") == 0)    return CL_STRING_P(obj) || CL_STRING_VECTOR_P(obj);
    if (strcmp(tname, "SIMPLE-BASE-STRING") == 0) return CL_STRING_P(obj);
#else
    if (strcmp(tname, "BASE-CHAR") == 0)     return CL_CHAR_P(obj);
    if (strcmp(tname, "STANDARD-CHAR") == 0) return CL_CHAR_P(obj);
    if (strcmp(tname, "EXTENDED-CHAR") == 0) return 0;
    if (strcmp(tname, "STRING") == 0)         return CL_STRING_P(obj) || CL_STRING_VECTOR_P(obj);
    if (strcmp(tname, "SIMPLE-STRING") == 0)  return CL_STRING_P(obj);
    if (strcmp(tname, "BASE-STRING") == 0)    return CL_STRING_P(obj) || CL_STRING_VECTOR_P(obj);
    if (strcmp(tname, "SIMPLE-BASE-STRING") == 0) return CL_STRING_P(obj);
#endif
    if (strcmp(tname, "BIT-VECTOR") == 0)
        return CL_BIT_VECTOR_P(obj);
    if (strcmp(tname, "SIMPLE-BIT-VECTOR") == 0) {
        if (!CL_BIT_VECTOR_P(obj)) return 0;
        { CL_BitVector *bv = (CL_BitVector *)CL_OBJ_TO_PTR(obj);
          return !(bv->flags & (CL_VEC_FLAG_FILL_POINTER | CL_VEC_FLAG_ADJUSTABLE)); }
    }
    if (strcmp(tname, "ARRAY") == 0 || strcmp(tname, "SIMPLE-ARRAY") == 0) {
        if (CL_ANY_STRING_P(obj)) return 1;
        if (CL_BIT_VECTOR_P(obj)) {
            if (strcmp(tname, "SIMPLE-ARRAY") == 0) {
                CL_BitVector *bv = (CL_BitVector *)CL_OBJ_TO_PTR(obj);
                return !(bv->flags & (CL_VEC_FLAG_FILL_POINTER | CL_VEC_FLAG_ADJUSTABLE));
            }
            return 1;
        }
        if (!CL_VECTOR_P(obj)) return 0;
        if (strcmp(tname, "SIMPLE-ARRAY") == 0) {
            CL_Vector *v = (CL_Vector *)CL_OBJ_TO_PTR(obj);
            /* simple = no fill-pointer and not adjustable (multidim flag is ok) */
            return !(v->flags & (CL_VEC_FLAG_FILL_POINTER | CL_VEC_FLAG_ADJUSTABLE));
        }
        return 1;
    }
    if (strcmp(tname, "VECTOR") == 0) {
        if (CL_ANY_STRING_P(obj)) return 1;
        if (CL_BIT_VECTOR_P(obj)) return 1;
        if (!CL_VECTOR_P(obj)) return 0;
        { CL_Vector *v = (CL_Vector *)CL_OBJ_TO_PTR(obj); return v->rank <= 1; }
    }
    if (strcmp(tname, "SIMPLE-VECTOR") == 0) {
        /* 1D, element-type T (not string), no fill-pointer, not adjustable */
        CL_Vector *v;
        if (!CL_VECTOR_P(obj)) return 0;
        v = (CL_Vector *)CL_OBJ_TO_PTR(obj);
        return v->rank <= 1 && v->flags == 0;
    }
    if (strcmp(tname, "SEQUENCE") == 0)
        return CL_NULL_P(obj) || CL_CONS_P(obj) || CL_ANY_STRING_P(obj) || CL_VECTOR_P(obj) || CL_BIT_VECTOR_P(obj);
    if (strcmp(tname, "FUNCTION") == 0)
        return CL_FUNCTION_P(obj) || CL_CLOSURE_P(obj) || CL_BYTECODE_P(obj);
    if (strcmp(tname, "COMPILED-FUNCTION") == 0)
        return CL_CLOSURE_P(obj) || CL_BYTECODE_P(obj);
    if (strcmp(tname, "HASH-TABLE") == 0)     return CL_HASHTABLE_P(obj);
    if (strcmp(tname, "PACKAGE") == 0)        return CL_PACKAGE_P(obj);
    if (strcmp(tname, "STREAM") == 0)        return CL_STREAM_P(obj);
    if (strcmp(tname, "SYNONYM-STREAM") == 0) {
        if (!CL_STREAM_P(obj)) return 0;
        return ((CL_Stream *)CL_OBJ_TO_PTR(obj))->stream_type == CL_STREAM_SYNONYM;
    }
    if (strcmp(tname, "FILE-STREAM") == 0) {
        if (!CL_STREAM_P(obj)) return 0;
        { uint32_t st = ((CL_Stream *)CL_OBJ_TO_PTR(obj))->stream_type;
          return st == CL_STREAM_FILE || st == CL_STREAM_CONSOLE; }
    }
    if (strcmp(tname, "STRING-STREAM") == 0) {
        if (!CL_STREAM_P(obj)) return 0;
        return ((CL_Stream *)CL_OBJ_TO_PTR(obj))->stream_type == CL_STREAM_STRING;
    }
    if (strcmp(tname, "TWO-WAY-STREAM") == 0 ||
        strcmp(tname, "BROADCAST-STREAM") == 0 ||
        strcmp(tname, "CONCATENATED-STREAM") == 0 ||
        strcmp(tname, "ECHO-STREAM") == 0) return 0;
    if (strcmp(tname, "RANDOM-STATE") == 0) return CL_RANDOM_STATE_P(obj);
    if (strcmp(tname, "PATHNAME") == 0)     return CL_PATHNAME_P(obj);
    if (strcmp(tname, "LOGICAL-PATHNAME") == 0) return 0;
    if (strcmp(tname, "READTABLE") == 0) {
        /* Readtables are fixnum pool indices */
        if (!CL_FIXNUM_P(obj)) return 0;
        { int idx = CL_FIXNUM_VAL(obj); return idx >= 0 && idx < CL_RT_POOL_SIZE; }
    }

    /* Structure types — check hierarchy for struct objects */
    if (strcmp(tname, "STRUCTURE") == 0 || strcmp(tname, "STRUCTURE-OBJECT") == 0)
        return CL_STRUCT_P(obj);
    {
        extern int cl_is_struct_type(CL_Obj type_sym);
        extern int cl_struct_type_matches(CL_Obj obj_type, CL_Obj test_type);
        extern int cl_clos_type_matches(CL_Obj obj_type, CL_Obj test_type);
        if (cl_is_struct_type(type_sym)) {
            if (!CL_STRUCT_P(obj)) return 0;
            {
                CL_Struct *st = (CL_Struct *)CL_OBJ_TO_PTR(obj);
                /* Try single-parent struct chain first, then CLOS CPL */
                if (cl_struct_type_matches(st->type_desc, type_sym))
                    return 1;
                return cl_clos_type_matches(st->type_desc, type_sym);
            }
        }
    }

    /* Condition types — check hierarchy for condition objects,
     * return 0 for non-condition objects with condition type specs */
    {
        extern int cl_condition_type_matches(CL_Obj cond_type, CL_Obj handler_type);
        extern int cl_is_condition_type(CL_Obj type_sym);
        if (cl_is_condition_type(type_sym)) {
            if (!CL_CONDITION_P(obj)) return 0;
            {
                CL_Condition *cond = (CL_Condition *)CL_OBJ_TO_PTR(obj);
                return cl_condition_type_matches(cond->type_name, type_sym);
            }
        }
    }

    /* Check user-defined types */
    {
        CL_Obj expander = cl_get_type_expander(type_sym);
        if (!CL_NULL_P(expander)) {
            /* Call expander with no args, get expanded type spec */
            CL_Obj expanded = cl_vm_apply(expander, NULL, 0);
            return typep_check(obj, expanded);
        }
    }

    /* CLOS class table fallback — handles bootstrap classes (e.g.
     * standard-object) that are in the class table but not the struct
     * registry, and any other CLOS-only type specifiers. */
    {
        extern int cl_clos_type_matches(CL_Obj obj_type, CL_Obj test_type);
        extern int cl_clos_class_exists(CL_Obj name);
        if (cl_clos_class_exists(type_sym)) {
            if (!CL_STRUCT_P(obj)) return 0;
            {
                CL_Struct *st = (CL_Struct *)CL_OBJ_TO_PTR(obj);
                return cl_clos_type_matches(st->type_desc, type_sym);
            }
        }
    }

    cl_error(CL_ERR_TYPE, "TYPEP: unknown type specifier %s",
             cl_symbol_name(type_sym));
    return 0;
}

/* --- Numeric range type check: (type [low [high]]) ---
 * low/high can be: * (unbounded), number (inclusive), (number) (exclusive) */
static int check_numeric_range(CL_Obj obj, CL_Obj head, CL_Obj args)
{
    CL_Obj low_spec, high_spec;
    int cmp;

    /* First check if obj is the right base type */
    if (head == TYPE_SYM_INTEGER || head == TYPE_SYM_FIXNUM ||
        head == TYPE_SYM_BIGNUM) {
        if (!CL_INTEGER_P(obj)) return 0;
        if (head == TYPE_SYM_FIXNUM && !CL_FIXNUM_P(obj)) return 0;
        if (head == TYPE_SYM_BIGNUM && !CL_BIGNUM_P(obj)) return 0;
    } else if (head == TYPE_SYM_RATIO) {
        if (!CL_RATIO_P(obj)) return 0;
    } else if (head == TYPE_SYM_RATIONAL) {
        if (!CL_RATIONAL_P(obj)) return 0;
    } else if (head == TYPE_SYM_FLOAT) {
        if (!CL_FLOATP(obj)) return 0;
    } else if (head == TYPE_SYM_SINGLE_FLOAT || head == TYPE_SYM_SHORT_FLOAT) {
        if (!CL_SINGLE_FLOAT_P(obj)) return 0;
    } else if (head == TYPE_SYM_DOUBLE_FLOAT || head == TYPE_SYM_LONG_FLOAT) {
        if (!CL_DOUBLE_FLOAT_P(obj)) return 0;
    } else if (head == TYPE_SYM_REAL) {
        if (!CL_REALP(obj)) return 0;
    } else if (head == TYPE_SYM_NUMBER) {
        if (!CL_NUMBER_P(obj)) return 0;
    } else {
        return 0;
    }

    /* No bounds specified: (type) = just the base type check */
    if (CL_NULL_P(args)) return 1;

    /* Parse low bound */
    low_spec = cl_car(args);
    args = cl_cdr(args);

    /* Check low bound */
    if (low_spec != TYPE_SYM_STAR && !CL_NULL_P(low_spec)) {
        if (CL_CONS_P(low_spec)) {
            /* (number) = exclusive lower bound */
            CL_Obj bound = cl_car(low_spec);
            cmp = cl_arith_compare(obj, bound);
            if (cmp <= 0) return 0;  /* obj <= bound: fail */
        } else {
            /* number = inclusive lower bound */
            cmp = cl_arith_compare(obj, low_spec);
            if (cmp < 0) return 0;   /* obj < bound: fail */
        }
    }

    /* No high bound specified */
    if (CL_NULL_P(args)) return 1;

    /* Parse high bound */
    high_spec = cl_car(args);

    /* Check high bound */
    if (high_spec != TYPE_SYM_STAR && !CL_NULL_P(high_spec)) {
        if (CL_CONS_P(high_spec)) {
            /* (number) = exclusive upper bound */
            CL_Obj bound = cl_car(high_spec);
            cmp = cl_arith_compare(obj, bound);
            if (cmp >= 0) return 0;  /* obj >= bound: fail */
        } else {
            /* number = inclusive upper bound */
            cmp = cl_arith_compare(obj, high_spec);
            if (cmp > 0) return 0;   /* obj > bound: fail */
        }
    }

    return 1;
}

/* --- Recursive typep for compound type specifiers --- */

static int typep_check(CL_Obj obj, CL_Obj type_spec)
{
    /* Symbol type specifier */
    if (CL_SYMBOL_P(type_spec) || CL_NULL_P(type_spec)) {
        return typep_symbol(obj, type_spec);
    }

    /* Compound type specifier: (head args...) */
    if (CL_CONS_P(type_spec)) {
        CL_Obj head = cl_car(type_spec);
        CL_Obj args = cl_cdr(type_spec);

        /* (or t1 t2 ...) */
        if (head == TYPE_SYM_OR) {
            CL_Obj rest = args;
            while (!CL_NULL_P(rest)) {
                if (typep_check(obj, cl_car(rest)))
                    return 1;
                rest = cl_cdr(rest);
            }
            return 0;
        }

        /* (and t1 t2 ...) */
        if (head == TYPE_SYM_AND) {
            CL_Obj rest = args;
            while (!CL_NULL_P(rest)) {
                if (!typep_check(obj, cl_car(rest)))
                    return 0;
                rest = cl_cdr(rest);
            }
            return 1;
        }

        /* (not t) */
        if (head == TYPE_SYM_NOT) {
            if (CL_NULL_P(args))
                cl_error(CL_ERR_ARGS, "TYPEP: (NOT) requires a type argument");
            return !typep_check(obj, cl_car(args));
        }

        /* (member o1 o2 ...) */
        if (head == TYPE_SYM_MEMBER) {
            CL_Obj rest = args;
            while (!CL_NULL_P(rest)) {
                if (obj == cl_car(rest))  /* eql comparison */
                    return 1;
                rest = cl_cdr(rest);
            }
            return 0;
        }

        /* (eql o) */
        if (head == TYPE_SYM_EQL) {
            if (CL_NULL_P(args))
                cl_error(CL_ERR_ARGS, "TYPEP: (EQL) requires an argument");
            return obj == cl_car(args);
        }

        /* (satisfies pred) */
        if (head == TYPE_SYM_SATISFIES) {
            CL_Obj pred_name;
            CL_Symbol *pred_sym;
            CL_Obj pred_fn, result;
            if (CL_NULL_P(args))
                cl_error(CL_ERR_ARGS, "TYPEP: (SATISFIES) requires a predicate name");
            pred_name = cl_car(args);
            if (!CL_SYMBOL_P(pred_name))
                cl_error(CL_ERR_TYPE, "TYPEP: SATISFIES predicate must be a symbol");
            pred_sym = (CL_Symbol *)CL_OBJ_TO_PTR(pred_name);
            pred_fn = pred_sym->function;
            if (CL_NULL_P(pred_fn) || pred_fn == CL_UNBOUND) {
                pred_fn = cl_symbol_value(pred_name);
                if (CL_NULL_P(pred_fn) || pred_fn == CL_UNBOUND)
                    cl_error(CL_ERR_UNDEFINED, "TYPEP: undefined predicate %s",
                             cl_symbol_name(pred_name));
            }
            result = cl_vm_apply(pred_fn, &obj, 1);
            return !CL_NULL_P(result);
        }

        /* Numeric range type specifiers: (integer low high), (real low high), etc. */
        if (head == TYPE_SYM_INTEGER || head == TYPE_SYM_FIXNUM ||
            head == TYPE_SYM_BIGNUM || head == TYPE_SYM_RATIO ||
            head == TYPE_SYM_RATIONAL || head == TYPE_SYM_REAL ||
            head == TYPE_SYM_FLOAT || head == TYPE_SYM_SINGLE_FLOAT ||
            head == TYPE_SYM_SHORT_FLOAT || head == TYPE_SYM_DOUBLE_FLOAT ||
            head == TYPE_SYM_LONG_FLOAT || head == TYPE_SYM_NUMBER) {
            return check_numeric_range(obj, head, args);
        }

        /* (simple-vector size) */
        if (head == TYPE_SYM_SIMPLE_VECTOR) {
            if (!typep_symbol(obj, head)) return 0;
            if (!CL_NULL_P(args)) {
                CL_Obj size_spec = cl_car(args);
                if (CL_FIXNUM_P(size_spec)) {
                    CL_Vector *v = (CL_Vector *)CL_OBJ_TO_PTR(obj);
                    return v->length == (uint32_t)CL_FIXNUM_VAL(size_spec);
                }
                /* * means any size */
            }
            return 1;
        }

        /* (vector element-type size) */
        if (head == TYPE_SYM_VECTOR) {
            if (!typep_symbol(obj, head)) return 0;
            /* Skip element-type check (we only have T vectors) */
            if (!CL_NULL_P(args) && !CL_NULL_P(cl_cdr(args))) {
                CL_Obj size_spec = cl_car(cl_cdr(args));
                if (CL_FIXNUM_P(size_spec)) {
                    CL_Vector *v = (CL_Vector *)CL_OBJ_TO_PTR(obj);
                    return v->length == (uint32_t)CL_FIXNUM_VAL(size_spec);
                }
            }
            return 1;
        }

        /* (string size), (simple-string size), (base-string size),
           (simple-base-string size) */
        if (head == TYPE_SYM_STRING || head == TYPE_SYM_SIMPLE_STRING ||
            head == TYPE_SYM_BASE_STRING || head == TYPE_SYM_SIMPLE_BASE_STRING) {
            if (!CL_ANY_STRING_P(obj)) return 0;
            if (!CL_NULL_P(args)) {
                CL_Obj size_spec = cl_car(args);
                if (CL_FIXNUM_P(size_spec)) {
                    CL_String *s = (CL_String *)CL_OBJ_TO_PTR(obj);
                    return s->length == (uint32_t)CL_FIXNUM_VAL(size_spec);
                }
            }
            return 1;
        }

        /* (bit-vector size), (simple-bit-vector size) */
        if (head == TYPE_SYM_BIT_VECTOR || head == TYPE_SYM_SIMPLE_BIT_VECTOR) {
            if (!CL_BIT_VECTOR_P(obj)) return 0;
            if (!CL_NULL_P(args)) {
                CL_Obj size_spec = cl_car(args);
                if (CL_FIXNUM_P(size_spec)) {
                    CL_BitVector *bv = (CL_BitVector *)CL_OBJ_TO_PTR(obj);
                    return bv->length == (uint32_t)CL_FIXNUM_VAL(size_spec);
                }
            }
            return 1;
        }

        /* (array element-type dims), (simple-array element-type dims) */
        if (head == TYPE_SYM_ARRAY || head == TYPE_SYM_SIMPLE_ARRAY) {
            if (!typep_symbol(obj, head)) return 0;
            /* Skip element-type, check rank/dimensions if provided */
            if (!CL_NULL_P(args) && !CL_NULL_P(cl_cdr(args))) {
                CL_Obj dims = cl_car(cl_cdr(args));
                if (CL_FIXNUM_P(dims)) {
                    /* dims is a rank */
                    CL_Vector *v = (CL_Vector *)CL_OBJ_TO_PTR(obj);
                    return v->rank == (uint32_t)CL_FIXNUM_VAL(dims);
                }
            }
            return 1;
        }

        /* (unsigned-byte n) — integer [0, 2^n) */
        if (CL_SYMBOL_P(head) &&
            strcmp(cl_symbol_name(head), "UNSIGNED-BYTE") == 0) {
            if (!CL_FIXNUM_P(obj) && !CL_BIGNUM_P(obj)) return 0;
            if (cl_arith_minusp(obj)) return 0;
            if (!CL_NULL_P(args) && CL_FIXNUM_P(cl_car(args))) {
                int32_t bits = CL_FIXNUM_VAL(cl_car(args));
                CL_Obj limit = cl_arith_ash(CL_MAKE_FIXNUM(1), CL_MAKE_FIXNUM(bits));
                return cl_arith_minusp(cl_arith_sub(obj, limit));
            }
            return 1;
        }

        /* (signed-byte n) — integer [-2^(n-1), 2^(n-1)) */
        if (CL_SYMBOL_P(head) &&
            strcmp(cl_symbol_name(head), "SIGNED-BYTE") == 0) {
            if (!CL_FIXNUM_P(obj) && !CL_BIGNUM_P(obj)) return 0;
            if (!CL_NULL_P(args) && CL_FIXNUM_P(cl_car(args))) {
                int32_t bits = CL_FIXNUM_VAL(cl_car(args));
                CL_Obj half = cl_arith_ash(CL_MAKE_FIXNUM(1),
                                           CL_MAKE_FIXNUM(bits - 1));
                CL_Obj neg_half = cl_arith_negate(half);
                /* obj >= -half && obj < half */
                return !cl_arith_minusp(cl_arith_sub(obj, neg_half)) &&
                        cl_arith_minusp(cl_arith_sub(obj, half));
            }
            return 1;
        }

        /* (integer low high) — integer in range */
        if (CL_SYMBOL_P(head) &&
            strcmp(cl_symbol_name(head), "INTEGER") == 0) {
            if (!CL_FIXNUM_P(obj) && !CL_BIGNUM_P(obj)) return 0;
            /* Check bounds if provided */
            if (!CL_NULL_P(args)) {
                CL_Obj low = cl_car(args);
                if (CL_FIXNUM_P(low) || CL_BIGNUM_P(low)) {
                    if (cl_arith_minusp(cl_arith_sub(obj, low))) return 0;
                }
                if (!CL_NULL_P(cl_cdr(args))) {
                    CL_Obj high = cl_car(cl_cdr(args));
                    if (CL_FIXNUM_P(high) || CL_BIGNUM_P(high)) {
                        if (cl_arith_minusp(cl_arith_sub(high, obj))) return 0;
                    }
                }
            }
            return 1;
        }

        /* (cons [car-type [cdr-type]]) — cons cell type */
        if (CL_SYMBOL_P(head) &&
            strcmp(cl_symbol_name(head), "CONS") == 0) {
            if (!CL_CONS_P(obj)) return 0;
            /* Optionally check car/cdr types */
            if (!CL_NULL_P(args)) {
                CL_Obj car_type = cl_car(args);
                if (CL_SYMBOL_P(car_type) &&
                    strcmp(cl_symbol_name(car_type), "*") != 0) {
                    if (!typep_check(cl_car(obj), car_type)) return 0;
                }
                if (!CL_NULL_P(cl_cdr(args))) {
                    CL_Obj cdr_type = cl_car(cl_cdr(args));
                    if (CL_SYMBOL_P(cdr_type) &&
                        strcmp(cl_symbol_name(cdr_type), "*") != 0) {
                        if (!typep_check(cl_cdr(obj), cdr_type)) return 0;
                    }
                }
            }
            return 1;
        }

        /* User-defined parameterized type: (my-type args...) */
        if (CL_SYMBOL_P(head)) {
            CL_Obj expander = cl_get_type_expander(head);
            if (!CL_NULL_P(expander)) {
                CL_Obj arg_array[16];
                int nargs = 0;
                CL_Obj rest = args;
                CL_Obj expanded;
                while (!CL_NULL_P(rest) && nargs < 16) {
                    arg_array[nargs++] = cl_car(rest);
                    rest = cl_cdr(rest);
                }
                expanded = cl_vm_apply(expander, arg_array, nargs);
                return typep_check(obj, expanded);
            }
        }

        {
            char buf[128];
            cl_prin1_to_string(head, buf, sizeof(buf));
            cl_error(CL_ERR_TYPE,
                     "TYPEP: invalid compound type specifier head: %s", buf);
        }
    }

    /* Class object as type specifier: extract class name (slot 0) */
    if (CL_STRUCT_P(type_spec)) {
        CL_Struct *st = (CL_Struct *)CL_OBJ_TO_PTR(type_spec);
        CL_Obj class_name = st->slots[0];
        if (CL_SYMBOL_P(class_name))
            return typep_symbol(obj, class_name);
    }

    {
        char buf[128];
        cl_prin1_to_string(type_spec, buf, sizeof(buf));
        cl_error(CL_ERR_TYPE, "TYPEP: invalid type specifier: %s", buf);
    }
    return 0;
}

/* --- Builtins --- */

static CL_Obj bi_type_of(CL_Obj *args, int n)
{
    const char *name;
    CL_UNUSED(n);
    /* For conditions, return the specific condition type name */
    if (CL_CONDITION_P(args[0])) {
        CL_Condition *cond = (CL_Condition *)CL_OBJ_TO_PTR(args[0]);
        return cond->type_name;
    }
    /* For structures, return the struct type name */
    if (CL_STRUCT_P(args[0])) {
        CL_Struct *st = (CL_Struct *)CL_OBJ_TO_PTR(args[0]);
        return st->type_desc;
    }
    /* For bit vectors */
    if (CL_BIT_VECTOR_P(args[0])) {
        CL_BitVector *bv = (CL_BitVector *)CL_OBJ_TO_PTR(args[0]);
        name = (bv->flags == 0) ? "SIMPLE-BIT-VECTOR" : "BIT-VECTOR";
        return cl_intern(name, (uint32_t)strlen(name));
    }
    /* For vectors/arrays, return specific type */
    if (CL_VECTOR_P(args[0])) {
        CL_Vector *v = (CL_Vector *)CL_OBJ_TO_PTR(args[0]);
        if (v->flags & CL_VEC_FLAG_STRING)
            name = "STRING";
        else if (v->rank > 1)
            name = !(v->flags & (CL_VEC_FLAG_FILL_POINTER | CL_VEC_FLAG_ADJUSTABLE))
                   ? "SIMPLE-ARRAY" : "ARRAY";
        else if (v->flags == 0)
            name = "SIMPLE-VECTOR";
        else
            name = "VECTOR";
        return cl_intern(name, (uint32_t)strlen(name));
    }
    name = cl_type_name(args[0]);
    return cl_intern(name, (uint32_t)strlen(name));
}

static CL_Obj bi_typep(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    return typep_check(args[0], args[1]) ? SYM_T : CL_NIL;
}

static CL_Obj bi_coerce(CL_Obj *args, int n)
{
    CL_Obj obj = args[0];
    CL_Obj result_type = args[1];
    const char *tname;
    CL_UNUSED(n);

    /* Handle compound type specifiers like (simple-array fixnum (*)):
     * extract the base type name and coerce as that sequence type. */
    if (CL_CONS_P(result_type)) {
        extern CL_Obj cl_package_cl;
        CL_Obj head = cl_car(result_type);
        if (CL_SYMBOL_P(head)) {
            const char *hname = cl_symbol_name(head);
            /* (simple-array ...) / (array ...) / (vector ...) / (simple-vector ...) */
            if (strcmp(hname, "SIMPLE-ARRAY") == 0 || strcmp(hname, "ARRAY") == 0 ||
                strcmp(hname, "VECTOR") == 0 || strcmp(hname, "SIMPLE-VECTOR") == 0) {
                /* Coerce to vector (we don't specialize element types) */
                result_type = cl_intern_in("VECTOR", 6, cl_package_cl);
            } else {
                /* For other compound types, try the base type name */
                result_type = head;
            }
        } else {
            cl_error(CL_ERR_TYPE, "COERCE: invalid result type");
        }
    }

    if (!CL_SYMBOL_P(result_type) && !CL_NULL_P(result_type))
        cl_error(CL_ERR_TYPE, "COERCE: result type must be a symbol");

    tname = cl_symbol_name(result_type);

    /* (coerce x 't) — identity */
    if (strcmp(tname, "T") == 0)
        return obj;

    /* (coerce x 'character) */
    if (strcmp(tname, "CHARACTER") == 0) {
        if (CL_CHAR_P(obj)) return obj;
        if (CL_FIXNUM_P(obj)) return CL_MAKE_CHAR((uint32_t)CL_FIXNUM_VAL(obj));
        if (CL_ANY_STRING_P(obj) && cl_string_length(obj) == 1)
            return CL_MAKE_CHAR(cl_string_char_at(obj, 0));
        cl_error(CL_ERR_TYPE, "COERCE: cannot coerce to CHARACTER");
        return CL_NIL;
    }

    /* (coerce x 'float) / 'single-float / 'short-float */
    if (strcmp(tname, "FLOAT") == 0 || strcmp(tname, "SINGLE-FLOAT") == 0 ||
        strcmp(tname, "SHORT-FLOAT") == 0) {
        if (CL_SINGLE_FLOAT_P(obj)) return obj;
        if (CL_NUMBER_P(obj)) return cl_make_single_float(cl_to_float(obj));
        cl_error(CL_ERR_TYPE, "COERCE: cannot coerce to SINGLE-FLOAT");
        return CL_NIL;
    }

    /* (coerce x 'double-float) / 'long-float */
    if (strcmp(tname, "DOUBLE-FLOAT") == 0 || strcmp(tname, "LONG-FLOAT") == 0) {
        if (CL_DOUBLE_FLOAT_P(obj)) return obj;
        if (CL_NUMBER_P(obj)) return cl_make_double_float(cl_to_double(obj));
        cl_error(CL_ERR_TYPE, "COERCE: cannot coerce to DOUBLE-FLOAT");
        return CL_NIL;
    }

    /* (coerce x 'integer) / 'fixnum / 'number */
    if (strcmp(tname, "INTEGER") == 0 || strcmp(tname, "FIXNUM") == 0 || strcmp(tname, "NUMBER") == 0) {
        if (CL_FIXNUM_P(obj)) return obj;
        if (CL_CHAR_P(obj)) return CL_MAKE_FIXNUM(CL_CHAR_VAL(obj));
        cl_error(CL_ERR_TYPE, "COERCE: cannot coerce to INTEGER");
        return CL_NIL;
    }

    /* (coerce x 'string) or 'simple-string or 'base-string or 'simple-base-string */
    if (strcmp(tname, "STRING") == 0 || strcmp(tname, "SIMPLE-STRING") == 0 ||
        strcmp(tname, "BASE-STRING") == 0 || strcmp(tname, "SIMPLE-BASE-STRING") == 0) {
        if (CL_ANY_STRING_P(obj)) return obj;
        if (CL_NULL_P(obj)) return cl_make_string("NIL", 3);
        if (CL_SYMBOL_P(obj)) {
            const char *sname = cl_symbol_name(obj);
            return cl_make_string(sname, (uint32_t)strlen(sname));
        }
        if (CL_CHAR_P(obj)) {
            char c = (char)CL_CHAR_VAL(obj);
            return cl_make_string(&c, 1);
        }
        /* Coerce list of characters to string */
        if (CL_NULL_P(obj) || CL_CONS_P(obj)) {
            CL_Obj p = obj;
            uint32_t len = 0;
            uint32_t i;
            CL_Obj result;
            while (!CL_NULL_P(p)) { len++; p = cl_cdr(p); }
            result = cl_make_string(NULL, len);
            p = obj;
            for (i = 0; i < len; i++) {
                CL_Obj elem = cl_car(p);
                if (!CL_CHAR_P(elem))
                    cl_error(CL_ERR_TYPE, "COERCE: list element is not a character");
                cl_string_set_char_at(result, i, CL_CHAR_VAL(elem));
                p = cl_cdr(p);
            }
            return result;
        }
        /* Coerce vector of characters to string */
        if (CL_VECTOR_P(obj)) {
            CL_Vector *v = (CL_Vector *)CL_OBJ_TO_PTR(obj);
            uint32_t len = cl_vector_active_length(v);
            CL_Obj *elts = cl_vector_data(v);
            uint32_t i;
            CL_Obj result = cl_make_string(NULL, len);
            for (i = 0; i < len; i++) {
                if (!CL_CHAR_P(elts[i]))
                    cl_error(CL_ERR_TYPE, "COERCE: vector element is not a character");
                cl_string_set_char_at(result, i, CL_CHAR_VAL(elts[i]));
            }
            return result;
        }
        cl_error(CL_ERR_TYPE, "COERCE: cannot coerce to STRING");
        return CL_NIL;
    }

    /* (coerce x 'bit-vector) */
    if (strcmp(tname, "BIT-VECTOR") == 0 || strcmp(tname, "SIMPLE-BIT-VECTOR") == 0) {
        if (CL_BIT_VECTOR_P(obj)) return obj;
        if (CL_NULL_P(obj) || CL_CONS_P(obj)) {
            CL_Obj p = obj;
            uint32_t len = 0;
            uint32_t ii;
            CL_Obj bvobj;
            CL_BitVector *bv;
            while (!CL_NULL_P(p)) { len++; p = cl_cdr(p); }
            bvobj = cl_make_bit_vector(len);
            bv = (CL_BitVector *)CL_OBJ_TO_PTR(bvobj);
            p = obj;
            for (ii = 0; ii < len; ii++) {
                CL_Obj elem = cl_car(p);
                if (CL_FIXNUM_P(elem) && CL_FIXNUM_VAL(elem) == 1)
                    bv->data[ii / 32] |= (1u << (ii % 32));
                p = cl_cdr(p);
            }
            return bvobj;
        }
        cl_error(CL_ERR_TYPE, "COERCE: cannot coerce to BIT-VECTOR");
        return CL_NIL;
    }

    /* (coerce x 'list) */
    if (strcmp(tname, "LIST") == 0) {
        if (CL_NULL_P(obj) || CL_CONS_P(obj)) return obj;
        if (CL_BIT_VECTOR_P(obj)) {
            CL_BitVector *bv = (CL_BitVector *)CL_OBJ_TO_PTR(obj);
            CL_Obj result = CL_NIL;
            uint32_t ii = bv->length;
            CL_GC_PROTECT(result);
            while (ii > 0) {
                ii--;
                bv = (CL_BitVector *)CL_OBJ_TO_PTR(obj);
                result = cl_cons(CL_MAKE_FIXNUM(cl_bv_get_bit(bv, ii)), result);
            }
            CL_GC_UNPROTECT(1);
            return result;
        }
        if (CL_VECTOR_P(obj)) {
            CL_Vector *v;
            CL_Obj result = CL_NIL;
            uint32_t i;
            CL_GC_PROTECT(result);
            v = (CL_Vector *)CL_OBJ_TO_PTR(obj);
            i = cl_vector_active_length(v);
            while (i > 0) {
                i--;
                v = (CL_Vector *)CL_OBJ_TO_PTR(obj);
                result = cl_cons(cl_vector_data(v)[i], result);
            }
            CL_GC_UNPROTECT(1);
            return result;
        }
        if (CL_ANY_STRING_P(obj)) {
            CL_Obj result = CL_NIL;
            uint32_t i;
            CL_GC_PROTECT(result);
            i = cl_string_length(obj);
            while (i > 0) {
                i--;
                result = cl_cons(CL_MAKE_CHAR(cl_string_char_at(obj, i)), result);
            }
            CL_GC_UNPROTECT(1);
            return result;
        }
        cl_error(CL_ERR_TYPE, "COERCE: cannot coerce to LIST");
        return CL_NIL;
    }

    /* (coerce x 'vector) or (coerce x 'simple-vector) */
    if (strcmp(tname, "VECTOR") == 0 || strcmp(tname, "SIMPLE-VECTOR") == 0) {
        if (CL_VECTOR_P(obj)) return obj;
        if (CL_ANY_STRING_P(obj)) {
            uint32_t slen = cl_string_length(obj);
            uint32_t ii;
            CL_Obj vec = cl_make_vector(slen);
            CL_Vector *v = (CL_Vector *)CL_OBJ_TO_PTR(vec);
            for (ii = 0; ii < slen; ii++)
                cl_vector_data(v)[ii] = CL_MAKE_CHAR(cl_string_char_at(obj, ii));
            return vec;
        }
        if (CL_BIT_VECTOR_P(obj)) {
            CL_BitVector *bv = (CL_BitVector *)CL_OBJ_TO_PTR(obj);
            uint32_t bvlen = bv->length;
            uint32_t ii;
            CL_Obj vec = cl_make_vector(bvlen);
            CL_Vector *v;
            bv = (CL_BitVector *)CL_OBJ_TO_PTR(obj);
            v = (CL_Vector *)CL_OBJ_TO_PTR(vec);
            for (ii = 0; ii < bvlen; ii++)
                cl_vector_data(v)[ii] = CL_MAKE_FIXNUM(cl_bv_get_bit(bv, ii));
            return vec;
        }
        if (CL_NULL_P(obj) || CL_CONS_P(obj)) {
            CL_Obj p = obj;
            uint32_t len = 0;
            uint32_t i;
            CL_Obj vec;
            CL_Vector *v;
            while (!CL_NULL_P(p)) {
                len++;
                p = cl_cdr(p);
            }
            vec = cl_make_vector(len);
            v = (CL_Vector *)CL_OBJ_TO_PTR(vec);
            p = obj;
            for (i = 0; i < len; i++) {
                cl_vector_data(v)[i] = cl_car(p);
                p = cl_cdr(p);
            }
            return vec;
        }
        cl_error(CL_ERR_TYPE, "COERCE: cannot coerce to VECTOR");
        return CL_NIL;
    }

    /* (coerce x 'function) — CL spec: identity for functions,
     * fdefinition for symbols, compile for lambda exprs */
    if (strcmp(tname, "FUNCTION") == 0 ||
        strcmp(tname, "COMPILED-FUNCTION") == 0) {
        if (CL_FUNCTION_P(obj) || CL_BYTECODE_P(obj) || CL_CLOSURE_P(obj))
            return obj;
        if (CL_SYMBOL_P(obj))
            return cl_coerce_funcdesig(obj, "COERCE");
        /* Lambda expression: (lambda (...) ...) — compile (function <expr>) */
        if (CL_CONS_P(obj)) {
            CL_Obj head = cl_car(obj);
            if (CL_SYMBOL_P(head) &&
                strcmp(cl_symbol_name(head), "LAMBDA") == 0) {
                extern CL_Obj cl_compile(CL_Obj form);
                extern CL_Obj cl_vm_eval(CL_Obj bytecode);
                extern CL_Obj cl_intern_in(const char *name, uint32_t len, CL_Obj pkg);
                extern CL_Obj cl_package_cl;
                CL_Obj fn_sym = cl_intern_in("FUNCTION", 8, cl_package_cl);
                CL_Obj form = cl_cons(fn_sym, cl_cons(obj, CL_NIL));
                CL_Obj bytecode;
                CL_GC_PROTECT(form);
                bytecode = cl_compile(form);
                CL_GC_UNPROTECT(1);
                if (CL_NULL_P(bytecode))
                    cl_error(CL_ERR_TYPE, "COERCE: failed to compile lambda");
                return cl_vm_eval(bytecode);
            }
        }
        cl_error(CL_ERR_TYPE, "COERCE: cannot coerce to FUNCTION");
        return CL_NIL;
    }

    /* (coerce x 'pathname) */
    if (strcmp(tname, "PATHNAME") == 0) {
        if (CL_PATHNAME_P(obj)) return obj;
        if (CL_ANY_STRING_P(obj)) {
            extern CL_Obj cl_parse_namestring(const char *str, uint32_t len);
            CL_String *s = (CL_String *)CL_OBJ_TO_PTR(obj);
            return cl_parse_namestring(s->data, s->length);
        }
        cl_error(CL_ERR_TYPE, "COERCE: cannot coerce to PATHNAME");
        return CL_NIL;
    }

    cl_error(CL_ERR_TYPE, "COERCE: unknown result type %s",
             cl_symbol_name(result_type));
    return CL_NIL;
}

/* --- subtypep --- */

/* Type ID enum for hierarchy checks */
enum TypeId {
    TID_UNKNOWN = -1,
    TID_NIL = 0,
    TID_NULL,
    TID_FIXNUM,
    TID_BIGNUM,
    TID_INTEGER,
    TID_RATIO,
    TID_RATIONAL,
    TID_REAL,
    TID_NUMBER,
    TID_CHARACTER,
    TID_KEYWORD,
    TID_SYMBOL,
    TID_CONS,
    TID_LIST,
    TID_STRING,
    TID_SIMPLE_VECTOR,
    TID_VECTOR,
    TID_SIMPLE_ARRAY,
    TID_ARRAY,
    TID_SEQUENCE,
    TID_COMPILED_FUNCTION,
    TID_FUNCTION,
    TID_HASH_TABLE,
    TID_PACKAGE,
    TID_ATOM,
    TID_CONDITION,
    TID_WARNING,
    TID_SERIOUS_CONDITION,
    TID_ERROR,
    TID_SIMPLE_CONDITION,
    TID_SIMPLE_ERROR,
    TID_SIMPLE_WARNING,
    TID_TYPE_ERROR,
    TID_PROGRAM_ERROR,
    TID_CONTROL_ERROR,
    TID_ARITHMETIC_ERROR,
    TID_DIVISION_BY_ZERO,
    TID_UNBOUND_VARIABLE,
    TID_UNDEFINED_FUNCTION,
    TID_SINGLE_FLOAT,
    TID_DOUBLE_FLOAT,
    TID_FLOAT,
    TID_STRUCTURE,
    TID_BIT_VECTOR,
    TID_SIMPLE_BIT_VECTOR,
    TID_PATHNAME,
    TID_BOOLEAN,
    TID_T,
#ifdef CL_WIDE_STRINGS
    TID_BASE_CHAR,
    TID_STANDARD_CHAR,
    TID_EXTENDED_CHAR,
    TID_BASE_STRING,
    TID_SIMPLE_BASE_STRING,
#endif
    TID_COUNT
};

static int type_name_to_id(const char *name)
{
    if (strcmp(name, "NIL") == 0) return TID_NIL;
    if (strcmp(name, "NULL") == 0) return TID_NULL;
    if (strcmp(name, "FIXNUM") == 0) return TID_FIXNUM;
    if (strcmp(name, "BIGNUM") == 0) return TID_BIGNUM;
    if (strcmp(name, "INTEGER") == 0) return TID_INTEGER;
    if (strcmp(name, "RATIO") == 0) return TID_RATIO;
    if (strcmp(name, "RATIONAL") == 0) return TID_RATIONAL;
    if (strcmp(name, "REAL") == 0) return TID_REAL;
    if (strcmp(name, "NUMBER") == 0) return TID_NUMBER;
    if (strcmp(name, "CHARACTER") == 0) return TID_CHARACTER;
#ifdef CL_WIDE_STRINGS
    if (strcmp(name, "BASE-CHAR") == 0) return TID_BASE_CHAR;
    if (strcmp(name, "STANDARD-CHAR") == 0) return TID_STANDARD_CHAR;
    if (strcmp(name, "EXTENDED-CHAR") == 0) return TID_EXTENDED_CHAR;
#else
    if (strcmp(name, "BASE-CHAR") == 0) return TID_CHARACTER;
    if (strcmp(name, "STANDARD-CHAR") == 0) return TID_CHARACTER;
#endif
    if (strcmp(name, "KEYWORD") == 0) return TID_KEYWORD;
    if (strcmp(name, "SYMBOL") == 0) return TID_SYMBOL;
    if (strcmp(name, "CONS") == 0) return TID_CONS;
    if (strcmp(name, "LIST") == 0) return TID_LIST;
    if (strcmp(name, "STRING") == 0) return TID_STRING;
    if (strcmp(name, "SIMPLE-VECTOR") == 0) return TID_SIMPLE_VECTOR;
    if (strcmp(name, "VECTOR") == 0) return TID_VECTOR;
    if (strcmp(name, "SIMPLE-ARRAY") == 0) return TID_SIMPLE_ARRAY;
    if (strcmp(name, "ARRAY") == 0) return TID_ARRAY;
    if (strcmp(name, "SEQUENCE") == 0) return TID_SEQUENCE;
    if (strcmp(name, "COMPILED-FUNCTION") == 0) return TID_COMPILED_FUNCTION;
    if (strcmp(name, "FUNCTION") == 0) return TID_FUNCTION;
    if (strcmp(name, "HASH-TABLE") == 0) return TID_HASH_TABLE;
    if (strcmp(name, "PACKAGE") == 0) return TID_PACKAGE;
    if (strcmp(name, "ATOM") == 0) return TID_ATOM;
    if (strcmp(name, "CONDITION") == 0) return TID_CONDITION;
    if (strcmp(name, "WARNING") == 0) return TID_WARNING;
    if (strcmp(name, "SERIOUS-CONDITION") == 0) return TID_SERIOUS_CONDITION;
    if (strcmp(name, "ERROR") == 0) return TID_ERROR;
    if (strcmp(name, "SIMPLE-CONDITION") == 0) return TID_SIMPLE_CONDITION;
    if (strcmp(name, "SIMPLE-ERROR") == 0) return TID_SIMPLE_ERROR;
    if (strcmp(name, "SIMPLE-WARNING") == 0) return TID_SIMPLE_WARNING;
    if (strcmp(name, "TYPE-ERROR") == 0) return TID_TYPE_ERROR;
    if (strcmp(name, "PROGRAM-ERROR") == 0) return TID_PROGRAM_ERROR;
    if (strcmp(name, "CONTROL-ERROR") == 0) return TID_CONTROL_ERROR;
    if (strcmp(name, "ARITHMETIC-ERROR") == 0) return TID_ARITHMETIC_ERROR;
    if (strcmp(name, "DIVISION-BY-ZERO") == 0) return TID_DIVISION_BY_ZERO;
    if (strcmp(name, "UNBOUND-VARIABLE") == 0) return TID_UNBOUND_VARIABLE;
    if (strcmp(name, "UNDEFINED-FUNCTION") == 0) return TID_UNDEFINED_FUNCTION;
    if (strcmp(name, "SINGLE-FLOAT") == 0 || strcmp(name, "SHORT-FLOAT") == 0) return TID_SINGLE_FLOAT;
    if (strcmp(name, "DOUBLE-FLOAT") == 0 || strcmp(name, "LONG-FLOAT") == 0) return TID_DOUBLE_FLOAT;
    if (strcmp(name, "FLOAT") == 0) return TID_FLOAT;
    if (strcmp(name, "STRUCTURE") == 0 || strcmp(name, "STRUCTURE-OBJECT") == 0) return TID_STRUCTURE;
    if (strcmp(name, "BIT-VECTOR") == 0) return TID_BIT_VECTOR;
    if (strcmp(name, "SIMPLE-BIT-VECTOR") == 0) return TID_SIMPLE_BIT_VECTOR;
    if (strcmp(name, "PATHNAME") == 0) return TID_PATHNAME;
    if (strcmp(name, "BOOLEAN") == 0) return TID_BOOLEAN;
    if (strcmp(name, "T") == 0) return TID_T;
    return TID_UNKNOWN;
}

/* Public API: check if a name is a built-in type recognized by typep */
int cl_is_builtin_type_name(const char *name)
{
    return type_name_to_id(name) != TID_UNKNOWN;
}

/* Check if type1 is a subtype of type2 using the known hierarchy.
 * Returns: 1 = yes, 0 = no, -1 = unknown */
static int subtype_check(int id1, int id2)
{
    /* nil is subtype of everything */
    if (id1 == TID_NIL) return 1;
    /* everything is subtype of t */
    if (id2 == TID_T) return 1;
    /* same type */
    if (id1 == id2) return 1;
    /* t is NOT subtype of anything except t */
    if (id1 == TID_T) return 0;

    /* Numeric hierarchy: fixnum/bignum < integer < rational < real < number */
    if (id1 == TID_FIXNUM && (id2 == TID_INTEGER || id2 == TID_RATIONAL ||
                               id2 == TID_REAL || id2 == TID_NUMBER)) return 1;
    if (id1 == TID_BIGNUM && (id2 == TID_INTEGER || id2 == TID_RATIONAL ||
                               id2 == TID_REAL || id2 == TID_NUMBER)) return 1;
    if (id1 == TID_INTEGER && (id2 == TID_RATIONAL || id2 == TID_REAL ||
                                id2 == TID_NUMBER)) return 1;
    if (id1 == TID_RATIO && (id2 == TID_RATIONAL || id2 == TID_REAL ||
                              id2 == TID_NUMBER)) return 1;
    if (id1 == TID_RATIONAL && (id2 == TID_REAL || id2 == TID_NUMBER)) return 1;
    if (id1 == TID_REAL && id2 == TID_NUMBER) return 1;

    /* Float hierarchy: single-float/double-float < float < real < number */
    if (id1 == TID_SINGLE_FLOAT && (id2 == TID_FLOAT || id2 == TID_REAL ||
                                     id2 == TID_NUMBER)) return 1;
    if (id1 == TID_DOUBLE_FLOAT && (id2 == TID_FLOAT || id2 == TID_REAL ||
                                     id2 == TID_NUMBER)) return 1;
    if (id1 == TID_FLOAT && (id2 == TID_REAL || id2 == TID_NUMBER)) return 1;

    /* List hierarchy: null < list, cons < list, list < sequence */
    if (id1 == TID_NULL && (id2 == TID_LIST || id2 == TID_SYMBOL || id2 == TID_SEQUENCE)) return 1;
    if (id1 == TID_CONS && (id2 == TID_LIST || id2 == TID_SEQUENCE)) return 1;
    if (id1 == TID_LIST && id2 == TID_SEQUENCE) return 1;

    /* Symbol hierarchy: keyword < symbol, null < symbol, boolean < symbol */
    if (id1 == TID_KEYWORD && id2 == TID_SYMBOL) return 1;
    if (id1 == TID_BOOLEAN && id2 == TID_SYMBOL) return 1;
    if (id1 == TID_NULL && id2 == TID_BOOLEAN) return 1;

    /* Vector/array hierarchy:
     * simple-vector < simple-array < array
     * simple-vector < vector < array
     * vector < sequence
     * string < vector < array
     */
    if (id1 == TID_SIMPLE_VECTOR && (id2 == TID_VECTOR || id2 == TID_SIMPLE_ARRAY ||
                                      id2 == TID_ARRAY || id2 == TID_SEQUENCE)) return 1;
    if (id1 == TID_SIMPLE_ARRAY && id2 == TID_ARRAY) return 1;
    if (id1 == TID_VECTOR && (id2 == TID_ARRAY || id2 == TID_SEQUENCE)) return 1;
    if (id1 == TID_STRING && (id2 == TID_VECTOR || id2 == TID_SIMPLE_ARRAY ||
                               id2 == TID_ARRAY || id2 == TID_SEQUENCE)) return 1;
    if (id1 == TID_BIT_VECTOR && (id2 == TID_VECTOR || id2 == TID_ARRAY ||
                                   id2 == TID_SEQUENCE)) return 1;
    if (id1 == TID_SIMPLE_BIT_VECTOR && (id2 == TID_BIT_VECTOR || id2 == TID_VECTOR ||
                                          id2 == TID_SIMPLE_ARRAY || id2 == TID_ARRAY ||
                                          id2 == TID_SEQUENCE)) return 1;

    /* Function hierarchy: compiled-function < function */
    if (id1 == TID_COMPILED_FUNCTION && id2 == TID_FUNCTION) return 1;

#ifdef CL_WIDE_STRINGS
    /* Character hierarchy: base-char < character, standard-char < base-char < character,
       extended-char < character */
    if (id1 == TID_BASE_CHAR && id2 == TID_CHARACTER) return 1;
    if (id1 == TID_STANDARD_CHAR && (id2 == TID_BASE_CHAR || id2 == TID_CHARACTER)) return 1;
    if (id1 == TID_EXTENDED_CHAR && id2 == TID_CHARACTER) return 1;
#endif

    /* Fixnum/integer/number are atoms */
    if (id2 == TID_ATOM) {
        if (id1 != TID_CONS && id1 != TID_LIST) return 1;
        if (id1 == TID_LIST) return 0;  /* list includes cons */
    }

    /* Condition hierarchy:
     * condition
     *   warning
     *     simple-warning  (also simple-condition)
     *   serious-condition
     *     error
     *       simple-error  (also simple-condition)
     *       type-error
     *       program-error
     *       control-error
     *       unbound-variable
     *       undefined-function
     *       arithmetic-error
     *         division-by-zero
     *   simple-condition
     */
    if (id2 == TID_CONDITION) {
        if (id1 == TID_WARNING || id1 == TID_SERIOUS_CONDITION ||
            id1 == TID_ERROR || id1 == TID_SIMPLE_CONDITION ||
            id1 == TID_SIMPLE_ERROR || id1 == TID_SIMPLE_WARNING ||
            id1 == TID_TYPE_ERROR || id1 == TID_PROGRAM_ERROR ||
            id1 == TID_CONTROL_ERROR || id1 == TID_ARITHMETIC_ERROR ||
            id1 == TID_DIVISION_BY_ZERO || id1 == TID_UNBOUND_VARIABLE ||
            id1 == TID_UNDEFINED_FUNCTION)
            return 1;
    }
    if (id2 == TID_WARNING) {
        if (id1 == TID_SIMPLE_WARNING) return 1;
    }
    if (id2 == TID_SERIOUS_CONDITION) {
        if (id1 == TID_ERROR || id1 == TID_SIMPLE_ERROR ||
            id1 == TID_TYPE_ERROR || id1 == TID_PROGRAM_ERROR ||
            id1 == TID_CONTROL_ERROR || id1 == TID_ARITHMETIC_ERROR ||
            id1 == TID_DIVISION_BY_ZERO || id1 == TID_UNBOUND_VARIABLE ||
            id1 == TID_UNDEFINED_FUNCTION)
            return 1;
    }
    if (id2 == TID_ERROR) {
        if (id1 == TID_SIMPLE_ERROR || id1 == TID_TYPE_ERROR ||
            id1 == TID_PROGRAM_ERROR || id1 == TID_CONTROL_ERROR ||
            id1 == TID_ARITHMETIC_ERROR || id1 == TID_DIVISION_BY_ZERO ||
            id1 == TID_UNBOUND_VARIABLE || id1 == TID_UNDEFINED_FUNCTION)
            return 1;
    }
    if (id2 == TID_SIMPLE_CONDITION) {
        if (id1 == TID_SIMPLE_ERROR || id1 == TID_SIMPLE_WARNING)
            return 1;
    }
    if (id2 == TID_ARITHMETIC_ERROR) {
        if (id1 == TID_DIVISION_BY_ZERO) return 1;
    }

    /* Structure hierarchy: structure < atom */
    if (id1 == TID_STRUCTURE && id2 == TID_ATOM) return 1;

    /* Pathname is an atom */
    if (id1 == TID_PATHNAME && id2 == TID_ATOM) return 1;

    return 0;
}

static CL_Obj bi_subtypep(CL_Obj *args, int n)
{
    CL_Obj type1 = args[0];
    CL_Obj type2 = args[1];
    int id1, id2, result;
    CL_UNUSED(n);

    /* Expand deftype'd symbols before anything else */
    if (CL_SYMBOL_P(type1)) {
        CL_Obj expander = cl_get_type_expander(type1);
        if (!CL_NULL_P(expander)) {
            type1 = cl_vm_apply(expander, &type1, 0);
            /* Recurse with expanded types */
            CL_Obj rargs[2] = { type1, type2 };
            return bi_subtypep(rargs, 2);
        }
    }
    if (CL_SYMBOL_P(type2)) {
        CL_Obj expander = cl_get_type_expander(type2);
        if (!CL_NULL_P(expander)) {
            type2 = cl_vm_apply(expander, &type2, 0);
            CL_Obj rargs[2] = { type1, type2 };
            return bi_subtypep(rargs, 2);
        }
    }

    /* Class objects as type specifiers: extract class name (slot 0) */
    if (CL_STRUCT_P(type1)) {
        CL_Struct *st = (CL_Struct *)CL_OBJ_TO_PTR(type1);
        type1 = st->slots[0]; /* class name */
    }
    if (CL_STRUCT_P(type2)) {
        CL_Struct *st = (CL_Struct *)CL_OBJ_TO_PTR(type2);
        type2 = st->slots[0]; /* class name */
    }

    /* Only handle symbol type specifiers */
    if ((!CL_SYMBOL_P(type1) && !CL_NULL_P(type1)) ||
        (!CL_SYMBOL_P(type2) && !CL_NULL_P(type2))) {
        /* Compound types: return (NIL NIL) = "don't know" */
        cl_mv_count = 2;
        cl_mv_values[0] = CL_NIL;
        cl_mv_values[1] = CL_NIL;
        return CL_NIL;
    }

    id1 = type_name_to_id(cl_symbol_name(type1));
    id2 = type_name_to_id(cl_symbol_name(type2));

    if (id1 == TID_UNKNOWN || id2 == TID_UNKNOWN) {
        /* Check struct type hierarchy (CLOS classes) */
        extern int cl_struct_type_matches(CL_Obj obj_type, CL_Obj test_type);
        extern int cl_is_struct_type(CL_Obj type_sym);
        if (CL_SYMBOL_P(type1) && CL_SYMBOL_P(type2) &&
            cl_is_struct_type(type1) && cl_is_struct_type(type2)) {
            result = cl_struct_type_matches(type1, type2);
            cl_mv_count = 2;
            cl_mv_values[0] = result ? SYM_T : CL_NIL;
            cl_mv_values[1] = SYM_T;
            return result ? SYM_T : CL_NIL;
        }
        /* Unknown type: return (NIL NIL) */
        cl_mv_count = 2;
        cl_mv_values[0] = CL_NIL;
        cl_mv_values[1] = CL_NIL;
        return CL_NIL;
    }

    result = subtype_check(id1, id2);
    cl_mv_count = 2;
    cl_mv_values[0] = result ? SYM_T : CL_NIL;
    cl_mv_values[1] = SYM_T;  /* second value = "certain" */
    return result ? SYM_T : CL_NIL;
}

/* --- Public typep API --- */

int cl_typep(CL_Obj obj, CL_Obj type_spec)
{
    return typep_check(obj, type_spec);
}

/* --- Registration --- */

void cl_builtins_type_init(void)
{
    /* Pre-intern compound type specifier head symbols */
    TYPE_SYM_OR        = cl_intern_in("OR", 2, cl_package_cl);
    TYPE_SYM_AND       = cl_intern_in("AND", 3, cl_package_cl);
    TYPE_SYM_NOT       = cl_intern_in("NOT", 3, cl_package_cl);
    TYPE_SYM_MEMBER    = cl_intern_in("MEMBER", 6, cl_package_cl);
    TYPE_SYM_EQL       = cl_intern_in("EQL", 3, cl_package_cl);
    TYPE_SYM_SATISFIES = cl_intern_in("SATISFIES", 9, cl_package_cl);
    /* Numeric range type specifier head symbols */
    TYPE_SYM_INTEGER      = cl_intern_in("INTEGER", 7, cl_package_cl);
    TYPE_SYM_RATIONAL     = cl_intern_in("RATIONAL", 8, cl_package_cl);
    TYPE_SYM_REAL         = cl_intern_in("REAL", 4, cl_package_cl);
    TYPE_SYM_FLOAT        = cl_intern_in("FLOAT", 5, cl_package_cl);
    TYPE_SYM_SINGLE_FLOAT = cl_intern_in("SINGLE-FLOAT", 12, cl_package_cl);
    TYPE_SYM_SHORT_FLOAT  = cl_intern_in("SHORT-FLOAT", 11, cl_package_cl);
    TYPE_SYM_DOUBLE_FLOAT = cl_intern_in("DOUBLE-FLOAT", 12, cl_package_cl);
    TYPE_SYM_LONG_FLOAT   = cl_intern_in("LONG-FLOAT", 10, cl_package_cl);
    TYPE_SYM_FIXNUM       = cl_intern_in("FIXNUM", 6, cl_package_cl);
    TYPE_SYM_BIGNUM       = cl_intern_in("BIGNUM", 6, cl_package_cl);
    TYPE_SYM_RATIO        = cl_intern_in("RATIO", 5, cl_package_cl);
    TYPE_SYM_NUMBER       = cl_intern_in("NUMBER", 6, cl_package_cl);
    TYPE_SYM_STAR         = cl_intern_in("*", 1, cl_package_cl);
    /* Compound vector/array type specifier head symbols */
    TYPE_SYM_SIMPLE_VECTOR     = cl_intern_in("SIMPLE-VECTOR", 13, cl_package_cl);
    TYPE_SYM_VECTOR            = cl_intern_in("VECTOR", 6, cl_package_cl);
    TYPE_SYM_SIMPLE_ARRAY      = cl_intern_in("SIMPLE-ARRAY", 12, cl_package_cl);
    TYPE_SYM_ARRAY             = cl_intern_in("ARRAY", 5, cl_package_cl);
    TYPE_SYM_STRING            = cl_intern_in("STRING", 6, cl_package_cl);
    TYPE_SYM_SIMPLE_STRING     = cl_intern_in("SIMPLE-STRING", 13, cl_package_cl);
    TYPE_SYM_BASE_STRING       = cl_intern_in("BASE-STRING", 11, cl_package_cl);
    TYPE_SYM_SIMPLE_BASE_STRING = cl_intern_in("SIMPLE-BASE-STRING", 18, cl_package_cl);
    TYPE_SYM_BIT_VECTOR        = cl_intern_in("BIT-VECTOR", 10, cl_package_cl);
    TYPE_SYM_SIMPLE_BIT_VECTOR = cl_intern_in("SIMPLE-BIT-VECTOR", 17, cl_package_cl);

    defun("TYPE-OF", bi_type_of, 1, 1);
    defun("TYPEP", bi_typep, 2, 2);
    defun("COERCE", bi_coerce, 2, 2);
    defun("SUBTYPEP", bi_subtypep, 2, 2);

    /* Register cached symbols for GC compaction forwarding */
    cl_gc_register_root(&TYPE_SYM_OR);
    cl_gc_register_root(&TYPE_SYM_AND);
    cl_gc_register_root(&TYPE_SYM_NOT);
    cl_gc_register_root(&TYPE_SYM_MEMBER);
    cl_gc_register_root(&TYPE_SYM_EQL);
    cl_gc_register_root(&TYPE_SYM_SATISFIES);
    cl_gc_register_root(&TYPE_SYM_INTEGER);
    cl_gc_register_root(&TYPE_SYM_RATIONAL);
    cl_gc_register_root(&TYPE_SYM_REAL);
    cl_gc_register_root(&TYPE_SYM_FLOAT);
    cl_gc_register_root(&TYPE_SYM_SINGLE_FLOAT);
    cl_gc_register_root(&TYPE_SYM_SHORT_FLOAT);
    cl_gc_register_root(&TYPE_SYM_DOUBLE_FLOAT);
    cl_gc_register_root(&TYPE_SYM_LONG_FLOAT);
    cl_gc_register_root(&TYPE_SYM_FIXNUM);
    cl_gc_register_root(&TYPE_SYM_BIGNUM);
    cl_gc_register_root(&TYPE_SYM_RATIO);
    cl_gc_register_root(&TYPE_SYM_NUMBER);
    cl_gc_register_root(&TYPE_SYM_STAR);
    cl_gc_register_root(&TYPE_SYM_SIMPLE_VECTOR);
    cl_gc_register_root(&TYPE_SYM_VECTOR);
    cl_gc_register_root(&TYPE_SYM_SIMPLE_ARRAY);
    cl_gc_register_root(&TYPE_SYM_ARRAY);
    cl_gc_register_root(&TYPE_SYM_STRING);
    cl_gc_register_root(&TYPE_SYM_SIMPLE_STRING);
    cl_gc_register_root(&TYPE_SYM_BASE_STRING);
    cl_gc_register_root(&TYPE_SYM_SIMPLE_BASE_STRING);
    cl_gc_register_root(&TYPE_SYM_BIT_VECTOR);
    cl_gc_register_root(&TYPE_SYM_SIMPLE_BIT_VECTOR);
}
