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
#include "error.h"
#include "vm.h"
#include "reader.h"
#include "../platform/platform.h"
#include <string.h>

/* Block tracking for return/return-from */
#define CL_MAX_BLOCKS 16
#define CL_MAX_BLOCK_PATCHES 16

typedef struct {
    CL_Obj tag;
    int exit_patches[CL_MAX_BLOCK_PATCHES];
    int n_patches;
    int result_slot;  /* local slot where return value is stored */
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
} CL_TagbodyInfo;

/* Source line map tracking during compilation */
#define CL_MAX_LINE_ENTRIES 256

/* Compiler state */
typedef struct {
    uint8_t code[CL_MAX_CODE_SIZE];
    CL_Obj constants[CL_MAX_CONSTANTS];
    int code_pos;
    int const_count;
    CL_CompEnv *env;
    int in_tail;      /* Are we in tail position? */
    CL_BlockInfo blocks[CL_MAX_BLOCKS];
    int block_count;
    CL_TagbodyInfo tagbodies[CL_MAX_BLOCKS];
    int tagbody_count;
    /* Source location tracking */
    CL_LineEntry line_entries[CL_MAX_LINE_ENTRIES];
    int line_entry_count;
    int current_line;   /* Current source line being compiled */
} CL_Compiler;

/* Parsed lambda list structure */
typedef struct {
    CL_Obj required[CL_MAX_LOCALS];
    int n_required;
    CL_Obj opt_names[CL_MAX_LOCALS];
    CL_Obj opt_defaults[CL_MAX_LOCALS]; /* CL_NIL if no default */
    int n_optional;
    CL_Obj rest_name;       /* CL_NIL if no &rest */
    int has_rest;
    CL_Obj key_names[CL_MAX_LOCALS];
    CL_Obj key_keywords[CL_MAX_LOCALS]; /* keyword symbols */
    CL_Obj key_defaults[CL_MAX_LOCALS];
    int n_keys;
    int allow_other_keys;
} CL_ParsedLambdaList;

/* --- Shared globals (defined in compiler.c) --- */

extern CL_Obj macro_table;
extern CL_Obj setf_table;
extern CL_Obj type_table;
extern CL_Obj pending_lambda_name;
extern CL_Obj SETF_SYM_CAR, SETF_SYM_CDR, SETF_SYM_FIRST, SETF_SYM_REST;
extern CL_Obj SETF_SYM_NTH, SETF_SYM_AREF, SETF_SYM_SVREF;
extern CL_Obj SETF_SYM_SYMBOL_VALUE, SETF_SYM_SYMBOL_FUNCTION;
extern CL_Obj SETF_HELPER_NTH, SETF_HELPER_SV, SETF_HELPER_SF;
extern CL_Obj SETF_SYM_GETHASH, SETF_HELPER_GETHASH;
extern CL_Obj SETF_HELPER_AREF;
extern CL_Obj SETF_SYM_ROW_MAJOR_AREF, SETF_HELPER_ROW_MAJOR_AREF;
extern CL_Obj SETF_SYM_FILL_POINTER, SETF_HELPER_FILL_POINTER;

/* --- Emit helpers (defined in compiler.c) --- */

void cl_emit(CL_Compiler *c, uint8_t byte);
void cl_emit_u16(CL_Compiler *c, uint16_t val);
void cl_emit_i16(CL_Compiler *c, int16_t val);
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
void compile_flet(CL_Compiler *c, CL_Obj form);
void compile_labels(CL_Compiler *c, CL_Obj form);
void compile_destructuring_bind(CL_Compiler *c, CL_Obj form);
void compile_handler_bind(CL_Compiler *c, CL_Obj form);
void compile_restart_case(CL_Compiler *c, CL_Obj form);
void compile_macrolet(CL_Compiler *c, CL_Obj form);
void compile_symbol_macrolet(CL_Compiler *c, CL_Obj form);

/* compiler_extra.c */
void compile_and(CL_Compiler *c, CL_Obj form);
void compile_or(CL_Compiler *c, CL_Obj form);
void compile_cond(CL_Compiler *c, CL_Obj form);
void compile_quasiquote(CL_Compiler *c, CL_Obj form);
void compile_case(CL_Compiler *c, CL_Obj form, int error_if_no_match);
void compile_typecase(CL_Compiler *c, CL_Obj form, int error_if_no_match);
void compile_multiple_value_bind(CL_Compiler *c, CL_Obj form);
void compile_multiple_value_list(CL_Compiler *c, CL_Obj form);
void compile_nth_value(CL_Compiler *c, CL_Obj form);
void compile_multiple_value_prog1(CL_Compiler *c, CL_Obj form);
void compile_eval_when(CL_Compiler *c, CL_Obj form);
void compile_defsetf(CL_Compiler *c, CL_Obj form);
void compile_deftype(CL_Compiler *c, CL_Obj form);
void compile_defvar(CL_Compiler *c, CL_Obj form);
void compile_defparameter(CL_Compiler *c, CL_Obj form);
void compile_defconstant(CL_Compiler *c, CL_Obj form);
void compile_defun(CL_Compiler *c, CL_Obj form);
void compile_defmacro(CL_Compiler *c, CL_Obj form);
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

#endif /* COMPILER_INTERNAL_H */
