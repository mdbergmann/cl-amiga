#ifndef CL_REPL_H
#define CL_REPL_H

/*
 * Read-Eval-Print Loop
 */

#include "types.h"

/* Run interactive REPL (returns on EOF or quit) */
void cl_repl(void);

/* Run batch REPL: no prompts, no result echo, comments skipped */
void cl_repl_batch(void);

/* Eval a single string (for testing) */
CL_Obj cl_eval_string(const char *str);

/* Load a file by path (for --load/--script) */
void cl_load_file(const char *path);

/* Init: load boot.lisp, optionally user init, set up history symbols */
void cl_repl_init(void);
void cl_repl_init_no_userinit(int no_userinit);

/* Compute net parenthesis depth, skipping strings/comments/char literals */
int cl_paren_depth(const char *str);

/* Update REPL history variables (*, **, ***, +, ++, +++).
 * Called after each successful REPL eval. Exposed for testing. */
void cl_repl_update_history(CL_Obj form, CL_Obj result);

#endif /* CL_REPL_H */
