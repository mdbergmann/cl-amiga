#include "types.h"
#include "mem.h"
#include "../platform/platform.h"

#ifdef DEBUG_GC
#include "vm.h"
#include <stdio.h>
#endif

CL_Obj CL_T = CL_NIL;  /* Set properly during init by symbol/package setup */

CL_Obj cl_car(CL_Obj obj)
{
    if (CL_NULL_P(obj)) return CL_NIL;
    if (obj >= cl_heap.arena_size)
        cl_storage_error("CAR: corrupted pointer 0x%08x (arena size 0x%08x)",
                         (unsigned)obj, (unsigned)cl_heap.arena_size);
    return ((CL_Cons *)CL_OBJ_TO_PTR(obj))->car;
}

CL_Obj cl_cdr(CL_Obj obj)
{
    if (CL_NULL_P(obj)) return CL_NIL;
    if (obj >= cl_heap.arena_size)
        cl_storage_error("CDR: corrupted pointer 0x%08x (arena size 0x%08x)",
                         (unsigned)obj, (unsigned)cl_heap.arena_size);
    {
        CL_Obj result = ((CL_Cons *)CL_OBJ_TO_PTR(obj))->cdr;
#ifdef DEBUG_GC
        /* Detect use-after-free: if cdr is poison pattern, the cons was freed */
        if (result == 0xDEDEDEDEu) {
            char buf[512];
            CL_Cons *c = (CL_Cons *)CL_OBJ_TO_PTR(obj);
            int i;
            snprintf(buf, sizeof(buf),
                     "CDR-USE-AFTER-FREE: cons @0x%08x car=0x%08x cdr=POISON hdr=0x%08x\n"
                     "  VM sp=%d fp=%d\n",
                     (unsigned)obj, (unsigned)c->car, (unsigned)c->hdr.header,
                     cl_vm.sp, cl_vm.fp);
            platform_write_string(buf);
            for (i = 0; i < cl_vm.sp; i++) {
                if (cl_vm.stack[i] == obj) {
                    snprintf(buf, sizeof(buf), "  FOUND on VM stack[%d]\n", i);
                    platform_write_string(buf);
                }
            }
            for (i = 0; i < cl_vm.fp; i++) {
                snprintf(buf, sizeof(buf),
                         "  frame[%d] bp=%u n_locals=%d bytecode=0x%08x\n",
                         i, cl_vm.frames[i].bp, cl_vm.frames[i].n_locals,
                         (unsigned)cl_vm.frames[i].bytecode);
                platform_write_string(buf);
            }
            cl_capture_backtrace();
            platform_write_string(cl_backtrace_buf);
            platform_write_string("\n");
        }
#endif
        return result;
    }
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
        case TYPE_CONDITION: return "CONDITION";
        case TYPE_STRUCT:   return "STRUCTURE";
        case TYPE_BIGNUM:   return "BIGNUM";
        case TYPE_SINGLE_FLOAT: return "SINGLE-FLOAT";
        case TYPE_DOUBLE_FLOAT: return "DOUBLE-FLOAT";
        case TYPE_RATIO:    return "RATIO";
        case TYPE_COMPLEX:  return "COMPLEX";
        case TYPE_STREAM:   return "STREAM";
        case TYPE_RANDOM_STATE: return "RANDOM-STATE";
        case TYPE_BIT_VECTOR: return "BIT-VECTOR";
        case TYPE_PATHNAME: return "PATHNAME";
        case TYPE_CELL:     return "CELL";
        case TYPE_THREAD:   return "THREAD";
        case TYPE_LOCK:     return "LOCK";
        case TYPE_CONDVAR:  return "CONDITION-VARIABLE";
        default:            return "UNKNOWN";
        }
    }
    return "UNKNOWN";
}
