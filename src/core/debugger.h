#ifndef CL_DEBUGGER_H
#define CL_DEBUGGER_H

#include "types.h"

/* 1 = debugger enabled (interactive REPL), 0 = disabled (batch/tests) */
extern int cl_debugger_enabled;

/* Maximum nesting of cl_invoke_debugger before it forces a return to top
 * level instead of recursing further (guards against a re-signalling
 * *debugger-hook* / restart recursing the C stack into a SIGSEGV). */
#define CL_DEBUGGER_MAX_DEPTH 32

/* Initialize debugger subsystem (intern symbols, register builtins) */
void cl_debugger_init(void);

/* Invoke the interactive debugger for a condition.
 * Returns if user selects "return to top level". */
void cl_invoke_debugger(CL_Obj condition);

#endif /* CL_DEBUGGER_H */
