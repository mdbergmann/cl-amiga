#ifndef CL_REPL_H
#define CL_REPL_H

/*
 * Read-Eval-Print Loop
 */

/* Run interactive REPL (returns on EOF or quit) */
void cl_repl(void);

/* Run batch REPL: no prompts, no result echo, comments skipped */
void cl_repl_batch(void);

/* Eval a single string (for testing) */
#include "types.h"
CL_Obj cl_eval_string(const char *str);

void cl_repl_init(void);

/* Compute net parenthesis depth, skipping strings/comments/char literals */
int cl_paren_depth(const char *str);

/* Update REPL history variables (*, **, ***, +, ++, +++).
 * Called after each successful REPL eval. Exposed for testing. */
void cl_repl_update_history(CL_Obj form, CL_Obj result);

#endif /* CL_REPL_H */
