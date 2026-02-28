#ifndef CL_PACKAGE_H
#define CL_PACKAGE_H

#include "types.h"

/*
 * CL packages: CL, CL-USER, KEYWORD
 */

extern CL_Obj cl_package_cl;       /* COMMON-LISP package */
extern CL_Obj cl_package_cl_user;  /* COMMON-LISP-USER package */
extern CL_Obj cl_package_keyword;  /* KEYWORD package */
extern CL_Obj cl_current_package;  /* *PACKAGE* */

/* Initialize the three standard packages */
void cl_package_init(void);

/* Create a new package */
CL_Obj cl_make_package(const char *name);

/* Add a symbol to a package's symbol table */
void cl_package_add_symbol(CL_Obj package, CL_Obj symbol);

/* Look up a symbol in a package (and its use-list) */
CL_Obj cl_package_find_symbol(const char *name, uint32_t len, CL_Obj package);

#endif /* CL_PACKAGE_H */
