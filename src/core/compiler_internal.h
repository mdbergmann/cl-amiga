#ifndef COMPILER_INTERNAL_H
#define COMPILER_INTERNAL_H

/*
 * Internal header shared between compiler translation units.
 * Not part of the public API — use compiler.h for that.
 */

#include "compiler.h"
#include "env.h"
#include "opcodes.h"
#include "symbol.h"
#include "package.h"
#include "mem.h"
#include "float.h"
#include "vm.h"
#include "error.h"
#include "vm.h"
#include "debugger.h"
#include "reader.h"
#include "../platform/platform.h"
#include "printer.h"
#include <string.h>
#include <stdio.h>

/* Block tracking for return/return-from */
#define CL_MAX_BLOCKS 16
#define CL_MAX_BLOCK_PATCHES 16

typedef struct {
    CL_Obj tag;
    int exit_patches[CL_MAX_BLOCK_PATCHES];
    int n_patches;
    int result_slot;  /* local slot where return value is stored */
    int uses_nlx;     /* 1 if NLX-based (compile_block), 0 if local-jump (loop forms) */
    int dyn_depth;    /* special binding depth at block entry (for local-jump unwinding) */
} CL_BlockInfo;

/* Tagbody tracking for go */
#define CL_MAX_TAGBODY_TAGS 32

typedef struct {
    CL_Obj tag;
    int code_pos;          /* bytecode offset, -1 if forward */
    int forward_patches[CL_MAX_BLOCK_PATCHES];
    int n_forward;
} CL_TagInfo;

typedef struct {
    CL_TagInfo tags[CL_MAX_TAGBODY_TAGS];
    int n_tags;
    CL_Obj id;             /* unique tagbody identifier (for NLX) */
    int uses_nlx;          /* 1 if tagbody needs NLX support */
} CL_TagbodyInfo;

/* Outer tagbody tag info for cross-closure GO */
typedef struct {
    CL_Obj tag;            /* the go tag (symbol/integer) */
    CL_Obj tagbody_id;     /* identifier of the enclosing tagbody */
    int tag_index;         /* index of the tag within the tagbody */
} CL_OuterTagInfo;

/* Source line map tracking during compilation */
#define CL_MAX_LINE_ENTRIES 256

/* Parsed lambda list structure */
typedef struct {
    CL_Obj required[CL_MAX_LOCALS];
    int n_required;
    CL_Obj opt_names[CL_MAX_LOCALS];
    CL_Obj opt_defaults[CL_MAX_LOCALS]; /* CL_NIL if no default */
    CL_Obj opt_suppliedp[CL_MAX_LOCALS]; /* supplied-p var or CL_NIL */
    int n_optional;
    CL_Obj rest_name;       /* CL_NIL if no &rest */
    int has_rest;
    CL_Obj key_names[CL_MAX_LOCALS];
    CL_Obj key_keywords[CL_MAX_LOCALS]; /* keyword symbols */
    CL_Obj key_defaults[CL_MAX_LOCALS];
    CL_Obj key_suppliedp[CL_MAX_LOCALS]; /* supplied-p var or CL_NIL */
    int n_keys;
    int allow_other_keys;
    CL_Obj aux_names[CL_MAX_LOCALS];
    CL_Obj aux_inits[CL_MAX_LOCALS]; /* init form or CL_NIL */
    int n_aux;
} CL_ParsedLambdaList;

/* Compiler state */
typedef struct CL_Compiler_s {
    struct CL_Compiler_s *parent; /* for GC root chain */
    uint8_t code[CL_MAX_CODE_SIZE];
    CL_Obj constants[CL_MAX_CONSTANTS];
    int code_pos;
    int const_count;
    CL_CompEnv *env;
    int in_tail;      /* Are we in tail position? */
    int special_depth; /* Current number of active dynamic bindings */
    CL_BlockInfo blocks[CL_MAX_BLOCKS];
    int block_count;
    CL_TagbodyInfo tagbodies[CL_MAX_BLOCKS];
    int tagbody_count;
    /* Source location tracking */
    CL_LineEntry line_entries[CL_MAX_LINE_ENTRIES];
    int line_entry_count;
    int current_line;   /* Current source line being compiled */
    /* Outer block names visible from enclosing scopes (for cross-closure return-from) */
    CL_Obj outer_blocks[CL_MAX_BLOCKS];
    int outer_block_count;
    /* Outer tagbody tags visible from enclosing scopes (for cross-closure go) */
    CL_OuterTagInfo outer_tags[CL_MAX_BLOCKS * 4];
    int outer_tag_count;
    /* Lambda compilation scratch (in struct to avoid stack overflow on AmigaOS) */
    CL_ParsedLambdaList ll;
    int key_slot_indices[CL_MAX_LOCALS];
    int key_suppliedp_indices[CL_MAX_LOCALS];
    CL_Obj param_vars[CL_MAX_LOCALS];
    int param_slots[CL_MAX_LOCALS];
    uint8_t lambda_needs_boxing[CL_MAX_LOCALS];
} CL_Compiler;

/* --- Shared globals (defined in compiler.c) --- */

extern CL_Obj macro_table;
extern CL_Obj setf_table;
extern CL_Obj setf_fn_table;
extern CL_Obj setf_expander_table;
extern CL_Obj type_table;
extern CL_Obj SETF_SYM_CAR, SETF_SYM_CDR, SETF_SYM_FIRST, SETF_SYM_REST;
extern CL_Obj SETF_SYM_NTH, SETF_SYM_AREF, SETF_SYM_SVREF, SETF_SYM_CHAR, SETF_SYM_SCHAR;
extern CL_Obj SETF_SYM_SYMBOL_VALUE, SETF_SYM_SYMBOL_FUNCTION, SETF_SYM_FDEFINITION;
extern CL_Obj SETF_HELPER_NTH, SETF_HELPER_SV, SETF_HELPER_SF;
extern CL_Obj SETF_SYM_GETHASH, SETF_HELPER_GETHASH;
extern CL_Obj SETF_HELPER_AREF;
extern CL_Obj SETF_SYM_ROW_MAJOR_AREF, SETF_HELPER_ROW_MAJOR_AREF;
extern CL_Obj SETF_SYM_FILL_POINTER, SETF_HELPER_FILL_POINTER;
extern CL_Obj SETF_SYM_BIT, SETF_HELPER_BIT;
extern CL_Obj SETF_SYM_SBIT, SETF_HELPER_SBIT;
extern CL_Obj SETF_SYM_GET, SETF_HELPER_GET;

/* --- Emit helpers (defined in compiler.c) --- */

void cl_emit(CL_Compiler *c, uint8_t byte);
void cl_emit_u16(CL_Compiler *c, uint16_t val);
void cl_emit_i16(CL_Compiler *c, int16_t val);
void cl_emit_i32(CL_Compiler *c, int32_t val);
int cl_add_constant(CL_Compiler *c, CL_Obj obj);
void cl_emit_const(CL_Compiler *c, CL_Obj obj);
int cl_emit_jump(CL_Compiler *c, uint8_t op);
void cl_patch_jump(CL_Compiler *c, int patch_pos);
void cl_emit_loop_jump(CL_Compiler *c, uint8_t op, int target);

/* --- Compile functions shared across files --- */

/* compiler.c */
void compile_expr(CL_Compiler *c, CL_Obj expr);
void compile_body(CL_Compiler *c, CL_Obj forms);
void compile_progn(CL_Compiler *c, CL_Obj forms);
void compile_lambda(CL_Compiler *c, CL_Obj form);
int alloc_temp_slot(CL_CompEnv *env);

/* compiler_special.c */
void compile_block(CL_Compiler *c, CL_Obj form);
void compile_return_from(CL_Compiler *c, CL_Obj form);
void compile_return(CL_Compiler *c, CL_Obj form);
void compile_tagbody(CL_Compiler *c, CL_Obj form);
void compile_go(CL_Compiler *c, CL_Obj form);
void compile_catch(CL_Compiler *c, CL_Obj form);
void compile_unwind_protect(CL_Compiler *c, CL_Obj form);
void compile_dolist(CL_Compiler *c, CL_Obj form);
void compile_dotimes(CL_Compiler *c, CL_Obj form);
void compile_do(CL_Compiler *c, CL_Obj form);
void compile_do_star(CL_Compiler *c, CL_Obj form);
void compile_flet(CL_Compiler *c, CL_Obj form);
void compile_labels(CL_Compiler *c, CL_Obj form);
void compile_destructuring_bind(CL_Compiler *c, CL_Obj form);
void compile_handler_bind(CL_Compiler *c, CL_Obj form);
void compile_restart_case(CL_Compiler *c, CL_Obj form);
void compile_macrolet(CL_Compiler *c, CL_Obj form);
void compile_symbol_macrolet(CL_Compiler *c, CL_Obj form);
void compile_progv(CL_Compiler *c, CL_Obj form);

/* compiler_extra.c */
void compile_and(CL_Compiler *c, CL_Obj form);
void compile_or(CL_Compiler *c, CL_Obj form);
void compile_cond(CL_Compiler *c, CL_Obj form);
void compile_quasiquote(CL_Compiler *c, CL_Obj form);
void compile_case(CL_Compiler *c, CL_Obj form, int error_if_no_match);
void compile_typecase(CL_Compiler *c, CL_Obj form, int error_if_no_match);
void compile_multiple_value_bind(CL_Compiler *c, CL_Obj form);
void compile_multiple_value_call(CL_Compiler *c, CL_Obj form);
void compile_multiple_value_list(CL_Compiler *c, CL_Obj form);
void compile_nth_value(CL_Compiler *c, CL_Obj form);
void compile_multiple_value_prog1(CL_Compiler *c, CL_Obj form);
void compile_eval_when(CL_Compiler *c, CL_Obj form);
void compile_load_time_value(CL_Compiler *c, CL_Obj form);
void compile_defsetf(CL_Compiler *c, CL_Obj form);
void compile_deftype(CL_Compiler *c, CL_Obj form);
void compile_defvar(CL_Compiler *c, CL_Obj form);
void compile_defparameter(CL_Compiler *c, CL_Obj form);
void compile_defconstant(CL_Compiler *c, CL_Obj form);
void compile_defun(CL_Compiler *c, CL_Obj form);
void compile_defmacro(CL_Compiler *c, CL_Obj form);
CL_Obj defmacro_gensym(void);
int defmacro_is_ll_keyword(CL_Obj param);
int defmacro_needs_destructuring(CL_Obj ll);
void compile_declaim(CL_Compiler *c, CL_Obj form);
void compile_locally(CL_Compiler *c, CL_Obj form);
void compile_trace(CL_Compiler *c, CL_Obj form);
void compile_untrace(CL_Compiler *c, CL_Obj form);
void compile_time(CL_Compiler *c, CL_Obj form);
void compile_in_package(CL_Compiler *c, CL_Obj form);
void compile_the(CL_Compiler *c, CL_Obj form);

/* Declaration processing helpers */
CL_Obj process_body_declarations(CL_Compiler *c, CL_Obj body);
CL_Obj scan_local_specials(CL_Obj body);
int is_locally_special(CL_Obj var, CL_Obj local_specials);

/* Boxing analysis for mutable closure bindings */
void determine_boxed_vars(CL_Obj body, CL_Obj *vars, int n_vars,
                          uint8_t *boxed_out);
void scan_body_for_boxing(CL_Obj form, CL_Obj *vars, int n_vars,
                          uint8_t *mutated, uint8_t *captured,
                          int closure_depth);

#endif /* COMPILER_INTERNAL_H */
