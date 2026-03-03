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

/* Create an uninterned symbol (no home package) */
CL_Obj cl_make_uninterned_symbol(CL_Obj name_str);

/* Get symbol name as C string (pointer into CL_String data) */
const char *cl_symbol_name(CL_Obj sym);

/* Check if symbol is declared special (by defvar/defparameter) */
int cl_symbol_specialp(CL_Obj sym);

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
extern CL_Obj SYM_MULTIPLE_VALUE_BIND;
extern CL_Obj SYM_MULTIPLE_VALUE_LIST;
extern CL_Obj SYM_MULTIPLE_VALUE_PROG1;
extern CL_Obj SYM_NTH_VALUE;
extern CL_Obj SYM_DEFVAR;
extern CL_Obj SYM_DEFPARAMETER;
extern CL_Obj SYM_DEFCONSTANT;
extern CL_Obj SYM_SETF;
extern CL_Obj SYM_EVAL_WHEN;
extern CL_Obj SYM_DESTRUCTURING_BIND;
extern CL_Obj SYM_DEFSETF;
extern CL_Obj SYM_DEFTYPE;
extern CL_Obj SYM_DECLARE;
extern CL_Obj SYM_DECLAIM;
extern CL_Obj SYM_LOCALLY;
extern CL_Obj SYM_TRACE;
extern CL_Obj SYM_UNTRACE;
extern CL_Obj SYM_TIME;
extern CL_Obj SYM_IN_PACKAGE;
extern CL_Obj SYM_STAR_PACKAGE;
extern CL_Obj SYM_MACROLET;
extern CL_Obj SYM_SYMBOL_MACROLET;
extern CL_Obj SYM_THE;

/* Declaration specifier symbols */
extern CL_Obj SYM_SPECIAL_DECL;
extern CL_Obj SYM_TYPE_DECL;
extern CL_Obj SYM_FTYPE_DECL;
extern CL_Obj SYM_INLINE_DECL;
extern CL_Obj SYM_NOTINLINE_DECL;
extern CL_Obj SYM_OPTIMIZE_DECL;
extern CL_Obj SYM_IGNORE_DECL;
extern CL_Obj SYM_IGNORABLE_DECL;
extern CL_Obj SYM_DYNAMIC_EXTENT_DECL;

/* Optimize quality symbols */
extern CL_Obj SYM_SPEED;
extern CL_Obj SYM_SAFETY;
extern CL_Obj SYM_DEBUG;
extern CL_Obj SYM_SPACE;

/* Condition type name symbols */
extern CL_Obj SYM_CONDITION;
extern CL_Obj SYM_WARNING;
extern CL_Obj SYM_SERIOUS_CONDITION;
extern CL_Obj SYM_ERROR_COND;
extern CL_Obj SYM_SIMPLE_CONDITION;
extern CL_Obj SYM_SIMPLE_ERROR;
extern CL_Obj SYM_SIMPLE_WARNING;
extern CL_Obj SYM_TYPE_ERROR;
extern CL_Obj SYM_UNBOUND_VARIABLE_COND;
extern CL_Obj SYM_UNDEFINED_FUNCTION_COND;
extern CL_Obj SYM_PROGRAM_ERROR;
extern CL_Obj SYM_CONTROL_ERROR;
extern CL_Obj SYM_ARITHMETIC_ERROR;
extern CL_Obj SYM_DIVISION_BY_ZERO;

/* Signaling symbols */
extern CL_Obj SYM_SIGNAL;
extern CL_Obj SYM_WARN;
extern CL_Obj SYM_HANDLER_BIND;
extern CL_Obj SYM_RESTART_CASE;

/* Restart name symbols */
extern CL_Obj SYM_ABORT;
extern CL_Obj SYM_CONTINUE;
extern CL_Obj SYM_MUFFLE_WARNING;
extern CL_Obj SYM_USE_VALUE;
extern CL_Obj SYM_STORE_VALUE;

/* Condition keyword symbols */
extern CL_Obj KW_FORMAT_CONTROL;
extern CL_Obj KW_FORMAT_ARGUMENTS;
extern CL_Obj KW_DATUM;
extern CL_Obj KW_EXPECTED_TYPE;

/* REPL history symbols */
extern CL_Obj SYM_STAR;          /* * — last result */
extern CL_Obj SYM_STARSTAR;      /* ** — second-to-last result */
extern CL_Obj SYM_STARSTARSTAR;  /* *** — third-to-last result */
extern CL_Obj SYM_PLUS;          /* + — last form */
extern CL_Obj SYM_PLUSPLUS;       /* ++ — second-to-last form */
extern CL_Obj SYM_PLUSPLUSPLUS;  /* +++ — third-to-last form */
extern CL_Obj SYM_MINUS;         /* - — current form being evaluated */

/* Standard stream variables */
extern CL_Obj SYM_STANDARD_INPUT;    /* *STANDARD-INPUT* */
extern CL_Obj SYM_STANDARD_OUTPUT;   /* *STANDARD-OUTPUT* */
extern CL_Obj SYM_ERROR_OUTPUT;      /* *ERROR-OUTPUT* */
extern CL_Obj SYM_TRACE_OUTPUT;      /* *TRACE-OUTPUT* */
extern CL_Obj SYM_DEBUG_IO;          /* *DEBUG-IO* */
extern CL_Obj SYM_QUERY_IO;          /* *QUERY-IO* */
extern CL_Obj SYM_TERMINAL_IO;       /* *TERMINAL-IO* */

/* Feature system */
extern CL_Obj SYM_STAR_FEATURES;     /* *FEATURES* */

/* Readtable */
extern CL_Obj SYM_STAR_READTABLE;    /* *READTABLE* */

/* Feature keywords */
extern CL_Obj KW_CL_AMIGA;           /* :CL-AMIGA */
extern CL_Obj KW_COMMON_LISP;        /* :COMMON-LISP */
extern CL_Obj KW_POSIX;              /* :POSIX */
extern CL_Obj KW_AMIGAOS;            /* :AMIGAOS */
extern CL_Obj KW_M68K;               /* :M68K */

#endif /* CL_SYMBOL_H */
