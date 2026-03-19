#ifndef CL_PACKAGE_H
#define CL_PACKAGE_H

#include "types.h"

/*
 * CL packages: CL, CL-USER, KEYWORD
 */

extern CL_Obj cl_package_cl;       /* COMMON-LISP package */
extern CL_Obj cl_package_cl_user;  /* COMMON-LISP-USER package */
extern CL_Obj cl_package_keyword;  /* KEYWORD package */
extern CL_Obj cl_package_ext;      /* EXT (implementation extensions) package */
extern CL_Obj cl_package_clamiga;  /* CLAMIGA (implementation internals) package */
extern CL_Obj cl_package_mp;      /* MP (multiprocessing) package */
extern CL_Obj cl_current_package;  /* *PACKAGE* */

/* Package registry — alist ((name-str . pkg) ...) */
extern CL_Obj cl_package_registry;

/* Thread-safety: rwlock protecting package registry and symbol tables */
extern void *cl_package_rwlock;

/* Initialize the three standard packages */
void cl_package_init(void);

/* Create a new package */
CL_Obj cl_make_package(const char *name);

/* Add a symbol to a package's symbol table */
void cl_package_add_symbol(CL_Obj package, CL_Obj symbol);

/* Look up a symbol in a package (and its use-list) */
CL_Obj cl_package_find_symbol(const char *name, uint32_t len, CL_Obj package);

/* Find only exported symbols in a package (no use-list search) */
CL_Obj cl_package_find_external(const char *name, uint32_t len, CL_Obj package);

/* Find symbol with status:
   0 = not found, 1 = :INTERNAL, 2 = :EXTERNAL, 3 = :INHERITED */
CL_Obj cl_find_symbol_with_status(const char *name, uint32_t len,
                                   CL_Obj package, int *status);

/* Find package by name or nickname */
CL_Obj cl_find_package(const char *name, uint32_t len);

/* Register package in global registry */
void cl_register_package(CL_Obj pkg);

/* Export/unexport symbols */
void cl_export_symbol(CL_Obj sym, CL_Obj package);
void cl_unexport_symbol(CL_Obj sym, CL_Obj package);

/* Import symbol into package */
void cl_import_symbol(CL_Obj sym, CL_Obj package);

/* Shadow: create internal symbol that shadows inherited one */
void cl_shadow_symbol(const char *name, uint32_t len, CL_Obj package);

/* Use/unuse package */
void cl_use_package(CL_Obj pkg_to_use, CL_Obj using_pkg);
void cl_unuse_package(CL_Obj pkg_to_unuse, CL_Obj using_pkg);

/* CDR-10: Package-local nicknames */
void cl_add_package_local_nickname(CL_Obj nick_str, CL_Obj target_pkg, CL_Obj in_pkg);
void cl_remove_package_local_nickname(const char *name, uint32_t len, CL_Obj from_pkg);

/* Export all symbols currently in CL package (called after symbol init) */
void cl_package_export_all_cl_symbols(void);

/* Export only newly-defined CL symbols that have real bindings
   (function, value, macro, type, struct, or CLOS class).
   Already-exported symbols are left unchanged. */
void cl_package_export_defined_cl_symbols(void);

#endif /* CL_PACKAGE_H */
