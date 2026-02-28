#ifndef CL_SYMBOL_H
#define CL_SYMBOL_H

#include "types.h"

/*
 * Symbol interning with hash table.
 * Symbols are interned into packages; lookup is by string name.
 */

#define CL_SYMTAB_SIZE 256  /* Hash table buckets per package */

/* Hash a string */
uint32_t cl_hash_string(const char *str, uint32_t len);

/* Intern a symbol in the current package */
CL_Obj cl_intern(const char *name, uint32_t len);

/* Intern into a specific package */
CL_Obj cl_intern_in(const char *name, uint32_t len, CL_Obj package);

/* Find without creating */
CL_Obj cl_find_symbol(const char *name, uint32_t len, CL_Obj package);

/* Intern a keyword (in KEYWORD package, self-evaluating) */
CL_Obj cl_intern_keyword(const char *name, uint32_t len);

/* Get symbol name as C string (pointer into CL_String data) */
const char *cl_symbol_name(CL_Obj sym);

/* Initialize symbol system */
void cl_symbol_init(void);

/* Well-known symbols (set during init) */
extern CL_Obj SYM_T;
extern CL_Obj SYM_NIL;
extern CL_Obj SYM_QUOTE;
extern CL_Obj SYM_IF;
extern CL_Obj SYM_PROGN;
extern CL_Obj SYM_LAMBDA;
extern CL_Obj SYM_LET;
extern CL_Obj SYM_LETSTAR;
extern CL_Obj SYM_SETQ;
extern CL_Obj SYM_DEFUN;
extern CL_Obj SYM_DEFMACRO;
extern CL_Obj SYM_FUNCTION;
extern CL_Obj SYM_BLOCK;
extern CL_Obj SYM_RETURN_FROM;
extern CL_Obj SYM_QUASIQUOTE;
extern CL_Obj SYM_UNQUOTE;
extern CL_Obj SYM_UNQUOTE_SPLICING;
extern CL_Obj SYM_AMP_REST;
extern CL_Obj SYM_AMP_OPTIONAL;
extern CL_Obj SYM_AMP_BODY;
extern CL_Obj SYM_AND;
extern CL_Obj SYM_OR;
extern CL_Obj SYM_COND;
extern CL_Obj SYM_DO;
extern CL_Obj SYM_DOLIST;
extern CL_Obj SYM_DOTIMES;
extern CL_Obj SYM_RETURN;
extern CL_Obj SYM_CASE;
extern CL_Obj SYM_ECASE;
extern CL_Obj SYM_TYPECASE;
extern CL_Obj SYM_ETYPECASE;
extern CL_Obj SYM_OTHERWISE;
extern CL_Obj SYM_FLET;
extern CL_Obj SYM_LABELS;
extern CL_Obj SYM_AMP_KEY;
extern CL_Obj SYM_AMP_ALLOW_OTHER_KEYS;
extern CL_Obj KW_ALLOW_OTHER_KEYS;
extern CL_Obj SYM_TAGBODY;
extern CL_Obj SYM_GO;
extern CL_Obj SYM_CATCH;
extern CL_Obj SYM_THROW;
extern CL_Obj SYM_UNWIND_PROTECT;

#endif /* CL_SYMBOL_H */
