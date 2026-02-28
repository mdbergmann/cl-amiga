#ifndef CL_REPL_H
#define CL_REPL_H

/*
 * Read-Eval-Print Loop
 */

/* Run interactive REPL (returns on EOF or quit) */
void cl_repl(void);

/* Eval a single string (for testing) */
#include "types.h"
CL_Obj cl_eval_string(const char *str);

void cl_repl_init(void);

#endif /* CL_REPL_H */
