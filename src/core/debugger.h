#ifndef CL_DEBUGGER_H
#define CL_DEBUGGER_H

#include "types.h"

/* 1 = debugger enabled (interactive REPL), 0 = disabled (batch/tests) */
extern int cl_debugger_enabled;

/* Initialize debugger subsystem (intern symbols, register builtins) */
void cl_debugger_init(void);

/* Invoke the interactive debugger for a condition.
 * Returns if user selects "return to top level". */
void cl_invoke_debugger(CL_Obj condition);

#endif /* CL_DEBUGGER_H */
