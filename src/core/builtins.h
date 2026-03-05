#ifndef CL_BUILTINS_H
#define CL_BUILTINS_H

#include "types.h"

/*
 * Built-in CL functions implemented in C.
 * Registered into the CL package during initialization.
 */

void cl_builtins_init(void);

/* Coerce a function designator (function or symbol) to a callable function.
   If obj is already a function/closure/bytecode, returns it unchanged.
   If obj is a symbol, returns its function binding.
   Otherwise signals an error with the given context string. */
CL_Obj cl_coerce_funcdesig(CL_Obj obj, const char *context);

#endif /* CL_BUILTINS_H */
