#ifndef CL_COMPILER_H
#define CL_COMPILER_H

#include "types.h"

/*
 * Single-pass recursive compiler: S-expression → bytecode.
 * Handles special forms, lexical scope, upvalue capture, tail calls.
 */

#define CL_MAX_CODE_SIZE   4096
#define CL_MAX_CONSTANTS   256

/* Compile a top-level expression, returns a CL_Bytecode object */
CL_Obj cl_compile(CL_Obj expr);

/* Compile a defun/defmacro form */
CL_Obj cl_compile_defun(CL_Obj name, CL_Obj lambda_list, CL_Obj body);

/* Register a macro expander */
void cl_register_macro(CL_Obj name, CL_Obj expander);

/* Check if symbol names a macro */
int cl_macro_p(CL_Obj name);

/* Get macro expander for a symbol */
CL_Obj cl_get_macro(CL_Obj name);

void cl_compiler_init(void);

#endif /* CL_COMPILER_H */
