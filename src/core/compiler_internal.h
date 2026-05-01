#ifndef COMPILER_INTERNAL_H
#define COMPILER_INTERNAL_H

/*
 * Internal header shared between compiler translation units.
 * Not part of the public API — use compiler.h for that.
 */

#include "compiler.h"
#include "env.h"
#include <stdlib.h>
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
#include "builtins.h"
#include <string.h>
#include <stdio.h>

/* Block tracking for return/return-from.  Trivia's MATCH macroexpansion
 * produces 30+ nested BLOCKs and serapeum dispatch-case can chain even
 * more, so this limit needs significant headroom.  256 covers the cases
 * we've seen in the wild (trivia, serapeum, cl-conspack); compile_block
 * raises a clean error on overflow rather than corrupting the adjacent
 * block_count field via OOB write. */
#define CL_MAX_BLOCKS 256
#define CL_MAX_BLOCK_PATCHES 16
/* Tagbodies and outer-tag tables don't see the same nesting depth in
 * practice (real programs rarely nest tagbody more than a few levels),
 * so they keep tighter limits to avoid bloating CL_Compiler size. */
#define CL_MAX_TAGBODIES 16
#define CL_MAX_OUTER_TAGS 64

/* Tail-trampoline frame kinds.  When compile_expr trampolines on a form
 * that has a tail child (e.g. LET's last body form, PROGN's last form),
 * it pushes one of these onto the compiler's tail stack so the postlude
 * can be emitted after the chain unwinds.  Without this, the recursive
 * compile_expr → compile_let → compile_progn → compile_expr call chain
 * grows one C frame per source-level LET, exhausting the C stack on
 * deeply nested macro expansions (e.g. serapeum's optimized
 * dispatch-case output). */
typedef enum {
    CL_TAIL_PROGN = 0,             /* no postlude needed; placeholder */
    CL_TAIL_LET,                   /* pop locals, OP_DYNUNBIND if any specials */
    CL_TAIL_BLOCK_LOCAL,           /* local-jump block: store result, patch exits */
    CL_TAIL_BLOCK_NLX,             /* NLX block: emit OP_BLOCK_POP, patch jumps */
    CL_TAIL_RETURN_FROM_LOCAL,     /* DYNUNBIND, store result, JMP w/ patch */
    CL_TAIL_RETURN_FROM_NLX,       /* OP_BLOCK_RETURN tag_idx */
    CL_TAIL_OUTER_RETURN_FROM,     /* cross-closure: OP_BLOCK_RETURN tag_idx */
    CL_TAIL_LOCALLY,               /* no postlude — pure body wrapper */
    CL_TAIL_SYMBOL_MACROLET,       /* restore env->symbol_macro_count */
    CL_TAIL_MACROLET,              /* restore env->local_macro_count */
    CL_TAIL_PROGV,                 /* emit OP_PROGV_UNBIND, restore in_tail */
    CL_TAIL_IF_AFTER_THEN,         /* emit JMP, patch jnil_pos; dispatch ELSE (or emit NIL) */
    CL_TAIL_IF_AFTER_ELSE,         /* patch the JMP that bypasses ELSE (block_push_pos) */
    CL_TAIL_HANDLER_BIND,          /* emit OP_HANDLER_POP <count> */
    CL_TAIL_MULTIPLE_VALUE_BIND,   /* clear_boxed + restore env->local_count, in_tail */
    CL_TAIL_FLET,                  /* clear_boxed + restore local_count + local_fun_count */
    CL_TAIL_LABELS,                /* same as FLET (separate kind for symmetry/debug) */
    CL_TAIL_EVAL_WHEN,             /* no postlude — pure body wrapper */
    CL_TAIL_PROGN_ITER             /* emit OP_POP, dispatch next body form */
} CL_TailKind;

typedef struct {
    uint8_t  kind;
    /* LET postlude state (also reused: saved_local_count for BLOCK_LOCAL,
     * saved_tail for RETURN_FROM_*, etc.) */
    int      saved_local_count;
    int      special_count;
    int      saved_tail;
    int      n_gc_roots;             /* CL_GC_PROTECT calls to undo on drain */
    /* BLOCK / RETURN-FROM state */
    int      saved_block_count;      /* index of CL_BlockInfo for BLOCK */
    int      block_push_pos;         /* OP_BLOCK_PUSH offset slot (NLX block) */
    int      tag_idx;                /* constant index for NLX tag */
    int      result_slot;            /* local slot storing block result */
    int      unwind_count;           /* DYNUNBIND count for RETURN-FROM */
    int      bi_index;               /* CL_BlockInfo index for RETURN-FROM */
    /* MACROLET / SYMBOL-MACROLET state */
    int      saved_macro_count;      /* env->local_macro_count (MACROLET)
                                      * or env->symbol_macro_count (SYMBOL_MACROLET) */
    /* Continuation form to dispatch when this postlude drains (used by
     * IF_AFTER_THEN to carry the ELSE form across THEN's compilation).
     * GC-traced via cl_compiler_gc_mark_thread's tail_stack walk.
     * CL_NIL when not used. */
    CL_Obj   cont_form;
} CL_TailFrame;

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
    uint8_t protect;  /* If set, cl_compiler_restore_to won't free this compiler */
    uint8_t code[CL_MAX_CODE_SIZE];
    CL_Obj constants[CL_MAX_CONSTANTS];
    int code_pos;
    int const_count;
    CL_CompEnv *env;
    int in_tail;      /* Are we in tail position? */
    int special_depth; /* Current number of active dynamic bindings */
    CL_BlockInfo blocks[CL_MAX_BLOCKS];
    int block_count;
    CL_TagbodyInfo tagbodies[CL_MAX_TAGBODIES];
    int tagbody_count;
    /* Source location tracking */
    CL_LineEntry line_entries[CL_MAX_LINE_ENTRIES];
    int line_entry_count;
    int current_line;   /* Current source line being compiled */
    /* Outer block names visible from enclosing scopes (for cross-closure return-from) */
    CL_Obj outer_blocks[CL_MAX_BLOCKS];
    int outer_block_count;
    /* Outer tagbody tags visible from enclosing scopes (for cross-closure go) */
    CL_OuterTagInfo outer_tags[CL_MAX_OUTER_TAGS];
    int outer_tag_count;
    /* Lambda compilation scratch (in struct to avoid stack overflow on AmigaOS) */
    CL_ParsedLambdaList ll;
    int key_slot_indices[CL_MAX_LOCALS];
    int key_suppliedp_indices[CL_MAX_LOCALS];
    CL_Obj param_vars[CL_MAX_LOCALS];
    int param_slots[CL_MAX_LOCALS];
    uint8_t lambda_needs_boxing[CL_MAX_LOCALS];
    /* Tail-trampoline stack — heap-allocated, grown on demand */
    CL_TailFrame *tail_stack;
    int tail_count;
    int tail_capacity;
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
/* Trampoline-aware variants: compile non-tail forms (with OP_POP between)
 * and return the body's last form so the caller can splice it into an
 * outer trampoline.  Returns CL_NIL for an empty body. */
CL_Obj compile_progn_tail(CL_Compiler *c, CL_Obj forms);
CL_Obj compile_body_tail(CL_Compiler *c, CL_Obj forms);
void compile_lambda(CL_Compiler *c, CL_Obj form);
int alloc_temp_slot(CL_CompEnv *env);
/* Push a fresh tail frame onto c->tail_stack (grows it lazily); caller
 * fills in the kind and per-kind state.  Aborts on alloc failure. */
CL_TailFrame *cl_tail_push(CL_Compiler *c);

/* compiler_special.c
 *
 * compile_block / compile_return_from / compile_return are tail-trampoline
 * preludes: they emit the form's setup bytecode, push a CL_TailFrame for
 * the postlude, and return the body / value form for compile_expr's
 * trampoline to continue with (or CL_NIL when the body is empty). */
CL_Obj compile_block(CL_Compiler *c, CL_Obj form);
CL_Obj compile_return_from(CL_Compiler *c, CL_Obj form);
CL_Obj compile_return(CL_Compiler *c, CL_Obj form);
/* Postlude emitter, dispatched from compile_expr's drain loop. */
void emit_block_or_return_postlude(CL_Compiler *c, CL_TailFrame *tf);
void compile_tagbody(CL_Compiler *c, CL_Obj form);
void compile_go(CL_Compiler *c, CL_Obj form);
void compile_catch(CL_Compiler *c, CL_Obj form);
void compile_unwind_protect(CL_Compiler *c, CL_Obj form);
void compile_dolist(CL_Compiler *c, CL_Obj form);
void compile_dotimes(CL_Compiler *c, CL_Obj form);
void compile_do(CL_Compiler *c, CL_Obj form);
void compile_do_star(CL_Compiler *c, CL_Obj form);
/* Trampoline-aware: prelude binds locals / pushes handlers, postlude
 * restores env / pops handlers.  Returns the body's tail form (or CL_NIL
 * for an empty body — caller emits OP_NIL). */
CL_Obj compile_flet(CL_Compiler *c, CL_Obj form);
CL_Obj compile_labels(CL_Compiler *c, CL_Obj form);
void compile_destructuring_bind(CL_Compiler *c, CL_Obj form);
CL_Obj compile_handler_bind(CL_Compiler *c, CL_Obj form);
void compile_restart_case(CL_Compiler *c, CL_Obj form);
/* Trampoline-aware: return tail body form, push postlude frame.  */
CL_Obj compile_macrolet(CL_Compiler *c, CL_Obj form);
CL_Obj compile_symbol_macrolet(CL_Compiler *c, CL_Obj form);
CL_Obj compile_progv(CL_Compiler *c, CL_Obj form);

/* compiler_extra.c */
void compile_and(CL_Compiler *c, CL_Obj form);
void compile_or(CL_Compiler *c, CL_Obj form);
void compile_cond(CL_Compiler *c, CL_Obj form);
void compile_quasiquote(CL_Compiler *c, CL_Obj form);
void compile_case(CL_Compiler *c, CL_Obj form, int error_if_no_match);
void compile_typecase(CL_Compiler *c, CL_Obj form, int error_if_no_match);
/* Trampoline-aware. */
CL_Obj compile_multiple_value_bind(CL_Compiler *c, CL_Obj form);
/* Trampoline-aware. */
CL_Obj compile_eval_when(CL_Compiler *c, CL_Obj form);
void compile_multiple_value_call(CL_Compiler *c, CL_Obj form);
void compile_multiple_value_list(CL_Compiler *c, CL_Obj form);
void compile_nth_value(CL_Compiler *c, CL_Obj form);
void compile_multiple_value_prog1(CL_Compiler *c, CL_Obj form);
void compile_load_time_value(CL_Compiler *c, CL_Obj form);
void compile_defsetf(CL_Compiler *c, CL_Obj form);
void compile_deftype(CL_Compiler *c, CL_Obj form);
void compile_defvar(CL_Compiler *c, CL_Obj form);
void compile_defparameter(CL_Compiler *c, CL_Obj form);
void compile_defconstant(CL_Compiler *c, CL_Obj form);
void compile_named_lambda(CL_Compiler *c, CL_Obj form);
void compile_defun(CL_Compiler *c, CL_Obj form);
void compile_defmacro(CL_Compiler *c, CL_Obj form);
CL_Obj defmacro_gensym(void);
int defmacro_is_ll_keyword(CL_Obj param);
int defmacro_needs_destructuring(CL_Obj ll);
void compile_declaim(CL_Compiler *c, CL_Obj form);
/* Trampoline-aware. */
CL_Obj compile_locally(CL_Compiler *c, CL_Obj form);
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
