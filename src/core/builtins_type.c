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
#include "error.h"
#include "vm.h"
#include "compiler.h"
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

/* Forward declaration */
static int typep_check(CL_Obj obj, CL_Obj type_spec);

/* --- typep for simple symbol type specifiers --- */

static int typep_symbol(CL_Obj obj, CL_Obj type_sym)
{
    const char *tname = cl_symbol_name(type_sym);

    if (strcmp(tname, "T") == 0)              return 1;
    if (strcmp(tname, "NIL") == 0)            return 0;
    if (strcmp(tname, "NULL") == 0)           return CL_NULL_P(obj);
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
    if (strcmp(tname, "RATIONAL") == 0) return CL_INTEGER_P(obj);
    if (strcmp(tname, "SINGLE-FLOAT") == 0 || strcmp(tname, "SHORT-FLOAT") == 0)
        return CL_SINGLE_FLOAT_P(obj);
    if (strcmp(tname, "DOUBLE-FLOAT") == 0 || strcmp(tname, "LONG-FLOAT") == 0)
        return CL_DOUBLE_FLOAT_P(obj);
    if (strcmp(tname, "FLOAT") == 0)  return CL_FLOATP(obj);
    if (strcmp(tname, "REAL") == 0)   return CL_REALP(obj);
    if (strcmp(tname, "NUMBER") == 0) return CL_NUMBER_P(obj);
    if (strcmp(tname, "CHARACTER") == 0)      return CL_CHAR_P(obj);
    if (strcmp(tname, "STRING") == 0)         return CL_STRING_P(obj);
    if (strcmp(tname, "VECTOR") == 0 || strcmp(tname, "SIMPLE-VECTOR") == 0 ||
        strcmp(tname, "ARRAY") == 0 || strcmp(tname, "SIMPLE-ARRAY") == 0)
        return CL_STRING_P(obj) || CL_VECTOR_P(obj);
    if (strcmp(tname, "SEQUENCE") == 0)
        return CL_NULL_P(obj) || CL_CONS_P(obj) || CL_STRING_P(obj) || CL_VECTOR_P(obj);
    if (strcmp(tname, "FUNCTION") == 0)
        return CL_FUNCTION_P(obj) || CL_CLOSURE_P(obj) || CL_BYTECODE_P(obj);
    if (strcmp(tname, "COMPILED-FUNCTION") == 0)
        return CL_CLOSURE_P(obj) || CL_BYTECODE_P(obj);
    if (strcmp(tname, "HASH-TABLE") == 0)     return CL_HASHTABLE_P(obj);
    if (strcmp(tname, "PACKAGE") == 0)        return CL_PACKAGE_P(obj);
    if (strcmp(tname, "STREAM") == 0)        return CL_STREAM_P(obj);

    /* Structure types — check hierarchy for struct objects */
    if (strcmp(tname, "STRUCTURE") == 0 || strcmp(tname, "STRUCTURE-OBJECT") == 0)
        return CL_STRUCT_P(obj);
    {
        extern int cl_is_struct_type(CL_Obj type_sym);
        extern int cl_struct_type_matches(CL_Obj obj_type, CL_Obj test_type);
        if (cl_is_struct_type(type_sym)) {
            if (!CL_STRUCT_P(obj)) return 0;
            {
                CL_Struct *st = (CL_Struct *)CL_OBJ_TO_PTR(obj);
                return cl_struct_type_matches(st->type_desc, type_sym);
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

    cl_error(CL_ERR_TYPE, "TYPEP: unknown type specifier");
    return 0;
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
                pred_fn = pred_sym->value;
                if (CL_NULL_P(pred_fn) || pred_fn == CL_UNBOUND)
                    cl_error(CL_ERR_UNDEFINED, "TYPEP: undefined predicate %s",
                             cl_symbol_name(pred_name));
            }
            result = cl_vm_apply(pred_fn, &obj, 1);
            return !CL_NULL_P(result);
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

        cl_error(CL_ERR_TYPE, "TYPEP: invalid compound type specifier");
    }

    cl_error(CL_ERR_TYPE, "TYPEP: invalid type specifier");
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

    /* (coerce x 'string) */
    if (strcmp(tname, "STRING") == 0) {
        if (CL_STRING_P(obj)) return obj;
        if (CL_NULL_P(obj)) return cl_make_string("NIL", 3);
        if (CL_SYMBOL_P(obj)) {
            const char *sname = cl_symbol_name(obj);
            return cl_make_string(sname, (uint32_t)strlen(sname));
        }
        if (CL_CHAR_P(obj)) {
            char c = (char)CL_CHAR_VAL(obj);
            return cl_make_string(&c, 1);
        }
        cl_error(CL_ERR_TYPE, "COERCE: cannot coerce to STRING");
        return CL_NIL;
    }

    /* (coerce x 'list) */
    if (strcmp(tname, "LIST") == 0) {
        if (CL_NULL_P(obj) || CL_CONS_P(obj)) return obj;
        if (CL_VECTOR_P(obj)) {
            CL_Vector *v = (CL_Vector *)CL_OBJ_TO_PTR(obj);
            CL_Obj result = CL_NIL;
            uint32_t i = v->length;
            while (i > 0) {
                i--;
                result = cl_cons(v->data[i], result);
            }
            return result;
        }
        if (CL_STRING_P(obj)) {
            CL_String *s = (CL_String *)CL_OBJ_TO_PTR(obj);
            CL_Obj result = CL_NIL;
            uint32_t i = s->length;
            while (i > 0) {
                i--;
                result = cl_cons(CL_MAKE_CHAR((unsigned char)s->data[i]), result);
            }
            return result;
        }
        cl_error(CL_ERR_TYPE, "COERCE: cannot coerce to LIST");
        return CL_NIL;
    }

    /* (coerce x 'vector) */
    if (strcmp(tname, "VECTOR") == 0) {
        if (CL_VECTOR_P(obj)) return obj;
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
                v->data[i] = cl_car(p);
                p = cl_cdr(p);
            }
            return vec;
        }
        cl_error(CL_ERR_TYPE, "COERCE: cannot coerce to VECTOR");
        return CL_NIL;
    }

    cl_error(CL_ERR_TYPE, "COERCE: unknown result type");
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
    TID_T,
    TID_COUNT
};

static int type_name_to_id(const char *name)
{
    if (strcmp(name, "NIL") == 0) return TID_NIL;
    if (strcmp(name, "NULL") == 0) return TID_NULL;
    if (strcmp(name, "FIXNUM") == 0) return TID_FIXNUM;
    if (strcmp(name, "BIGNUM") == 0) return TID_BIGNUM;
    if (strcmp(name, "INTEGER") == 0) return TID_INTEGER;
    if (strcmp(name, "RATIONAL") == 0) return TID_RATIONAL;
    if (strcmp(name, "REAL") == 0) return TID_REAL;
    if (strcmp(name, "NUMBER") == 0) return TID_NUMBER;
    if (strcmp(name, "CHARACTER") == 0) return TID_CHARACTER;
    if (strcmp(name, "KEYWORD") == 0) return TID_KEYWORD;
    if (strcmp(name, "SYMBOL") == 0) return TID_SYMBOL;
    if (strcmp(name, "CONS") == 0) return TID_CONS;
    if (strcmp(name, "LIST") == 0) return TID_LIST;
    if (strcmp(name, "STRING") == 0) return TID_STRING;
    if (strcmp(name, "SIMPLE-VECTOR") == 0) return TID_SIMPLE_VECTOR;
    if (strcmp(name, "VECTOR") == 0) return TID_VECTOR;
    if (strcmp(name, "ARRAY") == 0 || strcmp(name, "SIMPLE-ARRAY") == 0) return TID_ARRAY;
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
    if (strcmp(name, "T") == 0) return TID_T;
    return TID_UNKNOWN;
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

    /* Symbol hierarchy: keyword < symbol, null < symbol */
    if (id1 == TID_KEYWORD && id2 == TID_SYMBOL) return 1;

    /* Vector hierarchy: simple-vector < vector < array, vector < sequence */
    if (id1 == TID_SIMPLE_VECTOR && (id2 == TID_VECTOR || id2 == TID_ARRAY || id2 == TID_SEQUENCE)) return 1;
    if (id1 == TID_VECTOR && (id2 == TID_ARRAY || id2 == TID_SEQUENCE)) return 1;
    if (id1 == TID_STRING && (id2 == TID_VECTOR || id2 == TID_ARRAY || id2 == TID_SEQUENCE)) return 1;

    /* Function hierarchy: compiled-function < function */
    if (id1 == TID_COMPILED_FUNCTION && id2 == TID_FUNCTION) return 1;

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

    return 0;
}

static CL_Obj bi_subtypep(CL_Obj *args, int n)
{
    CL_Obj type1 = args[0];
    CL_Obj type2 = args[1];
    int id1, id2, result;
    CL_UNUSED(n);

    /* Only handle symbol type specifiers for now */
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

    defun("TYPE-OF", bi_type_of, 1, 1);
    defun("TYPEP", bi_typep, 2, 2);
    defun("COERCE", bi_coerce, 2, 2);
    defun("SUBTYPEP", bi_subtypep, 2, 2);
}
