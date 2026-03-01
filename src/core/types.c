#include "types.h"
#include "../platform/platform.h"

CL_Obj CL_T = CL_NIL;  /* Set properly during init by symbol/package setup */

CL_Obj cl_car(CL_Obj obj)
{
    if (CL_NULL_P(obj)) return CL_NIL;
    return ((CL_Cons *)CL_OBJ_TO_PTR(obj))->car;
}

CL_Obj cl_cdr(CL_Obj obj)
{
    if (CL_NULL_P(obj)) return CL_NIL;
    return ((CL_Cons *)CL_OBJ_TO_PTR(obj))->cdr;
}

/* cl_cons is implemented in mem.c (needs allocator) */

const char *cl_type_name(CL_Obj obj)
{
    if (CL_NULL_P(obj))    return "NULL";
    if (CL_FIXNUM_P(obj))  return "FIXNUM";
    if (CL_CHAR_P(obj))    return "CHARACTER";
    if (CL_HEAP_P(obj)) {
        switch (CL_HDR_TYPE(CL_OBJ_TO_PTR(obj))) {
        case TYPE_CONS:     return "CONS";
        case TYPE_SYMBOL:   return "SYMBOL";
        case TYPE_STRING:   return "STRING";
        case TYPE_FUNCTION: return "FUNCTION";
        case TYPE_CLOSURE:  return "FUNCTION";
        case TYPE_BYTECODE: return "COMPILED-FUNCTION";
        case TYPE_VECTOR:   return "VECTOR";
        case TYPE_PACKAGE:  return "PACKAGE";
        case TYPE_HASHTABLE: return "HASH-TABLE";
        default:            return "UNKNOWN";
        }
    }
    return "UNKNOWN";
}
