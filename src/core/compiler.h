#ifndef CL_COMPILER_H
#define CL_COMPILER_H

#include "types.h"

/*
 * Single-pass recursive compiler: S-expression → bytecode.
 * Handles special forms, lexical scope, upvalue capture, tail calls.
 */

#define CL_MAX_CODE_SIZE   65536
#define CL_MAX_CONSTANTS   8192

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

/* Expand one level of macro (returns form unchanged if not a macro call) */
CL_Obj cl_macroexpand_1(CL_Obj form);

/* Type expander table (for deftype) */
void cl_register_type(CL_Obj name, CL_Obj expander);
CL_Obj cl_get_type_expander(CL_Obj name);

/* Setf function table: (defun (setf accessor) ...) */
void cl_register_setf_function(CL_Obj accessor, CL_Obj setf_fn_sym);

/* Optimization settings (used by (declare (optimize ...))) */
typedef struct {
    uint8_t speed;   /* 0-3, default 1 */
    uint8_t safety;  /* 0-3, default 1 */
    uint8_t debug;   /* 0-3, default 1 */
    uint8_t space;   /* 0-3, default 1 */
} CL_OptimizeSettings;

extern CL_OptimizeSettings cl_optimize_settings;

/* Process a single declaration specifier (for proclaim/declaim) */
void cl_process_declaration_specifier(CL_Obj spec);

/* GC marking: mark all CL_Obj values referenced by active compiler(s) */
void cl_compiler_gc_mark(void);

/* Save/restore active compiler chain across non-local exits.
 * cl_compiler_mark() returns an opaque snapshot.
 * cl_compiler_restore_to() frees any compilers allocated since the mark. */
void *cl_compiler_mark(void);
void cl_compiler_restore_to(void *saved);

#endif /* CL_COMPILER_H */
